#include "sol/scene/scene.h"
#include "sol/scene/node.h"
#include "sol/scene/component.h"
#include "sol/scene/node3d.h"
#include "sol/scene/node_factory.h"
#include "sol/scene/mesh_node.h"
#include "sol/scene/camera3d.h"
#include "sol/scene/light_node.h"
#include "sol/scene/collision_shape3d.h"
#include "sol/scene/static_body3d.h"
#include "sol/scene/character_body3d.h"
#include "sol/scene/area3d.h"
#include "sol/scene/rigid_body3d.h"
#include "sol/scene/scene_instance.h"
#include "sol/scene/model_node.h"
#include "sol/scene/fly_cam_controller.h"
#include "sol/scene/world_environment.h"
#include "sol/scene/script_node.h"
#include "sol/scene/lua_component.h"
#include "sol/scene/audio_stream_player.h"
#include "sol/scene/audio_stream_player3d.h"
#include "sol/script/script_engine.h"
#include "asset/model_loader.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include "sol/engine.h"
#include "sol/log.h"
#include "physics/physics.h"
#include "sol/render/renderer.h"
#include "sol/render/mesh.h"
#include "sol/render/material.h"
#include "sol/render/light.h"
#include "sol/reflect.h"

using AlphaMode = sol::AlphaMode;

#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Stage 1: tick-phase helper
// ---------------------------------------------------------------------------
static bool is_pre_phase(sol::ETickGroup g) {
    return g == sol::ETickGroup::PrePhysics || g == sol::ETickGroup::DuringPhysics;
}

// ---------------------------------------------------------------------------
// Reflection registrations for built-in node types
// ---------------------------------------------------------------------------

