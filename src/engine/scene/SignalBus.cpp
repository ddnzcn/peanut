#include "engine/scene/SignalBus.hpp"

#include "engine/scene/BehaviorRegistry.hpp"
#include "engine/scene/SceneTree.hpp"

#include <cstdio>
#include <cstring>

namespace engine
{

void SignalBus::Emit(uint32_t signalHash,
                     uint32_t sourceNodeIndex,
                     const SignalPayload &payload)
{
  Enqueue(signalHash, sourceNodeIndex, SIGNAL_TARGET_BROADCAST, payload);
}

void SignalBus::EmitTo(uint32_t signalHash,
                       uint32_t sourceNodeIndex,
                       uint32_t targetNodeIndex,
                       const SignalPayload &payload)
{
  Enqueue(signalHash, sourceNodeIndex, targetNodeIndex, payload);
}

bool SignalBus::Enqueue(uint32_t hash,
                        uint32_t sourceNodeIndex,
                        uint32_t targetNodeIndex,
                        const SignalPayload &payload)
{
  if (m_count >= MAX_SIGNALS_PER_FRAME)
  {
    static bool warned = false;
    if (!warned)
    {
      std::printf("[SignalBus] queue full (>%u), dropping signal 0x%08x\n",
                  static_cast<unsigned>(MAX_SIGNALS_PER_FRAME),
                  static_cast<unsigned>(hash));
      warned = true;
    }
    return false;
  }

  QueuedSignal &slot = m_queue[m_count++];
  slot.hash = hash;
  slot.sourceNodeIndex = sourceNodeIndex;
  slot.targetNodeIndex = targetNodeIndex;
  slot.payload = payload;
  return true;
}

void SignalBus::Dispatch(BehaviorRegistry &reg, SceneTree &tree, game::GameContext *gameCtx)
{
  const uint32_t safetyCap =
      static_cast<uint32_t>(MAX_DISPATCH_ROUNDS) * static_cast<uint32_t>(MAX_SIGNALS_PER_FRAME);
  uint32_t processed = 0;

  // Drain to empty, re-emits append while we iterate.
  uint16_t cursor = 0;
  while (cursor < m_count)
  {
    if (processed >= safetyCap)
    {
      static bool warned = false;
      if (!warned)
      {
        std::printf("[SignalBus] dispatch cutoff hit (>%u signals); aborting drain\n",
                    static_cast<unsigned>(safetyCap));
        warned = true;
      }
      break;
    }

    const QueuedSignal qs = m_queue[cursor++];
    ++processed;

    const uint16_t instCount = reg.GetInstanceCount();
    for (uint16_t i = 0; i < instCount; ++i)
    {
      const BehaviorInstance *inst = reg.GetInstance(i);
      if (!inst || inst->destroyed)
      {
        continue;
      }
      if (tree.IsNodeDead(inst->nodeIndex))
      {
        continue;
      }

      if (qs.targetNodeIndex != SIGNAL_TARGET_BROADCAST &&
          qs.targetNodeIndex != inst->nodeIndex)
      {
        continue;
      }

      bool subscribed = false;
      for (uint8_t s = 0; s < inst->subCount; ++s)
      {
        if (inst->subs[s] == qs.hash)
        {
          subscribed = true;
          break;
        }
      }
      if (!subscribed)
      {
        continue;
      }

      const BehaviorVTable *vt = reg.GetVTable(inst->vtableIndex);
      if (!vt || !vt->onSignal)
      {
        continue;
      }

      SceneNode *node = tree.GetNode(inst->nodeIndex);
      void *state = nullptr;
      if (inst->stateOffset != BEHAVIOR_STATE_NONE)
      {
        state = tree.GetBehaviorStateArena() + inst->stateOffset;
      }

      vt->onSignal(i,
                   inst->nodeIndex,
                   node,
                   state,
                   qs.hash,
                   qs.sourceNodeIndex,
                   &qs.payload,
                   gameCtx);
    }
  }

  m_count = 0;
}

void SignalBus::Clear()
{
  for (uint16_t i = 0; i < m_count; ++i)
  {
    m_queue[i] = QueuedSignal{};
  }
  m_count = 0;
}

} // namespace engine
