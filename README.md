# Peanut — PS2 2D Game Engine

A C++17 PS2SDK game engine for 2D tile-based games. Scene graph rendering, chunk-based tilemaps, sprite animations, a Godot-style **behavior + signal** system attached to scene nodes, runtime scene/atlas swapping, and runtime node removal.

> Detailed end-user documentation lives in [`docs/`](./docs/README.md). Start with [docs/getting-started.md](./docs/getting-started.md).

## Layout

```
include/
  engine/
    engine.hpp                  Engine loop (init, tick, swapScene, swapAtlas, shutdown)
    Signals.hpp                 signal::Hash + reserved signal name constants
    scene/
      PscnTypes.hpp             PSCN binary format structs (on-disk)
      PscnLoader.hpp            Scene file loader + validation
      SceneTree.hpp             Runtime node tree, world transforms, behavior state arena, signal bus
      Behavior.hpp              BehaviorVTable + handler typedefs
      BehaviorRegistry.hpp      Vtable + instance tables, Bind/Init/Update/Destroy/Subscribe
      SignalBus.hpp             Payload union, queued signal, broadcast + targeted emit, deferred dispatch
      SceneRenderer.hpp         Sprite / animated sprite / tilemap rendering through gsKit
  atlas2d/
    AtlasPack.hpp               Atlas binary loader (sprites, anims, hash table) + FNV1a32
    AtlasPackUtils.hpp          gsKit drawing helpers
  game/
    Game.hpp                    Lifecycle declarations (Configure/Init/OnSceneLoaded/Update/Shutdown)
    GameContext.hpp             Live state + swap-request fields + engine-owned observer pointers
    Behaviors.hpp               RegisterAllBehaviors entry point + S<T>() state-cast helper
  platform/
    asset_path.hpp              PS2 device path resolution (host:/mass:/cdrom0:)

src/                            Implementations, mirrored layout
assets/                         atlas.meta.bin, atlas.bin, <scene>.pscn.bin
docs/                           End-user documentation (start here)
tools/
  validate_assets.py            Pre-flight asset validator
  gen_compile_commands.py       Regenerate compile_commands.json for clangd
```

## Architecture in one paragraph

The **engine** owns hardware init, PSCN scene loading, the scene tree, rendering, behavior dispatch, the signal bus, and the tick loop. The **game** owns asset paths (via `GameConfig`), behavior registrations, per-scene setup (`OnSceneLoaded`), and per-frame global logic (`Update`) like camera follow. Behaviors are C-style function pointer vtables attached to scene nodes by the node's `scriptId` string (or auto-attached by node type). The full split, lifecycle, and tick order are in [docs/architecture.md](./docs/architecture.md).

## Build

Requires the PS2 toolchain (`PS2DEV`, `PS2SDK`, `GSKIT` set, EE binaries on `PATH`):

```sh
make            # produces peanut.elf
```

Clean build is warning-free under `-Wall -Wextra -Werror=return-type -Werror=uninitialized`.

## Run

```sh
make run        # ps2client execee host:peanut.elf (real PS2)
```

Or in PCSX2: **System → Run ELF → peanut.elf** (with host filesystem enabled).

## Assets

Drop the three files into `assets/`:

```
assets/
  atlas.meta.bin    Sprite metadata + animation tables + hash table
  atlas.bin         Raw RGBA pixel pages
  base0.pscn.bin    Scene graph (or whatever GameConfig::scenePath points to)
```

Author and export them from [peanut-assman](../peanut-assman). Validate before running:

```sh
python3 tools/validate_assets.py assets/atlas.meta.bin assets/atlas.bin assets/base0.pscn.bin
```

## Asset device selection

Asset paths are resolved at build time via the `ASSET_DEVICE` flag:

```sh
make ASSET_DEVICE=host                       # host:path (PCSX2 / ps2client, default)
make ASSET_DEVICE=mass ASSET_ROOT=peanut     # mass:/peanut/...  (USB)
make ASSET_DEVICE=cdrom0 ASSET_ROOT=ASSETS   # cdrom0:\ASSETS\...;1  (disc)
```

## Game side at a glance

The game implements five hooks in `src/game/Game.cpp` and registers behaviors in `src/game/Behaviors.cpp`:

```cpp
game::Configure()        → return a GameConfig (asset paths + initial atlas page)
game::Init(ctx)          → one-time engine startup: render config, RegisterAllBehaviors
game::OnSceneLoaded(ctx) → per-scene setup: derive world bounds, reset camera, etc.
game::Update(ctx)        → per-frame global logic: camera follow, screen shake, win check
game::Shutdown(ctx)      → teardown
```

A minimal behavior:

