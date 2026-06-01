#include "game/Behaviors.hpp"
#include "game/GameContext.hpp"

#include "atlas2d/AtlasPack.hpp"
#include "engine/Signals.hpp"
#include "engine/scene/Behavior.hpp"
#include "engine/scene/BehaviorRegistry.hpp"
#include "engine/scene/SceneTree.hpp"
#include "engine/scene/SignalBus.hpp"

#include <cstdint>
#include <cstdio>

extern "C"
{
#include <libpad.h>
}

namespace game
{

namespace
{

// 16.16 fixed-point: 1.0 pixel == 1 << 16.
constexpr int32_t kFixed16One = 1 << 16;

// Diagonal normalization in 8.8 fixed-point: 1/sqrt(2) ~= 0.7071 -> 181/256.
constexpr int32_t kDiagScale_8_8 = 181;
constexpr int32_t kFixed8One = 256;

struct PlayerState
{
  int32_t speedFixed;   // cached cameraSpeed * (1 << 16) at init
  uint8_t activeAnim;   // example field — toggled by signal handler
  uint8_t _pad[3];
};

void PlayerInit(uint16_t bindingIndex,
                uint32_t /*nodeIndex*/,
                engine::SceneNode * /*node*/,
                void *state,
                const char * /*behaviorData*/,
                game::GameContext *ctx)
{
  if (!ctx || !state)
  {
    return;
  }

  auto &s = S<PlayerState>(state);
  s.speedFixed = ctx->cameraSpeed * kFixed16One;
  s.activeAnim = 0;

  if (ctx->behaviorRegistry)
  {
    engine::signal::Subscribe(*ctx->behaviorRegistry, bindingIndex, engine::signal::kPadPressed);
    engine::signal::Subscribe(*ctx->behaviorRegistry, bindingIndex, engine::signal::kPadReleased);
  }
}

void PlayerUpdate(uint16_t /*bindingIndex*/,
                  uint32_t /*nodeIndex*/,
                  engine::SceneNode *node,
                  void *state,
                  game::GameContext *ctx)
{
  if (!ctx || !node || !state)
  {
    return;
  }

  auto &s = S<PlayerState>(state);

  // Build a direction vector (-1/0/+1 per axis). Opposite presses cancel.
  int dx = 0;
  int dy = 0;
  if ((ctx->padButtons & PAD_LEFT) != 0)  { dx -= 1; }
  if ((ctx->padButtons & PAD_RIGHT) != 0) { dx += 1; }
  if ((ctx->padButtons & PAD_UP) != 0)    { dy -= 1; }
  if ((ctx->padButtons & PAD_DOWN) != 0)  { dy += 1; }

  int32_t speed = s.speedFixed;
  // Normalize diagonals so up+left moves the same speed as a single direction
  // instead of sqrt(2) faster.
  if (dx != 0 && dy != 0)
  {
    speed = (speed * kDiagScale_8_8) / kFixed8One;
  }

  node->localX += dx * speed;
  node->localY += dy * speed;
}

void PlayerOnSignal(uint16_t /*bindingIndex*/,
                    uint32_t /*nodeIndex*/,
                    engine::SceneNode * /*node*/,
                    void *state,
                    uint32_t signalHash,
                    uint32_t /*sourceNodeIndex*/,
                    const engine::SignalPayload *payload,
                    game::GameContext * /*ctx*/)
{
  if (!state || !payload)
  {
    return;
  }

  // Stub: prove the wiring works. Replace with real reactions (jump on cross,
  // switch animation on direction press, etc.).
  std::printf("[player] signal 0x%08x mask=0x%04x edge=%u\n",
              static_cast<unsigned>(signalHash),
              static_cast<unsigned>(payload->u.button.mask),
              static_cast<unsigned>(payload->u.button.edge));

  if (signalHash == engine::signal::Hash(engine::signal::kPadPressed) &&
      payload->u.button.mask == PAD_CROSS)
  {
    S<PlayerState>(state).activeAnim = 1;
  }
}

} // namespace

void RegisterAllBehaviors(engine::BehaviorRegistry &registry)
{
  engine::BehaviorVTable vt = {};
  vt.nameHash = atlas2d::FNV1a32("player");
  vt.stateSize = sizeof(PlayerState);
  vt.autoAttachNodeType = engine::BEHAVIOR_AUTO_ATTACH_NONE;
  vt.onInit = PlayerInit;
  vt.onUpdate = PlayerUpdate;
  vt.onDestroy = nullptr;
  vt.onSignal = PlayerOnSignal;
  registry.Register(vt);
}

} // namespace game
