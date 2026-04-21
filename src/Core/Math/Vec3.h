#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

namespace engine {

struct Vec3 {
    float x, y, z;

    constexpr Vec3()                          : x(0), y(0), z(0) {}
    constexpr Vec3(float scalar)              : x(scalar), y(scalar), z(scalar) {}
    constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    // Convertation from/to GLM
    Vec3(const glm::vec3& v)       : x(v.x), y(v.y), z(v.z) {}
    operator glm::vec3() const     { return { x, y, z }; }

    Vec3 operator+(const Vec3& o) const { return { x+o.x, y+o.y, z+o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x-o.x, y-o.y, z-o.z }; }
    Vec3 operator*(float s)       const { return { x*s,   y*s,   z*s   }; }
    Vec3 operator/(float s)       const { return { x/s,   y/s,   z/s   }; }
    Vec3 operator-()              const { return { -x,    -y,    -z    }; }

    Vec3& operator+=(const Vec3& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x-=o.x; y-=o.y; z-=o.z; return *this; }
    Vec3& operator*=(float s)       { x*=s;   y*=s;   z*=s;   return *this; }
    Vec3& operator/=(float s)       { x/=s;   y/=s;   z/=s;   return *this; }

    bool operator==(const Vec3& o) const { return x==o.x && y==o.y && z==o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }

    float     length()              const { return glm::length(glm::vec3(*this)); }
    float     lengthSq()            const { return x*x + y*y + z*z; }
    Vec3      normalized()          const { return glm::normalize(glm::vec3(*this)); }
    float     dot(const Vec3& o)    const { return glm::dot(glm::vec3(*this), glm::vec3(o)); }
    Vec3      cross(const Vec3& o)  const { return glm::cross(glm::vec3(*this), glm::vec3(o)); }

    float*       data()       { return &x; }
    const float* data() const { return &x; }

    std::string toString() const {
        return "Vec3(" + std::to_string(x) + ", " +
                         std::to_string(y) + ", " +
                         std::to_string(z) + ")";
    }

    static constexpr Vec3 Zero()    { return {  0,  0,  0 }; }
    static constexpr Vec3 One()     { return {  1,  1,  1 }; }
    static constexpr Vec3 Up()      { return {  0,  1,  0 }; }
    static constexpr Vec3 Down()    { return {  0, -1,  0 }; }
    static constexpr Vec3 Forward() { return {  0,  0, -1 }; }
    static constexpr Vec3 Back()    { return {  0,  0,  1 }; }
    static constexpr Vec3 Right()   { return {  1,  0,  0 }; }
    static constexpr Vec3 Left()    { return { -1,  0,  0 }; }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

} // namespace engine