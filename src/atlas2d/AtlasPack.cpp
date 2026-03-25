#include "atlas2d/AtlasPack.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace atlas2d
{

namespace
{

bool AddOverflowsRange(uint32_t offset, uint32_t size, uint32_t limit)
{
  return offset > limit || size > (limit - offset);
}

} // namespace

bool AtlasPack::Load(const std::string &metaPath, const std::string &atlasPath)
{
  Clear();

  if (!ReadWholeFile(metaPath, &m_metaBytes, &m_lastError))
  {
    return false;
  }

  if (!ReadWholeFile(atlasPath, &m_atlasBytes, &m_lastError))
  {
    return false;
  }

  if (m_metaBytes.size() < sizeof(AtlasHeader))
  {
    m_lastError = "Meta file too small";
    return false;
  }

  m_header = reinterpret_cast<const AtlasHeader *>(m_metaBytes.data());

  if (!ValidateHeader())
  {
    return false;
  }

  if (!ResolveTables())
  {
    return false;
  }

  if (!ValidatePages())
  {
    return false;
  }

  if (!ValidateHashTable())
  {
    return false;
  }

  return true;
}

void AtlasPack::Clear()
{
  m_metaBytes.clear();
  m_atlasBytes.clear();

  m_header = nullptr;
  m_pages = nullptr;
  m_sprites = nullptr;
  m_hashes = nullptr;

  m_hashCount = 0;
  m_lastError.clear();
}

const std::string &AtlasPack::GetLastError() const
{
  return m_lastError;
}

const AtlasHeader *AtlasPack::GetHeader() const
{
  return m_header;
}

const AtlasPage *AtlasPack::GetPages() const
{
  return m_pages;
}

const AtlasSprite *AtlasPack::GetSprites() const
{
  return m_sprites;
}

uint16_t AtlasPack::GetPageCount() const
{
  return m_header ? m_header->pageCount : 0;
}

uint16_t AtlasPack::GetSpriteCount() const
{
  return m_header ? m_header->spriteCount : 0;
}

const AtlasSprite *AtlasPack::GetSpriteByIndex(uint32_t index) const
{
  if (!m_sprites || !m_header || index >= m_header->spriteCount)
  {
    return nullptr;
  }

  return &m_sprites[index];
}

const AtlasSprite *AtlasPack::FindSpriteById(uint32_t id) const
{
  if (!m_sprites || !m_header)
  {
    return nullptr;
  }

  for (uint32_t i = 0; i < m_header->spriteCount; ++i)
  {
    if (m_sprites[i].id == id)
    {
      return &m_sprites[i];
    }
  }

  return nullptr;
}

const AtlasSprite *AtlasPack::FindSpriteByHash(uint32_t hash) const
{
  if (!m_hashes || m_hashCount == 0 || !m_sprites || !m_header)
  {
    return nullptr;
  }

  uint32_t left = 0;
  uint32_t right = m_hashCount;

  while (left < right)
  {
    const uint32_t mid = left + (right - left) / 2;
    const AtlasHashEntry &entry = m_hashes[mid];

    if (entry.nameHash == hash)
    {
      if (entry.spriteIndex < m_header->spriteCount)
      {
        return &m_sprites[entry.spriteIndex];
      }
      return nullptr;
    }

    if (hash < entry.nameHash)
    {
      right = mid;
    }
    else
    {
      left = mid + 1;
    }
  }

  return nullptr;
}

AtlasImageView AtlasPack::GetPageImage(uint32_t pageIndex) const
{
  AtlasImageView view = {};

  if (!m_pages || !m_header || pageIndex >= m_header->pageCount)
  {
    return view;
  }

  const AtlasPage &page = m_pages[pageIndex];
  const uint32_t atlasSize = static_cast<uint32_t>(m_atlasBytes.size());
  if (AddOverflowsRange(page.dataOffset, page.dataSize, atlasSize))
  {
    return view;
  }

  view.pixels = m_atlasBytes.data() + page.dataOffset;
  view.sizeBytes = page.dataSize;
  view.width = page.width;
  view.height = page.height;
  view.format = static_cast<PageFormat>(page.format);
  return view;
}

// Returns normalized [0,1] UVs. Note: gsKit expects pixel-space texcoords,
// so use BuildAtlasQuad (in AtlasPackUtils) for rendering, not this.
SpriteUVRect AtlasPack::ComputeUVs(const AtlasSprite &sprite) const
{
  SpriteUVRect uv = {};

  if (!m_pages || !m_header || sprite.pageIndex >= m_header->pageCount)
  {
    return uv;
  }

  const AtlasPage &page = m_pages[sprite.pageIndex];
  if (page.width == 0 || page.height == 0)
  {
    return uv;
  }

  const float invW = 1.0f / static_cast<float>(page.width);
  const float invH = 1.0f / static_cast<float>(page.height);

  uv.u0 = static_cast<float>(sprite.x) * invW;
  uv.v0 = static_cast<float>(sprite.y) * invH;
  uv.u1 = static_cast<float>(sprite.x + sprite.w) * invW;
  uv.v1 = static_cast<float>(sprite.y + sprite.h) * invH;

  return uv;
}

bool AtlasPack::ReadWholeFile(const std::string &path,
                              std::vector<uint8_t> *outBytes,
                              std::string *outError)
{
  outBytes->clear();

  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0)
  {
    *outError = "Failed to open file: " + path;
    return false;
  }

  struct stat st;
  if (fstat(fd, &st) == 0 && st.st_size > 0)
  {
    outBytes->reserve(static_cast<size_t>(st.st_size));
  }

  uint8_t chunk[4096];
  for (;;)
  {
    const int bytesRead = read(fd, chunk, sizeof(chunk));
    if (bytesRead < 0)
    {
      close(fd);
      outBytes->clear();
      *outError = "Failed to read file: " + path;
      return false;
    }

    if (bytesRead == 0)
    {
      break;
    }

    outBytes->insert(outBytes->end(), chunk, chunk + bytesRead);
  }

  close(fd);
  return true;
}

