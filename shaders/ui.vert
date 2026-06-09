#version 410 core

layout(location = 0) in vec2 a_pos;

uniform float u_aspect;

void main() {
    gl_Position = vec4(a_pos.x / u_aspect, a_pos.y, 0.0, 1.0);
}
