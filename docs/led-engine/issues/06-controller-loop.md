# 06 — Controller loop: sole writer, render idle scene

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 02, 03, 04, 05

## Goal

Wire the engine into `loop()` and become the sole writer to the hardware strip. First
on-device visible result: a correct idle scene with crossfade to a second scene on manual
fact flip.

## Scope

### Controller members

```cpp
class LedRingController : public Component {
  // ... (from issue 01)
  Facts facts_;
  StateMachine sm_;
  SceneLibrary library_;
  Compositor compositor_;
  TransitionManager transition_;

  FrameBuffer work_{24};
  FrameBuffer prev_{24};

  SceneId active_scene_{SceneId::IDLE};
  uint32_t last_frame_ms_{0};
};
```

`library_.build(facts_)` is called in `setup()` after resolving LED count. Both `work_` and
`prev_` are sized to match the strip's actual LED count from
`strip_->get_addressable()->size()`.

### `setup()`

```cpp
void LedRingController::setup() {
  uint8_t num_leds = strip_->get_addressable()->size();
  work_  = FrameBuffer(num_leds);
  prev_  = FrameBuffer(num_leds);
  compositor_ = Compositor(num_leds);  // internal scratch buffer sized here too
  library_.build(facts_);
  facts_.init_in_progress = true;      // seed correct boot state
  last_frame_ms_ = millis();
}
```

### `loop()` — full render pipeline

```
1. THROTTLE
   if (millis() - last_frame_ms_ < frame_interval_ms_) return;
   uint32_t now_ms = millis();
   float dt = (now_ms - last_frame_ms_) / 1000.f;

2. BUILD RenderCtx
   auto lv = user_light_->current_values;
   Pixel base_color { lv.get_red(), lv.get_green(), lv.get_blue() };

   SceneId next_id = sm_.resolve(facts_);
   const Scene& scene = library_.get(next_id);

   float raw_b = lv.get_brightness();
   float base_brightness;
   switch (scene.brightness_mode) {
     case BrightnessMode::USER:    base_brightness = clamp(raw_b, 0.2f, 1.0f); break;
     case BrightnessMode::BOOSTED: base_brightness = clamp(raw_b + 0.1f, 0.2f, 1.0f); break;
     case BrightnessMode::FIXED:   base_brightness = scene.fixed_brightness; break;
   }

   RenderCtx ctx { now_ms, dt, base_color, base_brightness,
                   lv.get_state(),
                   facts_.media_volume, facts_.timer_ratio,
                   facts_.xmos_flash_progress };

3. SCENE CHANGE DETECTION
   if (next_id != active_scene_) {
     transition_.begin(prev_, scene.transition_in_ms, ease_in_out);
     // Reset all animations in the new scene.
     for (auto& layer : scene.layers) layer.anim->start(ctx);
     active_scene_ = next_id;
   }

4. COMPOSITE
   compositor_.render(work_, scene, ctx);

5. CROSSFADE (if active)
   if (transition_.active()) transition_.apply(work_, now_ms);

6. ONE-SHOT CHECK
   if (scene.one_shot) {
     bool all_done = true;
     for (const auto& layer : scene.layers)
       if (!layer.anim->is_finished()) { all_done = false; break; }
     if (all_done && scene.on_finished)
       scene.on_finished();   // clears the owning fact; resolve() will pick a different scene next frame
   }

7. COMMIT
   prev_ = work_;
   work_.write_to_strip(*strip_->get_addressable());
   strip_->get_addressable()->schedule_show();

8. last_frame_ms_ = now_ms;
```

### Initial scene set for this PR

`SceneLibrary::build()` needs to provide at least `IDLE` and one spin scene to satisfy the
acceptance criteria. A stub scene (e.g. `WAITING` with a solid-fill placeholder) is
sufficient — full primitives come in issues 08–09.

### Brightness / gamma note

Engine output is linear float 0..1. `write_to_strip` converts per channel as
`uint8_t(clamp(v, 0.f, 1.f) * 255.f + 0.5f)`. No additional gamma or brightness
multiplication is applied in the controller — `base_brightness` is already folded into
each animation's render output. The RMT strip outputs raw bytes; ESPHome strip-level
correction is not applied because `hw_led_ring` is `internal: true` and we write via the
`AddressableLight` pointer directly.

### Preventing double-write with `led_ring`

`hw_led_ring` must be marked `internal: true` in `led_ring.yaml`. The `led_ring` partition
entity remains user-facing for HA but never calls `schedule_show()` independently — the
controller is the only caller. If `led_ring` somehow schedules a show (e.g., user changes
brightness via HA), the next controller frame will overwrite it within 20ms, which is
acceptable.

## Acceptance

- On device, idle scene reflects `led_ring` color and brightness.
- A manual `facts_.warning = true` flip (temporary test setter in `dump_config`) causes the
  ring to crossfade to the warning/stub scene over `transition_in_ms`.
- No flicker or double-write artifacts.
- `ESP_LOGW` if `loop()` is running slower than `frame_interval_ms_ * 1.5` (detect
  scheduling overload early).

## Notes

This is the integration milestone. Behavior parity with the full scene set is not expected
yet — that arrives with issues 08–10. The engine just needs to render something correct and
not interfere with existing functionality.
