#version 300 es

precision highp float;

in vec2 uv;
out vec4 out_color;

uniform sampler2D rgba_tex;

void main() {
    vec4 bgra = texture(rgba_tex, uv);
    out_color = vec4(bgra.b, bgra.g, bgra.r, bgra.a);
}
