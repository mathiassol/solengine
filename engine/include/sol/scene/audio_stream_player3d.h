#pragma once
#include "sol/scene/node3d.h"
#include <string>

#ifndef miniaudio_h
typedef struct ma_sound ma_sound;
#endif

namespace sol {

// 3D positional audio player. Inherits Node3D so it has position/rotation/scale.
// Position is updated each frame for spatial attenuation.
class SOL_API AudioStreamPlayer3D : public Node3D {
public:
    std::string clip_path;               // path to audio file relative to project
    float       volume       = 1.0f;
    float       pitch        = 1.0f;
    bool        loop         = false;
    bool        autoplay     = false;
    std::string bus          = "SFX";
    float       max_distance = 20.0f;   // beyond this distance, volume is 0
    float       attenuation  = 1.0f;    // rolloff factor (higher = faster falloff)

    const char* type_name() const override { return "AudioStreamPlayer3D"; }

    void on_ready  (Engine& engine)                               override;
    void on_update (Engine& engine, float dt)                     override;
    void on_destroy(Engine& engine)                               override;

    void play();
    void stop();
    void pause();
    bool is_playing() const;

    void apply_params();

private:
    ma_sound* m_sound       = nullptr;
    bool      m_sound_valid = false;
};

} // namespace sol
