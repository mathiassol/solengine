// Voxel demo — minimal MC-style chunk skeleton.
// Demonstrates ECS components, math, and the engine ABI.
// (Mesh upload to bgfx is a TODO; chunk generation runs and is queryable.)
#include "sol/api.h"
#include "sol/engine.h"
#include "sol/log.h"

#include <glm/gtc/noise.hpp>
#include <imgui.h>
#include <vector>

namespace {

constexpr int CHUNK = 16;

enum class Block : uint8_t { Air = 0, Stone, Dirt, Grass };

struct Chunk {
    glm::ivec3 origin;
    Block      data[CHUNK][CHUNK][CHUNK]{};
};

struct VoxelWorld {
    std::vector<Chunk> chunks;
};

static void generate(Chunk& c) {
    for (int x = 0; x < CHUNK; ++x)
    for (int z = 0; z < CHUNK; ++z) {
        const float wx = (float)(c.origin.x + x);
        const float wz = (float)(c.origin.z + z);
        const float n  = glm::perlin(glm::vec2(wx * 0.05f, wz * 0.05f));
        const int   h  = 6 + int(n * 4.0f);
        for (int y = 0; y < CHUNK; ++y) {
            const int wy = c.origin.y + y;
            if      (wy >  h) c.data[x][y][z] = Block::Air;
            else if (wy == h) c.data[x][y][z] = Block::Grass;
            else if (wy >  h - 3) c.data[x][y][z] = Block::Dirt;
            else              c.data[x][y][z] = Block::Stone;
        }
    }
}

struct State {
    VoxelWorld world;
    int        frames = 0;
};

static State* g_state = nullptr;

void on_init(sol::Engine*) {
    g_state = new State();
    for (int cx = -2; cx <= 2; ++cx)
    for (int cz = -2; cz <= 2; ++cz) {
        Chunk c; c.origin = {cx * CHUNK, 0, cz * CHUNK};
        generate(c);
        g_state->world.chunks.push_back(std::move(c));
    }
    SOL_INFO("Voxel demo: generated 5x5 chunks");
}

void on_update(sol::Engine*, float) { ++g_state->frames; }

void on_render(sol::Engine*) {
    ImGui::Begin("Voxel demo");
    ImGui::Text("Chunks: %zu", g_state->world.chunks.size());
    ImGui::Text("Frame:  %d",  g_state->frames);
    ImGui::TextWrapped("Minecraft-style voxel skeleton. Chunk gen runs; "
                       "greedy meshing + bgfx upload TBD.");
    ImGui::End();
}

void on_shutdown(sol::Engine*) { delete g_state; g_state = nullptr; }

constexpr SolGameApi kApi = {
    SOL_ABI_VERSION, "Voxel Demo",
    on_init, on_update, on_render, on_shutdown,
};

} // namespace

SOL_EXPORT const SolGameApi* sol_get_game_api() { return &kApi; }
