# 07 — Setter actions API (automation.h + codegen)

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 06

## Goal

Expose the engine's `Facts` input surface to YAML so triggers can report state changes
without calling `script.execute: control_leds`. The engine re-resolves every frame, so
actions only need to update a fact field.

## Scope

### `automation.h` — action templates

Mirror the pattern in `esphome/components/satellite1/automation.h`.

```cpp
// led_ring_controller.set_phase
template<typename... Ts>
class SetPhaseAction : public Action<Ts...>, public Parented<LedRingController> {
  TEMPLATABLE_VALUE(int, phase)
  void play(Ts... x) override { parent_->facts_.va_phase = phase_.value(x...); }
};

// led_ring_controller.set_flag
enum class LedFlag {
  WARNING, JACK_PLUGGED, JACK_UNPLUGGED,
  VOLUME_BUTTONS_TOUCHED, BTN_ACTION,
  INIT_IN_PROGRESS, IMPROV_BLE, MASTER_MUTE, MEDIA_MUTED,
  NETWORK_OK, TIMER_RINGING, IS_TIMER_ACTIVE,
};

template<typename... Ts>
class SetFlagAction : public Action<Ts...>, public Parented<LedRingController> {
  TEMPLATABLE_VALUE(LedFlag, flag)
  TEMPLATABLE_VALUE(bool, value)
  void play(Ts... x) override;  // switch on flag_, set the corresponding Facts field
};

// led_ring_controller.set_media_volume
template<typename... Ts>
class SetMediaVolumeAction : public Action<Ts...>, public Parented<LedRingController> {
  TEMPLATABLE_VALUE(float, volume)
  void play(Ts... x) override {
    float v = volume_.value(x...);
    parent_->facts_.media_volume = v;
    parent_->facts_.media_muted  = (v == 0.f);
  }
};

// led_ring_controller.set_timer_ratio
template<typename... Ts>
class SetTimerRatioAction : public Action<Ts...>, public Parented<LedRingController> {
  TEMPLATABLE_VALUE(float, ratio)
  void play(Ts... x) override { parent_->facts_.timer_ratio = ratio_.value(x...); }
};

// led_ring_controller.event
enum class LedEvent {
  WARNING,
  JACK_PLUGGED, JACK_UNPLUGGED,
  XMOS_FLASH_START, XMOS_FLASH_PROGRESS, XMOS_SUCCESS, XMOS_ERROR,
};

template<typename... Ts>
class EventAction : public Action<Ts...>, public Parented<LedRingController> {
  TEMPLATABLE_VALUE(LedEvent, event)
  TEMPLATABLE_VALUE(float, value)    // used by XMOS_FLASH_PROGRESS only
  void play(Ts... x) override;       // sets the appropriate Facts field(s)
};
```

`EventAction::play()` mappings:
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

Register all actions via `automation.register_action`. Example YAML usage:

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

- A test YAML calls each action (e.g., `on_...: - led_ring_controller.set_phase: 4`) and
  compiles without error.
- Flipping `set_flag: {flag: warning, value: true}` via a temporary button trigger changes
  the resolved scene live on device.
- `set_media_volume: 0.0` sets both `media_volume = 0` and `media_muted = true`.
