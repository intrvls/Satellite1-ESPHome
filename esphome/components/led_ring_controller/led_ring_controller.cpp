#include "led_ring_controller.h"
#include "frame.h"

namespace esphome {
namespace led_ring_controller {

static const char *const TAG = "led_ring_controller";

void LedRingController::setup() {
  if (this->strip_ == nullptr) {
    ESP_LOGE(TAG, "strip not set");
    this->mark_failed();
    return;
  }

  // Issue 02 acceptance verification — remove before issue 06 merge.
  {
    FrameBuffer a(4), b(4);
    a.fill({1.0f, 0.0f, 0.0f});  // red
    b.fill({0.0f, 0.0f, 1.0f});  // blue

    auto mid = FrameBuffer::crossfade(a, b, 0.5f);
    ESP_LOGD(TAG, "crossfade(red,blue,0.5): r=%.2f g=%.2f b=%.2f  expect(0.50 0.00 0.50)",
             mid[0].r, mid[0].g, mid[0].b);

    a.fill({1.0f, 0.0f, 0.0f});  // reset
    a.blend_over(b, 0.5f);
    ESP_LOGD(TAG, "blend_over(red,blue,0.5): r=%.2f g=%.2f b=%.2f  expect(0.50 0.00 0.50)",
             a[0].r, a[0].g, a[0].b);
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
