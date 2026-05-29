#pragma once
#include "sol/export.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

// Forward declare miniaudio types — skipped if miniaudio.h is already included
#ifndef miniaudio_h
typedef struct ma_engine ma_engine;
typedef struct ma_sound_group ma_sound_group;
#endif

namespace sol {

// Named audio buses. All named buses route through Master.
// Bus names: "Master", "Music", "SFX", "UI"
class SOL_API AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    void shutdown();

    // Called each frame to update the listener position/orientation.
    // Pass Camera3D world position, forward, and up vectors.
    void update_listener(const glm::vec3& pos,
                         const glm::vec3& forward,
                         const glm::vec3& up);

    // Volume control (0.0 - 1.0+)
    void  set_master_volume(float v);
    float get_master_volume() const;

    void  set_bus_volume(const std::string& bus_name, float v);
    float get_bus_volume(const std::string& bus_name) const;

    // Get bus group by name — returns nullptr for unknown names.
    // AudioStreamPlayer uses this to route sound to the right bus.
    ma_sound_group* get_bus(const std::string& bus_name);

    ma_engine* native() { return m_engine; }

    // Fire-and-forget one-shot playback (no persistent sound object needed).
    // Uses miniaudio's resource manager cache — repeated calls to the same path are fast.
    void play_oneshot(const std::string& path, const std::string& bus = "SFX");

    bool is_initialized() const { return m_initialized; }

private:
    ma_engine*      m_engine      = nullptr;
    ma_sound_group* m_bus_master  = nullptr;
    ma_sound_group* m_bus_music   = nullptr;
    ma_sound_group* m_bus_sfx     = nullptr;
    ma_sound_group* m_bus_ui      = nullptr;
    bool            m_initialized = false;
};

} // namespace sol
