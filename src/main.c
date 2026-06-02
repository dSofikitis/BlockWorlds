#include "glcompat.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>

#include "vec3.h"
#include "mat4.h"
#include "world.h"
#include "texture.h"
#include "player.h"
#include "raycast.h"
#include "ui.h"
#include "text.h"
#include "console.h"
#include "falling.h"
#include "weather.h"
#include "lighttex.h"
#include "audio.h"
#include "framebuffer.h"
#include "platform.h"

#define PI 3.14159265358979323846f

#define DAY_LENGTH_SEC (96.0f * 60.0f)
#define REACH 6.0f
#define STEP_STRIDE       1.4f
#define STEP_STRIDE_WATER 1.3f
#define SWIM_STRIDE       1.7f
#define SHADOW_MAP_SIZE 4096
#define REFLECT_W 1280
#define REFLECT_H 720
#define SSAO_W 2560
#define SSAO_H 1440
#define WATER_SURFACE_Y ((float)SEA_LEVEL + 0.875f)

#define BW_DIR        ".blockworlds"
#define SAVES_DIR     BW_DIR "/saves"
#define SKINS_DIR     BW_DIR "/skins"
#define AUDIO_DIR     BW_DIR "/audio"
#define SETTINGS_PATH BW_DIR "/settings"
#define MAX_PACK_NAME 96

typedef enum {
    GS_TITLE, GS_CREATE, GS_SETTINGS, GS_LOADING, GS_PLAYING, GS_PAUSED, GS_INVENTORY,
    GS_TEXPACK, GS_AUDIOPACK
} game_state_t;

typedef struct {
    float fov;
    float mouse_sens;
    float volume;
    int   render_distance;
    int   fps_overlay;
    char  texture_pack[MAX_PACK_NAME];
    char  audio_pack[MAX_PACK_NAME];
} settings_t;

#define SETTINGS_DEFAULT_INIT { 70.0f, 0.0025f, 0.5f, 5, 1, "", "" }

typedef struct {
    char         path[192];
    char         dir[192];
    world_meta_t meta;
} world_entry_t;

typedef struct { float x, y, w, h; } rect_t;

static GLFWwindow *g_win = NULL;
static GLuint      atlas_tex = 0;

static player_t player;
static world_t  *world = NULL;
static ui_t     *ui = NULL;
static text_t   *text = NULL;
static falling_system_t *falling = NULL;
static weather_t *weather = NULL;
static lighttex_t *lighttex = NULL;
static audio_t  *audio = NULL;
static console_t *console = NULL;
static int       console_open_key = -1;

static double last_mouse_x = 0.0;
static double last_mouse_y = 0.0;
static int    mouse_initialized = 0;

static int   fps_shown = 1;
static float world_time = 0.30f;

#define LIGHT_TEX_DIM 128

static int    weather_kind        = WEATHER_CLEAR;
static int    weather_precip_type = WEATHER_RAIN;
static float  weather_overcast    = 0.0f;
static float  weather_precip      = 0.0f;
static double weather_until       = 0.0;
static int    weather_manual      = 0;
static float  weather_shelter     = 0.0f;

static game_state_t game_state = GS_TITLE;
static game_state_t settings_return = GS_TITLE;
static int   pause_capture_pending = 0;
static int   loading_started = 0;
static int   loading_peak = 0;

static int   ui_click = 0;
static float ui_click_x = 0.0f;
static float ui_click_y = 0.0f;
static int   ui_rclick = 0;
static float ui_rclick_x = 0.0f;
static float ui_rclick_y = 0.0f;
static float hover_sig = -1.0f;

static double water_enter_time = -1.0;
static int    prev_underwater = -1;

static char  save_path[192] = SAVES_DIR "/world/world.bw";

static const settings_t SETTINGS_DEFAULTS = SETTINGS_DEFAULT_INIT;
static settings_t settings = SETTINGS_DEFAULT_INIT;

#define MAX_WORLDS 16
static world_entry_t world_list[MAX_WORLDS];
static int           world_count = 0;

static char field_name[WORLD_NAME_MAX];
static char field_seed[16];
static int  field_focus = 0;
static int  create_gamemode = GAMEMODE_CREATIVE;
static int  create_allow_commands = 1;

#define MAX_PACKS 24
#define PACK_LIST_MAX 6

typedef struct {
    char   name[MAX_PACK_NAME];
    char   file[MAX_PACK_NAME];
    char   path[224];
    GLuint preview;
    int    valid;
} skin_entry_t;
static skin_entry_t skin_list[MAX_PACKS];
static int    skin_count = 0;
static GLuint default_preview = 0;

typedef struct {
    char name[MAX_PACK_NAME];
    char path[224];
    int  valid;
} audiopack_entry_t;
static audiopack_entry_t audiopack_list[MAX_PACKS];
static int  audiopack_count = 0;

static int  have_skins = 0;
static int  have_audiopacks = 0;

static void leave_texpack(void);

static const char *const CMD_NAMES[] = {
    "help", "time", "tp", "gamemode", "give", "seed", "fly", "weather", "clear",
};
static const char *const CMD_USAGE[] = {
    "/help",
    "/time set <day|noon|night|midnight>",
    "/tp <x> <y> <z>",
    "/gamemode <creative|survival>",
    "/give <block>",
    "/seed",
    "/fly <on|off>",
    "/weather <clear|rain|snow|auto>",
    "/clear",
};
#define CMD_COUNT ((int)(sizeof CMD_NAMES / sizeof CMD_NAMES[0]))

static GLuint prog = 0;
static GLint mvp_loc, sun_dir_loc, sun_strength_loc, ambient_loc;
static GLint sea_level_loc, chunk_origin_loc, eye_in_water_loc, eye_depth_loc;
static GLint light_vp_loc, shadow_map_loc, shadow_enabled_loc, shadow_texel_loc;
static GLint shadow_world_texel_loc, eye_loc, fog_color_loc, fog_density_loc;

static GLint clip_below_loc, clip_height_loc;
static GLint blocklight_tex_loc, light_origin_loc, light_dim_loc, blocklight_enabled_loc;
static int   debug_view = 0;

static GLuint blit_prog = 0;
static GLint  blit_tex_loc;

static GLuint sky_prog = 0;
static GLint  sky_right_loc, sky_up_loc, sky_fwd_loc, sky_tanfov_loc, sky_aspect_loc;
static GLint  sky_sun_loc, sky_time_loc, sky_zenith_loc, sky_horizon_loc, sky_day_loc, sky_atmo_loc;
static GLint  sky_eye_loc, sky_overcast_loc;
static vec3   atmo_tint = {1.0f, 1.0f, 1.0f};

static GLuint depth_prog = 0;
static GLint  depth_mvp_loc;
static framebuffer_t *shadow_fb = NULL;

static GLuint water_prog = 0;
static GLint  water_mvp_loc, water_origin_loc, water_refl_loc;
static GLint  water_eye_loc, water_time_loc, water_sun_dir_loc, water_sun_strength_loc;
static GLint  water_fog_color_loc, water_fog_density_loc;
static framebuffer_t *reflect_fb = NULL;

static GLint ssao_tex_loc, ssao_enabled_loc, screen_loc;
static GLuint ssao_prog = 0;
static GLint  ssao_depth_loc, ssao_proj_loc, ssao_radius_loc, ssao_bias_loc;
static GLuint fsq_vao = 0;
static framebuffer_t *ssao_depth_fb = NULL;
static framebuffer_t *ssao_fb = NULL;

static GLuint ssao_blur_prog = 0;
static GLint  ssao_blur_tex_loc, ssao_blur_texel_loc;
static framebuffer_t *ssao_blur_fb = NULL;

#define HOTBAR_COUNT 10
static block_t hotbar[HOTBAR_COUNT];
static int     hotbar_count[HOTBAR_COUNT];
static int     hotbar_sel = 0;

#define PACK_COLS  8
#define PACK_ROWS  4
#define PACK_COUNT (PACK_COLS * PACK_ROWS)
#define STACK_MAX  64
static block_t pack[PACK_COUNT];
static int     pack_count[PACK_COUNT];

_Static_assert(HOTBAR_COUNT == SAVE_HOTBAR_SLOTS, "hotbar slot count must match save format");
_Static_assert(PACK_COUNT == SAVE_PACK_SLOTS, "pack slot count must match save format");

static void hotbar_select(int slot) {
    if (slot < 0 || slot >= HOTBAR_COUNT) return;
    if (slot != hotbar_sel) audio_play_hotbar(audio);
    hotbar_sel = slot;
    player.selected_block = hotbar[slot];
}

static void reset_hotbar(block_t first) {
    for (int i = 0; i < HOTBAR_COUNT; i++) { hotbar[i] = BLOCK_AIR; hotbar_count[i] = 0; }
    for (int i = 0; i < PACK_COUNT; i++)   { pack[i] = BLOCK_AIR;   pack_count[i] = 0; }
    hotbar[0] = first;
    hotbar_count[0] = (first != BLOCK_AIR) ? 1 : 0;
    hotbar_sel = 0;
    player.selected_block = hotbar[0];
}

static int inventory_add(block_t b) {
    if (b == BLOCK_AIR) return 0;
    for (int i = 0; i < HOTBAR_COUNT; i++)
        if (hotbar[i] == b && hotbar_count[i] > 0 && hotbar_count[i] < STACK_MAX) {
            hotbar_count[i]++;
            if (i == hotbar_sel) player.selected_block = b;
            return 1;
        }
    for (int i = 0; i < PACK_COUNT; i++)
        if (pack[i] == b && pack_count[i] > 0 && pack_count[i] < STACK_MAX) {
            pack_count[i]++;
            return 1;
        }
    for (int i = 0; i < HOTBAR_COUNT; i++)
        if (hotbar[i] == BLOCK_AIR) {
            hotbar[i] = b;
            hotbar_count[i] = 1;
            if (i == hotbar_sel) player.selected_block = b;
            return 1;
        }
    for (int i = 0; i < PACK_COUNT; i++)
        if (pack[i] == BLOCK_AIR) {
            pack[i] = b;
            pack_count[i] = 1;
            return 1;
        }
    return 0;
}

static void inventory_consume_selected(void) {
    int i = hotbar_sel;
    if (hotbar[i] == BLOCK_AIR || hotbar_count[i] <= 0) return;
    hotbar_count[i]--;
    if (hotbar_count[i] <= 0) {
        hotbar[i] = BLOCK_AIR;
        hotbar_count[i] = 0;
        player.selected_block = BLOCK_AIR;
    }
}

static const block_t PALETTE[] = {
    BLOCK_STONE,    BLOCK_DIRT,    BLOCK_GRASS,       BLOCK_GRAVEL,  BLOCK_SAND,     BLOCK_SNOW,
    BLOCK_WOOD,     BLOCK_LEAVES,  BLOCK_PLANKS,      BLOCK_QUARTZ,  BLOCK_CONCRETE, BLOCK_GLASS,
    BLOCK_COAL_ORE, BLOCK_IRON_ORE, BLOCK_DIAMOND_ORE, BLOCK_GLOWSTONE, BLOCK_WATER, BLOCK_DEEPSTONE,
};
#define PALETTE_COUNT ((int)(sizeof PALETTE / sizeof PALETTE[0]))

static const char *block_name(block_t b) {
    switch (b) {
        case BLOCK_STONE:     return "stone";
        case BLOCK_DIRT:      return "dirt";
        case BLOCK_GRASS:     return "grass";
        case BLOCK_WOOD:      return "wood";
        case BLOCK_LEAVES:    return "leaves";
        case BLOCK_SAND:      return "sand";
        case BLOCK_GRAVEL:    return "gravel";
        case BLOCK_GLOWSTONE: return "glowstone";
        case BLOCK_PLANKS:    return "planks";
        case BLOCK_QUARTZ:    return "quartz";
        case BLOCK_CONCRETE:  return "concrete";
        case BLOCK_GLASS:     return "glass";
        case BLOCK_SNOW:      return "snow";
        case BLOCK_COAL_ORE:    return "coal_ore";
        case BLOCK_IRON_ORE:    return "iron_ore";
        case BLOCK_DIAMOND_ORE: return "diamond_ore";
        case BLOCK_DEEPSTONE: return "deepstone";
        case BLOCK_WATER:     return "water";
        default:              return "?";
    }
}

static void block_icon_tile(block_t b, int *tx, int *ty) {
    switch (b) {
        case BLOCK_STONE:       *tx = 0; *ty = 0; break;
        case BLOCK_DIRT:        *tx = 1; *ty = 0; break;
        case BLOCK_GRASS:       *tx = 3; *ty = 0; break;
        case BLOCK_WATER:       *tx = 0; *ty = 1; break;
        case BLOCK_GRAVEL:      *tx = 1; *ty = 1; break;
        case BLOCK_SAND:        *tx = 3; *ty = 1; break;
        case BLOCK_SNOW:        *tx = 1; *ty = 2; break;
        case BLOCK_WOOD:        *tx = 3; *ty = 2; break;
        case BLOCK_LEAVES:      *tx = 0; *ty = 3; break;
        case BLOCK_COAL_ORE:    *tx = 1; *ty = 3; break;
        case BLOCK_IRON_ORE:    *tx = 2; *ty = 3; break;
        case BLOCK_DIAMOND_ORE: *tx = 3; *ty = 3; break;
        case BLOCK_GLOWSTONE:   *tx = 4; *ty = 0; break;
        case BLOCK_PLANKS:      *tx = 4; *ty = 1; break;
        case BLOCK_QUARTZ:      *tx = 4; *ty = 2; break;
        case BLOCK_CONCRETE:    *tx = 4; *ty = 3; break;
        case BLOCK_GLASS:       *tx = 0; *ty = 4; break;
        default:                *tx = 0; *ty = 0; break;
    }
}

static int rect_hit(rect_t r, float mx, float my) {
    return mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h;
}

