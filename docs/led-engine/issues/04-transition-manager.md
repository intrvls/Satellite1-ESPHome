# 04 — TransitionManager (crossfade)

**Epic:** [Native LED Ring Animation Engine](../EPIC.md) · **Depends on:** 02, 03

## Goal

Smooth crossfades between scenes instead of today's instant cuts.

## Scope

### `engine/transition.h/.cpp`

```cpp
class TransitionManager {
 public:
  // Snapshot prev_final as the "from" frame and start a timed crossfade.
  // duration_ms == 0: hard cut — begin() marks the transition immediately inactive.
  // easing: any EasingFn from easing.h (typically ease_in_out).
  void begin(const FrameBuffer& prev_final, uint32_t duration_ms, EasingFn easing);

  bool active() const;

  // Blend prev_final_ into new_frame in-place using elapsed time.
  // result[i] = crossfade(prev_final_[i], new_frame[i], easing_(t))  where t = elapsed/duration
  // Once t >= 1.0, marks itself inactive; subsequent calls are no-ops.
  void apply(FrameBuffer& new_frame, uint32_t now_ms) const;

 private:
  FrameBuffer prev_final_{24};
  uint32_t start_ms_{0};
  uint32_t duration_ms_{0};
  EasingFn easing_{linear};
  bool active_{false};
};
```

**`EasingFn`** is `float (*)(float t)` defined in `easing.h` (issue 02). Function pointer
keeps this allocation-free; all three easings are stateless free functions.

### Behaviour details

| Condition | Result |
|-----------|--------|
| `duration_ms == 0` | Hard cut: `begin()` sets `active_ = false` immediately; `apply()` is a no-op |
| Mid-transition scene change | Controller calls `begin()` again with the current (blended) `work_` frame as `prev_final`; crossfade re-starts from the current mixed output with no pop |
| `t >= 1.0` | `apply()` becomes a no-op; `active()` returns false |

### Default easing

`ease_in_out` (smoothstep) for all scene transitions. The controller always passes
`ease_in_out` in issue 06; per-scene easing can be added to `Scene` later if needed.

## Acceptance

- Switching scenes blends over the configured duration.
- `transition_in_ms: 0` produces a hard cut with no intermediate frames.
- A mid-transition scene change re-snapshots from the current blended frame — verified by
  manually triggering two rapid scene changes in `dump_config`-time test code.
- `active()` returns false once `t >= 1.0`.