SOL_REFLECT_BEGIN(Node3D)
    SOL_FIELD(position, sol::FieldType::Vec3)
    SOL_FIELD(rotation, sol::FieldType::Vec3)
    SOL_FIELD(scale,    sol::FieldType::Vec3)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(ScriptNode)
    SOL_FIELD(position,    sol::FieldType::Vec3)
    SOL_FIELD(rotation,    sol::FieldType::Vec3)
    SOL_FIELD(scale,       sol::FieldType::Vec3)
    SOL_FIELD(script_path, sol::FieldType::AssetPath)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(Camera3D)
    SOL_FIELD(position,  sol::FieldType::Vec3)
    SOL_FIELD(rotation,  sol::FieldType::Vec3)
    SOL_FIELD(scale,     sol::FieldType::Vec3)
    SOL_FIELD(fov,       sol::FieldType::Float)
    SOL_FIELD(near_clip, sol::FieldType::Float)
    SOL_FIELD(far_clip,  sol::FieldType::Float)
    SOL_FIELD(current,   sol::FieldType::Bool)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(DirectionalLight)
    SOL_FIELD(position,     sol::FieldType::Vec3)
    SOL_FIELD(rotation,     sol::FieldType::Vec3)
    SOL_FIELD(color,        sol::FieldType::Color3)
    SOL_FIELD(intensity,    sol::FieldType::Float)
    SOL_FIELD(cast_shadow,  sol::FieldType::Bool)
    SOL_FIELD(shadow_mode,  sol::FieldType::Int)
    SOL_FIELD(csm_far,              sol::FieldType::Float)
    SOL_FIELD(csm_lambda,           sol::FieldType::Float)
    SOL_FIELD_ENUM(shadow_quality,  "Low", "Medium", "High")
    SOL_FIELD(shadow_pcf_radius,    sol::FieldType::Float)
    SOL_FIELD(shadow_pcss_light,    sol::FieldType::Float)
    SOL_FIELD(contact_shadow_dist,  sol::FieldType::Float)
    SOL_FIELD(contact_shadow_thick, sol::FieldType::Float)
    SOL_FIELD(temporal_shadow,      sol::FieldType::Bool)
    SOL_FIELD(temporal_shadow_alpha,sol::FieldType::Float)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(PointLight)
    SOL_FIELD(position,  sol::FieldType::Vec3)
    SOL_FIELD(color,     sol::FieldType::Color3)
    SOL_FIELD(intensity, sol::FieldType::Float)
    SOL_FIELD(range,     sol::FieldType::Float)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(ModelNode)
    SOL_FIELD(position, sol::FieldType::Vec3)
    SOL_FIELD(rotation, sol::FieldType::Vec3)
    SOL_FIELD(scale,    sol::FieldType::Vec3)
    SOL_FIELD(path,     sol::FieldType::AssetPath)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(WorldEnvironment)
    SOL_SECTION("Sky")
    SOL_FIELD_ENUM(sky_mode,     "Procedural", "HDR Panorama")
    SOL_FIELD(hdr_path,          sol::FieldType::AssetPath)
    SOL_FIELD(zenith_color,      sol::FieldType::Color3)
    SOL_FIELD(horizon_color,     sol::FieldType::Color3)
    SOL_FIELD(sun_color,         sol::FieldType::Color3)
    SOL_FIELD(sun_disk_size,     sol::FieldType::Float)
    SOL_FIELD(follow_sun,        sol::FieldType::Bool)
    SOL_SECTION("Ambient")
    SOL_FIELD(ambient_color,     sol::FieldType::Color3)
    SOL_FIELD(ambient_intensity, sol::FieldType::Float)
    SOL_SECTION("Tonemapping")
    SOL_FIELD_ENUM(tonemap_mode, "ACES", "Reinhard", "Linear", "AgX")
    SOL_FIELD(exposure,          sol::FieldType::Float)
    SOL_SECTION("Bloom")
    SOL_FIELD(bloom_enabled,     sol::FieldType::Bool)
    SOL_FIELD(bloom_threshold,   sol::FieldType::Float)
    SOL_FIELD(bloom_intensity,   sol::FieldType::Float)
    SOL_SECTION("IBL")
    SOL_FIELD(ibl_enabled,       sol::FieldType::Bool)
    SOL_FIELD(ibl_intensity,     sol::FieldType::Float)
    SOL_FIELD(ibl_diffuse_scale,  sol::FieldType::Float)
    SOL_FIELD(ibl_specular_scale, sol::FieldType::Float)
    SOL_SECTION("SSAO")
    SOL_FIELD(ssao_enabled,      sol::FieldType::Bool)
    SOL_FIELD(ssao_radius,       sol::FieldType::Float)
    SOL_FIELD(ssao_bias,         sol::FieldType::Float)
    SOL_FIELD(ssao_power,        sol::FieldType::Float)
    SOL_FIELD(ssao_strength,     sol::FieldType::Float)
    SOL_SECTION("SSR")
    SOL_FIELD(ssr_enabled,          sol::FieldType::Bool)
    SOL_FIELD(ssr_steps,            sol::FieldType::Int)
    SOL_FIELD(ssr_thickness,        sol::FieldType::Float)
    SOL_FIELD(ssr_max_distance,     sol::FieldType::Float)
    SOL_FIELD(ssr_roughness_cutoff, sol::FieldType::Float)
    SOL_FIELD(ssr_intensity,        sol::FieldType::Float)
    SOL_SECTION("Volumetrics")
    SOL_FIELD(vol_enabled,    sol::FieldType::Bool)
    SOL_FIELD(vol_density,    sol::FieldType::Float)
    SOL_FIELD(vol_scattering, sol::FieldType::Float)
    SOL_FIELD(vol_g,          sol::FieldType::Float)
    SOL_FIELD(vol_march_steps, sol::FieldType::Int)
    SOL_SECTION("Anti-Aliasing")
    SOL_FIELD_ENUM(aa_mode,  "None", "MSAA 2x", "MSAA 4x", "MSAA 8x", "TAA")
    SOL_FIELD(taa_blend,      sol::FieldType::Float)
    SOL_FIELD(taa_sharpening, sol::FieldType::Float)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(MeshNode)
    SOL_FIELD(position,          sol::FieldType::Vec3)
    SOL_FIELD(rotation,          sol::FieldType::Vec3)
    SOL_FIELD(scale,             sol::FieldType::Vec3)
    SOL_FIELD(mesh_name,         sol::FieldType::String)
    SOL_FIELD(mesh_path,         sol::FieldType::AssetPath)
    SOL_FIELD(collision_enabled, sol::FieldType::Bool)
    SOL_SECTION("Material")
    _desc.fields.push_back({"base_color", sol::FieldType::Color4,
        [](void* n) -> void* { return &static_cast<_T*>(n)->material.base_color; }, {}});
    _desc.fields.push_back({"metallic", sol::FieldType::Float,
        [](void* n) -> void* { return &static_cast<_T*>(n)->material.metallic; }, {}});
    _desc.fields.push_back({"roughness", sol::FieldType::Float,
        [](void* n) -> void* { return &static_cast<_T*>(n)->material.roughness; }, {}});
    _desc.fields.push_back({"emissive", sol::FieldType::Color3,
        [](void* n) -> void* { return &static_cast<_T*>(n)->material.emissive; }, {}});
    _desc.fields.push_back({"alpha_cutoff", sol::FieldType::Float,
        [](void* n) -> void* { return &static_cast<_T*>(n)->material.alpha_cutoff; }, {}});
    _desc.fields.push_back({"lit", sol::FieldType::Bool,
        [](void* n) -> void* { return &static_cast<_T*>(n)->material.lit; }, {}});
    _desc.fields.push_back({"double_sided", sol::FieldType::Bool,
        [](void* n) -> void* { return &static_cast<_T*>(n)->material.double_sided; }, {}});
    SOL_SECTION("Textures")
    SOL_FIELD(mat_albedo_path,  sol::FieldType::AssetPath)
    SOL_FIELD(mat_normal_path,  sol::FieldType::AssetPath)
    SOL_FIELD(mat_mr_path,      sol::FieldType::AssetPath)
    SOL_FIELD(mat_emissive_path,sol::FieldType::AssetPath)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(AudioStreamPlayer)
    SOL_SECTION("Audio")
    SOL_FIELD(clip_path,  sol::FieldType::AssetPath)
    SOL_FIELD(volume,     sol::FieldType::Float)
    SOL_FIELD(pitch,      sol::FieldType::Float)
    SOL_FIELD(loop,       sol::FieldType::Bool)
    SOL_FIELD(autoplay,   sol::FieldType::Bool)
    SOL_FIELD(bus,        sol::FieldType::String)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(AudioStreamPlayer3D)
    SOL_SECTION("Transform")
    SOL_FIELD(position,      sol::FieldType::Vec3)
    SOL_FIELD(rotation,      sol::FieldType::Vec3)
    SOL_FIELD(scale,         sol::FieldType::Vec3)
    SOL_SECTION("Audio")
    SOL_FIELD(clip_path,     sol::FieldType::AssetPath)
    SOL_FIELD(volume,        sol::FieldType::Float)
    SOL_FIELD(pitch,         sol::FieldType::Float)
    SOL_FIELD(loop,          sol::FieldType::Bool)
    SOL_FIELD(autoplay,      sol::FieldType::Bool)
    SOL_FIELD(bus,           sol::FieldType::String)
    SOL_SECTION("Spatial")
    SOL_FIELD(max_distance,  sol::FieldType::Float)
    SOL_FIELD(attenuation,   sol::FieldType::Float)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(Area3D)
    SOL_SECTION("Transform")
    SOL_FIELD(position, sol::FieldType::Vec3)
    SOL_FIELD(rotation, sol::FieldType::Vec3)
    SOL_FIELD(scale,    sol::FieldType::Vec3)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(RigidBody3D)
    SOL_SECTION("Transform")
    SOL_FIELD(position,      sol::FieldType::Vec3)
    SOL_FIELD(rotation,      sol::FieldType::Vec3)
    SOL_FIELD(scale,         sol::FieldType::Vec3)
    SOL_SECTION("Physics")
    SOL_FIELD(mass,          sol::FieldType::Float)
    SOL_FIELD(gravity_scale, sol::FieldType::Float)
    SOL_FIELD(is_kinematic,  sol::FieldType::Bool)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(StaticBody3D)
    SOL_SECTION("Transform")
    SOL_FIELD(position, sol::FieldType::Vec3)
    SOL_FIELD(rotation, sol::FieldType::Vec3)
    SOL_FIELD(scale,    sol::FieldType::Vec3)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(CharacterBody3D)
    SOL_SECTION("Transform")
    SOL_FIELD(position,       sol::FieldType::Vec3)
    SOL_FIELD(rotation,       sol::FieldType::Vec3)
    SOL_FIELD(scale,          sol::FieldType::Vec3)
    SOL_SECTION("Capsule")
    SOL_FIELD(capsule_radius, sol::FieldType::Float)
    SOL_FIELD(capsule_height, sol::FieldType::Float)
SOL_REFLECT_END()

// NOTE: CollisionShape3D::Shape is enum class with int underlying type.
// SOL_FIELD_ENUM writes to it via int* which is safe on all platforms.
SOL_REFLECT_BEGIN(CollisionShape3D)
    SOL_SECTION("Transform")
    SOL_FIELD(position, sol::FieldType::Vec3)
    SOL_FIELD(rotation, sol::FieldType::Vec3)
    SOL_FIELD(scale,    sol::FieldType::Vec3)
    SOL_SECTION("Shape")
    SOL_FIELD_ENUM(shape, "Box", "Sphere", "Capsule")
    SOL_FIELD(extents,  sol::FieldType::Vec3)
    SOL_FIELD(radius,   sol::FieldType::Float)
    SOL_FIELD(height,   sol::FieldType::Float)