static void cursor_ui_pos(GLFWwindow *win, float *mx, float *my) {
    double cx = 0.0, cy = 0.0;
    glfwGetCursorPos(win, &cx, &cy);
    int ww = 0, wh = 0;
    glfwGetWindowSize(win, &ww, &wh);
    (void)ww;
    float h = (wh > 0) ? (float)wh : 1.0f;
    *mx = (float)cx / h;
    *my = (float)cy / h;
}

static int button_box(rect_t r, float aspect, float mx, float my) {
    int hover = rect_hit(r, mx, my);
    if (hover) {
        float sig = r.x * 137.0f + r.y;
        if (sig != hover_sig) { hover_sig = sig; audio_play_hover(audio); }
    }
    float base = hover ? 0.30f : 0.17f;
    ui_draw_rect(ui, r.x, r.y, r.w, r.h, base, base + 0.05f, base + 0.15f, 0.95f, aspect);
    float t  = 0.004f;
    float bc = hover ? 0.95f : 0.50f;
    ui_draw_rect(ui, r.x, r.y, r.w, t, bc, bc, bc, 0.85f, aspect);
    ui_draw_rect(ui, r.x, r.y + r.h - t, r.w, t, bc, bc, bc, 0.85f, aspect);
    ui_draw_rect(ui, r.x, r.y, t, r.h, bc, bc, bc, 0.85f, aspect);
    ui_draw_rect(ui, r.x + r.w - t, r.y, t, r.h, bc, bc, bc, 0.85f, aspect);
    int clicked = ui_click && rect_hit(r, ui_click_x, ui_click_y);
    if (clicked) audio_play_ui_click(audio);
    return clicked;
}

static int do_button(rect_t r, const char *label, float aspect, float mx, float my) {
    int clicked = button_box(r, aspect, mx, my);
    float scale = 0.040f;
    float tw = text_width(label, scale);
    text_draw(text, label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - text_height(scale)) * 0.5f,
              scale, 1.0f, 1.0f, 1.0f, 1.0f, aspect);
    return clicked;
}

static void draw_centered_text(const char *s, float y, float scale, float aspect,
                               float r, float g, float b, float a) {
    float w = text_width(s, scale);
    text_draw(text, s, aspect * 0.5f - w * 0.5f, y, scale, r, g, b, a, aspect);
}

static void compute_sky(float t, vec3 *sun_dir, float *sun_strength, float *ambient) {
    float sun_angle = t * 2.0f * PI - PI * 0.5f;
    sun_dir->x = cosf(sun_angle);
    sun_dir->y = sinf(sun_angle);
    sun_dir->z = 0.0f;
    *sun_strength = (sun_dir->y > 0.0f) ? sun_dir->y * 0.85f : 0.0f;
    *ambient = 0.22f + 0.30f * (sun_dir->y > 0.0f ? sun_dir->y : 0.0f);
}

static void sky_color(vec3 sun_dir, int underwater, float *r, float *g, float *b) {
    vec3 night_sky = { 0.04f, 0.05f, 0.12f };
    vec3 day_sky   = { 0.55f, 0.75f, 0.95f };
    vec3 dusk_sky  = { 0.85f, 0.55f, 0.40f };
    float day_factor = (sun_dir.y + 0.3f) / 0.6f;
    if (day_factor < 0.0f) day_factor = 0.0f;
    if (day_factor > 1.0f) day_factor = 1.0f;
    float dusk_factor = 1.0f - fabsf(sun_dir.y) / 0.3f;
    if (dusk_factor < 0.0f) dusk_factor = 0.0f;
    dusk_factor *= 0.55f;

    float sr = (1.0f - day_factor) * night_sky.x + day_factor * day_sky.x;
    float sg = (1.0f - day_factor) * night_sky.y + day_factor * day_sky.y;
    float sb = (1.0f - day_factor) * night_sky.z + day_factor * day_sky.z;
    sr = (1.0f - dusk_factor) * sr + dusk_factor * dusk_sky.x;
    sg = (1.0f - dusk_factor) * sg + dusk_factor * dusk_sky.y;
    sb = (1.0f - dusk_factor) * sb + dusk_factor * dusk_sky.z;
    if (underwater) { sr *= 0.35f; sg *= 0.55f; sb *= 0.85f; }
    *r = sr; *g = sg; *b = sb;
}

static vec3 vlerp(vec3 a, vec3 b, float t) {
    return vec3_add(a, vec3_scale(vec3_sub(b, a), t));
}

static vec3 biome_atmo_tint(biome_t b) {
    switch (b) {
        case BIOME_DESERT: return (vec3){1.10f, 1.02f, 0.84f};
        case BIOME_SNOW:   return (vec3){0.93f, 0.97f, 1.10f};
        case BIOME_FOREST: return (vec3){0.90f, 1.00f, 0.92f};
        default:           return (vec3){1.00f, 1.00f, 1.00f};
    }
}

static void compute_sky_colors(vec3 sun_dir, vec3 *zenith, vec3 *horizon, float *day) {
    float df = (sun_dir.y + 0.15f) / 0.40f;
    if (df < 0.0f) df = 0.0f;
    if (df > 1.0f) df = 1.0f;
    *day = df;

    vec3 zen_day = {0.20f, 0.45f, 0.85f}, hor_day = {0.62f, 0.78f, 0.95f};
    vec3 zen_nt  = {0.015f, 0.02f, 0.06f}, hor_nt  = {0.04f, 0.05f, 0.10f};
    vec3 z = vlerp(zen_nt, zen_day, df);
    vec3 h = vlerp(hor_nt, hor_day, df);

    float dusk = 1.0f - fabsf(sun_dir.y) / 0.25f;
    if (dusk < 0.0f) dusk = 0.0f;
    dusk *= 0.7f;
    vec3 dusk_col = {0.95f, 0.52f, 0.32f};
    h = vlerp(h, dusk_col, dusk);

    *zenith = z;
    *horizon = h;
}

static void roll_weather(double now) {
    int r = rand() % 100;
    double dur;
    if (r < 62) {
        weather_kind = WEATHER_CLEAR;
        dur = 60.0 + (double)(rand() % 120);
    } else {
        biome_t b = world_biome_at(world_seed(world),
                                   (int)floorf(player.pos.x), (int)floorf(player.pos.z));
        if (b == BIOME_SNOW)                       weather_kind = WEATHER_SNOW;
        else if (b == BIOME_DESERT && (rand() & 1)) weather_kind = WEATHER_CLEAR;
        else                                        weather_kind = WEATHER_RAIN;
        dur = 35.0 + (double)(rand() % 70);
    }
    weather_until = now + dur;
}

static void update_weather(double now, float dt) {
    if (!weather_manual && now >= weather_until) roll_weather(now);

    float ov_target = 0.0f, pr_target = 0.0f;
    if (weather_kind == WEATHER_RAIN)      { ov_target = 0.9f; pr_target = 1.0f; }
    else if (weather_kind == WEATHER_SNOW) { ov_target = 0.7f; pr_target = 1.0f; }
    if (weather_kind != WEATHER_CLEAR) weather_precip_type = weather_kind;

    float ks = 1.0f - expf(-0.25f * dt);
    weather_overcast += (ov_target - weather_overcast) * ks;

    vec3 eye = player_eye_pos(&player);
    int ex = (int)floorf(eye.x), ez = (int)floorf(eye.z);
    int covered = 0;
    for (int y = (int)floorf(eye.y) + 1; y <= (int)floorf(eye.y) + 16 && y < WORLD_HEIGHT; y++) {
        block_t b = world_get_block(world, ex, y, ez);
        if (b != BLOCK_AIR && b != BLOCK_LEAVES && b != BLOCK_GLASS && b != BLOCK_WATER) { covered = 1; break; }
    }
    weather_shelter += ((covered ? 1.0f : 0.0f) - weather_shelter) * (1.0f - expf(-4.0f * dt));

    float kp = 1.0f - expf(-0.7f * dt);
    float pr = pr_target * (1.0f - weather_shelter);
    weather_precip += (pr - weather_precip) * kp;

    int pmode = (weather_precip > 0.01f) ? weather_precip_type : WEATHER_CLEAR;
    weather_set(weather, pmode);
    weather_update(weather, eye, dt, weather_precip, world_seed(world));
}

static mat4 compute_light_vp(vec3 sun_dir) {
    float S    = (float)(settings.render_distance + 1) * (float)CHUNK_DIM;
    float dist = S + 32.0f;
    vec3 ldir  = vec3_normalize(sun_dir);
    vec3 up    = (fabsf(ldir.y) > 0.99f) ? (vec3){0.0f, 0.0f, 1.0f} : (vec3){0.0f, 1.0f, 0.0f};

    vec3 f = vec3_scale(ldir, -1.0f);
    vec3 s = vec3_normalize(vec3_cross(f, up));
    vec3 u = vec3_cross(s, f);

    vec3 center = { player.pos.x, player.pos.y, player.pos.z };
    float texel = (2.0f * S) / (float)SHADOW_MAP_SIZE;
    float cs = floorf(vec3_dot(center, s) / texel) * texel;
    float cu = floorf(vec3_dot(center, u) / texel) * texel;
    float cf = vec3_dot(center, f);
    center = vec3_add(vec3_add(vec3_scale(s, cs), vec3_scale(u, cu)), vec3_scale(f, cf));

    vec3 eye  = vec3_add(center, vec3_scale(ldir, dist));
    mat4 view = mat4_look_at(eye, center, up);
    mat4 proj = mat4_ortho(-S, S, -S, S, 1.0f, 2.0f * dist);
    return mat4_multiply(proj, view);
}

static mat4 render_shadow_map(vec3 sun_dir) {
    mat4 light_vp = compute_light_vp(sun_dir);
    framebuffer_bind(shadow_fb);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_CLAMP);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 4.0f);
    glUseProgram(depth_prog);
    world_render_depth(world, light_vp, depth_mvp_loc);
    glPolygonOffset(0.0f, 0.0f);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_CLAMP);
    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(g_win, &fbw, &fbh);
    framebuffer_unbind(fbw, fbh);
    return light_vp;
}

