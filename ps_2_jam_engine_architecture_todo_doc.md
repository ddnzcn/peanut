# PS2 Jam Engine Architecture Notes

## Goal
Build a small, PS2-friendly 2D tile-based engine with a native C++ core and a QuickJS scripting layer for gameplay behavior.

This document is intentionally practical. It is not a full PS2 architecture reference. It is a planning document for getting to a working game-jam engine with the least architectural regret.

---

## 1. Project goals

### Primary goals
- [ ] Build a tile-based 2D game runtime that is stable on PS2SDK
- [ ] Keep the native engine simple enough to finish before the jam
- [ ] Expose enough scripting to make gameplay iteration fast
- [ ] Avoid architecture choices that fight the PS2
- [ ] Keep rendering, collision, and update logic predictable

### Non-goals for v1
- [ ] No 3D engine work
- [ ] No VU-heavy architecture
- [ ] No general-purpose ECS
- [ ] No advanced physics engine
- [ ] No editor tooling unless absolutely necessary
- [ ] No inheritance-heavy game object model in script

---

## 2. PS2 architecture: only what matters for this engine

### EE (Emotion Engine)
This is where the main game logic should live.

Use the EE for:
- [ ] main loop
- [ ] scene management
- [ ] entity update
- [ ] tile collision
- [ ] camera logic
- [ ] QuickJS host runtime
- [ ] asset management

Do not overcomplicate this.

### GS (Graphics Synthesizer)
Treat the GS as a low-level renderer. For v1, build a simple 2D render path.

Rendering model should be:
- [ ] clear
- [ ] draw tile layers
- [ ] draw entities
- [ ] draw UI/debug text
- [ ] flip

### IOP
Treat the IOP as the place where supporting modules and device-facing services live.

For now:
- [ ] use PS2SDK libraries normally
- [ ] avoid custom IOP architecture unless needed later

### VU0 / VU1
Do not design around them for v1.

- [ ] no gameplay dependence on VUs
- [ ] no attempt to offload tile/collision logic to vector units
- [ ] revisit only if there is a proven bottleneck later

### Memory constraints
Assume memory discipline matters from day one.

This affects:
- [ ] entity storage
- [ ] tilemap storage
- [ ] texture usage
- [ ] JS heap pressure
- [ ] temporary allocation behavior

---

## 3. High-level architecture

The main rule:

**Native owns the world. Script owns behavior.**

### Native C++ owns
- [ ] engine loop
- [ ] scene storage
- [ ] tilemap data
- [ ] collision map
- [ ] entity pool
- [ ] camera state
- [ ] rendering
- [ ] input state snapshots
- [ ] asset/resource lifetimes

### QuickJS owns
- [ ] game rules
- [ ] scene setup scripts
- [ ] per-entity behavior callbacks
- [ ] scripted triggers/events
- [ ] high-level orchestration

### Avoid
- [ ] duplicated world state in C++ and JS
- [ ] JS as the owner of core engine memory
- [ ] deep object graphs with unclear ownership
- [ ] per-frame creation/destruction of large numbers of wrapped objects

---

## 4. Engine systems

### 4.1 Engine
Responsibilities:
- [ ] initialize runtime
- [ ] initialize input
- [ ] initialize renderer
- [ ] initialize scripting VM
- [ ] run fixed-timestep loop
- [ ] dispatch update and render phases

Suggested shape:
- [ ] `Engine`
- [ ] `run()`
- [ ] `step(dt)`
- [ ] `render()`
- [ ] `shutdown()`

### 4.2 TileScene2D
This should be the main gameplay container.

Responsibilities:
- [ ] own map dimensions
- [ ] own tile layers
- [ ] own collision layer
- [ ] own entity pool
- [ ] own active camera
- [ ] provide scene-level update hooks

Suggested API goals:
- [ ] create entity
- [ ] set tile
- [ ] get tile
- [ ] set collision flags
- [ ] query solid/hazard tiles
- [ ] update scene
- [ ] render scene

### 4.3 Entity2D
Keep entities data-oriented.

