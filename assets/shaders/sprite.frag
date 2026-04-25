#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform PushConstants {
    vec2 worldOffset;
    vec2 worldSize;
    vec4 color;
    vec4 uvRect;
} pc;

void main() {
    vec2 atlasUV = mix(pc.uvRect.xy, pc.uvRect.zw, fragUV);
    vec4 sampled = texture(uTexture, atlasUV);
    outColor     = sampled * pc.color;
}
