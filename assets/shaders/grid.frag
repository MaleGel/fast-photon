#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

// One sampled texture at (set=0, binding=0).
layout(set = 0, binding = 0) uniform sampler2D uTexture;

// Same push-constant block as in the vertex shader — we read 'color' here.
// Layout must match exactly (GLSL spec) even though we only use one field.
layout(push_constant) uniform PushConstants {
    vec2 offset;
    vec2 scale;
    vec4 color;
} pc;

void main() {
    vec4 sampled = texture(uTexture, fragUV);
    outColor     = sampled * pc.color;  // tint the tile texture
}
