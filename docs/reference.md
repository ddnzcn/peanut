# Reference

Quick-lookup tables for constants, conventions, and file format magic. The other docs explain *how* things work; this one is the cheat sheet.

## Directory layout

```
include/
  atlas2d/
    AtlasPack.hpp          Atlas binary loader, sprite/anim lookup, FNV1a32
    AtlasPackUtils.hpp     gsKit drawing helpers (BuildAtlasQuad, DrawAtlasSprite)
  engine/
    engine.hpp             Engine class (run, init, tick, swapScene, swapAtlas)
    Signals.hpp            signal::Hash + reserved signal name constants
    scene/
      PscnTypes.hpp        On-disk PSCN structs (no runtime state)
      PscnLoader.hpp       PscnFile class — load + validate + accessors
      SceneTree.hpp        Runtime SceneNode array + extension blob + behavior arena + SignalBus
      Behavior.hpp         BehaviorVTable + handler typedefs
      BehaviorRegistry.hpp Vtable table, instance table, BindScene/InitAll/UpdateAll/DestroyAll/Subscribe
      SignalBus.hpp        SignalPayload union + SignalBus class
      SceneRenderer.hpp    RenderScene entry point + SceneRenderParams
  game/
    Game.hpp               Configure / Init / OnSceneLoaded / Update / Shutdown declarations
    GameContext.hpp        Live state struct passed to every callback
    Behaviors.hpp          RegisterAllBehaviors + S<T> helper
  platform/
    asset_path.hpp         ResolveAssetPath (host:/mass:/cdrom0:)

src/                       Same layout, .cpp implementations
assets/                    Drop atlas.meta.bin, atlas.bin, <scene>.pscn.bin here
tools/
  validate_assets.py       Pre-flight asset validator
  gen_compile_commands.py  Regenerate compile_commands.json for clangd
docs/                      This documentation
.clangd                    clangd config (strips MIPS-only flags)
Makefile                   PS2 cross-compile build rules
```

## Constants

### Scene tree

| Symbol | Value | Header |
|---|---|---|
| `MAX_SCENE_NODES` | 512 (`PSCN_MAX_NODES`) | `SceneTree.hpp` |
| `MAX_EXTENSION_BYTES` | 8192 | `SceneTree.hpp` |
| `BEHAVIOR_STATE_ARENA` | 32768 (32 KB) | `SceneTree.hpp` |
| `NODE_FLAG_VISIBLE` | `1 << 0` | `PscnTypes.hpp` |
| `NODE_FLAG_LOCKED` | `1 << 1` | `PscnTypes.hpp` |
| `NODE_FLAG_RUNTIME_DEAD` | `1 << 4` | `SceneTree.hpp` |

### Behaviors

| Symbol | Value | Header |
|---|---|---|
| `MAX_BEHAVIOR_VTABLES` | 64 | `BehaviorRegistry.hpp` |
| `MAX_BEHAVIOR_INSTANCES` | 512 | `BehaviorRegistry.hpp` |
| `MAX_SUBS_PER_INSTANCE` | 8 | `BehaviorRegistry.hpp` |
| `BEHAVIOR_STATE_NONE` | `0xFFFF` | `BehaviorRegistry.hpp` |
| `BEHAVIOR_AUTO_ATTACH_NONE` | `0xFF` | `Behavior.hpp` |

### Signals

| Symbol | Value | Header |
|---|---|---|
| `MAX_SIGNALS_PER_FRAME` | 128 | `SignalBus.hpp` |
| `MAX_DISPATCH_ROUNDS` | 4 | `SignalBus.hpp` |
| `SIGNAL_TARGET_BROADCAST` | `0xFFFFFFFFu` | `SignalBus.hpp` |
| `signal::kPadPressed` | `"pad.pressed"` | `Signals.hpp` |
| `signal::kPadReleased` | `"pad.released"` | `Signals.hpp` |
| `signal::kSceneReady` | `"scene.ready"` | `Signals.hpp` |

