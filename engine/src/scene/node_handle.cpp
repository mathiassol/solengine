#include "sol/scene/node_handle.h"
#include <cassert>

namespace sol {

NodeRegistry::NodeRegistry() {
    // Slot 0 is reserved as the "invalid" sentinel — never used for real nodes.
    m_slots.push_back({ nullptr, 0u, false });
}

NodeHandle NodeRegistry::register_node(Node* node) {
    assert(node != nullptr);

    uint32_t index;
    if (!m_free_list.empty()) {
        index = m_free_list.back();
        m_free_list.pop_back();
        m_slots[index].node     = node;
        m_slots[index].occupied = true;
        // generation was already bumped during unregister_node
    } else {
        index = static_cast<uint32_t>(m_slots.size());
        m_slots.push_back({ node, 0u, true });
    }

    ++m_live;
    return NodeHandle{ index, m_slots[index].generation };
}

void NodeRegistry::unregister_node(NodeHandle handle) {
    if (!is_alive(handle)) return;

    auto& slot = m_slots[handle.index];
    slot.node     = nullptr;
    slot.occupied = false;
    ++slot.generation;  // bump: existing handles with old generation become stale
    m_free_list.push_back(handle.index);
    --m_live;
}

Node* NodeRegistry::resolve(NodeHandle handle) const {
    if (handle.index == NodeHandle::INVALID || handle.index >= m_slots.size())
        return nullptr;
    const auto& slot = m_slots[handle.index];
    if (!slot.occupied || slot.generation != handle.generation)
        return nullptr;
    return slot.node;
}

bool NodeRegistry::is_alive(NodeHandle handle) const {
    return resolve(handle) != nullptr;
}

} // namespace sol
