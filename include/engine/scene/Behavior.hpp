#ifndef ENGINE_SCENE_BEHAVIOR_HPP
#define ENGINE_SCENE_BEHAVIOR_HPP

#include <cstdint>

namespace game
{
struct GameContext;
} // namespace game

namespace engine
{

struct SceneNode;
struct SignalPayload;

// 0xFF disables auto-attach (the vtable is matched purely by nameHash).
constexpr uint8_t BEHAVIOR_AUTO_ATTACH_NONE = 0xFF;

typedef void (*BehaviorInitFn)(uint16_t bindingIndex,
                               uint32_t nodeIndex,
                               SceneNode *node,
                               void *state,
                               const char *behaviorData,
                               game::GameContext *gameCtx);

typedef void (*BehaviorUpdateFn)(uint16_t bindingIndex,
                                 uint32_t nodeIndex,
                                 SceneNode *node,
                                 void *state,
                                 game::GameContext *gameCtx);

typedef void (*BehaviorDestroyFn)(uint16_t bindingIndex,
                                  uint32_t nodeIndex,
                                  SceneNode *node,
                                  void *state,
                                  game::GameContext *gameCtx);

typedef void (*BehaviorSignalFn)(uint16_t bindingIndex,
                                 uint32_t nodeIndex,
                                 SceneNode *node,
                                 void *state,
                                 uint32_t signalHash,
                                 uint32_t sourceNodeIndex,
                                 const SignalPayload *payload,
                                 game::GameContext *gameCtx);

struct BehaviorVTable
{
  uint32_t nameHash = 0;                                 // FNV-1a; 0 = auto-attach-only
  uint16_t stateSize = 0;                                // 0 = no per-instance state
  uint8_t autoAttachNodeType = BEHAVIOR_AUTO_ATTACH_NONE;
  uint8_t _pad = 0;
  BehaviorInitFn onInit = nullptr;
  BehaviorUpdateFn onUpdate = nullptr;
  BehaviorDestroyFn onDestroy = nullptr;
  BehaviorSignalFn onSignal = nullptr;
};

} // namespace engine

#endif
