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
    uint16_t flags;

    uint32_t pageTableOffset;
    uint32_t spriteTableOffset;
    uint32_t animTableOffset;
    uint32_t frameTableOffset;
    uint32_t hashTableOffset;
  };

  struct AtlasPage
  {
    uint32_t dataOffset;
    uint32_t dataSize;

    uint16_t width;
    uint16_t height;

    uint8_t format;
    uint8_t flags;

    uint16_t clutEntryCount;
    uint16_t tbw;

    uint32_t clutOffset;
    uint32_t user0;
    uint32_t user1;
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

    int16_t pivotX;
    int16_t pivotY;

    uint16_t sourceW;
    uint16_t sourceH;

    int16_t trimX;
    int16_t trimY;

    int16_t hitboxX;
    int16_t hitboxY;
    uint16_t hitboxW;
    uint16_t hitboxH;
  };

  struct AtlasHashEntry
  {
    uint32_t nameHash;
    uint32_t spriteIndex;
  };

#pragma pack(pop)

#ifndef __INTELLISENSE__
  static_assert(sizeof(AtlasHeader) == 44, "AtlasHeader size mismatch");
  static_assert(sizeof(AtlasPage) == 30, "AtlasPage size mismatch");
  static_assert(sizeof(AtlasSprite) == 40, "AtlasSprite size mismatch");
  static_assert(sizeof(AtlasHashEntry) == 8, "AtlasHashEntry size mismatch");
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

    const AtlasSprite *GetSpriteByIndex(uint32_t index) const;
    const AtlasSprite *FindSpriteById(uint32_t id) const;
    const AtlasSprite *FindSpriteByIdFast(uint32_t id) const;
    const AtlasSprite *FindSpriteByHash(uint32_t hash) const;

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

    std::vector<uint16_t> m_idToIndex;
    uint32_t m_idBase = 0;

    std::string m_lastError;

    void BuildIdLookup();

    bool ValidateHeader();
    bool ResolveTables();
    bool ValidatePages();
    bool ValidateHashTable();
  };

  uint32_t FNV1a32(const char *str);

} // namespace atlas2d

#endif
