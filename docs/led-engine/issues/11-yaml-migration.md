# 11 — YAML migration: rewire triggers, gut led_ring.yaml

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 10

## Goal

Cut over to the engine: rewire all YAML triggers to setter actions and remove the old
dispatcher entirely. The engine already produces behavioral parity (issue 10); this PR makes
that the live path.

## Scope

### `config/common/led_ring.yaml` — structural changes

**Add:**
```yaml
led_ring_controller:
  id: led_controller
  strip_id: hw_led_ring
  user_light_id: led_ring
  frame_interval: 20ms
```

**Mark `hw_led_ring` internal:**
```yaml
light:
  - platform: esp32_rmt_led_strip
    id: hw_led_ring
    internal: true   # ← add this
    ...
```

**Delete entirely:**
- `voice_assistant_leds` partition light (all ~25 `addressable_lambda` effects under it)
- `control_leds` script and all `control_leds_*` child scripts
- `global_led_animation_index` global
- `led_anim_speed` global

**Keep:**
- `hw_led_ring` (now internal)
- `led_ring` partition (user-facing HA entity — unchanged)

### Call-site rewiring (~45 sites across 9 files)

The mechanical replacement rule:
- `set <global_var>; script.execute: control_leds` → setter action(s)
- `script.execute: control_leds` alone → **delete** (engine re-resolves every frame)

#### `config/common/voice_assistant.yaml` (~13 sites)

| Old | New |
|-----|-----|
| `id(voice_assistant_phase) = VA_NOT_READY; script.execute: control_leds` | `led_ring_controller.set_phase: 10` |
| `id(voice_assistant_phase) = VA_WAITING; script.execute: control_leds` | `led_ring_controller.set_phase: 2` |
| `id(voice_assistant_phase) = VA_LISTENING; script.execute: control_leds` | `led_ring_controller.set_phase: 3` |
| `id(voice_assistant_phase) = VA_THINKING; script.execute: control_leds` | `led_ring_controller.set_phase: 4` |
| `id(voice_assistant_phase) = VA_REPLYING; script.execute: control_leds` | `led_ring_controller.set_phase: 5` |
| `id(voice_assistant_phase) = VA_ERROR; script.execute: control_leds` | `led_ring_controller.set_phase: 11` |
| `id(voice_assistant_phase) = VA_IDLE; script.execute: control_leds` | `led_ring_controller.set_phase: 1` |
| Standalone `script.execute: control_leds` (re-dispatches after error) | **delete** |

#### `config/satellite1.base.yaml` (~6 sites)

| Old | New |
|-----|-----|
| `id(init_in_progress) = true; script.execute: control_leds` | `led_ring_controller.set_flag: {flag: init_in_progress, value: true}` |
| `id(init_in_progress) = false; script.execute: control_leds` | `led_ring_controller.set_flag: {flag: init_in_progress, value: false}` |
| `id(xmos_flashing_state) = 1; script.execute: control_leds` (on_flashing_start) | `led_ring_controller.event: xmos_flash_start` |
| `id(global_led_animation_index) = ...; script.execute: control_leds` (on_progress_update) | `led_ring_controller.event: {event: xmos_flash_progress, value: !lambda return id(xflash).flashing_progress;}` |
| `id(xmos_flashing_state) = 2; script.execute: control_leds; id(xmos_flashing_state) = 0` (on_flashing_success) | `led_ring_controller.event: xmos_success` ← **remove the trailing `= 0`** (engine clears via on_finished) |
| `id(xmos_flashing_state) = 3; script.execute: control_leds; id(xmos_flashing_state) = 0` (on_flashing_failed) | `led_ring_controller.event: xmos_error` ← **remove the trailing `= 0`** |

#### `config/common/home_assistant.yaml` (~5 sites)

| Old | New |
|-----|-----|
| `id(init_in_progress) = false; script.execute: control_leds` (on api connected) | `led_ring_controller.set_flag: {flag: init_in_progress, value: false}` |
| `id(warning) = true; script.execute: control_leds` | `led_ring_controller.event: warning` |
| WiFi connected/disconnected → `script.execute: control_leds` | `led_ring_controller.set_flag: {flag: network_ok, value: true/false}` |
| API connected/disconnected → `script.execute: control_leds` | `led_ring_controller.set_flag: {flag: network_ok, value: true/false}` (AND with wifi state) |

