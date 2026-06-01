#include "engine/scene/PscnLoader.hpp"

#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace engine
{

namespace
{

bool AddOverflowsRange(size_t offset, size_t size, size_t limit)
{
  return offset > limit || size > (limit - offset);
}

bool IsValidStringIndex(uint32_t index, uint16_t count)
{
  return index == PSCN_STRING_NONE || index < count;
}

} // namespace

bool PscnFile::Load(const std::string &path)
{
  Clear();

  if (!ReadWholeFile(path, &m_bytesStorage, &m_lastError))
  {
    return false;
  }

  if (m_bytesStorage.size() < sizeof(PscnHeader))
  {
    m_lastError = "PSCN file too small";
    return false;
  }

  m_bytes = m_bytesStorage.data();
  m_size = m_bytesStorage.size();
  m_header = reinterpret_cast<const PscnHeader *>(m_bytes);

  if (!ValidateHeader())
  {
    const std::string error = m_lastError;
    Clear();
    m_lastError = error;
    return false;
  }

  if (!ResolveSections())
  {
    const std::string error = m_lastError;
    Clear();
    m_lastError = error;
    return false;
  }

  if (!ValidateRanges())
  {
    const std::string error = m_lastError;
    Clear();
    m_lastError = error;
    return false;
  }

  return true;
}

void PscnFile::Clear()
{
  m_bytesStorage.clear();
  m_bytes = nullptr;
  m_size = 0;

  m_header = nullptr;
  m_resolvedNodeCount = 0;
  std::memset(m_nodeOffsets, 0, sizeof(m_nodeOffsets));

  m_tilesets = nullptr;
  m_chunks = nullptr;
  m_chunkData = nullptr;
  m_strings = nullptr;
  m_stringData = nullptr;

  m_lastError.clear();
}

uint16_t PscnFile::GetNodeCount() const
{
  return m_header ? m_header->nodeCount : 0;
}

uint16_t PscnFile::GetTilesetCount() const
{
  return m_header ? m_header->tilesetCount : 0;
}

uint16_t PscnFile::GetChunkCount() const
{
  return m_header ? m_header->chunkCount : 0;
}

uint16_t PscnFile::GetStringCount() const
{
  return m_header ? m_header->stringCount : 0;
}

const PscnNodeBase *PscnFile::GetNodeBase(uint32_t index) const
{
  if (!m_header || !m_bytes || index >= m_resolvedNodeCount)
  {
    return nullptr;
  }

  return reinterpret_cast<const PscnNodeBase *>(m_bytes + m_nodeOffsets[index]);
}

const void *PscnFile::GetNodeExtension(uint32_t index) const
{
  const PscnNodeBase *base = GetNodeBase(index);
  if (!base || base->extSize == 0)
  {
    return nullptr;
  }

  return reinterpret_cast<const uint8_t *>(base) + sizeof(PscnNodeBase);
}

const PscnTilesetDef *PscnFile::GetTileset(uint32_t index) const
{
  if (!m_tilesets || !m_header || index >= m_header->tilesetCount)
  {
    return nullptr;
  }

  return &m_tilesets[index];
}

const PscnChunkDef *PscnFile::GetChunk(uint32_t index) const
{
  if (!m_chunks || !m_header || index >= m_header->chunkCount)
  {
    return nullptr;
  }

  return &m_chunks[index];
}

const PscnTileCell *PscnFile::GetChunkTiles(const PscnChunkDef &chunk) const
{
  if (!m_chunkData || !m_header)
  {
    return nullptr;
  }

  const size_t chunkDataBytes = m_size - static_cast<size_t>(m_header->chunkDataOffset);
  const size_t tileBytes = static_cast<size_t>(chunk.tileCount) * sizeof(PscnTileCell);
  if (AddOverflowsRange(chunk.tileDataOffset, tileBytes, chunkDataBytes))
  {
    return nullptr;
  }

  return reinterpret_cast<const PscnTileCell *>(m_chunkData + chunk.tileDataOffset);
}

const char *PscnFile::GetString(uint32_t index) const
{
  if (index == PSCN_STRING_NONE)
  {
    return nullptr;
  }

  if (!m_strings || !m_stringData || !m_header || index >= m_header->stringCount)
  {
    return nullptr;
  }

  const PscnStringEntry &entry = m_strings[index];
  return m_stringData + entry.offset;
}

bool PscnFile::GetAtlasSpriteIdForTile(const PscnTilesetDef &tileset,
                                       uint32_t tileId,
                                       uint32_t *outSpriteId) const
{
  if (!m_header || !m_bytes || !outSpriteId)
  {
    return false;
  }

  if (tileId < tileset.firstTileId ||
      tileId >= (tileset.firstTileId + tileset.tileCount))
  {
    return false;
  }

  const uint32_t tileOffset = tileId - tileset.firstTileId;

  const size_t remapOffset =
      static_cast<size_t>(tileset.remapTableOffset) +
      static_cast<size_t>(tileOffset) * sizeof(uint32_t);
  if (AddOverflowsRange(remapOffset, sizeof(uint32_t), m_size))
  {
    return false;
  }

  const uint32_t *remapEntry =
      reinterpret_cast<const uint32_t *>(m_bytes + remapOffset);
  *outSpriteId = *remapEntry;
  return true;
}

bool PscnFile::ReadWholeFile(const std::string &path,
                             std::vector<uint8_t> *outBytes,
                             std::string *outError)
{
  outBytes->clear();

  const int fd = open(path.c_str(), O_RDONLY);
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

bool PscnFile::ValidateHeader()
{
  if (!m_header)
  {
    m_lastError = "PSCN header not resolved";
    return false;
  }

  if (m_header->magic != PSCN_MAGIC)
  {
    m_lastError = "Invalid PSCN magic";
    return false;
  }

  if (m_header->versionMajor != PSCN_VERSION_MAJOR)
  {
    m_lastError = "Unsupported PSCN major version";
    return false;
  }

  if (m_header->versionMinor > PSCN_VERSION_MINOR)
  {
    m_lastError = "Unsupported PSCN minor version";
    return false;
  }

  if (m_header->fileSize != m_size)
  {
    m_lastError = "PSCN fileSize mismatch";
    return false;
  }

  if (m_header->nodeCount == 0)
  {
    m_lastError = "PSCN has no nodes";
    return false;
  }

  if (m_header->nodeCount > PSCN_MAX_NODES)
  {
    m_lastError = "PSCN node count exceeds maximum";
    return false;
  }

  return true;
}

bool PscnFile::ResolveSections()
{
  if (!m_header || !m_bytes)
  {
    m_lastError = "PSCN header not resolved";
    return false;
  }

  // Build node offset table by walking variable-size nodes
  size_t cursor = static_cast<size_t>(m_header->nodeTableOffset);
  for (uint16_t i = 0; i < m_header->nodeCount; ++i)
  {
    if (AddOverflowsRange(cursor, sizeof(PscnNodeBase), m_size))
    {
      m_lastError = "Node table extends past file boundary";
      return false;
    }

    m_nodeOffsets[i] = static_cast<uint32_t>(cursor);

    const PscnNodeBase *base =
        reinterpret_cast<const PscnNodeBase *>(m_bytes + cursor);
    const size_t nodeSize = sizeof(PscnNodeBase) + static_cast<size_t>(base->extSize);

    if (AddOverflowsRange(cursor, nodeSize, m_size))
    {
      m_lastError = "Node extension extends past file boundary";
      return false;
    }

    cursor += nodeSize;
  }
  m_resolvedNodeCount = m_header->nodeCount;

  // Tilesets
  const size_t tilesetsSize =
      static_cast<size_t>(m_header->tilesetCount) * sizeof(PscnTilesetDef);
  if (AddOverflowsRange(m_header->tilesetTableOffset, tilesetsSize, m_size))
  {
    m_lastError = "Tileset table out of range";
    return false;
  }
  m_tilesets = (tilesetsSize > 0)
                   ? reinterpret_cast<const PscnTilesetDef *>(
                         m_bytes + m_header->tilesetTableOffset)
                   : nullptr;

  // Chunks
  const size_t chunksSize =
      static_cast<size_t>(m_header->chunkCount) * sizeof(PscnChunkDef);
  if (AddOverflowsRange(m_header->chunkTableOffset, chunksSize, m_size))
  {
    m_lastError = "Chunk table out of range";
    return false;
  }
  m_chunks = (chunksSize > 0)
                 ? reinterpret_cast<const PscnChunkDef *>(
                       m_bytes + m_header->chunkTableOffset)
                 : nullptr;

  // Chunk data
  if (m_header->chunkDataOffset > m_size)
  {
    m_lastError = "Chunk data offset out of range";
    return false;
  }
  m_chunkData = m_bytes + m_header->chunkDataOffset;

  // Strings
  const size_t stringsSize =
      static_cast<size_t>(m_header->stringCount) * sizeof(PscnStringEntry);
  if (AddOverflowsRange(m_header->stringTableOffset, stringsSize, m_size))
  {
    m_lastError = "String table out of range";
    return false;
  }
  m_strings = (stringsSize > 0)
                  ? reinterpret_cast<const PscnStringEntry *>(
                        m_bytes + m_header->stringTableOffset)
                  : nullptr;

  if (m_header->stringDataOffset > m_size)
  {
    m_lastError = "String data offset out of range";
    return false;
  }
  m_stringData = reinterpret_cast<const char *>(m_bytes + m_header->stringDataOffset);

  return true;
}

bool PscnFile::ValidateRanges()
{
  if (!m_header)
  {
    m_lastError = "PSCN header not resolved";
    return false;
  }

  // Validate nodes
  for (uint16_t i = 0; i < m_resolvedNodeCount; ++i)
  {
    const PscnNodeBase *base = GetNodeBase(i);
    if (!base)
    {
      m_lastError = "Failed to read node base";
      return false;
    }

    if (i == 0)
    {
      if (base->parentIndex != -1)
      {
        m_lastError = "Root node must have parentIndex -1";
        return false;
      }
      if (base->nodeType != NODE_ROOT)
      {
        m_lastError = "First node must be Root type";
        return false;
      }
    }
    else
    {
      if (base->parentIndex < 0 ||
          static_cast<uint32_t>(base->parentIndex) >= i)
      {
        m_lastError = "Node parentIndex out of pre-order range";
        return false;
      }
    }

    if (base->childCount > 0)
    {
      if (base->firstChildIndex >= m_resolvedNodeCount ||
          base->childCount > (m_resolvedNodeCount - base->firstChildIndex))
      {
        m_lastError = "Node child range out of bounds";
        return false;
      }
    }

    if (!IsValidStringIndex(base->scriptIdStringIndex, m_header->stringCount))
    {
      m_lastError = "Node scriptIdStringIndex out of range";
      return false;
    }

    if (!IsValidStringIndex(base->scriptDataStringIndex, m_header->stringCount))
    {
      m_lastError = "Node scriptDataStringIndex out of range";
      return false;
    }

    // Validate extension sizes
    switch (base->nodeType)
    {
    case NODE_ROOT:
    case NODE_NODE2D:
      if (base->extSize != 0)
      {
        m_lastError = "Root/Node2D should have zero extSize";
        return false;
      }
      break;
    case NODE_SPRITE:
      if (base->extSize != sizeof(PscnSpriteExt))
      {
        m_lastError = "Sprite extSize mismatch";
        return false;
      }
      break;
    case NODE_ANIMATED_SPRITE:
    {
      if (base->extSize < sizeof(PscnAnimatedSpriteExt))
      {
        m_lastError = "AnimatedSprite extSize too small";
        return false;
      }
      const PscnAnimatedSpriteExt *animExt = reinterpret_cast<const PscnAnimatedSpriteExt *>(
          reinterpret_cast<const uint8_t *>(base) + sizeof(PscnNodeBase));
      const uint16_t expectedSize =
          static_cast<uint16_t>(sizeof(PscnAnimatedSpriteExt) +
                                static_cast<size_t>(animExt->animCount) * sizeof(uint32_t));
      if (base->extSize != expectedSize)
      {
        m_lastError = "AnimatedSprite extSize does not match animCount";
        return false;
      }
      break;
    }
    case NODE_TILEMAP:
    {
      if (base->extSize != sizeof(PscnTileMapExt))
      {
        m_lastError = "TileMap extSize mismatch";
        return false;
      }
      const PscnTileMapExt *tmExt = reinterpret_cast<const PscnTileMapExt *>(
          reinterpret_cast<const uint8_t *>(base) + sizeof(PscnNodeBase));
      if (tmExt->chunkWidthTiles == 0 || tmExt->chunkHeightTiles == 0)
      {
        m_lastError = "TileMap has zero chunk dimensions";
        return false;
      }
      if (tmExt->tileWidth == 0 || tmExt->tileHeight == 0)
      {
        m_lastError = "TileMap has zero tile dimensions";
        return false;
      }
      if (tmExt->chunkCount > 0)
      {
        if (tmExt->firstChunkIndex >= m_header->chunkCount ||
            tmExt->chunkCount > (m_header->chunkCount - tmExt->firstChunkIndex))
        {
          m_lastError = "TileMap chunk range out of bounds";
          return false;
        }
      }
      break;
    }
    case NODE_COLLISION_SHAPE:
      if (base->extSize != sizeof(PscnCollisionShapeExt))
      {
        m_lastError = "CollisionShape extSize mismatch";
        return false;
      }
      break;
    case NODE_AREA:
      if (base->extSize != sizeof(PscnAreaExt))
      {
        m_lastError = "Area extSize mismatch";
        return false;
      }
      break;
    case NODE_LIGHT2D:
      if (base->extSize != sizeof(PscnLight2DExt))
      {
        m_lastError = "Light2D extSize mismatch";
        return false;
      }
      break;
    case NODE_CAMERA2D:
      if (base->extSize != sizeof(PscnCamera2DExt))
      {
        m_lastError = "Camera2D extSize mismatch";
        return false;
      }
      break;
    case NODE_SPAWNER:
      if (base->extSize != sizeof(PscnSpawnerExt))
      {
        m_lastError = "Spawner extSize mismatch";
        return false;
      }
      break;
    case NODE_PATH2D:
    {
      if (base->extSize < sizeof(PscnPath2DExt))
      {
        m_lastError = "Path2D extSize too small";
        return false;
      }
      const PscnPath2DExt *pathExt = reinterpret_cast<const PscnPath2DExt *>(
          reinterpret_cast<const uint8_t *>(base) + sizeof(PscnNodeBase));
      const uint16_t expected = static_cast<uint16_t>(
          sizeof(PscnPath2DExt) + static_cast<size_t>(pathExt->pointCount) * 8u);
      if (base->extSize != expected)
      {
        m_lastError = "Path2D extSize does not match pointCount";
        return false;
      }
      break;
    }
    case NODE_PATH_FOLLOW2D:
      if (base->extSize != sizeof(PscnPathFollow2DExt))
      {
        m_lastError = "PathFollow2D extSize mismatch";
        return false;
      }
      break;
    case NODE_TIMER:
      if (base->extSize != sizeof(PscnTimerExt))
      {
        m_lastError = "Timer extSize mismatch";
        return false;
      }
      break;
    case NODE_DECAL:
      if (base->extSize != sizeof(PscnDecalExt))
      {
        m_lastError = "Decal extSize mismatch";
        return false;
      }
      break;
    case NODE_VISIBILITY_NOTIFIER:
      if (base->extSize != sizeof(PscnVisibilityNotifierExt))
      {
        m_lastError = "VisibilityNotifier extSize mismatch";
        return false;
      }
      break;
    case NODE_NAV_REGION2D:
    {
      if (base->extSize < sizeof(PscnNavRegion2DExt))
      {
        m_lastError = "NavRegion2D extSize too small";
        return false;
      }
      const PscnNavRegion2DExt *navExt = reinterpret_cast<const PscnNavRegion2DExt *>(
          reinterpret_cast<const uint8_t *>(base) + sizeof(PscnNodeBase));
      if (navExt->pointCount < 3)
      {
        m_lastError = "NavRegion2D pointCount must be >= 3";
        return false;
      }
      const uint16_t expected = static_cast<uint16_t>(
          sizeof(PscnNavRegion2DExt) + static_cast<size_t>(navExt->pointCount) * 8u);
      if (base->extSize != expected)
      {
        m_lastError = "NavRegion2D extSize does not match pointCount";
        return false;
      }
      break;
    }
    default:
      m_lastError = "Unknown node type";
      return false;
    }
  }

  // Validate tilesets
  for (uint32_t i = 0; i < m_header->tilesetCount; ++i)
  {
    const PscnTilesetDef &tileset = m_tilesets[i];
    if (tileset.tileWidth == 0 || tileset.tileHeight == 0 || tileset.columns == 0)
    {
      m_lastError = "Invalid tileset dimensions";
      return false;
    }

    if (tileset.tileCount > 0 &&
        tileset.firstTileId > UINT32_MAX - tileset.tileCount)
    {
      m_lastError = "Tileset firstTileId+tileCount overflows";
      return false;
    }

    const size_t remapBytes =
        static_cast<size_t>(tileset.tileCount) * sizeof(uint32_t);
    if (AddOverflowsRange(tileset.remapTableOffset, remapBytes, m_size))
    {
      m_lastError = "Tileset remap table out of range";
      return false;
    }
  }

  // Validate chunks
  const size_t chunkDataBytes = m_size - static_cast<size_t>(m_header->chunkDataOffset);

  for (uint32_t i = 0; i < m_header->chunkCount; ++i)
  {
    const PscnChunkDef &chunk = m_chunks[i];

    if (chunk.nodeIndex >= m_resolvedNodeCount)
    {
      m_lastError = "Chunk references invalid node";
      return false;
    }

    const PscnNodeBase *node = GetNodeBase(chunk.nodeIndex);
    if (!node || node->nodeType != NODE_TILEMAP)
    {
      m_lastError = "Chunk references non-TileMap node";
      return false;
    }

    if (chunk.usedTileCount > chunk.tileCount)
    {
      m_lastError = "Chunk usedTileCount exceeds tileCount";
      return false;
    }

    const size_t tileBytes = static_cast<size_t>(chunk.tileCount) * sizeof(PscnTileCell);
    if (AddOverflowsRange(chunk.tileDataOffset, tileBytes, chunkDataBytes))
    {
      m_lastError = "Chunk tile data out of range";
      return false;
    }
  }

  // Validate strings
  const size_t stringDataBytes = m_size - static_cast<size_t>(m_header->stringDataOffset);

  for (uint32_t i = 0; i < m_header->stringCount; ++i)
  {
    const PscnStringEntry &entry = m_strings[i];
    if (entry.offset > stringDataBytes)
    {
      m_lastError = "String entry offset out of range";
      return false;
    }

    if (entry.length >= (stringDataBytes - entry.offset))
    {
      m_lastError = "String entry length out of range";
      return false;
    }

    const char *str = m_stringData + entry.offset;
    if (str[entry.length] != '\0')
    {
      m_lastError = "String entry is not null-terminated";
      return false;
    }
  }

  return true;
}

} // namespace engine
