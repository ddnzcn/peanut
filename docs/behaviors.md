# Behaviors

A behavior is a vtable of function pointers you attach to scene nodes. Each (behavior, node) pair becomes a **binding** with its own per-instance state. The engine calls the vtable's functions at the right points in the lifecycle and dispatches signals to them.

## Mental model

Godot-style. Every node *can* have one or more behaviors attached:

- Behaviors are **types** registered once at engine startup (per game, not per scene).
- Bindings are **instances** of those types, created when the engine binds the scene.
- A node's `scriptId` string (set in the scene exporter) selects which behavior type attaches; a behavior may also auto-attach to every node of a given PSCN node type.

Behaviors never inherit from each other and never call virtuals. The "vtable" here is literal: a plain struct of `typedef`'d function pointers.

## Vtable

```cpp
struct BehaviorVTable {
  uint32_t nameHash;            // FNV-1a of the scriptId string; 0 = auto-attach-only
  uint16_t stateSize;           // bytes per binding; 0 = no per-instance state
  uint8_t  autoAttachNodeType;  // PscnNodeType; BEHAVIOR_AUTO_ATTACH_NONE (0xFF) disables
  uint8_t  _pad;
  BehaviorInitFn    onInit;     // may be nullptr
  BehaviorUpdateFn  onUpdate;   // may be nullptr
  BehaviorDestroyFn onDestroy;  // may be nullptr
  BehaviorSignalFn  onSignal;   // may be nullptr (no signal subscriptions then)
};
```

Set unused callbacks to `nullptr`. The engine skips them.

## Handler signatures

Every callback gets typed pointers — no `void*` cast at the top.

```cpp
typedef void (*BehaviorInitFn)(uint16_t bindingIndex, uint32_t nodeIndex,
                               engine::SceneNode* node, void* state,
                               const char* behaviorData,
                               game::GameContext* gameCtx);

typedef void (*BehaviorUpdateFn)(uint16_t bindingIndex, uint32_t nodeIndex,
                                 engine::SceneNode* node, void* state,
                                 game::GameContext* gameCtx);

typedef void (*BehaviorDestroyFn)(uint16_t bindingIndex, uint32_t nodeIndex,
                                  engine::SceneNode* node, void* state,
                                  game::GameContext* gameCtx);

typedef void (*BehaviorSignalFn)(uint16_t bindingIndex, uint32_t nodeIndex,
                                 engine::SceneNode* node, void* state,
                                 uint32_t signalHash, uint32_t sourceNodeIndex,
                                 const engine::SignalPayload* payload,
                                 game::GameContext* gameCtx);
```

Parameter meanings:

| Param | Notes |
|---|---|
| `bindingIndex` | Identity of this binding inside `BehaviorRegistry::m_instances`. You only need it when calling `signal::Subscribe`. |
| `nodeIndex` | Identity of the scene node this binding is attached to. Stable across the scene's lifetime. |
| `node` | Typed pointer to `SceneNode`. **The state argument is your data; this one is the engine's view of the node** (position, transform, flags). Modify `node->localX/Y` to move the node. |
| `state` | Pointer into the behavior state arena, sized to `stateSize`. Use the `S<T>` helper to cast. Zero-initialized before `onInit` runs. |
| `behaviorData` | UTF-8 string from the PSCN scene's `scriptData` field for this node (may be `nullptr`). Currently no parser ships — game code is free to interpret it (e.g. parse a simple `"hp=10;type=ogre"` format). |
| `gameCtx` | Typed pointer to `GameContext`. Access `padButtons`, `sceneTree`, `behaviorRegistry`, `signalBus`. |
| `signalHash`, `sourceNodeIndex`, `payload` | Signal-only. See [signals.md](signals.md). |

## Per-instance state

Each binding optionally owns `stateSize` bytes from a 32 KB arena on `SceneTree`. You declare the size at registration; the engine zero-initializes the slot before calling `onInit`. Reach it through `S<T>`:

```cpp
struct PlayerState {
  int32_t  speedFixed;
  uint8_t  activeAnim;
  uint8_t  _pad[3];
};

void PlayerInit(uint16_t, uint32_t, engine::SceneNode*, void* state,
                const char*, game::GameContext* ctx)
{
  auto& s = S<PlayerState>(state);
  s.speedFixed = ctx->cameraSpeed * (1 << 16);  // cache scaled speed
}

void PlayerUpdate(uint16_t, uint32_t, engine::SceneNode* node, void* state,
                  game::GameContext* ctx)
{
  auto& s = S<PlayerState>(state);
  // …use s.speedFixed, modify node->localX/Y…
}
```

