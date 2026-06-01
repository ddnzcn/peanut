# PS2 Jam Engine Architecture Notes

## Goal
Build a small, PS2-friendly 2D game engine with a native C++ core and a scene graph rendering pipeline. QuickJS scripting layer planned for gameplay behavior.

This document is a planning document for getting to a working game-jam engine with the least architectural regret.

---

## 1. Project goals

### Primary goals
- [x] Build a tile-based 2D game runtime that is stable on PS2SDK
- [x] Keep the native engine simple enough to finish before the jam
- [ ] Expose enough scripting to make gameplay iteration fast
- [x] Avoid architecture choices that fight the PS2
- [x] Keep rendering, collision, and update logic predictable

### Non-goals for v1
- [x] No 3D engine work
- [x] No VU-heavy architecture
- [x] No general-purpose ECS
- [x] No advanced physics engine
- [x] No editor tooling (handled by peanut-assman)
- [x] No inheritance-heavy game object model in script

---

## 2. PS2 architecture: only what matters for this engine

### EE (Emotion Engine)
- [x] main loop
- [x] scene management (PSCN scene graph)
- [ ] entity update (via script handlers)
- [ ] tile collision
- [x] camera logic (via game scripts)
- [ ] QuickJS host runtime
- [x] asset management (atlas + PSCN loader)

### GS (Graphics Synthesizer)
Rendering model:
- [x] clear
- [x] draw scene graph (tilemaps, sprites, animated sprites by render layer)
- [ ] draw UI/debug text
- [x] flip

### IOP
- [x] use PS2SDK libraries normally (SIO2MAN, PADMAN)

### VU0 / VU1
- [x] no gameplay dependence on VUs for v1

### Memory constraints
- [x] Fixed-size arrays for scene nodes (512 max), script handlers (64), bindings (512)
- [x] Extension data in contiguous 8KB blob, no per-node allocation
- [x] Atlas pixel data in single memaligned buffer
- [x] No per-frame heap allocation in hot paths

---

## 3. High-level architecture

**Native owns the world. Script owns behavior.**

### Engine (native C++) owns
- [x] engine loop (`Engine::run/init/tick/shutdown`)
- [x] scene storage (`SceneTree` — fixed 512-node array)
- [x] PSCN scene loading (`PscnFile`)
- [x] world transform computation (single O(N) forward pass)
- [x] rendering pipeline (`RenderScene` — render-layer sorted)
- [x] atlas/texture management (`AtlasPack`)
- [x] input state snapshots (pad reading)
- [x] script dispatch (`ScriptRegistry`)
- [x] asset/resource lifetimes

### Game scripts (C++ for now, QuickJS later) own
- [x] per-node behavior callbacks (registered by scriptId string)
- [x] camera follow logic
- [x] player movement
- [ ] game rules, scene setup
- [ ] scripted triggers/events

### Engine/game boundary
- Engine holds `game::GameContext` directly as a member
- Script handlers receive node index + `void* gameCtx`
- Game code never includes engine internals beyond the public API
- When QuickJS arrives, the `ScriptRegistry` can delegate to JS callbacks via a bridge handler

---

## 4. Engine systems

### 4.1 Engine (`engine::Engine`)
- [x] Initialize DMA, GS, pad
- [x] Load atlas + PSCN scene
- [x] Build scene tree + compute world transforms
- [x] Register game scripts + bind to scene nodes
- [x] Run tick loop: transforms → script update → render → flip

### 4.2 Scene graph (`engine::SceneTree`)
Node types from PSCN format:

| Type | Value | Extension | Status |
|------|-------|-----------|--------|
| Root | 0 | none | Loaded |
| Node2D | 1 | none | Loaded |
| Sprite | 2 | 12 bytes (spriteId, flip, tint) | Rendered |
| TileMap | 3 | 24 bytes (dimensions, projection, chunks) | Rendered |
| CollisionShape | 4 | 16 bytes (shape, dimensions) | Loaded, not used yet |
| Area | 5 | 16 bytes (shape, dimensions, tag) | Loaded, not used yet |
| Light2D | 6 | 20 bytes (radius, color, falloff) | Loaded, not used yet |
| AnimatedSprite | 7 | 16 bytes (animHash, flip, tint, default) | Rendered |

Scene tree features:
- [x] Pre-order node array (max 512 nodes)
- [x] World transform computation via parent chain accumulation
- [x] Render order sorted by `renderLayer` (insertion sort, stable)
- [x] Extension blob for type-specific data (8KB fixed buffer)
- [x] Parallax per node
- [x] Collision layer/mask per node (stored, not checked yet)

### 4.3 Script system (`engine::ScriptRegistry`)
- [x] C++ function pointer handlers (init/update/destroy)
- [x] Registered by scriptId string → FNV-1a hash
- [x] Bound to scene nodes at load time
- [x] Max 64 handlers, max 512 bindings
- [x] `void* gameCtx` passed opaquely

### 4.4 Scene renderer (`engine::RenderScene`)
- [x] Sprites: atlas lookup → world transform → parallax offset → textured quad
- [x] Animated sprites: `ResolveAnimFrame` → sprite lookup → render
- [x] Tilemaps: chunk culling → tileset remap → animated tile substitution → tile quads
- [x] Tile transforms: flipX, flipY, rot90 via UV manipulation
- [x] Orthographic projection
- [ ] Isometric projection (diamond + staggered) — code path exists, untested