Note: `network_ok` requires both WiFi AND API connected. Wire both `on_connect`/`on_disconnect`
events from both `wifi_id` and `api_id`, and set `network_ok = wifi.is_connected() && api.is_connected()`.

#### `config/common/media_player.yaml` (~5 sites)

| Old | New |
|-----|-----|
| `id(volume_buttons_touched) = false; script.execute: control_leds` (after 2s) | `led_ring_controller.set_flag: {flag: volume_buttons_touched, value: false}` (keep the 2s delay — **YAML retains ownership of this timer**) |
| Media volume change → `script.execute: control_leds` | `led_ring_controller.set_media_volume: !lambda return id(external_media_player).volume;` |

#### `config/satellite1.yaml` (~4 sites) and `config/common/speaker.yaml` (~4 sites)

| Old | New |
|-----|-----|
| `id(jack_plugged_recently) = true; script.execute: control_leds` (on_press) | `led_ring_controller.event: jack_plugged` |
| `delay: 200ms; delay: 800ms; id(jack_plugged_recently) = false; script.execute: control_leds` | **delete entirely** — engine auto-clears via `on_finished` |
| Same pattern for `jack_unplugged_recently` | `led_ring_controller.event: jack_unplugged`; delete the delay+clear |

#### `config/common/timer.yaml` (~2 sites)

| Old | New |
|-----|-----|
| Timer active → `script.execute: control_leds` | `led_ring_controller.set_flag: {flag: is_timer_active, value: true}` |
| `first_active_timer` ratio update | `led_ring_controller.set_timer_ratio: !lambda return id(first_active_timer).seconds_left / max(...);` |
| Timer ring → `script.execute: control_leds` | `led_ring_controller.set_flag: {flag: timer_ringing, value: true/false}` |

#### `config/common/buttons.yaml` (~2 sites)

| Old | New |
|-----|-----|
| `id(volume_buttons_touched) = true; script.execute: control_leds` (on_press vol+/-) | `led_ring_controller.set_flag: {flag: volume_buttons_touched, value: true}` |
| Action button `on_press` | `led_ring_controller.set_flag: {flag: btn_action, value: true}` |
| Action button `on_release` | `led_ring_controller.set_flag: {flag: btn_action, value: false}` |

#### `config/common/wifi_improv.yaml` (~2 sites)

| Old | New |
|-----|-----|
| `id(improv_ble_in_progress) = true/false; script.execute: control_leds` | `led_ring_controller.set_flag: {flag: improv_ble, value: true/false}` |

### Globals to delete from `satellite1.base.yaml` / `led_ring.yaml` / `buttons.yaml`

Once all call sites are rewired, delete these YAML globals (they have no remaining consumers):
- `global_led_animation_index` (led_ring.yaml)
- `led_anim_speed` (led_ring.yaml)
- `jack_plugged_recently` (speaker.yaml) — replaced by `facts_.jack_plugged`
- `jack_unplugged_recently` (speaker.yaml) — replaced by `facts_.jack_unplugged`
- `volume_buttons_touched` (buttons.yaml) — replaced by `facts_.volume_buttons_touched`
- `warning` (satellite1.base.yaml) — replaced by `facts_.warning`
- `init_in_progress` (satellite1.base.yaml) — replaced by `facts_.init_in_progress`
- `xmos_flashing_state` (satellite1.base.yaml) — replaced by `facts_.xmos_flashing_state`
- `improv_ble_in_progress` (wifi_improv.yaml) — replaced by `facts_.improv_ble`

`voice_assistant_phase` (voice_assistant.yaml) is kept as-is — it drives the existing VA
pipeline logic beyond just LEDs (e.g., the barge-in / ignore-mid-command conditions). The
setter actions call both `id(voice_assistant_phase) = X` and `set_phase: X` until the phase
global can be retired in a later refactor.

## Acceptance

- Full device pass of every state path (see EPIC verification) with no regressions.
- `led_ring` color/brightness + HA on/off still control idle appearance.
- Grep confirms no remaining references to `control_leds`, `voice_assistant_leds`,
  `global_led_animation_index`, `led_anim_speed`, `jack_plugged_recently`,
  `jack_unplugged_recently` in the config YAML files.
- `esphome compile config/satellite1.yaml` clean with no warnings about undefined IDs.

## Notes

This is the largest PR. If staged rollout is preferred, land the engine behind a build flag
first (issue 06 already suggests this) and run issues 08–10 on-device before cutting over.
After this PR, the old YAML LED system is gone and cannot be partially reverted without
reverting the whole PR.
