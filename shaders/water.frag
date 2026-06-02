#version 410 core

in vec3 v_world_pos;
in vec4 v_clip;

uniform sampler2D u_reflection;
uniform vec3  u_eye;
uniform float u_time;
uniform vec3  u_sun_dir;
uniform float u_sun_strength;
uniform vec3  u_fog_color;
uniform float u_fog_density;

out vec4 frag_color;

void main() {
    vec2 p = v_world_pos.xz;
    float t = u_time;

    float nx = 0.05 * (sin(p.x * 0.70 + t * 1.1) + sin((p.x + p.y) * 0.50 - t * 0.9))
             + 0.022 * (sin(p.x * 1.90 - t * 1.7) + sin(p.y * 1.60 + t * 1.3));
    float nz = 0.05 * (cos(p.y * 0.80 + t * 1.3) + sin((p.x - p.y) * 0.60 + t * 0.7))
             + 0.022 * (cos(p.y * 2.10 + t * 1.9) + sin(p.x * 1.70 - t * 1.1));
    vec3 n = normalize(vec3(nx, 1.0, nz));

    vec3 view_dir = normalize(u_eye - v_world_pos);

    vec2 uv = (v_clip.xy / v_clip.w) * 0.5 + 0.5;
    uv += n.xz * 0.025;
    uv = clamp(uv, 0.002, 0.998);
    vec3 reflection = texture(u_reflection, uv).rgb;

    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(view_dir, n), 0.0), 5.0);
    fresnel = clamp(fresnel, 0.0, 1.0);

    vec3 water_col = vec3(0.04, 0.13, 0.22);
    vec3 col = mix(water_col, reflection, fresnel);

    vec3 half_vec = normalize(view_dir + normalize(u_sun_dir));
    float spec = pow(max(dot(n, half_vec), 0.0), 200.0) * u_sun_strength;
    col += vec3(1.0, 0.96, 0.88) * spec;

    float fdist = length(v_world_pos - u_eye);
    float fog = clamp(1.0 - exp(-fdist * u_fog_density), 0.0, 1.0);
    float sun_amt = max(dot(-view_dir, normalize(u_sun_dir)), 0.0);
    vec3 fog_col = mix(u_fog_color, vec3(1.0, 0.86, 0.62), pow(sun_amt, 8.0) * u_sun_strength);
    col = mix(col, fog_col, fog);

    float alpha = mix(0.62, 0.96, fresnel);
    frag_color = vec4(col, alpha);
}