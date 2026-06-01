#!/usr/bin/env python3
"""Generate compile_commands.json for clangd (nvim LSP).

The PS2SDK Makefile uses a cross-compiler, so clangd needs explicit include
paths and defines. This regenerates the DB whenever source files are added.
Run from the project root: python3 tools/gen_compile_commands.py
"""

import json
import os
import subprocess

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PS2SDK = os.environ.get("PS2SDK", "/usr/local/ps2dev/ps2sdk")
GSKIT = os.environ.get("GSKIT", "/usr/local/ps2dev/gsKit")
CXX = "/usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-g++"

# Mirrors the flags in the Makefile + Makefile.eeglobal
FLAGS = [
    CXX,
    "-D_EE",
    "-G0",
    "-O2",
    "-std=c++17",
    "-Wall",
    "-Wextra",
    '-DPS2_ASSET_DEVICE="host"',
    '-DPS2_ASSET_ROOT="assets/"',
    f"-I{PS2SDK}/ee/include",
    f"-I{PS2SDK}/common/include",
    "-I.",
    f"-I{PROJECT_ROOT}/include",
    f"-I{GSKIT}/include",
]


def find_sources():
    sources = []
    for root, _dirs, files in os.walk(os.path.join(PROJECT_ROOT, "src")):
        for f in files:
            if f.endswith(".cpp"):
                sources.append(os.path.relpath(os.path.join(root, f), PROJECT_ROOT))
    return sorted(sources)


def main():
    db = []
    for src in find_sources():
        obj = src[:-4] + ".o"
        db.append({
            "directory": PROJECT_ROOT,
            "arguments": FLAGS + ["-c", src, "-o", obj],
            "file": src,
        })

    out_path = os.path.join(PROJECT_ROOT, "compile_commands.json")
    with open(out_path, "w") as fp:
        json.dump(db, fp, indent=2)
    print(f"Wrote {out_path} ({len(db)} entries)")


if __name__ == "__main__":
    main()
