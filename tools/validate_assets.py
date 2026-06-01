#!/usr/bin/env python3
"""Validate atlas and scene binaries against the peanut engine format spec.
Run this after exporting from peanut-assman to catch mismatches before loading on PS2.

Usage: python3 tools/validate_assets.py assets/atlas.meta.bin assets/atlas.bin assets/base0.pscn.bin
"""

import struct
import sys
import os

PASS = 0
WARN = 0
FAIL = 0

def ok(msg):
    global PASS
    PASS += 1
    print(f"  OK   {msg}")

def warn(msg):
    global WARN
    WARN += 1
    print(f"  WARN {msg}")

def fail(msg):
    global FAIL
    FAIL += 1
    print(f"  FAIL {msg}")

def check(cond, pass_msg, fail_msg):
    if cond:
        ok(pass_msg)
    else:
        fail(fail_msg)

# ── Atlas Meta ──────────────────────────────────────────────────────

def validate_atlas_meta(meta_path, atlas_path):
    print(f"\n{'='*60}")
    print(f"ATLAS META: {meta_path}")
    print(f"{'='*60}")

    data = open(meta_path, "rb").read()
    check(len(data) >= 56, f"File size {len(data)} >= 56 (header)", f"File too small: {len(data)} bytes")

    # Header (56 bytes)
    h = struct.unpack_from("<IHHIIHHHHHHHHIIIIIII", data, 0)
    magic, verMaj, verMin, fileSize, crc32 = h[0:5]
    pageCount, spriteCount, animCount, animFrameCount = h[5:9]
    animTileCount, animTileFrameCount, hashEntryCount, _pad = h[9:13]
    pageTableOff, spriteTableOff, animTableOff, animFrameTableOff, hashTableOff, animTileTableOff = h[13:19]

    check(magic == 0x54443241, f"Magic 0x{magic:08X}", f"Bad magic 0x{magic:08X}, expected 0x54443241")
    check(verMaj == 1, f"Version {verMaj}.{verMin}", f"Unsupported major version {verMaj}")
    check(fileSize == len(data), f"fileSize={fileSize} matches actual={len(data)}", f"fileSize={fileSize} != actual={len(data)}")

    print(f"\n  Counts: pages={pageCount} sprites={spriteCount} anims={animCount} animFrames={animFrameCount}")
    print(f"          animTiles={animTileCount} animTileFrames={animTileFrameCount} hashEntries={hashEntryCount}")
    print(f"  Offsets: pages={pageTableOff} sprites={spriteTableOff} anims={animTableOff}")
    print(f"           animFrames={animFrameTableOff} hash={hashTableOff} animTiles={animTileTableOff}")

    # Page entries (30 bytes each): u16 pageIndex, u16 width, u16 height, u32 dataOffset, u32 dataSize, u8[16] reserved
    atlas_data = open(atlas_path, "rb").read() if os.path.exists(atlas_path) else None
    atlas_size = len(atlas_data) if atlas_data else 0

    page_end = pageTableOff + pageCount * 30
    check(page_end <= len(data), f"Page table fits ({pageTableOff}+{pageCount}*30={page_end})", f"Page table overflows file ({page_end} > {len(data)})")

    for i in range(pageCount):
        off = pageTableOff + i * 30
        pg = struct.unpack_from("<HHHII", data, off)
        pageIdx, width, height, dataOffset, dataSize = pg
        expected_rgba = width * height * 4

        check(width > 0 and height > 0, f"Page {i}: {width}x{height}", f"Page {i}: zero dimensions ({width}x{height})")
        check((width & (width-1)) == 0, f"Page {i}: width {width} is power of 2", f"Page {i}: width {width} not power of 2")
        check((height & (height-1)) == 0, f"Page {i}: height {height} is power of 2", f"Page {i}: height {height} not power of 2")

        if atlas_data:
            check(dataOffset + dataSize <= atlas_size,
                  f"Page {i}: data range [{dataOffset}..{dataOffset+dataSize}) fits atlas ({atlas_size})",
                  f"Page {i}: data range [{dataOffset}..{dataOffset+dataSize}) overflows atlas ({atlas_size})")
            check(dataSize >= expected_rgba,
                  f"Page {i}: dataSize {dataSize} >= expected RGBA {expected_rgba}",
                  f"Page {i}: dataSize {dataSize} < expected RGBA {expected_rgba}")

    # Sprite entries (40 bytes each)
    sprite_end = spriteTableOff + spriteCount * 40
    check(sprite_end <= len(data), f"Sprite table fits ({spriteTableOff}+{spriteCount}*40={sprite_end})", f"Sprite table overflows file ({sprite_end} > {len(data)})")

    max_sprite_id = 0
    for i in range(spriteCount):
        off = spriteTableOff + i * 40
        sid, nhash, pageIdx, flags = struct.unpack_from("<IIHH", data, off)
        w, h = struct.unpack_from("<HH", data, off + 16)
        if sid > max_sprite_id:
            max_sprite_id = sid
        if pageIdx >= pageCount:
            fail(f"Sprite {i} (id={sid}): pageIndex {pageIdx} >= pageCount {pageCount}")

    ok(f"Sprites: {spriteCount} entries, max id={max_sprite_id}")

    # Hash table
    if hashEntryCount > 0 and hashTableOff > 0:
        hash_end = hashTableOff + hashEntryCount * 8
        check(hash_end <= len(data), f"Hash table fits", f"Hash table overflows file")
        check((hashEntryCount & (hashEntryCount - 1)) == 0,
              f"Hash table size {hashEntryCount} is power of 2",
              f"Hash table size {hashEntryCount} NOT power of 2")

        empty = 0
        for i in range(hashEntryCount):
            nh, si = struct.unpack_from("<II", data, hashTableOff + i * 8)
            if nh == 0:
                empty += 1
            elif si >= spriteCount:
                fail(f"Hash entry {i}: spriteIndex {si} >= spriteCount {spriteCount}")
        ok(f"Hash table: {hashEntryCount} slots, {empty} empty, {hashEntryCount-empty} used")

    # Anims
    if animCount > 0 and animTableOff > 0:
        anim_end = animTableOff + animCount * 12
        check(anim_end <= len(data), f"Anim table fits", f"Anim table overflows")
        for i in range(animCount):
            nh, firstFrame, frameCount, flags, _p = struct.unpack_from("<IHHHH", data, animTableOff + i * 12)
            if animFrameTableOff > 0 and frameCount > 0:
                frame_end_idx = firstFrame + frameCount
                if frame_end_idx > animFrameCount:
                    fail(f"Anim {i}: frame range [{firstFrame}..{frame_end_idx}) exceeds animFrameCount {animFrameCount}")
        ok(f"Anims: {animCount} entries, {animFrameCount} total frames")

    return spriteCount, max_sprite_id


