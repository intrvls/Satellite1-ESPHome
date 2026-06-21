#pragma once

#include "scene.h"
#include "state_machine.h"

#include <array>

namespace esphome {
namespace led_ring_controller {

class SceneLibrary {
 public:
  // Constructs all scenes and wires enabled_pred / on_finished closures over facts.
  // facts must outlive the SceneLibrary (owned by LedRingController) and must not be moved
  // after build() — the closures capture its fields by reference.
  void build(Facts &facts);

  const Scene &get(SceneId id) const { return scenes_[static_cast<size_t>(id)]; }

 private:
  // Returns the slot for id, stamping its id field. Used by build().
  Scene &slot(SceneId id) {
    Scene &s = scenes_[static_cast<size_t>(id)];
    s.id = id;
    return s;
  }

  std::array<Scene, SCENE_COUNT> scenes_;
};

}  // namespace led_ring_controller
}  // namespace esphome
