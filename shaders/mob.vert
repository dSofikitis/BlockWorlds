#version 410 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in float a_isfront;

uniform mat4 u_mvp;
uniform mat4 u_model;

out vec3 v_normal;
out vec2 v_uv;
out float v_isfront;
out vec3 v_world_pos;
out float v_world_y;

void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_normal = a_normal;
    v_uv = a_uv;
    v_isfront = a_isfront;
    v_world_pos = wp.xyz;
    v_world_y = wp.y;
}