### 4.5 Atlas system (`atlas2d::AtlasPack`)
- [x] Binary atlas loader (meta + pixel data)
- [x] Sprite lookup by ID, hash, or index
- [x] Animation frame resolution by time
- [x] Animated tile frame resolution
- [x] gsKit texture upload + VRAM management
- [x] FNV-1a hash function

### 4.6 Input
- [x] Per-frame pad button snapshot
- [x] Exposed to game via `GameContext::padButtons`
- [ ] Pressed/released edge detection

### 4.7 Asset path resolution (`platform::ResolveAssetPath`)
- [x] `host:` (ps2client debug)
- [x] `mass:/` (USB)
- [x] `cdrom0:\;1` (disc)
- [x] Configurable via `ASSET_DEVICE` and `ASSET_ROOT` build flags

---

## 5. Update loop

Fixed timestep at 1/60 (16ms per frame).

### Tick order
1. [x] Sample pad input
2. [x] Compute world transforms (so scripts read current-frame positions)
3. [x] Run script `onUpdate` callbacks
4. [x] Clear screen
5. [x] Render scene graph by render layer
6. [x] Queue exec + sync flip

---

## 6. Collision model

### Current state
- [x] CollisionShape and Area nodes loaded from PSCN
- [x] collision layer/mask stored per node (32-bit bitmasks)
- [ ] AABB collision detection not implemented
- [ ] Collision callbacks not implemented

### Planned approach
- AABB for entity nodes
- Tile-grid collision via CollisionShape nodes
- Collision rule: `(a.layer & b.mask) != 0 || (b.layer & a.mask) != 0`
- Axis-separated resolution

---

## 7. Memory and ownership

### Allocation strategy
| System | Storage | Size |
|--------|---------|------|
| Scene nodes | `SceneNode[512]` fixed array | ~48 KB |
| Extension data | `uint8_t[8192]` fixed blob | 8 KB |
| Render order | `uint16_t[512]` fixed array | 1 KB |
| Script handlers | `ScriptHandler[64]` fixed | ~2 KB |
| Script bindings | `ScriptBinding[512]` fixed | ~4 KB |
| Node offsets (loader) | `uint32_t[512]` fixed | 2 KB |
| Atlas meta/pixels | `vector<uint8_t>` (allocated once) | varies |
| PSCN file blob | `vector<uint8_t>` (allocated once) | varies |
| Atlas page buffer | `memalign(128, ...)` (allocated once) | varies |

No per-frame heap allocation in tick/render.

---

## 8. File structure

```
include/
  engine/
    engine.hpp                    Engine class
    scene/
      PscnTypes.hpp               PSCN binary format structs
      PscnLoader.hpp              Scene file loader
      SceneTree.hpp               Runtime node tree
      ScriptRegistry.hpp          Script handler dispatch
      SceneRenderer.hpp           Scene rendering
  atlas2d/
    AtlasPack.hpp                 Atlas binary loader
    AtlasPackUtils.hpp            gsKit drawing helpers
  game/
    GameContext.hpp                Game state
    GameScripts.hpp               Script handler registration
  platform/
    asset_path.hpp                PS2 device path resolution

src/
  main.cpp                        Entry point
  engine/                         Engine implementations
  atlas2d/                        Atlas implementations
  game/                           Game script implementations
  platform/                       Platform implementations
```

---

## 9. What's next

### Immediate
- [ ] Export a test `.pscn.bin` from peanut-assman and validate rendering
- [ ] Add camera bounds clamping in `camera_follow` script
- [ ] Add edge detection (pressed/released) to pad input

### Short-term
- [ ] Implement AABB collision detection using CollisionShape nodes
- [ ] Add collision callbacks to script system
- [ ] Add Area trigger enter/exit callbacks
- [ ] Test isometric tile projection

### Camera2D + viewport (deferred — needs format + exporter work)
Goal: engine-owned `Camera2D` driven by a scene node, with a screen viewport so
the world can render into a sub-region (split-screen / windowed) and a separate
plane can render outside it. Deferred because it requires a new camera object in
the PSCN format and exporter support, not just engine code.

- [ ] Add a Camera node type (or reserved Node2D convention) to the PSCN format spec + peanut-assman exporter
- [ ] `engine::Camera2D` struct: worldX/Y, screen viewport rect (viewX/Y/W/H), zoom
- [ ] Engine owns the active `Camera2D`; expose via `GameContext` pointer (like `sceneTree`)
- [ ] Drive camera position from the scene's camera node each frame (after `ComputeWorldTransforms`); fall back to (0,0) if absent; scripts may override
- [ ] Apply `gsKit_set_scissor(GS_SETREG_SCISSOR(x0,x1,y0,y1))` to clip world rendering to the camera viewport, then `GS_SCISSOR_RESET` for the separate plane
- [ ] Populate `SceneRenderParams` from `Camera2D` instead of the current `GameContext::cameraX/Y`
- [ ] Move `camera_follow` out of node scripts into camera-driving logic (it currently registers but never binds — no node carries that scriptId)

### Medium-term (QuickJS)
- [ ] Embed QuickJS runtime
- [ ] Create JS bridge handler that dispatches scriptId to JS functions
- [ ] Expose `SceneNode` properties to JS (position, scale, visibility)
- [ ] Expose `Input` module
- [ ] Expose `Camera` module

### Polish
- [ ] Debug overlay rendering (entity bounds, collision shapes, frame timing)
- [ ] Light2D rendering (GS alpha blending)
- [ ] Audio support
