#version 410 core

in vec2 v_uv;
in vec2 v_tile;

uniform sampler2D u_atlas;

void main() {
    vec2 atlas_uv = (v_tile + fract(v_uv)) / 16.0;
    if (texture(u_atlas, atlas_uv).a < 0.5) discard;
}
