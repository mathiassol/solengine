#include "sol/scene/scene.h"
#include "sol/scene/node.h"
#include "sol/scene/node3d.h"
#include "sol/scene/mesh_node.h"
#include "sol/scene/camera3d.h"
#include "sol/scene/light_node.h"
#include "sol/scene/collision_shape3d.h"
#include "sol/scene/static_body3d.h"
#include "sol/scene/character_body3d.h"
#include "sol/scene/scene_instance.h"

#include "sol/engine.h"
#include "sol/log.h"
#include "sol/render/renderer.h"
#include "sol/render/mesh.h"
#include "sol/render/material.h"
#include "sol/render/light.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <stdexcept>

using json = nlohmann::json;

namespace sol {

// ============================================================
//  Node base
// ============================================================

void Node::add_child(std::unique_ptr<Node> child) {
    child->m_parent = this;
    m_children.push_back(std::move(child));
}

Node* Node::find(const std::string& n) const {
    for (auto& c : m_children) {
        if (c->name == n) return c.get();
        if (auto* r = c->find(n)) return r;
    }
    return nullptr;
}

// ============================================================
//  MeshNode
// ============================================================

void MeshNode::on_ready(Engine& /*engine*/) {
    Mesh raw_mesh;
    if      (mesh_name == "cube")   raw_mesh = primitives::make_cube();
    else if (mesh_name == "sphere") raw_mesh = primitives::make_sphere();
    else if (mesh_name == "plane")  raw_mesh = primitives::make_plane();
    else if (mesh_name == "capsule")
        raw_mesh = primitives::make_sphere(0.35f, 16, 12);
    else {
        SOL_WARN("MeshNode '" + name + "': unknown mesh '" + mesh_name + "', using cube");
        raw_mesh = primitives::make_cube();
    }
    m_mesh = std::make_shared<Mesh>(std::move(raw_mesh));
}

void MeshNode::on_render(Engine& engine, const glm::mat4& world_xform) {
    if (m_mesh && m_mesh->valid())
        engine.renderer().submit(*m_mesh, material, world_xform);
}

// ============================================================
//  Camera3D
// ============================================================

Camera Camera3D::to_camera(float aspect) const {
    glm::mat4 world = global_transform();
    glm::vec3 eye   = glm::vec3(world[3]);
    glm::vec3 fwd   = -glm::vec3(world[2]); // -Z
    glm::vec3 up_v  =  glm::vec3(world[1]); //  Y

    Camera cam;
    cam.position      = eye;
    cam.target        = eye + fwd;
    cam.up            = up_v;
    cam.fov_y_radians = glm::radians(fov);
    cam.near_plane    = near_clip;
    cam.far_plane     = far_clip;
    return cam;
}

// ============================================================
//  SceneInstance
// ============================================================

void SceneInstance::on_ready(Engine& engine) {
    if (scene_path.empty()) return;
    auto sub = Scene::load(scene_path, engine);
    if (!sub || !sub->root()) {
        SOL_WARN("SceneInstance '" + name + "': failed to load '" + scene_path + "'");
        return;
    }
    add_child(sub->detach_root());
    // Children's on_ready will be called by the parent scene's traversal.
}

// ============================================================
//  Helpers: JSON <-> glm
// ============================================================

static glm::vec3 vec3_from_json(const json& j, glm::vec3 def = {0,0,0}) {
    if (!j.is_array() || j.size() < 3) return def;
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}

static glm::vec4 vec4_from_json(const json& j, glm::vec4 def = {1,1,1,1}) {
    if (!j.is_array() || j.size() < 4) return def;
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
}

static json vec3_to_json(const glm::vec3& v) { return { v.x, v.y, v.z }; }
static json vec4_to_json(const glm::vec4& v) { return { v.x, v.y, v.z, v.w }; }

// ============================================================
//  Deserialise a Node from JSON
// ============================================================

static std::unique_ptr<Node> node_from_json(const json& j) {
    std::string type = j.value("type", "Node3D");

    std::unique_ptr<Node> node;

    auto read_node3d = [&](Node3D* n) {
        if (j.contains("position")) n->position = vec3_from_json(j["position"]);
        if (j.contains("rotation")) n->rotation = vec3_from_json(j["rotation"]);
        if (j.contains("scale"))    n->scale    = vec3_from_json(j["scale"], {1,1,1});
    };

    if (type == "MeshNode") {
        auto n = std::make_unique<MeshNode>();
        read_node3d(n.get());
        n->mesh_name = j.value("mesh", "cube");
        if (j.contains("material")) {
            const auto& m = j["material"];
            if (m.contains("base_color"))
                n->material.base_color = vec4_from_json(m["base_color"]);
            n->material.lit          = m.value("lit", true);
            n->material.double_sided = m.value("double_sided", false);
        }
        node = std::move(n);
    } else if (type == "Camera3D") {
        auto n = std::make_unique<Camera3D>();
        read_node3d(n.get());
        n->fov       = j.value("fov",      70.0f);
        n->near_clip = j.value("near",     0.05f);
        n->far_clip  = j.value("far",   1000.0f);
        n->current   = j.value("current",  false);
        node = std::move(n);
    } else if (type == "DirectionalLight") {
        auto n = std::make_unique<DirectionalLight>();
        read_node3d(n.get());
        if (j.contains("color")) n->color = vec3_from_json(j["color"], {1,1,1});
        n->intensity = j.value("intensity", 1.0f);
        node = std::move(n);
    } else if (type == "PointLight") {
        auto n = std::make_unique<PointLight>();
        read_node3d(n.get());
        if (j.contains("color")) n->color = vec3_from_json(j["color"], {1,1,1});
        n->intensity = j.value("intensity", 1.0f);
        n->range     = j.value("range", 10.0f);
        node = std::move(n);
    } else if (type == "CollisionShape3D") {
        auto n = std::make_unique<CollisionShape3D>();
        read_node3d(n.get());
        std::string sh = j.value("shape", "box");
        if      (sh == "sphere")  n->shape = CollisionShape3D::Shape::Sphere;
        else if (sh == "capsule") n->shape = CollisionShape3D::Shape::Capsule;
        else                      n->shape = CollisionShape3D::Shape::Box;
        if (j.contains("extents")) n->extents = vec3_from_json(j["extents"], {0.5f,0.5f,0.5f});
        n->radius = j.value("radius", 0.5f);
        n->height = j.value("height", 1.0f);
        node = std::move(n);
    } else if (type == "StaticBody3D") {
        auto n = std::make_unique<StaticBody3D>();
        read_node3d(n.get());
        node = std::move(n);
    } else if (type == "CharacterBody3D") {
        auto n = std::make_unique<CharacterBody3D>();
        read_node3d(n.get());
        n->capsule_radius = j.value("capsule_radius", 0.35f);
        n->capsule_height = j.value("capsule_height", 1.5f);
        node = std::move(n);
    } else if (type == "SceneInstance") {
        auto n = std::make_unique<SceneInstance>();
        read_node3d(n.get());
        n->scene_path = j.value("scene", "");
        node = std::move(n);
    } else {
        // Generic Node3D (or unknown type — treat as Node3D)
        auto n = std::make_unique<Node3D>();
        read_node3d(n.get());
        node = std::move(n);
    }

    node->name = j.value("name", "Node");

    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& cj : j["children"])
            node->add_child(node_from_json(cj));
    }

    return node;
}

