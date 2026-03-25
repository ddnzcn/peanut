# PS2 Jam Engine

Minimal C++ `ps2sdk` scaffold for a simple 2D engine prototype.

## Layout

- `src/main.cpp`: ELF entry point
- `src/engine/engine.cpp`: minimal engine loop stub
- `include/engine/engine.hpp`: engine interface
- `Makefile`: `ps2sdk` build rules

## Build

On the machine that has the PS2 toolchain installed:

```sh
make
```

That builds a normal EE ELF by default.

To build in sample-style ERL mode instead:

```sh
make BUILD_MODE=erl
```

Expected environment:

- `PS2DEV` set
- `PS2SDK` set
- EE toolchain binaries on `PATH`

## Run over ps2client

```sh
make run
```

That expects `ps2client` to be installed and configured for your target.

For ERL mode:

```sh
make BUILD_MODE=erl run
```

## Asset Device Selection

Asset paths should stay logical in code and be resolved at build time.

Example:

```cpp
#include "atlas2d/AtlasPack.hpp"
#include "platform/asset_path.hpp"

atlas2d::AtlasPack pack;
pack.Load(
    platform::ResolveAssetPath("atlas.meta.bin"),
    platform::ResolveAssetPath("atlas.bin"));
```

Supported build flags:

- `ASSET_DEVICE=host` resolves to `host:...`
- `ASSET_DEVICE=mass` resolves to `mass:/...`
- `ASSET_DEVICE=cdrom0` resolves to `cdrom0:\...;1`
- `ASSET_ROOT=...` prepends a device-local asset directory

Examples:

```sh
make ASSET_DEVICE=host
make ASSET_DEVICE=mass ASSET_ROOT=assets
make ASSET_DEVICE=cdrom0 ASSET_ROOT=ASSETS
```
