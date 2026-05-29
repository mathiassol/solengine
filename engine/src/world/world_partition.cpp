#include "sol/world/world_partition.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace sol {

// ---------------------------------------------------------------------------
// WorldPartitionCell
// ---------------------------------------------------------------------------

bool WorldPartitionCell::contains(const glm::vec3& p) const {
    return std::abs(p.x - center.x) <= half_extent.x &&
           std::abs(p.y - center.y) <= half_extent.y &&
           std::abs(p.z - center.z) <= half_extent.z;
}

float WorldPartitionCell::sq_distance_to(const glm::vec3& p) const {
    float dx = std::max(0.0f, std::abs(p.x - center.x) - half_extent.x);
    float dy = std::max(0.0f, std::abs(p.y - center.y) - half_extent.y);
    float dz = std::max(0.0f, std::abs(p.z - center.z) - half_extent.z);
    return dx * dx + dy * dy + dz * dz;
}

// ---------------------------------------------------------------------------
// WorldPartition
// ---------------------------------------------------------------------------

void WorldPartition::add_cell(WorldPartitionCell cell) {
    for (auto& c : m_cells) {
        if (c.name == cell.name) {
            c = std::move(cell);
            return;
        }
    }
    m_cells.push_back(std::move(cell));
}

void WorldPartition::remove_cell(const std::string& name) {
    m_cells.erase(
        std::remove_if(m_cells.begin(), m_cells.end(),
            [&](const WorldPartitionCell& c) { return c.name == name; }),
        m_cells.end());
}

WorldPartitionCell* WorldPartition::find_cell(const std::string& name) {
    for (auto& c : m_cells)
        if (c.name == name) return &c;
    return nullptr;
}

std::string WorldPartition::to_json() const {
    nlohmann::json j;
    j["stream_in_radius"]  = stream_in_radius;
    j["stream_out_radius"] = stream_out_radius;
    j["cells"]             = nlohmann::json::array();
    for (const auto& c : m_cells) {
        nlohmann::json jc;
        jc["name"]         = c.name;
        jc["center"]       = { c.center.x,      c.center.y,      c.center.z };
        jc["half_extent"]  = { c.half_extent.x, c.half_extent.y, c.half_extent.z };
        jc["scene_path"]   = c.scene_path;
        jc["always_loaded"]= c.always_loaded;
        j["cells"].push_back(std::move(jc));
    }
    return j.dump(2);
}

bool WorldPartition::from_json(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);
        stream_in_radius  = j.value("stream_in_radius",  2000.f);
        stream_out_radius = j.value("stream_out_radius", 2500.f);
        m_cells.clear();
        if (j.contains("cells") && j["cells"].is_array()) {
            for (const auto& jc : j["cells"]) {
                WorldPartitionCell c;
                c.name         = jc.value("name",         "");
                c.scene_path   = jc.value("scene_path",   "");
                c.always_loaded= jc.value("always_loaded", false);
                if (jc.contains("center") && jc["center"].is_array() &&
                    jc["center"].size() >= 3) {
                    c.center = { jc["center"][0].get<float>(),
                                 jc["center"][1].get<float>(),
                                 jc["center"][2].get<float>() };
                }
                if (jc.contains("half_extent") && jc["half_extent"].is_array() &&
                    jc["half_extent"].size() >= 3) {
                    c.half_extent = { jc["half_extent"][0].get<float>(),
                                      jc["half_extent"][1].get<float>(),
                                      jc["half_extent"][2].get<float>() };
                }
                m_cells.push_back(std::move(c));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace sol
