# Signals

The signal bus is the engine's pub/sub system. Behaviors subscribe to named events; anyone with `ctx.signalBus` can emit them. Dispatch is **deferred** — signals queue up and drain at a fixed point in the tick, so there's no reentrancy risk and no surprise mid-update side effects.

Use signals for **transient events**: button presses, item pickups, dialog openings, animation finishes, deaths. Don't use them for continuous state — for "where is the player every frame," just read `node->world` directly.

## Quick example

A behavior that prints whenever the cross button is pressed:

```cpp
void Listener_Init(uint16_t binding, uint32_t, engine::SceneNode*, void*,
                   const char*, game::GameContext* ctx)
{
  engine::signal::Subscribe(*ctx->behaviorRegistry, binding,
                            engine::signal::kPadPressed);
}

void Listener_OnSignal(uint16_t, uint32_t, engine::SceneNode*, void*,
                       uint32_t hash, uint32_t source,
                       const engine::SignalPayload* p, game::GameContext*)
{
  if (hash == engine::signal::Hash(engine::signal::kPadPressed) &&
      p->u.button.mask == PAD_CROSS)
  {
    std::printf("[listener] CROSS pressed (source=%u)\n",
                static_cast<unsigned>(source));
  }
}
```

Subscribe in `onInit`, react in `onSignal`. Multiple bindings can subscribe to the same signal — every subscriber gets it.

## Emitting

Three ways to push a signal into the queue:

```cpp
ctx->signalBus->Emit(hash, sourceNodeIndex, payload);                    // broadcast
ctx->signalBus->EmitTo(hash, sourceNodeIndex, targetNodeIndex, payload); // targeted
```

`Emit` reaches every subscriber. `EmitTo` only reaches subscribers whose binding's `nodeIndex` matches `targetNodeIndex` — useful for camera→player, UI→focused widget, etc.

`sourceNodeIndex` flows through to the subscriber's `onSignal` so handlers can filter by who emitted (it's free metadata — pass any value if you don't care, e.g. the emitting node's index, or `SIGNAL_TARGET_BROADCAST`).

## Payload

A 20-byte tagged union. Keep payloads compact; if you need more, design a registry of named payload types or pass an index into your own data structure.

```cpp
enum SignalPayloadKind : uint8_t {
  SIGNAL_EMPTY  = 0,
  SIGNAL_INT    = 1,    // single int32
  SIGNAL_PAIR   = 2,    // (x, y)
  SIGNAL_QUAD   = 3,    // (a, b, c, d) — RGBA, rects with w/h, etc.
  SIGNAL_BUTTON = 4,    // pad mask + edge
};

struct SignalPayload {
  uint8_t kind;
  uint8_t _pad[3];
  union {
    int32_t i;
    struct { int32_t x, y; } pair;
    struct { int32_t a, b, c, d; } quad;
    struct { uint16_t mask; uint8_t edge; uint8_t _pad; } button;
  } u;
};
```

Construct and emit:

```cpp
engine::SignalPayload p;
p.kind = engine::SIGNAL_PAIR;
p.u.pair.x = 100;
p.u.pair.y = 200;
ctx->signalBus->Emit(engine::signal::Hash("spawn.requested"), nodeIndex, p);
```

Receive (always check `kind` if the signal's payload contract isn't fixed):

```cpp
void OnSignal(uint16_t, uint32_t, engine::SceneNode*, void*,
              uint32_t hash, uint32_t,
              const engine::SignalPayload* p, game::GameContext*)
{
  if (hash == engine::signal::Hash("spawn.requested") && p->kind == engine::SIGNAL_PAIR) {
    std::printf("spawn at %d,%d\n", p->u.pair.x, p->u.pair.y);
  }
}
```

## Reserved signal names

The engine reserves the `pad.*` and `scene.*` namespaces. Game code is free to use anything else.

| Name | Constant | Payload | When emitted |
|---|---|---|---|
| `"pad.pressed"` | `signal::kPadPressed` | `button.mask` = single button bit; `edge = 1` | Top of tick, one per newly-pressed button (rising edge) |
| `"pad.released"` | `signal::kPadReleased` | `button.mask` = single button bit; `edge = 0` | Top of tick, one per newly-released button (falling edge) |
| `"scene.ready"` | `signal::kSceneReady` | `EMPTY` | After every scene load (initial + every swap), once all bindings' `onInit` has run |

Use the constants and `signal::Hash(name)` rather than hardcoding hashes — keeps the code grep-able.

```cpp
#include "engine/Signals.hpp"

if (hash == engine::signal::Hash(engine::signal::kSceneReady)) {
  // first frame of a new scene — show a fade-in, spawn an intro effect, etc.
}
```

## Dispatch order and timing

