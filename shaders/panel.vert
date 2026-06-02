#version 410 core

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

uniform float u_aspect;

void main() {
    float ndc_x = (a_pos.x / u_aspect) * 2.0 - 1.0;
    float ndc_y = 1.0 - a_pos.y * 2.0;
    gl_Position = vec4(ndc_x, ndc_y, 0.0, 1.0);
}