static void render_world_scene(float aspect) {
    vec3 eye      = player_eye_pos(&player);
    vec3 fwd      = player_forward(&player);
    vec3 world_up = { 0.0f, 1.0f, 0.0f };

    mat4 view = mat4_look_at(eye, vec3_add(eye, fwd), world_up);
    mat4 proj = mat4_perspective(settings.fov * PI / 180.0f, aspect, 0.1f, 500.0f);
    mat4 pv   = mat4_multiply(proj, view);

    block_t eye_block = world_get_block(world, (int)floorf(eye.x), (int)floorf(eye.y), (int)floorf(eye.z));
    int underwater = (eye_block == BLOCK_WATER);

    vec3 sun_dir;
    float sun_strength, ambient;
    compute_sky(world_time, &sun_dir, &sun_strength, &ambient);

    vec3 sky_zenith, sky_horizon;
    float day_factor;
    compute_sky_colors(sun_dir, &sky_zenith, &sky_horizon, &day_factor);

    float overcast = weather_overcast;
    sun_strength *= (1.0f - 0.78f * overcast);

    int  sun_up   = sun_dir.y > 0.05f;
    mat4 light_vp = mat4_identity();
    if (sun_up) light_vp = render_shadow_map(sun_dir);

    float sr, sg, sb;
    sky_color(sun_dir, underwater, &sr, &sg, &sb);
    glClearColor(sr, sg, sb, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(prog);
    glUniform3f(sun_dir_loc,      sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform1f(sun_strength_loc, sun_strength);
    glUniform1f(ambient_loc,      ambient);
    glUniform1f(sea_level_loc,    (float)SEA_LEVEL);
    glUniform1f(clip_height_loc,  WATER_SURFACE_Y);
    glUniform1i(eye_in_water_loc, underwater ? 1 : 0);
    glUniform1f(eye_depth_loc,    underwater ? ((float)SEA_LEVEL - eye.y) : 0.0f);
    float light_S = (float)(settings.render_distance + 1) * (float)CHUNK_DIM;
    glUniformMatrix4fv(light_vp_loc, 1, GL_FALSE, light_vp.m);
    glUniform1i(shadow_enabled_loc, sun_up ? 1 : 0);
    glUniform1f(shadow_texel_loc,   1.0f / (float)SHADOW_MAP_SIZE);
    glUniform1f(shadow_world_texel_loc, 2.0f * light_S / (float)SHADOW_MAP_SIZE);
    float day_amt = sun_dir.y * 2.5f;
    if (day_amt < 0.0f) day_amt = 0.0f;
    if (day_amt > 1.0f) day_amt = 1.0f;
    float fog_density = (1.6f - 0.9f * day_amt) / light_S;
    fog_density *= (1.0f + 0.9f * overcast);
    vec3 fog_c = { sky_horizon.x * atmo_tint.x,
                   sky_horizon.y * atmo_tint.y,
                   sky_horizon.z * atmo_tint.z };
    vec3 overcast_grey = { 0.70f, 0.72f, 0.76f };
    fog_c = vlerp(fog_c, overcast_grey, overcast * 0.5f);
    glUniform3f(eye_loc, eye.x, eye.y, eye.z);
    glUniform3f(fog_color_loc, fog_c.x, fog_c.y, fog_c.z);
    glUniform1f(fog_density_loc, fog_density);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, framebuffer_depth_tex(shadow_fb));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(shadow_map_loc, 3);
    glUniform1i(ssao_enabled_loc, 0);

    int blocklight_on = lighttex_valid(lighttex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, lighttex_texture(lighttex));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(blocklight_tex_loc, 2);
    glUniform1i(blocklight_enabled_loc, blocklight_on ? 1 : 0);
    if (blocklight_on) {
        float lox, loy, loz;
        lighttex_origin(lighttex, &lox, &loy, &loz);
        glUniform3f(light_origin_loc, lox, loy, loz);
        glUniform1f(light_dim_loc, (float)lighttex_dim(lighttex));
    }

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(g_win, &fbw, &fbh);

    int draw_water = !underwater;

    if (draw_water) {
        mat4 reflect = mat4_identity();
        reflect.m[5]  = -1.0f;
        reflect.m[13] = 2.0f * WATER_SURFACE_Y;
        mat4 reflected_pv = mat4_multiply(pv, reflect);

        glUniform1i(clip_below_loc, 1);
        framebuffer_bind(reflect_fb);
        glClearColor(sr, sg, sb, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        world_render_reflection(world, mvp_loc, chunk_origin_loc, reflected_pv);
        framebuffer_unbind(fbw, fbh);
        glUniform1i(clip_below_loc, 0);
    }

    framebuffer_bind(ssao_depth_fb);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(depth_prog);
    world_render_depth(world, pv, depth_mvp_loc);
    framebuffer_unbind(fbw, fbh);

    framebuffer_bind(ssao_fb);
    glUseProgram(ssao_prog);
    glUniform4f(ssao_proj_loc, proj.m[0], proj.m[5], proj.m[10], proj.m[14]);
    glUniform1f(ssao_radius_loc, 2.2f);
    glUniform1f(ssao_bias_loc, 0.06f);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, framebuffer_depth_tex(ssao_depth_fb));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(ssao_depth_loc, 5);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(fsq_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    framebuffer_unbind(fbw, fbh);

    framebuffer_bind(ssao_blur_fb);
    glUseProgram(ssao_blur_prog);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, framebuffer_color_tex(ssao_fb));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(ssao_blur_tex_loc, 5);
    glUniform2f(ssao_blur_texel_loc, 1.0f / (float)SSAO_W, 1.0f / (float)SSAO_H);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(fsq_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    framebuffer_unbind(fbw, fbh);

    if (!underwater) {
        vec3 cright = vec3_normalize(vec3_cross(fwd, world_up));
        vec3 cup    = vec3_cross(cright, fwd);
        float tanhf = tanf(settings.fov * PI / 180.0f * 0.5f);
        glUseProgram(sky_prog);
        glUniform3f(sky_right_loc, cright.x, cright.y, cright.z);
        glUniform3f(sky_up_loc, cup.x, cup.y, cup.z);
        glUniform3f(sky_fwd_loc, fwd.x, fwd.y, fwd.z);
        glUniform1f(sky_tanfov_loc, tanhf);
        glUniform1f(sky_aspect_loc, aspect);
        glUniform3f(sky_sun_loc, sun_dir.x, sun_dir.y, sun_dir.z);
        glUniform1f(sky_time_loc, (float)glfwGetTime());
        glUniform3f(sky_zenith_loc, sky_zenith.x, sky_zenith.y, sky_zenith.z);
        glUniform3f(sky_horizon_loc, sky_horizon.x, sky_horizon.y, sky_horizon.z);
        glUniform1f(sky_day_loc, day_factor);
        glUniform3f(sky_atmo_loc, atmo_tint.x, atmo_tint.y, atmo_tint.z);
        glUniform3f(sky_eye_loc, eye.x, eye.y, eye.z);
        glUniform1f(sky_overcast_loc, overcast);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(fsq_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    glUseProgram(prog);
    glUniform1i(clip_below_loc, 0);
    glUniform1i(ssao_enabled_loc, 1);
    glUniform2f(screen_loc, (float)fbw, (float)fbh);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, framebuffer_color_tex(ssao_blur_fb));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(ssao_tex_loc, 5);
    world_render(world, mvp_loc, chunk_origin_loc, pv, 1);

    if (draw_water) {
        glUseProgram(water_prog);
        glUniform1i(water_refl_loc, 4);
        glUniform3f(water_eye_loc, eye.x, eye.y, eye.z);
        glUniform1f(water_time_loc, (float)glfwGetTime());
        glUniform3f(water_sun_dir_loc, sun_dir.x, sun_dir.y, sun_dir.z);
        glUniform1f(water_sun_strength_loc, sun_strength);
        glUniform3f(water_fog_color_loc, fog_c.x, fog_c.y, fog_c.z);
        glUniform1f(water_fog_density_loc, fog_density);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, framebuffer_color_tex(reflect_fb));
        glActiveTexture(GL_TEXTURE0);
        world_render_water(world, water_mvp_loc, water_origin_loc, pv);
    }

    falling_render(falling, pv, sun_dir, sun_strength, ambient);

    if (!underwater) {
        glDepthMask(GL_FALSE);
        weather_render(weather, pv, day_factor);
        glDepthMask(GL_TRUE);
    }

    raycast_hit_t hit = raycast(world, eye, fwd, REACH);
    if (hit.hit) ui_draw_block_outline(ui, hit.x, hit.y, hit.z, pv);

    if (underwater) {
        float fade = (water_enter_time >= 0.0) ? (float)((glfwGetTime() - water_enter_time) / 0.25) : 1.0f;
        if (fade > 1.0f) fade = 1.0f;
        if (fade < 0.0f) fade = 0.0f;
        ui_draw_underwater(ui, aspect, (float)glfwGetTime(), fade);
    }

    if (debug_view != 0) {
        GLuint dbg = (debug_view == 1) ? framebuffer_color_tex(reflect_fb)
                                       : framebuffer_color_tex(ssao_blur_fb);
        glDisable(GL_DEPTH_TEST);
        glUseProgram(blit_prog);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, dbg);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(blit_tex_loc, 6);
        glBindVertexArray(fsq_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glEnable(GL_DEPTH_TEST);
    }
}

static void draw_hotbar(float aspect) {
    int n = HOTBAR_COUNT;
    float slot = 0.058f;
    float pad  = 0.007f;
    float total_w = (float)n * slot + (float)(n + 1) * pad;
    float x0 = aspect * 0.5f - total_w * 0.5f;
    float y0 = 1.0f - slot - 0.03f;

    for (int i = 0; i < n; i++) {
        float sx = x0 + pad + (float)i * (slot + pad);
        float sy = y0;

        ui_draw_rect(ui, sx, sy, slot, slot, 0.15f, 0.15f, 0.18f, 0.75f, aspect);

        if (hotbar[i] != BLOCK_AIR) {
            float inset = slot * 0.12f;
            int tx, ty;
            block_icon_tile(hotbar[i], &tx, &ty);
            ui_draw_atlas_icon(ui, tx, ty, sx + inset, sy + inset, slot - 2.0f * inset, aspect);
            if (world_gamemode(world) == GAMEMODE_SURVIVAL && hotbar_count[i] > 0) {
                char cnt[8];
                snprintf(cnt, sizeof cnt, "%d", hotbar_count[i]);
                text_draw(text, cnt, sx + slot - 0.026f, sy + slot - 0.022f, 0.016f,
                          1.0f, 1.0f, 1.0f, 0.95f, aspect);
            }
        }

        if (i == hotbar_sel) {
            float t = 0.003f;
            ui_draw_rect(ui, sx - t, sy - t, slot + 2.0f * t, t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
            ui_draw_rect(ui, sx - t, sy + slot, slot + 2.0f * t, t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
            ui_draw_rect(ui, sx - t, sy - t, t, slot + 2.0f * t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
            ui_draw_rect(ui, sx + slot, sy - t, t, slot + 2.0f * t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
        }

        char num[2] = { (char)(i < 9 ? '1' + i : '0'), 0 };
        text_draw(text, num, sx + 0.004f, sy + 0.003f, 0.020f, 1.0f, 1.0f, 1.0f, 0.85f, aspect);
    }

    static int    name_block = -1;
    static double name_since = 0.0;
    double now = glfwGetTime();
    if (player.selected_block != name_block) {
        name_block = player.selected_block;
        name_since = now;
    }
    if (player.selected_block != BLOCK_AIR) {
        double elapsed = now - name_since;
        float a = (elapsed < 3.0) ? 0.95f : 0.95f * (float)(1.0 - (elapsed - 3.0) / 0.6);
        if (a > 0.0f) {
            const char *nm = block_name((block_t)player.selected_block);
            float ns = 0.03f;
            float nw = text_width(nm, ns);
            text_draw(text, nm, aspect * 0.5f - nw * 0.5f, y0 - pad - 0.045f, ns,
                      1.0f, 1.0f, 1.0f, a, aspect);
        }
    }
}

static void settings_apply(void) {
    player_set_sensitivity(settings.mouse_sens);
    if (audio) audio_set_volume(audio, settings.volume);
    fps_shown = settings.fps_overlay;
}

static void settings_clamp(void) {
    if (settings.fov < 40.0f) settings.fov = 40.0f;
    if (settings.fov > 110.0f) settings.fov = 110.0f;
    if (settings.mouse_sens < 0.0005f) settings.mouse_sens = 0.0005f;
    if (settings.mouse_sens > 0.0060f) settings.mouse_sens = 0.0060f;
    if (settings.volume < 0.0f) settings.volume = 0.0f;
    if (settings.volume > 1.0f) settings.volume = 1.0f;
    if (settings.render_distance < 2) settings.render_distance = 2;
    if (settings.render_distance > 8) settings.render_distance = 8;
    settings.fps_overlay = settings.fps_overlay ? 1 : 0;
}

static void settings_load(void) {
    settings = SETTINGS_DEFAULTS;
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;
        size_t vl = strlen(val);
        while (vl > 0 && (val[vl - 1] == '\n' || val[vl - 1] == '\r')) val[--vl] = '\0';
        if      (strcmp(key, "fov") == 0)             settings.fov = (float)atof(val);
        else if (strcmp(key, "mouse_sens") == 0)      settings.mouse_sens = (float)atof(val);
        else if (strcmp(key, "volume") == 0)          settings.volume = (float)atof(val);
        else if (strcmp(key, "render_distance") == 0) settings.render_distance = atoi(val);
        else if (strcmp(key, "fps_overlay") == 0)     settings.fps_overlay = atoi(val);
        else if (strcmp(key, "texture_pack") == 0)    snprintf(settings.texture_pack, sizeof settings.texture_pack, "%s", val);
        else if (strcmp(key, "audio_pack") == 0)      snprintf(settings.audio_pack, sizeof settings.audio_pack, "%s", val);
    }
    fclose(f);
    settings_clamp();
}

static void settings_save(void) {
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    if (fabsf(settings.fov - SETTINGS_DEFAULTS.fov) > 0.001f)              fprintf(f, "fov=%g\n", (double)settings.fov);
    if (fabsf(settings.mouse_sens - SETTINGS_DEFAULTS.mouse_sens) > 1e-6f) fprintf(f, "mouse_sens=%g\n", (double)settings.mouse_sens);
    if (fabsf(settings.volume - SETTINGS_DEFAULTS.volume) > 1e-4f)         fprintf(f, "volume=%g\n", (double)settings.volume);
    if (settings.render_distance != SETTINGS_DEFAULTS.render_distance) fprintf(f, "render_distance=%d\n", settings.render_distance);
    if (settings.fps_overlay != SETTINGS_DEFAULTS.fps_overlay)         fprintf(f, "fps_overlay=%d\n", settings.fps_overlay);
    if (settings.texture_pack[0])                                      fprintf(f, "texture_pack=%s\n", settings.texture_pack);
    if (settings.audio_pack[0])                                        fprintf(f, "audio_pack=%s\n", settings.audio_pack);
    fclose(f);
}

static void write_file_if_missing(const char *path, const char *content) {
    struct stat st;
    if (stat(path, &st) == 0) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

static void ensure_storage_dirs(void) {
    pf_mkdir(BW_DIR);
    pf_mkdir(SAVES_DIR);
    pf_mkdir(SKINS_DIR);
    pf_mkdir(AUDIO_DIR);
    FILE *f = fopen(SETTINGS_PATH, "a");
    if (f) fclose(f);

    write_file_if_missing(SKINS_DIR "/README.txt",
        "Texture packs\n"
        "Drop an 80x80 RGBA PNG here. It is a 5x5 grid of 16x16 tiles using the\n"
        "same layout as the built-in atlas; alpha/transparency is supported.\n"
        "Each .png becomes selectable in-game via Title > Texture Pack.\n");

    char audio_readme[1024];
    int off = snprintf(audio_readme, sizeof audio_readme,
        "Audio packs\n"
        "Create a subdirectory here (its folder name is the pack name) holding\n"
        "EXACTLY these %d .wav files (16-bit PCM or 32-bit float). A pack with any\n"
        "missing or extra .wav is rejected (shown as a red error in-game):\n",
        audio_wav_count());
    for (int i = 0; i < audio_wav_count(); i++) {
        if (off < 0 || (size_t)off >= sizeof audio_readme) break;
        off += snprintf(audio_readme + off, sizeof audio_readme - (size_t)off,
                        "  %s\n", audio_wav_name(i));
    }
    write_file_if_missing(AUDIO_DIR "/README.txt", audio_readme);
}

static void rmtree(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.' &&
                (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0'))) continue;
            char p[320];
            snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
            remove(p);
        }
        closedir(d);
    }
    pf_rmdir(dir);
}

static int unique_world_dir(const char *slug, char *out, size_t cap) {
    snprintf(out, cap, "%s/%s", SAVES_DIR, slug);
    struct stat st;
    for (int n = 2; n < 1000 && stat(out, &st) == 0; n++) {
        snprintf(out, cap, "%s/%s-%d", SAVES_DIR, slug, n);
    }
    return pf_mkdir(out) == 0;
}

static void scan_worlds(void) {
    world_count = 0;
    DIR *d = opendir(SAVES_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && world_count < MAX_WORLDS) {
        const char *fn = e->d_name;
        if (fn[0] == '.') continue;
        char dir[192], wpath[192];
        snprintf(dir, sizeof dir, "%s/%s", SAVES_DIR, fn);
        snprintf(wpath, sizeof wpath, "%s/world.bw", dir);
        world_meta_t meta;
        if (!world_peek_meta(wpath, &meta)) continue;
        snprintf(world_list[world_count].path, sizeof world_list[world_count].path, "%s", wpath);
        snprintf(world_list[world_count].dir,  sizeof world_list[world_count].dir,  "%s", dir);
        world_list[world_count].meta = meta;
        world_count++;
    }
    closedir(d);
}

static void world_slug(const char *name, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; name[i] && j + 1 < cap; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[j++] = c;
        else if (c == ' ' || c == '-' || c == '_') out[j++] = '_';
    }
    if (j == 0 && cap > 6) {
        out[j++] = 'w'; out[j++] = 'o'; out[j++] = 'r'; out[j++] = 'l'; out[j++] = 'd';
    }
    out[j] = '\0';
}

static uint32_t random_seed(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    uint32_t h = (uint32_t)ts.tv_sec
               ^ ((uint32_t)ts.tv_nsec * 2654435761u)
               ^ ((uint32_t)rand() << 1);
    h ^= h >> 16;
    h *= 0x9E3779B9u;
    h ^= h >> 16;
    return h ? h : 1u;
}

static uint32_t parse_seed(const char *s, const char *name) {
    (void)name;
    if (s[0] == '\0') return random_seed();
    int numeric = 1;
    for (size_t i = 0; s[i]; i++) if (s[i] < '0' || s[i] > '9') { numeric = 0; break; }
    if (numeric) return (uint32_t)strtoul(s, NULL, 10);
    uint32_t h = 2166136261u;
    for (size_t i = 0; s[i]; i++) { h ^= (uint32_t)(unsigned char)s[i]; h *= 16777619u; }
    return h;
}

static void save_game(void) {
    if (!world) return;
    save_player_t out = {
        .pos_x          = player.pos.x,
        .pos_y          = player.pos.y,
        .pos_z          = player.pos.z,
        .yaw            = player.yaw,
        .pitch          = player.pitch,
        .mode           = (player.mode == PLAYER_FLYING) ? 1 : 0,
        .selected_block = player.selected_block,
        .world_time     = world_time,
    };
    out.hotbar_sel = hotbar_sel;
    for (int i = 0; i < HOTBAR_COUNT; i++) {
        out.hotbar_block[i] = (uint8_t)hotbar[i];
        out.hotbar_count[i] = hotbar_count[i];
    }
    for (int i = 0; i < PACK_COUNT; i++) {
        out.pack_block[i] = (uint8_t)pack[i];
        out.pack_count[i] = pack_count[i];
    }
    world_save(world, &out, save_path);
}

static void apply_gamemode(int gamemode) {
    player_set_allow_fly(gamemode == GAMEMODE_CREATIVE);
    if (gamemode == GAMEMODE_SURVIVAL && player.mode == PLAYER_FLYING) {
        player.mode = PLAYER_WALKING;
    }
}

static void begin_loading(void) {
    game_state = GS_LOADING;
    loading_started = 0;
    loading_peak = 0;
    prev_underwater = -1;
    water_enter_time = -1.0;

    weather_manual   = 0;
    weather_kind     = WEATHER_CLEAR;
    weather_overcast = 0.0f;
    weather_precip   = 0.0f;
    weather_shelter  = 0.0f;
    weather_until    = glfwGetTime() + 45.0;
    weather_set(weather, WEATHER_CLEAR);
    lighttex_invalidate(lighttex);
}

static void spawn_player(uint32_t seed) {
    int spawn_h = world_height_at(seed, 0, 0);
    if (spawn_h < SEA_LEVEL) spawn_h = SEA_LEVEL;
    player_init(&player, (vec3){0.5f, (float)(spawn_h + 3), 0.5f});
}

static int enter_world_load(const char *path) {
    world_meta_t meta;
    if (!world_peek_meta(path, &meta)) return 0;
    world = world_create(meta.seed, settings.render_distance);
    if (!world) return 0;
    world_set_falling_system(world, falling);
    spawn_player(meta.seed);
    save_player_t saved = {0};
    if (world_load_into(world, &saved, path)) {
        player.pos.x          = saved.pos_x;
        player.pos.y          = saved.pos_y;
        player.pos.z          = saved.pos_z;
        player.yaw            = saved.yaw;
        player.pitch          = saved.pitch;
        player.mode           = (saved.mode == 1) ? PLAYER_FLYING : PLAYER_WALKING;
        player.selected_block = saved.selected_block;
        world_time            = saved.world_time;
    }
    if (saved.has_inventory) {
        for (int i = 0; i < HOTBAR_COUNT; i++) {
            hotbar[i]       = (block_t)saved.hotbar_block[i];
            hotbar_count[i] = saved.hotbar_count[i];
        }
        for (int i = 0; i < PACK_COUNT; i++) {
            pack[i]       = (block_t)saved.pack_block[i];
            pack_count[i] = saved.pack_count[i];
        }
        hotbar_sel = (saved.hotbar_sel >= 0 && saved.hotbar_sel < HOTBAR_COUNT) ? saved.hotbar_sel : 0;
        player.selected_block = hotbar[hotbar_sel];
    } else {
        reset_hotbar((block_t)player.selected_block);
    }
    snprintf(save_path, sizeof save_path, "%s", path);
    apply_gamemode(world_gamemode(world));
    begin_loading();
    return 1;
}

static int enter_world_new(const char *name, uint32_t seed, int gamemode, int allow_commands) {
    world = world_create(seed, settings.render_distance);
    if (!world) return 0;
    world_set_meta(world, name, gamemode, allow_commands);
    world_set_falling_system(world, falling);
    spawn_player(seed);
    reset_hotbar(BLOCK_AIR);
    world_time = 0.30f;
    char slug[64];
    world_slug(name, slug, sizeof slug);
    char dir[192];
    if (!unique_world_dir(slug, dir, sizeof dir)) {
        fprintf(stderr, "world: could not create save directory under %s\n", SAVES_DIR);
        world_destroy(world);
        world = NULL;
        return 0;
    }
    snprintf(save_path, sizeof save_path, "%s/world.bw", dir);
    apply_gamemode(gamemode);
    begin_loading();
    return 1;
}

static void quit_to_title(int save) {
    if (world) {
        if (save) save_game();
        world_destroy(world);
        world = NULL;
    }
    if (console) console_close(console);
    console_open_key = -1;
    prev_underwater = -1;
    water_enter_time = -1.0;
    game_state = GS_TITLE;
    scan_worlds();
    glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

static int block_from_name(const char *s) {
    if (!strcmp(s, "air"))       return BLOCK_AIR;
    if (!strcmp(s, "stone"))     return BLOCK_STONE;
    if (!strcmp(s, "dirt"))      return BLOCK_DIRT;
    if (!strcmp(s, "grass"))     return BLOCK_GRASS;
    if (!strcmp(s, "water"))     return BLOCK_WATER;
    if (!strcmp(s, "gravel"))    return BLOCK_GRAVEL;
    if (!strcmp(s, "deepstone")) return BLOCK_DEEPSTONE;
    if (!strcmp(s, "sand"))      return BLOCK_SAND;
    if (!strcmp(s, "snow"))      return BLOCK_SNOW;
    if (!strcmp(s, "wood"))      return BLOCK_WOOD;
    if (!strcmp(s, "leaves"))    return BLOCK_LEAVES;
    if (!strcmp(s, "coal") || !strcmp(s, "coal_ore"))       return BLOCK_COAL_ORE;
    if (!strcmp(s, "iron") || !strcmp(s, "iron_ore"))       return BLOCK_IRON_ORE;
    if (!strcmp(s, "diamond") || !strcmp(s, "diamond_ore")) return BLOCK_DIAMOND_ORE;
    if (!strcmp(s, "glowstone")) return BLOCK_GLOWSTONE;
    if (!strcmp(s, "planks"))    return BLOCK_PLANKS;
    if (!strcmp(s, "quartz"))    return BLOCK_QUARTZ;
    if (!strcmp(s, "concrete"))  return BLOCK_CONCRETE;
    if (!strcmp(s, "glass"))     return BLOCK_GLASS;
    return -1;
}

static void con_printf(const char *fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    console_print(console, buf);
}

static int tokenize(char *s, char **argv, int max) {
    int n = 0;
    char *p = s;
    while (*p && n < max) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[n++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

static int parse_time_keyword(const char *s, float *out) {
    if (!strcmp(s, "midnight")) { *out = 0.00f; return 1; }
    if (!strcmp(s, "sunrise"))  { *out = 0.25f; return 1; }
    if (!strcmp(s, "day"))      { *out = 0.30f; return 1; }
    if (!strcmp(s, "noon"))     { *out = 0.50f; return 1; }
    if (!strcmp(s, "sunset"))   { *out = 0.75f; return 1; }
    if (!strcmp(s, "night"))    { *out = 0.85f; return 1; }
    return 0;
}

static void run_command(int argc, char **argv) {
    const char *cmd = argv[0];
    if (!strcmp(cmd, "help")) {
        con_printf("Commands:");
        for (int i = 0; i < CMD_COUNT; i++) con_printf("  %s", CMD_USAGE[i]);
    } else if (!strcmp(cmd, "clear")) {
        console_clear(console);
    } else if (!strcmp(cmd, "seed")) {
        con_printf("Seed: %u", world_seed(world));
    } else if (!strcmp(cmd, "time")) {
        if (argc >= 3 && !strcmp(argv[1], "set")) {
            float t;
            if (parse_time_keyword(argv[2], &t)) {
                world_time = t;
            } else {
                world_time = (float)atoi(argv[2]) / 24000.0f + 0.25f;
                world_time -= floorf(world_time);
            }
            con_printf("Time set");
        } else if (argc >= 3 && !strcmp(argv[1], "add")) {
            world_time += (float)atoi(argv[2]) / 24000.0f;
            world_time -= floorf(world_time);
            con_printf("Time advanced");
        } else {
            con_printf("usage: /time set <day|noon|night|midnight>");
        }
    } else if (!strcmp(cmd, "tp")) {
        if (argc >= 4) {
            player.pos = (vec3){ (float)atof(argv[1]), (float)atof(argv[2]), (float)atof(argv[3]) };
            player.vel = (vec3){ 0.0f, 0.0f, 0.0f };
            con_printf("Teleported to %s %s %s", argv[1], argv[2], argv[3]);
        } else {
            con_printf("usage: /tp <x> <y> <z>");
        }
    } else if (!strcmp(cmd, "gamemode")) {
        int gm = -1;
        if (argc >= 2 && !strcmp(argv[1], "creative")) gm = GAMEMODE_CREATIVE;
        if (argc >= 2 && !strcmp(argv[1], "survival")) gm = GAMEMODE_SURVIVAL;
        if (gm < 0) {
            con_printf("usage: /gamemode <creative|survival>");
        } else {
            world_set_gamemode(world, gm);
            apply_gamemode(gm);
            con_printf("Gamemode: %s", gm == GAMEMODE_SURVIVAL ? "survival" : "creative");
        }
    } else if (!strcmp(cmd, "give")) {
        if (argc >= 2) {
            int b = block_from_name(argv[1]);
            if (b < 0) con_printf("Unknown block: %s", argv[1]);
            else { player.selected_block = b; con_printf("Now holding %s", argv[1]); }
        } else {
            con_printf("usage: /give <block>");
        }
    } else if (!strcmp(cmd, "weather")) {
        if (argc >= 2 && !strcmp(argv[1], "clear")) {
            weather_manual = 1; weather_kind = WEATHER_CLEAR;
            weather_until = glfwGetTime() + 600.0; con_printf("Weather: clear");
        } else if (argc >= 2 && !strcmp(argv[1], "rain")) {
            weather_manual = 1; weather_kind = WEATHER_RAIN;
            weather_until = glfwGetTime() + 600.0; con_printf("Weather: rain");
        } else if (argc >= 2 && !strcmp(argv[1], "snow")) {
            weather_manual = 1; weather_kind = WEATHER_SNOW;
            weather_until = glfwGetTime() + 600.0; con_printf("Weather: snow");
        } else if (argc >= 2 && !strcmp(argv[1], "auto")) {
            weather_manual = 0; weather_until = glfwGetTime(); con_printf("Weather: auto");
        } else {
            con_printf("usage: /weather <clear|rain|snow|auto>");
        }
    } else if (!strcmp(cmd, "fly")) {
        if (argc >= 2 && !strcmp(argv[1], "on")) {
            player_set_allow_fly(1);
            player.mode = PLAYER_FLYING;
            player.vel.y = 0.0f;
            con_printf("Fly enabled");
        } else if (argc >= 2 && !strcmp(argv[1], "off")) {
            player.mode = PLAYER_WALKING;
            player_set_allow_fly(world_gamemode(world) == GAMEMODE_CREATIVE);
            con_printf("Fly disabled");
        } else {
            con_printf("usage: /fly <on|off>");
        }
    } else {
        con_printf("Unknown command: %s  (try /help)", cmd);
    }
}

static void execute_console_line(const char *line) {
    if (line[0] != '/') {
        if (line[0]) console_print(console, line);
        return;
    }
    if (!world_allow_commands(world)) {
        console_print(console, "Commands are not allowed in this world");
        return;
    }
    char buf[256];
    snprintf(buf, sizeof buf, "%s", line + 1);
    char *argv[16];
    int argc = tokenize(buf, argv, 16);
    if (argc == 0) return;
    run_command(argc, argv);
}

static void glfw_error_cb(int code, const char *desc) {
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

static int is_menu_state(void) {
    return game_state == GS_TITLE || game_state == GS_CREATE ||
           game_state == GS_SETTINGS || game_state == GS_PAUSED ||
           game_state == GS_INVENTORY || game_state == GS_TEXPACK ||
           game_state == GS_AUDIOPACK;
}

static void close_inventory(void) {
    game_state = GS_PLAYING;
    glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    mouse_initialized = 0;
}

static void key_cb(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)scancode;
    if (game_state == GS_PLAYING && console_is_open(console)) {
        char submitted[256];
        if (console_key(console, key, action, submitted, (int)sizeof submitted)) {
            execute_console_line(submitted);
        }
        if (!console_is_open(console)) {
            console_open_key = -1;
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouse_initialized = 0;
        }
        return;
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (game_state == GS_PLAYING) {
            game_state = GS_PAUSED;
            pause_capture_pending = 1;
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        } else if (game_state == GS_PAUSED) {
            game_state = GS_PLAYING;
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouse_initialized = 0;
        } else if (game_state == GS_CREATE) {
            game_state = GS_TITLE;
        } else if (game_state == GS_SETTINGS) {
            settings_save();
            game_state = settings_return;
        } else if (game_state == GS_INVENTORY) {
            close_inventory();
        } else if (game_state == GS_TEXPACK) {
            leave_texpack();
        } else if (game_state == GS_AUDIOPACK) {
            game_state = GS_TITLE;
        } else if (game_state == GS_TITLE) {
            glfwSetWindowShouldClose(win, 1);
        }
    }
    if (game_state == GS_INVENTORY && key == GLFW_KEY_E && action == GLFW_PRESS) {
        audio_play_ui_click(audio);
        close_inventory();
    }
    if (game_state == GS_CREATE && key == GLFW_KEY_BACKSPACE &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        char *fld = (field_focus == 0) ? field_name : field_seed;
        size_t L = strlen(fld);
        if (L > 0) fld[L - 1] = '\0';
    }
    if (key == GLFW_KEY_F && action == GLFW_PRESS && game_state == GS_PLAYING) {
        fps_shown = !fps_shown;
    }
    if (key == GLFW_KEY_G && action == GLFW_PRESS && game_state == GS_PLAYING) {
        debug_view = (debug_view + 1) % 3;
    }
    if (game_state != GS_PLAYING) return;
    if (key == GLFW_KEY_T && action == GLFW_PRESS) {
        console_open_key = GLFW_KEY_T;
        console_open(console, "");
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }
    int plain = !(mods & (GLFW_MOD_SHIFT | GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER));
    if (key == GLFW_KEY_SLASH && action == GLFW_PRESS && plain) {
        console_open_key = GLFW_KEY_SLASH;
        console_open(console, "/");
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        if (player_on_space_press(&player)) audio_play_jump(audio);
    }
    if (action == GLFW_PRESS && key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
        hotbar_select(key - GLFW_KEY_1);
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_0) {
        hotbar_select(9);
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_E) {
        game_state = GS_INVENTORY;
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        audio_play_ui_click(audio);
    }
}

static void char_cb(GLFWwindow *win, unsigned int codepoint) {
    if (game_state == GS_PLAYING && console_is_open(console)) {
        if (console_open_key != -1) {
            if (glfwGetKey(win, console_open_key) == GLFW_PRESS) return;
            console_open_key = -1;
        }
        console_char(console, codepoint);
        return;
    }
    (void)win;
    if (game_state != GS_CREATE) return;
    if (codepoint < 0x20 || codepoint > 0x7e) return;
    char c = (char)codepoint;
    if (field_focus == 0) {
        size_t L = strlen(field_name);
        if (L + 1 < sizeof field_name) { field_name[L] = c; field_name[L + 1] = '\0'; }
    } else {
        size_t L = strlen(field_seed);
        if (L + 1 < sizeof field_seed) { field_seed[L] = c; field_seed[L + 1] = '\0'; }
    }
}

static void mouse_button_cb(GLFWwindow *win, int button, int action, int mods) {
    (void)mods;
    if (action != GLFW_PRESS) return;

    if (is_menu_state()) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            cursor_ui_pos(win, &ui_click_x, &ui_click_y);
            ui_click = 1;
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            cursor_ui_pos(win, &ui_rclick_x, &ui_rclick_y);
            ui_rclick = 1;
        }
        return;
    }
    if (game_state != GS_PLAYING) return;
    if (console_is_open(console)) return;

    vec3 eye = player_eye_pos(&player);
    vec3 dir = player_forward(&player);
    raycast_hit_t hit = raycast(world, eye, dir, REACH);
    if (!hit.hit) return;

    int survival = (world_gamemode(world) == GAMEMODE_SURVIVAL);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        block_t broken = world_get_block(world, hit.x, hit.y, hit.z);
        if (broken == BLOCK_DEEPSTONE) return;
        world_set_block(world, hit.x, hit.y, hit.z, BLOCK_AIR);
        lighttex_mark_dirty(lighttex);
        audio_play_break(audio, broken);
        if (survival && inventory_add(broken)) audio_play_pickup(audio);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (player.selected_block == BLOCK_AIR) return;
        if (survival && (hotbar[hotbar_sel] == BLOCK_AIR || hotbar_count[hotbar_sel] <= 0)) return;
        int px = hit.x + hit.face_x;
        int py = hit.y + hit.face_y;
        int pz = hit.z + hit.face_z;
        if (world_get_block(world, px, py, pz) != BLOCK_AIR &&
            world_get_block(world, px, py, pz) != BLOCK_WATER) return;
        float fminx = (float)px, fminy = (float)py, fminz = (float)pz;
        float fmaxx = fminx + 1.0f, fmaxy = fminy + 1.0f, fmaxz = fminz + 1.0f;
        float pminx = player.pos.x - 0.3f, pmaxx = player.pos.x + 0.3f;
        float pminy = player.pos.y, pmaxy = player.pos.y + 1.8f;
        float pminz = player.pos.z - 0.3f, pmaxz = player.pos.z + 0.3f;
        int overlap = !(fmaxx <= pminx || fminx >= pmaxx ||
                        fmaxy <= pminy || fminy >= pmaxy ||
                        fmaxz <= pminz || fminz >= pmaxz);
        if (overlap) return;
        world_set_block(world, px, py, pz, (block_t)player.selected_block);
        lighttex_mark_dirty(lighttex);
        audio_play_place(audio, (block_t)player.selected_block);
        if (survival) inventory_consume_selected();
    }
}

static void framebuffer_size_cb(GLFWwindow *win, int w, int h) {
    (void)win;
    glViewport(0, 0, w, h);
}

static void cursor_pos_cb(GLFWwindow *win, double x, double y) {
    (void)win;
    if (game_state != GS_PLAYING) return;
    if (console_is_open(console)) return;
    if (!mouse_initialized) {
        last_mouse_x = x;
        last_mouse_y = y;
        mouse_initialized = 1;
        return;
    }
    double dx = x - last_mouse_x;
    double dy = y - last_mouse_y;
    last_mouse_x = x;
    last_mouse_y = y;
    player_add_look(&player, (float)dx, (float)dy);
}

static char *read_text_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "failed to open %s\n", path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)size, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

static GLuint compile_shader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "shader compile failed:\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "program link failed:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint load_program(const char *vp, const char *fp) {
    char *vs = read_text_file(vp);
    char *fs = read_text_file(fp);
    if (!vs || !fs) { free(vs); free(fs); return 0; }
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    free(vs); free(fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = link_program(v, f);
    glDeleteShader(v); glDeleteShader(f);
    return p;
}

static void scroll_cb(GLFWwindow *win, double xoff, double yoff) {
    (void)win; (void)xoff;
    if (game_state != GS_PLAYING) return;
    if (yoff > 0.0)      hotbar_select((hotbar_sel + HOTBAR_COUNT - 1) % HOTBAR_COUNT);
    else if (yoff < 0.0) hotbar_select((hotbar_sel + 1) % HOTBAR_COUNT);
}

static int dir_count_png(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *fn = e->d_name;
        size_t L = strlen(fn);
        if (L >= 5 && strcasecmp(fn + L - 4, ".png") == 0) n++;
    }
    closedir(d);
    return n;
}

static int dir_count_subdirs(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char p[224];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) n++;
    }
    closedir(d);
    return n;
}

static void update_pack_availability(void) {
    have_skins = dir_count_png(SKINS_DIR) > 0;
    have_audiopacks = dir_count_subdirs(AUDIO_DIR) > 0;
}

static void free_skin_previews(void) {
    for (int i = 0; i < skin_count; i++) {
        if (skin_list[i].preview) { glDeleteTextures(1, &skin_list[i].preview); skin_list[i].preview = 0; }
    }
    if (default_preview) { glDeleteTextures(1, &default_preview); default_preview = 0; }
    skin_count = 0;
}

static void scan_skins(void) {
    free_skin_previews();
    default_preview = texture_create_atlas("");
    DIR *d = opendir(SKINS_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && skin_count < MAX_PACKS) {
        const char *fn = e->d_name;
        size_t L = strlen(fn);
        if (L < 5 || strcasecmp(fn + L - 4, ".png") != 0) continue;
        skin_entry_t *s = &skin_list[skin_count];
        snprintf(s->file, sizeof s->file, "%s", fn);
        snprintf(s->name, sizeof s->name, "%.*s", (int)(L - 4), fn);
        snprintf(s->path, sizeof s->path, "%s/%s", SKINS_DIR, fn);
        s->preview = texture_create_atlas(s->path);
        s->valid = s->preview != 0;
        skin_count++;
    }
    closedir(d);
}

static int audio_pack_valid(const char *dir) {
    int reqn = audio_wav_count();
    int found[32];
    for (int i = 0; i < reqn && i < 32; i++) found[i] = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int extra = 0;
    while ((e = readdir(d)) != NULL) {
        const char *fn = e->d_name;
        if (fn[0] == '.') continue;
        size_t L = strlen(fn);
        if (L < 5 || strcasecmp(fn + L - 4, ".wav") != 0) continue;
        int match = -1;
        for (int i = 0; i < reqn; i++) {
            if (strcasecmp(fn, audio_wav_name(i)) == 0) { match = i; break; }
        }
        if (match < 0) extra = 1;
        else if (match < 32) found[match] = 1;
    }
    closedir(d);
    if (extra) return 0;
    for (int i = 0; i < reqn && i < 32; i++) if (!found[i]) return 0;
    return 1;
}

static void scan_audio_packs(void) {
    audiopack_count = 0;
    DIR *d = opendir(AUDIO_DIR);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && audiopack_count < MAX_PACKS) {
        if (e->d_name[0] == '.') continue;
        char p[224];
        snprintf(p, sizeof p, "%s/%s", AUDIO_DIR, e->d_name);
        struct stat st;
        if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        audiopack_entry_t *a = &audiopack_list[audiopack_count];
        snprintf(a->name, sizeof a->name, "%s", e->d_name);
        snprintf(a->path, sizeof a->path, "%s", p);
        a->valid = audio_pack_valid(p);
        audiopack_count++;
    }
    closedir(d);
}

