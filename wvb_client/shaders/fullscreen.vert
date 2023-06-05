#version 300 es

#define NUM_VIEWS 2
#define VIEW_ID gl_ViewID_OVR
#extension GL_OVR_multiview2 : require
layout(num_views=NUM_VIEWS) in;

uniform bool flip_y;
uniform bool split_texture;


const vec2 vertices[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0),
    vec2(-1.0, 1.0),
    // Triangle 2
    vec2(-1.0, 1.0),
    vec2(1.0, -1.0),
    vec2(1.0, 1.0)
);


const vec2 uvs[18] = vec2[18](
    // == Eye 0: left half of the texture ==
    vec2(0.0, 0.0),
    vec2(0.5, 0.0),
    vec2(0.0, 1.0),
    // Triangle 2
    vec2(0.0, 1.0),
    vec2(0.5, 0.0),
    vec2(0.5, 1.0),

    // == Eye 1: right half of the texture ==
    vec2(0.5, 0.0),
    vec2(1.0, 0.0),
    vec2(0.5, 1.0),
    // Triangle 2
    vec2(0.5, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),

    // == Use the whole texture ==
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    // Triangle 2
    vec2(0.0, 1.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0)
);


out vec2 uv;

void main() {
    gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);

    int id = int(VIEW_ID);
    if (split_texture == false) {
        id = 2;
    }

    uv = uvs[gl_VertexID + (id * 6)];

    // Flip the texture vertically
    if (flip_y == true) {
        uv.y = 1.0 - uv.y;
    }

}