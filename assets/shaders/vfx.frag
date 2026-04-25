#version 450

// VFX fragment shader: textured quad with per-instance tint.
// Alpha-blended (via the pipeline's blend state) — the sprite's alpha
// channel plus the per-particle color.a drive transparency.

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D uTex;

void main() {
    vec4 sampled = texture(uTex, fragUV);
    outColor = sampled * fragColor;
}
