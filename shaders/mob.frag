#version 410 core

in vec3 v_normal;
in vec2 v_uv;
in float v_isfront;
in vec3 v_world_pos;
in float v_world_y;

uniform vec3  u_sun_dir;
uniform float u_sun_strength;
uniform float u_ambient;
uniform vec3  u_color;
uniform float u_flash;

uniform sampler2D u_atlas;
uniform int   u_textured;
uniform vec2  u_body_tile;
uniform vec2  u_front_tile;
uniform float u_tile_size;

uniform vec3  u_eye;
uniform float u_sea_level;
uniform int   u_eye_in_water;
uniform float u_eye_water_depth;
uniform vec3  u_fog_color;
uniform float u_fog_density;

uniform sampler2DShadow u_shadow_map;
uniform int   u_shadow_enabled;
uniform float u_shadow_texel;
uniform float u_shadow_world_texel;
uniform mat4  u_light_vp;

uniform sampler3D u_blocklight_tex;
uniform vec3  u_light_origin;
uniform float u_light_dim;
uniform int   u_blocklight_enabled;

out vec4 frag_color;

float sun_occlusion_factor(float ndotl, vec3 n) {
    if (u_shadow_enabled == 0) return 0.0;
    if (ndotl <= 0.0) return 1.0;

    float noff = u_shadow_world_texel * (1.5 + 3.0 * (1.0 - ndotl));
    vec4 lp = u_light_vp * vec4(v_world_pos + n * noff, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;

    float bias = 0.0004;
    float lit = 0.0;
    for (int x = -2; x <= 2; x++) {
        for (int y = -2; y <= 2; y++) {
            vec2 off = vec2(float(x), float(y)) * u_shadow_texel;
            lit += texture(u_shadow_map, vec3(proj.xy + off, proj.z - bias));
        }
    }
    return 1.0 - lit / 25.0;
}

vec3 apply_fog(vec3 col) {
    float fdist = length(v_world_pos - u_eye);
    if (u_eye_in_water != 0) {
        float dens  = clamp(0.07 + u_eye_water_depth * 0.007, 0.07, 0.26);
        float extra = (v_world_y >= u_sea_level) ? 3.2 : 1.0;
        float fog   = clamp(1.0 - exp(-fdist * dens * extra), 0.0, 1.0);
        vec3  deep  = vec3(0.03, 0.11, 0.24);
        return mix(col, deep, fog);
    }
    float fog   = clamp(1.0 - exp(-fdist * u_fog_density), 0.0, 1.0);
    vec3  vdir  = normalize(v_world_pos - u_eye);
    float sun_amt = max(dot(vdir, normalize(u_sun_dir)), 0.0);
    vec3  fog_col = mix(u_fog_color, vec3(1.0, 0.86, 0.62), pow(sun_amt, 8.0) * u_sun_strength);
    return mix(col, fog_col, fog);
}

void main() {
    vec3 base;
    if (u_textured == 1) {
        vec2 tile = (v_isfront > 0.5) ? u_front_tile : u_body_tile;
        vec2 uv = (tile + clamp(v_uv, 0.001, 0.999)) * u_tile_size;
        vec4 t = texture(u_atlas, uv);
        if (t.a < 0.5) discard;
        base = t.rgb;
    } else {
        base = u_color;
    }

    vec3  n       = normalize(v_normal);
    float ndotl   = dot(n, normalize(u_sun_dir));
    float diffuse = max(0.0, ndotl);
    float face_bias = 0.82 + 0.18 * n.y;
    if (n.y < -0.5) face_bias = 0.55;

    float occ            = sun_occlusion_factor(ndotl, n);
    float sun_visibility = 1.0 - occ * 0.92;
    float shadow_dim     = 1.0 - occ * 0.30;

    float sky_term = (u_ambient * face_bias + diffuse * u_sun_strength * sun_visibility) * shadow_dim;

    float block_term = 0.0;
    if (u_blocklight_enabled == 1) {
        vec3 sp = v_world_pos + n * 0.3;
        vec3 lc = (sp - u_light_origin) / u_light_dim;
        if (all(greaterThan(lc, vec3(0.0))) && all(lessThan(lc, vec3(1.0)))) {
            block_term = texture(u_blocklight_tex, lc).r;
        }
    }
    vec3 block_color = vec3(1.00, 0.86, 0.62);

    float cave_fill = (0.06 + u_ambient * 0.22) * face_bias;
    float light = max(max(sky_term, block_term), cave_fill);
    vec3 tint   = mix(vec3(1.0), block_color, clamp(block_term * 0.7, 0.0, 0.7));
    vec3 c = base * light * tint;

    c = mix(c, vec3(1.0), clamp(u_flash, 0.0, 1.0) * 0.7);

    float view_depth = 0.0;
    if (u_eye_in_water > 0) {
        if (v_world_y >= u_sea_level) {
            float above = v_world_y - u_sea_level;
            view_depth = u_eye_water_depth + 13.0 + above * 0.60;
        } else {
            view_depth = u_eye_water_depth + (u_sea_level - v_world_y);
        }
    } else if (v_world_y < u_sea_level) {
        view_depth = (u_sea_level - v_world_y) * 0.85;
    }
    if (view_depth > 0.0) {
        float water_factor = exp(-view_depth * 0.24);
        if (water_factor < 0.02) water_factor = 0.02;
        vec3 water_tint = vec3(0.06, 0.18, 0.38);
        c = mix(water_tint * c, c, water_factor);
        c += base * block_term * block_color * 0.65;
    }

    c = apply_fog(c);
    c = clamp(c, vec3(0.02), vec3(1.0));
    frag_color = vec4(c, 1.0);
}
