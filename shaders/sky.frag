#version 410 core

in vec2 v_uv;

uniform vec3  u_cam_right;
uniform vec3  u_cam_up;
uniform vec3  u_cam_fwd;
uniform float u_tan_half_fov;
uniform float u_aspect;
uniform vec3  u_sun_dir;
uniform float u_time;
uniform vec3  u_zenith;
uniform vec3  u_horizon;
uniform float u_day;
uniform vec3  u_atmo_tint;
uniform vec3  u_eye;
uniform float u_overcast;

out vec4 frag_color;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

const float CLOUD_Y0 = 150.0;
const float CLOUD_Y1 = 210.0;

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float vnoise3(vec3 p) {
    vec3 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}

float fbm3(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) {
        v += a * vnoise3(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}

float cloud_density(vec3 p) {
    vec3 wp = p + vec3(u_time * 3.2, 0.0, u_time * 1.6);
    float base = fbm3(wp * 0.012);
    float cov  = mix(0.50, 0.16, u_overcast);
    float d    = smoothstep(cov, cov + 0.28, base);
    float h    = (p.y - CLOUD_Y0) / (CLOUD_Y1 - CLOUD_Y0);
    float vshape = smoothstep(0.0, 0.22, h) * smoothstep(1.0, 0.55, h);
    return d * vshape * mix(0.7, 1.5, u_overcast);
}

float light_march(vec3 p, vec3 sun) {
    float dens = 0.0;
    for (int i = 1; i <= 3; i++) {
        dens += cloud_density(p + sun * (13.0 * float(i)));
    }
    return exp(-dens * 0.75);
}

vec2 march_clouds(vec3 ro, vec3 rd, vec3 sun) {
    if (rd.y < 0.02) return vec2(0.0);
    float t0 = (CLOUD_Y0 - ro.y) / rd.y;
    float t1 = (CLOUD_Y1 - ro.y) / rd.y;
    if (t1 < 0.0) return vec2(0.0);
    t0 = max(t0, 0.0);
    float seg = min(t1 - t0, 1100.0);
    const int STEPS = 20;
    float dt = seg / float(STEPS);
    float t  = t0 + dt * 0.5;
    float transmittance = 1.0;
    float light_energy  = 0.0;
    for (int i = 0; i < STEPS; i++) {
        vec3 p = ro + rd * t;
        float d = cloud_density(p);
        if (d > 0.001) {
            float shadow = light_march(p, sun);
            float lstep = exp(-d * dt * 0.10);
            float absorbed = (1.0 - lstep) * transmittance;
            light_energy += absorbed * shadow;
            transmittance *= lstep;
            if (transmittance < 0.03) break;
        }
        t += dt;
    }
    float alpha  = 1.0 - transmittance;
    float bright = light_energy / max(alpha, 0.001);
    return vec2(alpha, bright);
}

void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 ray = normalize(u_cam_fwd
                         + u_cam_right * (ndc.x * u_tan_half_fov * u_aspect)
                         + u_cam_up    * (ndc.y * u_tan_half_fov));

    vec3 sun = normalize(u_sun_dir);
    float night = 1.0 - u_day;

    float t = smoothstep(-0.05, 0.55, ray.y);
    vec3 col = mix(u_horizon, u_zenith, t) * u_atmo_tint;
    col = mix(col, vec3(0.62, 0.65, 0.70) * (0.25 + 0.75 * u_day), u_overcast * 0.6);

    float sd = max(dot(ray, sun), 0.0);
    vec3 sun_col = vec3(1.0, 0.92, 0.74);
    float halo = pow(sd, 8.0) * 0.35 + pow(sd, 220.0) * 0.7;
    float disk = smoothstep(0.9991, 0.9996, sd);
    float sun_occ = 1.0 - u_overcast * 0.85;
    col += sun_col * (halo * u_day + disk * (0.4 + 0.6 * u_day)) * sun_occ;

    float md   = max(dot(ray, -sun), 0.0);
    float moon = smoothstep(0.9993, 0.9997, md);
    col += vec3(0.85, 0.88, 0.97) * moon * night * sun_occ;
    col += vec3(0.45, 0.50, 0.65) * pow(md, 48.0) * 0.25 * night * sun_occ;

    if (ray.y > 0.04) {
        vec2  sp   = ray.xz / (ray.y + 0.15) * 16.0;
        vec2  cell = floor(sp);
        vec2  fp   = fract(sp);
        float h    = hash21(cell);
        float star = 0.0;
        if (h > 0.90) {
            vec2  pos    = vec2(hash21(cell + 7.1), hash21(cell + 3.3));
            float d      = length(fp - pos);
            float bright = (h - 0.90) * 9.0;
            float tw     = 0.6 + 0.4 * sin(u_time * 2.5 + h * 60.0);
            star = smoothstep(0.05, 0.0, d) * bright * tw;
        }
        col += vec3(star) * night * smoothstep(0.04, 0.22, ray.y) * (1.0 - u_overcast * 0.9);
    }

    vec2 cl = march_clouds(u_eye, ray, sun);
    float fade = smoothstep(0.02, 0.13, ray.y);
    if (cl.x > 0.0 && fade > 0.0) {
        vec3 shadow_col = vec3(0.40, 0.43, 0.50);
        vec3 lit_col    = vec3(1.0, 0.98, 0.94);
        vec3 cloud = mix(shadow_col, lit_col, clamp(cl.y, 0.0, 1.0));
        cloud *= (0.16 + 0.84 * u_day);
        cloud *= mix(1.0, 0.6, u_overcast);
        cloud = mix(cloud, sun_col, pow(sd, 3.0) * 0.35 * u_day);
        col = mix(col, cloud, cl.x * fade);
    }

    frag_color = vec4(col, 1.0);
}
