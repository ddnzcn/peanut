#ifndef ATLAS2D_ATLASPACK_HPP
#define ATLAS2D_ATLASPACK_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace atlas2d
{

  static constexpr uint32_t ATLAS_MAGIC = 0x54443241;

  enum class PageFormat : uint8_t
  {
    RGBA32 = 0,
    IDX8 = 1,
    IDX4 = 2
  };

#pragma pack(push, 1)

  struct AtlasHeader
  {
    uint32_t magic;
    uint16_t versionMajor;
    uint16_t versionMinor;
    uint32_t fileSize;
    uint32_t crc32;
    uint16_t pageCount;
    uint16_t spriteCount;
    uint16_t animCount;
    uint16_t animFrameCount;
    uint16_t animTileCount;
    uint16_t animTileFrameCount;
    uint16_t hashEntryCount;
    uint16_t _pad;
    uint32_t pageTableOffset;
    uint32_t spriteTableOffset;
    uint32_t animTableOffset;
    uint32_t animFrameTableOffset;
    uint32_t hashTableOffset;
    uint32_t animTileTableOffset;
  };

  struct AtlasPage
  {
    uint16_t pageIndex;
    uint16_t width;
    uint16_t height;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint8_t reserved[16];
  };

  struct AtlasSprite
  {
    uint32_t id;
    uint32_t nameHash;
    uint16_t pageIndex;
    uint16_t flags;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t trimX;
    uint16_t trimY;
    uint16_t sourceW;
    uint16_t sourceH;
    uint8_t reserved[12];
  };

  struct AtlasHashEntry
  {
    uint32_t nameHash;
    uint32_t spriteIndex;
  };

  enum AtlasAnimFlags : uint16_t
  {
    AtlasAnimFlag_None = 0,
    AtlasAnimFlag_Loop = 1 << 0
  };

  struct AtlasAnim
  {
    uint32_t nameHash;
    uint16_t firstFrameIndex;
    uint16_t frameCount;
    uint16_t flags;
    uint16_t _pad;
  };

  struct AtlasFrame
  {
    uint32_t spriteIndex;
    uint16_t durationMs;
    uint16_t _pad;
  };

  struct AtlasAnimTile
  {
    uint32_t baseSpriteIndex;
    uint16_t firstFrameIndex;
    uint16_t frameCount;
  };

  struct AtlasAnimTileFrame
  {
    uint32_t spriteIndex;
    uint16_t durationMs;
    uint16_t _pad;
  };

#pragma pack(pop)

#ifndef __INTELLISENSE__
  static_assert(sizeof(AtlasHeader) == 56, "AtlasHeader size mismatch");
  static_assert(sizeof(AtlasPage) == 30, "AtlasPage size mismatch");
  static_assert(sizeof(AtlasSprite) == 40, "AtlasSprite size mismatch");
  static_assert(sizeof(AtlasHashEntry) == 8, "AtlasHashEntry size mismatch");
  static_assert(sizeof(AtlasAnim) == 12, "AtlasAnim size mismatch");
  static_assert(sizeof(AtlasFrame) == 8, "AtlasFrame size mismatch");
  static_assert(sizeof(AtlasAnimTile) == 8, "AtlasAnimTile size mismatch");
  static_assert(sizeof(AtlasAnimTileFrame) == 8, "AtlasAnimTileFrame size mismatch");
#endif

  struct AtlasImageView
  {
    const uint8_t *pixels = nullptr;
    uint32_t sizeBytes = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    PageFormat format = PageFormat::RGBA32;
  };

  struct SpriteUVRect
  {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
  };

  class AtlasPack
  {
  public:
    bool Load(const std::string &metaPath, const std::string &atlasPath);
    void Clear();

    const std::string &GetLastError() const;
    const AtlasHeader *GetHeader() const;
    const AtlasPage *GetPages() const;
    const AtlasSprite *GetSprites() const;

    uint16_t GetPageCount() const;
    uint16_t GetSpriteCount() const;
    uint16_t GetAnimCount() const;

    const AtlasSprite *GetSpriteByIndex(uint32_t index) const;
    const AtlasSprite *FindSpriteById(uint32_t id) const;
    const AtlasSprite *FindSpriteByHash(uint32_t hash) const;

    const AtlasAnim *GetAnims() const;
    const AtlasFrame *GetFrames() const;
    const AtlasAnim *FindAnimByHash(uint32_t nameHash) const;
    // Returns the sprite index for the given animation at timeMs, or UINT32_MAX if not found
    uint32_t ResolveAnimFrame(uint32_t nameHash, uint32_t timeMs) const;

    uint16_t GetAnimTileCount() const;
    const AtlasAnimTile *GetAnimTiles() const;
    const AtlasAnimTileFrame *GetAnimTileFrames() const;
    // Returns the sprite index for the animated tile whose base is baseSpriteIndex at timeMs.
    // Returns baseSpriteIndex unchanged if no animated tile entry is found.
    uint32_t ResolveAnimTileFrame(uint32_t baseSpriteIndex, uint32_t timeMs) const;

    AtlasImageView GetPageImage(uint32_t pageIndex) const;
    SpriteUVRect ComputeUVs(const AtlasSprite &sprite) const;

  private:
    std::vector<uint8_t> m_metaBytes;
    std::vector<uint8_t> m_atlasBytes;

    const AtlasHeader *m_header = nullptr;
    const AtlasPage *m_pages = nullptr;
    const AtlasSprite *m_sprites = nullptr;
    const AtlasHashEntry *m_hashes = nullptr;
    uint32_t m_hashCount = 0;
    const AtlasAnim *m_anims = nullptr;
    const AtlasFrame *m_frames = nullptr;
    const AtlasAnimTile *m_animTiles = nullptr;
    const AtlasAnimTileFrame *m_animTileFrames = nullptr;

    std::string m_lastError;

    static bool ReadWholeFile(const std::string &path,
                              std::vector<uint8_t> *outBytes,
                              std::string *outError);

    bool ValidateHeader();
    bool ResolveTables();
    bool ValidatePages();
    bool ValidateHashTable();
  };

  uint32_t FNV1a32(const char *str);

} // namespace atlas2d

#endif
