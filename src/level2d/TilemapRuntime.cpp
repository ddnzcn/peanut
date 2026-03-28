#include "level2d/TilemapRuntime.hpp"

#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace level2d
{

namespace
{

bool AddOverflowsRange(size_t offset, size_t size, size_t limit)
{
  return offset > limit || size > (limit - offset);
}

int FloorDiv(int value, int divisor)
{
  if (divisor <= 0)
  {
    return 0;
  }

  int quotient = value / divisor;
  const int remainder = value % divisor;
  if (remainder != 0 && value < 0)
  {
    --quotient;
  }

  return quotient;
}

bool IsValidStringIndex(uint32_t index, uint16_t count)
{
  return index == INVALID_STRING_INDEX || index < count;
}

} // namespace

bool Tilemap::Load(const std::string &path)
{
  Clear();

  if (!ReadWholeFile(path, &m_bytesStorage, &m_lastError))
  {
    return false;
  }

  if (m_bytesStorage.size() < sizeof(TilemapHeader))
  {
    m_lastError = "Tilemap file too small";
    return false;
  }

  m_bytes = m_bytesStorage.data();
  m_size = m_bytesStorage.size();
  m_header = reinterpret_cast<const TilemapHeader *>(m_bytes);

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

void Tilemap::Clear()
{
  m_bytesStorage.clear();
  m_bytes = nullptr;
  m_size = 0;

  m_header = nullptr;
  m_tilesets = nullptr;
  m_layers = nullptr;
  m_chunks = nullptr;
  m_chunkData = nullptr;
  m_collisions = nullptr;
  m_markers = nullptr;
  m_strings = nullptr;
  m_stringData = nullptr;
  m_usesTilesetRemapTable = false;

  m_lastError.clear();
}

bool Tilemap::ReadWholeFile(const std::string &path,
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

uint16_t Tilemap::GetTilesetCount() const
{
  return m_header ? m_header->tilesetCount : 0;
}

uint16_t Tilemap::GetLayerCount() const
{
  return m_header ? m_header->layerCount : 0;
}

uint16_t Tilemap::GetChunkCount() const
{
  return m_header ? m_header->chunkCount : 0;
}

uint16_t Tilemap::GetCollisionCount() const
{
  return m_header ? m_header->collisionCount : 0;
}

uint16_t Tilemap::GetMarkerCount() const
{
  return m_header ? m_header->markerCount : 0;
}

uint16_t Tilemap::GetStringCount() const
{
  return m_header ? m_header->stringCount : 0;
}

const TilesetDef *Tilemap::GetTilesetByIndex(uint32_t index) const
{
  if (!m_tilesets || !m_header || index >= m_header->tilesetCount)
  {
    return nullptr;
  }

  return &m_tilesets[index];
}

const LayerDef *Tilemap::GetLayerByIndex(uint32_t index) const
{
  if (!m_layers || !m_header || index >= m_header->layerCount)
  {
    return nullptr;
  }

  return &m_layers[index];
}

const CollisionArea *Tilemap::GetCollisionByIndex(uint32_t index) const
{
  if (!m_collisions || !m_header || index >= m_header->collisionCount)
  {
    return nullptr;
  }

  return &m_collisions[index];
}

const Marker *Tilemap::GetMarkerByIndex(uint32_t index) const
{
  if (!m_markers || !m_header || index >= m_header->markerCount)
  {
    return nullptr;
  }

  return &m_markers[index];
}

bool Tilemap::GetAtlasSpriteIdForTile(const TilesetDef &tileset,
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
  if (!m_usesTilesetRemapTable)
  {
    *outSpriteId = tileset.atlasSpriteRemapOffset + tileOffset;
    return true;
  }

  const size_t remapOffset =
      static_cast<size_t>(tileset.atlasSpriteRemapOffset) +
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

const char *Tilemap::GetStringByIndex(uint32_t index) const
{
  if (!m_strings || !m_stringData || !m_header || index >= m_header->stringCount)
  {
    return nullptr;
  }

  const StringEntry &entry = m_strings[index];
  return m_stringData + entry.offset;
}

bool Tilemap::GetChunkView(uint32_t chunkIndex, TileChunkView *outView) const
{
  if (!outView)
  {
    return false;
  }

  outView->hasChunk = false;
  outView->chunk = {};
  outView->tiles = nullptr;

  if (!m_chunks || !m_chunkData || !m_header || chunkIndex >= m_header->chunkCount)
  {
    return false;
  }

  const ChunkDef &chunk = m_chunks[chunkIndex];
  std::memcpy(&outView->chunk, &chunk, sizeof(ChunkDef));
  outView->hasChunk = true;
  outView->tiles =
      reinterpret_cast<const TileEntry *>(m_chunkData + outView->chunk.tileDataOffset);
  return true;
}

bool Tilemap::GetLayerView(uint32_t layerIndex, LayerView *outView) const
{
  if (!outView)
  {
    return false;
  }

  outView->layer = nullptr;
  outView->chunks = nullptr;
  outView->collisions = nullptr;
  outView->markers = nullptr;

  const LayerDef *layer = GetLayerByIndex(layerIndex);
  if (!layer)
  {
    return false;
  }

  outView->layer = layer;
  outView->chunks = (layer->chunkCount > 0) ? (m_chunks + layer->firstChunkIndex) : nullptr;
  outView->collisions =
      (layer->collisionCount > 0) ? (m_collisions + layer->firstCollisionIndex) : nullptr;
  outView->markers = (layer->markerCount > 0) ? (m_markers + layer->firstMarkerIndex) : nullptr;
  return true;
}

bool Tilemap::ComputeVisibleChunkRange(const LayerDef &layer,
                                       int cameraX,
                                       int cameraY,
                                       int viewW,
                                       int viewH,
                                       int *outChunkMinX,
                                       int *outChunkMinY,
                                       int *outChunkMaxX,
                                       int *outChunkMaxY) const
{
  if (!m_header || !outChunkMinX || !outChunkMinY || !outChunkMaxX || !outChunkMaxY)
  {
    return false;
  }

  if (viewW <= 0 || viewH <= 0 || layer.chunkCols == 0 || layer.chunkRows == 0)
  {
    return false;
  }

  const int chunkPixelW =
      static_cast<int>(m_header->chunkWidthTiles) * static_cast<int>(m_header->tileWidth);
  const int chunkPixelH =
      static_cast<int>(m_header->chunkHeightTiles) * static_cast<int>(m_header->tileHeight);

  if (chunkPixelW <= 0 || chunkPixelH <= 0)
  {
    return false;
  }

  const int parallaxX = static_cast<int>(layer.parallaxX_8_8);
  const int parallaxY = static_cast<int>(layer.parallaxY_8_8);

  const int visibleMinX = FloorDiv(cameraX * parallaxX, 256) - static_cast<int>(layer.offsetX);
  const int visibleMinY = FloorDiv(cameraY * parallaxY, 256) - static_cast<int>(layer.offsetY);
  const int visibleMaxX = visibleMinX + viewW - 1;
  const int visibleMaxY = visibleMinY + viewH - 1;

  int chunkMinX = FloorDiv(visibleMinX, chunkPixelW);
  int chunkMinY = FloorDiv(visibleMinY, chunkPixelH);
  int chunkMaxX = FloorDiv(visibleMaxX, chunkPixelW);
  int chunkMaxY = FloorDiv(visibleMaxY, chunkPixelH);

  if (chunkMaxX < 0 || chunkMaxY < 0 ||
      chunkMinX >= static_cast<int>(layer.chunkCols) ||
      chunkMinY >= static_cast<int>(layer.chunkRows))
  {
    return false;
  }

  if (chunkMinX < 0)
  {
    chunkMinX = 0;
  }
  if (chunkMinY < 0)
  {
    chunkMinY = 0;
  }
  if (chunkMaxX >= static_cast<int>(layer.chunkCols))
  {
    chunkMaxX = static_cast<int>(layer.chunkCols) - 1;
  }
  if (chunkMaxY >= static_cast<int>(layer.chunkRows))
  {
    chunkMaxY = static_cast<int>(layer.chunkRows) - 1;
  }

  *outChunkMinX = chunkMinX;
  *outChunkMinY = chunkMinY;
  *outChunkMaxX = chunkMaxX;
  *outChunkMaxY = chunkMaxY;
  return true;
}

const Marker *Tilemap::FindFirstMarkerByType(const char *typeString) const
{
  if (!typeString || !m_markers || !m_header)
  {
    return nullptr;
  }

  for (uint32_t i = 0; i < m_header->markerCount; ++i)
  {
    const Marker &marker = m_markers[i];
    const char *markerType = GetStringByIndex(marker.typeStringIndex);
    if (markerType && std::strcmp(markerType, typeString) == 0)
    {
      return &marker;
    }
  }

  return nullptr;
}

const Marker *Tilemap::FindFirstMarkerByEvent(const char *eventString) const
{
  if (!eventString || !m_markers || !m_header)
  {
    return nullptr;
  }

  for (uint32_t i = 0; i < m_header->markerCount; ++i)
  {
    const Marker &marker = m_markers[i];
    const char *markerEvent = GetStringByIndex(marker.eventStringIndex);
    if (markerEvent && std::strcmp(markerEvent, eventString) == 0)
    {
      return &marker;
    }
  }

  return nullptr;
}

bool Tilemap::ValidateHeader()
{
  if (!m_header)
  {
    m_lastError = "Tilemap header not resolved";
    return false;
  }

  if (m_header->magic != TILEMAP_MAGIC)
  {
    m_lastError = "Invalid tilemap magic";
    return false;
  }

  if (m_header->versionMajor != TILEMAP_VERSION_MAJOR)
  {
    m_lastError = "Unsupported tilemap major version";
    return false;
  }

  if (m_header->versionMinor > TILEMAP_VERSION_MINOR)
  {
    m_lastError = "Unsupported tilemap minor version";
    return false;
  }

  if (m_header->fileSize != m_size)
  {
    m_lastError = "Tilemap fileSize mismatch";
    return false;
  }

  if (m_header->tileWidth == 0 || m_header->tileHeight == 0)
  {
    m_lastError = "Invalid tile dimensions";
    return false;
  }

  if (m_header->chunkWidthTiles == 0 || m_header->chunkHeightTiles == 0)
  {
    m_lastError = "Invalid chunk dimensions";
    return false;
  }

  return true;
}

bool Tilemap::ResolveSections()
{
  if (!m_header || !m_bytes)
  {
    m_lastError = "Tilemap header not resolved";
    return false;
  }

  m_usesTilesetRemapTable = m_header->versionMinor >= 1;

  const size_t tilesetsSize =
      static_cast<size_t>(m_header->tilesetCount) * sizeof(TilesetDef);
  const size_t layersSize =
      static_cast<size_t>(m_header->layerCount) * sizeof(LayerDef);
  const size_t chunksSize =
      static_cast<size_t>(m_header->chunkCount) * sizeof(ChunkDef);
  const size_t collisionsSize =
      static_cast<size_t>(m_header->collisionCount) * sizeof(CollisionArea);
  const size_t markersSize =
      static_cast<size_t>(m_header->markerCount) * sizeof(Marker);
  const size_t stringsSize =
      static_cast<size_t>(m_header->stringCount) * sizeof(StringEntry);

  if (AddOverflowsRange(m_header->tilesetTableOffset, tilesetsSize, m_size))
  {
    m_lastError = "Tileset table out of range";
    return false;
  }

  if (AddOverflowsRange(m_header->layerTableOffset, layersSize, m_size))
  {
    m_lastError = "Layer table out of range";
    return false;
  }

  if (AddOverflowsRange(m_header->chunkTableOffset, chunksSize, m_size))
  {
    m_lastError = "Chunk table out of range";
    return false;
  }

  if (m_header->chunkDataOffset > m_size)
  {
    m_lastError = "Chunk data offset out of range";
    return false;
  }

  if (AddOverflowsRange(m_header->collisionTableOffset, collisionsSize, m_size))
  {
    m_lastError = "Collision table out of range";
    return false;
  }

  if (AddOverflowsRange(m_header->markerTableOffset, markersSize, m_size))
  {
    m_lastError = "Marker table out of range";
    return false;
  }

  if (AddOverflowsRange(m_header->stringTableOffset, stringsSize, m_size))
  {
    m_lastError = "String table out of range";
    return false;
  }

  if (m_header->stringDataOffset > m_size)
  {
    m_lastError = "String data offset out of range";
    return false;
  }

  m_tilesets = (tilesetsSize > 0)
                   ? reinterpret_cast<const TilesetDef *>(m_bytes + m_header->tilesetTableOffset)
                   : nullptr;
  m_layers = (layersSize > 0)
                 ? reinterpret_cast<const LayerDef *>(m_bytes + m_header->layerTableOffset)
                 : nullptr;
  m_chunks = (chunksSize > 0)
                 ? reinterpret_cast<const ChunkDef *>(m_bytes + m_header->chunkTableOffset)
                 : nullptr;
  m_chunkData = m_bytes + m_header->chunkDataOffset;
  m_collisions = (collisionsSize > 0)
                     ? reinterpret_cast<const CollisionArea *>(
                           m_bytes + m_header->collisionTableOffset)
                     : nullptr;
  m_markers = (markersSize > 0)
                  ? reinterpret_cast<const Marker *>(m_bytes + m_header->markerTableOffset)
                  : nullptr;
  m_strings = (stringsSize > 0)
                  ? reinterpret_cast<const StringEntry *>(m_bytes + m_header->stringTableOffset)
                  : nullptr;
  m_stringData = reinterpret_cast<const char *>(m_bytes + m_header->stringDataOffset);

  return true;
}

bool Tilemap::ValidateRanges()
{
  if (!m_header)
  {
    m_lastError = "Tilemap header not resolved";
    return false;
  }

  for (uint32_t i = 0; i < m_header->tilesetCount; ++i)
  {
    const TilesetDef &tileset = m_tilesets[i];
    if (tileset.tileWidth == 0 || tileset.tileHeight == 0 || tileset.columns == 0)
    {
      m_lastError = "Invalid tileset dimensions";
      return false;
    }

    if (tileset.tileWidth != m_header->tileWidth ||
        tileset.tileHeight != m_header->tileHeight)
    {
      m_lastError = "Tileset dimensions do not match tilemap header";
      return false;
    }

    if (m_usesTilesetRemapTable)
    {
      const size_t remapBytes =
          static_cast<size_t>(tileset.tileCount) * sizeof(uint32_t);
      if (AddOverflowsRange(tileset.atlasSpriteRemapOffset, remapBytes, m_size))
      {
        m_lastError = "Tileset remap table out of range";
        return false;
      }
    }
  }

  const uint32_t maxChunkTiles =
      static_cast<uint32_t>(m_header->chunkWidthTiles) *
      static_cast<uint32_t>(m_header->chunkHeightTiles);
  const size_t chunkDataBytes = m_size - static_cast<size_t>(m_header->chunkDataOffset);
  const size_t stringDataBytes = m_size - static_cast<size_t>(m_header->stringDataOffset);

  for (uint32_t i = 0; i < m_header->layerCount; ++i)
  {
    const LayerDef &layer = m_layers[i];
    const uint32_t maxChunkCount =
        static_cast<uint32_t>(layer.chunkCols) * static_cast<uint32_t>(layer.chunkRows);

    if (layer.widthTiles == 0 || layer.heightTiles == 0)
    {
      m_lastError = "Layer has invalid dimensions";
      return false;
    }

    if (layer.widthTiles > m_header->mapWidthTiles || layer.heightTiles > m_header->mapHeightTiles)
    {
      m_lastError = "Layer dimensions exceed map bounds";
      return false;
    }

    if (layer.chunkCount > maxChunkCount)
    {
      m_lastError = "Layer chunk count exceeds chunk grid";
      return false;
    }

    if (layer.firstChunkIndex > m_header->chunkCount ||
        layer.chunkCount > (static_cast<uint32_t>(m_header->chunkCount) - layer.firstChunkIndex))
    {
      m_lastError = "Layer chunk range out of bounds";
      return false;
    }

    if (layer.firstCollisionIndex > m_header->collisionCount ||
        layer.collisionCount >
            (static_cast<uint32_t>(m_header->collisionCount) - layer.firstCollisionIndex))
    {
      m_lastError = "Layer collision range out of bounds";
      return false;
    }

    if (layer.firstMarkerIndex > m_header->markerCount ||
        layer.markerCount > (static_cast<uint32_t>(m_header->markerCount) - layer.firstMarkerIndex))
    {
      m_lastError = "Layer marker range out of bounds";
      return false;
    }
  }

  for (uint32_t i = 0; i < m_header->chunkCount; ++i)
  {
    const ChunkDef &chunk = m_chunks[i];
    if (chunk.layerIndex >= m_header->layerCount)
    {
      m_lastError = "Chunk references invalid layer";
      return false;
    }

    const LayerDef &layer = m_layers[chunk.layerIndex];
    if (chunk.chunkX >= layer.chunkCols || chunk.chunkY >= layer.chunkRows)
    {
      m_lastError = "Chunk coordinates out of layer bounds";
      return false;
    }

    if (chunk.tileCount > maxChunkTiles || chunk.usedTileCount > chunk.tileCount)
    {
      m_lastError = "Chunk tile count invalid";
      return false;
    }

    const size_t tileBytes = static_cast<size_t>(chunk.tileCount) * sizeof(TileEntry);
    if (AddOverflowsRange(chunk.tileDataOffset, tileBytes, chunkDataBytes))
    {
      m_lastError = "Chunk tile data out of range";
      return false;
    }
  }

  for (uint32_t i = 0; i < m_header->markerCount; ++i)
  {
    const Marker &marker = m_markers[i];
    if (marker.shape != static_cast<uint16_t>(MarkerShape::Point) &&
        marker.shape != static_cast<uint16_t>(MarkerShape::Rect))
    {
      m_lastError = "Marker has invalid shape";
      return false;
    }

    if (!IsValidStringIndex(marker.typeStringIndex, m_header->stringCount) ||
        !IsValidStringIndex(marker.eventStringIndex, m_header->stringCount) ||
        !IsValidStringIndex(marker.nameStringIndex, m_header->stringCount))
    {
      m_lastError = "Marker string index out of range";
      return false;
    }
  }

  for (uint32_t i = 0; i < m_header->stringCount; ++i)
  {
    const StringEntry &entry = m_strings[i];
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

} // namespace level2d
