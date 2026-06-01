# Peanut — PS2 2D Game Engine

A C++17 PS2SDK game engine for 2D tile-based games. Scene graph rendering, chunk-based tilemaps, sprite animations, and a native script system designed for QuickJS integration later.

Detailed docs can be found at [docs folder](./docs/getting-started.md)
## Layout

```
include/
  engine/
    engine.hpp                  Engine loop, init, tick, shutdown
    scene/
      PscnTypes.hpp             PSCN binary format structs
      PscnLoader.hpp            Scene file loader
      SceneTree.hpp             Runtime node tree + world transforms
      ScriptRegistry.hpp        Script handler dispatch
      SceneRenderer.hpp         Scene graph renderer
  atlas2d/
    AtlasPack.hpp               Atlas binary loader (sprites, anims)
    AtlasPackUtils.hpp          gsKit sprite drawing helpers
  game/
    Game.hpp                    Game lifecycle + config (Configure/Init/Update/Shutdown)
    GameContext.hpp              Game state (camera, input, config)
    GameScripts.hpp             Script handler registration
  platform/
    asset_path.hpp              PS2 device path resolution

src/
  main.cpp                      ELF entry point
  engine/
    engine.cpp                  Engine implementation (hardware + generic host)
    scene/
      PscnLoader.cpp            PSCN file loader
      SceneTree.cpp             Scene tree + transform computation
      ScriptRegistry.cpp        Script dispatch
      SceneRenderer.cpp         Sprite/tilemap/anim rendering
  atlas2d/
    AtlasPack.cpp               Atlas loader
    AtlasPackUtils.cpp          gsKit drawing
  game/
    Game.cpp                    Game lifecycle implementation
    GameScripts.cpp             Game script handlers
  platform/
    asset_path.cpp              Asset path resolution ```

## Architecture

**Engine** owns hardware init (DMA, GS, pad), asset loading, scene tree management, world transform computation, rendering, and script dispatch. It never decides *what* to load — the game does.

**Game** owns configuration and behavior. It tells the engine which assets to load via `GameConfig`, registers script handlers, sets initial state, and runs per-frame game logic. The engine calls four game lifecycle hooks:

```
game::Configure()  → returns GameConfig (asset paths, atlas page)
game::Init()       → register scripts, set initial GameContext state
game::Update()     → per-frame non-script game logic
game::Shutdown()   → game cleanup
```

This separation keeps the engine reusable and prepares for QuickJS scripting later.

### Scene Graph

The engine loads `.pscn.bin` files — a binary scene format exported by [peanut-assman](../peanut-assman). Scenes are node trees with these types:

| Type | Description |
|------|-------------|
| Root | Scene root (always node 0) |
| Node2D | Transform-only group node |
| Sprite | Static sprite from the atlas |
| AnimatedSprite | Animation clip from the atlas |
| TileMap | Chunk-based tilemap with tileset remap |
| CollisionShape | Rect, circle, or polygon (not rendered) |
| Area | Point or rect trigger zone |
| Light2D | Omni or directional light |

Each node has a position (16.16 fixed-point), rotation and scale (8.8 fixed-point), render layer, collision layer/mask, parallax, and optional script bindings.

World transforms are computed in a single O(N) forward pass using the pre-order node layout guaranteed by the PSCN format.

### Script System

Any node in the scene can have a `scriptId` string (e.g. `"player"`, `"camera_follow"`) and a `scriptData` JSON string. At startup, game code registers C++ handlers:

```cpp
// src/game/GameScripts.cpp
void RegisterAllScripts(engine::ScriptRegistry &registry)
{
  registry.RegisterHandler("player", nullptr, PlayerUpdate, nullptr);
  registry.RegisterHandler("camera_follow", nullptr, CameraFollowUpdate, nullptr);
}
```

Handlers receive the node index and a `void*` game context each frame. When QuickJS arrives, the same registry can delegate to JS callbacks.

### Rendering Pipeline

1. Nodes sorted by `renderLayer` (computed once at scene load)
2. For each visible node in render order:
   - **Sprite/AnimatedSprite**: look up atlas sprite, apply world transform + parallax, draw textured quad
   - **TileMap**: cull chunks against camera, resolve tile IDs through tileset remap tables, substitute animated tiles, draw tile quads with flip/rotate transforms
3. All rendering uses gsKit two-triangle textured quads

### Atlas Format

Sprite atlases are a pair of binary files (`atlas.meta.bin` + `atlas.bin`) containing packed RGBA pages, sprite metadata, animations, animated tiles, and an FNV-1a hash table for name lookups. See [peanut-assman docs](../peanut-assman/docs/atlas-format.md).

## Build

Requires the PS2 toolchain (`PS2DEV`, `PS2SDK`, `GSKIT` set, EE toolchain on `PATH`):

```sh
make
```

## Run

```sh
make run
```

Expects `ps2client` configured for your target. The engine loads these assets from the device root:

- `atlas.meta.bin` — atlas metadata
- `atlas.bin` — atlas pixel data
- `scene.pscn.bin` — scene graph

## Asset Device Selection

Asset paths are resolved at build time via the `ASSET_DEVICE` flag:

```sh
make ASSET_DEVICE=host              # host:path (default, ps2client)
make ASSET_DEVICE=mass ASSET_ROOT=assets   # mass:/assets/path (USB)
make ASSET_DEVICE=cdrom0 ASSET_ROOT=ASSETS # cdrom0:\ASSETS\path;1 (disc)
```

## Game Setup

The game layer lives in `src/game/` and `include/game/`. The engine calls into the game at four points:

### 1. Configure — choose what to load

```cpp
// src/game/Game.cpp
game::GameConfig game::Configure()
{
  GameConfig config;
  config.atlasMetaPath = "atlas.meta.bin";
  config.atlasDataPath = "atlas.bin";
  config.scenePath     = "scene.pscn.bin";
  config.atlasPageIndex = 0;
  return config;
}
```

### 2. Init — register scripts and set initial state

```cpp
void game::Init(GameContext &ctx, engine::ScriptRegistry &registry)
{
  ctx.renderScale = 4.0f;
  ctx.cameraSpeed = 2;
  RegisterAllScripts(registry);
}
```

### 3. Update — per-frame game logic (beyond scripts)

```cpp
void game::Update(GameContext &ctx)
{
  // e.g. check win conditions, manage game state transitions
}
```

### 4. Shutdown — cleanup

```cpp
void game::Shutdown(GameContext &ctx) { /* free game resources */ }
```

## Adding Game Scripts

1. Write handler functions in `src/game/GameScripts.cpp`
2. Register them in `RegisterAllScripts()` with the `scriptId` string that matches the scene node
3. Use `GameContext` to access camera, pad input, scene tree
4. The engine calls `onUpdate` every frame for each bound node

Handler signature:

```cpp
void MyHandler(uint32_t nodeIndex, void *gameCtx)
{
  auto *ctx = static_cast<game::GameContext *>(gameCtx);
  engine::SceneNode *node = ctx->sceneTree->GetNode(nodeIndex);
  // modify node->localX, node->localY, etc.
}
```

## Fixed-Point Conventions

| Format | Encode | Decode | Used for |
|--------|--------|--------|----------|
| 16.16 | `value * 65536` | `raw / 65536.0f` | Node position |
| 8.8 | `value * 256` | `raw / 256.0f` | Rotation, scale, parallax |
