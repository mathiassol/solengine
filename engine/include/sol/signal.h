#pragma once
#include <functional>
#include <vector>
#include <algorithm>

namespace sol {

template<typename... Args>
class Signal {
public:
    using Callback = std::function<void(Args...)>;
    using Handle   = int;

    Handle connect(Callback cb) {
        int h = m_next_handle++;
        m_slots.push_back({h, std::move(cb)});
        return h;
    }

    void disconnect(Handle h) {
        m_slots.erase(
            std::remove_if(m_slots.begin(), m_slots.end(),
                [h](const Slot& s){ return s.handle == h; }),
            m_slots.end());
    }

    void disconnect_all() { m_slots.clear(); }

    void emit(Args... args) const {
        for (const auto& s : m_slots)
            s.callback(args...);
    }

    bool empty() const { return m_slots.empty(); }

private:
    struct Slot { Handle handle; Callback callback; };
    std::vector<Slot> m_slots;
    int m_next_handle = 1;
};

} // namespace sol
