#pragma once
#include "sol/scene/node.h"
#include <string>

#ifndef miniaudio_h
typedef struct ma_sound ma_sound;
#endif

namespace sol {

// 2D non-positional audio player. Use for music, UI sounds, ambient loops.
// Does NOT inherit Node3D (has no spatial position).
class SOL_API AudioStreamPlayer : public Node {
public:
    std::string clip_path;         // path to audio file relative to project
    float       volume   = 1.0f;   // 0.0 - 1.0+
    float       pitch    = 1.0f;   // 1.0 = normal speed
    bool        loop     = false;
    bool        autoplay = false;
    std::string bus      = "SFX"; // "Master", "Music", "SFX", "UI"

    const char* type_name() const override { return "AudioStreamPlayer"; }

    void on_ready  (Engine& engine)                               override;
    void on_destroy(Engine& engine)                               override;

    void play();
    void stop();
    void pause();
    bool is_playing() const;

    // Apply volume/pitch/loop to the loaded sound (call after changing properties).
    void apply_params();

private:
    ma_sound* m_sound       = nullptr;
    bool      m_sound_valid = false;
};

} // namespace sol
