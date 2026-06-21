# 10 — Full priority table + one-shot auto-revert

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 09

## Goal

Complete the priority table with all scenes from issues 08–09, confirm one-shot auto-revert
works, and establish full behavioral parity with the current `control_leds` dispatcher before
the YAML cutover in issue 11.

## Scope

### Priority table verification

The table is already defined in `state_machine.cpp` (issue 05). This PR confirms it covers
every scene from issues 08–09 in the correct order. For reference:

```
xmos_flashing_state == XMOS_FLASHING → XMOS_FLASH
xmos_flashing_state == XMOS_SUCCESS  → XMOS_SUCCESS    ← one-shot
xmos_flashing_state == XMOS_ERROR    → XMOS_ERROR      ← one-shot
improv_ble                           → IMPROV
init_in_progress &&  network_ok      → INIT
init_in_progress && !network_ok      → INIT_NO_NETWORK
!network_ok                          → NO_HA
volume_buttons_touched               → VOLUME
btn_action                           → ACTION_BUTTON
jack_plugged                         → JACK_PLUGGED     ← one-shot
jack_unplugged                       → JACK_UNPLUGGED   ← one-shot
warning                              → WARNING          ← one-shot
timer_ringing                        → TIMER_RING
va_phase == VA_WAITING               → WAITING
va_phase == VA_LISTENING             → LISTENING
va_phase == VA_THINKING              → THINKING
va_phase == VA_REPLYING              → REPLYING
va_phase == VA_ERROR                 → ERROR            ← sustained (NOT a one-shot)
va_phase == VA_NOT_READY             → NOT_READY
is_timer_active                      → TIMER_TICK
master_mute || media_muted           → MUTED
(default)                            → IDLE
```

### One-shot scenes and their `on_finished` callbacks

The following scenes have `one_shot=true` and a non-null `on_finished`. All callbacks are
wired in `scene_library::build(Facts& facts)` by closing over `facts` by reference.

| SceneId | Trigger cleared | Callback |
|---------|----------------|----------|
| `WARNING` | `facts_.warning` | `[&facts]{ facts.warning = false; }` |
| `JACK_PLUGGED` | `facts_.jack_plugged` | `[&facts]{ facts.jack_plugged = false; }` |
| `JACK_UNPLUGGED` | `facts_.jack_unplugged` | `[&facts]{ facts.jack_unplugged = false; }` |
| `XMOS_SUCCESS` | `facts_.xmos_flashing_state` | `[&facts]{ facts.xmos_flashing_state = XMOS_IDLE; }` |
| `XMOS_ERROR` | `facts_.xmos_flashing_state` | `[&facts]{ facts.xmos_flashing_state = XMOS_IDLE; }` |
| `SUCCESS` | `facts_.xmos_flashing_state` | `[&facts]{ facts.xmos_flashing_state = XMOS_IDLE; }` |

**`ERROR` (`va_phase == VA_ERROR`) is not in this list.** It is sustained by the VA component
until the next phase transition. Do not set `one_shot=true` for `ERROR`.

### One-shot invocation (controller, from issue 06)

```cpp
// Step 6 in loop():
if (scene.one_shot) {
  bool all_done = true;
  for (const auto& layer : scene.layers)
    if (!layer.anim->is_finished()) { all_done = false; break; }
  if (all_done && scene.on_finished)
    scene.on_finished();
}
```

After `on_finished()` runs, `facts_` is updated. On the next `loop()` call, `sm_.resolve()`
returns a different `SceneId` and a crossfade begins automatically. No explicit re-dispatch is
needed.

### One-shot preemption

Because `sm_.resolve()` runs every frame, a higher-priority fact can preempt a running
one-shot mid-animation. Example: `warning` is playing its 5-pulse animation, but then
`va_phase` changes to `VA_WAITING` — `WARNING` ranks above `WAITING`, so the warning
continues. But if `improv_ble` becomes true (ranks above `WARNING`), the `IMPROV` scene
takes over immediately on the next frame. The partial one-shot's `on_finished` is never
called in this case — `facts_.warning` remains true and `WARNING` will resume when
`improv_ble` clears.

This is acceptable and matches the current YAML behavior (scripts can be interrupted).

### `Animation::start()` on scene re-entry

When the controller detects a scene change (step 3 in issue 06), it calls `anim->start(ctx)`
on all layers of the new scene. This resets `index_`, `cycle_count_`, `pos_`, etc. so that
re-entering a one-shot scene (e.g., a second `warning` event while the first is in progress)
restarts the animation from the beginning.

## Status

Verified against the implementation (issues 05–09):
- `state_machine.cpp` `resolve()` returns 22 SceneIds — the full table above (21 conditions +
  default `IDLE`). `SUCCESS` is intentionally absent (standalone, not resolve-reachable).
- The six `one_shot=true` scenes (`WARNING`, `JACK_PLUGGED`, `JACK_UNPLUGGED`, `XMOS_SUCCESS`,
  `XMOS_ERROR`, `SUCCESS`) each have an `on_finished` closure over `facts`; `ERROR` is not one.
- The controller's step-6 one-shot check + `start()`-on-scene-change reset are in place
  (issue 06).
- Engine source contains no references to `control_leds`, `voice_assistant_leds`, or
  `led_anim_speed`.

The remaining acceptance items are on-device behavioural checks performed during/after the
issue 11 cutover (the engine isn't the live LED path until then).

## Acceptance

- Full device pass of every state path described in the EPIC verification section.
- `WARNING` event → 5 red pulses → auto-reverts to previous scene; `facts_.warning == false`.
- `JACK_PLUGGED` event → outward ripple → auto-clears; no YAML delay required.
- `XMOS_SUCCESS` event → green pulse ×2 → `xmos_flashing_state` resets to `XMOS_IDLE`.
- Mid-warning wake word: `WARNING` → `WAITING` crossfade occurs immediately on next frame.
- `ERROR` phase: sustained red pulse, reverts only when `set_phase: 1` (idle) is called.
- Grep confirms no remaining references to `control_leds` script, `voice_assistant_leds`,
  or `led_anim_speed` in the engine source files.
