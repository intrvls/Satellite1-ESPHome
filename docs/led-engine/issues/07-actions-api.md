# 07 — Setter actions API (automation.h + codegen)

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 06

## Goal

Expose the engine's `Facts` input surface to YAML so triggers can report state changes
without calling `script.execute: control_leds`. The engine re-resolves every frame, so
actions only need to update a fact field.

## Scope

### Mutation surface on `LedRingController`

`facts_` stays `protected`. Rather than have each action reach into `parent_->facts_`
directly, the controller exposes a small public mutation surface (mirroring how
`satellite1`'s actions call public parent methods like `xmos_hardware_reset()`); the actions
just forward to it. `LedFlag` / `LedEvent` live in `led_ring_controller.h` so both the method
signatures and the actions can use them.

```cpp
// led_ring_controller.h
enum class LedFlag {
  WARNING, JACK_PLUGGED, JACK_UNPLUGGED,
  VOLUME_BUTTONS_TOUCHED, BTN_ACTION,
  INIT_IN_PROGRESS, IMPROV_BLE, MASTER_MUTE, MEDIA_MUTED,
  NETWORK_OK, TIMER_RINGING, IS_TIMER_ACTIVE,
};
enum class LedEvent {
  WARNING, JACK_PLUGGED, JACK_UNPLUGGED,
  XMOS_FLASH_START, XMOS_FLASH_PROGRESS, XMOS_SUCCESS, XMOS_ERROR,
};

void set_va_phase(int phase);                 // facts_.va_phase = phase
void set_flag(LedFlag flag, bool value);      // switch on flag -> Facts bool field
void set_media_volume(float volume);          // facts_.media_volume = v; media_muted = (v == 0)
void set_timer_ratio(float ratio);            // facts_.timer_ratio = ratio
void handle_event(LedEvent event, float value);  // see mapping table below
```

### `automation.h` — action templates

Mirror the pattern in `esphome/components/satellite1/automation.h`. Each `play()` just forwards
to the controller method. `flag` and `event` are compile-time enum members (set via a plain
setter, not `TEMPLATABLE_VALUE`) — they are always literals in YAML, which avoids templating an
enum; `value` stays templatable.

```cpp
template<typename... Ts> class SetPhaseAction : public Action<Ts...>, public Parented<LedRingController> {
  TEMPLATABLE_VALUE(int, phase)
  void play(Ts... x) override { this->parent_->set_va_phase(this->phase_.value(x...)); }
};

template<typename... Ts> class SetFlagAction : public Action<Ts...>, public Parented<LedRingController> {
  TEMPLATABLE_VALUE(bool, value)
  void set_flag(LedFlag flag);   // compile-time
  void play(Ts... x) override { this->parent_->set_flag(this->flag_, this->value_.value(x...)); }
};

template<typename... Ts> class SetMediaVolumeAction ... {
  TEMPLATABLE_VALUE(float, volume)
  void play(Ts... x) override { this->parent_->set_media_volume(this->volume_.value(x...)); }
};

template<typename... Ts> class SetTimerRatioAction ... {
  TEMPLATABLE_VALUE(float, ratio)
  void play(Ts... x) override { this->parent_->set_timer_ratio(this->ratio_.value(x...)); }
};

template<typename... Ts> class EventAction ... {
  TEMPLATABLE_VALUE(float, value)   // used by XMOS_FLASH_PROGRESS only; 0 otherwise
  void set_event(LedEvent event);   // compile-time
  void play(Ts... x) override { this->parent_->handle_event(this->event_, this->value_.value(x...)); }
};
```

`handle_event()` mappings (the `event` action forwards here):
| Event | Facts mutation |
|-------|----------------|
| `WARNING` | `facts_.warning = true` |
| `JACK_PLUGGED` | `facts_.jack_plugged = true` |
| `JACK_UNPLUGGED` | `facts_.jack_unplugged = true` |
| `XMOS_FLASH_START` | `facts_.xmos_flashing_state = XMOS_FLASHING` |
| `XMOS_FLASH_PROGRESS` | `facts_.xmos_flash_progress = value / 100.f` |
| `XMOS_SUCCESS` | `facts_.xmos_flashing_state = XMOS_SUCCESS` |
| `XMOS_ERROR` | `facts_.xmos_flashing_state = XMOS_ERROR` |

### `__init__.py` — action registration

Register all actions via `automation.register_action(..., synchronous=True)` — every `play()`
sets a field and returns immediately, so they qualify for the StringRef optimisation. `set_phase`,
`set_media_volume`, `set_timer_ratio` and `event` use `cv.maybe_simple_value(..., key=...)` so
they accept both the bare-value shorthand and the full `{id, ...}` form; `set_flag` requires the
mapping form (flag + value). `volume` and `ratio` validate as `cv.percentage` (so `50%` and
`0.5` both work); `event`'s `value` is an optional `cv.float_` (only `xmos_flash_progress` uses
it). `LedFlag` / `LedEvent` are exposed to codegen via `ns.enum(..., is_class=True)` and mapped
with `cv.enum(..., lower=True)`.

Example YAML usage:

```yaml
on_...:
  - led_ring_controller.set_phase: 4
  - led_ring_controller.set_flag:
      flag: warning
      value: true
  - led_ring_controller.set_media_volume: !lambda return id(external_media_player).volume;
  - led_ring_controller.event: xmos_flash_start
  - led_ring_controller.event:
      event: xmos_flash_progress
      value: !lambda return id(xflash).flashing_progress;
```

## Flag ownership: who clears what

This table is the authoritative contract between YAML callers and the engine.

| Fact | Set by | Cleared by | Mechanism |
|------|--------|------------|-----------|
| `warning` | `home_assistant.yaml` via `event(WARNING)` | **Engine** | `Scene::on_finished` after WARNING animation |
| `jack_plugged` | `speaker.yaml` via `event(JACK_PLUGGED)` | **Engine** | `Scene::on_finished` after Ripple animation |
| `jack_unplugged` | `speaker.yaml` via `event(JACK_UNPLUGGED)` | **Engine** | `Scene::on_finished` after Ripple animation |
| `xmos_flashing_state` (SUCCESS/ERROR) | `memory_flasher` via `event(XMOS_SUCCESS/ERROR)` | **Engine** | `Scene::on_finished` resets to `XMOS_IDLE` |
| `volume_buttons_touched` | `buttons.yaml` on press | **YAML** (`media_player.yaml` `delay: 2s`) | Time-based, not animation-based |
| `btn_action` | `buttons.yaml` `on_press` | **YAML** (`buttons.yaml` `on_release`) | Press/release pair |

`volume_buttons_touched` and `btn_action` are explicitly **not** engine-cleared. The 2-second
volume display window and the action-button press duration are caller-managed timing decisions
that don't correspond to animation completion.

## Acceptance

- A test YAML calls each action and compiles without error. A temporary `on_boot` block in
  `config/satellite1.yaml` exercises all five actions (both schema shapes); it settles back to a
  clean state and is removed during the issue 11 YAML migration.
- Flipping `set_flag: {flag: warning, value: true}` (or `event: warning`) changes the resolved
  scene live on device — the WARNING scene plays and then auto-clears via `on_finished`.
- `set_media_volume: 0%` sets both `media_volume = 0` and `media_muted = true`.

> The `run_selftest_()` self-test from issue 06 is removed in this issue, as planned — the
> actions API is now the runtime path for driving and observing scene changes on hardware.
