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

    return cfg;
}

} // namespace sol