SOL_REFLECT_END()

SOL_REFLECT_BEGIN(SceneInstance)
    SOL_SECTION("Transform")
    SOL_FIELD(position,   sol::FieldType::Vec3)
    SOL_FIELD(rotation,   sol::FieldType::Vec3)
    SOL_FIELD(scale,      sol::FieldType::Vec3)
    SOL_SECTION("Scene")
    SOL_FIELD(scene_path, sol::FieldType::AssetPath)
SOL_REFLECT_END()

using json = nlohmann::json;

namespace sol {

static constexpr JPH::ObjectLayer LAYER_NON_MOVING = 0;

// ============================================================
//  Node base
// ============================================================

void Node::add_child(std::unique_ptr<Node> child) {
    child->m_parent = this;
    m_children.push_back(std::move(child));
}

bool Node::remove_child(Node* child) {
    auto it = std::find_if(m_children.begin(), m_children.end(),
        [child](const std::unique_ptr<Node>& node) {
            return node.get() == child;
        });
    if (it == m_children.end()) return false;
    (*it)->m_parent = nullptr;
    m_children.erase(it);
    return true;
}

Node* Node::find(const std::string& n) const {
    for (auto& c : m_children) {
        if (c->name == n) return c.get();
        if (auto* r = c->find(n)) return r;
    }
    return nullptr;
}

IComponent* Node::add_component(std::unique_ptr<IComponent> comp) {
    comp->owner = this;
    IComponent* raw = comp.get();
    m_components.push_back(std::move(comp));
    return raw;
}

bool Node::remove_component(const char* type_name) {
    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        if (std::string((*it)->component_type()) == type_name) {
            m_components.erase(it);
            return true;
        }
    }
    return false;
}

bool Node::remove_component(IComponent* comp) {
    auto it = std::find_if(m_components.begin(), m_components.end(),
        [comp](const std::unique_ptr<IComponent>& p) { return p.get() == comp; });
    if (it == m_components.end()) return false;
    m_components.erase(it);
    return true;
}

// ============================================================
//  MeshNode
// ============================================================