```
Each tick:
  1. Read pad → emit pad.pressed / pad.released (queued)
  2. UpdateAll                              (behaviors may emit; queued)
  3. ComputeWorldTransforms
  4. game::Update                           (may emit; queued)
  5. SignalBus.Dispatch                     ← all queued signals dispatched here
  6. ProcessDeadNodes                       (fires onDestroy on dead nodes)
  7. Render
```

Key consequences:

- **Input edges are processed in the same frame they happen.** `pad.pressed` fires at step 1, dispatched at step 5 → your `onSignal` runs before render.
- **A signal emitted from `onUpdate` reaches subscribers in the same frame.** Dispatch runs after all updates.
- **A signal emitted from inside `onSignal` (chained signals) drains in the same `Dispatch` call.** Dispatch loops until the queue is empty, bounded by `MAX_DISPATCH_ROUNDS * MAX_SIGNALS_PER_FRAME = 4 * 128 = 512` total signals before it logs a warning and bails out (prevents infinite signal loops).
- **A signal emitted from `onDestroy` runs at dispatch time of the *next* tick.** `ProcessDeadNodes` runs after `Dispatch`.

## Targeted dispatch

`EmitTo(hash, src, target, payload)` only delivers to subscribers whose binding's node index equals `target`. Other subscribers don't get this signal.

```cpp
// "Reset" a specific enemy to spawn position
engine::SignalPayload p;
p.kind = engine::SIGNAL_EMPTY;
ctx->signalBus->EmitTo(engine::signal::Hash("reset"),
                       myNodeIndex,     // I'm the source
                       enemyNodeIndex,  // only this enemy hears it
                       p);
```

Behavior `onSignal` receives `sourceNodeIndex` so subscribers can also do their own filtering on broadcast signals if needed.

## Subscription rules

```cpp
bool engine::signal::Subscribe(BehaviorRegistry& reg,
                               uint16_t bindingIndex,
                               const char* signalName);
```

- Call from `onInit`. The `bindingIndex` is what the engine passed your handler.
- Each binding can hold up to **8** subscriptions (`MAX_SUBS_PER_INSTANCE`).
- Duplicate subscriptions are silently rejected (returns `false`).
- A subscription persists for the lifetime of the binding — there's no `Unsubscribe`. To stop receiving signals, mark the node dead via `RemoveNode` (the binding gets skipped from then on) or check inside the handler and ignore.

## Pad input model

You read held buttons via the bitmask:

```cpp
if (ctx->padButtons & PAD_LEFT)  { /* held this frame */ }
if (ctx->padButtons & PAD_RIGHT) { /* held this frame */ }
```

You react to presses/releases via signals:

```cpp
// In onInit:
engine::signal::Subscribe(*ctx->behaviorRegistry, binding, engine::signal::kPadPressed);

// In onSignal:
if (hash == engine::signal::Hash(engine::signal::kPadPressed)) {
  if (p->u.button.mask == PAD_CROSS) { /* jump */ }
  if (p->u.button.mask == PAD_CIRCLE) { /* interact */ }
}
```

Both reads pull from the same single SIF RPC the engine does once per frame. Subscribing to pad signals does **not** add any extra IOP traffic — emission happens entirely on the EE side after the read.

The button bits are PS2 SDK `PAD_*` constants from `<libpad.h>`. The button payload's `edge` is `1` for pressed, `0` for released — redundant with the signal name but handy when the same handler subscribes to both signals.

## Custom signals

Use any name not starting with `pad.` or `scene.`. Pick something descriptive and namespaced:

```cpp
// in some emitter
ctx->signalBus->Emit(engine::signal::Hash("hud.score_changed"), nodeIndex, p);

// in a subscriber's onInit
engine::signal::Subscribe(*ctx->behaviorRegistry, binding, "hud.score_changed");
```

There's no central name registry — just be consistent. A common convention is `domain.event` (`hud.score_changed`, `dialog.choice_made`, `enemy.died`).

## When NOT to use signals

- **For continuous state** ("where is the player every frame," "current health"): just read the node / state directly. Per-frame queries are cheap.
- **For tightly-coupled call chains** where A always immediately calls B and B always knows about A: a direct function call is clearer. Signals shine when emitters don't know (or shouldn't care about) the receivers.
- **For data flows through the scene tree** like "this node's transform should follow that one": a behavior with a per-frame query is simpler than push-based updates.

## Limits

| Constant | Value | Defined in |
|---|---|---|
| `MAX_SIGNALS_PER_FRAME` | 128 | `SignalBus.hpp` |
| `MAX_DISPATCH_ROUNDS` | 4 | `SignalBus.hpp` |
| `MAX_SUBS_PER_INSTANCE` | 8 | `BehaviorRegistry.hpp` |
| `SIGNAL_TARGET_BROADCAST` | `0xFFFFFFFFu` | `SignalBus.hpp` |
| `SignalPayload` size | 20 bytes | enforced by `static_assert` |

Each guard logs a warning once when exceeded and stops dispatching, so a runaway loop won't hang the engine.
