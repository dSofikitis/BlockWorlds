#version 410 core

in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_atlas;

out vec4 frag;

void main() {
    vec4 t = texture(u_atlas, v_uv);
    if (t.a < 0.02) discard;
    frag = vec4(t.rgb * v_color.rgb, t.a * v_color.a);
}
