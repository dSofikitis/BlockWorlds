#version 410 core

in vec3 v_normal;

uniform vec3  u_color;
uniform vec3  u_sun_dir;
uniform float u_sun_strength;
uniform float u_ambient;

out vec4 frag_color;

void main() {
    float diffuse = max(0.0, dot(v_normal, u_sun_dir));
    float face_bias = 0.82 + 0.18 * v_normal.y;
    if (v_normal.y < -0.5) face_bias = 0.55;
    float light = u_ambient * face_bias + diffuse * u_sun_strength;
    light = clamp(light, 0.05, 1.0);
    frag_color = vec4(u_color * light, 1.0);
}