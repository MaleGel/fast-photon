#version 450

layout(location = 0) in  vec2 fragUV;
layout(location = 0) out vec4 outColor;

// Font atlas — RGBA where rgb=1 and alpha=glyph coverage.
layout(set = 1, binding = 0) uniform sampler2D uAtlas;

layout(push_constant) uniform PushConstants {
    vec2 posPx;
    vec2 sizePx;
    vec4 color;
    vec4 uvRect;
} pc;

void main() {
    vec2 atlasUV = mix(pc.uvRect.xy, pc.uvRect.zw, fragUV);
    float coverage = texture(uAtlas, atlasUV).a;
    outColor = vec4(pc.color.rgb, pc.color.a * coverage);
}
