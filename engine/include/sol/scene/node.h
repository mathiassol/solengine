#pragma once
#include "sol/export.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace sol {

class Engine;

// Base class for every object in a scene tree.
// Nodes own their children; the parent pointer is non-owning.
class SOL_API Node {
public:
    std::string name;

    Node()          = default;
    virtual ~Node() = default;

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

private:
    Node* m_parent = nullptr;
    std::vector<std::unique_ptr<Node>> m_children;
};

} // namespace sol