bool AtlasPack::ValidateHeader()
{
  if (!m_header)
  {
    m_lastError = "Header not resolved";
    return false;
  }

  if (m_header->magic != ATLAS_MAGIC)
  {
    m_lastError = "Invalid atlas magic";
    return false;
  }

  if (m_header->versionMajor != 1)
  {
    m_lastError = "Unsupported atlas major version";
    return false;
  }

  if (m_header->fileSize != static_cast<uint32_t>(m_metaBytes.size()))
  {
    m_lastError = "Meta fileSize mismatch";
    return false;
  }

  const uint32_t metaSize = static_cast<uint32_t>(m_metaBytes.size());
  if (m_header->pageTableOffset >= metaSize ||
      m_header->spriteTableOffset >= metaSize)
  {
    m_lastError = "Table offset out of range";
    return false;
  }

  return true;
}

bool AtlasPack::ResolveTables()
{
  if (!m_header)
  {
    m_lastError = "Header not resolved";
    return false;
  }

  const uint32_t metaSize = static_cast<uint32_t>(m_metaBytes.size());
  const uint32_t pagesSize =
      static_cast<uint32_t>(m_header->pageCount) * sizeof(AtlasPage);
  const uint32_t spritesSize =
      static_cast<uint32_t>(m_header->spriteCount) * sizeof(AtlasSprite);

  if (m_header->pageTableOffset % 4 != 0)
  {
    m_lastError = "Page table offset not 4-byte aligned";
    return false;
  }

  if (m_header->spriteTableOffset % 4 != 0)
  {
    m_lastError = "Sprite table offset not 4-byte aligned";
    return false;
  }

  if (AddOverflowsRange(m_header->pageTableOffset, pagesSize, metaSize))
  {
    m_lastError = "Page table out of range";
    return false;
  }

  if (AddOverflowsRange(m_header->spriteTableOffset, spritesSize, metaSize))
  {
    m_lastError = "Sprite table out of range";
    return false;
  }

  m_pages = reinterpret_cast<const AtlasPage *>(
      m_metaBytes.data() + m_header->pageTableOffset);
  m_sprites = reinterpret_cast<const AtlasSprite *>(
      m_metaBytes.data() + m_header->spriteTableOffset);

  if (m_header->hashTableOffset != 0)
  {
    if (m_header->hashTableOffset % 4 != 0)
    {
      m_lastError = "Hash table offset not 4-byte aligned";
      return false;
    }

    if (m_header->hashTableOffset >= metaSize)
    {
      m_lastError = "Hash table offset out of range";
      return false;
    }

    const uint32_t remaining = metaSize - m_header->hashTableOffset;
    m_hashCount = remaining / sizeof(AtlasHashEntry);
    m_hashes = reinterpret_cast<const AtlasHashEntry *>(
        m_metaBytes.data() + m_header->hashTableOffset);
  }

  return true;
}

