#pragma once
#include <glm/glm.hpp>
#include <string>

namespace engine {

struct Vec2 {
    float x, y;

    constexpr Vec2()                    : x(0), y(0) {}
    constexpr Vec2(float scalar)        : x(scalar), y(scalar) {}
    constexpr Vec2(float x, float y)    : x(x), y(y) {}

    Vec2(const glm::vec2& v)   : x(v.x), y(v.y) {}
    operator glm::vec2() const { return { x, y }; }

    Vec2 operator+(const Vec2& o) const { return { x+o.x, y+o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x-o.x, y-o.y }; }
    Vec2 operator*(float s)       const { return { x*s,   y*s   }; }
    Vec2 operator/(float s)       const { return { x/s,   y/s   }; }
    Vec2 operator-()              const { return { -x,    -y    }; }

    Vec2& operator+=(const Vec2& o) { x+=o.x; y+=o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x-=o.x; y-=o.y; return *this; }
    Vec2& operator*=(float s)       { x*=s;   y*=s;   return *this; }

    bool operator==(const Vec2& o) const { return x==o.x && y==o.y; }

    float  length()           const { return glm::length(glm::vec2(*this)); }
    float  lengthSq()         const { return x*x + y*y; }
    Vec2   normalized()       const { return glm::normalize(glm::vec2(*this)); }
    float  dot(const Vec2& o) const { return glm::dot(glm::vec2(*this), glm::vec2(o)); }

    std::string toString() const {
        return "Vec2(" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }

    static constexpr Vec2 Zero()  { return { 0, 0 }; }
    static constexpr Vec2 One()   { return { 1, 1 }; }
    static constexpr Vec2 Right() { return { 1, 0 }; }
    static constexpr Vec2 Up()    { return { 0, 1 }; }
};

inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

} // namespace engine