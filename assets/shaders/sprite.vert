#version 450

// Unit quad: local [-0.5, 0.5] + [0, 1] UV (identical to grid's).
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

// World camera (rebound once per frame).
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 viewProjection;
} uFrame;

// Per-sprite transform + sampling info.
layout(push_constant) uniform PushConstants {
    vec2 worldOffset;   // sprite center in world units
    vec2 worldSize;     // sprite size in world units
    vec4 color;         // tint
    vec4 uvRect;        // (u0, v0, u1, v1) inside the bound texture
} pc;

void main() {
    vec2 worldPos = inPosition * pc.worldSize + pc.worldOffset;
    gl_Position   = uFrame.viewProjection * vec4(worldPos, 0.0, 1.0);
    fragUV        = inUV;
}
