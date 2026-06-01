# Scene Management

Three runtime operations let you change what's loaded without restarting the engine:

- **Swap the scene** — change levels.
- **Swap the atlas** — change art (usually paired with a scene swap).
- **Remove a node** — delete a sprite, an enemy, a pickup, an entire subtree.

All three are non-destructive of the engine state and safe to trigger from any handler.

## Mental model

Game code requests the change by writing fields on `GameContext`. The engine consumes those requests **at end of tick**, after render+flip. This guarantees:

- The current frame finishes rendering cleanly.
- No handler can recurse into a second swap mid-tick.
- Behaviors run their final `onUpdate` and `onDestroy` against a stable scene before the swap happens.

The next frame starts with the new state.

## Swap scenes

The simplest request:

```cpp
void SomeHandler(/* … */, game::GameContext* ctx)
{
  ctx->requestedScene = "level2.pscn.bin";
}
```

`requestedScene` must outlive the current frame. String literals are fine. If you need to compute the path dynamically, store it in your own persistent buffer (a static char array, a member of GameContext you own, etc.).

### What happens at end of tick

```
1. capture path; null ctx.requestedScene (so a behavior can request another swap for next frame)
2. behaviorRegistry.DestroyAll(tree, ctx)   ← every binding's onDestroy fires
3. behaviorRegistry.ClearBindings()         ← instances wiped; vtables KEPT
4. sceneTree.Clear()                        ← nodes + signal bus + behavior arena reset
5. pscnFile.Clear()
6. pscnFile.Load(newPath)                   ← if it fails, engine logs and bails (empty scene)
7. sceneTree.BuildFromPscn()
8. game::OnSceneLoaded(ctx)                 ← per-scene game setup
9. behaviorRegistry.BindScene(tree, file)   ← rebind by scriptId + auto-attach
10. behaviorRegistry.InitAll(...)           ← onInit fires for each new binding
11. signalBus.Emit("scene.ready", BROADCAST, EMPTY)
```

Critical: **vtables stay registered.** You called `RegisterAllBehaviors` once in `game::Init`; you don't repeat that work on every swap. Only the *instances* and their state are recreated for the new scene.

### Failure handling

If `pscnFile.Load` or `BuildFromPscn` fails, the engine logs to the EE console and leaves itself in an empty-scene state (no nodes, no bindings). The render loop tolerates that (nothing to draw). Game code can detect this by querying `ctx.sceneTree->GetNodeCount() == 0` and request a fallback scene.

### What `OnSceneLoaded` should reset

Anything scene-specific:

```cpp
void game::OnSceneLoaded(GameContext& ctx)
{
  if (!ctx.sceneTree) return;

  // Camera doesn't carry across scenes.
  ctx.cameraX = 0;
  ctx.cameraY = 0;

  // Re-derive world bounds from the new background.
  ctx.worldWidthPx = 0;
  ctx.worldHeightPx = 0;

  const auto* bg = ctx.sceneTree->FindNodeByScriptId("background");
  if (bg) {
    const auto* tm = ctx.sceneTree->GetExtensionAs<engine::PscnTileMapExt>(*bg);
    if (tm) {
      ctx.worldWidthPx  = int(tm->mapWidthTiles)  * int(tm->tileWidth);
      ctx.worldHeightPx = int(tm->mapHeightTiles) * int(tm->tileHeight);
    }
  }
}
```

## Swap the atlas

Pair an atlas swap with a scene swap when different levels use different art:

```cpp
ctx->requestedAtlasMeta = "level2.atlas.meta.bin";
ctx->requestedAtlasData = "level2.atlas.bin";
ctx->requestedAtlasPage = 0;
ctx->requestedScene     = "level2.pscn.bin";
```

The engine swaps atlas **first**, then scene, so the new scene resolves sprite IDs from the new atlas.

### What happens at end of tick

```
1. capture & null all three atlas request fields
2. atlasPack.Clear()
3. atlasPack.Load(newMeta, newData)        ← if fails, logs and aborts (no scene swap occurs)
4. UploadAtlasPage(pageIndex)               ← VRAM reused if size fits, re-allocated otherwise
```

If a scene swap was also requested, it runs after the atlas finishes.

You can also swap the atlas alone (without a scene swap) — useful for swapping a character palette or unlocking new tilesets mid-level.

### VRAM allocation