void MeshNode::on_ready(Engine& engine) {
    if (!m_body_ids.empty())
        on_destroy(engine);

    m_mesh.reset();
    m_submeshes.clear();
    m_model_ref.reset();

    if (!mesh_path.empty()) {
        auto model = engine.assets().load(mesh_path);
        if (!model) {
            SOL_ERROR("MeshNode '" + name + "': failed to load '" + mesh_path + "'");
        } else {
            m_model_ref = model;
            m_submeshes.reserve(model->meshes.size());

            for (const auto& gm : model->meshes) {
                if (gm.vertices.empty() || gm.indices.empty()) continue;

                std::vector<Vertex> verts;
                verts.reserve(gm.vertices.size());
                for (const auto& mv : gm.vertices) {
                    Vertex v;
                    v.position = mv.position;
                    v.normal   = mv.normal;
                    v.uv       = mv.uv;
                    v.color    = 0xffffffff;
                    v.tangent  = mv.tangent;
                    verts.push_back(v);
                }

                auto mesh = std::make_shared<Mesh>(
                    Mesh::create(verts.data(), verts.size(),
                                 gm.indices.data(), gm.indices.size()));
                if (!mesh->valid()) continue;

                Material mat;
                mat.base_color   = gm.base_color;
                mat.metallic     = gm.metallic;
                mat.roughness    = gm.roughness;
                mat.emissive     = gm.emissive;
                mat.double_sided = gm.double_sided;
                if (gm.albedo_tex >= 0 && gm.albedo_tex < (int)model->textures.size())
                    mat.albedo = &model->textures[gm.albedo_tex];
                if (gm.normal_tex >= 0 && gm.normal_tex < (int)model->textures.size())
                    mat.normal_map = &model->textures[gm.normal_tex];
                if (gm.mr_tex >= 0 && gm.mr_tex < (int)model->textures.size())
                    mat.mr_map = &model->textures[gm.mr_tex];
                if (gm.emissive_tex >= 0 && gm.emissive_tex < (int)model->textures.size())
                    mat.emissive_tex = &model->textures[gm.emissive_tex];
                mat.alpha_mode   = static_cast<AlphaMode>(gm.alpha_mode);
                mat.alpha_cutoff = gm.alpha_cutoff;

                m_submeshes.push_back({std::move(mesh), mat, gm.node_transform});
            }

            SOL_INFO("MeshNode '" + name + "': loaded "
                     + std::to_string(m_submeshes.size()) + " submeshes");
        }
    } else {
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

    // Load texture overrides if paths are set
    if (!mat_albedo_path.empty() || !mat_normal_path.empty() ||
        !mat_mr_path.empty()    || !mat_emissive_path.empty())
        apply_material_textures(engine);

    if (!collision_enabled) return;

    auto* ps = engine.physics().system();
    if (!ps) return;

    const glm::mat4 world = global_transform();
    const glm::vec3 pos = glm::vec3(world[3]);
    const glm::vec3 half_extents = glm::abs(scale) * 0.5f;

    JPH::BoxShapeSettings shape_settings(JPH::Vec3(half_extents.x, half_extents.y, half_extents.z));
    auto shape_result = shape_settings.Create();
    if (!shape_result.IsValid()) return;

    JPH::BodyInterface& bi = ps->GetBodyInterface();
    JPH::BodyCreationSettings settings(
        shape_result.Get(),
        JPH::Vec3(pos.x, pos.y, pos.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        LAYER_NON_MOVING);

    JPH::Body* body = bi.CreateBody(settings);
    if (!body) return;

    bi.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    m_body_ids.push_back(body->GetID().GetIndexAndSequenceNumber());
}

void MeshNode::on_render(Engine& engine, const glm::mat4& world_xform) {
    if (!m_submeshes.empty()) {
        for (const auto& sm : m_submeshes)
            engine.renderer().submit(*sm.mesh, sm.mat, world_xform * sm.node_xform);
        return;
    }

    if (m_mesh && m_mesh->valid())
        engine.renderer().submit(*m_mesh, material, world_xform);
}

void MeshNode::on_destroy(Engine& engine) {
    auto* ps = engine.physics().system();
    if (ps) {
        JPH::BodyInterface& bi = ps->GetBodyInterface();
        for (uint32_t raw : m_body_ids) {
            JPH::BodyID id(raw);
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        }
    }

    m_body_ids.clear();
    m_submeshes.clear();
    m_model_ref.reset();
    m_mesh.reset();
}

static std::shared_ptr<sol::Texture> load_texture_from_path_(const std::string& path) {
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!pixels) {
        SOL_WARN("apply_material_textures: failed to load '" + path + "'");
        return nullptr;
    }
    auto tex = std::make_shared<sol::Texture>(sol::Texture::from_rgba8(pixels, w, h));
    stbi_image_free(pixels);
    return tex->valid() ? tex : nullptr;
}

void MeshNode::apply_material_textures(Engine&) {
    if (!mat_albedo_path.empty()) {
        m_owned_albedo = load_texture_from_path_(mat_albedo_path);
        material.albedo = m_owned_albedo ? m_owned_albedo.get() : nullptr;
    }
    if (!mat_normal_path.empty()) {
        m_owned_normal = load_texture_from_path_(mat_normal_path);
        material.normal_map = m_owned_normal ? m_owned_normal.get() : nullptr;
    }
    if (!mat_mr_path.empty()) {
        m_owned_mr = load_texture_from_path_(mat_mr_path);
        material.mr_map = m_owned_mr ? m_owned_mr.get() : nullptr;
    }
    if (!mat_emissive_path.empty()) {
        m_owned_emissive = load_texture_from_path_(mat_emissive_path);
        material.emissive_tex = m_owned_emissive ? m_owned_emissive.get() : nullptr;
    }
}

// ============================================================
//  ModelNode
// ============================================================

void ModelNode::on_ready(Engine& engine) {
    if (path.empty()) {
        SOL_WARN("ModelNode '" + name + "': no path set");
        return;
    }

    ModelLoadParams params;
    params.texture_search_dir = texture_dir;
    params.albedo_override    = albedo_override;
    auto model = engine.assets().load(path, params);
    if (!model) {
        SOL_ERROR("ModelNode '" + name + "': failed to load '" + path + "'");
        return;
    }

    m_model_ref = model;  // keep textures alive
    m_submeshes.reserve(model->meshes.size());

    for (const auto& gm : model->meshes) {
        if (gm.vertices.empty() || gm.indices.empty()) continue;

        std::vector<Vertex> verts;
        verts.reserve(gm.vertices.size());
        for (const auto& mv : gm.vertices) {
            Vertex v;
            v.position = mv.position;
            v.normal   = mv.normal;
            v.uv       = mv.uv;
            v.color    = 0xffffffff;
            v.tangent  = mv.tangent;
            verts.push_back(v);
        }

        auto mesh = std::make_shared<Mesh>(
            Mesh::create(verts.data(), verts.size(),
                         gm.indices.data(), gm.indices.size()));
        if (!mesh->valid()) continue;

        Material mat;
        mat.base_color   = gm.base_color;
        mat.metallic     = gm.metallic;
        mat.roughness    = gm.roughness;
        mat.emissive     = gm.emissive;
        mat.double_sided = gm.double_sided;
        if (gm.albedo_tex >= 0 && gm.albedo_tex < (int)model->textures.size())
            mat.albedo = &model->textures[gm.albedo_tex];
        if (gm.normal_tex >= 0 && gm.normal_tex < (int)model->textures.size())
            mat.normal_map = &model->textures[gm.normal_tex];
        if (gm.mr_tex >= 0 && gm.mr_tex < (int)model->textures.size())
            mat.mr_map = &model->textures[gm.mr_tex];
        if (gm.emissive_tex >= 0 && gm.emissive_tex < (int)model->textures.size())
            mat.emissive_tex = &model->textures[gm.emissive_tex];
        mat.alpha_mode   = static_cast<AlphaMode>(gm.alpha_mode);
        mat.alpha_cutoff = gm.alpha_cutoff;

        m_submeshes.push_back({std::move(mesh), mat, gm.node_transform});
    }

    SOL_INFO("ModelNode '" + name + "': loaded " +
             std::to_string(m_submeshes.size()) + " submeshes");
}

void ModelNode::on_render(Engine& engine, const glm::mat4& world_xform) {
    for (const auto& sm : m_submeshes)
        engine.renderer().submit(*sm.mesh, sm.mat, world_xform * sm.node_xform);
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
//  WorldEnvironment
// ============================================================

void WorldEnvironment::on_render(Engine& engine, const glm::mat4& /*xform*/) {
    auto& r = engine.renderer();
    auto& s = r.settings();

    if (sky_mode == 1 && !hdr_path.empty()) {
        r.set_hdr_sky(hdr_path);
    } else {
        glm::vec3 sun_dir{0.0f, 1.0f, 0.0f};
        if (follow_sun) {
            Node* root = this;
            while (root->parent()) root = root->parent();
            if (auto* dl = root->find_first<DirectionalLight>())
                sun_dir = glm::normalize(-dl->world_direction());
        }
        float cos_r = std::cos(glm::radians(sun_disk_size));
        r.set_sky(sun_dir, zenith_color, horizon_color, sun_color, cos_r);
    }
    s.ibl_enabled        = ibl_enabled;
    s.ibl_intensity      = ibl_intensity;
    s.ibl_diffuse_scale  = ibl_diffuse_scale;
    s.ibl_specular_scale = ibl_specular_scale;

    r.set_ambient(ambient_color * ambient_intensity);
    s.tonemap_mode = tonemap_mode;
    s.exposure     = exposure;
    s.bloom_enabled   = bloom_enabled;
    s.bloom_threshold = bloom_threshold;
    s.bloom_intensity = bloom_intensity;
    s.ssao_enabled  = ssao_enabled;
    s.ssao_radius   = ssao_radius;
    s.ssao_bias     = ssao_bias;
    s.ssao_power    = ssao_power;
    s.ssao_strength = ssao_strength;
    s.ssr_enabled          = ssr_enabled;
    s.ssr_steps            = ssr_steps;
    s.ssr_thickness        = ssr_thickness;
    s.ssr_max_distance     = ssr_max_distance;
    s.ssr_roughness_cutoff = ssr_roughness_cutoff;
    s.ssr_intensity        = ssr_intensity;
    s.vol_enabled    = vol_enabled;
    s.vol_density    = vol_density;
    s.vol_scattering = vol_scattering;
    s.vol_g          = vol_g;
    s.vol_march_steps = vol_march_steps;
    s.aa_mode       = static_cast<AaMode>(aa_mode);
    s.taa_blend     = taa_blend;
    s.taa_sharpening = taa_sharpening;
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

static std::unique_ptr<Node> node_from_json(const json& j, int depth = 0);

static std::unique_ptr<Node> node_from_json(const json& j, int depth) {
    if (depth > 64) {
        SOL_WARN("scene: node tree depth limit exceeded — truncating");
        auto n = std::make_unique<Node3D>();
        n->name = j.value("name", "Node");
        n->script_path = j.value("script_path", "");
        return n;
    }

    std::string type = j.value("type", "Node3D");
    std::unique_ptr<Node> node = NodeFactory::instance().create(type);
    if (!node) {
        if (type != "Node3D")
            SOL_WARN("scene: unknown node type '" + type + "' — loading as Node3D");
        node = std::make_unique<Node3D>();
    }

    auto read_node3d = [&](Node3D* n) {
        if (j.contains("position")) n->position = vec3_from_json(j["position"]);
        if (j.contains("rotation")) n->rotation = vec3_from_json(j["rotation"]);
        if (j.contains("scale"))    n->scale    = vec3_from_json(j["scale"], {1,1,1});
    };

    if (auto* n3d = dynamic_cast<Node3D*>(node.get())) {
        read_node3d(n3d);
    }

    if (type == "MeshNode") {
        auto* n = dynamic_cast<MeshNode*>(node.get());
        if (!n) return node;
        n->mesh_name = j.value("mesh", "cube");
        n->mesh_path = j.value("mesh_path", "");
        n->collision_enabled = j.value("collision_enabled", true);
        if (j.contains("material")) {
            const auto& m = j["material"];
            if (m.contains("base_color"))
                n->material.base_color = vec4_from_json(m["base_color"]);
            n->material.metallic     = m.value("metallic",     0.0f);
            n->material.roughness    = m.value("roughness",    0.5f);
            if (m.contains("emissive"))
                n->material.emissive = vec3_from_json(m["emissive"]);
            n->material.alpha_mode   = static_cast<AlphaMode>(m.value("alpha_mode", 0));
            n->material.alpha_cutoff = m.value("alpha_cutoff", 0.5f);
            n->material.lit          = m.value("lit", true);
            n->material.double_sided = m.value("double_sided", false);
        }
        n->mat_albedo_path   = j.value("mat_albedo_path",   "");
        n->mat_normal_path   = j.value("mat_normal_path",   "");
        n->mat_mr_path       = j.value("mat_mr_path",       "");
        n->mat_emissive_path = j.value("mat_emissive_path", "");
    } else if (type == "Camera3D") {
        auto* n = dynamic_cast<Camera3D*>(node.get());
        if (!n) return node;
        n->fov       = j.value("fov",       70.0f);
        n->near_clip = j.value("near_clip", j.value("near", 0.05f));
        n->far_clip  = j.value("far_clip",  j.value("far",  1000.0f));
        n->current   = j.value("current",   false);
    } else if (type == "DirectionalLight") {
        auto* n = dynamic_cast<DirectionalLight*>(node.get());
        if (!n) return node;
        if (j.contains("color")) n->color = vec3_from_json(j["color"], {1,1,1});
        n->intensity    = j.value("intensity",    1.0f);
        n->cast_shadow  = j.value("cast_shadow",  n->cast_shadow);
        n->shadow_mode  = j.value("shadow_mode",  n->shadow_mode);
        n->csm_far              = j.value("csm_far",              100.0f);
        n->csm_lambda           = j.value("csm_lambda",           0.75f);
        n->shadow_quality       = j.value("shadow_quality",       1);
        n->shadow_pcf_radius    = j.value("shadow_pcf_radius",    1.5f);
        n->shadow_pcss_light    = j.value("shadow_pcss_light",    3.0f);
        n->contact_shadow_dist  = j.value("contact_shadow_dist",  0.0f);
        n->contact_shadow_thick = j.value("contact_shadow_thick", 0.5f);
        n->temporal_shadow      = j.value("temporal_shadow",      true);
        n->temporal_shadow_alpha = j.value("temporal_shadow_alpha", 0.1f);
    } else if (type == "PointLight") {
        auto* n = dynamic_cast<PointLight*>(node.get());
        if (!n) return node;
        if (j.contains("color")) n->color = vec3_from_json(j["color"], {1,1,1});
        n->intensity = j.value("intensity", 1.0f);
        n->range     = j.value("range", 10.0f);
    } else if (type == "CollisionShape3D") {
        auto* n = dynamic_cast<CollisionShape3D*>(node.get());
        if (!n) return node;
        std::string sh = j.value("shape", "box");
        if      (sh == "sphere")  n->shape = CollisionShape3D::Shape::Sphere;
        else if (sh == "capsule") n->shape = CollisionShape3D::Shape::Capsule;
        else                      n->shape = CollisionShape3D::Shape::Box;
        if (j.contains("extents")) n->extents = vec3_from_json(j["extents"], {0.5f,0.5f,0.5f});
        n->radius = j.value("radius", 0.5f);
        n->height = j.value("height", 1.0f);
    } else if (type == "CharacterBody3D") {
        auto* n = dynamic_cast<CharacterBody3D*>(node.get());
        if (!n) return node;
        n->capsule_radius = j.value("capsule_radius", 0.35f);
        n->capsule_height = j.value("capsule_height", 1.5f);
    } else if (type == "SceneInstance") {
        auto* n = dynamic_cast<SceneInstance*>(node.get());
        if (!n) return node;
        n->scene_path = j.value("scene", "");
    } else if (type == "ModelNode") {
        auto* n = dynamic_cast<ModelNode*>(node.get());
        if (!n) return node;
        n->path            = j.value("path",           "");
        n->texture_dir     = j.value("texture_dir",    "");
        n->albedo_override = j.value("albedo",         "");
    } else if (type == "ScriptNode") {
        // No extra fields beyond Node3D + script_path.
    } else if (type == "FlyCamController") {
        auto* n = dynamic_cast<FlyCamController*>(node.get());
        if (!n) return node;
        n->yaw          = j.value("yaw",           n->yaw);
        n->pitch        = j.value("pitch",         n->pitch);
        n->move_speed   = j.value("move_speed",    n->move_speed);
        n->mouse_sens   = j.value("mouse_sens",    n->mouse_sens);
        n->key_look_spd = j.value("key_look_spd",  n->key_look_spd);
        n->fov          = j.value("fov",           n->fov);
        n->near_clip    = j.value("near_clip",     n->near_clip);
        n->far_clip     = j.value("far_clip",      n->far_clip);
        n->models_dir   = j.value("models_dir",    n->models_dir);
        n->show_ui      = j.value("show_ui",       n->show_ui);
        if (j.contains("extra_scenes") && j["extra_scenes"].is_array()) {
            for (const auto& s : j["extra_scenes"])
                n->extra_scenes.push_back(s.get<std::string>());
        }
    } else if (type == "WorldEnvironment") {
        auto* n = dynamic_cast<WorldEnvironment*>(node.get());
        if (!n) return node;
        if (j.contains("zenith_color"))  n->zenith_color  = vec3_from_json(j["zenith_color"],  {0.08f, 0.15f, 0.40f});
        if (j.contains("horizon_color")) n->horizon_color = vec3_from_json(j["horizon_color"], {0.50f, 0.60f, 0.70f});
        if (j.contains("sun_color"))     n->sun_color     = vec3_from_json(j["sun_color"],     {3.0f,  2.5f,  2.0f});
        if (j.contains("ambient_color")) n->ambient_color = vec3_from_json(j["ambient_color"], {0.30f, 0.30f, 0.35f});
        n->sky_mode          = j.value("sky_mode",          0);
        n->hdr_path          = j.value("hdr_path",          "");
        n->sun_disk_size     = j.value("sun_disk_size",     1.8f);
        n->follow_sun        = j.value("follow_sun",        true);
        n->ambient_intensity = j.value("ambient_intensity", 1.0f);
        n->tonemap_mode      = j.value("tonemap_mode",      3);
        n->exposure          = j.value("exposure",          1.0f);
        n->bloom_enabled     = j.value("bloom_enabled",     true);
        n->bloom_threshold   = j.value("bloom_threshold",   1.2f);
        n->bloom_intensity   = j.value("bloom_intensity",   0.15f);
        n->ssao_enabled      = j.value("ssao_enabled",      false);
        n->ssao_radius       = j.value("ssao_radius",       0.5f);
        n->ssao_bias         = j.value("ssao_bias",         0.025f);
        n->ssao_power        = j.value("ssao_power",        2.0f);
        n->ssao_strength     = j.value("ssao_strength",     1.0f);
        n->ssr_enabled          = j.value("ssr_enabled",          false);
        n->ssr_steps            = j.value("ssr_steps",            64);
        n->ssr_thickness        = j.value("ssr_thickness",        0.3f);
        n->ssr_max_distance     = j.value("ssr_max_distance",     8.0f);
        n->ssr_roughness_cutoff = j.value("ssr_roughness_cutoff", 0.5f);
        n->ssr_intensity        = j.value("ssr_intensity",        1.0f);
        n->ibl_enabled       = j.value("ibl_enabled",       true);
        n->ibl_intensity     = j.value("ibl_intensity",     1.0f);
        n->ibl_diffuse_scale  = j.value("ibl_diffuse_scale",  1.0f);
        n->ibl_specular_scale = j.value("ibl_specular_scale", 1.0f);
        n->vol_enabled    = j.value("vol_enabled",    false);
        n->vol_density    = j.value("vol_density",    0.05f);
        n->vol_scattering = j.value("vol_scattering", 0.3f);
        n->vol_g          = j.value("vol_g",          0.0f);
        n->vol_march_steps = j.value("vol_march_steps", 32);
        n->aa_mode        = j.value("aa_mode",        4);
        n->taa_blend      = j.value("taa_blend",      0.1f);
        n->taa_sharpening = j.value("taa_sharpening", 0.2f);
    } else if (type == "AudioStreamPlayer") {
        auto* n = dynamic_cast<AudioStreamPlayer*>(node.get());
        if (!n) return node;
        n->clip_path = j.value("clip_path", "");
        n->volume    = j.value("volume",    1.0f);
        n->pitch     = j.value("pitch",     1.0f);
        n->loop      = j.value("loop",      false);
        n->autoplay  = j.value("autoplay",  false);
        n->bus       = j.value("bus",       "SFX");
    } else if (type == "AudioStreamPlayer3D") {
        auto* n = dynamic_cast<AudioStreamPlayer3D*>(node.get());
        if (!n) return node;
        n->clip_path    = j.value("clip_path",    "");
        n->volume       = j.value("volume",       1.0f);
        n->pitch        = j.value("pitch",        1.0f);
        n->loop         = j.value("loop",         false);
        n->autoplay     = j.value("autoplay",     false);
        n->bus          = j.value("bus",          "SFX");
        n->max_distance = j.value("max_distance", 20.0f);
        n->attenuation  = j.value("attenuation",  1.0f);
    } else if (type == "RigidBody3D") {
        auto* n = dynamic_cast<RigidBody3D*>(node.get());
        if (n) {
            n->mass          = j.value("mass",          1.0f);
            n->gravity_scale = j.value("gravity_scale", 1.0f);
            n->is_kinematic  = j.value("is_kinematic",  false);
        }
    }
    // Area3D has no extra properties beyond Node3D transform

    node->name = j.value("name", "Node");
    node->script_path = j.value("script_path", "");

    // Restore LuaComponents saved in scene file.
    if (j.contains("components") && j["components"].is_array()) {
        for (const auto& cj : j["components"]) {
            const std::string ctype = cj.value("type", "");
            if (ctype == "LuaComponent") {
                const std::string cpath = cj.value("script_path", "");
                if (!cpath.empty())
                    node->add_component(std::make_unique<LuaComponent>(cpath));
            }
        }
    }

    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& cj : j["children"])
            node->add_child(node_from_json(cj, depth + 1));
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
    if (!node->script_path.empty())
        j["script_path"] = node->script_path;

    // Serialize attached LuaComponents.
    {
        json comp_arr = json::array();
        for (const auto& c : node->components()) {
            if (std::strcmp(c->component_type(), "LuaComponent") == 0) {
                if (const auto* lc = dynamic_cast<const LuaComponent*>(c.get())) {
                    json cj;
                    cj["type"] = "LuaComponent";
                    if (!lc->script_path().empty())
                        cj["script_path"] = lc->script_path();
                    comp_arr.push_back(cj);
                }
            }
        }
        if (!comp_arr.empty())
            j["components"] = comp_arr;
    }

    if (const auto* n3d = dynamic_cast<const Node3D*>(node)) {
        j["position"] = vec3_to_json(n3d->position);
        j["rotation"] = vec3_to_json(n3d->rotation);
        j["scale"]    = vec3_to_json(n3d->scale);
    }

    if (const auto* n = dynamic_cast<const MeshNode*>(node)) {
        j["mesh"] = n->mesh_name;
        if (!n->mesh_path.empty())
            j["mesh_path"] = n->mesh_path;
        j["collision_enabled"] = n->collision_enabled;
        json m;
        m["base_color"]   = vec4_to_json(n->material.base_color);
        m["metallic"]     = n->material.metallic;
        m["roughness"]    = n->material.roughness;
        m["emissive"]     = vec3_to_json(n->material.emissive);
        m["alpha_mode"]   = (int)n->material.alpha_mode;
        m["alpha_cutoff"] = n->material.alpha_cutoff;
        m["lit"]          = n->material.lit;
        m["double_sided"] = n->material.double_sided;
        j["material"] = m;
        if (!n->mat_albedo_path.empty())   j["mat_albedo_path"]   = n->mat_albedo_path;
        if (!n->mat_normal_path.empty())   j["mat_normal_path"]   = n->mat_normal_path;
        if (!n->mat_mr_path.empty())       j["mat_mr_path"]       = n->mat_mr_path;
        if (!n->mat_emissive_path.empty()) j["mat_emissive_path"] = n->mat_emissive_path;
    } else if (const auto* n = dynamic_cast<const Camera3D*>(node)) {
        j["fov"]       = n->fov;
        j["near_clip"] = n->near_clip;
        j["far_clip"]  = n->far_clip;
        j["current"]   = n->current;
    } else if (const auto* n = dynamic_cast<const DirectionalLight*>(node)) {
        j["color"]               = vec3_to_json(n->color);
        j["intensity"]           = n->intensity;
        j["cast_shadow"]         = n->cast_shadow;
        j["shadow_mode"]         = n->shadow_mode;
        j["csm_far"]             = n->csm_far;
        j["csm_lambda"]          = n->csm_lambda;
        j["shadow_quality"]      = n->shadow_quality;
        j["shadow_pcf_radius"]   = n->shadow_pcf_radius;
        j["shadow_pcss_light"]   = n->shadow_pcss_light;
        j["contact_shadow_dist"]  = n->contact_shadow_dist;
        j["contact_shadow_thick"] = n->contact_shadow_thick;
        j["temporal_shadow"]      = n->temporal_shadow;
        j["temporal_shadow_alpha"] = n->temporal_shadow_alpha;
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
    } else if (const auto* mn = dynamic_cast<const ModelNode*>(node)) {
        j["path"] = mn->path;
        if (!mn->texture_dir.empty())     j["texture_dir"] = mn->texture_dir;
        if (!mn->albedo_override.empty()) j["albedo"]      = mn->albedo_override;
    } else if (dynamic_cast<const ScriptNode*>(node)) {
        // No extra fields beyond Node3D + script_path.
    } else if (const auto* fc = dynamic_cast<const FlyCamController*>(node)) {
        j["yaw"]          = fc->yaw;
        j["pitch"]        = fc->pitch;
        j["move_speed"]   = fc->move_speed;
        j["mouse_sens"]   = fc->mouse_sens;
        j["key_look_spd"] = fc->key_look_spd;
        j["fov"]          = fc->fov;
        j["near_clip"]    = fc->near_clip;
        j["far_clip"]     = fc->far_clip;
        j["models_dir"]   = fc->models_dir;
        j["show_ui"]      = fc->show_ui;
        if (!fc->extra_scenes.empty()) {
            json arr = json::array();
            for (const auto& s : fc->extra_scenes) arr.push_back(s);
            j["extra_scenes"] = arr;
        }
    } else if (const auto* we = dynamic_cast<const WorldEnvironment*>(node)) {
        j["sky_mode"]          = we->sky_mode;
        j["hdr_path"]          = we->hdr_path;
        j["zenith_color"]      = vec3_to_json(we->zenith_color);
        j["horizon_color"]     = vec3_to_json(we->horizon_color);
        j["sun_color"]         = vec3_to_json(we->sun_color);
        j["sun_disk_size"]     = we->sun_disk_size;
        j["follow_sun"]        = we->follow_sun;
        j["ambient_color"]     = vec3_to_json(we->ambient_color);
        j["ambient_intensity"] = we->ambient_intensity;
        j["tonemap_mode"]      = we->tonemap_mode;
        j["exposure"]          = we->exposure;
        j["bloom_enabled"]     = we->bloom_enabled;
        j["bloom_threshold"]   = we->bloom_threshold;
        j["bloom_intensity"]   = we->bloom_intensity;
        j["ssao_enabled"]      = we->ssao_enabled;
        j["ssao_radius"]       = we->ssao_radius;
        j["ssao_bias"]         = we->ssao_bias;
        j["ssao_power"]        = we->ssao_power;
        j["ssao_strength"]     = we->ssao_strength;
        j["ssr_enabled"]          = we->ssr_enabled;
        j["ssr_steps"]            = we->ssr_steps;
        j["ssr_thickness"]        = we->ssr_thickness;
        j["ssr_max_distance"]     = we->ssr_max_distance;
        j["ssr_roughness_cutoff"] = we->ssr_roughness_cutoff;
        j["ssr_intensity"]        = we->ssr_intensity;
        j["ibl_enabled"]       = we->ibl_enabled;
        j["ibl_intensity"]     = we->ibl_intensity;
        j["ibl_diffuse_scale"]  = we->ibl_diffuse_scale;
        j["ibl_specular_scale"] = we->ibl_specular_scale;
        j["vol_enabled"]    = we->vol_enabled;
        j["vol_density"]    = we->vol_density;
        j["vol_scattering"] = we->vol_scattering;
        j["vol_g"]          = we->vol_g;
        j["vol_march_steps"] = we->vol_march_steps;
        j["aa_mode"]        = we->aa_mode;
        j["taa_blend"]      = we->taa_blend;
        j["taa_sharpening"] = we->taa_sharpening;
    } else if (const auto* n = dynamic_cast<const AudioStreamPlayer*>(node)) {
        if (!n->clip_path.empty()) j["clip_path"] = n->clip_path;
        j["volume"]   = n->volume;
        j["pitch"]    = n->pitch;
        j["loop"]     = n->loop;
        j["autoplay"] = n->autoplay;
        j["bus"]      = n->bus;
    } else if (const auto* n = dynamic_cast<const AudioStreamPlayer3D*>(node)) {
        if (!n->clip_path.empty()) j["clip_path"] = n->clip_path;
        j["volume"]       = n->volume;
        j["pitch"]        = n->pitch;
        j["loop"]         = n->loop;
        j["autoplay"]     = n->autoplay;
        j["bus"]          = n->bus;
        j["max_distance"] = n->max_distance;
        j["attenuation"]  = n->attenuation;
    } else if (const auto* n = dynamic_cast<const RigidBody3D*>(node)) {
        j["mass"]          = n->mass;
        j["gravity_scale"] = n->gravity_scale;
        j["is_kinematic"]  = n->is_kinematic;
    }
    // Area3D: no extra serialised fields

    if (!node->children().empty()) {
        json arr = json::array();
        for (const auto& c : node->children()) {
            // Skip runtime-injected FlyCamController clones — they must not be persisted.
            // If saved, they'd reload without m_is_clone=true and trigger an infinite loop.
            if (const auto* fc = dynamic_cast<const FlyCamController*>(c.get())) {
                if (fc->is_clone()) continue;
            }
            arr.push_back(node_to_json(c.get()));
        }
        if (!arr.empty()) j["children"] = arr;
    }

    return j;
}

// ============================================================
//  Scene — traversal
// ============================================================

void Scene::ready_node(Engine& engine, Node* node) {
    node->on_ready(engine);
    // Dispatch component on_ready in attachment order.
    for (auto& comp : node->components())
        comp->on_ready(engine);
    if (!node->script_path.empty() && engine.has_script())
        engine.script().node_ready(node, engine);
    // Use index-based loop: on_ready may add children (SceneInstance).
    for (size_t i = 0; i < node->children().size(); ++i)
        ready_node(engine, node->children()[i].get());
}

void Scene::destroy_node(Engine& engine, Node* node) {
    for (auto& c : node->children())
        destroy_node(engine, c.get());
    if (!node->script_path.empty() && engine.has_script())
        engine.script().node_detach(node);
    // Destroy components in reverse order (LIFO — mirrors typical destructor order).
    auto& comps = const_cast<std::vector<std::unique_ptr<IComponent>>&>(node->components());
    for (int i = static_cast<int>(comps.size()) - 1; i >= 0; --i)
        comps[i]->on_destroy(engine);
    node->on_destroy(engine);
}

void Scene::update_node_phase(Engine& engine, Node* node, float dt, bool pre_physics) {
    if (is_pre_phase(node->tick_group) == pre_physics) {
        node->on_update(engine, dt);
        for (auto& comp : node->components())
            if (is_pre_phase(comp->tick_group()) == pre_physics)
                comp->on_update(engine, dt);
        if (!node->script_path.empty() && engine.has_script())
            engine.script().node_update(node, engine, dt);
    }
    for (size_t i = 0; i < node->children().size(); ++i)
        update_node_phase(engine, node->children()[i].get(), dt, pre_physics);
}

void Scene::render_node(Engine& engine, Node* node, const glm::mat4& parent_xform) {
    if (auto* n3d = dynamic_cast<Node3D*>(node); n3d && !n3d->visible) return;

    glm::mat4 world = parent_xform;
    if (auto* n3d = dynamic_cast<Node3D*>(node))
        world = parent_xform * n3d->local_transform();

    node->on_render(engine, world);

    // Index-based: on_render may append children
    for (size_t i = 0; i < node->children().size(); ++i)
        render_node(engine, node->children()[i].get(), world);
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
    // Scene-specific HDR sky overrides the default set by FlyCamController.
    if (!hdr_sky.empty())
        engine.renderer().set_hdr_sky(hdr_sky);
}

void Scene::on_destroy(Engine& engine) {
    if (m_root) destroy_node(engine, m_root.get());
}

void Scene::update_pre_physics(Engine& engine, float dt) {
    if (m_root) update_node_phase(engine, m_root.get(), dt, true);
    engine.ecs_world().tick(ETickGroup::PrePhysics, dt);
    engine.ecs_world().tick(ETickGroup::DuringPhysics, dt);
}

void Scene::update_post_physics(Engine& engine, float dt) {
    if (m_root) update_node_phase(engine, m_root.get(), dt, false);
    engine.ecs_world().tick(ETickGroup::PostPhysics, dt);
    engine.ecs_world().tick(ETickGroup::PostUpdateWork, dt);
    flush_deferred();
}

// Combined (backward compat): pre + post without physics step between them.
void Scene::update(Engine& engine, float dt) {
    update_pre_physics(engine, dt);
    update_post_physics(engine, dt);
}

void Scene::defer(std::function<void()> cmd) {
    m_deferred.push_back(std::move(cmd));
}

void Scene::flush_deferred() {
    // Loop-safe: commands queued inside the flush are also processed.
    while (!m_deferred.empty()) {
        std::vector<std::function<void()>> batch = std::move(m_deferred);
        m_deferred.clear();
        for (auto& cmd : batch)
            cmd();
    }
}

void Scene::spawn_node(Node* parent, std::unique_ptr<Node> child, Engine& engine) {
    if (!parent || !child) return;
    Node* raw = child.get();
    auto owned = std::make_shared<std::unique_ptr<Node>>(std::move(child));
    defer([this, parent, raw, owned, &engine]() {
        parent->add_child(std::move(*owned));
        ready_node(engine, raw);
    });
}

void Scene::despawn_node(Node* node, Engine& engine) {
    if (!node) return;
    Node* par = node->parent();
    if (!par) return;
    defer([this, node, par, &engine]() {
        destroy_node(engine, node);
        par->remove_child(node);
    });
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
    engine.renderer().set_ambient({0.30f, 0.30f, 0.35f});
    std::function<void(Node*)> collect_lights = [&](Node* node) {
        if (auto* dl = dynamic_cast<DirectionalLight*>(node)) {
            Light l;
            l.type = LightType::Directional;
            l.direction = dl->world_direction(); // normalized, toward-light direction
            l.color = dl->color;
            l.intensity = dl->intensity;
            l.cast_shadow = dl->cast_shadow;
            l.shadow_mode = dl->shadow_mode;
            engine.renderer().submit_light(l);

            // Drive skybox from the directional light (toward_sun = -shining direction)
            engine.renderer().set_sky(-dl->world_direction());
            auto& rs = engine.renderer().settings();
            rs.csm_far              = dl->csm_far;
            rs.csm_lambda           = dl->csm_lambda;
            rs.shadow_quality       = dl->shadow_quality;
            rs.shadow_pcf_radius    = dl->shadow_pcf_radius;
            rs.shadow_pcss_light    = dl->shadow_pcss_light;
            rs.contact_shadow_distance  = dl->contact_shadow_dist;
            rs.contact_shadow_thickness = dl->contact_shadow_thick;
            rs.temporal_shadow_enabled  = dl->temporal_shadow;
            rs.temporal_shadow_alpha    = dl->temporal_shadow_alpha;
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
    scene->hdr_sky = j.value("hdr_sky", "");

    if (j.contains("root")) {
        try {
            scene->set_root(node_from_json(j["root"]));
        } catch (const std::exception& e) {
            SOL_ERROR(std::string("Scene::load node parse error: ") + e.what());
            return nullptr;
        }
    }

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
    return f.good();
}

std::string Scene::save_to_string() const {
    json j;
    j["sol_scene"] = 1;
    j["name"]      = name;
    j["path"]      = path;
    j["hdr_sky"]   = hdr_sky;
    if (m_root) j["root"] = node_to_json(m_root.get());
    return j.dump(2);
}

std::unique_ptr<Scene> Scene::load_from_string(const std::string& json_str, Engine& /*engine*/) {
    json j;
    try {
        j = json::parse(json_str);
    } catch (const std::exception& e) {
        SOL_ERROR(std::string("Scene::load_from_string JSON parse error: ") + e.what());
        return nullptr;
    }

    auto scene      = std::make_unique<Scene>();
    scene->name     = j.value("name", "Scene");
    scene->path     = j.value("path", "");
    scene->hdr_sky  = j.value("hdr_sky", "");

    if (j.contains("root")) {
        try {
            scene->set_root(node_from_json(j["root"]));
        } catch (const std::exception& e) {
            SOL_ERROR(std::string("Scene::load_from_string node parse error: ") + e.what());
            return nullptr;
        }
    }
    return scene;
}

} // namespace sol
