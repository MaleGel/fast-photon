#version 450

// Step B: vignette + ACES tone mapping.
//
// The HDR scene can hold values outside [0, 1] (floats), so we can't just
// sample and write — the swapchain is 8-bit per channel sRGB-style. Two
// things have to happen before we write:
//   1. Exposure scale (uniform multiplier on radiance).
//   2. Tone map HDR → LDR (ACES filmic curve — preserves highlights without
//      the hard clipping you'd get from simple min(c, 1)).
// Vignette is a post-tonemap multiplier: darker near the corners, unchanged
// in the middle. Gives the image a camera-lens feel.

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform PushConstants {
    float exposure;           // linear multiplier on the HDR sample (1.0 = unchanged)
    float vignetteRadius;     // distance from centre at which falloff starts
    float vignetteSoftness;   // width of the transition from full bright to full dark
    float vignetteIntensity;  // 0.0 disables, 1.0 full strength
} pc;

// Narkowicz 2015 ACES fit — a good balance of quality and single-line cost.
// Maps HDR (0..∞) to LDR (~0..1) with a filmic S-curve.
vec3 acesTonemap(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, fragUV).rgb * pc.exposure;
    vec3 ldr = acesTonemap(hdr);

    // Distance from screen centre, in UV space (so it's resolution-independent).
    // smoothstep(radius, radius + softness, dist) → 0 in bright centre,
    // 1 past the outer edge. Multiplied by intensity so the effect can be
    // scaled or turned off at runtime.
    float dist = distance(fragUV, vec2(0.5));
    float v = smoothstep(pc.vignetteRadius, pc.vignetteRadius + pc.vignetteSoftness, dist);
    ldr *= mix(1.0, 1.0 - v, pc.vignetteIntensity);

    outColor = vec4(ldr, 1.0);
}
