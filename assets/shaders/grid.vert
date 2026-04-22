#version 450

// Per-vertex input
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;   // quad-local UV in [0,1]

// Passed to fragment shader
layout(location = 0) out vec2 fragUV;

// Push constants — shared with grid.frag. Layout must match GridPushConstants in C++.
layout(push_constant) uniform PushConstants {
    vec2 offset;  // tile center in NDC
    vec2 scale;   // tile size in NDC
    vec4 color;   // tint (fragment stage)
    vec4 uvRect;  // (u0, v0, u1, v1) sub-rect inside the bound texture (fragment stage)
} pc;

void main() {
    gl_Position = vec4(inPosition * pc.scale + pc.offset, 0.0, 1.0);
    fragUV      = inUV;
}