// ============================================================
//  Serialise a Node to JSON
// ============================================================

static json node_to_json(const Node* node) {
    json j;
    j["type"] = node->type_name();
    j["name"] = node->name;

    if (const auto* n3d = dynamic_cast<const Node3D*>(node)) {
        j["position"] = vec3_to_json(n3d->position);
        j["rotation"] = vec3_to_json(n3d->rotation);
        j["scale"]    = vec3_to_json(n3d->scale);
    }

    if (const auto* n = dynamic_cast<const MeshNode*>(node)) {
        j["mesh"] = n->mesh_name;
        json m;
        m["base_color"]   = vec4_to_json(n->material.base_color);
        m["lit"]          = n->material.lit;
        m["double_sided"] = n->material.double_sided;
        j["material"] = m;
    } else if (const auto* n = dynamic_cast<const Camera3D*>(node)) {
        j["fov"]     = n->fov;
        j["near"]    = n->near_clip;
        j["far"]     = n->far_clip;
        j["current"] = n->current;
    } else if (const auto* n = dynamic_cast<const DirectionalLight*>(node)) {
        j["color"]     = vec3_to_json(n->color);
        j["intensity"] = n->intensity;
    } else if (const auto* n = dynamic_cast<const PointLight*>(node)) {
        j["color"]     = vec3_to_json(n->color);
        j["intensity"] = n->intensity;
        j["range"]     = n->range;
    } else if (const auto* n = dynamic_cast<const CollisionShape3D*>(node)) {
        switch (n->shape) {
        case CollisionShape3D::Shape::Sphere:  j["shape"] = "sphere";  break;
        case CollisionShape3D::Shape::Capsule: j["shape"] = "capsule"; break;
        default:                               j["shape"] = "box";     break;
        }
        j["extents"] = vec3_to_json(n->extents);
        j["radius"]  = n->radius;
        j["height"]  = n->height;
    } else if (const auto* n = dynamic_cast<const CharacterBody3D*>(node)) {
        j["capsule_radius"] = n->capsule_radius;
        j["capsule_height"] = n->capsule_height;
    } else if (const auto* n = dynamic_cast<const SceneInstance*>(node)) {
        j["scene"] = n->scene_path;
    }

    if (!node->children().empty()) {
        json arr = json::array();
        for (const auto& c : node->children())
            arr.push_back(node_to_json(c.get()));
        j["children"] = arr;
    }

    return j;
}

