#pragma once
#include "sol/export.h"
#include <string>
#include <vector>

namespace sol {

// ---- Input action configuration (parsed from "input_actions" in project.sol) ----

struct SOL_API InputBinding {
    std::string type;      // "key", "mouse_button", "mouse_wheel", "gamepad_button", "gamepad_axis"
    std::string key;       // e.g. "KEY_SPACE" — for type="key"
    std::string button;    // "LEFT", "RIGHT", "MIDDLE" — for type="mouse_button"
    std::string wheel;     // "UP", "DOWN" — for type="mouse_wheel"
    int         code     = 0;
    bool        positive = true;
    float       deadzone = 0.15f;
};

struct SOL_API InputActionConfig {
    std::string               name;
    std::vector<InputBinding> bindings;
};

// ---- Project configuration ----

// Loaded from a project.sol JSON file.
// Contains everything needed to boot the engine for this project.
struct SOL_API ProjectConfig {
    std::string name         = "Unnamed";
    std::string version      = "0.1.0";
    std::string main_scene;           // path to starting scene (e.g. "scenes/main.solscene")

    // Window settings
    std::string window_title  = "Sol Game";
    int         window_width  = 1280;
    int         window_height = 720;
    bool        window_vsync  = true;

    // Input actions (loaded from "input_actions" in project.sol)
    std::vector<InputActionConfig> input_actions;

    // Load a project.sol file. Returns defaults if the file is missing.
    static ProjectConfig load(const std::string& path);
};

} // namespace sol
