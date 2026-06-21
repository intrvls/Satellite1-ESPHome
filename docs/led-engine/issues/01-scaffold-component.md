# 01 — Scaffold `led_ring_controller` component + codegen skeleton

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** none

## Goal

Create the new external component so it registers and compiles in CI — no behavior yet.

## Scope

### Directory structure

```
esphome/components/led_ring_controller/
  __init__.py
  led_ring_controller.h
  led_ring_controller.cpp
```

### `__init__.py` — config schema

```python
CODEOWNERS = ["@futureproofhomes"]
AUTO_LOAD = []

led_ring_controller_ns = cg.esphome_ns.namespace("led_ring_controller")
LedRingController = led_ring_controller_ns.class_(
    "LedRingController", cg.Component
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LedRingController),
    cv.Required("strip_id"):    cv.use_id(light.AddressableLightState),
    cv.Required("user_light_id"): cv.use_id(light.LightState),
    cv.Optional("frame_interval", default="20ms"):
        cv.positive_time_period_milliseconds,
}).extend(cv.COMPONENT_SCHEMA)
```

`to_code` resolves both light pointers and calls setters on the component.

### `led_ring_controller.h`

```cpp
namespace led_ring_controller {

class LedRingController : public Component {
 public:
  void set_strip(light::AddressableLightState *strip) { strip_ = strip; }
  void set_user_light(light::LightState *user_light) { user_light_ = user_light; }
  void set_frame_interval_ms(uint32_t ms) { frame_interval_ms_ = ms; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  light::AddressableLightState *strip_{nullptr};
  light::LightState *user_light_{nullptr};
  uint32_t frame_interval_ms_{20};
};

}  // namespace led_ring_controller
```

### `led_ring_controller.cpp`

Empty `setup()`, `loop()`, and a `dump_config()` that logs:

```
LedRingController:
  Strip LEDs: 24
  Frame interval: 20 ms
```

LED count is read from the strip's `AddressableLight` at setup time via
`strip_->get_addressable()->size()`.

## Test YAML

Add a minimal block to `config/satellite1.yaml` (behind a comment guard for this PR only):

```yaml
led_ring_controller:
  strip_id: hw_led_ring        # must be marked internal: true here
  user_light_id: led_ring
  frame_interval: 20ms
```

`hw_led_ring` **must** be marked `internal: true` in `led_ring.yaml` for this PR's test to
compile cleanly (prevents ESPHome from generating a redundant HA entity for the raw strip).
Keep `led_ring` (the partition) exposed as-is — it remains the user-facing HA entity.

## Acceptance

- `esphome compile config/satellite1.yaml` succeeds with the test block added.
- `dump_config()` log line appears at boot with the correct LED count (24) and frame interval.
- No warnings about duplicate light entities or missing IDs.

## Notes

Does not touch `led_ring.yaml` effects or `control_leds` script. No rendering. The component
is wired but does nothing in `loop()` — that comes in issue 06.
