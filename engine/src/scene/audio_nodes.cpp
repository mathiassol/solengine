// Include miniaudio.h WITHOUT MINIAUDIO_IMPLEMENTATION — implementation is in audio_engine.cpp
#include <miniaudio.h>

#include "sol/scene/audio_stream_player.h"
#include "sol/scene/audio_stream_player3d.h"
#include "sol/audio/audio_engine.h"
#include "sol/engine.h"
#include "sol/log.h"

#include <filesystem>

namespace sol {

// Helper to resolve a path relative to the project directory.
static std::string resolve_audio_path(const std::string& clip_path) {
    if (clip_path.empty()) return {};
    if (std::filesystem::path(clip_path).is_absolute()) return clip_path;
    auto p = std::filesystem::current_path() / clip_path;
    if (std::filesystem::exists(p)) return p.string();
    return clip_path; // fallback, let miniaudio try
}

// ============================================================
//  AudioStreamPlayer  (2D non-positional)
// ============================================================

void AudioStreamPlayer::on_ready(Engine& engine) {
    if (clip_path.empty()) return;
    if (!engine.audio().is_initialized()) return;

    auto* ae = engine.audio().native();
    const std::string& bus_name = bus.empty() ? std::string("SFX") : bus;
    auto* bus_grp = engine.audio().get_bus(bus_name);

    std::string resolved = resolve_audio_path(clip_path);

    m_sound = new ma_sound{};
    ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_result result = ma_sound_init_from_file(ae, resolved.c_str(), flags, bus_grp, nullptr, m_sound);
    if (result != MA_SUCCESS) {
        SOL_ERROR("AudioStreamPlayer '" + name + "': failed to load '" + resolved
                  + "' (error " + std::to_string(result) + ")");
        delete m_sound;
        m_sound = nullptr;
        m_sound_valid = false;
        return;
    }
    m_sound_valid = true;
    apply_params();

    if (autoplay && !engine.is_editor_mode())
        play();
}

void AudioStreamPlayer::on_destroy(Engine& /*engine*/) {
    if (m_sound_valid && m_sound) {
        ma_sound_uninit(m_sound);
        m_sound_valid = false;
    }
    delete m_sound;
    m_sound = nullptr;
}

void AudioStreamPlayer::play() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_seek_to_pcm_frame(m_sound, 0);
    ma_sound_start(m_sound);
}

void AudioStreamPlayer::stop() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_stop(m_sound);
    ma_sound_seek_to_pcm_frame(m_sound, 0);
}

void AudioStreamPlayer::pause() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_stop(m_sound);
}

bool AudioStreamPlayer::is_playing() const {
    if (!m_sound_valid || !m_sound) return false;
    return ma_sound_is_playing(m_sound) == MA_TRUE;
}

void AudioStreamPlayer::apply_params() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_set_volume(m_sound, volume);
    ma_sound_set_pitch(m_sound, pitch);
    ma_sound_set_looping(m_sound, loop ? MA_TRUE : MA_FALSE);
}

// ============================================================
//  AudioStreamPlayer3D  (3D positional)
// ============================================================

void AudioStreamPlayer3D::on_ready(Engine& engine) {
    if (clip_path.empty()) return;
    if (!engine.audio().is_initialized()) return;

    auto* ae = engine.audio().native();
    const std::string& bus_name = bus.empty() ? std::string("SFX") : bus;
    auto* bus_grp = engine.audio().get_bus(bus_name);

    std::string resolved = resolve_audio_path(clip_path);

    m_sound = new ma_sound{};
    // No MA_SOUND_FLAG_NO_SPATIALIZATION — we want 3D audio
    ma_result result = ma_sound_init_from_file(ae, resolved.c_str(), 0, bus_grp, nullptr, m_sound);
    if (result != MA_SUCCESS) {
        SOL_ERROR("AudioStreamPlayer3D '" + name + "': failed to load '" + resolved
                  + "' (error " + std::to_string(result) + ")");
        delete m_sound;
        m_sound = nullptr;
        m_sound_valid = false;
        return;
    }
    m_sound_valid = true;

    // Set initial position
    ma_sound_set_position(m_sound, position.x, position.y, position.z);
    apply_params();

    if (autoplay && !engine.is_editor_mode())
        play();
}

void AudioStreamPlayer3D::on_update(Engine& /*engine*/, float /*dt*/) {
    if (m_sound_valid && m_sound)
        ma_sound_set_position(m_sound, position.x, position.y, position.z);
}

void AudioStreamPlayer3D::on_destroy(Engine& /*engine*/) {
    if (m_sound_valid && m_sound) {
        ma_sound_uninit(m_sound);
        m_sound_valid = false;
    }
    delete m_sound;
    m_sound = nullptr;
}

void AudioStreamPlayer3D::play() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_seek_to_pcm_frame(m_sound, 0);
    ma_sound_start(m_sound);
}

void AudioStreamPlayer3D::stop() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_stop(m_sound);
    ma_sound_seek_to_pcm_frame(m_sound, 0);
}

void AudioStreamPlayer3D::pause() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_stop(m_sound);
}

bool AudioStreamPlayer3D::is_playing() const {
    if (!m_sound_valid || !m_sound) return false;
    return ma_sound_is_playing(m_sound) == MA_TRUE;
}

void AudioStreamPlayer3D::apply_params() {
    if (!m_sound_valid || !m_sound) return;
    ma_sound_set_volume(m_sound, volume);
    ma_sound_set_pitch(m_sound, pitch);
    ma_sound_set_looping(m_sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_max_distance(m_sound, max_distance);
    ma_sound_set_rolloff(m_sound, attenuation);
    ma_sound_set_attenuation_model(m_sound, ma_attenuation_model_inverse);
}

} // namespace sol
