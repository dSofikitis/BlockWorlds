#version 410 core

layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec3 a_normal;
layout(location = 7) in float a_flags;

uniform mat4 u_mvp;
uniform vec3 u_chunk_origin;

out vec3 v_world_pos;
out vec4 v_clip;
out float v_flags;

void main() {
    vec3 world_pos = u_chunk_origin + a_pos;
    v_world_pos = world_pos;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_clip = gl_Position;
    v_flags = a_flags;
}
