#pragma once

namespace esphome {
namespace led_ring_controller {

// Function pointer type for all easing functions.
// Stateless free functions — no heap allocation needed.
// t in [0, 1] → output in [0, 1]
using EasingFn = float (*)(float t);

float linear(float t);
float ease_in_out(float t);  // smoothstep: 3t² - 2t³
float sine(float t);         // 0.5 - 0.5·cos(π·t)

}  // namespace led_ring_controller
}  // namespace esphome
