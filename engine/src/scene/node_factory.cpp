#include "sol/scene/node_factory.h"

#include "sol/scene/node.h"
#include "sol/scene/node3d.h"
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
#include "sol/scene/audio_stream_player.h"
#include "sol/scene/audio_stream_player3d.h"

#include <mutex>
#include <utility>

namespace sol {

NodeFactory& NodeFactory::instance() {
    static NodeFactory s_instance;
    return s_instance;
}

void NodeFactory::register_type(const std::string& type_name, Creator creator) {
    if (m_creators.count(type_name)) return;
    m_creators[type_name] = std::move(creator);
    m_names.push_back(type_name);
}

std::unique_ptr<Node> NodeFactory::create(const std::string& type_name) const {
    auto it = m_creators.find(type_name);
    if (it == m_creators.end()) return nullptr;
    return it->second();
}

bool NodeFactory::is_registered(const std::string& type_name) const {
    return m_creators.count(type_name) > 0;
}

void register_builtin_node_types() {
    static std::once_flag once;
    std::call_once(once, [] {
        auto& f = NodeFactory::instance();
        f.register_type("Node",              [](){ return std::make_unique<Node>(); });
        f.register_type("Node3D",            [](){ return std::make_unique<Node3D>(); });
        f.register_type("MeshNode",          [](){ return std::make_unique<MeshNode>(); });
        f.register_type("Camera3D",          [](){ return std::make_unique<Camera3D>(); });
        f.register_type("DirectionalLight",  [](){ return std::make_unique<DirectionalLight>(); });
        f.register_type("PointLight",        [](){ return std::make_unique<PointLight>(); });
        f.register_type("CollisionShape3D",  [](){ return std::make_unique<CollisionShape3D>(); });
        f.register_type("StaticBody3D",      [](){ return std::make_unique<StaticBody3D>(); });
        f.register_type("CharacterBody3D",   [](){ return std::make_unique<CharacterBody3D>(); });
        f.register_type("Area3D",            [](){ return std::make_unique<Area3D>(); });
        f.register_type("RigidBody3D",       [](){ return std::make_unique<RigidBody3D>(); });
        f.register_type("SceneInstance",     [](){ return std::make_unique<SceneInstance>(); });
        f.register_type("ModelNode",         [](){ return std::make_unique<ModelNode>(); });
        f.register_type("FlyCamController",  [](){ return std::make_unique<FlyCamController>(); });
        f.register_type("WorldEnvironment",  [](){ return std::make_unique<WorldEnvironment>(); });
        f.register_type("ScriptNode",        [](){ return std::make_unique<ScriptNode>(); });
        f.register_type("AudioStreamPlayer",   [](){ return std::make_unique<AudioStreamPlayer>(); });
        f.register_type("AudioStreamPlayer3D", [](){ return std::make_unique<AudioStreamPlayer3D>(); });
    });
}

} // namespace sol
