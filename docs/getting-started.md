# Getting Started

This walks through building, running, dropping in assets, and writing your first behavior.

## Requirements

- PS2 toolchain installed at `/usr/local/ps2dev/` (or `$PS2DEV` / `$PS2SDK` / `$GSKIT` env vars set)
- A PS2 emulator (PCSX2 recommended for development) or real hardware with `ps2client`
- Python 3 (for the asset validator and the clangd compile-commands generator)

## Build

```sh
make            # produces peanut.elf at the project root
make clean      # remove .o files and the elf
```

The Makefile uses `-Wall -Wextra -Werror=return-type -Werror=uninitialized`; a clean build should produce zero warnings.

### Asset device

Asset paths are resolved at runtime through `platform::ResolveAssetPath`, which prefixes the path with a device based on a build-time flag.

```sh
make                                          # default: ASSET_DEVICE=host  ASSET_ROOT=assets/
make ASSET_DEVICE=mass ASSET_ROOT=peanut       # mass:/peanut/... (USB)
make ASSET_DEVICE=cdrom0 ASSET_ROOT=ASSETS     # cdrom0:\ASSETS\...;1
```

When developing in PCSX2, leave `ASSET_DEVICE=host`. PCSX2 must have host filesystem support enabled; paths resolve relative to the ELF's directory.

## Run

```sh
make run            # ps2client execee host:peanut.elf  (real PS2 + ps2client)
```

Or in PCSX2: **System → Run ELF →** select `peanut.elf`.

## Drop in assets

The engine expects three files under `assets/` by default (configurable — see [game-layer.md](game-layer.md)):

```
assets/
  atlas.meta.bin
  atlas.bin
  base0.pscn.bin       (or whatever GameConfig::scenePath points to)
```

Author and export them from [peanut-assman](../../peanut-assman). Before running on PS2, validate them:

```sh
python3 tools/validate_assets.py \
    assets/atlas.meta.bin \
    assets/atlas.bin \
    assets/base0.pscn.bin
```

The validator checks header magic numbers, file sizes, struct layouts, hash table integrity, sprite ID ranges, and tileset remap validity against the engine's expectations. It exits non-zero on any failure.

## Your first run

Run the ELF. The EE console (`Debug → EE log` in PCSX2 Qt) should show roughly:

```
InitPad: opened pad on port=0 slot=0
UploadAtlasPage: uploaded page=0 size=256x256 vram=...
[scene] loading host:assets/base0.pscn.bin
[scene] nodes=4 tilesets=1 chunks=19 strings=3
[scene] behaviors: 1 vtables, 1 instances, 16 arena bytes
```

If you see `Scene loaded` followed by tilemaps and a player sprite, you're up.

If you don't, the engine flashes a solid color before exiting:

| Color | Meaning |
|---|---|
| **Red** | Atlas file open / validation failed |
| **Orange** | Atlas VRAM upload failed |
| **Blue** | Scene file (`.pscn.bin`) open / validation failed |
| **Magenta** | Scene tree build failed (extension data mismatch, OOM in arena, etc.) |

The actual error string is printed before the color flash.

## Your first behavior

Behaviors are C-style function pointer vtables you register at engine startup and the engine binds to scene nodes by the node's `scriptId` (a string set in the scene exporter). Every node tagged `"hello"` in the scene gets your `Hello` behavior attached.

In `src/game/Behaviors.cpp`:

```cpp
struct HelloState {
  uint32_t frameCount;
};

void HelloInit(uint16_t /*binding*/, uint32_t nodeIndex,
               engine::SceneNode* /*node*/, void* state,
               const char* /*data*/, game::GameContext* /*ctx*/)
{
  S<HelloState>(state).frameCount = 0;
  std::printf("[hello] init node=%u\n", static_cast<unsigned>(nodeIndex));
}

void HelloUpdate(uint16_t, uint32_t, engine::SceneNode*, void* state, game::GameContext*)
{
  auto& s = S<HelloState>(state);
  if (++s.frameCount % 60 == 0) {
    std::printf("[hello] %u frames\n", static_cast<unsigned>(s.frameCount));
  }
}
```

Register it inside `RegisterAllBehaviors`:

```cpp
void RegisterAllBehaviors(engine::BehaviorRegistry& reg)
{
  // ... existing registrations ...

  engine::BehaviorVTable hello = {};
  hello.nameHash = atlas2d::FNV1a32("hello");
  hello.stateSize = sizeof(HelloState);
  hello.autoAttachNodeType = engine::BEHAVIOR_AUTO_ATTACH_NONE;
  hello.onInit = HelloInit;
  hello.onUpdate = HelloUpdate;
  reg.Register(hello);
}
```

Tag a node `"hello"` in the scene exporter, re-export, rebuild, run. Every second it prints a frame-count line.

Read [behaviors.md](behaviors.md) for the full behavior reference.

## Tooling

### clangd / nvim LSP

The Makefile flags don't auto-flow to clangd. Regenerate `compile_commands.json` whenever you add a `.cpp`:

```sh
python3 tools/gen_compile_commands.py
```

A `.clangd` config strips MIPS-only flags the system clang can't parse. For full standard-library resolution against the PS2 cross-toolchain, your `clangd` command line needs `--query-driver=/usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-*` (in nvim, set this in your clangd LSP config).

## Next steps

- [architecture.md](architecture.md) — what runs when, and who owns what
- [behaviors.md](behaviors.md) — full behavior + state guide
- [signals.md](signals.md) — events, including pad-press events
- [scene-management.md](scene-management.md) — swap scenes, swap atlases, remove nodes