static void apply_texture_pack(const char *src) {
    GLuint t = texture_create_atlas(src);
    if (!t) return;
    if (atlas_tex) glDeleteTextures(1, &atlas_tex);
    atlas_tex = t;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex);
}

static void draw_select_frame(rect_t r, float aspect) {
    float t = 0.005f;
    ui_draw_rect(ui, r.x, r.y, r.w, t, 0.25f, 0.95f, 0.35f, 0.95f, aspect);
    ui_draw_rect(ui, r.x, r.y + r.h - t, r.w, t, 0.25f, 0.95f, 0.35f, 0.95f, aspect);
    ui_draw_rect(ui, r.x, r.y, t, r.h, 0.25f, 0.95f, 0.35f, 0.95f, aspect);
    ui_draw_rect(ui, r.x + r.w - t, r.y, t, r.h, 0.25f, 0.95f, 0.35f, 0.95f, aspect);
}

static void draw_pack_preview(GLuint tex, float x, float y, float size, float aspect) {
    if (!tex) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    ui_draw_atlas_icon(ui, 0, 0, x, y, size, aspect);
    glBindTexture(GL_TEXTURE_2D, atlas_tex);
}

static void leave_texpack(void) {
    free_skin_previews();
    game_state = GS_TITLE;
}

static void draw_texpack_screen(float aspect, float mx, float my) {
    glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw_centered_text("TEXTURE PACK", 0.09f, 0.06f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);

    float cx = aspect * 0.5f;
    float rw = 0.5f, rh = 0.068f, gap = 0.012f;
    float ps = rh - 0.016f;
    float y = 0.22f;

    rect_t dr = { cx - rw * 0.5f, y, rw, rh };
    int dclick = button_box(dr, aspect, mx, my);
    if (settings.texture_pack[0] == '\0') draw_select_frame(dr, aspect);
    draw_pack_preview(default_preview, dr.x + 0.008f, dr.y + (rh - ps) * 0.5f, ps, aspect);
    text_draw(text, "Default (built-in)", dr.x + ps + 0.022f, dr.y + (rh - 0.030f) * 0.5f,
              0.030f, 1.0f, 1.0f, 1.0f, 1.0f, aspect);
    if (dclick) { settings.texture_pack[0] = '\0'; apply_texture_pack(""); settings_save(); return; }
    y += rh + gap;

    int shown = 0, more = 0;
    for (int i = 0; i < skin_count; i++) {
        skin_entry_t *s = &skin_list[i];
        if (!s->valid) continue;
        if (shown >= PACK_LIST_MAX) { more++; continue; }
        rect_t r = { cx - rw * 0.5f, y, rw, rh };
        int click = button_box(r, aspect, mx, my);
        if (strcmp(settings.texture_pack, s->file) == 0) draw_select_frame(r, aspect);
        draw_pack_preview(s->preview, r.x + 0.008f, r.y + (rh - ps) * 0.5f, ps, aspect);
        text_draw(text, s->name, r.x + ps + 0.022f, r.y + (rh - 0.030f) * 0.5f,
                  0.030f, 1.0f, 1.0f, 1.0f, 1.0f, aspect);
        if (click) {
            snprintf(settings.texture_pack, sizeof settings.texture_pack, "%s", s->file);
            apply_texture_pack(s->path);
            settings_save();
            return;
        }
        y += rh + gap;
        shown++;
    }
    if (more) {
        char note[48];
        snprintf(note, sizeof note, "+%d more (showing %d)", more, PACK_LIST_MAX);
        draw_centered_text(note, y + 0.006f, 0.020f, aspect, 0.6f, 0.66f, 0.78f, 1.0f);
        y += 0.030f;
    }

    float ey = y + 0.014f;
    for (int i = 0; i < skin_count; i++) {
        if (skin_list[i].valid) continue;
        char msg[160];
        snprintf(msg, sizeof msg, "Not showing '%s' texture pack: not a valid 80x80 RGBA atlas PNG",
                 skin_list[i].name);
        draw_centered_text(msg, ey, 0.020f, aspect, 0.92f, 0.32f, 0.32f, 1.0f);
        ey += 0.030f;
    }

    rect_t bback = { cx - 0.15f, 0.88f, 0.30f, 0.075f };
    if (do_button(bback, "Back", aspect, mx, my)) leave_texpack();
}

