#version 450

// Per-vertex input: local-space tile position + quad-local UV.
layout(location = 0) in vec2 inPosition;   // in [-0.5, 0.5]
layout(location = 1) in vec2 inUV;         // in [0, 1]

// Passed to fragment shader
layout(location = 0) out vec2 fragUV;

// Frame-level uniforms (camera). set=0 rebinds once per frame.
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 viewProjection;
} uFrame;

// Per-draw tile data. GLSL requires identical block layout between stages
// — fragment reads color / uvRect from here too.
layout(push_constant) uniform PushConstants {
    vec2 worldOffset;  // tile center in world units
    vec2 worldSize;    // tile size in world units
    vec4 color;
    vec4 uvRect;
} pc;

void main() {
    // Local [-0.5,0.5] → world coordinates, then apply camera VP.
    vec2 worldPos = inPosition * pc.worldSize + pc.worldOffset;
    gl_Position   = uFrame.viewProjection * vec4(worldPos, 0.0, 1.0);
    fragUV        = inUV;
}
