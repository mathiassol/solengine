#pragma once
#include "sol/scene/node.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace sol {

// Node with a 3D transform (position, rotation in degrees YXZ, scale).
// All spatial nodes inherit from this.
class SOL_API Node3D : public Node {
public:
    glm::vec3 position {0.0f, 0.0f,  0.0f};
    glm::vec3 rotation {0.0f, 0.0f,  0.0f};  // degrees, applied YXZ
    glm::vec3 scale    {1.0f, 1.0f,  1.0f};
    bool visible = true;

    const char* type_name() const override { return "Node3D"; }

    // Local transform (T * Ry * Rx * Rz * S)
    glm::mat4 local_transform() const {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
        glm::mat4 ry = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.y), {0,1,0});
        glm::mat4 rx = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.x), {1,0,0});
        glm::mat4 rz = glm::rotate(glm::mat4(1.0f), glm::radians(rotation.z), {0,0,1});
        glm::mat4 s  = glm::scale(glm::mat4(1.0f), scale);
        return t * ry * rx * rz * s;
    }

    // Accumulated world transform (walks up parent chain)
    glm::mat4 global_transform() const {
        glm::mat4 local = local_transform();
        if (parent()) {
            if (auto* p3d = dynamic_cast<const Node3D*>(parent()))
                return p3d->global_transform() * local;
        }
        return local;
    }

    // Direction vectors in world space
    glm::vec3 forward() const { return glm::normalize(glm::vec3(global_transform() * glm::vec4(0,0,-1,0))); }
    glm::vec3 right()   const { return glm::normalize(glm::vec3(global_transform() * glm::vec4(1,0, 0,0))); }
    glm::vec3 up()      const { return glm::normalize(glm::vec3(global_transform() * glm::vec4(0,1, 0,0))); }
};

} // namespace sol
