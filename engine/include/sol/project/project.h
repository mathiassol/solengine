#pragma once
#include "sol/export.h"
#include <string>

namespace sol {

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

    // Load a project.sol file. Returns defaults if the file is missing.
    static ProjectConfig load(const std::string& path);
};

} // namespace sol