# ── PSCN Scene ──────────────────────────────────────────────────────

def validate_pscn(scene_path, atlas_sprite_count, atlas_max_sprite_id):
    print(f"\n{'='*60}")
    print(f"PSCN SCENE: {scene_path}")
    print(f"{'='*60}")

    data = open(scene_path, "rb").read()
    check(len(data) >= 64, f"File size {len(data)} >= 64 (header)", f"File too small: {len(data)}")

    h = struct.unpack_from("<IHHIIHHHHIIIIII16s", data, 0)
    magic, verMaj, verMin, fileSize, crc32 = h[0:5]
    nodeCount, tilesetCount, chunkCount, stringCount = h[5:9]
    nodeTableOff, tilesetTableOff, chunkTableOff, chunkDataOff, stringTableOff, stringDataOff = h[9:15]

    check(magic == 0x4E435350, f"Magic 0x{magic:08X}", f"Bad magic 0x{magic:08X}, expected 0x4E435350")
    check(verMaj == 1, f"Version {verMaj}.{verMin}", f"Unsupported major version {verMaj}")
    check(fileSize == len(data), f"fileSize={fileSize} matches actual={len(data)}", f"fileSize={fileSize} != actual={len(data)}")

    print(f"\n  Counts: nodes={nodeCount} tilesets={tilesetCount} chunks={chunkCount} strings={stringCount}")

    # Walk nodes
    type_names = {0:"Root", 1:"Node2D", 2:"Sprite", 3:"TileMap", 4:"CollisionShape", 5:"Area", 6:"Light2D", 7:"AnimSprite"}
    off = nodeTableOff
    print(f"\n  Nodes:")
    for i in range(nodeCount):
        if off + 64 > len(data):
            fail(f"Node {i}: base extends past file at offset {off}")
            break
        nodeId, parentIdx, nameHash, nodeType, flags, renderLayer = struct.unpack_from("<IiIBBH", data, off)
        posX, posY = struct.unpack_from("<ii", data, off + 16)
        extSize = struct.unpack_from("<H", data, off + 56)[0]
        tname = type_names.get(nodeType, f"?{nodeType}")
        vis = "VIS" if (flags & 1) else "---"

        if off + 64 + extSize > len(data):
            fail(f"Node {i}: extension overflows file ({off}+64+{extSize}={off+64+extSize} > {len(data)})")
            break

        print(f"    [{i:3d}] {tname:14s} {vis} layer={renderLayer} pos=({posX/65536:.1f},{posY/65536:.1f}) extSize={extSize}")

        # Validate TileMap extensions
        if nodeType == 3 and extSize >= 24:
            tm = struct.unpack_from("<HHHHHHBBHI", data, off + 64)
            tileW, tileH, chunkW, chunkH, mapW, mapH, proj, _p, tmChunkCount, firstChunkIdx = tm
            check(tileW > 0 and tileH > 0, f"  TileMap[{i}]: tile {tileW}x{tileH}", f"  TileMap[{i}]: zero tile dimensions")
            check(chunkW > 0 and chunkH > 0, f"  TileMap[{i}]: chunk {chunkW}x{chunkH}", f"  TileMap[{i}]: zero chunk dimensions")
            if tmChunkCount > 0:
                check(firstChunkIdx + tmChunkCount <= chunkCount,
                      f"  TileMap[{i}]: chunks [{firstChunkIdx}..{firstChunkIdx+tmChunkCount}) within {chunkCount}",
                      f"  TileMap[{i}]: chunks [{firstChunkIdx}..{firstChunkIdx+tmChunkCount}) exceeds {chunkCount}")

        off += 64 + extSize

    # Validate tilesets and remap tables
    if tilesetCount > 0:
        print(f"\n  Tilesets:")
        for i in range(tilesetCount):
            ts_off = tilesetTableOff + i * 28
            tsId, nameHash, firstTileId, tileCount, remapOff = struct.unpack_from("<IIIII", data, ts_off)
            tileW, tileH, cols, tsflags = struct.unpack_from("<HHHH", data, ts_off + 20)
            print(f"    [{i}] id={tsId} firstTile={firstTileId} count={tileCount} remapOff={remapOff} tile={tileW}x{tileH}")

            # Validate remap entries reference valid atlas sprites
            bad = 0
            for t in range(tileCount):
                remap_addr = remapOff + t * 4
                if remap_addr + 4 > len(data):
                    fail(f"  Tileset {i}: remap entry {t} at offset {remap_addr} overflows file")
                    break
                sprId = struct.unpack_from("<I", data, remap_addr)[0]
                if sprId > atlas_max_sprite_id:
                    bad += 1

            if bad > 0:
                fail(f"  Tileset {i}: {bad}/{tileCount} remap entries reference spriteIds beyond atlas max ({atlas_max_sprite_id})")
            else:
                ok(f"  Tileset {i}: all {tileCount} remap entries reference valid sprite IDs")

    # Check chunks reference valid tile data
    if chunkCount > 0:
        chunkDataSize = len(data) - chunkDataOff
        for i in range(chunkCount):
            c_off = chunkTableOff + i * 20
            nodeIdx, cx, cy, _p, tileDataOff, tileCnt, usedCnt, _p2 = struct.unpack_from("<HHHHIIHH", data, c_off)
            tile_bytes = tileCnt * 8
            if tileDataOff + tile_bytes > chunkDataSize:
                fail(f"  Chunk {i} ({cx},{cy}): tile data overflows chunk data section")

        ok(f"Chunks: {chunkCount} entries validated")


# ── Main ────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <atlas.meta.bin> <atlas.bin> <scene.pscn.bin>")
        sys.exit(1)

    meta_path, atlas_path, scene_path = sys.argv[1], sys.argv[2], sys.argv[3]

    sprite_count, max_sprite_id = validate_atlas_meta(meta_path, atlas_path)
    validate_pscn(scene_path, sprite_count, max_sprite_id)

    print(f"\n{'='*60}")
    print(f"RESULTS: {PASS} passed, {WARN} warnings, {FAIL} failures")
    print(f"{'='*60}")
    sys.exit(1 if FAIL > 0 else 0)