```cpp
struct PlayerState { int32_t speedFixed; };

void PlayerInit(uint16_t binding, uint32_t, engine::SceneNode*, void* state,
                const char*, game::GameContext* ctx)
{
  game::S<PlayerState>(state).speedFixed = ctx->cameraSpeed * (1 << 16);
  engine::signal::Subscribe(*ctx->behaviorRegistry, binding, engine::signal::kPadPressed);
}

void PlayerUpdate(uint16_t, uint32_t, engine::SceneNode* node, void* state,
                  game::GameContext* ctx)
{
  auto& s = game::S<PlayerState>(state);
  if (ctx->padButtons & PAD_LEFT)  node->localX -= s.speedFixed;
  if (ctx->padButtons & PAD_RIGHT) node->localX += s.speedFixed;
}

void RegisterAllBehaviors(engine::BehaviorRegistry& reg)
{
  engine::BehaviorVTable vt = {};
  vt.nameHash  = atlas2d::FNV1a32("player");      // matches scene node's scriptId
  vt.stateSize = sizeof(PlayerState);
  vt.onInit    = PlayerInit;
  vt.onUpdate  = PlayerUpdate;
  reg.Register(vt);
}
```

Full behavior reference in [docs/behaviors.md](./docs/behaviors.md).

## Runtime scene control

Game code requests scene/atlas swaps and node removal via fields on `GameContext`; the engine consumes them at end of tick:

```cpp
ctx.requestedScene     = "level2.pscn.bin";       // load a new scene next frame
ctx.requestedAtlasMeta = "level2.atlas.meta.bin"; // (paired with scene swap) change art too
ctx.requestedAtlasData = "level2.atlas.bin";

ctx.sceneTree->RemoveNode(nodeIndex);             // mark a node + subtree dead
```

`onDestroy` fires automatically at end-of-tick for removed nodes; on scene swap for the outgoing scene's bindings; and on engine shutdown. See [docs/scene-management.md](./docs/scene-management.md).

## Signals

Behaviors subscribe in `onInit`, react in `onSignal`, emit anywhere with `ctx.signalBus`:

```cpp
engine::signal::Subscribe(*ctx->behaviorRegistry, binding, "enemy.died");

ctx->signalBus->Emit(engine::signal::Hash("enemy.died"), sourceNodeIndex, payload);
```

Input edges are exposed as engine signals: `pad.pressed`, `pad.released` (single SIF RPC per frame, zero added IOP traffic). Held input stays on `ctx.padButtons`. See [docs/signals.md](./docs/signals.md).

## Scene Graph

PSCN node types currently parsed by the loader:

| Type | Status | Type | Status |
|------|--------|------|--------|
| Root, Node2D | layout | Camera2D | parsed (not yet driving) |
| Sprite | rendered | Spawner | parsed |
| AnimatedSprite | rendered | Path2D / PathFollow2D | parsed |
| TileMap | rendered | Timer | parsed |
| CollisionShape, Area | parsed (no collision yet) | Decal | parsed |
| Light2D | parsed (no light render yet) | VisibilityNotifier | parsed |
| | | NavRegion2D | parsed |

Each node has position (16.16 fixed-point), rotation/scale (8.8 fixed-point), render layer, collision layer/mask, parallax, and optional `scriptId` / `scriptData` strings. World transforms compute in a single O(N) forward pass thanks to the PSCN pre-order layout.

## Fixed-point conventions

| Format | Encode | Decode | Used for |
|--------|--------|--------|----------|
| 16.16 | `value * 65536` | `raw / 65536.0f` | Node position, world transform |
| 8.8 | `value * 256` | `raw / 256.0f` | Rotation, scale, parallax, zoom |

Helper constants in `src/game/Behaviors.cpp`:

```cpp
constexpr int32_t kFixed16One   = 1 << 16;
constexpr int32_t kFixed8One    = 256;
constexpr int32_t kDiagScale_8_8 = 181;  // 1/sqrt(2) for diagonal-normalized input
```

## Tooling

- `tools/validate_assets.py` — pre-flight checks for atlas + PSCN files (catches version drift between exporter and engine).
- `tools/gen_compile_commands.py` — regenerate `compile_commands.json` for clangd / nvim LSP after adding `.cpp` files.
- `.clangd` — strips MIPS-only flags clangd's parser doesn't understand. For full standard-library resolution, your clangd command-line needs `--query-driver=/usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-*`.

## See also

- [`peanut-assman`](../peanut-assman) — Tauri-based scene exporter; produces the `.pscn.bin` + `atlas.{meta,bin}` files the engine loads.
- Asset format specs ship with the assman:
  - `peanut-assman/docs/atlas-format.md`
  - `peanut-assman/docs/pscn-format.md`
  - `peanut-assman/docs/runtime-structs.md`
  - `peanut-assman/docs/scene-runtime-guide.md`

## License / status

Active development; APIs may shift across commits. Architecture notes and the planned-feature backlog live in [`ps_2_jam_engine_architecture_todo_doc.md`](./ps_2_jam_engine_architecture_todo_doc.md).
