#version 410 core

in vec2 v_uv;

uniform sampler2D u_atlas;
uniform vec4 u_tint;

out vec4 frag_color;

void main() {
    vec4 t = texture(u_atlas, v_uv);
    frag_color = vec4(t.rgb * u_tint.rgb, u_tint.a);
}