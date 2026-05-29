#pragma once
#include "sol/export.h"
#include <entt/entt.hpp>
#include <functional>
#include <vector>

namespace sol {

// UE5 FMassCommandBuffer equivalent.
// Collects deferred structural ECS mutations (entity create/destroy, component add/remove)
// that are unsafe to perform during a processor's execute() call.
// Call flush() after all processors in a tick group have finished.
class SOL_API MassCommandBuffer {
public:
    using Command = std::function<void(entt::registry&)>;

    // Queue a deferred command.
    void push(Command cmd) { m_commands.emplace_back(std::move(cmd)); }

    // Convenience: queue entity destruction.
    void destroy_entity(entt::entity e) {
        push([e](entt::registry& r) { if (r.valid(e)) r.destroy(e); });
    }

    // Flush all queued commands against the given registry. Clears the buffer.
    // Loop-safe: new commands pushed during flush are also flushed.
    void flush(entt::registry& registry) {
        size_t i = 0;
        while (i < m_commands.size()) {
            m_commands[i](registry);
            ++i;
        }
        m_commands.clear();
    }

    bool   empty() const { return m_commands.empty(); }
    size_t size()  const { return m_commands.size(); }

private:
    std::vector<Command> m_commands;
};

} // namespace sol