The engine allocates VRAM through `gsKit_vram_alloc(GSKIT_ALLOC_USERBUFFER)` and reuses the slot if the new atlas page fits. If the new page is larger, a new slot is allocated; the old allocation is leaked (gsKit's bump allocator has no per-block free). Avoid loading larger atlases mid-game if you can.

## Remove a node

Mark a node and its entire descendant subtree dead. The engine handles render skipping, behavior skipping, and `onDestroy` calls automatically.

```cpp
ctx->sceneTree->RemoveNode(enemyNodeIndex);
```

### What happens

Immediately when you call `RemoveNode`:

- `NODE_FLAG_RUNTIME_DEAD` is set on the node.
- A single forward pass through the node array marks every descendant (any node whose parent chain leads to the removed node) with the same flag.

From this point on, in the current and all future frames:

- `behaviorRegistry.UpdateAll` skips bindings on dead nodes (their `onUpdate` does NOT run again).
- `signalBus.Dispatch` skips bindings on dead nodes (subscribers don't receive signals after the node dies).
- The renderer skips dead nodes (no further draws).

At end of the same tick, after `Dispatch`:

- `behaviorRegistry.ProcessDeadNodes` walks the instance array and, for each binding on a now-dead node whose `destroyed` flag isn't set, calls `onDestroy(binding, node, state, ctx)` and sets the flag.

So `onDestroy` fires **once**, **on the same frame as the removal**, **after dispatch and before render of the next frame**.

### Cascading removes

If an `onDestroy` handler calls `RemoveNode` on another node, that node's `onDestroy` runs the **next** frame (not the current one). `ProcessDeadNodes` is single-pass for predictability. If you need cascading removal in one frame, mark the nodes dead synchronously without leaning on `onDestroy` chains.

### What `RemoveNode` does NOT do

- It does NOT compact the node array. Indices remain valid; any cached `nodeIndex` you stored elsewhere still resolves to the same `SceneNode*` (now flagged dead).
- It does NOT reclaim the binding's state slot in the arena. Dead bindings keep their state until the next scene clear. Predictable, bounded, fine for normal gameplay. **Don't spawn and destroy thousands of nodes per frame** — the arena will fill up.
- It does NOT free the PSCN extension data — extensions are read-only and live in a separate blob.
- It does NOT remove from the render order array. The render loop just skips dead nodes.

### Detecting dead nodes

```cpp
if (ctx->sceneTree->IsNodeDead(nodeIndex)) {
  // Skip whatever you were going to do with this node.
}
```

Out-of-range indices return `true` (defensive — safe to call with garbage).

## Ordering rules

When multiple requests are pending at end of tick:

1. **Atlas swap** (if `requestedAtlasMeta` and `requestedAtlasData` are both set)
2. **Scene swap** (if `requestedScene` is set)

A scene swap always destroys all bindings; you don't need to separately call `RemoveNode` before swapping. Node removal is for mid-scene removal of individual entities, not for scene transitions.

## Caveats

- **Pointers on GameContext stay valid across all of this.** `ctx.sceneTree` is the same `SceneTree` instance (the engine clears it and reuses it); `ctx.signalBus` is a member of it; `ctx.behaviorRegistry` is the same engine-owned object.
- **String pointers in requests must outlive the frame.** Stack-local strings are unsafe. String literals are fine. Buffers stored on `GameContext` are fine.
- **A behavior's state does NOT survive a scene swap.** If you need persistent values (player save, level progress), keep them on `GameContext` or in your own statics — set them in `Init` and read them in `OnSceneLoaded` to re-spawn the player.
- **Atlas swap clears the entire `AtlasPack`.** Any spriteId resolved against the old atlas is stale after the swap. The renderer re-resolves per frame, so this is automatic — but if game code caches `AtlasSprite*` pointers, refresh them in `OnSceneLoaded`.

## Worked examples

### "On door touched, go to next level"

```cpp
void DoorOnSignal(uint16_t, uint32_t nodeIndex, engine::SceneNode*, void*,
                  uint32_t hash, uint32_t sourceNodeIndex,
                  const engine::SignalPayload*, game::GameContext* ctx)
{
  // Assume some collision behavior emits "player.touched" with the door as target.
  if (hash == engine::signal::Hash("player.touched") &&
      sourceNodeIndex /* … the right source … */)
  {
    ctx->requestedScene = "level2.pscn.bin";
  }
}
```

### "Boss died — spawn next boss after fade"

```cpp
void BossOnSignal(uint16_t, uint32_t nodeIndex, engine::SceneNode*, void* state,
                  uint32_t hash, uint32_t,
                  const engine::SignalPayload*, game::GameContext* ctx)
{
  if (hash == engine::signal::Hash("boss.died")) {
    ctx->sceneTree->RemoveNode(nodeIndex);  // hide the boss now
    // Schedule next boss spawn via game::Update flag or another signal at end of fade.
  }
}
```

### "Switch art on level transition"

```cpp
ctx->requestedAtlasMeta = "ice_world.atlas.meta.bin";
ctx->requestedAtlasData = "ice_world.atlas.bin";
ctx->requestedScene     = "ice_world_01.pscn.bin";
```

One line each; the engine does the teardown, atlas swap, scene swap, and rebind in order.