Suggested native fields:
- [ ] id
- [ ] active flag
- [ ] visible flag
- [ ] x, y
- [ ] vx, vy
- [ ] w, h
- [ ] grounded
- [ ] tag/type
- [ ] collision flags
- [ ] sprite/tile/frame reference
- [ ] script callback handle(s)

Responsibilities:
- [ ] movement state
- [ ] collision state
- [ ] render data reference
- [ ] script hooks

### 4.4 Camera2D
Keep this minimal in v1.

Suggested fields:
- [ ] x, y
- [ ] viewport width
- [ ] viewport height
- [ ] followed entity id/handle
- [ ] clamp flags

Suggested features:
- [ ] manual positioning
- [ ] follow entity
- [ ] clamp to scene bounds
- [ ] optional deadzone later

### 4.5 Renderer2D
Keep renderer scope intentionally narrow.

Render responsibilities:
- [ ] clear screen
- [ ] draw tile layers
- [ ] draw sprite quads
- [ ] apply camera offset
- [ ] draw debug overlay
- [ ] present frame

Avoid in v1:
- [ ] complex batching systems before needed
- [ ] material systems
- [ ] generalized scene graph
- [ ] expensive abstraction layers

### 4.6 Input
Keep input state stable and frame-based.

Need:
- [ ] pressed
- [ ] released
- [ ] held/down
- [ ] per-frame snapshot

Expose to script cleanly.

### 4.7 ScriptVM
QuickJS integration layer.

Responsibilities:
- [ ] create runtime/context
- [ ] install modules/classes
- [ ] load scripts
- [ ] call update callbacks
- [ ] report script errors cleanly

Need early decisions:
- [ ] how native objects are wrapped
- [ ] how callbacks are stored
- [ ] how entity handles are validated

---

## 5. Update loop

Use a fixed timestep.

### Target
- [ ] fixed `dt = 1/60`

### Main loop
- [ ] poll input
- [ ] step fixed update
- [ ] render

### Update order draft
1. [ ] sample input
2. [ ] run scene-level script update
3. [ ] run entity script updates
4. [ ] apply native movement and physics
5. [ ] resolve collisions
6. [ ] update camera
7. [ ] render frame

### Important rule
Native should stay in control of timing.

Do not let script drive the main loop in v1.

---

## 6. Collision model

Use simple axis-aligned collision.

### Collision approach
- [ ] AABB for entities
- [ ] tile-grid collision for world
- [ ] axis-separated resolution
- [ ] fixed collision flags/properties

### World collision
Need tile flags such as:
- [ ] solid
- [ ] one-way platform
- [ ] hazard
- [ ] ladder
- [ ] trigger

### Entity collision
Decide whether v1 supports:
- [ ] entity vs world only
- [ ] entity vs entity overlap events
- [ ] entity vs entity blocking collisions

Suggested v1:
- [ ] entity vs world blocking
- [ ] entity vs entity overlap callbacks
- [ ] no full rigid-body behavior

### Native collision responsibilities
- [ ] integrate velocity
- [ ] resolve X axis
- [ ] resolve Y axis
- [ ] set grounded flag
- [ ] emit collision events/callbacks

---

## 7. Memory and ownership

This is one of the most important sections.

### Core rules
- [ ] avoid per-frame heap allocation in hot paths
- [ ] prefer pools or stable slot arrays
- [ ] prefer fixed-capacity containers where practical
- [ ] keep lifetime explicit

### Entity storage
Recommended:
- [ ] entity pool / slot array
- [ ] stable numeric IDs
- [ ] free-list for reuse

Avoid:
- [ ] raw pointers exposed directly to script if they can become invalid
- [ ] `std::vector` reallocation invalidating script-facing object pointers

### Script wrapper strategy
Recommended:
- [ ] JS wrapper stores scene ref + entity id
- [ ] every access validates id against native pool
- [ ] destroyed entity wrappers fail safely

### Resource ownership
Native should own:
- [ ] textures
- [ ] maps
- [ ] sound resources
- [ ] script bytecode/source lifetime containers if needed

Use RAII for native resources where cleanup matters.

---

## 8. C++ style choices

### Good uses of C++ here
- [ ] RAII for resources
- [ ] modules/namespaces
- [ ] light classes for systems
- [ ] simple wrappers over C APIs

