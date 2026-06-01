# Game Layer

This is the surface area your game code lives in. Five things to learn:

1. `GameConfig` — what assets to load at boot
2. `Configure()` — return your `GameConfig`
3. `Init(ctx)` — one-time engine startup
4. `OnSceneLoaded(ctx)` — runs after every scene build
5. `Update(ctx)` — per-frame game logic
6. `Shutdown(ctx)` — engine teardown
7. `GameContext` — the live state every callback sees

Plus the entry point you register behaviors in: `RegisterAllBehaviors(reg)`.

## File layout

```
include/game/
  Game.hpp            Hook declarations + GameConfig
  GameContext.hpp     Live state struct
  Behaviors.hpp       RegisterAllBehaviors + S<T> helper

src/game/
  Game.cpp            Implementations of Configure / Init / OnSceneLoaded / Update / Shutdown
  Behaviors.cpp       Behavior vtables, state structs, RegisterAllBehaviors body
```

`include/engine/engine.hpp` includes `game/Game.hpp`, so the engine sees these declarations. Game code never `#include`s engine internals like `engine/scene/BehaviorRegistry.hpp` directly except in `Behaviors.cpp`.

## `GameConfig`

Picks initial assets. Returned by `Configure()` once at boot.

```cpp
struct GameConfig {
  const char* atlasMetaPath = "atlas.meta.bin";
  const char* atlasDataPath = "atlas.bin";
  const char* scenePath     = "scene.pscn.bin";
  uint16_t    atlasPageIndex = 0;
};
```

Paths are relative; the engine wraps them with `platform::ResolveAssetPath` so they pick up the device + root configured at build time (see [getting-started.md](getting-started.md)).

```cpp
GameConfig game::Configure()
{
  GameConfig cfg;
  cfg.atlasMetaPath = "atlas.meta.bin";
  cfg.atlasDataPath = "atlas.bin";
  cfg.scenePath     = "base0.pscn.bin";
  cfg.atlasPageIndex = 0;
  return cfg;
}
```

## Hooks

### `void Init(GameContext& ctx)` — one-time

Called once at engine startup, **before any scene is loaded**. Use it for things that don't depend on scene contents:

- Default render config (`ctx.renderScale`, `ctx.clearR/G/B`)
- Tuning constants (`ctx.cameraSpeed`)
- **Registering behavior vtables** via `ctx.behaviorRegistry`