Set `stateSize = 0` and pass `nullptr` for state if your behavior is genuinely stateless.

### State arena rules

- Allocated forward only (bump allocator). Behavior bindings claim slots in registration/bind order.
- Total budget: 32 KB (`BEHAVIOR_STATE_ARENA`). The engine logs a warning and skips a binding if it would overflow.
- The arena resets on `SceneTree::Clear()` — i.e. on every scene swap and on shutdown. **State does not persist across scene swaps.** If you want persistent data (player save, level select), keep it on `GameContext` or in your own statics, not in a behavior state slot.
- Removing a node mid-scene does NOT reclaim that node's state slot — the slot leaks for the rest of the scene. Predictable, bounded, fine in practice (don't spawn 1000 enemies per frame).

## Registering a behavior

In `RegisterAllBehaviors`:

```cpp
engine::BehaviorVTable vt = {};
vt.nameHash             = atlas2d::FNV1a32("player");   // matches scriptId in the scene
vt.stateSize            = sizeof(PlayerState);
vt.autoAttachNodeType   = engine::BEHAVIOR_AUTO_ATTACH_NONE;
vt.onInit    = PlayerInit;
vt.onUpdate  = PlayerUpdate;
vt.onDestroy = nullptr;
vt.onSignal  = PlayerOnSignal;

reg.Register(vt);
```

`Register` rejects duplicate name hashes and returns `false` on table overflow (`MAX_BEHAVIOR_VTABLES = 64`).

## Binding rules

When a scene loads, `BehaviorRegistry::BindScene` runs two passes:

1. **Named pass.** For every node whose PSCN `scriptIdStringIndex` is set, look up the resolved string's FNV-1a hash in the vtable table. If a vtable matches, create a binding.
2. **Auto-attach pass.** For every registered vtable with `autoAttachNodeType != 0xFF`, create a binding on every node whose `nodeType` matches. **Same (node, vtable) pair is deduplicated** — if pass 1 already created the binding, pass 2 won't double-create.

So a node can carry:
- Zero behaviors (no `scriptId`, no matching auto-attach).
- One **named** behavior (matched by `scriptId`).
- One or more **auto-attached** behaviors (engine-side defaults).
- Both — a named behavior in addition to auto-attached ones.

`InitAll` then walks the instance array in creation order and calls `onInit` on each. Order: pass-1 bindings run before pass-2 bindings for a given node, which preserves "your scripted behavior runs before the engine default" semantics.

## Lifecycle

```
Scene load
  BindScene                   (creates instances + claims state slots)
  InitAll                     (your onInit fires once per binding)
  emit "scene.ready"

Each frame
  UpdateAll                   (your onUpdate fires once per non-dead binding)
  …game::Update…
  SignalBus.Dispatch          (your onSignal fires for subscribed signals)
  ProcessDeadNodes            (your onDestroy fires for any binding marked dead this frame)

Scene swap or shutdown
  DestroyAll                  (your onDestroy fires for all still-alive bindings)
  ClearBindings (swap) or Clear (shutdown)
```

`onDestroy` fires **exactly once** per binding (and only if the vtable provides it). The `destroyed` flag inside `BehaviorInstance` prevents double-calls.

## Subscribing to signals

Done inside `onInit`. The `bindingIndex` you receive is what `signal::Subscribe` needs.

```cpp
#include "engine/Signals.hpp"  // for signal::Subscribe + kPadPressed/kPadReleased

void PlayerInit(uint16_t bindingIndex, uint32_t, engine::SceneNode*, void* state,
                const char*, game::GameContext* ctx)
{
  if (ctx && ctx->behaviorRegistry) {
    engine::signal::Subscribe(*ctx->behaviorRegistry, bindingIndex,
                              engine::signal::kPadPressed);
    engine::signal::Subscribe(*ctx->behaviorRegistry, bindingIndex,
                              engine::signal::kPadReleased);
  }
}
```

Each binding can subscribe up to `MAX_SUBS_PER_INSTANCE = 8` signal hashes. Duplicate subscriptions are silently rejected.

You can subscribe to game-defined signals too — anything not in the `pad.*` or `scene.*` reserved namespace. See [signals.md](signals.md).

## Emitting signals

Anywhere you have `gameCtx`:

```cpp
engine::SignalPayload p;
p.kind = engine::SIGNAL_INT;
p.u.i  = 42;
gameCtx->signalBus->Emit(engine::signal::Hash("door.opened"),
                         nodeIndex,  // sourceNodeIndex — useful for filtering
                         p);
```

Emission is **deferred** — the signal is queued and dispatched between `game::Update` and `Render` in the same frame. Reentrant emits during dispatch get drained in the same call.

