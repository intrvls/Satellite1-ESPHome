# 05 — StateMachine: Facts + table-driven resolve

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 03

## Goal

Move the `control_leds` priority ladder into C++ as a typed `Facts` struct and a data table.
`SceneId` is defined in `scene.h` (issue 03); this issue adds `Facts` and `resolve()`.

## Scope

### `engine/state_machine.h/.cpp`

#### Phase and XMOS constants

Define alongside `Facts` so both the state machine and `scene_library.cpp` use the same values
without pulling in YAML substitution strings:

```cpp
// Voice assistant phases — mirror voice_assistant.yaml substitutions exactly.
constexpr int VA_IDLE      = 1;
constexpr int VA_WAITING   = 2;
constexpr int VA_LISTENING = 3;
constexpr int VA_THINKING  = 4;
constexpr int VA_REPLYING  = 5;
constexpr int VA_NOT_READY = 10;
constexpr int VA_ERROR     = 11;

// XMOS flashing state values — mirror satellite1.base.yaml globals exactly.
constexpr int XMOS_IDLE     = 0;
constexpr int XMOS_FLASHING = 1;  // in progress; sweep animation running
constexpr int XMOS_SUCCESS  = 2;  // flash succeeded; one-shot green pulse
constexpr int XMOS_ERROR    = 3;  // flash failed;   one-shot red pulse
```

#### `Facts` struct

```cpp
struct Facts {
  // XMOS flashing pipeline
  int   xmos_flashing_state{XMOS_IDLE}; // XMOS_IDLE / XMOS_FLASHING / XMOS_SUCCESS / XMOS_ERROR
  float xmos_flash_progress{0.f};       // 0..1; maps to LED index for Sweep animation

  // Onboarding / connectivity
  bool improv_ble{false};
  bool init_in_progress{true};   // true at boot; cleared on HA connect or 10-min timeout
  bool network_ok{false};        // true when BOTH wifi AND api are connected

  // Transient user interactions (momentary; YAML caller owns the clear timing)
  bool volume_buttons_touched{false};
  bool btn_action{false};

  // Jack events (one-shot; engine clears via on_finished after animation completes)
  bool jack_plugged{false};
  bool jack_unplugged{false};

  // Alerts
  bool warning{false};           // one-shot; engine clears via on_finished

  // Timer
  bool timer_ringing{false};
  bool is_timer_active{false};
  float timer_ratio{0.f};        // 0..1; seconds_left / total_seconds

  // Voice assistant
  int va_phase{VA_NOT_READY};

  // Audio state
  bool master_mute{false};
  bool media_muted{false};       // true when media_volume == 0 OR player is_muted()
  float media_volume{0.f};       // 0..1
};
```

**Flag ownership notes:**
- `volume_buttons_touched`: set on button press; cleared by `media_player.yaml` after `delay: 2s`.
- `btn_action`: set on button press, cleared on button release — both from `buttons.yaml`.
- `jack_plugged` / `jack_unplugged`: set by `speaker.yaml`; **cleared by the engine** via
  `Scene::on_finished` when the Ripple animation finishes. The YAML `delay: 800ms + clear`
  pattern is removed in issue 11.
- `warning`: set by `home_assistant.yaml`; **cleared by the engine** via `Scene::on_finished`.
- `xmos_flashing_state == XMOS_SUCCESS/XMOS_ERROR`: set by `memory_flasher` callbacks; **cleared
  by the engine** via `Scene::on_finished`. The YAML `id(xmos_flashing_state) = 0` lines after
  `on_flashing_success`/`on_flashing_failed` are removed in issue 11.

#### Priority table (full, in evaluation order)

```cpp
SceneId StateMachine::resolve(const Facts& f) {
  // Table evaluated top-to-bottom; first match wins.
  if (f.xmos_flashing_state == XMOS_FLASHING) return SceneId::XMOS_FLASH;
  if (f.xmos_flashing_state == XMOS_SUCCESS)  return SceneId::XMOS_SUCCESS;
  if (f.xmos_flashing_state == XMOS_ERROR)    return SceneId::XMOS_ERROR;
  if (f.improv_ble)                            return SceneId::IMPROV;
  if (f.init_in_progress &&  f.network_ok)    return SceneId::INIT;
  if (f.init_in_progress && !f.network_ok)    return SceneId::INIT_NO_NETWORK;
  if (!f.network_ok)                           return SceneId::NO_HA;
  if (f.volume_buttons_touched)               return SceneId::VOLUME;
  if (f.btn_action)                           return SceneId::ACTION_BUTTON;
  if (f.jack_plugged)                         return SceneId::JACK_PLUGGED;
  if (f.jack_unplugged)                       return SceneId::JACK_UNPLUGGED;
  if (f.warning)                              return SceneId::WARNING;
  if (f.timer_ringing)                        return SceneId::TIMER_RING;
  if (f.va_phase == VA_WAITING)               return SceneId::WAITING;
  if (f.va_phase == VA_LISTENING)             return SceneId::LISTENING;
  if (f.va_phase == VA_THINKING)              return SceneId::THINKING;
  if (f.va_phase == VA_REPLYING)              return SceneId::REPLYING;
  if (f.va_phase == VA_ERROR)                 return SceneId::ERROR;
  if (f.va_phase == VA_NOT_READY)             return SceneId::NOT_READY;
  if (f.is_timer_active)                      return SceneId::TIMER_TICK;
  if (f.master_mute || f.media_muted)         return SceneId::MUTED;
  return SceneId::IDLE;
}
```

The `INIT` / `INIT_NO_NETWORK` split replaces the original single `init_in_progress` branch,
which internally checked `wifi.connected`. Making it explicit in the table allows each init
variant to have its own scene definition without conditional logic inside the scene.

`NO_HA` fires only post-init (`!init_in_progress` is implied by the earlier rows exhausting
the `init_in_progress` cases).

## Acceptance

- Unit-style checks against representative `Facts` inputs:
  - `{xmos_flashing_state=XMOS_FLASHING}` → `XMOS_FLASH`
  - `{init_in_progress=true, network_ok=false}` → `INIT_NO_NETWORK`
  - `{init_in_progress=true, network_ok=true}` → `INIT`
  - `{network_ok=false, init_in_progress=false}` → `NO_HA`
  - `{warning=true, va_phase=VA_WAITING}` → `WARNING` (warning outranks VA phases)
  - `{va_phase=VA_IDLE, master_mute=false, media_muted=false}` → `IDLE`
  - `{master_mute=true}` → `MUTED`
  - `{is_timer_active=true, master_mute=false}` → `TIMER_TICK`
- All checks logged via `ESP_LOGD` in a temporary `setup()` block, removed before merge.
