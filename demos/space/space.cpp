// Space demo — basic Newtonian ship + a few orbiting bodies via ECS.
#include "sol/api.h"
#include "sol/engine.h"
#include "sol/log.h"

#include <imgui.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>

namespace {

struct Transform { glm::vec3 pos{0}; glm::quat rot{1,0,0,0}; };
struct RigidBody { glm::vec3 vel{0}; float mass = 1.0f; };
struct Ship      { float thrust = 25.0f; };
struct Planet    { float gm = 50.0f; }; // gravitational parameter

struct State { entt::entity ship = entt::null; int frames = 0; };
static State* g_state = nullptr;

void on_init(sol::Engine* e) {
    g_state = new State();
    auto& reg = e->registry();

    g_state->ship = reg.create();
    reg.emplace<Transform>(g_state->ship, Transform{{0, 0, 20}});
    reg.emplace<RigidBody>(g_state->ship, RigidBody{{2, 0, 0}, 1.0f});
    reg.emplace<Ship>     (g_state->ship);

    auto sun = reg.create();
    reg.emplace<Transform>(sun);
    reg.emplace<Planet>   (sun, Planet{200.0f});

    auto planet = reg.create();
    reg.emplace<Transform>(planet, Transform{{30, 0, 0}});
    reg.emplace<RigidBody>(planet, RigidBody{{0, 0, 2.5f}, 5.0f});
    reg.emplace<Planet>   (planet, Planet{30.0f});

    SOL_INFO("Space demo: scene built");
}

void on_update(sol::Engine* e, float dt) {
    ++g_state->frames;
    auto& reg = e->registry();

    // n-body-ish: every Planet pulls every RigidBody.
    auto attractors = reg.view<Transform, Planet>();
    for (auto [_, t, rb] : reg.view<Transform, RigidBody>().each()) {
        glm::vec3 a{0};
        for (auto src : attractors) {
            const auto& st  = reg.get<Transform>(src);
            const auto& sp  = reg.get<Planet>(src);
            glm::vec3 d = st.pos - t.pos;
            float r2 = glm::dot(d, d);
            if (r2 < 0.5f) continue;
            a += (sp.gm / (r2 * std::sqrt(r2))) * d;
        }
        rb.vel += a * dt;
        t.pos  += rb.vel * dt;
    }
}

void on_render(sol::Engine* e) {
    auto& reg = e->registry();
    auto& t   = reg.get<Transform>(g_state->ship);
    auto& rb  = reg.get<RigidBody>(g_state->ship);

    ImGui::Begin("Space demo");
    ImGui::Text("Frame: %d", g_state->frames);
    ImGui::Text("Ship pos: %.2f %.2f %.2f", t.pos.x, t.pos.y, t.pos.z);
    ImGui::Text("Ship vel: %.2f %.2f %.2f", rb.vel.x, rb.vel.y, rb.vel.z);
    ImGui::TextWrapped("Newtonian gravity sim across an EnTT registry. "
                       "Ship rendering + input wiring TBD.");
    ImGui::End();
}

void on_shutdown(sol::Engine*) { delete g_state; g_state = nullptr; }

constexpr SolGameApi kApi = {
    SOL_ABI_VERSION, "Space Demo",
    on_init, on_update, on_render, on_shutdown,
};

} // namespace

SOL_EXPORT const SolGameApi* sol_get_game_api() { return &kApi; }
