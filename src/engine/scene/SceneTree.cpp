#include "engine/scene/SceneTree.hpp"
#include "atlas2d/AtlasPack.hpp"

#include <cstring>

namespace engine
{

bool SceneTree::BuildFromPscn(const PscnFile &file)
{
  Clear();

  const uint16_t nodeCount = file.GetNodeCount();
  if (nodeCount == 0 || nodeCount > MAX_SCENE_NODES)
  {
    return false;
  }

  uint16_t extUsed = 0;

  for (uint16_t i = 0; i < nodeCount; ++i)
  {
    const PscnNodeBase *base = file.GetNodeBase(i);
    if (!base)
    {
      Clear();
      return false;
    }

    SceneNode &node = m_nodes[i];
    node.nodeId = base->nodeId;
    node.nameHash = base->nameHash;
    node.nodeType = base->nodeType;
    node.flags = base->flags;
    node.renderLayer = base->renderLayer;
    node.localX = base->posX;
    node.localY = base->posY;
    node.localRot = base->rotation;
    node.localScaleX = base->scaleX;
    node.localScaleY = base->scaleY;
    node.parentIndex = base->parentIndex;
    node.firstChildIndex = base->firstChildIndex;
    node.childCount = base->childCount;
    node.parallaxX = base->parallaxX;
    node.parallaxY = base->parallaxY;
    node.collisionLayer = base->collisionLayer;
    node.collisionMask = base->collisionMask;
    node.scriptIdStringIndex = base->scriptIdStringIndex;
    node.scriptDataStringIndex = base->scriptDataStringIndex;

    node.scriptIdHash = 0;
    if (base->scriptIdStringIndex != PSCN_STRING_NONE)
    {
      const char *scriptId = file.GetString(base->scriptIdStringIndex);
      if (scriptId)
      {
        node.scriptIdHash = atlas2d::FNV1a32(scriptId);
      }
    }

    if (base->extSize > 0)
    {
      const void *ext = file.GetNodeExtension(i);
      if (!ext)
      {
        Clear();
        return false;
      }

      if (static_cast<size_t>(extUsed) + base->extSize > MAX_EXTENSION_BYTES)
      {
        Clear();
        return false;
      }

      node.extOffset = extUsed;
      node.extSize = base->extSize;
      std::memcpy(m_extensionBlob + extUsed, ext, base->extSize);
      extUsed += base->extSize;
    }
    else
    {
      node.extOffset = 0;
      node.extSize = 0;
    }
  }

  m_nodeCount = nodeCount;
  m_extensionUsed = extUsed;

  BuildRenderOrder();
  ComputeWorldTransforms();

  return true;
}

void SceneTree::Clear()
{
  m_nodeCount = 0;
  m_extensionUsed = 0;
  m_behaviorArenaUsed = 0;
  m_signalBus.Clear();
}

bool SceneTree::IsNodeDead(uint32_t index) const
{
  if (index >= m_nodeCount)
  {
    return true; // out-of-range counts as dead so callers can defensively skip
  }
  return (m_nodes[index].flags & NODE_FLAG_RUNTIME_DEAD) != 0;
}

void SceneTree::RemoveNode(uint32_t index)
{
  if (index >= m_nodeCount)
  {
    return;
  }
  if ((m_nodes[index].flags & NODE_FLAG_RUNTIME_DEAD) != 0)
  {
    return; // already dead
  }

  m_nodes[index].flags |= NODE_FLAG_RUNTIME_DEAD;

  // PSCN guarantees pre-order serialization, so a node's descendants always
  // have a higher index AND their parent chain leads back through other
  // higher-indexed nodes already-marked dead in this same forward sweep.
  for (uint16_t i = static_cast<uint16_t>(index + 1); i < m_nodeCount; ++i)
  {
    const int32_t parentIdx = m_nodes[i].parentIndex;
    if (parentIdx < 0)
    {
      continue;
    }
    if ((m_nodes[parentIdx].flags & NODE_FLAG_RUNTIME_DEAD) != 0)
    {
      m_nodes[i].flags |= NODE_FLAG_RUNTIME_DEAD;
    }
  }
}

bool SceneTree::ClaimBehaviorState(uint16_t sizeBytes, uint16_t *outOffset)
{
  if (!outOffset || sizeBytes == 0)
  {
    return false;
  }
  const uint32_t end = static_cast<uint32_t>(m_behaviorArenaUsed) + static_cast<uint32_t>(sizeBytes);
  if (end > BEHAVIOR_STATE_ARENA)
  {
    return false;
  }
  *outOffset = m_behaviorArenaUsed;
  // Zero-init the claimed slice so new instances start clean.
  std::memset(m_behaviorStateArena + m_behaviorArenaUsed, 0, sizeBytes);
  m_behaviorArenaUsed = static_cast<uint16_t>(end);
  return true;
}

SceneNode *SceneTree::GetNode(uint32_t index)
{
  if (index >= m_nodeCount)
  {
    return nullptr;
  }
  return &m_nodes[index];
}

const SceneNode *SceneTree::GetNode(uint32_t index) const
{
  if (index >= m_nodeCount)
  {
    return nullptr;
  }
  return &m_nodes[index];
}

int32_t SceneTree::FindNodeIndexByScriptIdHash(uint32_t scriptIdHash) const
{
  if (scriptIdHash == 0)
  {
    return -1;
  }

  for (uint16_t i = 0; i < m_nodeCount; ++i)
  {
    if (m_nodes[i].scriptIdHash == scriptIdHash)
    {
      return static_cast<int32_t>(i);
    }
  }
  return -1;
}

int32_t SceneTree::FindNodeIndexByScriptId(const char *scriptId) const
{
  if (!scriptId)
  {
    return -1;
  }
  return FindNodeIndexByScriptIdHash(atlas2d::FNV1a32(scriptId));
}

SceneNode *SceneTree::FindNodeByScriptIdHash(uint32_t scriptIdHash)
{
  const int32_t idx = FindNodeIndexByScriptIdHash(scriptIdHash);
  return (idx >= 0) ? &m_nodes[idx] : nullptr;
}

const SceneNode *SceneTree::FindNodeByScriptIdHash(uint32_t scriptIdHash) const
{
  const int32_t idx = FindNodeIndexByScriptIdHash(scriptIdHash);
  return (idx >= 0) ? &m_nodes[idx] : nullptr;
}

SceneNode *SceneTree::FindNodeByScriptId(const char *scriptId)
{
  const int32_t idx = FindNodeIndexByScriptId(scriptId);
  return (idx >= 0) ? &m_nodes[idx] : nullptr;
}

const SceneNode *SceneTree::FindNodeByScriptId(const char *scriptId) const
{
  const int32_t idx = FindNodeIndexByScriptId(scriptId);
  return (idx >= 0) ? &m_nodes[idx] : nullptr;
}

const void *SceneTree::GetExtension(const SceneNode &node) const
{
  if (node.extSize == 0)
  {
    return nullptr;
  }
  return m_extensionBlob + node.extOffset;
}

const uint32_t *SceneTree::GetAnimNameHashes(const SceneNode &node, uint8_t *outCount) const
{
  if (outCount)
  {
    *outCount = 0;
  }

  if (node.nodeType != NODE_ANIMATED_SPRITE || node.extSize < sizeof(PscnAnimatedSpriteExt))
  {
    return nullptr;
  }

  const PscnAnimatedSpriteExt *ext =
      reinterpret_cast<const PscnAnimatedSpriteExt *>(m_extensionBlob + node.extOffset);

  if (ext->animCount == 0)
  {
    return nullptr;
  }

  const uint16_t expectedSize =
      static_cast<uint16_t>(sizeof(PscnAnimatedSpriteExt) +
                            static_cast<size_t>(ext->animCount) * sizeof(uint32_t));
  if (node.extSize < expectedSize)
  {
    return nullptr;
  }

  if (outCount)
  {
    *outCount = ext->animCount;
  }

  return reinterpret_cast<const uint32_t *>(
      m_extensionBlob + node.extOffset + sizeof(PscnAnimatedSpriteExt));
}

void SceneTree::ComputeWorldTransforms()
{
  for (uint16_t i = 0; i < m_nodeCount; ++i)
  {
    SceneNode &node = m_nodes[i];

    if (node.parentIndex < 0)
    {
      node.world.worldX = node.localX;
      node.world.worldY = node.localY;
      node.world.worldRot = node.localRot;
      node.world.worldScaleX = node.localScaleX;
      node.world.worldScaleY = node.localScaleY;
      continue;
    }

    const SceneNode &parent = m_nodes[node.parentIndex];

    // Scale: (parentScale * localScale) >> 8 for 8.8 fixed-point
    node.world.worldScaleX =
        static_cast<int16_t>((static_cast<int32_t>(parent.world.worldScaleX) *
                              static_cast<int32_t>(node.localScaleX)) >>
                             8);
    node.world.worldScaleY =
        static_cast<int16_t>((static_cast<int32_t>(parent.world.worldScaleY) *
                              static_cast<int32_t>(node.localScaleY)) >>
                             8);

    // Position: parent world pos + (local pos * parent world scale) >> 8
    // localX/Y are 16.16, parent scale is 8.8, so shift by 8
    node.world.worldX =
        parent.world.worldX +
        static_cast<int32_t>(
            (static_cast<int64_t>(node.localX) *
             static_cast<int64_t>(parent.world.worldScaleX)) >>
            8);
    node.world.worldY =
        parent.world.worldY +
        static_cast<int32_t>(
            (static_cast<int64_t>(node.localY) *
             static_cast<int64_t>(parent.world.worldScaleY)) >>
            8);

    // Rotation: sum
    node.world.worldRot =
        static_cast<int16_t>(parent.world.worldRot + node.localRot);
  }
}

void SceneTree::BuildRenderOrder()
{
  for (uint16_t i = 0; i < m_nodeCount; ++i)
  {
    m_renderOrder[i] = i;
  }

  // Insertion sort by renderLayer (stable, small N)
  for (uint16_t i = 1; i < m_nodeCount; ++i)
  {
    const uint16_t key = m_renderOrder[i];
    const uint16_t keyLayer = m_nodes[key].renderLayer;
    int j = static_cast<int>(i) - 1;
    while (j >= 0 && m_nodes[m_renderOrder[j]].renderLayer > keyLayer)
    {
      m_renderOrder[j + 1] = m_renderOrder[j];
      --j;
    }
    m_renderOrder[j + 1] = key;
  }
}

} // namespace engine