### Keep gameplay data plain
Recommended:
- [ ] POD-style or near-POD structs for hot game data
- [ ] simple field access
- [ ] update via native systems/functions

Avoid in hot gameplay code:
- [ ] heavy inheritance
- [ ] virtual-everything design
- [ ] exceptions as control flow
- [ ] overly clever templates

### Need to decide
- [ ] whether to use STL in limited form
- [ ] whether to use custom containers for pools
- [ ] whether to ban dynamic allocation inside update/render entirely

---

## 9. QuickJS integration model

### Main principle
Bind engine-owned objects into JS as handles/wrappers.

### Planned exposed classes
- [ ] `TileScene2D`
- [ ] `Entity2D`
- [ ] `Camera2D`

### Planned exposed modules/namespaces
- [ ] `Input`
- [ ] `Screen`
- [ ] `Math2D` or simple math helpers
- [ ] possibly `Audio` later

### Binding strategy
For each wrapped class:
- [ ] native class/struct exists first
- [ ] QuickJS wrapper points to native object or stable id
- [ ] properties proxy to native state
- [ ] methods call native engine code
- [ ] finalizers do not destroy engine-owned objects accidentally unless intended

### Need to define callback model
Potential callbacks:
- [ ] `scene.onUpdate(dt)`
- [ ] `entity.onUpdate(dt)`
- [ ] `entity.onCollide(other)`
- [ ] `entity.onTileCollision(tx, ty, flags)`

### Avoid in v1
- [ ] complex inheritance between JS and native engine classes
- [ ] letting JS subclass native engine classes deeply
- [ ] JS-controlled lifetime for all entities

---

## 10. Proposed JS-facing API

This is intentionally small.

### Example target shape
```js
const scene = new TileScene2D(64, 32, 16, 16);
const camera = new Camera2D();
scene.setCamera(camera);

const player = scene.createEntity({
  x: 32,
  y: 32,
  w: 16,
  h: 16,
  tag: "player"
});

player.onUpdate = function(dt) {
  if (Input.down("left")) this.vx = -80;
  else if (Input.down("right")) this.vx = 80;
  else this.vx = 0;

  if (Input.pressed("cross") && this.grounded) {
    this.vy = -220;
  }
};

camera.follow(player);
```

### Desired scene methods
- [ ] `createEntity(opts)`
- [ ] `setTile(x, y, tileId)`
- [ ] `getTile(x, y)`
- [ ] `setTileFlags(x, y, flags)`
- [ ] `setCamera(camera)`
- [ ] `findByTag(tag)`

### Desired entity fields/methods
- [ ] `x, y, vx, vy`
- [ ] `w, h`
- [ ] `grounded`
- [ ] `tag`
- [ ] `destroy()`
- [ ] `onUpdate`
- [ ] `onCollide`

### Desired camera methods
- [ ] `follow(entity)`
- [ ] `setPosition(x, y)`
- [ ] `setClamp(enabled)`

---

## 11. Rendering design for v1

### Tile rendering
Need to decide:
- [ ] single tile layer first, or support multiple immediately
- [ ] tile IDs only, or tile + flags + palette info
- [ ] one tileset or multiple tilesets

Recommended v1:
- [ ] one visible tile layer
- [ ] one collision layer
- [ ] one tileset texture atlas

### Entity rendering
Need:
- [ ] sprite frame index or tile index per entity
- [ ] flip flags later if needed
- [ ] camera-relative draw

### Debug rendering
Useful before jam:
- [ ] entity bounds
- [ ] collision tiles
- [ ] player position
- [ ] frame/update timing
- [ ] current scene info

---

## 12. Asset strategy

### Tiles/maps
Need to choose:
- [ ] hardcoded arrays first
- [ ] simple custom binary/json-like format later
- [ ] Tiled export support eventually or not

Recommended v1:
- [ ] simple native-loaded tilemap format
- [ ] no editor integration until engine works

### Textures
Need to define:
- [ ] texture loading path
- [ ] atlas layout convention
- [ ] sprite frame lookup strategy

### Audio
Not needed before engine core is stable.

