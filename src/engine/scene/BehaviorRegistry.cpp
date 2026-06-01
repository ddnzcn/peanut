#include "engine/scene/BehaviorRegistry.hpp"

#include "atlas2d/AtlasPack.hpp"
#include "engine/scene/PscnLoader.hpp"
#include "engine/scene/PscnTypes.hpp"
#include "engine/scene/SceneTree.hpp"

#include <cstdio>
#include <cstring>

namespace engine
{

bool BehaviorRegistry::Register(const BehaviorVTable &vt)
{
  if (m_vtableCount >= MAX_BEHAVIOR_VTABLES)
  {
    std::printf("[BehaviorRegistry] vtable table full (>%u)\n",
                static_cast<unsigned>(MAX_BEHAVIOR_VTABLES));
    return false;
  }

  // Reject duplicate name hashes (0 = auto-attach-only; multiple of those are allowed).
  if (vt.nameHash != 0 && FindVTable(vt.nameHash) >= 0)
  {
    std::printf("[BehaviorRegistry] duplicate vtable nameHash=0x%08x\n",
                static_cast<unsigned>(vt.nameHash));
    return false;
  }

  m_vtables[m_vtableCount++] = vt;
  return true;
}

int16_t BehaviorRegistry::FindVTable(uint32_t nameHash) const
{
  for (uint16_t i = 0; i < m_vtableCount; ++i)
  {
    if (m_vtables[i].nameHash == nameHash)
    {
      return static_cast<int16_t>(i);
    }
  }
  return -1;
}

bool BehaviorRegistry::HasInstance(uint32_t nodeIndex, uint16_t vtableIndex) const
{
  for (uint16_t i = 0; i < m_instanceCount; ++i)
  {
    if (m_instances[i].nodeIndex == nodeIndex &&
        m_instances[i].vtableIndex == vtableIndex)
    {
      return true;
    }
  }
  return false;
}

void BehaviorRegistry::BindScene(SceneTree &tree, const PscnFile &file)
{
  ClearBindings();

  const uint16_t nodeCount = tree.GetNodeCount();

  // Pass 1: scriptId-named bindings.
  for (uint16_t i = 0; i < nodeCount; ++i)
  {
    const SceneNode *node = tree.GetNode(i);
    if (!node)
    {
      continue;
    }
    if (node->scriptIdStringIndex == PSCN_STRING_NONE)
    {
      continue;
    }

    const char *behaviorId = file.GetString(node->scriptIdStringIndex);
    if (!behaviorId)
    {
      continue;
    }

    const uint32_t hash = atlas2d::FNV1a32(behaviorId);
    const int16_t vtIdx = FindVTable(hash);
    if (vtIdx < 0)
    {
      continue;
    }

    if (m_instanceCount >= MAX_BEHAVIOR_INSTANCES)
    {
      std::printf("[BehaviorRegistry] instance table full\n");
      return;
    }

    const BehaviorVTable &vt = m_vtables[vtIdx];

    BehaviorInstance &inst = m_instances[m_instanceCount];
    inst.nodeIndex = i;
    inst.vtableIndex = static_cast<uint16_t>(vtIdx);
    inst.subCount = 0;
    inst.stateOffset = BEHAVIOR_STATE_NONE;
    std::memset(inst.subs, 0, sizeof(inst.subs));

    if (vt.stateSize > 0)
    {
      uint16_t offset = 0;
      if (tree.ClaimBehaviorState(vt.stateSize, &offset))
      {
        inst.stateOffset = offset;
      }
      else
      {
        std::printf("[BehaviorRegistry] arena overflow for behavior 0x%08x; skipping\n",
                    static_cast<unsigned>(vt.nameHash));
        continue;
      }
    }

    ++m_instanceCount;
  }

  // Pass 2: auto-attach by node type.
  for (uint16_t v = 0; v < m_vtableCount; ++v)
  {
    const BehaviorVTable &vt = m_vtables[v];
    if (vt.autoAttachNodeType == BEHAVIOR_AUTO_ATTACH_NONE)
    {
      continue;
    }

    for (uint16_t i = 0; i < nodeCount; ++i)
    {
      const SceneNode *node = tree.GetNode(i);
      if (!node || node->nodeType != vt.autoAttachNodeType)
      {
        continue;
      }
      if (HasInstance(i, v))
      {
        continue;
      }

      if (m_instanceCount >= MAX_BEHAVIOR_INSTANCES)
      {
        std::printf("[BehaviorRegistry] instance table full\n");
        return;
      }

      BehaviorInstance &inst = m_instances[m_instanceCount];
      inst.nodeIndex = i;
      inst.vtableIndex = v;
      inst.subCount = 0;
      inst.stateOffset = BEHAVIOR_STATE_NONE;
      std::memset(inst.subs, 0, sizeof(inst.subs));

      if (vt.stateSize > 0)
      {
        uint16_t offset = 0;
        if (tree.ClaimBehaviorState(vt.stateSize, &offset))
        {
          inst.stateOffset = offset;
        }
        else
        {
          std::printf("[BehaviorRegistry] arena overflow (auto-attach 0x%08x); skipping\n",
                      static_cast<unsigned>(vt.nameHash));
          continue;
        }
      }

      ++m_instanceCount;
    }
  }
}

void BehaviorRegistry::InitAll(SceneTree &tree, const PscnFile &file, game::GameContext *gameCtx)
{
  for (uint16_t i = 0; i < m_instanceCount; ++i)
  {
    BehaviorInstance &inst = m_instances[i];
    const BehaviorVTable &vt = m_vtables[inst.vtableIndex];
    if (!vt.onInit)
    {
      continue;
    }

    SceneNode *node = tree.GetNode(inst.nodeIndex);
    void *state = (inst.stateOffset != BEHAVIOR_STATE_NONE)
                      ? (tree.GetBehaviorStateArena() + inst.stateOffset)
                      : nullptr;

    const char *behaviorData = nullptr;
    if (node && node->scriptDataStringIndex != PSCN_STRING_NONE)
    {
      behaviorData = file.GetString(node->scriptDataStringIndex);
    }

    vt.onInit(i, inst.nodeIndex, node, state, behaviorData, gameCtx);
  }
}

void BehaviorRegistry::UpdateAll(SceneTree &tree, game::GameContext *gameCtx)
{
  for (uint16_t i = 0; i < m_instanceCount; ++i)
  {
    BehaviorInstance &inst = m_instances[i];
    if (inst.destroyed)
    {
      continue;
    }
    if (tree.IsNodeDead(inst.nodeIndex))
    {
      continue; // ProcessDeadNodes will fire onDestroy at end-of-tick
    }

    const BehaviorVTable &vt = m_vtables[inst.vtableIndex];
    if (!vt.onUpdate)
    {
      continue;
    }

    SceneNode *node = tree.GetNode(inst.nodeIndex);
    void *state = (inst.stateOffset != BEHAVIOR_STATE_NONE)
                      ? (tree.GetBehaviorStateArena() + inst.stateOffset)
                      : nullptr;

    vt.onUpdate(i, inst.nodeIndex, node, state, gameCtx);
  }
}

void BehaviorRegistry::DestroyAll(SceneTree &tree, game::GameContext *gameCtx)
{
  for (uint16_t i = 0; i < m_instanceCount; ++i)
  {
    BehaviorInstance &inst = m_instances[i];
    if (inst.destroyed)
    {
      continue;
    }

    const BehaviorVTable &vt = m_vtables[inst.vtableIndex];
    SceneNode *node = tree.GetNode(inst.nodeIndex);
    void *state = (inst.stateOffset != BEHAVIOR_STATE_NONE)
                      ? (tree.GetBehaviorStateArena() + inst.stateOffset)
                      : nullptr;

    if (vt.onDestroy)
    {
      vt.onDestroy(i, inst.nodeIndex, node, state, gameCtx);
    }
    inst.destroyed = 1;
  }
}

void BehaviorRegistry::ProcessDeadNodes(SceneTree &tree, game::GameContext *gameCtx)
{
  for (uint16_t i = 0; i < m_instanceCount; ++i)
  {
    BehaviorInstance &inst = m_instances[i];
    if (inst.destroyed)
    {
      continue;
    }
    if (!tree.IsNodeDead(inst.nodeIndex))
    {
      continue;
    }

    const BehaviorVTable &vt = m_vtables[inst.vtableIndex];
    SceneNode *node = tree.GetNode(inst.nodeIndex);
    void *state = (inst.stateOffset != BEHAVIOR_STATE_NONE)
                      ? (tree.GetBehaviorStateArena() + inst.stateOffset)
                      : nullptr;

    if (vt.onDestroy)
    {
      vt.onDestroy(i, inst.nodeIndex, node, state, gameCtx);
    }
    inst.destroyed = 1;
  }
}

void BehaviorRegistry::Clear()
{
  for (uint16_t i = 0; i < m_vtableCount; ++i)
  {
    m_vtables[i] = BehaviorVTable{};
  }
  for (uint16_t i = 0; i < m_instanceCount; ++i)
  {
    m_instances[i] = BehaviorInstance{};
  }
  m_vtableCount = 0;
  m_instanceCount = 0;
}

void BehaviorRegistry::ClearBindings()
{
  for (uint16_t i = 0; i < m_instanceCount; ++i)
  {
    m_instances[i] = BehaviorInstance{};
  }
  m_instanceCount = 0;
}

bool BehaviorRegistry::Subscribe(uint16_t bindingIndex, uint32_t signalHash)
{
  if (bindingIndex >= m_instanceCount || signalHash == 0)
  {
    return false;
  }

  BehaviorInstance &inst = m_instances[bindingIndex];
  // Reject duplicates.
  for (uint8_t s = 0; s < inst.subCount; ++s)
  {
    if (inst.subs[s] == signalHash)
    {
      return false;
    }
  }
  if (inst.subCount >= MAX_SUBS_PER_INSTANCE)
  {
    std::printf("[BehaviorRegistry] instance %u: sub list full\n",
                static_cast<unsigned>(bindingIndex));
    return false;
  }
  inst.subs[inst.subCount++] = signalHash;
  return true;
}

const BehaviorVTable *BehaviorRegistry::GetVTable(uint16_t index) const
{
  if (index >= m_vtableCount)
  {
    return nullptr;
  }
  return &m_vtables[index];
}

const BehaviorInstance *BehaviorRegistry::GetInstance(uint16_t index) const
{
  if (index >= m_instanceCount)
  {
    return nullptr;
  }
  return &m_instances[index];
}

} // namespace engine