static void draw_audiopack_screen(float aspect, float mx, float my) {
    glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw_centered_text("AUDIO PACK", 0.09f, 0.06f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);

    float cx = aspect * 0.5f;
    float rw = 0.5f, rh = 0.068f, gap = 0.012f;
    float y = 0.22f;

    rect_t dr = { cx - rw * 0.5f, y, rw, rh };
    int dclick = button_box(dr, aspect, mx, my);
    if (settings.audio_pack[0] == '\0') draw_select_frame(dr, aspect);
    text_draw(text, "Built-in (synth)", dr.x + 0.02f, dr.y + (rh - 0.030f) * 0.5f,
              0.030f, 1.0f, 1.0f, 1.0f, 1.0f, aspect);
    if (dclick) { settings.audio_pack[0] = '\0'; audio_reload(audio, NULL); settings_save(); return; }
    y += rh + gap;

    int shown = 0, more = 0;
    for (int i = 0; i < audiopack_count; i++) {
        audiopack_entry_t *a = &audiopack_list[i];
        if (!a->valid) continue;
        if (shown >= PACK_LIST_MAX) { more++; continue; }
        rect_t r = { cx - rw * 0.5f, y, rw, rh };
        int click = button_box(r, aspect, mx, my);
        if (strcmp(settings.audio_pack, a->name) == 0) draw_select_frame(r, aspect);
        text_draw(text, a->name, r.x + 0.02f, r.y + (rh - 0.030f) * 0.5f,
                  0.030f, 1.0f, 1.0f, 1.0f, 1.0f, aspect);
        if (click) {
            snprintf(settings.audio_pack, sizeof settings.audio_pack, "%s", a->name);
            audio_reload(audio, a->path);
            settings_save();
            return;
        }
        y += rh + gap;
        shown++;
    }
    if (more) {
        char note[48];
        snprintf(note, sizeof note, "+%d more (showing %d)", more, PACK_LIST_MAX);
        draw_centered_text(note, y + 0.006f, 0.020f, aspect, 0.6f, 0.66f, 0.78f, 1.0f);
        y += 0.030f;
    }

    float ey = y + 0.014f;
    for (int i = 0; i < audiopack_count; i++) {
        if (audiopack_list[i].valid) continue;
        char msg[160];
        snprintf(msg, sizeof msg, "Not showing '%s' audio pack due to invalid/missing wav files",
                 audiopack_list[i].name);
        draw_centered_text(msg, ey, 0.020f, aspect, 0.92f, 0.32f, 0.32f, 1.0f);
        ey += 0.030f;
    }

    rect_t bback = { cx - 0.15f, 0.88f, 0.30f, 0.075f };
    if (do_button(bback, "Back", aspect, mx, my)) game_state = GS_TITLE;
}

