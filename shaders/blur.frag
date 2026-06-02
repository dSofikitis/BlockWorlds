#version 410 core

in vec2 v_uv;

uniform sampler2D u_scene;
uniform vec2  u_texel;
uniform float u_darken;

out vec4 frag_color;

void main() {
    vec3 sum = vec3(0.0);
    const float spread = 2.0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            vec2 off = vec2(float(dx), float(dy)) * u_texel * spread;
            sum += texture(u_scene, v_uv + off).rgb;
        }
    }
    vec3 col = sum / 25.0;
    col *= u_darken;
    frag_color = vec4(col, 1.0);
}