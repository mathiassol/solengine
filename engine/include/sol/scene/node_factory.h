#pragma once
#include "sol/export.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sol {

class Node;

// NodeFactory: central registry that maps type-name strings to creator lambdas.
// Replaces the duplicate if-else chains in scene.cpp and host.cpp.
class SOL_API NodeFactory {
public:
    using Creator = std::function<std::unique_ptr<Node>()>;

    static NodeFactory& instance();

    void register_type(const std::string& type_name, Creator creator);

    // Returns nullptr if type_name is unregistered.
    std::unique_ptr<Node> create(const std::string& type_name) const;

    bool is_registered(const std::string& type_name) const;

    // All registered names in registration order (for editor menus).
    const std::vector<std::string>& type_names() const { return m_names; }

private:
    std::unordered_map<std::string, Creator> m_creators;
    std::vector<std::string> m_names;
};

SOL_API void register_builtin_node_types();

} // namespace sol
