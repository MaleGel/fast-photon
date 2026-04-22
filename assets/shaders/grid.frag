#version 450

layout(location = 0) in  vec2 fragUV;   // quad-local UV in [0,1]
layout(location = 0) out vec4 outColor;

// One sampled texture at (set=0, binding=0) — either the whole raw PNG
// or a multi-sprite atlas page, depending on the asset mode.
layout(set = 0, binding = 0) uniform sampler2D uTexture;

// Same block as in grid.vert — GLSL requires identical layouts.
layout(push_constant) uniform PushConstants {
    vec2 offset;
    vec2 scale;
    vec4 color;
    vec4 uvRect;   // (u0, v0, u1, v1)
} pc;

void main() {
    // Map quad-local UV [0,1] → sub-rect within the full texture.
    vec2 atlasUV = mix(pc.uvRect.xy, pc.uvRect.zw, fragUV);
    vec4 sampled = texture(uTexture, atlasUV);
    outColor     = sampled * pc.color;
}
