#include "scene_factory.h"

// The JSON scene parser is only compiled when the loader is enabled (enable_json_loader: true),
// so it costs no flash/RAM otherwise. The whole translation unit is gated.
#ifdef USE_LED_RING_JSON_LOADER

#include "animation.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

#include <cstdlib>
#include <cstring>
#include <memory>

namespace esphome {
namespace led_ring_controller {

static const char *const TAG = "led_ring_controller.json";

namespace {

bool scene_id_from_name(const char *name, SceneId &out) {
  struct Entry {
    const char *name;
    SceneId id;
  };
  static const Entry TABLE[] = {
      {"xmos_flash", SceneId::XMOS_FLASH},     {"xmos_success", SceneId::XMOS_SUCCESS},
      {"xmos_error", SceneId::XMOS_ERROR},     {"improv", SceneId::IMPROV},
      {"init", SceneId::INIT},                 {"init_no_network", SceneId::INIT_NO_NETWORK},
      {"no_ha", SceneId::NO_HA},               {"volume", SceneId::VOLUME},
      {"action_button", SceneId::ACTION_BUTTON}, {"jack_plugged", SceneId::JACK_PLUGGED},
      {"jack_unplugged", SceneId::JACK_UNPLUGGED}, {"warning", SceneId::WARNING},
      {"timer_ring", SceneId::TIMER_RING},     {"waiting", SceneId::WAITING},
      {"listening", SceneId::LISTENING},       {"thinking", SceneId::THINKING},
      {"replying", SceneId::REPLYING},         {"error", SceneId::ERROR},
      {"not_ready", SceneId::NOT_READY},       {"timer_tick", SceneId::TIMER_TICK},
      {"muted", SceneId::MUTED},               {"idle", SceneId::IDLE},
      {"success", SceneId::SUCCESS},
  };
  for (const auto &e : TABLE) {
    if (std::strcmp(e.name, name) == 0) {
      out = e.id;
      return true;
    }
  }
  return false;
}

bool parse_hex(const char *s, Pixel &out) {
  if (s == nullptr || s[0] != '#' || std::strlen(s) != 7)
    return false;
  auto hx = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  int v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = hx(s[i + 1]);
    if (v[i] < 0)
      return false;
  }
  out = {(v[0] * 16 + v[1]) / 255.0f, (v[2] * 16 + v[3]) / 255.0f, (v[4] * 16 + v[5]) / 255.0f};
  return true;
}

// Reads params["color"]: "user" -> is_user=true; "#RRGGBB" -> out. Missing color counts as
// "user" unless require_concrete (then it is an error).
bool get_color(JsonObject params, Pixel &out, bool &is_user, bool require_concrete) {
  if (!params["color"].is<const char *>()) {
    if (require_concrete) {
      ESP_LOGE(TAG, "primitive requires a concrete \"color\" (#RRGGBB)");
      return false;
    }
    is_user = true;
    return true;
  }
  const char *c = params["color"];
  if (std::strcmp(c, "user") == 0) {
    if (require_concrete) {
      ESP_LOGE(TAG, "primitive does not support \"color\": \"user\"");
      return false;
    }
    is_user = true;
    return true;
  }
  is_user = false;
  if (!parse_hex(c, out)) {
    ESP_LOGE(TAG, "invalid color '%s' (expected \"user\" or #RRGGBB)", c);
    return false;
  }
  return true;
}

std::vector<uint8_t> read_positions(JsonObject params, const char *key) {
  std::vector<uint8_t> v;
  JsonArray a = params[key].as<JsonArray>();
  if (!a.isNull()) {
    for (JsonVariant x : a)
      v.push_back(static_cast<uint8_t>(x.as<int>()));
  }
  return v;
}

std::unique_ptr<Animation> make_animation(JsonObject layer, bool &is_marker) {
  is_marker = false;
  const char *prim = layer["primitive"] | "";
  JsonObject p = layer["params"].as<JsonObject>();

  if (std::strcmp(prim, "solid") == 0) {
    Pixel col;
    bool is_user = false;
    if (!get_color(p, col, is_user, false))
      return nullptr;
    if (is_user)
      return std::make_unique<SolidFill>(static_cast<bool>(p["respect_light_on"] | true));
    return std::make_unique<SolidFill>(col);
  }

  if (std::strcmp(prim, "rotating_blob") == 0) {
    if (p["speed"].isNull()) {
      ESP_LOGE(TAG, "rotating_blob requires \"speed\"");
      return nullptr;
    }
    float speed = p["speed"];
    uint8_t trail = p["trail_len"] | 2;
    return std::make_unique<RotatingBlob>(RotatingBlobParams{speed, trail});
  }

  if (std::strcmp(prim, "pulse") == 0) {
    if (p["period_ms"].isNull()) {
      ESP_LOGE(TAG, "pulse requires \"period_ms\"");
      return nullptr;
    }
    PulseParams pp;
    pp.min_b = p["min_b"] | 0.0f;
    pp.max_b = p["max_b"] | 1.0f;
    pp.period_ms = p["period_ms"];
    pp.max_cycles = p["max_cycles"] | 0;
    pp.positions = read_positions(p, "positions");
    Pixel col;
    bool is_user = false;
    if (!get_color(p, col, is_user, false))
      return nullptr;
    if (is_user)
      return std::make_unique<Pulse>(std::move(pp));
    return std::make_unique<FixedColorPulse>(col, std::move(pp));
  }

  if (std::strcmp(prim, "progress_arc") == 0) {
    ProgressArcParams ap;
    ap.use_timer_ratio = std::strcmp(p["source"] | "volume", "timer") == 0;
    ap.reverse = p["reverse"] | false;
    ap.zero_indicator = {0.0f, 0.0f, 0.0f};
    if (p["zero_indicator"].is<const char *>() && !parse_hex(p["zero_indicator"], ap.zero_indicator)) {
      ESP_LOGE(TAG, "progress_arc invalid \"zero_indicator\"");
      return nullptr;
    }
    ap.moving_tick = p["moving_tick"] | false;
    ap.tick_step_ms = p["tick_step_ms"] | 100;
    return std::make_unique<ProgressArc>(std::move(ap));
  }

  if (std::strcmp(prim, "twinkle") == 0) {
    Pixel col;
    bool is_user = false;
    if (!get_color(p, col, is_user, true))
      return nullptr;
    float prob = p["probability"] | 0.5f;
    return std::make_unique<Twinkle>(TwinkleParams{prob, col});
  }

  if (std::strcmp(prim, "ripple") == 0) {
    return std::make_unique<Ripple>(RippleParams{static_cast<bool>(p["outward"] | true)});
  }

  if (std::strcmp(prim, "sweep") == 0) {
    Pixel col;
    bool is_user = false;
    if (!get_color(p, col, is_user, true))
      return nullptr;
    SweepParams sp;
    sp.fill_below = p["fill_below"] | false;
    sp.color = col;
    sp.step_interval_ms = p["step_interval_ms"] | 0;
    return std::make_unique<Sweep>(sp);
  }

  if (std::strcmp(prim, "markers") == 0) {
    auto positions = read_positions(p, "positions");
    if (positions.empty()) {
      ESP_LOGE(TAG, "markers requires non-empty \"positions\"");
      return nullptr;
    }
    Pixel col;
    bool is_user = false;
    if (!get_color(p, col, is_user, true))
      return nullptr;
    PositionMarkersParams mp;
    mp.positions = std::move(positions);
    mp.run = p["run"] | 1;
    mp.guard = p["guard"] | (p["width"] | 1);
    mp.color = col;
    is_marker = true;
    return std::make_unique<PositionMarkers>(std::move(mp));
  }

  ESP_LOGE(TAG,
           "unknown primitive '%s' (valid: solid, rotating_blob, pulse, progress_arc, twinkle, "
           "ripple, sweep, markers)",
           prim);
  return nullptr;
}

// A single fact token -> bool accessor. Returns an empty std::function for unknown names.
std::function<bool()> bool_fact(Facts &f, const std::string &name) {
  Facts *fp = &f;
  if (name == "master_mute")
    return [fp] { return fp->master_mute; };
  if (name == "media_muted")
    return [fp] { return fp->media_muted; };
  if (name == "improv_ble")
    return [fp] { return fp->improv_ble; };
  if (name == "init_in_progress")
    return [fp] { return fp->init_in_progress; };
  if (name == "network_ok")
    return [fp] { return fp->network_ok; };
  if (name == "volume_buttons_touched")
    return [fp] { return fp->volume_buttons_touched; };
  if (name == "btn_action")
    return [fp] { return fp->btn_action; };
  if (name == "jack_plugged")
    return [fp] { return fp->jack_plugged; };
  if (name == "jack_unplugged")
    return [fp] { return fp->jack_unplugged; };
  if (name == "warning")
    return [fp] { return fp->warning; };
  if (name == "timer_ringing")
    return [fp] { return fp->timer_ringing; };
  if (name == "is_timer_active")
    return [fp] { return fp->is_timer_active; };
  return nullptr;
}

std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t");
  size_t b = s.find_last_not_of(" \t");
  if (a == std::string::npos)
    return "";
  return s.substr(a, b - a + 1);
}

// A single term: optional leading '!', then a fact name. Returns nullptr on unknown name.
std::function<bool()> parse_term(Facts &f, const std::string &raw) {
  std::string t = trim(raw);
  bool negate = false;
  if (!t.empty() && t[0] == '!') {
    negate = true;
    t = trim(t.substr(1));
  }
  auto base = bool_fact(f, t);
  if (!base)
    return nullptr;
  if (!negate)
    return base;
  return [base] { return !base(); };
}

// Predicate grammar (a bounded, enumerated set — not arbitrary expressions):
//   "<fact>" | "!<fact>" | "<fact> && <fact>" |
//   "phase == <int>" | "xmos_flashing_state == <int>"
std::function<bool()> make_predicate(Facts &f, const std::string &when) {
  std::string w = trim(when);

  size_t eq = w.find("==");
  if (eq != std::string::npos) {
    std::string lhs = trim(w.substr(0, eq));
    int rhs = std::atoi(trim(w.substr(eq + 2)).c_str());
    Facts *fp = &f;
    if (lhs == "phase")
      return [fp, rhs] { return fp->va_phase == rhs; };
    if (lhs == "xmos_flashing_state")
      return [fp, rhs] { return fp->xmos_flashing_state == rhs; };
    ESP_LOGE(TAG, "unknown comparison lhs '%s' in \"when\"", lhs.c_str());
    return nullptr;
  }

  size_t amp = w.find("&&");
  if (amp != std::string::npos) {
    auto lhs = parse_term(f, w.substr(0, amp));
    auto rhs = parse_term(f, w.substr(amp + 2));
    if (!lhs || !rhs) {
      ESP_LOGE(TAG, "unknown fact in \"when\": %s", w.c_str());
      return nullptr;
    }
    return [lhs, rhs] { return lhs() && rhs(); };
  }

  auto term = parse_term(f, w);
  if (!term)
    ESP_LOGE(TAG, "unknown fact name in \"when\": '%s'", w.c_str());
  return term;
}

// Fixed clear-ownership: one-shot scenes clear their own fact via on_finished. Derived from the
// scene id so the JSON stays purely visual (matches scene_library).
void apply_oneshot_semantics(Scene &s, Facts &f) {
  Facts *fp = &f;
  switch (s.id) {
    case SceneId::WARNING:
      s.one_shot = true;
      s.on_finished = [fp] { fp->warning = false; };
      break;
    case SceneId::JACK_PLUGGED:
      s.one_shot = true;
      s.on_finished = [fp] { fp->jack_plugged = false; };
      break;
    case SceneId::JACK_UNPLUGGED:
      s.one_shot = true;
      s.on_finished = [fp] { fp->jack_unplugged = false; };
      break;
    case SceneId::XMOS_SUCCESS:
    case SceneId::XMOS_ERROR:
    case SceneId::SUCCESS:
      s.one_shot = true;
      s.on_finished = [fp] { fp->xmos_flashing_state = XMOS_IDLE; };
      break;
    default:
      break;
  }
}

bool parse_brightness_mode(JsonObject so, Scene &s) {
  const char *bm = so["brightness_mode"] | "user";
  if (std::strcmp(bm, "user") == 0) {
    s.brightness_mode = BrightnessMode::USER;
  } else if (std::strcmp(bm, "boosted") == 0) {
    s.brightness_mode = BrightnessMode::BOOSTED;
  } else if (std::strncmp(bm, "fixed:", 6) == 0) {
    s.brightness_mode = BrightnessMode::FIXED;
    s.fixed_brightness = static_cast<float>(std::atof(bm + 6));
  } else {
    ESP_LOGE(TAG, "unknown brightness_mode '%s' (user / boosted / fixed:<float>)", bm);
    return false;
  }
  return true;
}

}  // namespace