### gsKit / hardware

| Constant | Value | Where set |
|---|---|---|
| Oneshot queue pool | 4 MB | `engine.cpp` (`gsKit_init_global_custom`) |
| Persistent queue pool | `GS_RENDER_QUEUE_PER_POOLSIZE` default | `engine.cpp` |
| Video mode | PAL, interlaced, 640×512 | `engine.cpp` |
| Pixel format | `GS_PSM_CT32` (RGBA8888) | `engine.cpp` |
| Atlas texture filter | `GS_FILTER_NEAREST` | `engine.cpp` |

## Fixed-point conventions

The engine and PSCN format use two fixed-point formats. Game code that touches `localX/Y` or scale/rotation needs to know these.

| Format | Encode (float → raw) | Decode (raw → float) | Range | Used for |
|---|---|---|---|---|
| 16.16 | `value * (1 << 16)` | `raw / 65536.0f` | ±32767.99 | Node `localX`, `localY`, world transforms |
| 8.8 | `value * (1 << 8)` | `raw / 256.0f` | ±127.99 | Rotation, scale, parallax |

Helper constants the codebase uses:

```cpp
constexpr int32_t kFixed16One   = 1 << 16;   // 1.0 in 16.16
constexpr int32_t kFixed8One    = 256;       // 1.0 in 8.8
constexpr int32_t kDiagScale_8_8 = 181;      // 1/sqrt(2) ≈ 0.707 in 8.8
```

Common patterns:

```cpp
// Convert pixels to 16.16:
int32_t px_fixed = pixels * kFixed16One;

// Add a pixel-space offset to a 16.16 node position:
node->localX += 4 * kFixed16One;

// Read a 16.16 position back as pixels:
int pixels = node->world.worldX >> 16;
```

## File format magic numbers

| Format | Magic | Decoded | Where checked |
|---|---|---|---|
| Atlas meta | `0x54443241` | `"A2DT"` (LE) | `AtlasPack::ValidateHeader` |
| PSCN scene | `0x4E435350` | `"PSCN"` (LE) | `PscnFile::ValidateHeader` |
| PSCN string-none sentinel | `0xFFFFFFFFu` | — | `PscnTypes.hpp::PSCN_STRING_NONE` |

Versions currently supported: PSCN v1.0, Atlas v1.1. The exporter docs in `peanut-assman/docs/` are the authoritative format specs.

## Hash function

`atlas2d::FNV1a32(const char*)` — exported from `include/atlas2d/AtlasPack.hpp`. The exporter uses the identical algorithm; engine + assets agree on the hash space.

```cpp
uint32_t FNV1a32(const char* s) {
  uint32_t h = 0x811c9dc5;
  for (; *s; ++s) {
    h ^= static_cast<uint8_t>(*s);
    h *= 0x01000193;
  }
  return h;
}
```

Used for:
- Behavior `nameHash` (matches PSCN node `scriptId`)
- Signal name hashes (`signal::Hash(name)`)
- Sprite name lookups in `AtlasPack::FindSpriteByHash`
- Animation name lookups in `AtlasPack::FindAnimByHash`

## PSCN node types

| Value | Type | Extension struct | Render? |
|---|---|---|---|
| 0 | Root | — | No |
| 1 | Node2D | — | No (transform group) |
| 2 | Sprite | `PscnSpriteExt` | Yes |
| 3 | TileMap | `PscnTileMapExt` | Yes (chunk-culled) |
| 4 | CollisionShape | `PscnCollisionShapeExt` | No (planned) |
| 5 | Area | `PscnAreaExt` | No (planned) |
| 6 | Light2D | `PscnLight2DExt` | No (planned) |
| 7 | AnimatedSprite | `PscnAnimatedSpriteExt` | Yes |

Access an extension at runtime:

```cpp
const auto* ext = sceneTree->GetExtensionAs<engine::PscnTileMapExt>(*node);
if (ext) {
  int mapPx = int(ext->mapWidthTiles) * int(ext->tileWidth);
}
```

Returns `nullptr` if the node's `extSize` is too small for the requested type — defensive against mis-typed PSCN files.

## Memory budget summary

| Subsystem | Bytes | Notes |
|---|---|---|
| SceneNode array | ~48 KB | 512 × ~96 B |
| PSCN extension blob | 8 KB | read-only after load |
| Behavior state arena | 32 KB | runtime mutable, per-scene |
| Render order | 1 KB | 512 × 2 B |
| Vtable table | ~2 KB | 64 × 32 B |
| Instance table | ~26 KB | 512 × ~52 B |
| Signal queue | ~4.5 KB | 128 × 36 B |
| Atlas page buffer | variable | `memalign(128, w*h*4)` |
| gsKit oneshot queue | 4 MB | hardware-side |
| **Total persistent EE allocations** | ~125 KB + page | out of 32 MB |

## Build flags

The Makefile applies:

```
-std=c++17  -O2  -G0
-Wall  -Wextra
-Werror=return-type  -Werror=uninitialized
-DPS2_ASSET_DEVICE="..."  -DPS2_ASSET_ROOT="..."
```

`-Werror=return-type` and `-Werror=uninitialized` are the only `Werror` items — most warnings will fail-build-on if you add `-Werror`, which is encouraged for new code.

## Build-time defines

| Define | Default | Purpose |
|---|---|---|
| `PS2_ASSET_DEVICE` | `"host"` | Prefix for `platform::ResolveAssetPath` |
| `PS2_ASSET_ROOT` | `"assets/"` | Subdirectory under the device |
| `_EE` | always set | PS2SDK convention (EE-side build) |

Override at the `make` command line:

```sh
make ASSET_DEVICE=mass ASSET_ROOT=peanut
```

## Asset validator quick reference

```sh
python3 tools/validate_assets.py <atlas.meta.bin> <atlas.bin> <scene.pscn.bin>
```

Checks:

- Atlas header magic, version, fileSize, page table, sprite table, hash table integrity, animation table.
- PSCN header magic, version, fileSize, node tree consistency, tileset remap valid sprite IDs, chunk data ranges.
- Reports per-check pass/fail with a final exit code (0 = clean, 1 = any failures).

Run after every export from peanut-assman before shipping the assets to your build directory.

## Error-screen color codes

If the engine fails to load its initial assets, it flashes a solid color and exits.

| Color | Constant | Meaning |
|---|---|---|
| Red | `HaltWithColor(0xFF, 0x00, 0x00)` | `AtlasPack::Load` failed |
| Orange | `HaltWithColor(0xFF, 0x80, 0x00)` | `UploadAtlasPage` failed (VRAM, page format, etc.) |
| Blue | `HaltWithColor(0x00, 0x00, 0xFF)` | `PscnFile::Load` failed |
| Magenta | `HaltWithColor(0xFF, 0x00, 0xFF)` | `SceneTree::BuildFromPscn` failed |

The actual error string is logged to the EE console before the flash. Capture it from PCSX2's EE log to debug.

## Pad button bits

From PS2SDK's `<libpad.h>`:

| Bit | Name |
|---|---|
| `PAD_SELECT` | Select |
| `PAD_L3` | L3 |
| `PAD_R3` | R3 |
| `PAD_START` | Start |
| `PAD_UP` / `DOWN` / `LEFT` / `RIGHT` | D-pad |
| `PAD_L2` / `R2` / `L1` / `R1` | Shoulders |
| `PAD_TRIANGLE` / `CIRCLE` / `CROSS` / `SQUARE` | Face buttons |

Use them as masks against `ctx.padButtons` (held) or `payload.button.mask` (pressed/released signal).