To target a specific node:

```cpp
gameCtx->signalBus->EmitTo(engine::signal::Hash("set_target"),
                           myNodeIndex,        // source
                           targetNodeIndex,    // target — only this node's bindings hear it
                           p);
```

See [signals.md](signals.md) for the payload variants and reentrancy rules.

## Auto-attach example

Useful for engine-side defaults that should hook every node of a given type:

```cpp
engine::BehaviorVTable advance = {};
advance.nameHash           = 0;  // auto-attach-only — no scriptId match
advance.stateSize          = 0;
advance.autoAttachNodeType = engine::NODE_ANIMATED_SPRITE;
advance.onUpdate           = AnimAdvance;  // ticks `node->activeAnimIndex`, etc.
reg.Register(advance);
```

Every `AnimatedSprite` in every scene will now carry this behavior in addition to whatever named behavior it has.

## Full example: jumping player

```cpp
struct PlayerState {
  int32_t speedFixed;
  int32_t jumpVel;   // 16.16
  int32_t velY;      // 16.16
  uint8_t grounded;
  uint8_t _pad[3];
};

void PlayerInit(uint16_t binding, uint32_t, engine::SceneNode*, void* state,
                const char*, game::GameContext* ctx)
{
  auto& s = S<PlayerState>(state);
  s.speedFixed = ctx->cameraSpeed * (1 << 16);
  s.jumpVel    = 6 * (1 << 16);  // 6 px/frame initial jump
  s.velY       = 0;
  s.grounded   = 1;
  engine::signal::Subscribe(*ctx->behaviorRegistry, binding, engine::signal::kPadPressed);
}

void PlayerUpdate(uint16_t, uint32_t, engine::SceneNode* node, void* state,
                  game::GameContext* ctx)
{
  auto& s = S<PlayerState>(state);
  if ((ctx->padButtons & PAD_LEFT)  != 0) node->localX -= s.speedFixed;
  if ((ctx->padButtons & PAD_RIGHT) != 0) node->localX += s.speedFixed;

  // Gravity (no collision system yet — pretend the floor is y=0)
  s.velY += (1 << 14);  // ¼ px/frame²
  node->localY += s.velY;
  if (node->localY > 0) { node->localY = 0; s.velY = 0; s.grounded = 1; }
}

void PlayerOnSignal(uint16_t, uint32_t, engine::SceneNode*, void* state,
                    uint32_t hash, uint32_t,
                    const engine::SignalPayload* p, game::GameContext*)
{
  auto& s = S<PlayerState>(state);
  if (hash == engine::signal::Hash(engine::signal::kPadPressed) &&
      p->u.button.mask == PAD_CROSS &&
      s.grounded)
  {
    s.velY = -s.jumpVel;
    s.grounded = 0;
  }
}
```

Register it with `onInit`, `onUpdate`, `onSignal` (no `onDestroy` needed).

## Limits

| Constant | Value | Defined in |
|---|---|---|
| `MAX_BEHAVIOR_VTABLES` | 64 | `BehaviorRegistry.hpp` |
| `MAX_BEHAVIOR_INSTANCES` | 512 | `BehaviorRegistry.hpp` |
| `MAX_SUBS_PER_INSTANCE` | 8 | `BehaviorRegistry.hpp` |
| `BEHAVIOR_STATE_ARENA` | 32 KB | `SceneTree.hpp` |
| `BEHAVIOR_AUTO_ATTACH_NONE` | `0xFF` | `Behavior.hpp` |

All of these are static; bumping them just means recompiling.

## Common gotchas

- **Don't call `FindNodeByScriptId` every frame inside `onUpdate`.** The whole point of the typed `node` parameter is to skip that lookup. (`game::Update` is fine to use it — that's global, not per-binding.)
- **State doesn't survive a scene swap.** If you need persistence, stash on `GameContext` (and reset / restore in `OnSceneLoaded`).
- **`onDestroy` runs at end of tick after `RemoveNode`, not immediately.** See [scene-management.md](scene-management.md).
- **`onSignal` runs AFTER `onUpdate`** in the same frame for signals emitted during this tick. Input edge signals are emitted at the top of tick, so they're available to all handlers in the same frame.
- **Cascading removes get processed next frame.** If `onDestroy` calls `RemoveNode` on another node, that node's `onDestroy` fires next tick. `ProcessDeadNodes` is single-pass on purpose.
- **The `nameHash` is FNV-1a of the scriptId string** — use `atlas2d::FNV1a32`. The exporter uses the same function, so the engine and the asset agree on the hash space.
