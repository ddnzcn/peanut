#ifndef LEVEL2D_TILEMAPRUNTIME_HPP
#define LEVEL2D_TILEMAPRUNTIME_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace level2d
{

  static constexpr uint32_t TILEMAP_MAGIC = 0x50414D54;
  static constexpr uint16_t TILEMAP_VERSION_MAJOR = 2;
  static constexpr uint16_t TILEMAP_VERSION_MINOR = 1;
  static constexpr uint32_t INVALID_STRING_INDEX = 0xFFFFFFFFu;

  enum TileFlags : uint8_t
  {
    TileFlag_None = 0,
    TileFlag_FlipX = 1 << 0,
    TileFlag_FlipY = 1 << 1,
    TileFlag_Rot90 = 1 << 2
  };

  enum LayerFlags : uint16_t
  {
    LayerFlag_None = 0,
    LayerFlag_Visible = 1 << 0,
    LayerFlag_Locked = 1 << 1,
    LayerFlag_RepeatX = 1 << 2,
    LayerFlag_RepeatY = 1 << 3,
    LayerFlag_Foreground = 1 << 4,
    LayerFlag_HasTiles = 1 << 5,
    LayerFlag_HasCollision = 1 << 6,
    LayerFlag_HasMarkers = 1 << 7
  };

  enum class CollisionType : uint16_t
  {
    Solid = 0,
    OneWay = 1,
    Trigger = 2,
    Hurt = 3
  };

  enum CollisionFlags : uint16_t
  {
    CollisionFlag_None = 0,
    CollisionFlag_PlayerOnly = 1 << 0,
    CollisionFlag_EnemyOnly = 1 << 1,
    CollisionFlag_Enabled = 1 << 2
  };

  enum class MarkerShape : uint16_t
  {
    Point = 0,
    Rect = 1
  };

#pragma pack(push, 1)

  struct TilemapHeader
  {
    uint32_t magic;
    uint16_t versionMajor;
    uint16_t versionMinor;

    uint32_t fileSize;
    uint32_t crc32;

    uint16_t mapWidthTiles;
    uint16_t mapHeightTiles;

    uint16_t tileWidth;
    uint16_t tileHeight;

    uint16_t chunkWidthTiles;
    uint16_t chunkHeightTiles;

    uint16_t tilesetCount;
    uint16_t layerCount;

    uint16_t chunkCount;
    uint16_t collisionCount;

    uint16_t markerCount;
    uint16_t stringCount;

    uint32_t tilesetTableOffset;
    uint32_t layerTableOffset;
    uint32_t chunkTableOffset;
    uint32_t chunkDataOffset;
    uint32_t collisionTableOffset;
    uint32_t markerTableOffset;
    uint32_t stringTableOffset;
    uint32_t stringDataOffset;
  };

  struct TilesetDef
  {
    uint32_t tilesetId;
    uint32_t nameHash;

    uint32_t firstTileId;
    uint32_t tileCount;

    uint32_t atlasSpriteRemapOffset;
    uint16_t tileWidth;
    uint16_t tileHeight;

    uint16_t columns;
    uint16_t flags;
  };

  struct LayerDef
  {
    uint32_t layerId;
    uint32_t nameHash;

    uint16_t flags;
    uint16_t drawOrder;

    int16_t parallaxX_8_8;
    int16_t parallaxY_8_8;

    int16_t offsetX;
    int16_t offsetY;

    uint16_t widthTiles;
    uint16_t heightTiles;

    uint16_t chunkCols;
    uint16_t chunkRows;

    uint32_t firstChunkIndex;
    uint32_t chunkCount;

    uint32_t firstCollisionIndex;
    uint16_t collisionCount;

    uint32_t firstMarkerIndex;
    uint16_t markerCount;

    uint16_t reserved0;
  };

  struct ChunkDef
  {
    uint16_t layerIndex;
    uint16_t chunkX;
    uint16_t chunkY;
    uint16_t flags;
    uint16_t reserved0;

    uint32_t tileDataOffset;
    uint32_t tileCount;
    uint16_t usedTileCount;
    uint16_t reserved1;
  };

  struct TileEntry
  {
    uint32_t tileId;
    uint8_t flags;
    uint8_t reserved0;
    uint16_t reserved1;
  };

  struct CollisionArea
  {
    uint16_t type;
    uint16_t flags;

    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;

    uint32_t userData0;
    uint32_t userData1;
  };

  struct Marker
  {
    uint32_t markerId;

    uint16_t shape;
    uint16_t flags;

    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;

    uint32_t typeStringIndex;
    uint32_t eventStringIndex;
    uint32_t nameStringIndex;

    uint32_t userData0;
    uint32_t userData1;
  };

  struct StringEntry
  {
    uint32_t offset;
    uint32_t length;
  };

#pragma pack(pop)

#ifndef __INTELLISENSE__
  static_assert(sizeof(TilemapHeader) == 72, "TilemapHeader size mismatch");
  static_assert(sizeof(TilesetDef) == 28, "TilesetDef size mismatch");
  static_assert(sizeof(LayerDef) == 50, "LayerDef size mismatch");
  static_assert(sizeof(ChunkDef) == 22, "ChunkDef size mismatch");
  static_assert(sizeof(TileEntry) == 8, "TileEntry size mismatch");
  static_assert(sizeof(CollisionArea) == 28, "CollisionArea size mismatch");
  static_assert(sizeof(Marker) == 44, "Marker size mismatch");
  static_assert(sizeof(StringEntry) == 8, "StringEntry size mismatch");
#endif

  struct TileChunkView
  {
    bool hasChunk = false;
    ChunkDef chunk = {};
    const TileEntry *tiles = nullptr;
  };

  struct LayerView
  {
    const LayerDef *layer = nullptr;
    const ChunkDef *chunks = nullptr;
    const CollisionArea *collisions = nullptr;
    const Marker *markers = nullptr;
  };

  class Tilemap
  {
  public:
    bool Load(const std::string &path);
    void Clear();

    bool IsValid() const { return m_header != nullptr; }

    const TilemapHeader *GetHeader() const { return m_header; }
    const TilesetDef *GetTilesets() const { return m_tilesets; }
    const LayerDef *GetLayers() const { return m_layers; }
    const ChunkDef *GetChunks() const { return m_chunks; }
    const CollisionArea *GetCollisions() const { return m_collisions; }
    const Marker *GetMarkers() const { return m_markers; }
    const StringEntry *GetStrings() const { return m_strings; }

    uint16_t GetTilesetCount() const;
    uint16_t GetLayerCount() const;
    uint16_t GetChunkCount() const;
    uint16_t GetCollisionCount() const;
    uint16_t GetMarkerCount() const;
    uint16_t GetStringCount() const;

    const TilesetDef *GetTilesetByIndex(uint32_t index) const;
    const LayerDef *GetLayerByIndex(uint32_t index) const;
    const CollisionArea *GetCollisionByIndex(uint32_t index) const;
    const Marker *GetMarkerByIndex(uint32_t index) const;
    bool GetAtlasSpriteIdForTile(const TilesetDef &tileset,
                                 uint32_t tileId,
                                 uint32_t *outSpriteId) const;

    const char *GetStringByIndex(uint32_t index) const;

    bool GetChunkView(uint32_t chunkIndex, TileChunkView *outView) const;
    bool GetLayerView(uint32_t layerIndex, LayerView *outView) const;

    bool ComputeVisibleChunkRange(const LayerDef &layer,
                                  int cameraX,
                                  int cameraY,
                                  int viewW,
                                  int viewH,
                                  int *outChunkMinX,
                                  int *outChunkMinY,
                                  int *outChunkMaxX,
                                  int *outChunkMaxY) const;

    const Marker *FindFirstMarkerByType(const char *typeString) const;
    const Marker *FindFirstMarkerByEvent(const char *eventString) const;

    const std::string &GetLastError() const { return m_lastError; }

  private:
    std::vector<uint8_t> m_bytesStorage;
    const uint8_t *m_bytes = nullptr;
    size_t m_size = 0;

    const TilemapHeader *m_header = nullptr;
    const TilesetDef *m_tilesets = nullptr;
    const LayerDef *m_layers = nullptr;
    const ChunkDef *m_chunks = nullptr;
    const uint8_t *m_chunkData = nullptr;
    const CollisionArea *m_collisions = nullptr;
    const Marker *m_markers = nullptr;
    const StringEntry *m_strings = nullptr;
    const char *m_stringData = nullptr;
    bool m_usesTilesetRemapTable = false;

    std::string m_lastError;

    bool ValidateHeader();
    bool ResolveSections();
    bool ValidateRanges();
  };

} // namespace level2d

#endif
