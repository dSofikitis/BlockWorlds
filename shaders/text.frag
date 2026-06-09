#version 410 core

in vec2 v_uv;

uniform sampler2D u_font;
uniform vec4 u_color;

out vec4 frag_color;

void main() {
    float cov = texture(u_font, v_uv).a;
    if (cov < 0.5) discard;
    frag_color = vec4(u_color.rgb, u_color.a);
}
