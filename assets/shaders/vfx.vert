#version 450

// Instanced billboard vertex shader for VFX particles.
//
// The vertex buffer is the same unit quad used by SpriteRenderer
// (vec2 pos in [-0.5,0.5] + vec2 uv in [0,1]). Per-instance data is
// pulled directly from the particle pool by gl_InstanceIndex, so we
// can draw 'capacity' instances and let the shader collapse dead ones
// to zero size.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

struct Particle {
    vec2 position;
    vec2 velocity;
    vec4 colorStart;
    vec4 colorEnd;
    float lifetime;
    float timeAlive;
    float sizeStart;
    float sizeEnd;
    vec2 gravity;
    uint alive;
    uint _pad0;
};

layout(set = 0, binding = 0, std430) readonly buffer Pool { Particle p[]; } pool;

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProjection;
} frame;

layout(push_constant) uniform PC {
    vec4 uvRect;   // (u0, v0, u1, v1) in the bound texture
} pc;

void main() {
    Particle p = pool.p[gl_InstanceIndex];

    if (p.alive == 0u) {
        // Degenerate quad: send every vertex to the same point so the
        // triangle has zero area and gets discarded by the rasterizer
        // without ever invoking the fragment shader.
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        fragUV    = vec2(0.0);
        fragColor = vec4(0.0);
        return;
    }

    float t    = clamp(p.timeAlive / p.lifetime, 0.0, 1.0);
    float size = mix(p.sizeStart, p.sizeEnd, t);
    vec4  col  = mix(p.colorStart, p.colorEnd, t);

    vec2 worldPos = p.position + inPosition * size;
    gl_Position = frame.viewProjection * vec4(worldPos, 0.0, 1.0);

    // Map [0,1] quad UV into the sprite's atlas-relative rect.
    fragUV    = mix(pc.uvRect.xy, pc.uvRect.zw, inUV);
    fragColor = col;
}
