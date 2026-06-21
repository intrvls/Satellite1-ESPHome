# 12 — Markup format: JSON scene descriptors + runtime loader

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 10
**Design:** [MARKUP_FORMAT.md](../MARKUP_FORMAT.md)

## Goal

Make scenes and the priority table describable in **JSON** so they can be defined or edited
without recompiling — eventually pushable at runtime from Home Assistant.

## Scope

### `engine/scene_factory.h/.cpp`

```cpp
class SceneFactory {
 public:
  // facts must outlive the SceneFactory (same lifetime contract as SceneLibrary).
  // The factory closes over specific facts fields when parsing predicate strings.
  explicit SceneFactory(Facts& facts);

  // Parse a JSON document and produce a vector of Scenes + a priority table.
  // Returns false and logs errors on unknown primitive/fact/key names.
  bool load(const char* json, size_t len,
            std::vector<Scene>& scenes_out,
            std::vector<std::pair<std::function<bool()>, SceneId>>& priority_out);

 private:
  Facts& facts_;

  // Map primitive name string → factory function
  std::unique_ptr<Animation> make_animation(const JsonObject& obj) const;

  // Map fact-name string → nullary predicate closing over facts_
  // Supported names: "master_mute", "media_muted", "improv_ble", "init_in_progress",
  //   "network_ok", "volume_buttons_touched", "btn_action", "jack_plugged",
  //   "jack_unplugged", "warning", "timer_ringing", "is_timer_active",
  //   "phase == <int>" (e.g., "phase == 2")
  // Returns nullptr for unknown names and logs an error.
  std::function<bool()> make_predicate(const char* when_str) const;
};
```

`SceneFactory::load()` parses via ArduinoJson (already in ESPHome toolchain). Closures for
`enabled_pred` and `on_finished` are constructed identically to how `scene_library::build()`
constructs them — the factory is just another producer of the same structs.

### Two producers, one struct format

```
compile-time:  scene_library::build(facts_)     → Scene/Layer structs (hardcoded)
runtime:       SceneFactory::load(json, facts_) → Scene/Layer structs (from JSON)
```

Engine internals never see JSON. After `load()`, the controller replaces the active scene
set atomically (swap pointer or copy into the library's array).

### Compile-time path: `default_scenes.json`

The default scene set is checked in as `engine/default_scenes.json`. At compile time,
a build step (or `__init__.py` codegen) embeds it as a PROGMEM `const char[]`. On boot,
`SceneFactory::load()` parses this to confirm JSON parity with the hardcoded `SceneLibrary`.

This gives a single source of truth for the default scenes in JSON form, and provides a
regression test: if the hardcoded scenes and the JSON produce different visuals, the JSON
is out of date.

### Runtime path: install from HA

A `led_ring_controller.load_scenes` action accepts a string (e.g., from a `text` entity
or an HA service call) and calls `SceneFactory::load()` with the controller's `facts_`
reference. The new scenes replace the active set without restarting the controller.

This action is **optional** and gated by a config flag to avoid flash/RAM cost where unused:

```yaml
led_ring_controller:
  ...
  enable_json_loader: true   # adds ~6 KB flash for ArduinoJson scene parser
```

### JSON schema

```jsonc
{
  "version": 1,
  "scenes": {
    "thinking": {
      "transition_in_ms": 200,
      "brightness_mode": "user",
      "layers": [
        {
          "primitive": "pulse",
          "params": {
            "color": "user",          // "user" = base_color from RenderCtx
            "min_b": 0.0,
            "max_b": 1.0,
            "period_ms": 200,
            "max_cycles": 0,
            "positions": [2, 14]
          }
        }
      ]
    },
    "muted": {
      "transition_in_ms": 150,
      "brightness_mode": "boosted",
      "layers": [
        { "primitive": "solid", "params": { "color": "user" } },
        {
          "primitive": "markers",
          "when": "master_mute",
          "params": { "positions": [0,6,12,18], "width": 1, "color": "#FF0000" }
        },
        {
          "primitive": "markers",
          "when": "media_muted",
          "params": { "positions": [1,7,13,19], "width": 0, "color": "#C80000" }
        }
      ]
    }
  },
  "priority": [
    { "when": "xmos_flashing_state == 1", "scene": "xmos_flash" },
    { "when": "improv_ble",               "scene": "improv" },
    { "when": "phase == 2",               "scene": "waiting" },
    { "when": "master_mute",              "scene": "muted" },
    { "default": true,                    "scene": "idle" }
  ]
}
```

**Conventions:**
- `"color": "user"` — reads `ctx.base_color * ctx.base_brightness` at render time.
- `"color": "#RRGGBB"` — fixed hex color, parsed once at load time.
- `"when"` on a layer — maps to `layer.enabled_pred` via `make_predicate()`.
- `"when"` on a priority row — maps to the priority table predicate.
- `"brightness_mode"`: `"user"` / `"boosted"` / `"fixed:<float>"`.
- Predicates are a **fixed, enumerated set** — no arbitrary expression evaluation.
  Unknown `"when"` strings are rejected with an error log; the load is aborted.

### Validation rules

- Unknown `"primitive"` name → reject with `ESP_LOGE` listing valid names.
- Unknown `"when"` fact name → reject.
- Unknown `"brightness_mode"` → reject.
- Missing required params (e.g., `pulse` without `period_ms`) → reject.
- `"version"` field mismatch → reject.
- Partial load failure → entire `load()` returns false; active scene set is unchanged.

## Acceptance

- `default_scenes.json` parsed successfully at boot with `enable_json_loader: true`.
- The JSON-defined scenes produce visually identical output to the hardcoded `SceneLibrary`
  scenes (verified on device by side-by-side toggling between JSON and hardcoded paths).
- A modified JSON (e.g., changed transition duration or added a layer) takes effect without
  recompiling when loaded via `led_ring_controller.load_scenes` from an HA service call.
- With `enable_json_loader: false` (default), binary size is unchanged vs issue 10.
- Unknown primitive names log a clear error and abort the load without crashing.
