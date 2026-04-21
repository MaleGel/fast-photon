#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace engine::Math {

constexpr float PI      = 3.14159265358979323846f;
constexpr float TWO_PI  = PI * 2.0f;
constexpr float HALF_PI = PI * 0.5f;
constexpr float DEG2RAD = PI / 180.0f;
constexpr float RAD2DEG = 180.0f / PI;
constexpr float EPSILON = 1e-6f;

inline float toRadians(float degrees) { return degrees * DEG2RAD; }
inline float toDegrees(float radians) { return radians * RAD2DEG; }

inline float clamp(float v, float lo, float hi) { return std::clamp(v, lo, hi); }
inline float clamp01(float v)                   { return clamp(v, 0.0f, 1.0f); }

inline float lerp(float a, float b, float t)    { return a + (b - a) * t; }
inline float smoothstep(float a, float b, float t) {
    t = clamp01((t - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

inline float sign(float v)   { return v >= 0.0f ? 1.0f : -1.0f; }
inline float abs(float v)    { return std::abs(v); }
inline float sqrt(float v)   { return std::sqrt(v); }
inline float pow(float b, float e) { return std::pow(b, e); }

inline bool  nearlyEqual(float a, float b, float eps = EPSILON) {
    return std::abs(a - b) < eps;
}
inline bool  nearlyZero(float v, float eps = EPSILON) {
    return std::abs(v) < eps;
}

} // namespace engine::Math