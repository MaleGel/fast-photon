#pragma once
#include <glm/glm.hpp>

namespace engine {

struct Vec4 {
    float x, y, z, w;

    constexpr Vec4()                             : x(0), y(0), z(0), w(0) {}
    constexpr Vec4(float scalar)                 : x(scalar), y(scalar), z(scalar), w(scalar) {}
    constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    Vec4(const glm::vec4& v)   : x(v.x), y(v.y), z(v.z), w(v.w) {}
    operator glm::vec4() const { return { x, y, z, w }; }

    Vec4 operator+(const Vec4& o) const { return { x+o.x, y+o.y, z+o.z, w+o.w }; }
    Vec4 operator-(const Vec4& o) const { return { x-o.x, y-o.y, z-o.z, w-o.w }; }
    Vec4 operator*(float s)       const { return { x*s,   y*s,   z*s,   w*s   }; }
    Vec4 operator/(float s)       const { return { x/s,   y/s,   z/s,   w/s   }; }

    Vec4& operator+=(const Vec4& o) { x+=o.x; y+=o.y; z+=o.z; w+=o.w; return *this; }
    Vec4& operator*=(float s)       { x*=s;   y*=s;   z*=s;   w*=s;   return *this; }

    float*       data()       { return &x; }
    const float* data() const { return &x; }

    static constexpr Vec4 Zero() { return { 0, 0, 0, 0 }; }
    static constexpr Vec4 One()  { return { 1, 1, 1, 1 }; }
};

} // namespace engine