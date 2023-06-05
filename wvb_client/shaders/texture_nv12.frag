#version 300 es

precision highp float;

in vec2 uv;
out vec4 out_color;

uniform sampler2D nv12_tex_y; // Y plane
uniform sampler2D nv12_tex_uv; // UV plane, downsampled interleaved

void main() {
    vec3 yuv;
    yuv.x = texture(nv12_tex_y, uv).r;
    yuv.yz = texture(nv12_tex_uv, uv).rg - vec2(0.5, 0.5);

    // Convert to RGB
    out_color = vec4(yuv.x                   + 1.28033 * yuv.z,
                     yuv.x - 0.21482 * yuv.y - 0.38059 * yuv.z,
                     yuv.x + 2.12798 * yuv.y,
                     1.0);

}
