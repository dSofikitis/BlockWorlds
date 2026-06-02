#version 410 core

layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in vec2  a_tile;
layout(location = 3) in vec3  a_normal;
layout(location = 4) in float a_skylight;
layout(location = 5) in float a_blocklight;
layout(location = 6) in float a_ao;

uniform mat4 u_mvp;
uniform vec3 u_chunk_origin;

out vec2  v_uv;
out vec2  v_tile;
out vec3  v_normal;
out float v_skylight;
out float v_blocklight;
out float v_ao;
out float v_world_y;
out vec3  v_world_pos;

void main() {
    vec3 world_pos = u_chunk_origin + a_pos;
    gl_Position    = u_mvp * vec4(a_pos, 1.0);
    v_uv           = a_uv;
    v_tile         = a_tile;
    v_normal       = a_normal;
    v_skylight     = a_skylight;
    v_blocklight   = a_blocklight;
    v_ao           = a_ao;
    v_world_y      = world_pos.y;
    v_world_pos    = world_pos;
}