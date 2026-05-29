#pragma once
#include "sol/export.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace sol {

// ---------------------------------------------------------------------------
// WorldPartitionCell — a named, axis-aligned region of the world.
// Stage 3: data structures + serialisation only. Actual streaming (load/unload
// based on camera proximity) is deferred to Stage 4.
// ---------------------------------------------------------------------------
struct SOL_API WorldPartitionCell {
    std::string  name;           // human-readable, e.g. "cell_0_0"
    glm::vec3    center{0,0,0};
    glm::vec3    half_extent{500,500,500};
    std::string  scene_path;     // path to .solscene that populates this cell
    bool         always_loaded = false;  // ignore proximity, always streamed in

    // Returns true if point is inside this cell's AABB.
    bool contains(const glm::vec3& p) const;
    // Returns squared distance from point to nearest point on the AABB.
    float sq_distance_to(const glm::vec3& p) const;
};

// ---------------------------------------------------------------------------
// WorldPartition — collection of cells + streaming config.
// Serialised to/from JSON alongside the root scene.
// ---------------------------------------------------------------------------
class SOL_API WorldPartition {
public:
    float stream_in_radius  = 2000.f;  // cells whose sq_distance < this^2 load
    float stream_out_radius = 2500.f;  // cells further than this unload (hysteresis)

    void add_cell   (WorldPartitionCell cell);
    void remove_cell(const std::string& name);
    WorldPartitionCell* find_cell(const std::string& name);

    const std::vector<WorldPartitionCell>& cells() const { return m_cells; }

    // Serialise / deserialise to JSON string (nlohmann::json under the hood).
    std::string to_json()             const;
    bool        from_json(const std::string& json_str);

private:
    std::vector<WorldPartitionCell> m_cells;
};

} // namespace sol
