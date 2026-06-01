#ifndef ENGINE_SCENE_SCENETREE_HPP
#define ENGINE_SCENE_SCENETREE_HPP

#include "engine/scene/PscnLoader.hpp"
#include "engine/scene/PscnTypes.hpp"
#include "engine/scene/SignalBus.hpp"

#include <cstdint>

namespace engine {

static constexpr uint16_t MAX_SCENE_NODES = PSCN_MAX_NODES;
static constexpr uint16_t MAX_EXTENSION_BYTES = 8192;
static constexpr uint32_t BEHAVIOR_STATE_ARENA = 32 * 1024;

// Runtime-only flag bit on SceneNode::flags. The low bits (visible/locked)
// come from the PSCN format; high bits are reserved for runtime state.
static constexpr uint8_t NODE_FLAG_RUNTIME_DEAD = 1 << 4;
static_assert(
    MAX_EXTENSION_BYTES <= 65535u,
    "MAX_EXTENSION_BYTES must fit in SceneNode::extOffset (uint16_t)");
static_assert(BEHAVIOR_STATE_ARENA <= 65534u,
              "BEHAVIOR_STATE_ARENA must fit in a uint16 offset (0xFFFF "
              "reserved as sentinel)");

struct Transform2D {
  int32_t worldX = 0;
  int32_t worldY = 0;
  int16_t worldRot = 0;
  int16_t worldScaleX = 256;
  int16_t worldScaleY = 256;
};

struct SceneNode {
  uint32_t nodeId = 0;
  uint32_t nameHash = 0;
  uint8_t nodeType = 0;
  uint8_t flags = 0;
  uint16_t renderLayer = 0;

  int32_t localX = 0;
  int32_t localY = 0;
  int16_t localRot = 0;
  int16_t localScaleX = 256;
  int16_t localScaleY = 256;

  Transform2D world;

  int32_t parentIndex = -1;
  uint32_t firstChildIndex = 0;
  uint16_t childCount = 0;

  int16_t parallaxX = 256;
  int16_t parallaxY = 256;

  uint32_t collisionLayer = 0;
  uint32_t collisionMask = 0;

  uint32_t scriptIdStringIndex = PSCN_STRING_NONE;
  uint32_t scriptDataStringIndex = PSCN_STRING_NONE;
  uint32_t scriptIdHash = 0; // FNV-1a of scriptId string, 0 = no script

  uint16_t extOffset = 0;
  uint16_t extSize = 0;

  uint8_t activeAnimIndex = 0;
};

class SceneTree {
public:
  bool BuildFromPscn(const PscnFile &file);
  void Clear();

  uint16_t GetNodeCount() const { return m_nodeCount; }
  SceneNode *GetNode(uint32_t index);
  const SceneNode *GetNode(uint32_t index) const;

  // Runtime removal: marks the node and its entire descendant subtree dead.
  // Memory is NOT compacted (indices stay valid for any cached references);
  // dead nodes are skipped by render / behaviors / signal dispatch.
  // Behaviors attached to dead nodes get onDestroy called at end-of-tick.
  bool IsNodeDead(uint32_t index) const;
  void RemoveNode(uint32_t index);

  // Find the first node whose scriptId matches. Returns nullptr / -1 if none.
  SceneNode *FindNodeByScriptId(const char *scriptId);
  const SceneNode *FindNodeByScriptId(const char *scriptId) const;
  int32_t FindNodeIndexByScriptId(const char *scriptId) const;

  // Hash variant (avoids re-hashing when the caller already has the hash).
  SceneNode *FindNodeByScriptIdHash(uint32_t scriptIdHash);
  const SceneNode *FindNodeByScriptIdHash(uint32_t scriptIdHash) const;
  int32_t FindNodeIndexByScriptIdHash(uint32_t scriptIdHash) const;

  const void *GetExtension(const SceneNode &node) const;

  template <typename T> const T *GetExtensionAs(const SceneNode &node) const {
    if (node.extSize < sizeof(T)) {
      return nullptr;
    }
    return reinterpret_cast<const T *>(m_extensionBlob + node.extOffset);
  }

  const uint32_t *GetAnimNameHashes(const SceneNode &node,
                                    uint8_t *outCount) const;

  void ComputeWorldTransforms();

  const uint16_t *GetRenderOrder() const { return m_renderOrder; }
  uint16_t GetRenderOrderCount() const { return m_nodeCount; }

  // Signal bus + behavior-instance state arena live next to the node array so
  // dispatch resolves (nodeIndex -> SceneNode*) and (offset -> state*) on the
  // same memory neighbourhood as transform reads.
  SignalBus &GetSignalBus() { return m_signalBus; }
  const SignalBus &GetSignalBus() const { return m_signalBus; }

  uint8_t *GetBehaviorStateArena() { return m_behaviorStateArena; }
  uint16_t GetBehaviorArenaUsed() const { return m_behaviorArenaUsed; }
  bool ClaimBehaviorState(uint16_t sizeBytes, uint16_t *outOffset);

private:
  SceneNode m_nodes[MAX_SCENE_NODES] = {};
  uint8_t m_extensionBlob[MAX_EXTENSION_BYTES] = {};
  uint16_t m_renderOrder[MAX_SCENE_NODES] = {};
  uint16_t m_nodeCount = 0;
  uint16_t m_extensionUsed = 0;

  SignalBus m_signalBus;
  uint8_t m_behaviorStateArena[BEHAVIOR_STATE_ARENA] = {};
  uint16_t m_behaviorArenaUsed = 0;

  void BuildRenderOrder();
};

} // namespace engine

#endif
