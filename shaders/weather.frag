#version 410 core

in float v_alpha;

uniform vec3 u_color;
uniform int  u_round;

out vec4 frag_color;

void main() {
    if (u_round == 1) {
        vec2 d = gl_PointCoord - vec2(0.5);
        float r = dot(d, d);
        if (r > 0.25) discard;
        float soft = 1.0 - smoothstep(0.10, 0.25, r);
        frag_color = vec4(u_color, v_alpha * soft);
    } else {
        frag_color = vec4(u_color, v_alpha);
    }
}
