#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Vec3.h"
#include "Vec4.h"

namespace engine {

struct Quat;

struct Mat4 {
    glm::mat4 m;

    Mat4()                   : m(1.0f) {}
    Mat4(const glm::mat4& v) : m(v) {}
    operator glm::mat4() const { return m; }

    Mat4 operator*(const Mat4& o) const { return m * o.m; }
    Vec4 operator*(const Vec4& v) const { return m * glm::vec4(v); }

    Mat4        transposed()  const { return glm::transpose(m); }
    Mat4        inversed()    const { return glm::inverse(m); }
    const float* data()       const { return glm::value_ptr(m); }

    static Mat4 identity()    { return {}; }

    static Mat4 translation(const Vec3& t) {
        return glm::translate(glm::mat4(1.0f), glm::vec3(t));
    }
    static Mat4 rotation(const Quat& q);

    static Mat4 scale(const Vec3& s) {
        return glm::scale(glm::mat4(1.0f), glm::vec3(s));
    }
    static Mat4 perspective(float fovDeg, float aspect, float near, float far) {
        return glm::perspective(glm::radians(fovDeg), aspect, near, far);
    }
    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
        return glm::ortho(left, right, bottom, top, near, far);
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        return glm::lookAt(glm::vec3(eye), glm::vec3(center), glm::vec3(up));
    }
    static Mat4 TRS(const Vec3& t, const Quat& r, const Vec3& s);
};

} // namespace engine