#version 410 core

layout(location = 0) in vec3  a_pos;
layout(location = 1) in float a_alpha;

uniform mat4  u_mvp;
uniform float u_point_size;

out float v_alpha;

void main() {
    gl_Position  = u_mvp * vec4(a_pos, 1.0);
    gl_PointSize = u_point_size;
    v_alpha      = a_alpha;
}
