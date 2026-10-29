#version 450
// SDL's renderer feeds a textured quad: color at location 0, uv at location 1. A full-target triangle gives the same.
layout(location = 0) out vec4 color;
layout(location = 1) out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    color = vec4(1.0);
    uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
