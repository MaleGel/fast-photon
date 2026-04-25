#version 450

// Full-screen triangle, no vertex buffer. gl_VertexIndex in [0, 2] maps to
// a triangle that fully covers the [-1, 1] clip-space rectangle. Using a
// single oversized triangle (instead of two) avoids a diagonal seam and
// gives the GPU one less primitive to set up.
//
//    vertex 0: (-1, -1)   → uv (0, 0)
//    vertex 1: ( 3, -1)   → uv (2, 0)
//    vertex 2: (-1,  3)   → uv (0, 2)
//
// The parts of the triangle outside [-1,1] get clipped away — what remains
// covers the screen exactly once.

layout(location = 0) out vec2 fragUV;

void main() {
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
}
