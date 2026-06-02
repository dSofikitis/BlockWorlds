#version 410 core

in vec2 v_uv;

uniform sampler2D u_ssao;
uniform vec2 u_texel;

out vec4 frag_color;

void main() {
    float sum = 0.0;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            sum += texture(u_ssao, v_uv + vec2(float(x), float(y)) * u_texel).r;
        }
    }
    frag_color = vec4(vec3(sum / 25.0), 1.0);
}