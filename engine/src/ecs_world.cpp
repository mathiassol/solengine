#include "sol/ecs_world.h"
#include <algorithm>
#include <future>
#include <queue>
#include <unordered_map>

namespace sol {

void EcsWorld::register_system(std::string name, ETickGroup group, SystemFn fn) {
    // Replace if already registered under the same name.
    for (auto& s : m_systems) {
        if (s.name == name) {
            s.group = group;
            s.fn    = std::move(fn);
            return;
        }
    }
    m_systems.push_back({ std::move(name), group, std::move(fn) });
}

void EcsWorld::unregister_system(const std::string& name) {
    m_systems.erase(
        std::remove_if(m_systems.begin(), m_systems.end(),
            [&](const SystemEntry& s) { return s.name == name; }),
        m_systems.end());
}

void EcsWorld::tick(ETickGroup group, float dt) {
    for (auto& s : m_systems)
        if (s.group == group)
            s.fn(m_registry, dt);
}

void EcsWorld::tick_all(float dt) {
    constexpr ETickGroup groups[] = {
        ETickGroup::PrePhysics,
        ETickGroup::DuringPhysics,
        ETickGroup::PostPhysics,
        ETickGroup::PostUpdateWork,
    };
    for (auto g : groups)
        tick(g, dt);
}

// ---------------------------------------------------------------------------
// Processor pipeline (Stage 3-A)
// ---------------------------------------------------------------------------

void EcsWorld::register_processor(std::unique_ptr<IProcessor> p) {
    const std::string name = p->processor_name();
    for (auto& existing : m_processors) {
        if (existing->processor_name() == name) {
            p->initialize(m_registry);
            existing = std::move(p);
            return;
        }
    }
    p->initialize(m_registry);
    m_processors.push_back(std::move(p));
}

void EcsWorld::unregister_processor(const std::string& name) {
    m_processors.erase(
        std::remove_if(m_processors.begin(), m_processors.end(),
            [&](const std::unique_ptr<IProcessor>& p) {
                return p->processor_name() == name;
            }),
        m_processors.end());
}

void EcsWorld::tick_processors(ETickGroup group, float dt) {
    // Collect processors for this group
    std::vector<IProcessor*> group_procs;
    for (auto& p : m_processors)
        if (p->tick_group() == group)
            group_procs.push_back(p.get());

    if (!group_procs.empty()) {
        const int n = static_cast<int>(group_procs.size());

        // Build name → index map for dependency resolution
        std::unordered_map<std::string, int> name_to_idx;
        name_to_idx.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            name_to_idx[group_procs[i]->processor_name()] = i;

        // Kahn's topological sort: adj[i] = list of indices that must run AFTER i
        std::vector<int> in_degree(static_cast<size_t>(n), 0);
        std::vector<std::vector<int>> adj(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            for (const auto& dep_name : group_procs[i]->run_after()) {
                auto it = name_to_idx.find(dep_name);
                if (it != name_to_idx.end()) {
                    const int dep = it->second;
                    adj[static_cast<size_t>(dep)].push_back(i);
                    ++in_degree[static_cast<size_t>(i)];
                }
            }
        }

        std::queue<int> ready;
        for (int i = 0; i < n; ++i)
            if (in_degree[static_cast<size_t>(i)] == 0)
                ready.push(i);

        std::vector<IProcessor*> sorted;
        sorted.reserve(static_cast<size_t>(n));
        while (!ready.empty()) {
            const int idx = ready.front(); ready.pop();
            sorted.push_back(group_procs[static_cast<size_t>(idx)]);
            for (int next : adj[static_cast<size_t>(idx)]) {
                if (--in_degree[static_cast<size_t>(next)] == 0)
                    ready.push(next);
            }
        }

        // Graceful cycle handling: append any unsorted processors in original order
        if (static_cast<int>(sorted.size()) < n) {
            for (auto* p : group_procs) {
                bool found = false;
                for (auto* s : sorted)
                    if (s == p) { found = true; break; }
                if (!found) sorted.push_back(p);
            }
        }

        // Execute: batch consecutive can_tick_in_parallel processors via std::async
        std::vector<std::future<void>> futures;
        size_t i = 0;
        while (i < sorted.size()) {
            if (sorted[i]->can_tick_in_parallel()) {
                const size_t batch_start = i;
                while (i < sorted.size() && sorted[i]->can_tick_in_parallel())
                    ++i;
                for (size_t j = batch_start; j < i; ++j) {
                    IProcessor* proc = sorted[j];
                    futures.push_back(std::async(std::launch::async,
                        [proc, this, dt]() {
                            proc->execute(m_registry, dt);
                        }));
                }
                for (auto& f : futures) f.get();
                futures.clear();
            } else {
                sorted[i]->execute(m_registry, dt);
                ++i;
            }
        }
    }

    // Always flush deferred commands after all processors in this group finish
    m_command_buffer.flush(m_registry);
}

} // namespace sol