- [ ] defer audio unless a specific mechanic needs it

---

## 13. Milestones

### Milestone 0: toolchain sanity
- [ ] C sample builds
- [ ] C++ sample builds
- [ ] simple ELF runs
- [ ] debug output works

### Milestone 1: native-only engine core
- [ ] engine loop
- [ ] input polling
- [ ] one scene
- [ ] one camera
- [ ] tile collision
- [ ] one moving entity
- [ ] one renderable tilemap

### Milestone 2: native scene architecture
- [ ] entity pool
- [ ] scene update order finalized
- [ ] camera follow works
- [ ] scene render order works

### Milestone 3: QuickJS bring-up
- [ ] runtime/context init
- [ ] one native function exposed
- [ ] one native class exposed
- [ ] script file load/execute works

### Milestone 4: scriptable gameplay objects
- [ ] `TileScene2D` wrapper
- [ ] `Entity2D` wrapper
- [ ] `Camera2D` wrapper
- [ ] `entity.onUpdate(dt)` callback works

### Milestone 5: playable vertical slice
- [ ] player movement
- [ ] tilemap collision
- [ ] camera follow
- [ ] one enemy or hazard
- [ ] restart loop
- [ ] one short level

---

## 14. Risks

### Architectural risks
- [ ] overbuilding script integration too early
- [ ] making entities too OO-heavy
- [ ] duplicating native and JS state
- [ ] designing for flexibility instead of finishing

### Technical risks
- [ ] asset pipeline delays
- [ ] renderer quirks on real hardware
- [ ] performance death by allocations
- [ ] callback/lifetime bugs across QuickJS boundary

### Scope risks
- [ ] trying to support too many gameplay patterns
- [ ] building an engine instead of a jam game

---

## 15. Immediate decisions to make

- [ ] fixed timestep = 1/60?
- [ ] entity pool size?
- [ ] tile size = 16x16 or 32x32?
- [ ] room-based or scrolling world?
- [ ] one layer or multiple tile layers in v1?
- [ ] script callbacks only, or also scene script modules?
- [ ] raw pointers or stable IDs in JS wrappers? recommended: stable IDs
- [ ] use stock QuickJS first, no engine patches? recommended: yes

---

## 16. Recommended v1 architecture summary

### Native side
- [ ] `Engine`
- [ ] `Renderer2D`
- [ ] `TileScene2D`
- [ ] `Camera2D`
- [ ] `EntityPool`
- [ ] `InputState`
- [ ] `ScriptVM`

### Script side
- [ ] scene setup script
- [ ] entity behavior callbacks
- [ ] game rule scripts

### Main rules
- [ ] native owns data
- [ ] script owns behavior
- [ ] fixed timestep
- [ ] no VU dependency
- [ ] no fancy physics
- [ ] no deep inheritance model

---

## 17. First implementation checklist

### Week 1 / prep phase
- [ ] create native engine skeleton
- [ ] implement fixed loop
- [ ] get tilemap drawing working
- [ ] get one entity moving with collision
- [ ] get camera follow working

### Week 2 / scripting phase
- [ ] embed QuickJS
- [ ] expose `Input`
- [ ] expose `Entity2D`
- [ ] expose `TileScene2D`
- [ ] run `onUpdate(dt)` from script

### Jam-ready baseline
- [ ] start game
- [ ] load level
- [ ] move player
- [ ] collide with walls
- [ ] restart after death
- [ ] script one enemy or hazard

---

## 18. Open questions

- [ ] Should scene rendering be fully native or partially script-driven?
- [ ] Should `Entity2D` support animation in v1 or only sprite frame indices?
- [ ] Should scene callbacks run before or after native movement by default?
- [ ] Should collision callbacks be immediate or deferred until after scene update?
- [ ] Should map data be script-created first or loaded from native files first?
- [ ] Should `Camera2D` be one active singleton per scene in v1?

---

## 19. Final recommendation

Build the smallest architecture that can support one real game.

That means:
- [ ] one scene type
- [ ] one entity type
- [ ] one camera type
- [ ] one collision model
- [ ] one renderer path
- [ ] one scripting model

Do not design for future engine elegance until the first playable room exists.

