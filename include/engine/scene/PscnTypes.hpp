#ifndef ENGINE_SCENE_PSCNTYPES_HPP
#define ENGINE_SCENE_PSCNTYPES_HPP

#include <cstdint>

namespace engine
{

static constexpr uint32_t PSCN_MAGIC = 0x4E435350;
static constexpr uint16_t PSCN_VERSION_MAJOR = 1;
static constexpr uint16_t PSCN_VERSION_MINOR = 0;
static constexpr uint32_t PSCN_STRING_NONE = 0xFFFFFFFFu;

enum PscnNodeType : uint8_t
{
  NODE_ROOT = 0,
  NODE_NODE2D = 1,
  NODE_SPRITE = 2,
  NODE_TILEMAP = 3,
  NODE_COLLISION_SHAPE = 4,
  NODE_AREA = 5,
  NODE_LIGHT2D = 6,
  NODE_ANIMATED_SPRITE = 7,
  NODE_CAMERA2D = 8,
  NODE_SPAWNER = 9,
  NODE_PATH2D = 10,
  NODE_PATH_FOLLOW2D = 11,
  NODE_TIMER = 12,
  NODE_DECAL = 13,
  NODE_VISIBILITY_NOTIFIER = 14,
  NODE_NAV_REGION2D = 15,
};

enum PscnNodeFlags : uint8_t
{
  NODE_FLAG_VISIBLE = 1 << 0,
  NODE_FLAG_LOCKED = 1 << 1,
};

enum PscnTileTransform : uint8_t
{
  TILE_FLIP_X = 1 << 0,
  TILE_FLIP_Y = 1 << 1,
  TILE_ROT90 = 1 << 2,
};

enum PscnProjection : uint8_t
{
  PROJECTION_ORTHO = 0,
  PROJECTION_ISO_DIAMOND = 1,
  PROJECTION_ISO_STAGGERED = 2,
};

enum PscnCollisionShapeType : uint8_t
{
  COLLISION_RECT = 0,
  COLLISION_CIRCLE = 1,
  COLLISION_POLYGON = 2,
};

enum PscnAreaShape : uint8_t
{
  AREA_POINT = 0,
  AREA_RECT = 1,
};

enum PscnLightVariant : uint8_t
{
  LIGHT_OMNI = 0,
  LIGHT_DIRECTIONAL = 1,
};

enum PscnCamera2DFlags : uint16_t
{
  CAMERA_FLAG_IS_CURRENT = 1 << 0,
  CAMERA_FLAG_USE_BOUNDS = 1 << 1,
};

enum PscnSpawnerFlags : uint16_t
{
  SPAWNER_FLAG_AUTO_START = 1 << 0,
};

enum PscnPath2DFlags : uint16_t
{
  PATH2D_FLAG_CLOSED = 1 << 0,
};

enum PscnPathFollow2DFlags : uint16_t
{
  PATH_FOLLOW_FLAG_LOOP = 1 << 0,
  PATH_FOLLOW_FLAG_ROTATE_TO_PATH = 1 << 1,
  PATH_FOLLOW_FLAG_CUBIC_INTERP = 1 << 2,
};

enum PscnTimerFlags : uint16_t
{
  TIMER_FLAG_ONE_SHOT = 1 << 0,
  TIMER_FLAG_AUTO_START = 1 << 1,
};

enum PscnDecalBlend : uint8_t
{
  DECAL_ALPHA = 0,
  DECAL_ADDITIVE = 1,
  DECAL_MULTIPLY = 2,
};

enum PscnDecalFlipFlags : uint8_t
{
  DECAL_FLIP_H = 1 << 0,
  DECAL_FLIP_V = 1 << 1,
};

#pragma pack(push, 1)

struct PscnHeader
{
  uint32_t magic;
  uint16_t versionMajor;
  uint16_t versionMinor;
  uint32_t fileSize;
  uint32_t crc32;
  uint16_t nodeCount;
  uint16_t tilesetCount;
  uint16_t chunkCount;
  uint16_t stringCount;
  uint32_t nodeTableOffset;
  uint32_t tilesetTableOffset;
  uint32_t chunkTableOffset;
  uint32_t chunkDataOffset;
  uint32_t stringTableOffset;
  uint32_t stringDataOffset;
  uint8_t reserved[16];
};

struct PscnNodeBase
{
  uint32_t nodeId;
  int32_t parentIndex;
  uint32_t nameHash;
  uint8_t nodeType;
  uint8_t flags;
  uint16_t renderLayer;
  int32_t posX;
  int32_t posY;
  int16_t rotation;
  int16_t scaleX;
  int16_t scaleY;
  uint16_t childCount;
  uint32_t firstChildIndex;
  uint32_t scriptIdStringIndex;
  uint32_t scriptDataStringIndex;
  uint32_t collisionLayer;
  uint32_t collisionMask;
  int16_t parallaxX;
  int16_t parallaxY;
  uint16_t extSize;
  uint8_t reserved[6];
};

struct PscnSpriteExt
{
  uint32_t spriteId;
  uint8_t flipH;
  uint8_t flipV;
  uint16_t _pad;
  uint32_t tintColor;
};

struct PscnAnimatedSpriteExt
{
  uint8_t animCount;
  uint8_t flipH;
  uint8_t flipV;
  uint8_t _pad;
  uint32_t tintColor;
  uint32_t defaultSpriteId;
  // followed by animCount * uint32_t animNameHashes
};

struct PscnTileMapExt
{
  uint16_t tileWidth;
  uint16_t tileHeight;
  uint16_t chunkWidthTiles;
  uint16_t chunkHeightTiles;
  uint16_t mapWidthTiles;
  uint16_t mapHeightTiles;
  uint8_t projection;
  uint8_t _pad;
  uint16_t chunkCount;
  uint32_t firstChunkIndex;
  uint32_t reserved;
};

struct PscnCollisionShapeExt
{
  uint8_t shape;
  uint8_t _pad[3];
  int32_t width;
  int32_t height;
  int32_t radius;
};

struct PscnAreaExt
{
  uint8_t shape;
  uint8_t _pad[3];
  int32_t width;
  int32_t height;
  uint32_t tagStringIndex;
};

struct PscnLight2DExt
{
  int32_t radius;
  uint32_t color;
  uint16_t intensity;
  uint16_t falloff;
  uint8_t variant;
  uint8_t _pad;
  int16_t directionAngle;
  int16_t coneAngle;
  int16_t _reserved;
};

struct PscnCamera2DExt
{
  int16_t zoom;            // 8.8 fixed (256 = 1.0)
  int16_t smoothingSpeed;  // 8.8 fixed (0 = snap)
  uint16_t flags;          // PscnCamera2DFlags
  uint16_t _pad;
  uint32_t followTargetHash; // FNV-1a; 0 = none
  int16_t boundsLeft;
  int16_t boundsTop;
  int16_t boundsRight;
  int16_t boundsBottom;
  uint32_t _reserved;
};

struct PscnSpawnerExt
{
  uint32_t sceneNameHash;
  uint16_t spawnIntervalMs;  // 0 = manual
  uint16_t maxAlive;          // 0 = unlimited
  uint16_t flags;             // PscnSpawnerFlags
  uint16_t _pad;
  uint32_t spawnAreaRadius;   // 16.16 fixed
};

// Variable-size: 8-byte base + pointCount * { i32 x, i32 y } (16.16 fixed).
struct PscnPath2DExt
{
  uint16_t pointCount;
  uint16_t flags;             // PscnPath2DFlags
  uint32_t color;             // RGBA preview
  // followed by pointCount * 8 bytes (int32 x, int32 y)
};

struct PscnPathFollow2DExt
{
  uint32_t pathNodeHash;   // FNV-1a of Path2D node name
  int16_t progress;        // 8.8 fixed (0..1)
  uint16_t flags;          // PscnPathFollow2DFlags
  uint32_t loopOffsetMs;
};

struct PscnTimerExt
{
  uint32_t waitTimeMs;
  uint16_t flags;          // PscnTimerFlags
  uint16_t _pad;
  uint32_t eventNameHash;  // FNV-1a
};

struct PscnDecalExt
{
  uint32_t spriteId;
  uint8_t blendMode;       // PscnDecalBlend
  int8_t sortOffset;
  uint8_t flipFlags;       // PscnDecalFlipFlags
  uint8_t _pad;
  uint32_t tintColor;
  uint32_t _reserved;
};

struct PscnVisibilityNotifierExt
{
  int32_t width;           // 16.16 fixed
  int32_t height;          // 16.16 fixed
  uint32_t enterEventHash;
  uint32_t exitEventHash;
};

// Variable-size: 8-byte base + pointCount * { i32 x, i32 y } (16.16 fixed).
// Polygon is always closed (last→first segment implied).
struct PscnNavRegion2DExt
{
  uint16_t pointCount;    // minimum 3
  uint16_t navLayer;      // bitmask
  uint32_t _reserved;
  // followed by pointCount * 8 bytes (int32 x, int32 y)
};

struct PscnTilesetDef
{
  uint32_t id;
  uint32_t nameHash;
  uint32_t firstTileId;
  uint32_t tileCount;
  uint32_t remapTableOffset;
  uint16_t tileWidth;
  uint16_t tileHeight;
  uint16_t columns;
  uint16_t flags;
};

struct PscnChunkDef
{
  uint16_t nodeIndex;
  uint16_t chunkX;
  uint16_t chunkY;
  uint16_t _pad;
  uint32_t tileDataOffset;
  uint32_t tileCount;
  uint16_t usedTileCount;
  uint16_t _pad2;
};

struct PscnTileCell
{
  uint32_t tileId;
  uint8_t flags;
  uint8_t _pad0;
  uint16_t _pad1;
};

struct PscnStringEntry
{
  uint32_t offset;
  uint32_t length;
};

#pragma pack(pop)

#ifndef __INTELLISENSE__
static_assert(sizeof(PscnHeader) == 64, "PscnHeader size mismatch");
static_assert(sizeof(PscnNodeBase) == 64, "PscnNodeBase size mismatch");
static_assert(sizeof(PscnSpriteExt) == 12, "PscnSpriteExt size mismatch");
static_assert(sizeof(PscnAnimatedSpriteExt) == 12, "PscnAnimatedSpriteExt base size mismatch");
static_assert(sizeof(PscnTileMapExt) == 24, "PscnTileMapExt size mismatch");
static_assert(sizeof(PscnCollisionShapeExt) == 16, "PscnCollisionShapeExt size mismatch");
static_assert(sizeof(PscnAreaExt) == 16, "PscnAreaExt size mismatch");
static_assert(sizeof(PscnLight2DExt) == 20, "PscnLight2DExt size mismatch");
static_assert(sizeof(PscnCamera2DExt) == 24, "PscnCamera2DExt size mismatch");
static_assert(sizeof(PscnSpawnerExt) == 16, "PscnSpawnerExt size mismatch");
static_assert(sizeof(PscnPath2DExt) == 8, "PscnPath2DExt base size mismatch");
static_assert(sizeof(PscnPathFollow2DExt) == 12, "PscnPathFollow2DExt size mismatch");
static_assert(sizeof(PscnTimerExt) == 12, "PscnTimerExt size mismatch");
static_assert(sizeof(PscnDecalExt) == 16, "PscnDecalExt size mismatch");
static_assert(sizeof(PscnVisibilityNotifierExt) == 16, "PscnVisibilityNotifierExt size mismatch");
static_assert(sizeof(PscnNavRegion2DExt) == 8, "PscnNavRegion2DExt base size mismatch");
static_assert(sizeof(PscnTilesetDef) == 28, "PscnTilesetDef size mismatch");
static_assert(sizeof(PscnChunkDef) == 20, "PscnChunkDef size mismatch");
static_assert(sizeof(PscnTileCell) == 8, "PscnTileCell size mismatch");
static_assert(sizeof(PscnStringEntry) == 8, "PscnStringEntry size mismatch");
#endif

} // namespace engine

#endif
