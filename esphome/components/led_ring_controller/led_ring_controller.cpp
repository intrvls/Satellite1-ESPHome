#include "led_ring_controller.h"

namespace esphome {
namespace led_ring_controller {

static const char *const TAG = "led_ring_controller";

void LedRingController::setup() {
  if (this->strip_ == nullptr) {
    ESP_LOGE(TAG, "strip not set");
    this->mark_failed();
    return;
  }
}

void LedRingController::loop() {
  // Implemented in issue 06.
}

void LedRingController::dump_config() {
  auto *addressable = static_cast<light::AddressableLight *>(this->strip_->get_output());
  ESP_LOGCONFIG(TAG, "LedRingController:");
  ESP_LOGCONFIG(TAG, "  Strip LEDs: %d", addressable->size());
  ESP_LOGCONFIG(TAG, "  Frame interval: %u ms", this->frame_interval_ms_);
}

}  // namespace led_ring_controller
}  // namespace esphome
