#version 450

// Per-vertex input
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

// Passed to fragment shader
layout(location = 0) out vec2 fragUV;

// Push constant: tile transform + tint color (color used by fragment stage)
layout(push_constant) uniform PushConstants {
    vec2 offset;  // tile center in NDC (-1..1)
    vec2 scale;   // tile size in NDC
    vec4 color;   // tile tint color (RGBA), read by fragment shader
} pc;

void main() {
    gl_Position = vec4(inPosition * pc.scale + pc.offset, 0.0, 1.0);
    fragUV      = inUV;
}