// ============================================================
//  Scene — traversal
// ============================================================

void Scene::ready_node(Engine& engine, Node* node) {
    node->on_ready(engine);
    // Use index-based loop: on_ready may add children (SceneInstance).
    for (size_t i = 0; i < node->children().size(); ++i)
        ready_node(engine, node->children()[i].get());
}

void Scene::destroy_node(Engine& engine, Node* node) {
    for (auto& c : node->children())
        destroy_node(engine, c.get());
    node->on_destroy(engine);
}

void Scene::update_node(Engine& engine, Node* node, float dt) {
    node->on_update(engine, dt);
    for (auto& c : node->children())
        update_node(engine, c.get(), dt);
}

void Scene::render_node(Engine& engine, Node* node, const glm::mat4& parent_xform) {
    glm::mat4 world = parent_xform;
    if (auto* n3d = dynamic_cast<Node3D*>(node))
        world = parent_xform * n3d->local_transform();

    node->on_render(engine, world);

    for (auto& c : node->children())
        render_node(engine, c.get(), world);
}

Camera3D* Scene::active_camera() const {
    if (!m_root) return nullptr;
    return const_cast<Scene*>(this)->m_root->find_first<Camera3D>();
}

DirectionalLight* Scene::active_light() const {
    if (!m_root) return nullptr;
    return const_cast<Scene*>(this)->m_root->find_first<DirectionalLight>();
}

void Scene::on_ready(Engine& engine) {
    if (m_root) ready_node(engine, m_root.get());
}

void Scene::on_destroy(Engine& engine) {
    if (m_root) destroy_node(engine, m_root.get());
}

void Scene::update(Engine& engine, float dt) {
    if (m_root) update_node(engine, m_root.get(), dt);
}

void Scene::render(Engine& engine) {
    if (!m_root) return;

    // Set camera
    if (Camera3D* cam = active_camera()) {
        float aspect = engine.renderer().height() > 0
            ? float(engine.renderer().width()) / float(engine.renderer().height())
            : 1.0f;
        engine.renderer().set_camera(cam->to_camera(aspect));
    }

    // Submit all lights found in the scene
    engine.renderer().set_ambient({0.15f, 0.15f, 0.20f});
    std::function<void(Node*)> collect_lights = [&](Node* node) {
        if (auto* dl = dynamic_cast<DirectionalLight*>(node)) {
            Light l;
            l.type = LightType::Directional;
            l.direction = dl->world_direction(); // normalized, toward-light direction
            l.color = dl->color;
            l.intensity = dl->intensity;
            l.cast_shadow = dl->cast_shadow;
            engine.renderer().submit_light(l);
        } else if (auto* pl = dynamic_cast<PointLight*>(node)) {
            Light l;
            l.type = LightType::Point;
            glm::mat4 wt = pl->global_transform();
            l.position = glm::vec3(wt[3]);
            l.color = pl->color;
            l.intensity = pl->intensity;
            l.range = pl->range;
            engine.renderer().submit_light(l);
        }
        for (auto& c : node->children())
            collect_lights(c.get());
    };
    collect_lights(m_root.get());

    render_node(engine, m_root.get(), glm::mat4(1.0f));
}

// ============================================================
//  Scene — serialisation
// ============================================================

std::unique_ptr<Scene> Scene::load(const std::string& file_path, Engine& /*engine*/) {
    std::ifstream f(file_path);
    if (!f.is_open()) {
        SOL_WARN("Scene::load: cannot open '" + file_path + "'");
        return nullptr;
    }

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        SOL_ERROR(std::string("Scene::load JSON parse error: ") + e.what());
        return nullptr;
    }

    auto scene    = std::make_unique<Scene>();
    scene->name   = j.value("name", "Scene");
    scene->path   = file_path;

    if (j.contains("root"))
        scene->set_root(node_from_json(j["root"]));

    return scene;
}

bool Scene::save(const std::string& file_path) const {
    json j;
    j["sol_scene"] = 1;
    j["name"]      = name;
    if (m_root) j["root"] = node_to_json(m_root.get());

    std::ofstream f(file_path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}

} // namespace sol
