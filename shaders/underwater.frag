#version 410 core

in vec2 v_ndc;

uniform float u_time;
uniform float u_aspect;
uniform float u_fade;

out vec4 frag_color;

void main() {
    vec2 cuv = v_ndc * vec2(u_aspect, 1.0);

    float vignette = smoothstep(0.50, 1.40, length(v_ndc));

    float wave = 0.97 + 0.05 * sin(cuv.y * 1.6 + u_time * 0.35)
                       + 0.03 * sin(cuv.x * 1.1 - u_time * 0.25);

    vec3  tint  = vec3(0.06, 0.24, 0.58) * wave;
    tint = mix(tint, vec3(0.02, 0.06, 0.18), vignette);

    float alpha = (0.76 + 0.17 * vignette) * u_fade;
    frag_color = vec4(tint, alpha);
}