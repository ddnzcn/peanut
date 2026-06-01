#ifndef ENGINE_SCENE_PSCNLOADER_HPP
#define ENGINE_SCENE_PSCNLOADER_HPP

#include "engine/scene/PscnTypes.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace engine
{

static constexpr uint16_t PSCN_MAX_NODES = 512;

class PscnFile
{
public:
  bool Load(const std::string &path);
  void Clear();

  bool IsValid() const { return m_header != nullptr; }

  const PscnHeader *GetHeader() const { return m_header; }

  uint16_t GetNodeCount() const;
  uint16_t GetTilesetCount() const;
  uint16_t GetChunkCount() const;
  uint16_t GetStringCount() const;

  const PscnNodeBase *GetNodeBase(uint32_t index) const;
  const void *GetNodeExtension(uint32_t index) const;

  const PscnTilesetDef *GetTileset(uint32_t index) const;
  const PscnChunkDef *GetChunk(uint32_t index) const;
  const PscnTileCell *GetChunkTiles(const PscnChunkDef &chunk) const;

  const char *GetString(uint32_t index) const;

  bool GetAtlasSpriteIdForTile(const PscnTilesetDef &tileset,
                               uint32_t tileId,
                               uint32_t *outSpriteId) const;

  const std::string &GetLastError() const { return m_lastError; }

private:
  std::vector<uint8_t> m_bytesStorage;
  const uint8_t *m_bytes = nullptr;
  size_t m_size = 0;

  const PscnHeader *m_header = nullptr;
  uint32_t m_nodeOffsets[PSCN_MAX_NODES] = {};
  uint16_t m_resolvedNodeCount = 0;

  const PscnTilesetDef *m_tilesets = nullptr;
  const PscnChunkDef *m_chunks = nullptr;
  const uint8_t *m_chunkData = nullptr;
  const PscnStringEntry *m_strings = nullptr;
  const char *m_stringData = nullptr;

  std::string m_lastError;

  static bool ReadWholeFile(const std::string &path,
                            std::vector<uint8_t> *outBytes,
                            std::string *outError);
  bool ValidateHeader();
  bool ResolveSections();
  bool ValidateRanges();
};

} // namespace engine

#endif