bool AtlasPack::ValidatePages()
{
  if (!m_pages || !m_sprites || !m_header)
  {
    m_lastError = "Tables not resolved";
    return false;
  }

  const uint32_t atlasSize = static_cast<uint32_t>(m_atlasBytes.size());

  for (uint32_t i = 0; i < m_header->pageCount; ++i)
  {
    const AtlasPage &page = m_pages[i];

    if (page.width == 0 || page.height == 0)
    {
      m_lastError = "Invalid page dimensions";
      return false;
    }

    if (AddOverflowsRange(page.dataOffset, page.dataSize, atlasSize))
    {
      m_lastError = "Atlas page data out of range";
      return false;
    }

    switch (static_cast<PageFormat>(page.format))
    {
    case PageFormat::RGBA32:
    {
      const uint32_t expected =
          static_cast<uint32_t>(page.width) *
          static_cast<uint32_t>(page.height) * 4u;
      if (page.dataSize < expected)
      {
        m_lastError = "RGBA32 page data smaller than expected";
        return false;
      }
      break;
    }
    case PageFormat::IDX8:
    case PageFormat::IDX4:
      break;
    default:
      m_lastError = "Unsupported page format";
      return false;
    }
  }

  for (uint32_t i = 0; i < m_header->spriteCount; ++i)
  {
    const AtlasSprite &sprite = m_sprites[i];

    if (sprite.pageIndex >= m_header->pageCount)
    {
      m_lastError = "Sprite references invalid page";
      return false;
    }

    const AtlasPage &page = m_pages[sprite.pageIndex];
    const uint32_t x1 = static_cast<uint32_t>(sprite.x);
    const uint32_t y1 = static_cast<uint32_t>(sprite.y);
    const uint32_t x2 = x1 + static_cast<uint32_t>(sprite.w);
    const uint32_t y2 = y1 + static_cast<uint32_t>(sprite.h);

    if (x2 > page.width || y2 > page.height)
    {
      m_lastError = "Sprite rect out of page bounds";
      return false;
    }
  }

  return true;
}

bool AtlasPack::ValidateHashTable()
{
  if (!m_hashes || m_hashCount == 0 || !m_header)
  {
    return true;
  }

  for (uint32_t i = 0; i < m_hashCount; ++i)
  {
    if (m_hashes[i].spriteIndex >= m_header->spriteCount)
    {
      m_lastError = "Hash table sprite index out of range";
      return false;
    }

    if (i > 0)
    {
      if (m_hashes[i - 1].nameHash > m_hashes[i].nameHash)
      {
        m_lastError = "Hash table not sorted";
        return false;
      }

      if (m_hashes[i - 1].nameHash == m_hashes[i].nameHash)
      {
        m_lastError = "Duplicate name hash in hash table";
        return false;
      }
    }
  }

  return true;
}

uint32_t FNV1a32(const char *str)
{
  uint32_t hash = 2166136261u;

  while (*str)
  {
    hash ^= static_cast<uint8_t>(*str);
    hash *= 16777619u;
    ++str;
  }

  return hash;
}

} // namespace atlas2d
