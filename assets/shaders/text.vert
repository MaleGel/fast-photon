#version 450

// Unit quad [-0.5, 0.5] + [0, 1] UV, same buffer as the grid renderer.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;

// Frame-level orthographic projection (screen-pixel space → clip space).
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 screenProjection;
} uFrame;

// Per-glyph placement + sampling info.
layout(push_constant) uniform PushConstants {
    vec2 posPx;     // top-left in pixels
    vec2 sizePx;    // glyph size in pixels
    vec4 color;
    vec4 uvRect;    // (u0, v0, u1, v1) inside the font atlas
} pc;

void main() {
    // Unit quad [-0.5..0.5] → [0..1] → glyph rect in pixels.
    vec2 local = inPosition + vec2(0.5);
    vec2 screenPx = pc.posPx + local * pc.sizePx;

    gl_Position = uFrame.screenProjection * vec4(screenPx, 0.0, 1.0);
    fragUV      = inUV;
}
