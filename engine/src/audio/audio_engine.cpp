// stb_vorbis defines single-letter macros (L, C, R) for channel layout tables
// that collide with Windows thread-pool typedefs inside <threadpoolapiset.h>.
// Pulling in <windows.h> first ensures all Win32 types are already resolved
// before those macros exist; then we clean them up immediately after.
#ifdef _WIN32
#include <windows.h>
#endif
#include "extras/stb_vorbis.c"
// Clean up stb_vorbis's polluting single-letter macros before miniaudio
// includes any Windows headers.
#ifdef L
#undef L
#endif
#ifdef C
#undef C
#endif
#ifdef R
#undef R
#endif

// IMPORTANT: Define MINIAUDIO_IMPLEMENTATION exactly once.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "sol/audio/audio_engine.h"
#include "sol/log.h"
#include <cstring>
#include <filesystem>

namespace sol {

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
    if (m_initialized) return true;

    m_engine = new ma_engine{};
    if (ma_engine_init(nullptr, m_engine) != MA_SUCCESS) {
        SOL_ERROR("AudioEngine: failed to init ma_engine");
        delete m_engine;
        m_engine = nullptr;
        return false;
    }

    // Create bus groups: Master, then Music/SFX/UI routing through Master.
    m_bus_master = new ma_sound_group{};
    m_bus_music  = new ma_sound_group{};
    m_bus_sfx    = new ma_sound_group{};
    m_bus_ui     = new ma_sound_group{};

    if (ma_sound_group_init(m_engine, 0, nullptr,      m_bus_master) != MA_SUCCESS ||
        ma_sound_group_init(m_engine, 0, m_bus_master, m_bus_music)  != MA_SUCCESS ||
        ma_sound_group_init(m_engine, 0, m_bus_master, m_bus_sfx)    != MA_SUCCESS ||
        ma_sound_group_init(m_engine, 0, m_bus_master, m_bus_ui)     != MA_SUCCESS)
    {
        SOL_ERROR("AudioEngine: failed to create bus groups");
        shutdown();
        return false;
    }

    m_initialized = true;
    SOL_INFO("AudioEngine initialized (miniaudio)");
    return true;
}

void AudioEngine::shutdown() {
    if (!m_initialized && !m_engine) return;

    if (m_bus_ui)     { ma_sound_group_uninit(m_bus_ui);     delete m_bus_ui;     m_bus_ui     = nullptr; }
    if (m_bus_sfx)    { ma_sound_group_uninit(m_bus_sfx);    delete m_bus_sfx;    m_bus_sfx    = nullptr; }
    if (m_bus_music)  { ma_sound_group_uninit(m_bus_music);  delete m_bus_music;  m_bus_music  = nullptr; }
    if (m_bus_master) { ma_sound_group_uninit(m_bus_master); delete m_bus_master; m_bus_master = nullptr; }

    if (m_engine) {
        ma_engine_uninit(m_engine);
        delete m_engine;
        m_engine = nullptr;
    }
    m_initialized = false;
}

void AudioEngine::update_listener(const glm::vec3& pos,
                                   const glm::vec3& forward,
                                   const glm::vec3& up) {
    if (!m_initialized) return;
    ma_engine_listener_set_position (m_engine, 0,  pos.x,     pos.y,     pos.z);
    ma_engine_listener_set_direction(m_engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up (m_engine, 0,  up.x,      up.y,      up.z);
}

void AudioEngine::set_master_volume(float v) {
    if (m_bus_master) ma_sound_group_set_volume(m_bus_master, v);
}

float AudioEngine::get_master_volume() const {
    if (!m_bus_master) return 1.0f;
    return ma_sound_group_get_volume(m_bus_master);
}

void AudioEngine::set_bus_volume(const std::string& bus_name, float v) {
    if (auto* g = get_bus(bus_name))
        ma_sound_group_set_volume(g, v);
}

float AudioEngine::get_bus_volume(const std::string& bus_name) const {
    ma_sound_group* g = const_cast<AudioEngine*>(this)->get_bus(bus_name);
    if (!g) return 1.0f;
    return ma_sound_group_get_volume(g);
}

ma_sound_group* AudioEngine::get_bus(const std::string& bus_name) {
    if (bus_name == "Master") return m_bus_master;
    if (bus_name == "Music")  return m_bus_music;
    if (bus_name == "SFX")    return m_bus_sfx;
    if (bus_name == "UI")     return m_bus_ui;
    return m_bus_sfx; // fallback
}

void AudioEngine::play_oneshot(const std::string& path, const std::string& bus_name) {
    if (!m_initialized || !m_engine) return;
    namespace fs = std::filesystem;
    std::string resolved = fs::path(path).is_absolute()
        ? path
        : (fs::current_path() / path).string();
    auto* grp = get_bus(bus_name);
    ma_result r = ma_engine_play_sound(m_engine, resolved.c_str(), grp);
    if (r != MA_SUCCESS) {
        // Retry without bus group — some miniaudio builds require nullptr group
        r = ma_engine_play_sound(m_engine, resolved.c_str(), nullptr);
    }
    if (r != MA_SUCCESS)
        SOL_WARN("AudioEngine::play_oneshot failed (err=" + std::to_string(r)
                 + ") path='" + resolved + "'");
}

} // namespace sol
