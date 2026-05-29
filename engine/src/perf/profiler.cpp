#include "sol/perf/profiler.h"

namespace sol {

Profiler& Profiler::instance() {
    static Profiler p;
    return p;
}

void Profiler::begin(const char* name) {
    m_open[name].start = std::chrono::high_resolution_clock::now();
}

void Profiler::end(const char* name) {
    auto it = m_open.find(name);
    if (it == m_open.end()) return;

    const auto now = std::chrono::high_resolution_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(
        now - it->second.start).count();
    m_open.erase(it);

    // Update rolling average (exponential moving average, ~64-sample window)
    auto& roll = m_rolling[name];
    auto& cnt  = m_roll_count[name];
    if (cnt < 64) {
        roll = (roll * static_cast<double>(cnt) + ms) / static_cast<double>(cnt + 1);
        ++cnt;
    } else {
        roll = roll * (63.0 / 64.0) + ms * (1.0 / 64.0);
    }

    // Update existing sample or append a new one
    for (auto& s : m_samples) {
        if (s.name == name) {
            s.ms     = ms;
            s.ms_avg = roll;
            return;
        }
    }
    m_samples.push_back({ name, ms, roll });
}

void Profiler::reset() {
    m_open.clear();
    m_samples.clear();
    // m_rolling / m_roll_count intentionally survive reset — they accumulate
    // across frames to produce a stable rolling average.
}

} // namespace sol
