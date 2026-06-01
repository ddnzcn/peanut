#ifndef ENGINE_SCENE_SIGNALBUS_HPP
#define ENGINE_SCENE_SIGNALBUS_HPP

#include <cstdint>

namespace game
{
struct GameContext;
} // namespace game

namespace engine
{

class BehaviorRegistry;
class SceneTree;

enum SignalPayloadKind : uint8_t
{
  SIGNAL_EMPTY = 0,
  SIGNAL_INT = 1,
  SIGNAL_PAIR = 2,   // (x, y)
  SIGNAL_QUAD = 3,
  SIGNAL_BUTTON = 4,
};

struct SignalPayload
{
  uint8_t kind = SIGNAL_EMPTY;
  uint8_t _pad[3] = {};
  union
  {
    int32_t i;
    struct
    {
      int32_t x, y;
    } pair;
    struct
    {
      int32_t a, b, c, d;
    } quad;
    struct
    {
      uint16_t mask;
      uint8_t edge;
      uint8_t _pad;
    } button;
  } u = {};
};

static_assert(sizeof(SignalPayload) == 20, "SignalPayload should be 20 bytes");

constexpr uint16_t MAX_SIGNALS_PER_FRAME = 128;
constexpr uint16_t MAX_DISPATCH_ROUNDS = 4;
constexpr uint32_t SIGNAL_TARGET_BROADCAST = 0xFFFFFFFFu;

struct QueuedSignal
{
  uint32_t hash = 0;
  uint32_t sourceNodeIndex = SIGNAL_TARGET_BROADCAST;
  uint32_t targetNodeIndex = SIGNAL_TARGET_BROADCAST;
  SignalPayload payload;
};

class SignalBus
{
public:
  void Emit(uint32_t signalHash, uint32_t sourceNodeIndex, const SignalPayload &payload);
  void EmitTo(uint32_t signalHash,
              uint32_t sourceNodeIndex,
              uint32_t targetNodeIndex,
              const SignalPayload &payload);

  // Drains the queue, dispatching to subscribers in BehaviorRegistry. Re-emits
  // append to the queue and are drained in the same call until empty (bounded
  // by MAX_DISPATCH_ROUNDS * MAX_SIGNALS_PER_FRAME; on overflow logs once).
  void Dispatch(BehaviorRegistry &reg, SceneTree &tree, game::GameContext *gameCtx);

  void Clear();

  uint16_t GetPendingCount() const { return m_count; }

private:
  bool Enqueue(uint32_t hash,
               uint32_t sourceNodeIndex,
               uint32_t targetNodeIndex,
               const SignalPayload &payload);

  QueuedSignal m_queue[MAX_SIGNALS_PER_FRAME];
  uint16_t m_count = 0;
};

} // namespace engine

#endif
