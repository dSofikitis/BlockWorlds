#version 410 core

in vec2 v_uv;

uniform sampler2D u_depth;
uniform vec4  u_proj;
uniform float u_radius;
uniform float u_bias;

out vec4 frag_color;

float hash1(float n) { return fract(sin(n) * 43758.5453123); }
float hash2(vec2 p)  { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453123); }

vec3 view_pos(vec2 uv, float d) {
    float ndcz = d * 2.0 - 1.0;
    float vz = -u_proj.w / (u_proj.z + ndcz);
    float vx = -(uv.x * 2.0 - 1.0) * vz / u_proj.x;
    float vy = -(uv.y * 2.0 - 1.0) * vz / u_proj.y;
    return vec3(vx, vy, vz);
}

vec2 project(vec3 v) {
    vec2 ndc = vec2(u_proj.x * v.x, u_proj.y * v.y) / (-v.z);
    return ndc * 0.5 + 0.5;
}

void main() {
    float d = texture(u_depth, v_uv).r;
    if (d >= 1.0) { frag_color = vec4(1.0); return; }

    vec3 P = view_pos(v_uv, d);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));

    vec2 noise_tile = mod(gl_FragCoord.xy, 4.0);
    float ang = hash2(noise_tile) * 6.2831853;
    vec3 randv = vec3(cos(ang), sin(ang), 0.0);
    vec3 T = normalize(randv - N * dot(randv, N));
    vec3 B = cross(N, T);
    mat3 tbn = mat3(T, B, N);

    const int SAMPLES = 16;
    float occ = 0.0;
    for (int i = 0; i < SAMPLES; i++) {
        float fi = float(i);
        vec3 dir = normalize(vec3(
            hash1(fi * 3.1 + 0.5) * 2.0 - 1.0,
            hash1(fi * 3.1 + 1.5) * 2.0 - 1.0,
            hash1(fi * 3.1 + 2.5)
        ));
        float scale = fi / float(SAMPLES);
        dir *= mix(0.1, 1.0, scale * scale);

        vec3 sample_pos = P + (tbn * dir) * u_radius;
        vec2 suv = project(sample_pos);
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;

        float sd = texture(u_depth, suv).r;
        if (sd >= 1.0) continue;
        vec3 surf = view_pos(suv, sd);

        float range = smoothstep(0.0, 1.0, u_radius / max(abs(P.z - surf.z), 0.0001));
        if (surf.z >= sample_pos.z + u_bias) occ += range;
    }

    float ao = 1.0 - occ / float(SAMPLES);
    frag_color = vec4(vec3(ao), 1.0);
}