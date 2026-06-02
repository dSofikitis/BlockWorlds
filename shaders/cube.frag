#version 410 core

in vec2  v_uv;
in vec2  v_tile;
in vec3  v_normal;
in float v_skylight;
in float v_blocklight;
in float v_ao;
in float v_world_y;
in vec3  v_world_pos;

uniform sampler2D u_atlas;
uniform vec3  u_sun_dir;
uniform float u_sun_strength;
uniform float u_ambient;
uniform float u_sea_level;
uniform int   u_eye_in_water;
uniform float u_eye_water_depth;

uniform sampler2DShadow u_shadow_map;
uniform int   u_shadow_enabled;
uniform float u_shadow_texel;

uniform int   u_clip_below;
uniform float u_clip_height;

uniform sampler2D u_ssao;
uniform int   u_ssao_enabled;
uniform vec2  u_screen;

uniform sampler3D u_blocklight_tex;
uniform vec3  u_light_origin;
uniform float u_light_dim;
uniform int   u_blocklight_enabled;

uniform mat4  u_light_vp;
uniform float u_shadow_world_texel;

uniform vec3  u_eye;
uniform vec3  u_fog_color;
uniform float u_fog_density;

out vec4 frag_color;

const float ATLAS_TILES = 5.0;

bool is_emissive_tile(vec2 tile) {
    return tile.x > 3.5 && tile.y < 0.5;
}

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
    if (u_eye_in_water != 0) return col;
    float fdist = length(v_world_pos - u_eye);
    float fog   = clamp(1.0 - exp(-fdist * u_fog_density), 0.0, 1.0);
    vec3  vdir  = normalize(v_world_pos - u_eye);
    float sun_amt = max(dot(vdir, normalize(u_sun_dir)), 0.0);
    vec3  fog_col = mix(u_fog_color, vec3(1.0, 0.86, 0.62), pow(sun_amt, 8.0) * u_sun_strength);
    return mix(col, fog_col, fog);
}

void main() {
    if (u_clip_below == 1 && v_world_y < u_clip_height) discard;

    vec2 local = fract(v_uv);
    vec2 atlas_uv = (v_tile + local) / ATLAS_TILES;
    vec4 tex = texture(u_atlas, atlas_uv);
    if (tex.a < 0.5) discard;
    float alpha = tex.a;

    if (v_tile.x < 0.5 && v_tile.y > 0.5 && v_tile.y < 1.5) {
        alpha = 0.7;
    }
    if (v_tile.x < 0.5 && v_tile.y > 3.5 && v_tile.y < 4.5) {
        alpha = 0.45;
    }

    if (is_emissive_tile(v_tile)) {
        frag_color = vec4(apply_fog(tex.rgb), alpha);
        return;
    }

    vec3  n       = normalize(v_normal);
    float ndotl   = dot(n, normalize(u_sun_dir));
    float diffuse = max(0.0, ndotl);

    float face_bias = 0.82 + 0.18 * v_normal.y;
    if (v_normal.y < -0.5) face_bias = 0.55;

    float occ            = sun_occlusion_factor(ndotl, n);
    float sun_visibility = 1.0 - occ * 0.92;
    float shadow_dim     = 1.0 - occ * 0.30;

    float ao_env = 1.0;
    if (u_ssao_enabled == 1) {
        ao_env = clamp(pow(texture(u_ssao, gl_FragCoord.xy / u_screen).r, 2.2), 0.0, 1.0);
    }

    float sky_term = (u_ambient * face_bias * ao_env + diffuse * u_sun_strength * sun_visibility) * v_skylight * shadow_dim * v_ao;

    float block_term  = v_blocklight;
    if (u_blocklight_enabled == 1) {
        vec3 sp = v_world_pos + n * 0.5;
        vec3 lc = (sp - u_light_origin) / u_light_dim;
        if (all(greaterThan(lc, vec3(0.0))) && all(lessThan(lc, vec3(1.0)))) {
            float s3 = texture(u_blocklight_tex, lc).r;
            vec3  e  = min(lc, vec3(1.0) - lc);
            float edge = clamp(min(e.x, min(e.y, e.z)) / 0.06, 0.0, 1.0);
            block_term = mix(v_blocklight, s3, edge);
        }
    }
    vec3  block_color = vec3(1.00, 0.86, 0.62);

    float cave_fill = (0.06 + u_ambient * 0.22) * face_bias * ao_env * v_ao;
    float light = max(max(sky_term, block_term), cave_fill);
    vec3 tint   = mix(vec3(1.0), block_color, clamp(block_term * 0.7, 0.0, 0.7));
    vec3 final_rgb = tex.rgb * light * tint;
    final_rgb *= mix(1.0, ao_env, 0.45);

    float view_depth = 0.0;
    if (u_eye_in_water > 0) {
        if (v_world_y >= u_sea_level) {
            float above = v_world_y - u_sea_level;
            view_depth = u_eye_water_depth + 13.0 + above * 0.60;
        } else {
            view_depth = u_eye_water_depth + (u_sea_level - v_world_y);
        }
    }
    if (view_depth > 0.0) {
        float water_factor = exp(-view_depth * 0.24);
        if (water_factor < 0.02) water_factor = 0.02;
        vec3 water_tint = vec3(0.06, 0.18, 0.38);
        final_rgb = mix(water_tint * final_rgb, final_rgb, water_factor);
        final_rgb += tex.rgb * block_term * block_color * 0.65;
    }

    final_rgb = apply_fog(final_rgb);

    final_rgb = clamp(final_rgb, vec3(0.02), vec3(1.0));
    frag_color = vec4(final_rgb, alpha);
}