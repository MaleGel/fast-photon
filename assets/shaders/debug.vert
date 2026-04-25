#version 450

// Per-vertex: world position + flat RGBA color.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

// World camera (shared with grid/sprite).
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 viewProjection;
} uFrame;

void main() {
    gl_Position = uFrame.viewProjection * vec4(inPosition, 0.0, 1.0);
    fragColor   = inColor;
}