static void draw_title_screen(float aspect, float mx, float my) {
    glClearColor(0.09f, 0.11f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_centered_text("BLOCKWORLDS", 0.08f, 0.09f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);
    draw_centered_text("select a world", 0.20f, 0.028f, aspect, 0.6f, 0.7f, 0.85f, 1.0f);

    float cx = aspect * 0.5f;
    float rw = 0.62f, rh = 0.072f, gap = 0.012f;
    float y = 0.26f;
    int shown = world_count < 6 ? world_count : 6;
    for (int i = 0; i < shown; i++) {
        rect_t r = { cx - rw * 0.5f, y, rw, rh };
        int clicked = button_box(r, aspect, mx, my);
        const world_entry_t *we = &world_list[i];
        const char *nm = we->meta.name[0] ? we->meta.name : "(unnamed)";
        text_draw(text, nm, r.x + 0.02f, r.y + 0.012f, 0.032f, 1.0f, 1.0f, 1.0f, 1.0f, aspect);
        char info[64];
        snprintf(info, sizeof info, "seed %u  %s", we->meta.seed,
                 we->meta.gamemode == GAMEMODE_SURVIVAL ? "survival" : "creative");
        text_draw(text, info, r.x + 0.02f, r.y + 0.046f, 0.020f, 0.6f, 0.68f, 0.8f, 1.0f, aspect);
        rect_t del = { r.x + r.w - 0.062f, r.y + (rh - 0.046f) * 0.5f, 0.05f, 0.046f };
        int del_clicked = do_button(del, "X", aspect, mx, my);
        if (del_clicked) { rmtree(we->dir); scan_worlds(); return; }
        if (clicked) { enter_world_load(we->path); return; }
        y += rh + gap;
    }
    if (world_count == 0) {
        draw_centered_text("no worlds yet", 0.40f, 0.03f, aspect, 0.55f, 0.6f, 0.7f, 1.0f);
    }

    update_pack_availability();
    if (have_skins || have_audiopacks) {
        float pw = 0.28f, ph = 0.07f, pgap = 0.03f;
        if (have_skins && have_audiopacks) {
            rect_t bt = { cx - pw - pgap * 0.5f, 0.74f, pw, ph };
            rect_t ba = { cx + pgap * 0.5f,       0.74f, pw, ph };
            if (do_button(bt, "Texture Pack", aspect, mx, my)) { scan_skins(); game_state = GS_TEXPACK; }
            if (do_button(ba, "Audio Pack",   aspect, mx, my)) { scan_audio_packs(); game_state = GS_AUDIOPACK; }
        } else if (have_skins) {
            rect_t bt = { cx - pw * 0.5f, 0.74f, pw, ph };
            if (do_button(bt, "Texture Pack", aspect, mx, my)) { scan_skins(); game_state = GS_TEXPACK; }
        } else {
            rect_t ba = { cx - pw * 0.5f, 0.74f, pw, ph };
            if (do_button(ba, "Audio Pack", aspect, mx, my)) { scan_audio_packs(); game_state = GS_AUDIOPACK; }
        }
    }

    float bw = 0.30f, bh = 0.08f;
    rect_t bnew = { cx - bw - 0.16f, 0.86f, bw, bh };
    rect_t bset = { cx - bw * 0.5f,  0.86f, bw, bh };
    rect_t bquit = { cx + 0.16f,     0.86f, bw, bh };
    if (do_button(bnew, "New World", aspect, mx, my)) {
        field_name[0] = '\0';
        snprintf(field_seed, sizeof field_seed, "%u", random_seed());
        field_focus = 0;
        create_gamemode = GAMEMODE_CREATIVE;
        create_allow_commands = 1;
        game_state = GS_CREATE;
    }
    if (do_button(bset, "Settings", aspect, mx, my)) {
        settings_return = GS_TITLE;
        game_state = GS_SETTINGS;
    }
    if (do_button(bquit, "Quit", aspect, mx, my)) {
        glfwSetWindowShouldClose(g_win, 1);
    }
}

static void draw_field(rect_t r, const char *label, const char *value, int focused, float aspect) {
    text_draw(text, label, r.x, r.y - 0.040f, 0.026f, 0.7f, 0.78f, 0.9f, 1.0f, aspect);
    float bg = focused ? 0.22f : 0.12f;
    ui_draw_rect(ui, r.x, r.y, r.w, r.h, bg, bg, bg + 0.05f, 0.95f, aspect);
    float t  = 0.003f;
    float bc = focused ? 0.9f : 0.4f;
    ui_draw_rect(ui, r.x, r.y, r.w, t, bc, bc, bc, 0.8f, aspect);
    ui_draw_rect(ui, r.x, r.y + r.h - t, r.w, t, bc, bc, bc, 0.8f, aspect);
    ui_draw_rect(ui, r.x, r.y, t, r.h, bc, bc, bc, 0.8f, aspect);
    ui_draw_rect(ui, r.x + r.w - t, r.y, t, r.h, bc, bc, bc, 0.8f, aspect);
    int blink = focused && (((int)(glfwGetTime() * 2.0)) & 1);
    char shown[80];
    snprintf(shown, sizeof shown, "%s%s", value, blink ? "_" : "");
    text_draw(text, shown, r.x + 0.012f, r.y + (r.h - 0.032f) * 0.5f, 0.032f,
              1.0f, 1.0f, 1.0f, 1.0f, aspect);
}

static void mode_toggle(rect_t a, rect_t b, const char *la, const char *lb,
                        int a_selected, float aspect) {
    ui_draw_rect(ui, a.x, a.y, a.w, a.h,
                 a_selected ? 0.25f : 0.14f, a_selected ? 0.40f : 0.16f, a_selected ? 0.30f : 0.20f, 0.95f, aspect);
    ui_draw_rect(ui, b.x, b.y, b.w, b.h,
                 !a_selected ? 0.40f : 0.14f, !a_selected ? 0.25f : 0.16f, !a_selected ? 0.22f : 0.20f, 0.95f, aspect);
    float wa = text_width(la, 0.032f), wb = text_width(lb, 0.032f);
    text_draw(text, la, a.x + (a.w - wa) * 0.5f, a.y + 0.015f, 0.032f, 1, 1, 1, 1, aspect);
    text_draw(text, lb, b.x + (b.w - wb) * 0.5f, b.y + 0.015f, 0.032f, 1, 1, 1, 1, aspect);
}

static void draw_create_screen(float aspect, float mx, float my) {
    glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_centered_text("NEW WORLD", 0.09f, 0.065f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);

    float cx = aspect * 0.5f;
    float fw = 0.55f, fh = 0.065f;
    rect_t fname = { cx - fw * 0.5f, 0.24f, fw, fh };
    rect_t fseed = { cx - fw * 0.5f, 0.38f, fw, fh };

    if (ui_click && rect_hit(fname, ui_click_x, ui_click_y)) field_focus = 0;
    if (ui_click && rect_hit(fseed, ui_click_x, ui_click_y)) field_focus = 1;

    draw_field(fname, "World name", field_name, field_focus == 0, aspect);
    draw_field(fseed, "Seed (blank = random)", field_seed, field_focus == 1, aspect);

    float gw = 0.26f, gh = 0.06f;
    text_draw(text, "Mode", cx - fw * 0.5f, 0.49f, 0.026f, 0.7f, 0.78f, 0.9f, 1.0f, aspect);
    rect_t gcrea = { cx - fw * 0.5f, 0.52f, gw, gh };
    rect_t gsurv = { cx - fw * 0.5f + gw + 0.03f, 0.52f, gw, gh };
    mode_toggle(gcrea, gsurv, "Creative", "Survival", create_gamemode == GAMEMODE_CREATIVE, aspect);
    if (ui_click && rect_hit(gcrea, ui_click_x, ui_click_y)) create_gamemode = GAMEMODE_CREATIVE;
    if (ui_click && rect_hit(gsurv, ui_click_x, ui_click_y)) create_gamemode = GAMEMODE_SURVIVAL;

    text_draw(text, "Allow commands", cx - fw * 0.5f, 0.61f, 0.026f, 0.7f, 0.78f, 0.9f, 1.0f, aspect);
    rect_t con  = { cx - fw * 0.5f, 0.64f, gw, gh };
    rect_t coff = { cx - fw * 0.5f + gw + 0.03f, 0.64f, gw, gh };
    mode_toggle(con, coff, "On", "Off", create_allow_commands != 0, aspect);
    if (ui_click && rect_hit(con,  ui_click_x, ui_click_y)) create_allow_commands = 1;
    if (ui_click && rect_hit(coff, ui_click_x, ui_click_y)) create_allow_commands = 0;

    rect_t bcreate = { cx - 0.33f, 0.80f, 0.30f, 0.08f };
    rect_t bback   = { cx + 0.03f, 0.80f, 0.30f, 0.08f };
    if (do_button(bcreate, "Create", aspect, mx, my)) {
        const char *nm = field_name[0] ? field_name : "World";
        uint32_t seed = parse_seed(field_seed, nm);
        if (enter_world_new(nm, seed, create_gamemode, create_allow_commands)) {
            glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            mouse_initialized = 0;
        }
        return;
    }
    if (do_button(bback, "Back", aspect, mx, my)) {
        game_state = GS_TITLE;
    }
}

static void setting_row(float y, const char *label, const char *value, float aspect,
                        float mx, float my, int *minus, int *plus) {
    float cx = aspect * 0.5f;
    text_draw(text, label, cx - 0.46f, y + 0.013f, 0.030f, 0.85f, 0.9f, 1.0f, 1.0f, aspect);
    rect_t m = { cx + 0.04f, y, 0.06f, 0.052f };
    rect_t p = { cx + 0.40f, y, 0.06f, 0.052f };
    *minus = do_button(m, "-", aspect, mx, my);
    *plus  = do_button(p, "+", aspect, mx, my);
    float vw = text_width(value, 0.030f);
    text_draw(text, value, cx + 0.25f - vw * 0.5f, y + 0.013f, 0.030f, 1.0f, 1.0f, 1.0f, 1.0f, aspect);
}

static void draw_settings_screen(float aspect, float mx, float my) {
    glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_centered_text("SETTINGS", 0.10f, 0.07f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);

    int changed = 0, dm = 0, dp = 0;
    char val[32];

    snprintf(val, sizeof val, "%d", (int)(settings.fov + 0.5f));
    setting_row(0.28f, "Field of view", val, aspect, mx, my, &dm, &dp);
    if (dm) { settings.fov -= 5.0f; changed = 1; }
    if (dp) { settings.fov += 5.0f; changed = 1; }

    snprintf(val, sizeof val, "%d", (int)(settings.mouse_sens * 10000.0f + 0.5f));
    setting_row(0.37f, "Mouse sensitivity", val, aspect, mx, my, &dm, &dp);
    if (dm) { settings.mouse_sens -= 0.0005f; changed = 1; }
    if (dp) { settings.mouse_sens += 0.0005f; changed = 1; }

    snprintf(val, sizeof val, "%d%%", (int)(settings.volume * 100.0f + 0.5f));
    setting_row(0.46f, "Volume", val, aspect, mx, my, &dm, &dp);
    if (dm) { settings.volume -= 0.1f; changed = 1; }
    if (dp) { settings.volume += 0.1f; changed = 1; }

    snprintf(val, sizeof val, "%d", settings.render_distance);
    setting_row(0.55f, "Render distance", val, aspect, mx, my, &dm, &dp);
    if (dm) { settings.render_distance -= 1; changed = 1; }
    if (dp) { settings.render_distance += 1; changed = 1; }

    setting_row(0.64f, "FPS overlay", settings.fps_overlay ? "on" : "off", aspect, mx, my, &dm, &dp);
    if (dm || dp) { settings.fps_overlay = !settings.fps_overlay; changed = 1; }

    draw_centered_text("render distance applies on next world load", 0.73f, 0.020f,
                       aspect, 0.55f, 0.6f, 0.7f, 1.0f);

    rect_t bback = { aspect * 0.5f - 0.15f, 0.82f, 0.30f, 0.08f };
    if (do_button(bback, "Back", aspect, mx, my)) {
        settings_save();
        game_state = settings_return;
    }

    if (changed) {
        settings_clamp();
        settings_apply();
    }
}

static void draw_inventory_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0.0f, 0.0f, aspect, 1.0f, 0.0f, 0.0f, 0.0f, 0.55f, aspect);

    const char *title = (world_gamemode(world) == GAMEMODE_SURVIVAL) ? "INVENTORY" : "BLOCKS";
    draw_centered_text(title, 0.09f, 0.05f, aspect, 1.0f, 1.0f, 1.0f, 1.0f);

    float cx = aspect * 0.5f;
    int survival = (world_gamemode(world) == GAMEMODE_SURVIVAL);
    float cell = 0.075f, gap = 0.012f;

    if (survival) {
        int cols = PACK_COLS;
        float grid_w = (float)cols * cell + (float)(cols - 1) * gap;
        float gx0 = cx - grid_w * 0.5f;
        float gy0 = 0.20f;
        for (int i = 0; i < PACK_COUNT; i++) {
            int r = i / cols, c = i % cols;
            float bx = gx0 + (float)c * (cell + gap);
            float by = gy0 + (float)r * (cell + gap);
            rect_t cr = { bx, by, cell, cell };
            int hov = rect_hit(cr, mx, my);
            ui_draw_rect(ui, bx, by, cell, cell,
                         hov ? 0.30f : 0.16f, hov ? 0.32f : 0.16f, hov ? 0.40f : 0.20f, 0.95f, aspect);
            if (pack[i] != BLOCK_AIR) {
                int tx, ty;
                block_icon_tile(pack[i], &tx, &ty);
                float inset = cell * 0.10f;
                ui_draw_atlas_icon(ui, tx, ty, bx + inset, by + inset, cell - 2.0f * inset, aspect);
                char cnt[8];
                snprintf(cnt, sizeof cnt, "%d", pack_count[i]);
                text_draw(text, cnt, bx + cell - 0.030f, by + cell - 0.026f, 0.018f,
                          1.0f, 1.0f, 1.0f, 0.95f, aspect);
            }
            if (ui_click && rect_hit(cr, ui_click_x, ui_click_y)) {
                audio_play_ui_click(audio);
                block_t tb = hotbar[hotbar_sel];
                int     tc = hotbar_count[hotbar_sel];
                hotbar[hotbar_sel]       = pack[i];
                hotbar_count[hotbar_sel] = pack_count[i];
                pack[i]       = tb;
                pack_count[i] = tc;
                player.selected_block = hotbar[hotbar_sel];
            }
        }
    } else {
        int cols = 6;
        int rows = (PALETTE_COUNT + cols - 1) / cols;
        float grid_w = (float)cols * cell + (float)(cols - 1) * gap;
        float gx0 = cx - grid_w * 0.5f;
        float gy0 = 0.20f;
        int hovered = -1;
        for (int i = 0; i < PALETTE_COUNT; i++) {
            int r = i / cols, c = i % cols;
            float bx = gx0 + (float)c * (cell + gap);
            float by = gy0 + (float)r * (cell + gap);
            rect_t cr = { bx, by, cell, cell };
            int hov = rect_hit(cr, mx, my);
            ui_draw_rect(ui, bx, by, cell, cell,
                         hov ? 0.30f : 0.16f, hov ? 0.32f : 0.16f, hov ? 0.40f : 0.20f, 0.95f, aspect);
            int tx, ty;
            block_icon_tile(PALETTE[i], &tx, &ty);
            float inset = cell * 0.10f;
            ui_draw_atlas_icon(ui, tx, ty, bx + inset, by + inset, cell - 2.0f * inset, aspect);
            if (hov) hovered = i;
            if (ui_click && rect_hit(cr, ui_click_x, ui_click_y)) {
                audio_play_ui_click(audio);
                hotbar[hotbar_sel] = PALETTE[i];
                player.selected_block = hotbar[hotbar_sel];
            }
        }
        if (hovered >= 0) {
            draw_centered_text(block_name(PALETTE[hovered]),
                               gy0 + (float)rows * (cell + gap) + 0.012f, 0.028f,
                               aspect, 0.8f, 0.85f, 0.95f, 1.0f);
        }
    }

    float slot = 0.07f, pad = 0.01f;
    float total_w = (float)HOTBAR_COUNT * slot + (float)(HOTBAR_COUNT + 1) * pad;
    float hx0 = cx - total_w * 0.5f;
    float hy = 0.78f;
    for (int i = 0; i < HOTBAR_COUNT; i++) {
        float sx = hx0 + pad + (float)i * (slot + pad);
        rect_t sr = { sx, hy, slot, slot };
        int sel = (i == hotbar_sel);
        ui_draw_rect(ui, sx, hy, slot, slot,
                     sel ? 0.30f : 0.15f, sel ? 0.32f : 0.15f, sel ? 0.40f : 0.18f, 0.9f, aspect);
        if (hotbar[i] != BLOCK_AIR) {
            int tx, ty;
            block_icon_tile(hotbar[i], &tx, &ty);
            float inset = slot * 0.12f;
            ui_draw_atlas_icon(ui, tx, ty, sx + inset, hy + inset, slot - 2.0f * inset, aspect);
            if (survival && hotbar_count[i] > 0) {
                char cnt[8];
                snprintf(cnt, sizeof cnt, "%d", hotbar_count[i]);
                text_draw(text, cnt, sx + slot - 0.030f, hy + slot - 0.026f, 0.018f,
                          1.0f, 1.0f, 1.0f, 0.95f, aspect);
            }
        }
        if (sel) {
            float t = 0.003f;
            ui_draw_rect(ui, sx - t, hy - t, slot + 2.0f * t, t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
            ui_draw_rect(ui, sx - t, hy + slot, slot + 2.0f * t, t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
            ui_draw_rect(ui, sx - t, hy - t, t, slot + 2.0f * t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
            ui_draw_rect(ui, sx + slot, hy - t, t, slot + 2.0f * t, 1.0f, 1.0f, 1.0f, 0.55f, aspect);
        }
        char num[2] = { (char)(i < 9 ? '1' + i : '0'), 0 };
        text_draw(text, num, sx + 0.005f, hy + 0.004f, 0.022f, 1.0f, 1.0f, 1.0f, 0.85f, aspect);
        if (ui_click && rect_hit(sr, ui_click_x, ui_click_y)) hotbar_select(i);
        if (!survival && ui_rclick && rect_hit(sr, ui_rclick_x, ui_rclick_y)) {
            hotbar[i] = BLOCK_AIR;
            if (i == hotbar_sel) player.selected_block = BLOCK_AIR;
        }
    }

    const char *hint = survival
        ? "break blocks to collect them   click a pack slot to move it into the selected hotbar slot"
        : "left-click a block to fill the selected slot   right-click a slot to clear";
    draw_centered_text(hint, 0.885f, 0.018f, aspect, 0.6f, 0.65f, 0.75f, 1.0f);
    rect_t bdone = { cx - 0.12f, 0.91f, 0.24f, 0.06f };
    if (do_button(bdone, "Done", aspect, mx, my)) close_inventory();
}

int main(void) {
    glfwSetErrorCallback(glfw_error_cb);
    if (!glfwInit()) return EXIT_FAILURE;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow *win = glfwCreateWindow(1280, 720, "BlockWorlds", NULL, NULL);
    if (!win) { glfwTerminate(); return EXIT_FAILURE; }
    g_win = win;

    glfwMakeContextCurrent(win);
    if (!gl_load()) {
        fprintf(stderr, "failed to load OpenGL functions\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, key_cb);
    glfwSetCharCallback(win, char_cb);
    glfwSetMouseButtonCallback(win, mouse_button_cb);
    glfwSetScrollCallback(win, scroll_cb);
    glfwSetFramebufferSizeCallback(win, framebuffer_size_cb);
    glfwSetCursorPosCallback(win, cursor_pos_cb);

    printf("GL %s | GLSL %s\n",
           glGetString(GL_VERSION),
           glGetString(GL_SHADING_LANGUAGE_VERSION));

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);

    struct timespec seed_ts;
    timespec_get(&seed_ts, TIME_UTC);
    srand((unsigned)seed_ts.tv_nsec ^ ((unsigned)seed_ts.tv_sec << 16));
    ensure_storage_dirs();
    settings_load();

    prog = load_program("shaders/cube.vert", "shaders/cube.frag");
    if (!prog) { glfwTerminate(); return EXIT_FAILURE; }

    {
        char skinpath[256];
        const char *src = "";
        if (settings.texture_pack[0]) {
            snprintf(skinpath, sizeof skinpath, "%s/%s", SKINS_DIR, settings.texture_pack);
            src = skinpath;
        }
        atlas_tex = texture_create_atlas(src);
        if (!atlas_tex && src[0]) {
            settings.texture_pack[0] = '\0';
            atlas_tex = texture_create_atlas("");
        }
    }
    if (!atlas_tex) { glfwTerminate(); return EXIT_FAILURE; }

    ui = ui_create();
    if (!ui) { glfwTerminate(); return EXIT_FAILURE; }

    text = text_create();
    if (!text) { glfwTerminate(); return EXIT_FAILURE; }

    console = console_create(text, ui);
    if (!console) { glfwTerminate(); return EXIT_FAILURE; }
    console_set_commands(console, CMD_NAMES, CMD_USAGE, CMD_COUNT);

    falling = falling_create();
    if (!falling) { glfwTerminate(); return EXIT_FAILURE; }

    weather = weather_create();
    if (!weather) { glfwTerminate(); return EXIT_FAILURE; }

    lighttex = lighttex_create(LIGHT_TEX_DIM);
    if (!lighttex) { glfwTerminate(); return EXIT_FAILURE; }

    {
        char apath[256];
        const char *adir = NULL;
        if (settings.audio_pack[0]) {
            snprintf(apath, sizeof apath, "%s/%s", AUDIO_DIR, settings.audio_pack);
            adir = apath;
        }
        audio = audio_create(adir);
    }
    if (!audio) fprintf(stderr, "audio: device init failed; running without sound\n");

    settings_apply();

    GLint atlas_loc  = glGetUniformLocation(prog, "u_atlas");
    mvp_loc          = glGetUniformLocation(prog, "u_mvp");
    sun_dir_loc      = glGetUniformLocation(prog, "u_sun_dir");
    sun_strength_loc = glGetUniformLocation(prog, "u_sun_strength");
    ambient_loc      = glGetUniformLocation(prog, "u_ambient");
    sea_level_loc    = glGetUniformLocation(prog, "u_sea_level");
    chunk_origin_loc = glGetUniformLocation(prog, "u_chunk_origin");
    eye_in_water_loc = glGetUniformLocation(prog, "u_eye_in_water");
    eye_depth_loc    = glGetUniformLocation(prog, "u_eye_water_depth");
    light_vp_loc       = glGetUniformLocation(prog, "u_light_vp");
    shadow_map_loc     = glGetUniformLocation(prog, "u_shadow_map");
    shadow_enabled_loc = glGetUniformLocation(prog, "u_shadow_enabled");
    shadow_texel_loc   = glGetUniformLocation(prog, "u_shadow_texel");
    shadow_world_texel_loc = glGetUniformLocation(prog, "u_shadow_world_texel");
    eye_loc            = glGetUniformLocation(prog, "u_eye");
    fog_color_loc      = glGetUniformLocation(prog, "u_fog_color");
    fog_density_loc    = glGetUniformLocation(prog, "u_fog_density");
    clip_below_loc     = glGetUniformLocation(prog, "u_clip_below");
    clip_height_loc    = glGetUniformLocation(prog, "u_clip_height");
    ssao_tex_loc       = glGetUniformLocation(prog, "u_ssao");
    ssao_enabled_loc   = glGetUniformLocation(prog, "u_ssao_enabled");
    screen_loc         = glGetUniformLocation(prog, "u_screen");
    blocklight_tex_loc     = glGetUniformLocation(prog, "u_blocklight_tex");
    light_origin_loc       = glGetUniformLocation(prog, "u_light_origin");
    light_dim_loc          = glGetUniformLocation(prog, "u_light_dim");
    blocklight_enabled_loc = glGetUniformLocation(prog, "u_blocklight_enabled");

    depth_prog = load_program("shaders/depth.vert", "shaders/depth.frag");
    if (!depth_prog) { glfwTerminate(); return EXIT_FAILURE; }
    depth_mvp_loc = glGetUniformLocation(depth_prog, "u_mvp");

    shadow_fb = framebuffer_create_depth(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
    if (!shadow_fb) { fprintf(stderr, "failed to create shadow map\n"); glfwTerminate(); return EXIT_FAILURE; }

    water_prog = load_program("shaders/water.vert", "shaders/water.frag");
    if (!water_prog) { glfwTerminate(); return EXIT_FAILURE; }
    water_mvp_loc          = glGetUniformLocation(water_prog, "u_mvp");
    water_origin_loc       = glGetUniformLocation(water_prog, "u_chunk_origin");
    water_refl_loc         = glGetUniformLocation(water_prog, "u_reflection");
    water_eye_loc          = glGetUniformLocation(water_prog, "u_eye");
    water_time_loc         = glGetUniformLocation(water_prog, "u_time");
    water_sun_dir_loc      = glGetUniformLocation(water_prog, "u_sun_dir");
    water_sun_strength_loc = glGetUniformLocation(water_prog, "u_sun_strength");
    water_fog_color_loc    = glGetUniformLocation(water_prog, "u_fog_color");
    water_fog_density_loc  = glGetUniformLocation(water_prog, "u_fog_density");

    reflect_fb = framebuffer_create_color(REFLECT_W, REFLECT_H);
    if (!reflect_fb) { fprintf(stderr, "failed to create reflection buffer\n"); glfwTerminate(); return EXIT_FAILURE; }

    ssao_prog = load_program("shaders/ssao.vert", "shaders/ssao.frag");
    if (!ssao_prog) { glfwTerminate(); return EXIT_FAILURE; }
    ssao_depth_loc  = glGetUniformLocation(ssao_prog, "u_depth");
    ssao_proj_loc   = glGetUniformLocation(ssao_prog, "u_proj");
    ssao_radius_loc = glGetUniformLocation(ssao_prog, "u_radius");
    ssao_bias_loc   = glGetUniformLocation(ssao_prog, "u_bias");
    glGenVertexArrays(1, &fsq_vao);

    ssao_depth_fb = framebuffer_create_depth_raw(SSAO_W, SSAO_H);
    ssao_fb       = framebuffer_create_color(SSAO_W, SSAO_H);
    if (!ssao_depth_fb || !ssao_fb) { fprintf(stderr, "failed to create SSAO buffers\n"); glfwTerminate(); return EXIT_FAILURE; }

    ssao_blur_fb = framebuffer_create_color(SSAO_W, SSAO_H);
    if (!ssao_blur_fb) { fprintf(stderr, "failed to create SSAO blur buffer\n"); glfwTerminate(); return EXIT_FAILURE; }
    ssao_blur_prog = load_program("shaders/ssao.vert", "shaders/ssao_blur.frag");
    if (!ssao_blur_prog) { glfwTerminate(); return EXIT_FAILURE; }
    ssao_blur_tex_loc   = glGetUniformLocation(ssao_blur_prog, "u_ssao");
    ssao_blur_texel_loc = glGetUniformLocation(ssao_blur_prog, "u_texel");

    blit_prog = load_program("shaders/ssao.vert", "shaders/blit.frag");
    if (!blit_prog) { glfwTerminate(); return EXIT_FAILURE; }
    blit_tex_loc = glGetUniformLocation(blit_prog, "u_tex");

    sky_prog = load_program("shaders/ssao.vert", "shaders/sky.frag");
    if (!sky_prog) { glfwTerminate(); return EXIT_FAILURE; }
    sky_right_loc   = glGetUniformLocation(sky_prog, "u_cam_right");
    sky_up_loc      = glGetUniformLocation(sky_prog, "u_cam_up");
    sky_fwd_loc     = glGetUniformLocation(sky_prog, "u_cam_fwd");
    sky_tanfov_loc  = glGetUniformLocation(sky_prog, "u_tan_half_fov");
    sky_aspect_loc  = glGetUniformLocation(sky_prog, "u_aspect");
    sky_sun_loc     = glGetUniformLocation(sky_prog, "u_sun_dir");
    sky_time_loc    = glGetUniformLocation(sky_prog, "u_time");
    sky_zenith_loc  = glGetUniformLocation(sky_prog, "u_zenith");
    sky_horizon_loc = glGetUniformLocation(sky_prog, "u_horizon");
    sky_day_loc     = glGetUniformLocation(sky_prog, "u_day");
    sky_atmo_loc    = glGetUniformLocation(sky_prog, "u_atmo_tint");
    sky_eye_loc     = glGetUniformLocation(sky_prog, "u_eye");
    sky_overcast_loc = glGetUniformLocation(sky_prog, "u_overcast");

    glUseProgram(prog);
    glUniform1i(atlas_loc, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex);

    player_init(&player, (vec3){0.5f, 80.0f, 0.5f});
    scan_worlds();

    double last_time         = glfwGetTime();
    double fps_window_start  = last_time;
    int    fps_frame_count   = 0;
    const double target_dt   = 1.0 / 120.0;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = (float)(now - last_time);
        if (dt > 0.1f) dt = 0.1f;
        last_time = now;

        int width, height;
        glfwGetFramebufferSize(win, &width, &height);
        float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

        float mx, my;
        cursor_ui_pos(win, &mx, &my);

        if (game_state == GS_TITLE) {
            draw_title_screen(aspect, mx, my);
        } else if (game_state == GS_CREATE) {
            draw_create_screen(aspect, mx, my);
        } else if (game_state == GS_SETTINGS) {
            draw_settings_screen(aspect, mx, my);
        } else if (game_state == GS_LOADING) {
            vec3 sun_dir;
            float ss, amb;
            compute_sky(world_time, &sun_dir, &ss, &amb);
            world_set_sun_dir(world, sun_dir.x, sun_dir.y, sun_dir.z);

            if (!loading_started) {
                world_update_streaming(world, player.pos.x, player.pos.z);
                loading_started = 1;
            }
            for (int i = 0; i < 4; i++) world_pump_meshing(world);

            int remaining = world_work_remaining(world);
            if (remaining > loading_peak) loading_peak = remaining;

            glClearColor(0.07f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            draw_centered_text("GENERATING WORLD", 0.40f, 0.05f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);

            float frac = (loading_peak > 0) ? 1.0f - (float)remaining / (float)loading_peak : 1.0f;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            float bw = 0.5f, bh = 0.03f;
            float bx = aspect * 0.5f - bw * 0.5f, by = 0.52f;
            ui_draw_rect(ui, bx - 0.004f, by - 0.004f, bw + 0.008f, bh + 0.008f, 0.9f, 0.9f, 0.95f, 0.5f, aspect);
            ui_draw_rect(ui, bx, by, bw, bh, 0.12f, 0.13f, 0.18f, 1.0f, aspect);
            ui_draw_rect(ui, bx, by, bw * frac, bh, 0.45f, 0.70f, 0.95f, 1.0f, aspect);

            char pct[8];
            snprintf(pct, sizeof pct, "%d%%", (int)(frac * 100.0f));
            draw_centered_text(pct, 0.58f, 0.03f, aspect, 0.7f, 0.8f, 0.9f, 1.0f);

            if (loading_started && remaining == 0) {
                game_state = GS_PLAYING;
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                mouse_initialized = 0;
            }
        } else if (game_state == GS_PLAYING) {
            player_set_input_enabled(!console_is_open(console));
            player_update(&player, win, world, (double)dt);
            world_update_streaming(world, player.pos.x, player.pos.z);
            if (falling_update(falling, world, dt) > 0) audio_play_block_land(audio);
            world_tick_water(world, 8);

            static float step_accum   = 0.0f;
            static int   was_grounded = 1;
            static float fall_peak    = 0.0f;
            float gspeed = sqrtf(player.vel.x * player.vel.x + player.vel.z * player.vel.z);
            if (player.mode == PLAYER_WALKING && player.grounded && gspeed > 0.5f) {
                float stride = player.in_water ? STEP_STRIDE_WATER : STEP_STRIDE;
                step_accum += gspeed * dt;
                if (step_accum >= stride) {
                    if (player.in_water) audio_play_water_step(audio);
                    else {
                        block_t ground = world_get_block(world,
                            (int)floorf(player.pos.x),
                            (int)floorf(player.pos.y - 0.1f),
                            (int)floorf(player.pos.z));
                        audio_play_step(audio, ground);
                    }
                    step_accum -= stride;
                }
            } else if (player.mode == PLAYER_WALKING && player.in_water && !player.grounded && gspeed > 0.6f) {
                step_accum += gspeed * dt;
                if (step_accum >= SWIM_STRIDE) {
                    audio_play_swim(audio);
                    step_accum -= SWIM_STRIDE;
                }
            } else {
                step_accum = 0.0f;
            }

            if (player.grounded) {
                if (!was_grounded && !player.in_water) audio_play_land(audio, -fall_peak);
                fall_peak = 0.0f;
            } else if (player.vel.y < fall_peak) {
                fall_peak = player.vel.y;
            }
            was_grounded = player.grounded;

            vec3 eye = player_eye_pos(&player);
            block_t eye_block = world_get_block(world, (int)floorf(eye.x), (int)floorf(eye.y), (int)floorf(eye.z));
            int underwater = (eye_block == BLOCK_WATER);
            if (prev_underwater == 0 && underwater) { audio_play_splash(audio); water_enter_time = now; }
            if (prev_underwater == 1 && !underwater) audio_play_exit_water(audio);
            prev_underwater = underwater;

            world_time += dt / DAY_LENGTH_SEC;
            if (world_time >= 1.0f) world_time -= 1.0f;

            float sun_angle = world_time * 2.0f * PI - PI * 0.5f;
            vec3 sun_dir = { cosf(sun_angle), sinf(sun_angle), 0.0f };
            world_set_sun_dir(world, sun_dir.x, sun_dir.y, sun_dir.z);

            biome_t pbio = world_biome_at(world_seed(world),
                                          (int)floorf(player.pos.x), (int)floorf(player.pos.z));
            float ak = 1.0f - expf(-0.5f * dt);
            atmo_tint = vlerp(atmo_tint, biome_atmo_tint(pbio), ak);

            world_pump_meshing(world);

            update_weather(now, dt);
            vec3 light_center = player_eye_pos(&player);
            lighttex_update(lighttex, world, light_center.x, light_center.y, light_center.z, dt);

            render_world_scene(aspect);
            draw_hotbar(aspect);
            if (console_is_open(console)) {
                console_render(console, aspect);
            } else {
                ui_draw_crosshair(ui, aspect);
                console_render_feed(console, aspect);
            }
        } else if (game_state == GS_INVENTORY) {
            draw_inventory_screen(aspect, mx, my);
        } else if (game_state == GS_TEXPACK) {
            draw_texpack_screen(aspect, mx, my);
        } else if (game_state == GS_AUDIOPACK) {
            draw_audiopack_screen(aspect, mx, my);
        } else {
            if (pause_capture_pending) {
                render_world_scene(aspect);
                ui_capture_screen(ui, width, height);
                pause_capture_pending = 0;
            }
            ui_draw_blur(ui, 0.55f);

            draw_centered_text("PAUSED", 0.18f, 0.07f, aspect, 1.0f, 1.0f, 1.0f, 1.0f);

            float cx = aspect * 0.5f;
            rect_t resume   = { cx - 0.18f, 0.36f, 0.36f, 0.08f };
            rect_t bset     = { cx - 0.18f, 0.46f, 0.36f, 0.08f };
            rect_t savequit = { cx - 0.18f, 0.56f, 0.36f, 0.08f };
            rect_t quitns   = { cx - 0.18f, 0.66f, 0.36f, 0.08f };
            if (do_button(resume, "Resume", aspect, mx, my)) {
                game_state = GS_PLAYING;
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                mouse_initialized = 0;
            }
            if (do_button(bset, "Settings", aspect, mx, my)) {
                settings_return = GS_PAUSED;
                game_state = GS_SETTINGS;
            }
            if (do_button(savequit, "Save & Quit", aspect, mx, my)) {
                quit_to_title(1);
            }
            if (do_button(quitns, "Quit", aspect, mx, my)) {
                quit_to_title(0);
            }
        }

        ui_click = 0;
        ui_rclick = 0;

        glfwSwapBuffers(win);
        glfwPollEvents();

        fps_frame_count++;
        double fps_window_dt = now - fps_window_start;
        if (fps_window_dt >= 0.5) {
            char title[200];
            if (game_state == GS_PLAYING && fps_shown) {
                double fps = (double)fps_frame_count / fps_window_dt;
                const char *mode = (player.mode == PLAYER_FLYING) ? "fly" : "walk";
                int hh = (int)(world_time * 24.0f) % 24;
                int mm = (int)(world_time * 24.0f * 60.0f) % 60;
                snprintf(title, sizeof title,
                         "BlockWorlds | %.1f fps | %s | %02d:%02d | xyz=%.1f,%.1f,%.1f | hold:%s",
                         fps, mode, hh, mm,
                         (double)player.pos.x, (double)player.pos.y, (double)player.pos.z,
                         block_name((block_t)player.selected_block));
            } else {
                snprintf(title, sizeof title, "BlockWorlds");
            }
            glfwSetWindowTitle(win, title);
            fps_window_start = now;
            fps_frame_count  = 0;
        }

        double frame_elapsed = glfwGetTime() - now;
        if (frame_elapsed < target_dt) {
            double sleep_for = target_dt - frame_elapsed;
            struct timespec ts;
            ts.tv_sec  = (time_t)sleep_for;
            ts.tv_nsec = (long)((sleep_for - (double)ts.tv_sec) * 1e9);
            nanosleep(&ts, NULL);
        }
    }

    if (world && (game_state == GS_PLAYING || game_state == GS_PAUSED ||
                  game_state == GS_LOADING || game_state == GS_SETTINGS)) {
        save_game();
    }

    audio_destroy(audio);
    falling_destroy(falling);
    weather_destroy(weather);
    lighttex_destroy(lighttex);
    console_destroy(console);
    text_destroy(text);
    ui_destroy(ui);
    if (world) world_destroy(world);
    glDeleteProgram(prog);
    glDeleteProgram(depth_prog);
    glDeleteProgram(water_prog);
    glDeleteProgram(ssao_prog);
    glDeleteProgram(blit_prog);
    glDeleteProgram(sky_prog);
    glDeleteVertexArrays(1, &fsq_vao);
    framebuffer_destroy(shadow_fb);
    framebuffer_destroy(reflect_fb);
    framebuffer_destroy(ssao_depth_fb);
    framebuffer_destroy(ssao_fb);
    framebuffer_destroy(ssao_blur_fb);
    glDeleteProgram(ssao_blur_prog);
    glDeleteTextures(1, &atlas_tex);

    glfwDestroyWindow(win);
    glfwTerminate();
    return EXIT_SUCCESS;
}