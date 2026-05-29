#pragma once
#include "sol/export.h"
#include "sol/scene/component.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_set>

namespace sol {

class Engine;

// Base class for every object in a scene tree.
// Nodes own their children; the parent pointer is non-owning.
class SOL_API Node {
public:
    std::string name;
    std::string script_path;
    std::unordered_set<std::string> tags;
    void add_tag(const std::string& t)       { tags.insert(t); }
    bool has_tag(const std::string& t) const { return tags.count(t) > 0; }
    void remove_tag(const std::string& t)    { tags.erase(t); }

    // Tick phase — which update pass this node runs in (default: PrePhysics).
    // Stage 1 will honour this when the engine loop is split into phases.
    ETickGroup tick_group = ETickGroup::PrePhysics;

    Node()          = default;
    virtual ~Node() = default;

    // Non-copyable (owns children via unique_ptr)
    Node(const Node&)            = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&)                 = default;
    Node& operator=(Node&&)      = default;

    // Lifecycle callbacks — called by Scene during load, update, render, unload.
    virtual void on_ready  (Engine& engine)                             {}
    virtual void on_update (Engine& engine, float dt)                   {}
    virtual void on_render (Engine& engine, const glm::mat4& world_xform) {}
    virtual void on_destroy(Engine& engine)                             {}

    virtual const char* type_name() const { return "Node"; }

    // Tree access
    Node*  parent() const { return m_parent; }
    const  std::vector<std::unique_ptr<Node>>& children() const { return m_children; }

    void add_child(std::unique_ptr<Node> child);
    bool remove_child(Node* child);

    // Find by name (first match, depth-first)
    Node* find(const std::string& name) const;

    // Find first descendant of type T (depth-first, includes self)
    template<typename T>
    T* find_first() {
        if (auto* t = dynamic_cast<T*>(this)) return t;
        for (auto& c : m_children)
            if (auto* t = c->find_first<T>()) return t;
        return nullptr;
    }

    // Visit all descendants of type T
    template<typename T>
    void each(const std::function<void(T&)>& fn) {
        if (auto* t = dynamic_cast<T*>(this)) fn(*t);
        for (auto& c : m_children) c->each<T>(fn);
    }

    // ---- Component management ----------------------------------------

    // Attach a component; sets owner pointer and returns the raw pointer.
    IComponent* add_component(std::unique_ptr<IComponent> comp);

    // Remove the first component whose component_type() matches type_name.
    // Returns true if found and removed.
    bool remove_component(const char* type_name);

    // Pointer-based removal — safer when multiple components share the same type name.
    bool remove_component(IComponent* comp);

    // Typed accessor — returns nullptr if no component of type T is attached.
    template<typename T>
    T* get_component() {
        for (auto& c : m_components)
            if (auto* t = dynamic_cast<T*>(c.get())) return t;
        return nullptr;
    }

    // Read-only access to the component list.
    const std::vector<std::unique_ptr<IComponent>>& components() const {
        return m_components;
    }

private:
    Node* m_parent = nullptr;
    std::vector<std::unique_ptr<Node>>       m_children;
    std::vector<std::unique_ptr<IComponent>> m_components;
};

} // namespace sol
