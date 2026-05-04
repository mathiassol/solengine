#pragma once
#include <memory>

namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
}

namespace sol {

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    bool init();
    void shutdown();
    void step(float dt);

    JPH::PhysicsSystem* system() const { return m_system.get(); }

private:
    std::unique_ptr<JPH::TempAllocatorImpl>   m_temp;
    std::unique_ptr<JPH::JobSystemThreadPool> m_jobs;
    std::unique_ptr<JPH::PhysicsSystem>       m_system;
    bool m_initialized = false;
};

} // namespace sol
