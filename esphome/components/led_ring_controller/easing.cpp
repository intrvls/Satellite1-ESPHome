#include "easing.h"

#include <cmath>

namespace esphome {
namespace led_ring_controller {

float linear(float t) { return t; }

float ease_in_out(float t) { return t * t * (3.0f - 2.0f * t); }

float sine(float t) { return 0.5f - 0.5f * cosf(static_cast<float>(M_PI) * t); }

}  // namespace led_ring_controller
}  // namespace esphome