Do NOT query the scene here — `ctx.sceneTree` is valid (it's a pointer to an empty `SceneTree`), but no nodes exist yet.

```cpp
void game::Init(GameContext& ctx)
{
  ctx.renderScale = 2.0f;
  ctx.cameraSpeed = 2;
  ctx.clearR = 8;  ctx.clearG = 16;  ctx.clearB = 32;

  if (ctx.behaviorRegistry) {
    RegisterAllBehaviors(*ctx.behaviorRegistry);
  }
}
```

### `void OnSceneLoaded(GameContext& ctx)` — per scene

Called **after every scene build** (initial load and every swap), with the new tree fully populated but before behaviors have been bound. Use it for:

- Deriving world bounds for camera clamping
- Resetting the camera so it doesn't carry across scenes
- Picking a render scale specific to this scene
- Finding canonical nodes you'll reference later (boss, spawn point, etc.)

```cpp
void game::OnSceneLoaded(GameContext& ctx)
{
  if (!ctx.sceneTree) return;

  ctx.cameraX = 0;  ctx.cameraY = 0;
  ctx.worldWidthPx = 0;  ctx.worldHeightPx = 0;

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

### `void Update(GameContext& ctx)` — every frame

Global game logic that isn't a behavior. Runs **after** all behavior `onUpdate` calls and **after** `ComputeWorldTransforms`, so it reads fresh world positions.

The canonical example: camera follow + edge clamping. The camera isn't tied to a specific node, so making it a behavior would be awkward.

```cpp
void game::Update(GameContext& ctx)
{
  if (!ctx.sceneTree) return;

  const auto* player = ctx.sceneTree->FindNodeByScriptId("player");
  if (player) {
    const float invScale = (ctx.renderScale > 0.0f) ? 1.0f / ctx.renderScale : 1.0f;
    const int halfW = int(float(ctx.viewW) * invScale * 0.5f);
    const int halfH = int(float(ctx.viewH) * invScale * 0.5f);

    ctx.cameraX = (player->world.worldX >> 16) - halfW;
    ctx.cameraY = (player->world.worldY >> 16) - halfH;

    if (ctx.worldWidthPx > 0) {
      const int viewW = int(float(ctx.viewW) * invScale);
      const int maxX = (ctx.worldWidthPx > viewW) ? ctx.worldWidthPx - viewW : 0;
      if (ctx.cameraX < 0) ctx.cameraX = 0;
      if (ctx.cameraX > maxX) ctx.cameraX = maxX;
    }
    // …repeat for Y…
  }
}
```

### `void Shutdown(GameContext& ctx)` — one-time

Called as the engine tears down, before `behaviorRegistry.DestroyAll`. Free anything your game allocated outside the per-binding state arena (rare).

```cpp
void game::Shutdown(GameContext&) { /* usually empty */ }
```

## `GameContext`

The live state struct every hook + behavior sees. Engine-owned fields and game-owned fields share the same struct.

### Engine-set (read-only from the game's perspective in most cases)

| Field | Type | Set by | Meaning |
|---|---|---|---|
| `sceneTree` | `engine::SceneTree*` | engine, once | Current scene |
| `behaviorRegistry` | `engine::BehaviorRegistry*` | engine, once | Vtable + instance store |
| `signalBus` | `engine::SignalBus*` | engine, once | Same as `&sceneTree->GetSignalBus()` |
| `padButtons` | `uint16_t` | engine, every frame | Held-bitmask (e.g. `PAD_LEFT` from `<libpad.h>`) |
| `viewW`, `viewH` | `int` | engine, every frame | Screen size in pixels |

### Game-set

| Field | Type | Default | Meaning |
|---|---|---|---|
| `renderScale` | `float` | `4.0` | Multiplier from world pixels to screen pixels |
| `cameraX`, `cameraY` | `int` | `0` | World-space scroll offset (top-left of view, in world pixels) |
| `cameraSpeed` | `int` | `2` | Used by the player example |
| `worldWidthPx`, `worldHeightPx` | `int` | `0` | Map extents for camera clamping; `0` = unbounded |
| `clearR/G/B` | `uint8_t` | `8/16/32` | Background color |
| `debugTileGrid`, `debugChunkGrid` | `bool` | `false` | (Currently unused; reserved for debug overlay) |

### Scene/atlas swap requests (write to trigger end-of-tick swap)

| Field | Type | Engine action |
|---|---|---|
| `requestedScene` | `const char*` | Swap to this scene at end of tick |
| `requestedAtlasMeta` + `requestedAtlasData` | `const char*` × 2 | Swap atlas at end of tick, before scene swap |
| `requestedAtlasPage` | `uint16_t` | Page index for the new atlas (default `0`) |

The engine **captures** these into locals before processing, then nulls the fields. A handler running during the swap can immediately request another swap for the next frame — it won't recurse within the current tick.

See [scene-management.md](scene-management.md) for details and examples.

## `RegisterAllBehaviors`

The single entry point where every vtable gets installed. Called from `game::Init` via `ctx.behaviorRegistry`.

```cpp
// include/game/Behaviors.hpp
namespace game {
  template <typename T> inline T& S(void* p) { return *static_cast<T*>(p); }
  void RegisterAllBehaviors(engine::BehaviorRegistry& reg);
}

// src/game/Behaviors.cpp
void game::RegisterAllBehaviors(engine::BehaviorRegistry& reg)
{
  engine::BehaviorVTable vt = {};
  vt.nameHash = atlas2d::FNV1a32("player");
  vt.stateSize = sizeof(PlayerState);
  vt.autoAttachNodeType = engine::BEHAVIOR_AUTO_ATTACH_NONE;
  vt.onInit    = PlayerInit;
  vt.onUpdate  = PlayerUpdate;
  vt.onDestroy = nullptr;
  vt.onSignal  = PlayerOnSignal;
  reg.Register(vt);

  // … more vtables …
}
```

`Register` rejects duplicate name hashes (returns `false`) but allows multiple zero-hash vtables (those are auto-attach-only). See [behaviors.md](behaviors.md).

## The `S<T>` helper

A two-line template that hides the cast from `void*` state slot to your typed state struct. Zero runtime cost.

```cpp
template <typename T> inline T& S(void* p) { return *static_cast<T*>(p); }

// usage:
void PlayerUpdate(uint16_t, uint32_t, engine::SceneNode* node, void* state, game::GameContext* ctx)
{
  auto& s = S<PlayerState>(state);
  // …read/write s.speedFixed, s.activeAnim, etc.
}
```

By convention, name the alias `s` so handlers stay readable.

## Putting it together: minimal `Game.cpp`

```cpp
#include "game/Game.hpp"
#include "game/Behaviors.hpp"
#include "engine/scene/BehaviorRegistry.hpp"

namespace game {

GameConfig Configure() {
  GameConfig cfg;
  cfg.scenePath = "main.pscn.bin";
  return cfg;
}

void Init(GameContext& ctx) {
  ctx.renderScale = 2.0f;
  if (ctx.behaviorRegistry) RegisterAllBehaviors(*ctx.behaviorRegistry);
}

void OnSceneLoaded(GameContext& ctx) {
  ctx.cameraX = 0;
  ctx.cameraY = 0;
}

void Update(GameContext&) {}
void Shutdown(GameContext&) {}

} // namespace game
```

That's a complete, minimum-viable game-side wiring. Add behaviors in `Behaviors.cpp` and you have a game.
