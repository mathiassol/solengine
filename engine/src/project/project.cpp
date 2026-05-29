// project.cpp

#include "sol/project/project.h"
#include "sol/log.h"

#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace sol {

ProjectConfig ProjectConfig::load(const std::string& path) {
    ProjectConfig cfg;
    std::ifstream f(path);
    if (!f.is_open()) {
        SOL_WARN("ProjectConfig: cannot open '" + path + "', using defaults");
        return cfg;
    }

    json j;
    try { f >> j; }
    catch (const std::exception& e) {
        SOL_ERROR(std::string("ProjectConfig parse error: ") + e.what());
        return cfg;
    }

    cfg.name       = j.value("name",       cfg.name);
    cfg.version    = j.value("version",    cfg.version);
    cfg.main_scene = j.value("main_scene", cfg.main_scene);

    if (j.contains("window")) {
        const auto& w  = j["window"];
        cfg.window_title  = w.value("title",  cfg.window_title);
        cfg.window_width  = w.value("width",  cfg.window_width);
        cfg.window_height = w.value("height", cfg.window_height);
        cfg.window_vsync  = w.value("vsync",  cfg.window_vsync);
    }

    if (j.contains("input_actions") && j["input_actions"].is_object()) {
        for (auto& [action_name, bindings_j] : j["input_actions"].items()) {
            InputActionConfig ac;
            ac.name = action_name;

            if (bindings_j.is_array()) {
                for (const auto& bj : bindings_j) {
                    InputBinding b;
                    if (bj.contains("key")) {
                        b.type = "key";
                        b.key  = bj["key"].get<std::string>();
                    } else if (bj.contains("mouse_button")) {
                        b.type   = "mouse_button";
                        b.button = bj["mouse_button"].get<std::string>();
                    } else if (bj.contains("mouse_wheel")) {
                        b.type  = "mouse_wheel";
                        b.wheel = bj["mouse_wheel"].get<std::string>();
                    } else if (bj.contains("gamepad_button")) {
                        b.type = "gamepad_button";
                        b.code = bj["gamepad_button"].get<int>();
                    } else if (bj.contains("gamepad_axis")) {
                        b.type     = "gamepad_axis";
                        b.code     = bj["gamepad_axis"].get<int>();
                        b.positive = bj.value("positive", true);
                        b.deadzone = bj.value("deadzone", 0.15f);
                    }
                    ac.bindings.push_back(b);
                }
            }

            cfg.input_actions.push_back(ac);
        }
    }

    return cfg;
}

} // namespace sol
