#pragma once
#include "Vec3.h"
#include "Quat.h"
#include "Mat4.h"

namespace engine {

struct Transform {
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::identity();
    Vec3 scale    = Vec3::One();

    Vec3 forward() const { return rotation * Vec3::Forward(); }
    Vec3 right()   const { return rotation * Vec3::Right();   }
    Vec3 up()      const { return rotation * Vec3::Up();      }

    Mat4 toMatrix() const {
        return Mat4::translation(position)
             * Mat4(glm::mat4_cast(glm::quat(rotation)))
             * Mat4::scale(scale);
    }

    void translate(const Vec3& delta)           { position += delta; }
    void rotate(const Vec3& axis, float degrees) {
        rotation = (Quat::fromAxisAngle(axis, degrees) * rotation).normalized();
    }
    void lookAt(const Vec3& target) {
        rotation = Quat::lookAt(position, target);
    }
};

} // namespace engine