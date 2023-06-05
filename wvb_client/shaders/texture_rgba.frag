#version 300 es

precision highp float;

in vec2 uv;
out vec4 out_color;

uniform sampler2D rgba_tex;

void main() {
    out_color = texture(rgba_tex, uv);

}
