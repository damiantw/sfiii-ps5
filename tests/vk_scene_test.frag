#version 450
// Test shader with geometry.hlsl's resource layout (SDL_GPU SPIR-V convention).
layout(set = 2, binding = 0) uniform sampler2D picture;
layout(set = 2, binding = 1) readonly buffer Scene { uint scene[]; };
layout(set = 2, binding = 2) readonly buffer Textures { uint textures[]; };
layout(set = 3, binding = 0) uniform Pass { uint aa_pass; };
layout(location = 0) in vec4 color;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 o;
void main() {
    if (aa_pass == 0u) {                 // geometry pass: pixel (x, y) takes scene[y*w + x] (w in scene[0]) as 0x00RRGGBB
        uint w = scene[0];
        uvec2 p = uvec2(gl_FragCoord.xy);
        uint v = scene[1u + p.y * w + p.x] ^ textures[0];
        o = vec4(float((v >> 16) & 255u), float((v >> 8) & 255u), float(v & 255u), 255.0) / 255.0 * texture(picture, uv);
    } else {                             // effects pass: invert the previous output
        o = vec4(1.0 - texture(picture, uv).rgb, 1.0);
    }
}
