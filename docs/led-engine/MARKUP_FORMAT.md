# Markup Format for the LED Ring Controller

## Decision

**Use JSON scene descriptors as the runtime markup format**, with an optional YAML authoring
layer (compile-time) that emits the same descriptor structs. Reject a bespoke DSL.

## What the format must describe

1. **Scenes** — a named visual state, built from one or more layers.
2. **Layers** — a primitive + params + blend mode + alpha + an optional enable predicate.
3. **Primitives** — the parameterized building blocks (`solid`, `rotating_blob`, `pulse`,
   `progress_arc`, `twinkle`, `ripple`, `sweep`, `markers`).
4. **Priority table** — ordered `{predicate-over-facts → scene}` rules (the `control_leds`
   ladder, made data).
5. **Transitions** — per-scene `transition_in_ms` + easing.

## Options considered

| Format | Pros | Cons |
|--------|------|------|
| **JSON** (chosen) | Runtime-loadable (push from HA via API/service, no reflash); ArduinoJson already in ESPHome toolchain; maps 1:1 to POD param structs; human-editable; easy to validate | Slightly verbose; needs a schema doc |
| YAML (ESPHome-native) | Familiar to users; integrates with existing config codegen | Only available at **compile time** (no runtime push); heavier parser on-device; ESPHome YAML is already the config layer — nesting a second YAML doc is confusing |
| Bespoke DSL | Compact, expressive | Parser + tooling + editor support cost; no ecosystem; high maintenance for marginal gain over JSON |
| Lua / scripting | Maximum flexibility | Large flash/RAM cost on ESP32; sandboxing/safety; overkill for declarative scenes |

**Why JSON wins:** the stated goal is "a markup language that can control animations and LED
states," ideally without reflashing. JSON is the only option that is both runtime-pushable
(over the HA API / a service call / an HTTP endpoint) and a near-direct serialization of the
engine's POD param structs. The same JSON can also be **baked to PROGMEM at compile time** for
the default hardcoded scenes, so there is one format end to end.

## Layering: YAML authoring → JSON descriptors → engine structs

- **Compile time (optional):** authors may write scenes in ESPHome YAML; codegen serializes
  them to the same descriptor structs (baked to flash). Good for the built-in default scenes.
- **Runtime (the capability):** a JSON document is parsed by a `SceneFactory` into
  `Scene`/`Layer` structs and installed into the engine — no recompile. Source can be an HA
  service call, a text entity, or an HTTP POST.
- Engine internals never see JSON or YAML — they only see structs. Hardcoding lives **only** in
  `scene_library.cpp`; the markup loader is just another producer of the same structs.

## Draft JSON schema (illustrative)

```jsonc
{
  "version": 1,
  "scenes": {
    "thinking": {
      "transition_in_ms": 200,
      "layers": [
        { "primitive": "pulse",
          "params": { "color": "user", "min": 0.0, "max": 1.0, "period_ms": 200 } }
      ]
    },
    "muted": {
      "transition_in_ms": 150,
      "layers": [
        { "primitive": "solid", "params": { "color": "user" } },
        { "primitive": "markers", "when": "master_mute",
          "params": { "positions": [0,6,12,18], "width": 1, "color": "#FF0000" },
          "blend": "over", "alpha": 1.0 }
      ]
    }
  },
  "priority": [
    { "when": "xmos_flashing", "scene": "xmos_flash" },
    { "when": "improv_ble",    "scene": "improv" },
    { "when": "phase == thinking", "scene": "thinking" },
    { "when": "master_mute",   "scene": "muted" },
    { "default": true,         "scene": "idle" }
  ]
}
```

Conventions:
- `color`: `"user"` (read live from `led_ring.current_values`) or a `#RRGGBB` literal.
- `when`: a named fact (`master_mute`, `improv_ble`, `xmos_flashing`, …) or a simple
  `phase == <name>` comparison. Predicates are a fixed, enumerated set — no arbitrary
  expression evaluation on-device.
- `priority` is evaluated top-to-bottom; first match wins (mirrors the current ladder).

## Implementation notes (tracked in [issue 12](issues/12-markup-format.md))

- Validate against the fixed primitive/fact enums; reject unknown keys with a clear log line.
- Keep the runtime parser optional behind a config flag to avoid flash cost where unused.
- Provide the default scene set as a checked-in `.json` that doubles as documentation and the
  PROGMEM-baked source of truth.
