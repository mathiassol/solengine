#pragma once

#include "sol/export.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace sol {

// POD camera. Compute view/proj on demand; nothing GPU-side here.
struct SOL_API Camera {
    glm::vec3 position{0.0f, 5.0f, 10.0f};
    glm::vec3 target  {0.0f, 0.0f,  0.0f};
    glm::vec3 up      {0.0f, 1.0f,  0.0f};

    float fov_y_radians = glm::radians(60.0f);
    float near_plane    = 0.1f;
    float far_plane     = 1000.0f;

    glm::mat4 view() const {
        return glm::lookAt(position, target, up);
    }

    // homogeneous_depth: pass true for OpenGL/WebGL (clip -1..1), false for D3D/Vulkan/Metal (0..1).
    glm::mat4 proj(float aspect, bool homogeneous_depth) const {
        glm::mat4 p = glm::perspective(fov_y_radians, aspect, near_plane, far_plane);
        if (!homogeneous_depth) {
            // Convert right-handed GL [-1,1] to right-handed D3D [0,1] depth range.
            // RH_NO: p[2][2]=-(f+n)/(f-n),  p[3][2]=-2fn/(f-n)
            // RH_ZO: p[2][2]=-f/(f-n),       p[3][2]=-nf/(f-n)
            const float f = far_plane, n = near_plane;
            p[2][2] = -f / (f - n);
            p[3][2] = -(n * f) / (f - n);
        }
        return p;
    }
};

} // namespace sol
