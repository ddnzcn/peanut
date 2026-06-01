# Peanut Engine — Documentation

Peanut is a 2D game engine for the PlayStation 2, built on PS2SDK and gsKit. It loads scenes exported by [peanut-assman](../../peanut-assman), renders them via a scene graph, and runs gameplay through a behavior + signal system with a clean engine/game boundary.

## Documents

| File | Read this when you want to… |
|---|---|
| [getting-started.md](getting-started.md) | Build, run, and see your first asset on screen |
| [architecture.md](architecture.md) | Understand the engine/game split, the tick loop, and what owns what |
| [game-layer.md](game-layer.md) | Write the game side — `Configure`, `Init`, `OnSceneLoaded`, `Update`, `Shutdown`, and the fields on `GameContext` |
| [behaviors.md](behaviors.md) | Attach gameplay to scene nodes via behaviors (vtables, per-instance state, the `S<T>` helper) |
| [signals.md](signals.md) | Emit and subscribe to events; handle input edges (`pad.pressed` / `pad.released`) |
| [scene-management.md](scene-management.md) | Swap scenes at runtime, swap atlases, remove nodes during gameplay |
| [reference.md](reference.md) | Quick lookup: directory layout, constants, fixed-point conventions, file format magic numbers |

## What this engine does and doesn't do

**Does:**
- Loads PSCN binary scenes (node tree: Root, Node2D, Sprite, AnimatedSprite, TileMap, CollisionShape, Area, Light2D)
- Chunk-culled tilemap rendering and sprite/anim rendering through gsKit
- Per-frame behavior callbacks attached to scene nodes (Godot-style)
- Pub/sub signal bus with deferred dispatch
- Pad input as held-bitmask plus pressed/released edge signals
- Runtime scene swapping, atlas swapping, and node removal
- Asset validation tool (`tools/validate_assets.py`)

**Does not (yet):**
- Collision detection / response (`CollisionShape` / `Area` nodes are loaded, not yet checked)
- Light2D rendering
- Audio
- QuickJS scripting bindings (planned — the behavior system is designed to host a JS dispatch behavior later)
- A camera as a first-class scene node (see [architecture.md](architecture.md) deferred section)

## Asset pipeline at a glance

You author a scene in [peanut-assman](../../peanut-assman) (Vite app) and export:

| File | Content |
|---|---|
| `atlas.meta.bin` | Sprite metadata, animation entries, hash table |
| `atlas.bin` | Raw RGBA pixel data, one or more pages |
| `<scene>.pscn.bin` | Scene tree, tilesets, chunk data, string table |

Drop them into `assets/` next to the ELF, point your game at them via `GameConfig`, and the engine handles the rest.

For format specs, see the docs that ship with `peanut-assman`:

- `peanut-assman/docs/atlas-format.md`
- `peanut-assman/docs/pscn-format.md`
- `peanut-assman/docs/runtime-structs.md`
- `peanut-assman/docs/scene-runtime-guide.md`

## Quick orientation

```
peanut/
  include/
    atlas2d/      Atlas binary loader + gsKit drawing helpers
    engine/       Engine core (PSCN loader, scene tree, behaviors, signals, renderer)
    game/         Game-side hooks (GameContext, GameConfig, Behaviors entry point)
    platform/     PS2 device path resolution
  src/            Same layout, implementations
  assets/         Drop atlas.meta.bin, atlas.bin, <scene>.pscn.bin here
  tools/
    validate_assets.py        Asset validator
    gen_compile_commands.py   Generates compile_commands.json for clangd
  docs/           This directory
```

Next: [getting-started.md](getting-started.md).
