#pragma once

#include "scene.h"
#include "state_machine.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace esphome {
namespace led_ring_controller {

// A priority rule: a nullary predicate over Facts + the scene to select when it matches.
// The last rule is typically an always-true default.
using PriorityRule = std::pair<std::function<bool()>, SceneId>;

// Runtime producer of Scene/Layer structs from a JSON document — the sibling of the
// compile-time scene_library::build(). Engine internals never see JSON; the factory just emits
// the same structs. Closures for enabled_pred / on_finished capture `facts` by reference, so
// `facts` must outlive everything the factory produces (same contract as SceneLibrary).
class SceneFactory {
 public:
  explicit SceneFactory(Facts &facts) : facts_(facts) {}

  // Parse `json` into scenes_out + priority_out. Returns false (and leaves the outputs empty)
  // on any error: bad JSON, unknown version, unknown scene/primitive/predicate name, or a
  // missing required param. Errors are logged via ESP_LOGE.
  bool load(const std::string &json, std::vector<Scene> &scenes_out,
            std::vector<PriorityRule> &priority_out);

 private:
  Facts &facts_;
};

}  // namespace led_ring_controller
}  // namespace esphome
