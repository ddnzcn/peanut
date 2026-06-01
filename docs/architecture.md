# Architecture

The engine/game boundary is the most important thing to internalize. Once you see what each side owns, the rest of the docs fall into place.

## Engine / game split

| Owned by **engine** | Owned by **game** |
|---|---|
| PS2 hardware init (DMA, GS, pad) | Asset paths and game config |
| PSCN file loading + validation | Behavior registration + per-instance state |
| Scene tree (nodes, world transforms, render order) | Per-scene setup (`OnSceneLoaded`) |
| Behavior dispatch (`InitAll`/`UpdateAll`/`DestroyAll`/`ProcessDeadNodes`) | Per-frame game logic that isn't a behavior (camera follow, etc.) |
| Signal bus + delivery | Reactions to signals via behavior `onSignal` |
| Atlas loading + VRAM upload + texture management | Choice of when to swap atlas / swap scene / remove nodes |
| Renderer (sprites, animated sprites, tilemaps, parallax, chunk culling) | Render-related config: `renderScale`, `clearR/G/B`, scene composition |
| Pad polling and edge detection | Held-input movement, edge reactions via signals |
| The tick loop itself | What happens inside the hooks the loop calls |

The boundary is enforced by **headers**. `engine/*` headers never `#include "game/..."`; game headers freely `#include "engine/..."`.

## Lifecycle

```
main()
  Engine engine;
  engine.run();
    initHardware()                   // DMA, gsKit, pad
    loadAssets(config)               // atlas + UploadAtlasPage
    wire gameCtx pointers            // sceneTree, behaviorRegistry, signalBus
    cache pad signal hashes
    game::Init(ctx)                  // ONE-TIME: behavior registrations + config
    swapScene(config.scenePath)      // loads scene, calls OnSceneLoaded, BindScene, InitAll, emits scene.ready
    while (running) {
      tick();                        // see Tick order below
    }
    shutdown();                      // DestroyAll, Clear everything, free pad/SIF
```

`shutdown()` order: `game::Shutdown(ctx)` → `behaviorRegistry.DestroyAll` → `behaviorRegistry.Clear` → `sceneTree.Clear` → `pscnFile.Clear` → `atlasPack.Clear` → release atlas page buffer → `ShutdownPad`.

## Tick order (per frame)

```
1. Read pad → save prev mask + new mask
2. Compute pressed/released edges
3. Emit pad.pressed / pad.released signals for each set edge bit
4. behaviorRegistry.UpdateAll(tree, ctx)        // every behavior's onUpdate
5. sceneTree.ComputeWorldTransforms()           // fresh world from updated locals
6. game::Update(ctx)                            // camera follow / clamping / global game logic
7. signalBus.Dispatch(reg, tree, ctx)           // drain queued signals; reentrant emits drain too
8. behaviorRegistry.ProcessDeadNodes(tree, ctx) // fire onDestroy for newly dead nodes
9. Clear screen → RenderScene → queue exec → sync flip
10. If ctx.requestedAtlasMeta/Data set: swapAtlas(...)
11. If ctx.requestedScene set: swapScene(...)
```

Two things to internalize:

- **`onUpdate` runs before `ComputeWorldTransforms`.** Behaviors modify `node->localX/Y`; the engine then computes world transforms before render and `game::Update`. So `game::Update` sees fresh world positions for the current frame.
- **Atlas / scene swaps happen at the end of the tick, after render.** A behavior or game code can request a swap any time; the engine consumes the request at end-of-tick, so swap teardown can't reenter the same frame's render or update.

## Memory model

All large per-scene buffers are fixed-size, allocated statically inside their owning class.

| What | Size | Where it lives |
|---|---|---|
| Scene nodes | `SceneNode[512]` ≈ 48 KB | `SceneTree::m_nodes` |
| PSCN extension blob | `uint8_t[8192]` = 8 KB | `SceneTree::m_extensionBlob` (read-only after load) |
| Behavior state arena | `uint8_t[32768]` = 32 KB | `SceneTree::m_behaviorStateArena` (runtime mutable) |
| Render order indices | `uint16_t[512]` = 1 KB | `SceneTree::m_renderOrder` |
| Signal queue | `QueuedSignal[128]` ≈ 4.5 KB | `SceneTree::m_signalBus` |
| Vtable table | `BehaviorVTable[64]` ≈ 2 KB | `BehaviorRegistry::m_vtables` |
| Instance table | `BehaviorInstance[512]` ≈ 26 KB | `BehaviorRegistry::m_instances` |
| Atlas page buffer | `width*height*4` bytes | `memalign(128, …)` in `engine.cpp` |
| gsKit oneshot queue | 4 MB | `gsKit_init_global_custom` |

**Total static-side footprint: ~120 KB out of 32 MB EE RAM.** The 4 MB gsKit queue dominates because it has to fit a frame's worth of textured quads — see [reference.md](reference.md) for tuning.

### Why the behavior state arena lives on `SceneTree`

`SceneTree` already owns the PSCN extension blob (read-only data attached to nodes from the on-disk format). Behavior state is the runtime-mutable equivalent — data attached to a node, accessed in the same hot paths as transforms. Co-locating them means dispatch reads node + state + transform out of the same memory neighbourhood.

`BehaviorRegistry` only stores the dispatch metadata (which vtable belongs to which node, what offset its state slot starts at). The state bytes themselves are in `SceneTree`. `SceneTree::Clear()` resets all three regions together.

## Pointers on `GameContext`

`GameContext` carries non-owning pointers that the engine sets up once at startup. They stay valid for the engine's lifetime (the underlying objects are members of `Engine`):

```cpp
engine::SceneTree*       sceneTree;          // current scene
engine::BehaviorRegistry* behaviorRegistry;  // vtables + instances
engine::SignalBus*       signalBus;          // owned by sceneTree
```

A scene swap doesn't invalidate these — it `Clear()`s the same `SceneTree` and rebuilds.

## What's NOT here (deferred to planned features)

- **First-class `Camera2D`** with a viewport and a scene-tree node. Today the "camera" is `cameraX/cameraY` on `GameContext` set each frame by `game::Update`. See [architecture-todo](../ps_2_jam_engine_architecture_todo_doc.md) for the planned design.
- **Camera-as-behavior** for split-screen / windowed views with scissor regions.
- **Asset hot-reload.** Atlas swap is end-of-tick; you can rebuild the binary from peanut-assman and request a swap, but there's no automatic re-detection.
- **Multi-page atlas streaming.** Engine uploads page 0 (or `GameConfig::atlasPageIndex`); multi-page support means changing the renderer to filter by page on the fly.

## When to use what

- **Need to react to scene events (player died, item collected, dialog opened)?** → [Signals](signals.md).
- **Need to keep state per node (this enemy's health, this NPC's dialog index)?** → [Behaviors](behaviors.md) with per-instance state.
- **Need to do something every frame regardless of nodes (camera, screen shake, world physics step)?** → `game::Update`. See [game-layer.md](game-layer.md).
- **Need to react when a node is created vs destroyed?** → `onInit` / `onDestroy` on a behavior bound to that node.
- **Need to change levels?** → Set `ctx.requestedScene` (and optionally `requestedAtlasMeta`/`requestedAtlasData`). See [scene-management.md](scene-management.md).
- **Need to read which buttons are held?** → `ctx.padButtons & PAD_LEFT`. **Pressed/released edges?** → subscribe to `pad.pressed` / `pad.released` signals. See [signals.md](signals.md).
