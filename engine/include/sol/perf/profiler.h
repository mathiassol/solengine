#pragma once
#include "sol/export.h"
#include <string>
#include <unordered_map>
#include <chrono>
#include <vector>

namespace sol {

struct SOL_API ProfilerSample {
    std::string name;
    double      ms     = 0.0;   // last frame duration
    double      ms_avg = 0.0;   // rolling average (~64 frames)
};

// Simple CPU profiler — call begin/end around a named scope.
// All data is per-frame; reset() clears the accumulator (rolling average persists).
class SOL_API Profiler {
public:
    static Profiler& instance();

    void begin(const char* name);
    void end  (const char* name);

    // Returns all samples recorded since last reset().
    const std::vector<ProfilerSample>& samples() const { return m_samples; }

    // Call at the start of each frame. Clears open samples and the sample list.
    void reset();

private:
    struct OpenSample {
        std::chrono::high_resolution_clock::time_point start;
    };
    std::unordered_map<std::string, OpenSample> m_open;
    std::unordered_map<std::string, double>     m_rolling;    // rolling average value
    std::unordered_map<std::string, int>        m_roll_count; // samples counted so far
    std::vector<ProfilerSample>                 m_samples;
};

// RAII scope guard
struct SOL_API ProfileScope {
    const char* name;
    explicit ProfileScope(const char* n) : name(n) { Profiler::instance().begin(n); }
    ~ProfileScope()                                 { Profiler::instance().end(name); }
};

} // namespace sol

#define SOL_PROFILE_SCOPE(name) ::sol::ProfileScope _prof_scope_##__LINE__{ name }
#define SOL_PROFILE_BEGIN(name) ::sol::Profiler::instance().begin(name)
#define SOL_PROFILE_END(name)   ::sol::Profiler::instance().end(name)