bool SceneFactory::load(const std::string &json, std::vector<Scene> &scenes_out,
                        std::vector<PriorityRule> &priority_out) {
  Facts &facts = this->facts_;

  bool ok = json::parse_json(json, [&](JsonObject root) -> bool {
    if ((root["version"] | 0) != 1) {
      ESP_LOGE(TAG, "unsupported or missing \"version\" (expected 1)");
      return false;
    }

    JsonObject scenes = root["scenes"].as<JsonObject>();
    if (scenes.isNull()) {
      ESP_LOGE(TAG, "missing \"scenes\" object");
      return false;
    }
    for (JsonPair kv : scenes) {
      SceneId id;
      if (!scene_id_from_name(kv.key().c_str(), id)) {
        ESP_LOGE(TAG, "unknown scene name '%s'", kv.key().c_str());
        return false;
      }
      JsonObject so = kv.value().as<JsonObject>();
      Scene scene;
      scene.id = id;
      scene.transition_in_ms = so["transition_in_ms"] | 0;
      if (!parse_brightness_mode(so, scene))
        return false;

      JsonArray layers = so["layers"].as<JsonArray>();
      if (layers.isNull()) {
        ESP_LOGE(TAG, "scene '%s' missing \"layers\"", kv.key().c_str());
        return false;
      }
      for (JsonObject layer : layers) {
        bool is_marker = false;
        auto anim = make_animation(layer, is_marker);
        if (!anim)
          return false;
        Layer l;
        l.anim = std::move(anim);
        l.in_place = is_marker;
        l.alpha = layer["alpha"] | 1.0f;
        if (layer["when"].is<const char *>()) {
          auto pred = make_predicate(facts, layer["when"].as<const char *>());
          if (!pred)
            return false;
          l.enabled_pred = pred;
        }
        scene.layers.push_back(std::move(l));
      }

      apply_oneshot_semantics(scene, facts);
      scenes_out.push_back(std::move(scene));
    }

    JsonArray priority = root["priority"].as<JsonArray>();
    if (!priority.isNull()) {
      for (JsonObject row : priority) {
        SceneId sid;
        if (!scene_id_from_name(row["scene"] | "", sid)) {
          ESP_LOGE(TAG, "priority row has unknown scene '%s'", row["scene"] | "");
          return false;
        }
        if (row["default"] | false) {
          priority_out.emplace_back([] { return true; }, sid);
        } else if (row["when"].is<const char *>()) {
          auto pred = make_predicate(facts, row["when"].as<const char *>());
          if (!pred)
            return false;
          priority_out.emplace_back(pred, sid);
        } else {
          ESP_LOGE(TAG, "priority row needs \"when\" or \"default\"");
          return false;
        }
      }
    }
    return true;
  });

  if (!ok) {
    ESP_LOGE(TAG, "scene JSON load failed; active scenes unchanged");
    scenes_out.clear();
    priority_out.clear();
  }
  return ok;
}

}  // namespace led_ring_controller
}  // namespace esphome

#endif  // USE_LED_RING_JSON_LOADER
