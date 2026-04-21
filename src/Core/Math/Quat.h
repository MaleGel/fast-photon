#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Vec3.h"
#include "Mat4.h"

namespace engine {

struct Mat4;

struct Quat {
    float x, y, z, w;

    constexpr Quat()                             : x(0), y(0), z(0), w(1) {}
    constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    Quat(const glm::quat& q)   : x(q.x), y(q.y), z(q.z), w(q.w) {}
    operator glm::quat() const { return { w, x, y, z }; }

    Quat operator*(const Quat& o) const { return glm::quat(*this) * glm::quat(o); }
    Vec3 operator*(const Vec3& v) const { return glm::rotate(glm::quat(*this), glm::vec3(v)); }

    Quat  normalized()           const { return glm::normalize(glm::quat(*this)); }
    Quat  conjugate()            const { return glm::conjugate(glm::quat(*this)); }
    Vec3  eulerAngles()          const { return glm::degrees(glm::eulerAngles(glm::quat(*this))); }

    static Quat identity()                              { return {}; }
    static Quat fromAxisAngle(const Vec3& axis, float degrees) {
        return glm::angleAxis(glm::radians(degrees), glm::vec3(axis));
    }
    static Quat fromEuler(const Vec3& degrees) {
        return glm::quat(glm::radians(glm::vec3(degrees)));
    }
    static Quat lookAt(const Vec3& from, const Vec3& to, const Vec3& up = Vec3::Up()) {
        glm::vec3 dir = glm::normalize(glm::vec3(to) - glm::vec3(from));
        return glm::quatLookAt(dir, glm::vec3(up));
    }
    static Quat slerp(const Quat& a, const Quat& b, float t) {
        return glm::slerp(glm::quat(a), glm::quat(b), t);
    }
};

} // namespace engine