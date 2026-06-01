#ifndef ENGINE_SCENE_BEHAVIORREGISTRY_HPP
#define ENGINE_SCENE_BEHAVIORREGISTRY_HPP

#include "engine/scene/Behavior.hpp"

#include <cstdint>

namespace game
{
struct GameContext;
} // namespace game

namespace engine
{

class SceneTree;
class PscnFile;

constexpr uint16_t MAX_BEHAVIOR_VTABLES = 64;
constexpr uint16_t MAX_BEHAVIOR_INSTANCES = 512;
constexpr uint16_t MAX_SUBS_PER_INSTANCE = 8;
constexpr uint16_t BEHAVIOR_STATE_NONE = 0xFFFF;

struct BehaviorInstance
{
  uint32_t nodeIndex = 0;
  uint16_t vtableIndex = 0;
  uint16_t stateOffset = BEHAVIOR_STATE_NONE; // offset into SceneTree's behavior arena
  uint8_t subCount = 0;
  uint8_t destroyed = 0; // 1 once onDestroy has fired; skipped by Update/Dispatch
  uint8_t _pad[2] = {};
  uint32_t subs[MAX_SUBS_PER_INSTANCE] = {}; // signal hashes; 0 = empty slot
};

class BehaviorRegistry
{
public:
  bool Register(const BehaviorVTable &vt);

  // Two-pass bind:
  //  1) every node with scriptIdStringIndex != PSCN_STRING_NONE is matched by
  //     FNV-1a(scriptId) against registered vtables.
  //  2) every vtable with autoAttachNodeType != BEHAVIOR_AUTO_ATTACH_NONE
  //     attaches to every node of that type, skipping any (node, vtable) pair
  //     already created by pass 1.
  // State is claimed from SceneTree's behavior arena.
  void BindScene(SceneTree &tree, const PscnFile &file);

  void InitAll(SceneTree &tree, const PscnFile &file, game::GameContext *gameCtx);
  void UpdateAll(SceneTree &tree, game::GameContext *gameCtx);
  void DestroyAll(SceneTree &tree, game::GameContext *gameCtx);

  // Fires onDestroy on every still-alive instance whose node was marked dead
  // via SceneTree::RemoveNode this frame, then sets `destroyed = 1` on it.
  // Engine calls this at end of tick after SignalBus::Dispatch.
  void ProcessDeadNodes(SceneTree &tree, game::GameContext *gameCtx);

  void Clear();
  void ClearBindings();

  bool Subscribe(uint16_t bindingIndex, uint32_t signalHash);

  uint16_t GetVTableCount() const { return m_vtableCount; }
  uint16_t GetInstanceCount() const { return m_instanceCount; }
  const BehaviorVTable *GetVTable(uint16_t index) const;
  const BehaviorInstance *GetInstance(uint16_t index) const;

private:
  int16_t FindVTable(uint32_t nameHash) const;
  bool HasInstance(uint32_t nodeIndex, uint16_t vtableIndex) const;

  BehaviorVTable m_vtables[MAX_BEHAVIOR_VTABLES] = {};
  BehaviorInstance m_instances[MAX_BEHAVIOR_INSTANCES] = {};
  uint16_t m_vtableCount = 0;
  uint16_t m_instanceCount = 0;
};

} // namespace engine

#endif
