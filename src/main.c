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
#include "registry.h"
#include "survival.h"
#include "texture.h"
#include "player.h"
#include "raycast.h"
#include "ui.h"
#include "text.h"
#include "console.h"
#include "falling.h"
#include "particle.h"
#include "craft.h"
#include "entity.h"
#include "toast.h"
#include "weather.h"
#include "lighttex.h"
#include "audio.h"
#include "framebuffer.h"
#include "platform.h"
#include "net.h"

#define PI 3.14159265358979323846f

#define GAME_VERSION "0.1.0 beta"

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
    GS_TEXPACK, GS_AUDIOPACK,
    GS_DEATH, GS_CRAFTING, GS_FURNACE, GS_FORGE, GS_ANVIL, GS_CHEST, GS_BREWING,
    GS_MP_JOIN, GS_LOAD
} game_state_t;

typedef struct {
    float fov;
    float mouse_sens;
    float volume;
    float vol_ui, vol_mobs, vol_player, vol_env;
    int   render_distance;
    int   fps_overlay;
    int   fullscreen;
    int   mp_port;
    char  texture_pack[MAX_PACK_NAME];
    char  audio_pack[MAX_PACK_NAME];
} settings_t;

#define SETTINGS_DEFAULT_INIT { 70.0f, 0.0025f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 5, 1, 0, 25655, "", "" }

typedef struct {
    char         path[192];
    char         dir[192];
    world_meta_t meta;
} world_entry_t;

typedef struct { float x, y, w, h; } rect_t;

static int  rect_hit(rect_t r, float mx, float my);
static void close_station(void);
static int  open_station_for_block(block_t b, int x, int y, int z);
static void con_printf(const char *fmt, ...);

static GLFWwindow *g_win = NULL;
static int win_save_x = 80, win_save_y = 80, win_save_w = 1280, win_save_h = 720;

static void apply_fullscreen(int on) {
    if (!g_win) return;
    GLFWmonitor *mon = glfwGetWindowMonitor(g_win);
    if (on && !mon) {
        glfwGetWindowPos(g_win, &win_save_x, &win_save_y);
        glfwGetWindowSize(g_win, &win_save_w, &win_save_h);
        GLFWmonitor *m = glfwGetPrimaryMonitor();
        const GLFWvidmode *vm = glfwGetVideoMode(m);
        if (m && vm) glfwSetWindowMonitor(g_win, m, 0, 0, vm->width, vm->height, vm->refreshRate);
    } else if (!on && mon) {
        glfwSetWindowMonitor(g_win, NULL, win_save_x, win_save_y,
                             win_save_w > 0 ? win_save_w : 1280, win_save_h > 0 ? win_save_h : 720, 0);
    }
}
static GLuint      atlas_tex = 0;

static player_t player;
static world_t  *world = NULL;
static ui_t     *ui = NULL;
static text_t   *text = NULL;
static falling_system_t *falling = NULL;
static particle_system_t *particles = NULL;
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
static int    fall_was_grounded = 1;
static float  fall_peak_vel = 0.0f;

static char  save_path[192] = SAVES_DIR "/world/world.bw";

static const settings_t SETTINGS_DEFAULTS = SETTINGS_DEFAULT_INIT;
static settings_t settings = SETTINGS_DEFAULT_INIT;
static int settings_tab = 0;

#define MAX_WORLDS 16
static world_entry_t world_list[MAX_WORLDS];
static int           world_count = 0;

static char field_name[WORLD_NAME_MAX];
static char field_seed[16];
static int  field_focus = 0;
static int  create_gamemode = GAMEMODE_CREATIVE;
static int  create_allow_commands = 1;
static int  create_difficulty = 2;
static int  create_keep_inv = 0;
static int  create_mob_spawn = 1;

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
    "difficulty", "gamerule", "effect", "xp", "heal", "kill", "summon", "dawn",
};
static const char *const CMD_USAGE[] = {
    "/help",
    "/time set <day|noon|night|midnight>",
    "/tp <x> <y> <z>",
    "/gamemode <creative|survival>",
    "/give <item> [count]",
    "/seed",
    "/fly <on|off>",
    "/weather <clear|rain|snow|auto>",
    "/clear",
    "/difficulty <peaceful|easy|normal|hard>",
    "/gamerule <rule> <on|off>",
    "/effect <name> [seconds] [amp]",
    "/xp <amount>",
    "/heal",
    "/kill [mobs]",
    "/summon <pig|cow|chicken|sheep|zombie|skeleton|creeper|spider>",
    "/dawn <sunrise|day|noon|0.0-1.0>",
    "/permission <user> <command|all> <on|off>",
};
#define CMD_COUNT ((int)(sizeof CMD_NAMES / sizeof CMD_NAMES[0]))

static char g_local_user[USER_NAME_MAX] = "host";

static GLuint prog = 0;
static GLint mvp_loc, sun_dir_loc, sun_strength_loc, ambient_loc;
static GLint sea_level_loc, chunk_origin_loc, eye_in_water_loc, eye_depth_loc;
static GLint light_vp_loc, shadow_map_loc, shadow_enabled_loc, shadow_texel_loc;
static GLint shadow_world_texel_loc, eye_loc, fog_color_loc, fog_density_loc;

static GLint clip_below_loc, clip_height_loc;
static GLint blocklight_tex_loc, light_origin_loc, light_dim_loc, blocklight_enabled_loc;
static int   debug_view = 0;
static float g_fps = 0.0f;

static GLuint blit_prog = 0;
static GLint  blit_tex_loc;

static GLuint sky_prog = 0;
static GLint  sky_right_loc, sky_up_loc, sky_fwd_loc, sky_tanfov_loc, sky_aspect_loc;
static GLint  sky_sun_loc, sky_time_loc, sky_zenith_loc, sky_horizon_loc, sky_day_loc, sky_atmo_loc;
static GLint  sky_eye_loc, sky_overcast_loc;
static vec3   atmo_tint = {1.0f, 1.0f, 1.0f};

static GLuint depth_prog = 0;
static GLint  depth_mvp_loc;
static GLint  depth_atlas_loc;
static framebuffer_t *shadow_fb = NULL;

static GLuint water_prog = 0;
static GLint  water_mvp_loc, water_origin_loc, water_refl_loc;
static GLint  water_eye_loc, water_time_loc, water_sun_dir_loc, water_sun_strength_loc;
static GLint  water_fog_color_loc, water_fog_density_loc;
static GLint  water_depth_loc, water_screen_loc, water_near_loc, water_far_loc;
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
#define PACK_COLS  8
#define PACK_ROWS  4
#define PACK_COUNT (PACK_COLS * PACK_ROWS)
#define STACK_MAX  64
static inv_slot_t hotbar[HOTBAR_COUNT];
static inv_slot_t pack[PACK_COUNT];
static inv_slot_t armor_slots[4];
static inv_slot_t offhand;
static inv_slot_t ui_cursor;
static int        palette_scroll_row = 0;
static int        hotbar_sel = 0;

static GLuint item_atlas_tex = 0;
static GLuint hud_atlas_tex  = 0;

static int g_lmb_down = 0;
static int g_rmb_down = 0;

static station_t *open_station = NULL;
static inv_slot_t craft_grid[9];
static int        craft_dim = 2;

static net_t *net = NULL;
static int    mp_is_host = 0;
static int    mp_active = 0;
static int    mp_awaiting_welcome = 0;
static int32_t mp_my_id = 0;
static char   join_ip[64] = "127.0.0.1";
static char   player_name[NET_NAME_MAX] = "Player";
static char   mp_status[96] = "";
static double mp_state_timer = 0.0;
static double mp_time_timer = 0.0;

typedef struct {
    int     active;
    int32_t id;
    char    name[NET_NAME_MAX];
    vec3    pos, prev_pos;
    float   yaw, pitch, anim, move_timer;
} remote_player_t;
static remote_player_t remote_players[NET_MAX_PEERS];

typedef struct {
    uint8_t  used;
    char     name[NET_NAME_MAX];
    float    x, y, z, yaw, pitch;
    uint16_t blob_len;
    uint8_t  blob[NET_BLOB_MAX];
} mp_record_t;
static mp_record_t mp_records[WORLD_MAX_USERS];
static inv_slot_t anvil_in[2];
static entity_system_t *entities = NULL;
static toast_system_t  *toasts = NULL;

_Static_assert(HOTBAR_COUNT == SAVE_HOTBAR_SLOTS, "hotbar slot count must match save format");
_Static_assert(PACK_COUNT == SAVE_PACK_SLOTS, "pack slot count must match save format");

static inline int slot_empty(inv_slot_t s) { return s.id == 0 || s.count <= 0; }

static void sync_selected(void) {
    player.selected_block = slot_empty(hotbar[hotbar_sel]) ? 0 : (int)hotbar[hotbar_sel].id;
}

static void hotbar_select(int slot) {
    if (slot < 0 || slot >= HOTBAR_COUNT) return;
    if (slot != hotbar_sel) audio_play_hotbar(audio);
    hotbar_sel = slot;
    sync_selected();
}

static void reset_hotbar(item_id first) {
    inv_slot_t empty = {0, 0, 0};
    for (int i = 0; i < HOTBAR_COUNT; i++) hotbar[i] = empty;
    for (int i = 0; i < PACK_COUNT; i++)   pack[i]   = empty;
    for (int i = 0; i < 4; i++)            armor_slots[i] = empty;
    offhand = empty;
    ui_cursor = empty;
    if (first) { hotbar[0].id = first; hotbar[0].count = 1; }
    hotbar_sel = 0;
    sync_selected();
}

static int inventory_add(item_id id, int count, uint16_t dur) {
    if (id == 0 || count <= 0) return 0;
    int mstack = item_max_stack(id);
    if (mstack <= 1) {
        int maxd = item_max_durability(id);
        if (maxd > 0 && dur == 0) dur = (uint16_t)maxd;
    }
    inv_slot_t *pools[2] = { hotbar, pack };
    int sizes[2] = { HOTBAR_COUNT, PACK_COUNT };
    int added = 0;
    if (mstack > 1) {
        for (int p = 0; p < 2 && added < count; p++)
            for (int i = 0; i < sizes[p] && added < count; i++)
                if (pools[p][i].id == id && pools[p][i].count > 0 && pools[p][i].count < mstack) {
                    int can  = mstack - pools[p][i].count;
                    int take = (count - added < can) ? (count - added) : can;
                    pools[p][i].count = (int16_t)(pools[p][i].count + take);
                    added += take;
                }
    }
    for (int p = 0; p < 2 && added < count; p++)
        for (int i = 0; i < sizes[p] && added < count; i++)
            if (slot_empty(pools[p][i])) {
                int take = (mstack > 1) ? ((count - added < mstack) ? count - added : mstack) : 1;
                pools[p][i].id         = id;
                pools[p][i].count      = (int16_t)take;
                pools[p][i].durability = (uint16_t)(mstack > 1 ? 0 : dur);
                added += take;
            }
    sync_selected();
    return added;
}

static int inventory_consume_selected(void) {
    inv_slot_t *s = &hotbar[hotbar_sel];
    if (slot_empty(*s)) return 0;
    s->count--;
    if (s->count <= 0) { inv_slot_t e = {0,0,0}; *s = e; }
    sync_selected();
    return 1;
}

static item_id arrow_tip_potion(item_id arrow) {
    if (arrow == ITEM_ARROW_POISON)  return ITEM_POTION_POISON;
    if (arrow == ITEM_ARROW_HARMING) return ITEM_POTION_HARMING;
    return 0;
}

static item_id inventory_take_arrow(void) {
    static const item_id ORDER[3] = { ITEM_ARROW_POISON, ITEM_ARROW_HARMING, ITEM_ARROW };
    for (int k = 0; k < 3; k++) {
        item_id want = ORDER[k];
        for (int i = 0; i < HOTBAR_COUNT; i++)
            if (hotbar[i].id == want && hotbar[i].count > 0) {
                if (--hotbar[i].count <= 0) { inv_slot_t e = {0,0,0}; hotbar[i] = e; }
                sync_selected();
                return want;
            }
        for (int i = 0; i < PACK_COUNT; i++)
            if (pack[i].id == want && pack[i].count > 0) {
                if (--pack[i].count <= 0) { inv_slot_t e = {0,0,0}; pack[i] = e; }
                return want;
            }
    }
    return 0;
}

static void inventory_damage_selected(int dmg) {
    inv_slot_t *s = &hotbar[hotbar_sel];
    if (slot_empty(*s)) return;
    int maxd = item_max_durability(s->id);
    if (maxd <= 0) return;
    if ((int)s->durability <= dmg) {
        inv_slot_t e = {0,0,0}; *s = e;
        audio_play_item_break(audio);
    } else {
        s->durability = (uint16_t)((int)s->durability - dmg);
    }
    sync_selected();
}

static save_slot_t to_save_slot(inv_slot_t s)   { save_slot_t o = { (uint16_t)s.id, s.count, s.durability }; return o; }
static inv_slot_t  from_save_slot(save_slot_t s) { inv_slot_t  o = { (item_id)s.id, s.count, s.durability }; return o; }

static const item_id PALETTE[] = {
    BLOCK_STONE, BLOCK_COBBLESTONE, BLOCK_DIRT, BLOCK_GRASS, BLOCK_GRAVEL, BLOCK_SAND, BLOCK_SNOW, BLOCK_WOOD,
    BLOCK_LEAVES, BLOCK_LEAVES_PINE, BLOCK_LEAVES_ACACIA, BLOCK_LEAVES_SWAMP, BLOCK_LEAVES_JUNGLE, BLOCK_TALL_GRASS, BLOCK_PLANKS, BLOCK_QUARTZ,
    BLOCK_CONCRETE, BLOCK_GLASS, BLOCK_WOOL, BLOCK_GLOWSTONE, BLOCK_TORCH,
    BLOCK_COAL_ORE, BLOCK_IRON_ORE, BLOCK_DIAMOND_ORE, BLOCK_IRON_BLOCK, BLOCK_DIAMOND_BLOCK, BLOCK_COAL_BLOCK, BLOCK_WATER, BLOCK_DEEPSTONE,
    BLOCK_CRAFTING_TABLE, BLOCK_FURNACE, BLOCK_FORGE, BLOCK_ANVIL, BLOCK_CHEST, ITEM_BED, BLOCK_SAPLING, ITEM_SEEDS,
    ITEM_WOOD_PICKAXE, ITEM_STONE_PICKAXE, ITEM_IRON_PICKAXE, ITEM_DIAMOND_PICKAXE, ITEM_DIAMOND_AXE, ITEM_DIAMOND_SHOVEL, ITEM_DIAMOND_SWORD, ITEM_DIAMOND_HOE,
    ITEM_BOW, ITEM_ARROW, ITEM_IRON_HELMET, ITEM_IRON_CHEST, ITEM_IRON_LEGS, ITEM_IRON_BOOTS, ITEM_DIAMOND_CHEST, ITEM_DIAMOND_HELMET,
    ITEM_COAL, ITEM_IRON_INGOT, ITEM_DIAMOND, ITEM_STICK, ITEM_LEATHER, ITEM_STRING, ITEM_WHEAT, ITEM_BREAD,
    ITEM_APPLE, ITEM_COOKED_BEEF, ITEM_COOKED_PORK, ITEM_COOKED_CHICKEN, ITEM_BONE, ITEM_FEATHER, ITEM_GUNPOWDER, ITEM_FLINT,
    BLOCK_BREWING_STAND, ITEM_GLASS_BOTTLE, ITEM_WATER_BOTTLE, ITEM_NETHER_WART, ITEM_BLAZE_POWDER, ITEM_BLAZE_ROD, ITEM_FERMENTED_SPIDER_EYE, ITEM_SUGAR,
    ITEM_MAGMA_CREAM, ITEM_GLISTERING_MELON, ITEM_GHAST_TEAR, ITEM_PUFFERFISH, ITEM_AWKWARD_POTION, ITEM_REDSTONE, ITEM_ARROW_POISON, ITEM_ARROW_HARMING,
    ITEM_POTION_REGEN, ITEM_POTION_SWIFTNESS, ITEM_POTION_STRENGTH, ITEM_POTION_HEALING, ITEM_POTION_POISON, ITEM_POTION_FIRE_RES,
    ITEM_POTION_WATER_BREATH, ITEM_POTION_HARMING, ITEM_POTION_SLOWNESS, ITEM_POTION_WEAKNESS, ITEM_POTION_RESISTANCE,
    ITEM_POTION_REGEN + POTION_SPLASH_OFFSET, ITEM_POTION_HEALING + POTION_SPLASH_OFFSET, ITEM_POTION_HARMING + POTION_SPLASH_OFFSET,
    ITEM_POTION_POISON + POTION_SPLASH_OFFSET, ITEM_POTION_SWIFTNESS + POTION_SPLASH_OFFSET,
};
#define PALETTE_COUNT ((int)(sizeof PALETTE / sizeof PALETTE[0]))

static const char *block_name(item_id id) { return item_name(id); }

static void draw_item_icon(item_id id, float x, float y, float size, float aspect) {
    if (id == 0) return;
    const item_props_t *p = item_get(id);
    GLuint tex = (id < 128) ? atlas_tex : item_atlas_tex;
    ui_draw_atlas_icon_n(ui, tex, 16, p->icon_tx, p->icon_ty, x, y, size, aspect);
}

static void recompute_armor(void) {
    int pts = 0;
    for (int i = 0; i < 4; i++)
        if (!slot_empty(armor_slots[i])) pts += item_get(armor_slots[i].id)->armor_points;
    player.armor_points = pts;
}

static void ach(int id) { if (toasts) toast_unlock(toasts, id); }

static void reset_world_state(void) {
    if (entities) { for (int i = 0; i < MAX_ENTITIES; i++) entities->e[i].active = 0; entities->count = 0; }
    craft_stations_init();
    if (toasts) toast_set_bits(toasts, 0);
    open_station = NULL;
    inv_slot_t e = {0,0,0};
    for (int i = 0; i < 9; i++) craft_grid[i] = e;
    anvil_in[0] = e; anvil_in[1] = e; ui_cursor = e;
    memset(mp_records, 0, sizeof mp_records);
}

static int  ctx_try_pickup(item_id id, int count, uint16_t dur) { return inventory_add(id, count, dur); }
static void ctx_grant_xp(int amount) { player_add_xp(&player, amount); }
static void ctx_hurt_player(float dmg, int type, vec3 src) {
    if (player.is_dead) return;
    if (world_gamemode(world) == GAMEMODE_CREATIVE) return;
    int before = (int)player.health;
    player_take_damage(&player, dmg, (damage_type_t)type);
    if ((int)player.health < before) {
        vec3 d = vec3_sub(player.pos, src);
        player_apply_knockback(&player, d, 0.4f);
        audio_play_hurt(audio);
        int dur_loss = (int)dmg / 4; if (dur_loss < 1) dur_loss = 1;
        for (int i = 0; i < 4; i++) {
            if (slot_empty(armor_slots[i])) continue;
            if ((int)armor_slots[i].durability <= dur_loss) {
                inv_slot_t e = {0,0,0}; armor_slots[i] = e; audio_play_item_break(audio);
            } else {
                armor_slots[i].durability = (uint16_t)((int)armor_slots[i].durability - dur_loss);
            }
        }
        recompute_armor();
    }
}
static void ctx_apply_effect(int effect, int ticks, int amp) {
    status_add(&player, effect, ticks, amp);
}
static void ctx_break_block(int x, int y, int z) {
    if (!world_mob_griefing(world)) return;
    block_t b = world_get_block(world, x, y, z);
    if (b == BLOCK_AIR || b == BLOCK_DEEPSTONE) return;
    station_t *st = craft_station_get(x, y, z);
    if (st) {
        inv_slot_t *slots = NULL; int ns = 0;
        if (st->type == ST_CHEST)        { slots = st->u.chest;        ns = 27; }
        else if (st->type == ST_FURNACE) { slots = st->u.furnace.slot; ns = 3;  }
        else if (st->type == ST_BREWING) { slots = st->u.brewing.slot;  ns = 5;  }
        if (entities)
            for (int i = 0; i < ns; i++)
                if (!slot_empty(slots[i])) {
                    vec3 dp = { (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f };
                    entity_spawn_item(entities, dp, slots[i].id, slots[i].count, slots[i].durability);
                }
        craft_station_remove(x, y, z);
    }
    world_set_block(world, x, y, z, BLOCK_AIR);
    lighttex_mark_dirty(lighttex);
}
static player_ctx_t make_player_ctx(void) {
    player_ctx_t pc;
    pc.pos = player.pos;
    pc.eye = player_eye_pos(&player);
    pc.forward = player_forward(&player);
    pc.difficulty = world_difficulty(world);
    pc.pvp_enabled = world_pvp(world);
    pc.player_invulnerable = player.invuln_timer > 0.0f;
    pc.player_targetable = (world_gamemode(world) != GAMEMODE_CREATIVE) && !player.is_dead;
    pc.try_pickup = ctx_try_pickup;
    pc.grant_xp = ctx_grant_xp;
    pc.hurt_player = ctx_hurt_player;
    pc.break_block = ctx_break_block;
    pc.apply_effect = ctx_apply_effect;
    return pc;
}

static void clear_inventory(void) {
    inv_slot_t e = {0, 0, 0};
    for (int i = 0; i < HOTBAR_COUNT; i++) hotbar[i] = e;
    for (int i = 0; i < PACK_COUNT; i++)   pack[i]   = e;
    for (int i = 0; i < 4; i++)            armor_slots[i] = e;
    offhand = e;
    recompute_armor();
    sync_selected();
}

static void draw_hud_icon(int tx, int ty, float x, float y, float size, float aspect) {
    ui_draw_atlas_icon_n(ui, hud_atlas_tex, HUD_ATLAS_TILES, tx, ty, x, y, size, aspect);
}

static inv_slot_t make_slot(item_id id, int count) {
    inv_slot_t s = {0, 0, 0};
    if (id == 0 || count <= 0) return s;
    s.id = id;
    int ms = item_max_stack(id);
    s.count = (int16_t)(count > ms ? ms : count);
    if (ms <= 1) { int maxd = item_max_durability(id); s.durability = (uint16_t)(maxd > 0 ? maxd : 0); }
    return s;
}

static void draw_slot_contents(inv_slot_t s, float sx, float sy, float slot, float aspect) {
    if (slot_empty(s)) return;
    float inset = slot * 0.12f;
    draw_item_icon(s.id, sx + inset, sy + inset, slot - 2.0f * inset, aspect);
    int maxd = item_max_durability(s.id);
    if (maxd > 0 && s.durability < maxd) {
        float frac = (float)s.durability / (float)maxd;
        float bw = slot - 2.0f * inset;
        float bx = sx + inset, by = sy + slot - inset - 0.008f;
        ui_draw_rect(ui, bx, by, bw, 0.008f, 0.0f, 0.0f, 0.0f, 0.7f, aspect);
        float r = (frac > 0.5f) ? (1.0f - frac) * 2.0f : 1.0f;
        float g = (frac > 0.5f) ? 1.0f : frac * 2.0f;
        ui_draw_rect(ui, bx, by, bw * frac, 0.008f, r, g, 0.1f, 0.95f, aspect);
    }
    if (s.count > 1) {
        char cnt[8];
        snprintf(cnt, sizeof cnt, "%d", s.count);
        float cw = text_width(cnt, 0.016f);
        text_draw(text, cnt, sx + slot - cw - 0.006f, sy + slot - 0.022f, 0.016f,
                  1.0f, 1.0f, 1.0f, 0.95f, aspect);
    }
}

static void slot_left_click(inv_slot_t *s) {
    if (slot_empty(ui_cursor)) {
        if (!slot_empty(*s)) { ui_cursor = *s; inv_slot_t e = {0,0,0}; *s = e; }
    } else if (slot_empty(*s)) {
        *s = ui_cursor; inv_slot_t e = {0,0,0}; ui_cursor = e;
    } else if (ui_cursor.id == s->id && item_max_stack(s->id) > 1) {
        int space = item_max_stack(s->id) - s->count;
        int move = ui_cursor.count < space ? ui_cursor.count : space;
        s->count = (int16_t)(s->count + move);
        ui_cursor.count = (int16_t)(ui_cursor.count - move);
        if (ui_cursor.count <= 0) { inv_slot_t e = {0,0,0}; ui_cursor = e; }
    } else {
        inv_slot_t tmp = *s; *s = ui_cursor; ui_cursor = tmp;
    }
    audio_play_ui_click(audio);
}

static void slot_right_click(inv_slot_t *s) {
    if (slot_empty(ui_cursor)) {
        if (!slot_empty(*s)) {
            int half = (s->count + 1) / 2;
            ui_cursor = *s; ui_cursor.count = (int16_t)half;
            s->count = (int16_t)(s->count - half);
            if (s->count <= 0) { inv_slot_t e = {0,0,0}; *s = e; }
        }
    } else if (slot_empty(*s)) {
        *s = ui_cursor; s->count = 1; ui_cursor.count--;
        if (ui_cursor.count <= 0) { inv_slot_t e = {0,0,0}; ui_cursor = e; }
    } else if (ui_cursor.id == s->id && s->count < item_max_stack(s->id)) {
        s->count = (int16_t)(s->count + 1); ui_cursor.count--;
        if (ui_cursor.count <= 0) { inv_slot_t e = {0,0,0}; ui_cursor = e; }
    }
    audio_play_ui_click(audio);
}

static int slot_widget(inv_slot_t *s, float x, float y, float size, float aspect,
                       float mx, float my, int flags) {
    rect_t r = { x, y, size, size };
    int hov = rect_hit(r, mx, my);
    ui_draw_rect(ui, x, y, size, size,
                 hov ? 0.30f : 0.16f, hov ? 0.32f : 0.16f, hov ? 0.40f : 0.20f, 0.95f, aspect);
    draw_slot_contents(*s, x, y, size, aspect);
    if ((flags & 1) && ui_click && rect_hit(r, ui_click_x, ui_click_y)) { slot_left_click(s); return 1; }
    if ((flags & 2) && ui_rclick && rect_hit(r, ui_rclick_x, ui_rclick_y)) { slot_right_click(s); return 2; }
    return 0;
}

static void return_slot_to_inventory(inv_slot_t *s) {
    if (slot_empty(*s)) return;
    int added = inventory_add(s->id, s->count, s->durability);
    int rem = s->count - added;
    if (rem > 0 && entities) {
        vec3 dp = player_eye_pos(&player);
        entity_spawn_item(entities, dp, s->id, rem, s->durability);
    }
    inv_slot_t e = {0,0,0}; *s = e;
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
    float maxw = r.w - 0.03f;
    if (tw > maxw && tw > 0.0f) { scale *= maxw / tw; tw = text_width(label, scale); }
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
        case BIOME_SWAMP:  return (vec3){0.82f, 0.92f, 0.80f};
        case BIOME_JUNGLE: return (vec3){0.84f, 1.04f, 0.86f};
        case BIOME_ROCKY:  return (vec3){0.96f, 0.95f, 0.93f};
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
        if (b != BLOCK_AIR && b != BLOCK_LEAVES && b != BLOCK_GLASS && !block_is_water(b)) { covered = 1; break; }
    }
    weather_shelter += ((covered ? 1.0f : 0.0f) - weather_shelter) * (1.0f - expf(-4.0f * dt));

    float kp = 1.0f - expf(-0.7f * dt);
    float pr = pr_target * (1.0f - weather_shelter);
    weather_precip += (pr - weather_precip) * kp;

    int pmode = (weather_precip > 0.01f) ? weather_precip_type : WEATHER_CLEAR;
    weather_set(weather, pmode);
    weather_update(weather, eye, dt, weather_precip, world_seed(world));

    static float rain_acc = 0.0f;
    if (audio && weather_precip_type == WEATHER_RAIN && weather_precip > 0.12f) {
        rain_acc += dt;
        float interval = 0.26f - 0.12f * weather_precip;
        if (rain_acc >= interval) { rain_acc = 0.0f; audio_play_rain(audio, weather_precip); }
    } else {
        rain_acc = 0.0f;
    }
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
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex);
    glUniform1i(depth_atlas_loc, 0);
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
    int underwater = block_is_water(eye_block);

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
    if (underwater) {
        float d = (float)SEA_LEVEL - eye.y; if (d < 0.0f) d = 0.0f;
        float k = 1.0f - d * 0.02f; if (k < 0.45f) k = 0.45f;
        glClearColor(0.03f * k, 0.11f * k, 0.24f * k, 1.0f);
    } else {
        glClearColor(sr, sg, sb, 1.0f);
    }
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
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas_tex);
        world_render_reflection(world, mvp_loc, chunk_origin_loc, reflected_pv);
        framebuffer_unbind(fbw, fbh);
        glUniform1i(clip_below_loc, 0);
    }

    framebuffer_bind(ssao_depth_fb);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(depth_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex);
    glUniform1i(depth_atlas_loc, 0);
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
    glBindTexture(GL_TEXTURE_2D, atlas_tex);
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
        glUniform2f(water_screen_loc, (float)fbw, (float)fbh);
        glUniform1f(water_near_loc, 0.1f);
        glUniform1f(water_far_loc, 500.0f);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, framebuffer_color_tex(reflect_fb));
        glUniform1i(water_refl_loc, 4);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, framebuffer_depth_tex(ssao_depth_fb));
        glUniform1i(water_depth_loc, 6);
        glActiveTexture(GL_TEXTURE0);
        world_render_water(world, water_mvp_loc, water_origin_loc, pv);
    }

    falling_render(falling, pv, sun_dir, sun_strength, ambient);

    mob_render_env_t menv;
    menv.eye = eye;
    menv.eye_in_water = underwater ? 1 : 0;
    menv.eye_water_depth = underwater ? ((float)SEA_LEVEL - eye.y) : 0.0f;
    menv.sea_level = (float)SEA_LEVEL;
    menv.fog_color = fog_c;
    menv.fog_density = fog_density;
    menv.light_vp = light_vp;
    menv.shadow_enabled = sun_up ? 1 : 0;
    menv.shadow_texel = 1.0f / (float)SHADOW_MAP_SIZE;
    menv.shadow_world_texel = 2.0f * light_S / (float)SHADOW_MAP_SIZE;
    menv.shadow_map_tex = framebuffer_depth_tex(shadow_fb);
    menv.blocklight_tex = lighttex_texture(lighttex);
    menv.blocklight_enabled = blocklight_on ? 1 : 0;
    if (blocklight_on) {
        float lox, loy, loz;
        lighttex_origin(lighttex, &lox, &loy, &loz);
        menv.light_origin = (vec3){ lox, loy, loz };
        menv.light_dim = (float)lighttex_dim(lighttex);
    } else {
        menv.light_origin = (vec3){ 0.0f, 0.0f, 0.0f };
        menv.light_dim = 1.0f;
    }

    entity_render(entities, pv, sun_dir, sun_strength, ambient, &menv);

    if (mp_active) {
        vec3 apos[NET_MAX_PEERS]; float ayaw[NET_MAX_PEERS], aanim[NET_MAX_PEERS];
        int na = 0;
        for (int i = 0; i < NET_MAX_PEERS; i++)
            if (remote_players[i].active) {
                apos[na]  = remote_players[i].pos;
                ayaw[na]  = PI - remote_players[i].yaw;
                aanim[na] = remote_players[i].anim;
                na++;
            }
        if (na > 0) entity_render_avatars(pv, sun_dir, sun_strength, ambient, apos, ayaw, aanim, na, &menv);
    }

    {
        vec3 wup = {0.0f, 1.0f, 0.0f};
        vec3 cam_r = vec3_normalize(vec3_cross(fwd, wup));
        vec3 cam_u = vec3_normalize(vec3_cross(cam_r, fwd));
        particle_render(particles, pv, cam_r, cam_u, sun_dir, sun_strength, ambient);
    }

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

static void draw_debug_overlay(float aspect) {
    char line[256];
    int hh = (int)(world_time * 24.0f) % 24;
    int mm = (int)(world_time * 24.0f * 60.0f) % 60;
    const char *mode = (player.mode == PLAYER_FLYING) ? "fly" : "walk";
    text_draw(text, "BlockWorlds " GAME_VERSION, 0.012f, 0.018f, 0.022f, 0.95f, 0.97f, 1.0f, 1.0f, aspect);
    snprintf(line, sizeof line, "%.0f fps  |  %s  |  %02d:%02d", (double)g_fps, mode, hh, mm);
    text_draw(text, line, 0.012f, 0.050f, 0.022f, 0.8f, 0.9f, 1.0f, 1.0f, aspect);
    snprintf(line, sizeof line, "xyz  %.1f  %.1f  %.1f", (double)player.pos.x, (double)player.pos.y, (double)player.pos.z);
    text_draw(text, line, 0.012f, 0.082f, 0.022f, 0.8f, 0.9f, 1.0f, 1.0f, aspect);
    if (mp_active) {
        snprintf(line, sizeof line, "%s  |  hold: %s", mp_is_host ? "hosting" : "client",
                 block_name((block_t)player.selected_block));
    } else {
        snprintf(line, sizeof line, "hold: %s", block_name((block_t)player.selected_block));
    }
    text_draw(text, line, 0.012f, 0.114f, 0.022f, 0.8f, 0.9f, 1.0f, 1.0f, aspect);
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

        draw_slot_contents(hotbar[i], sx, sy, slot, aspect);

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
    if (player.selected_block != 0) {
        double elapsed = now - name_since;
        float a = (elapsed < 3.0) ? 0.95f : 0.95f * (float)(1.0 - (elapsed - 3.0) / 0.6);
        if (a > 0.0f) {
            const char *nm = block_name((item_id)player.selected_block);
            float ns = 0.03f;
            float nw = text_width(nm, ns);
            text_draw(text, nm, aspect * 0.5f - nw * 0.5f, y0 - pad - 0.045f, ns,
                      1.0f, 1.0f, 1.0f, a, aspect);
        }
    }
}

static void status_effect_tile(int e, int *tx, int *ty) {
    switch (e) {
        case EFFECT_REGENERATION:    *tx = 0; *ty = 3; break;
        case EFFECT_POISON:          *tx = 1; *ty = 3; break;
        case EFFECT_STRENGTH:        *tx = 2; *ty = 3; break;
        case EFFECT_WEAKNESS:        *tx = 3; *ty = 3; break;
        case EFFECT_SPEED:           *tx = 4; *ty = 3; break;
        case EFFECT_SLOWNESS:        *tx = 5; *ty = 3; break;
        case EFFECT_RESISTANCE:      *tx = 6; *ty = 3; break;
        case EFFECT_FIRE_RESISTANCE: *tx = 0; *ty = 4; break;
        case EFFECT_WATER_BREATHING: *tx = 1; *ty = 4; break;
        case EFFECT_HUNGER:          *tx = 2; *ty = 4; break;
        case EFFECT_INSTANT_HEALTH:  *tx = 3; *ty = 4; break;
        case EFFECT_INSTANT_DAMAGE:  *tx = 4; *ty = 4; break;
        default:                     *tx = 1; *ty = 5; break;
    }
}

static void draw_survival_hud(float aspect) {
    float slot = 0.058f, pad = 0.007f;
    int n = HOTBAR_COUNT;
    float total_w = (float)n * slot + (float)(n + 1) * pad;
    float x0 = aspect * 0.5f - total_w * 0.5f;
    float y0 = 1.0f - slot - 0.03f;
    float is = 0.026f, gap = 0.027f;

    {
        float bx = x0, by = y0 - 0.022f, bw = total_w, bh = 0.008f;
        ui_draw_rect(ui, bx, by, bw, bh, 0.10f, 0.10f, 0.12f, 0.85f, aspect);
        if (player.xp_progress > 0.0f)
            ui_draw_rect(ui, bx, by, bw * player.xp_progress, bh, 0.45f, 0.85f, 0.25f, 0.95f, aspect);
        if (player.xp_level > 0) {
            char lv[8]; snprintf(lv, sizeof lv, "%d", player.xp_level);
            float lw = text_width(lv, 0.026f);
            text_draw(text, lv, aspect * 0.5f - lw * 0.5f, by - 0.028f, 0.026f, 0.55f, 1.0f, 0.35f, 1.0f, aspect);
        }
    }

    float row_y = y0 - 0.056f;
    int poison = status_has(&player, EFFECT_POISON);
    for (int i = 0; i < 10; i++) {
        float hx = x0 + (float)i * gap;
        int tx = 2;
        if (player.health >= (float)(i * 2 + 2)) tx = poison ? 3 : 0;
        else if (player.health >= (float)(i * 2 + 1)) tx = 1;
        draw_hud_icon(tx, 0, hx, row_y, is, aspect);
    }
    for (int i = 0; i < 10; i++) {
        float hx = x0 + total_w - is - (float)i * gap;
        int tx = 2;
        if (player.hunger >= i * 2 + 2) tx = 0;
        else if (player.hunger >= i * 2 + 1) tx = 1;
        draw_hud_icon(tx, 1, hx, row_y, is, aspect);
    }
    if (player.armor_points > 0) {
        float ay = row_y - 0.030f;
        for (int i = 0; i < 10; i++) {
            float ax = x0 + (float)i * gap;
            int tx = 2;
            if (player.armor_points >= i * 2 + 2) tx = 0;
            else if (player.armor_points >= i * 2 + 1) tx = 1;
            draw_hud_icon(tx, 2, ax, ay, is, aspect);
        }
    }
    if (player.air < PLAYER_MAX_AIR) {
        float ay = row_y - 0.030f;
        int bubbles = (player.air * 10 + PLAYER_MAX_AIR - 1) / PLAYER_MAX_AIR;
        for (int i = 0; i < 10; i++) {
            float bx = x0 + total_w - is - (float)i * gap;
            draw_hud_icon((i < bubbles) ? 3 : 4, 1, bx, ay, is, aspect);
        }
    }

    int row = 0;
    for (int e = 1; e < EFFECT_COUNT; e++) {
        if (!status_has(&player, e)) continue;
        int tx, ty;
        status_effect_tile(e, &tx, &ty);
        draw_hud_icon(tx, ty, aspect - 0.05f, 0.04f + (float)row * 0.05f, 0.035f, aspect);
        row++;
    }

    if (player.damage_flash_timer > 0.0f) {
        float a = player.damage_flash_timer / 0.4f * 0.30f;
        ui_draw_rect(ui, 0.0f, 0.0f, aspect, 1.0f, 0.75f, 0.0f, 0.0f, a, aspect);
    }
}

static void settings_apply(void) {
    player_set_sensitivity(settings.mouse_sens);
    if (audio) {
        audio_set_volume(audio, settings.volume);
        audio_set_category_volume(audio, AUDIO_CAT_UI,     settings.vol_ui);
        audio_set_category_volume(audio, AUDIO_CAT_MOBS,   settings.vol_mobs);
        audio_set_category_volume(audio, AUDIO_CAT_PLAYER, settings.vol_player);
        audio_set_category_volume(audio, AUDIO_CAT_ENV,    settings.vol_env);
    }
    fps_shown = settings.fps_overlay;
}

static void clamp01(float *v) { if (*v < 0.0f) *v = 0.0f; if (*v > 1.0f) *v = 1.0f; }

static void settings_clamp(void) {
    if (settings.fov < 40.0f) settings.fov = 40.0f;
    if (settings.fov > 110.0f) settings.fov = 110.0f;
    if (settings.mouse_sens < 0.0005f) settings.mouse_sens = 0.0005f;
    if (settings.mouse_sens > 0.0060f) settings.mouse_sens = 0.0060f;
    clamp01(&settings.volume);
    clamp01(&settings.vol_ui); clamp01(&settings.vol_mobs);
    clamp01(&settings.vol_player); clamp01(&settings.vol_env);
    if (settings.render_distance < 2) settings.render_distance = 2;
    if (settings.render_distance > 8) settings.render_distance = 8;
    settings.fps_overlay = settings.fps_overlay ? 1 : 0;
    settings.fullscreen = settings.fullscreen ? 1 : 0;
    if (settings.mp_port < 1024)  settings.mp_port = 1024;
    if (settings.mp_port > 65535) settings.mp_port = 65535;
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
        else if (strcmp(key, "vol_ui") == 0)          settings.vol_ui = (float)atof(val);
        else if (strcmp(key, "vol_mobs") == 0)        settings.vol_mobs = (float)atof(val);
        else if (strcmp(key, "vol_player") == 0)      settings.vol_player = (float)atof(val);
        else if (strcmp(key, "vol_env") == 0)         settings.vol_env = (float)atof(val);
        else if (strcmp(key, "render_distance") == 0) settings.render_distance = atoi(val);
        else if (strcmp(key, "fps_overlay") == 0)     settings.fps_overlay = atoi(val);
        else if (strcmp(key, "fullscreen") == 0)      settings.fullscreen = atoi(val);
        else if (strcmp(key, "mp_port") == 0)         settings.mp_port = atoi(val);
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
    if (fabsf(settings.vol_ui - SETTINGS_DEFAULTS.vol_ui) > 1e-4f)         fprintf(f, "vol_ui=%g\n", (double)settings.vol_ui);
    if (fabsf(settings.vol_mobs - SETTINGS_DEFAULTS.vol_mobs) > 1e-4f)     fprintf(f, "vol_mobs=%g\n", (double)settings.vol_mobs);
    if (fabsf(settings.vol_player - SETTINGS_DEFAULTS.vol_player) > 1e-4f) fprintf(f, "vol_player=%g\n", (double)settings.vol_player);
    if (fabsf(settings.vol_env - SETTINGS_DEFAULTS.vol_env) > 1e-4f)       fprintf(f, "vol_env=%g\n", (double)settings.vol_env);
    if (settings.render_distance != SETTINGS_DEFAULTS.render_distance) fprintf(f, "render_distance=%d\n", settings.render_distance);
    if (settings.fps_overlay != SETTINGS_DEFAULTS.fps_overlay)         fprintf(f, "fps_overlay=%d\n", settings.fps_overlay);
    if (settings.fullscreen != SETTINGS_DEFAULTS.fullscreen)           fprintf(f, "fullscreen=%d\n", settings.fullscreen);
    if (settings.mp_port != SETTINGS_DEFAULTS.mp_port)                fprintf(f, "mp_port=%d\n", settings.mp_port);
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
    for (int i = 0; i < HOTBAR_COUNT; i++) out.hotbar[i] = to_save_slot(hotbar[i]);
    for (int i = 0; i < PACK_COUNT; i++)   out.pack[i]   = to_save_slot(pack[i]);
    for (int i = 0; i < 4; i++)            out.armor[i]  = to_save_slot(armor_slots[i]);
    out.offhand = to_save_slot(offhand);
    out.health      = (int16_t)(player.health + 0.5f);
    out.max_health  = (int16_t)(player.max_health + 0.5f);
    out.hunger      = (float)player.hunger;
    out.saturation  = player.saturation;
    out.exhaustion  = player.exhaustion;
    out.air         = (int16_t)player.air;
    out.xp_total    = player.xp_total;
    out.xp_level    = player.xp_level;
    out.is_dead     = (uint8_t)(player.is_dead ? 1 : 0);
    out.has_respawn = (uint8_t)(player.has_respawn ? 1 : 0);
    out.respawn_x   = (int32_t)player.respawn_x;
    out.respawn_y   = (int32_t)player.respawn_y;
    out.respawn_z   = (int32_t)player.respawn_z;
    {
        int ec = 0;
        for (int i = 0; i < MAX_STATUS_EFFECTS && ec < SAVE_EFFECTS_MAX; i++) {
            if (player.effects[i].id == 0 || player.effects[i].ticks_remaining == 0) continue;
            out.effects[ec].id    = player.effects[i].id;
            out.effects[ec].ticks = player.effects[i].ticks_remaining;
            out.effects[ec].amp   = player.effects[i].amplifier;
            ec++;
        }
        out.effect_count = (uint8_t)ec;
    }
    out.achievements = toasts ? toast_bits(toasts) : 0u;
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
    reset_world_state();
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
        for (int i = 0; i < HOTBAR_COUNT; i++) hotbar[i] = from_save_slot(saved.hotbar[i]);
        for (int i = 0; i < PACK_COUNT; i++)   pack[i]   = from_save_slot(saved.pack[i]);
        for (int i = 0; i < 4; i++)            armor_slots[i] = from_save_slot(saved.armor[i]);
        offhand = from_save_slot(saved.offhand);
        hotbar_sel = (saved.hotbar_sel >= 0 && saved.hotbar_sel < HOTBAR_COUNT) ? saved.hotbar_sel : 0;
        sync_selected();
    } else {
        reset_hotbar((item_id)player.selected_block);
    }
    if (saved.has_survival) {
        player.health     = (float)saved.health;
        player.max_health = (float)saved.max_health;
        player.hunger     = (int)saved.hunger;
        player.saturation = saved.saturation;
        player.exhaustion = saved.exhaustion;
        player.air        = (int)saved.air;
        player.xp_total = 0; player.xp_level = 0;
        player_add_xp(&player, saved.xp_total);
        player.is_dead    = saved.is_dead ? 1 : 0;
        player.has_respawn = saved.has_respawn ? 1 : 0;
        player.respawn_x = (float)saved.respawn_x;
        player.respawn_y = (float)saved.respawn_y;
        player.respawn_z = (float)saved.respawn_z;
        status_clear(&player);
        for (int i = 0; i < saved.effect_count; i++)
            status_add(&player, saved.effects[i].id, saved.effects[i].ticks, saved.effects[i].amp);
    }
    if (toasts) toast_set_bits(toasts, saved.achievements);
    recompute_armor();
    snprintf(save_path, sizeof save_path, "%s", path);
    apply_gamemode(world_gamemode(world));
    begin_loading();
    return 1;
}

static int enter_world_new(const char *name, uint32_t seed, int gamemode, int allow_commands) {
    world = world_create(seed, settings.render_distance);
    if (!world) return 0;
    world_set_meta(world, name, gamemode, allow_commands);
    world_init_settings(world, gamemode == GAMEMODE_SURVIVAL ? create_difficulty : 2,
                        create_mob_spawn, 1.0f, create_keep_inv);
    world_set_falling_system(world, falling);
    reset_world_state();
    spawn_player(seed);
    reset_hotbar(BLOCK_AIR);
    world_set_spawn(world, (int)player.pos.x, (int)player.pos.y, (int)player.pos.z);
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

static remote_player_t *mp_find(int32_t id) {
    for (int i = 0; i < NET_MAX_PEERS; i++)
        if (remote_players[i].active && remote_players[i].id == id) return &remote_players[i];
    return NULL;
}
static remote_player_t *mp_add(int32_t id, const char *name) {
    remote_player_t *r = mp_find(id);
    if (r) return r;
    for (int i = 0; i < NET_MAX_PEERS; i++)
        if (!remote_players[i].active) {
            r = &remote_players[i];
            memset(r, 0, sizeof *r);
            r->active = 1; r->id = id;
            snprintf(r->name, sizeof r->name, "%s", name ? name : "player");
            return r;
        }
    return NULL;
}
static void mp_remove(int32_t id) { remote_player_t *r = mp_find(id); if (r) r->active = 0; }

static mp_record_t *mp_record_find(const char *name) {
    for (int i = 0; i < WORLD_MAX_USERS; i++)
        if (mp_records[i].used && !strcmp(mp_records[i].name, name)) return &mp_records[i];
    return NULL;
}
static mp_record_t *mp_record_get(const char *name) {
    mp_record_t *r = mp_record_find(name);
    if (r) return r;
    for (int i = 0; i < WORLD_MAX_USERS; i++)
        if (!mp_records[i].used) {
            r = &mp_records[i];
            memset(r, 0, sizeof *r);
            r->used = 1;
            snprintf(r->name, sizeof r->name, "%s", name);
            return r;
        }
    return NULL;
}

static void blob_w16(uint8_t *o, int *n, int cap, uint16_t v) {
    if (*n + 2 <= cap) { o[*n] = (uint8_t)v; o[*n + 1] = (uint8_t)(v >> 8); }
    *n += 2;
}
static uint16_t blob_r16(const uint8_t *in, int *n, int len) {
    uint16_t v = 0;
    if (*n + 2 <= len) v = (uint16_t)(in[*n] | (uint16_t)(in[*n + 1] << 8));
    *n += 2;
    return v;
}
static void blob_put_slot(uint8_t *o, int *n, int cap, inv_slot_t s) {
    blob_w16(o, n, cap, s.id);
    blob_w16(o, n, cap, (uint16_t)s.count);
    blob_w16(o, n, cap, s.durability);
}
static inv_slot_t blob_get_slot(const uint8_t *in, int *n, int len) {
    inv_slot_t s;
    s.id = (item_id)blob_r16(in, n, len);
    s.count = (int16_t)blob_r16(in, n, len);
    s.durability = blob_r16(in, n, len);
    return s;
}
static uint16_t mp_pack_playerdata(uint8_t *out, int cap) {
    int n = 0;
    for (int i = 0; i < HOTBAR_COUNT; i++) blob_put_slot(out, &n, cap, hotbar[i]);
    for (int i = 0; i < PACK_COUNT; i++)   blob_put_slot(out, &n, cap, pack[i]);
    for (int i = 0; i < 4; i++)            blob_put_slot(out, &n, cap, armor_slots[i]);
    blob_put_slot(out, &n, cap, offhand);
    blob_w16(out, &n, cap, (uint16_t)(int)player.health);
    blob_w16(out, &n, cap, (uint16_t)player.hunger);
    return (uint16_t)n;
}
static void mp_unpack_playerdata(const uint8_t *in, uint16_t len) {
    int n = 0;
    for (int i = 0; i < HOTBAR_COUNT; i++) hotbar[i] = blob_get_slot(in, &n, len);
    for (int i = 0; i < PACK_COUNT; i++)   pack[i]   = blob_get_slot(in, &n, len);
    for (int i = 0; i < 4; i++)            armor_slots[i] = blob_get_slot(in, &n, len);
    offhand = blob_get_slot(in, &n, len);
    player.health = (float)blob_r16(in, &n, len);
    player.hunger = (int)blob_r16(in, &n, len);
    recompute_armor();
    sync_selected();
}

static void mp_apply_block(int x, int y, int z, uint16_t b) {
    if (!world) return;
    world_set_block(world, x, y, z, (block_t)b);
    if (lighttex) lighttex_mark_dirty(lighttex);
}
static void mp_stream_edit_cb(int x, int y, int z, block_t b, void *ud) {
    int32_t peer = *(int32_t *)ud;
    net_msg_t m; memset(&m, 0, sizeof m);
    m.op = NET_BLOCK; m.bx = x; m.by = y; m.bz = z; m.block = b;
    net_send(net, peer, &m);
}
static void mp_send_to_others(const net_msg_t *m, int32_t except) {
    for (int i = 0; i < NET_MAX_PEERS; i++)
        if (remote_players[i].active && remote_players[i].id != except)
            net_send(net, remote_players[i].id, m);
}

static void mp_create_client_world(uint32_t seed, int gamemode, vec3 spawn, float wtime) {
    if (world) { world_wait_idle(world); world_destroy(world); world = NULL; }
    world = world_create(seed, settings.render_distance);
    if (!world) { game_state = GS_TITLE; return; }
    world_set_meta(world, "Multiplayer", gamemode, 1);
    world_set_falling_system(world, falling);
    reset_world_state();
    player_init(&player, spawn);
    reset_hotbar(BLOCK_AIR);
    world_time = wtime;
    save_path[0] = '\0';
    apply_gamemode(gamemode);
    begin_loading();
}

static void mp_on_msg(const net_msg_t *m, int32_t from, void *ud) {
    (void)ud;
    if (mp_is_host) {
        switch (m->op) {
            case NET_HELLO: {
                int32_t pid = from;
                remote_player_t *r = mp_add(pid, m->text);
                int sx = 0, sy = 0, sz = 0;
                world_get_spawn(world, &sx, &sy, &sz);
                net_msg_t w; memset(&w, 0, sizeof w);
                w.op = NET_WELCOME; w.player_id = pid;
                w.seed = world_seed(world); w.gamemode = world_gamemode(world);
                w.x = (float)sx + 0.5f; w.y = (float)sy + 2.0f; w.z = (float)sz + 0.5f;
                w.world_time = world_time;
                net_send(net, pid, &w);

                net_msg_t ap; memset(&ap, 0, sizeof ap);
                ap.op = NET_ADD_PLAYER; ap.player_id = 0;
                snprintf(ap.text, sizeof ap.text, "%s", player_name);
                net_send(net, pid, &ap);
                for (int i = 0; i < NET_MAX_PEERS; i++)
                    if (remote_players[i].active && remote_players[i].id != pid) {
                        net_msg_t a2; memset(&a2, 0, sizeof a2);
                        a2.op = NET_ADD_PLAYER; a2.player_id = remote_players[i].id;
                        snprintf(a2.text, sizeof a2.text, "%s", remote_players[i].name);
                        net_send(net, pid, &a2);
                    }
                net_msg_t nb; memset(&nb, 0, sizeof nb);
                nb.op = NET_ADD_PLAYER; nb.player_id = pid;
                snprintf(nb.text, sizeof nb.text, "%s", m->text);
                mp_send_to_others(&nb, pid);

                int32_t target = pid;
                world_foreach_edit(world, mp_stream_edit_cb, &target);

                mp_record_t *rec = mp_record_find(m->text);
                if (rec && rec->blob_len) {
                    net_msg_t pd; memset(&pd, 0, sizeof pd);
                    pd.op = NET_PLAYERDATA; pd.player_id = pid;
                    pd.blob_len = rec->blob_len; memcpy(pd.blob, rec->blob, rec->blob_len);
                    net_send(net, pid, &pd);
                    if (r) r->pos = (vec3){ rec->x, rec->y, rec->z };
                } else {
                    net_msg_t rq; memset(&rq, 0, sizeof rq); rq.op = NET_REQ_DATA;
                    net_send(net, pid, &rq);
                }
                con_printf("%s joined the game", m->text);
                break;
            }
            case NET_STATE: {
                remote_player_t *r = mp_find(from);
                if (r) {
                    float ddx = m->x - r->pos.x, ddz = m->z - r->pos.z;
                    if (ddx * ddx + ddz * ddz > 0.0004f) r->move_timer = 0.35f;
                    r->prev_pos = r->pos; r->pos = (vec3){ m->x, m->y, m->z };
                    r->yaw = m->yaw; r->pitch = m->pitch;
                }
                net_msg_t s = *m; s.player_id = from;
                mp_send_to_others(&s, from);
                break;
            }
            case NET_BLOCK:
                mp_apply_block(m->bx, m->by, m->bz, m->block);
                mp_send_to_others(m, from);
                break;
            case NET_CHAT: {
                remote_player_t *r = mp_find(from);
                char line[176];
                snprintf(line, sizeof line, "<%s> %s", r ? r->name : "?", m->text);
                console_print(console, line);
                net_msg_t c; memset(&c, 0, sizeof c); c.op = NET_CHAT;
                snprintf(c.text, sizeof c.text, "%s", line);
                mp_send_to_others(&c, from);
                break;
            }
            case NET_PLAYERDATA: {
                remote_player_t *r = mp_find(from);
                if (r) {
                    mp_record_t *rec = mp_record_get(r->name);
                    if (rec) {
                        rec->blob_len = m->blob_len > NET_BLOB_MAX ? NET_BLOB_MAX : m->blob_len;
                        memcpy(rec->blob, m->blob, rec->blob_len);
                        rec->x = r->pos.x; rec->y = r->pos.y; rec->z = r->pos.z;
                    }
                }
                break;
            }
            case NET_DEL_PLAYER: {
                remote_player_t *r = mp_find(m->player_id);
                if (r) {
                    con_printf("%s left the game", r->name);
                    mp_record_t *rec = mp_record_get(r->name);
                    if (rec) { rec->x = r->pos.x; rec->y = r->pos.y; rec->z = r->pos.z; }
                    mp_remove(m->player_id);
                }
                net_msg_t d; memset(&d, 0, sizeof d); d.op = NET_DEL_PLAYER; d.player_id = m->player_id;
                mp_send_to_others(&d, m->player_id);
                break;
            }
            default: break;
        }
    } else {
        switch (m->op) {
            case NET_WELCOME:
                mp_my_id = m->player_id;
                mp_awaiting_welcome = 0;
                mp_create_client_world(m->seed, m->gamemode, (vec3){ m->x, m->y, m->z }, m->world_time);
                break;
            case NET_ADD_PLAYER:
                if (m->player_id != mp_my_id) mp_add(m->player_id, m->text);
                break;
            case NET_DEL_PLAYER:
                mp_remove(m->player_id);
                break;
            case NET_STATE: {
                if (m->player_id == mp_my_id) break;
                remote_player_t *r = mp_find(m->player_id);
                if (!r) r = mp_add(m->player_id, "player");
                if (r) {
                    float ddx = m->x - r->pos.x, ddz = m->z - r->pos.z;
                    if (ddx * ddx + ddz * ddz > 0.0004f) r->move_timer = 0.35f;
                    r->prev_pos = r->pos; r->pos = (vec3){ m->x, m->y, m->z };
                    r->yaw = m->yaw; r->pitch = m->pitch;
                }
                break;
            }
            case NET_BLOCK:  mp_apply_block(m->bx, m->by, m->bz, m->block); break;
            case NET_CHAT:   console_print(console, m->text); break;
            case NET_TIME:   world_time = m->world_time; break;
            case NET_REQ_DATA: {
                net_msg_t pd; memset(&pd, 0, sizeof pd);
                pd.op = NET_PLAYERDATA; pd.player_id = mp_my_id;
                pd.blob_len = mp_pack_playerdata(pd.blob, NET_BLOB_MAX);
                net_send(net, -1, &pd);
                break;
            }
            case NET_PLAYERDATA: mp_unpack_playerdata(m->blob, m->blob_len); break;
            default: break;
        }
    }
}

static void mp_send_block(int x, int y, int z, uint16_t b) {
    if (!mp_active || !net) return;
    net_msg_t m; memset(&m, 0, sizeof m);
    m.op = NET_BLOCK; m.bx = x; m.by = y; m.bz = z; m.block = b;
    net_send(net, -1, &m);
}

static void mp_disconnect(void) {
    if (net) { net_close(net); net = NULL; }
    mp_active = mp_is_host = mp_awaiting_welcome = 0;
    mp_my_id = 0;
    for (int i = 0; i < NET_MAX_PEERS; i++) remote_players[i].active = 0;
}

static int mp_open_to_lan(void) {
    if (mp_active || !world) return 0;
    net = net_host_start(settings.mp_port);
    if (!net) { con_printf("Could not host (port %d busy?)", settings.mp_port); return 0; }
    mp_active = 1; mp_is_host = 1; mp_my_id = 0;
    mp_state_timer = mp_time_timer = 0.0;
    snprintf(g_local_user, sizeof g_local_user, "host");
    for (int i = 0; i < NET_MAX_PEERS; i++) remote_players[i].active = 0;
    con_printf("Opened '%s' to LAN on port %d", world_name(world), settings.mp_port);
    return 1;
}

static void mp_host_world(const char *path) {
    if (!enter_world_load(path)) { con_printf("Failed to open world for hosting"); return; }
    mp_open_to_lan();
}

static void mp_join(const char *addr) {
    char host[64]; int port = settings.mp_port;
    snprintf(host, sizeof host, "%s", addr);
    char *colon = strchr(host, ':');
    if (colon) { *colon = '\0'; int p = atoi(colon + 1); if (p > 0 && p < 65536) port = p; }
    while (*host == ' ') memmove(host, host + 1, strlen(host));
    snprintf(mp_status, sizeof mp_status, "Connecting to %s:%d ...", host, port);
    net = net_client_connect(host, port);
    if (!net) {
        snprintf(mp_status, sizeof mp_status, "Could not connect to %s:%d", host, port);
        return;
    }
    mp_status[0] = '\0';
    mp_active = 1; mp_is_host = 0; mp_awaiting_welcome = 1;
    mp_state_timer = 0.0;
    for (int i = 0; i < NET_MAX_PEERS; i++) remote_players[i].active = 0;
    snprintf(g_local_user, sizeof g_local_user, "%s", player_name);
    net_msg_t h; memset(&h, 0, sizeof h); h.op = NET_HELLO;
    snprintf(h.text, sizeof h.text, "%s", player_name);
    net_send(net, -1, &h);
}

static int mp_players_save(void *f) {
    FILE *fp = (FILE *)f;
    uint32_t n = 0;
    for (int i = 0; i < WORLD_MAX_USERS; i++) if (mp_records[i].used) n++;
    if (fwrite(&n, 4, 1, fp) != 1) return 0;
    for (int i = 0; i < WORLD_MAX_USERS; i++) {
        if (!mp_records[i].used) continue;
        if (fwrite(mp_records[i].name, 1, NET_NAME_MAX, fp) != NET_NAME_MAX) return 0;
        if (fwrite(&mp_records[i].x, sizeof(float), 5, fp) != 5) return 0;
        if (fwrite(&mp_records[i].blob_len, 2, 1, fp) != 1) return 0;
        if (mp_records[i].blob_len &&
            fwrite(mp_records[i].blob, 1, mp_records[i].blob_len, fp) != mp_records[i].blob_len) return 0;
    }
    return 1;
}
static int mp_players_load(void *f, unsigned version) {
    (void)version;
    FILE *fp = (FILE *)f;
    uint32_t n = 0;
    if (fread(&n, 4, 1, fp) != 1) return 0;
    if (n > WORLD_MAX_USERS) n = WORLD_MAX_USERS;
    for (uint32_t i = 0; i < n; i++) {
        mp_record_t *r = &mp_records[i];
        memset(r, 0, sizeof *r);
        if (fread(r->name, 1, NET_NAME_MAX, fp) != NET_NAME_MAX) return 0;
        if (fread(&r->x, sizeof(float), 5, fp) != 5) return 0;
        if (fread(&r->blob_len, 2, 1, fp) != 1) return 0;
        if (r->blob_len > NET_BLOB_MAX) return 0;
        if (r->blob_len && fread(r->blob, 1, r->blob_len, fp) != r->blob_len) return 0;
        r->used = 1;
    }
    return 1;
}

static void quit_to_title(int save) {
    if (world) {
        if (save && save_path[0]) save_game();
        world_destroy(world);
        world = NULL;
    }
    mp_disconnect();
    if (console) console_close(console);
    console_open_key = -1;
    prev_underwater = -1;
    water_enter_time = -1.0;
    game_state = GS_TITLE;
    scan_worlds();
    glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
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

static int effect_from_name(const char *s) {
    if (!strcmp(s, "regeneration") || !strcmp(s, "regen")) return EFFECT_REGENERATION;
    if (!strcmp(s, "poison"))        return EFFECT_POISON;
    if (!strcmp(s, "strength"))      return EFFECT_STRENGTH;
    if (!strcmp(s, "weakness"))      return EFFECT_WEAKNESS;
    if (!strcmp(s, "speed"))         return EFFECT_SPEED;
    if (!strcmp(s, "slowness"))      return EFFECT_SLOWNESS;
    if (!strcmp(s, "resistance"))    return EFFECT_RESISTANCE;
    if (!strcmp(s, "fire_resistance") || !strcmp(s, "fireres")) return EFFECT_FIRE_RESISTANCE;
    if (!strcmp(s, "water_breathing") || !strcmp(s, "waterbreath")) return EFFECT_WATER_BREATHING;
    if (!strcmp(s, "hunger"))        return EFFECT_HUNGER;
    if (!strcmp(s, "instant_health")) return EFFECT_INSTANT_HEALTH;
    if (!strcmp(s, "instant_damage")) return EFFECT_INSTANT_DAMAGE;
    return 0;
}
static int species_from_name(const char *s) {
    if (!strcmp(s, "pig"))      return SP_PIG;
    if (!strcmp(s, "cow"))      return SP_COW;
    if (!strcmp(s, "chicken"))  return SP_CHICKEN;
    if (!strcmp(s, "sheep"))    return SP_SHEEP;
    if (!strcmp(s, "zombie"))   return SP_ZOMBIE;
    if (!strcmp(s, "skeleton")) return SP_SKELETON;
    if (!strcmp(s, "creeper"))  return SP_CREEPER;
    if (!strcmp(s, "spider"))   return SP_SPIDER;
    return -1;
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
    } else if (!strcmp(cmd, "dawn")) {
        if (argc >= 2) {
            float t;
            if (!parse_time_keyword(argv[1], &t)) t = (float)atof(argv[1]);
            world_set_dawn_time(world, t);
            con_printf("Dawn (sleep wake) set to %.3f", (double)world_dawn_time(world));
        } else {
            con_printf("usage: /dawn <sunrise|day|noon|0.0-1.0>  (current %.3f)",
                       (double)world_dawn_time(world));
        }
    } else if (!strcmp(cmd, "permission")) {
        if (argc >= 3 && !strcmp(argv[1], "clear")) {
            world_perm_clear(world, argv[2]);
            con_printf("Cleared permissions for %s", argv[2]);
        } else if (argc >= 4) {
            int on = !strcmp(argv[3], "on") || !strcmp(argv[3], "allow") || !strcmp(argv[3], "true");
            int ci = -1;
            if (strcmp(argv[2], "all") != 0 && strcmp(argv[2], "cheats") != 0) {
                for (int i = 0; i < CMD_COUNT; i++) if (!strcmp(argv[2], CMD_NAMES[i])) { ci = i; break; }
                if (ci < 0) { con_printf("Unknown command: %s", argv[2]); return; }
            }
            world_perm_set(world, argv[1], ci, on);
            con_printf("Permission: %s / %s -> %s", argv[1], argv[2], on ? "on" : "off");
        } else {
            con_printf("usage: /permission <user> <command|all> <on|off>  |  /permission clear <user>");
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
            item_id id = item_from_name(argv[1]);
            if (!id) con_printf("Unknown item: %s", argv[1]);
            else {
                int count = (argc >= 3) ? atoi(argv[2]) : 1;
                if (count < 1) count = 1;
                if (count > 999) count = 999;
                int added = inventory_add(id, count, 0);
                con_printf("Gave %d %s", added, item_name(id));
            }
        } else {
            con_printf("usage: /give <item> [count]");
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
    } else if (!strcmp(cmd, "difficulty")) {
        int d = -1;
        if (argc >= 2) {
            if      (!strcmp(argv[1], "peaceful")) d = DIFF_PEACEFUL;
            else if (!strcmp(argv[1], "easy"))     d = DIFF_EASY;
            else if (!strcmp(argv[1], "normal"))   d = DIFF_NORMAL;
            else if (!strcmp(argv[1], "hard"))     d = DIFF_HARD;
        }
        if (d < 0) con_printf("usage: /difficulty <peaceful|easy|normal|hard>");
        else { world_set_difficulty(world, d); con_printf("Difficulty: %s", argv[1]); }
    } else if (!strcmp(cmd, "gamerule")) {
        if (argc < 3) con_printf("usage: /gamerule <rule> <value>");
        else if (!strcmp(argv[1], "spawnRate")) { world_set_spawn_rate(world, (float)atof(argv[2])); con_printf("spawnRate = %s", argv[2]); }
        else if (!strcmp(argv[1], "smeltMult")) { world_set_smelt_mult(world, (float)atof(argv[2])); con_printf("smeltMult = %s", argv[2]); }
        else {
            int on = !strcmp(argv[2], "on") || !strcmp(argv[2], "true") || !strcmp(argv[2], "1");
            int g = -1;
            if      (!strcmp(argv[1], "mobSpawning"))   g = GR_MOB_SPAWNING;
            else if (!strcmp(argv[1], "keepInventory")) g = GR_KEEP_INVENTORY;
            else if (!strcmp(argv[1], "naturalRegen"))  g = GR_NATURAL_REGEN;
            else if (!strcmp(argv[1], "pvp"))           g = GR_PVP;
            else if (!strcmp(argv[1], "fallDamage"))    g = GR_FALL_DAMAGE;
            else if (!strcmp(argv[1], "hunger"))        g = GR_HUNGER;
            else if (!strcmp(argv[1], "daylightCycle")) g = GR_DAYLIGHT_CYCLE;
            else if (!strcmp(argv[1], "mobGriefing"))   g = GR_MOB_GRIEFING;
            if (g < 0) con_printf("Unknown rule");
            else { world_set_gamerule(world, (gamerule_t)g, on); con_printf("%s = %s", argv[1], on ? "on" : "off"); }
        }
    } else if (!strcmp(cmd, "effect")) {
        int eff = argc >= 2 ? effect_from_name(argv[1]) : 0;
        if (eff <= 0) con_printf("usage: /effect <regeneration|poison|strength|weakness|speed|slowness|resistance|...> [seconds] [amp]");
        else {
            int secs = argc >= 3 ? atoi(argv[2]) : 30;
            if (secs < 0) secs = 0;
            long t = (long)secs * 20; if (t > 65535) t = 65535;
            int amp = argc >= 4 ? atoi(argv[3]) : 0;
            if (amp < 0) amp = 0; if (amp > 9) amp = 9;
            status_add(&player, eff, (int)t, amp);
            con_printf("Applied %s", argv[1]);
        }
    } else if (!strcmp(cmd, "xp")) {
        if (argc >= 2) { int a = atoi(argv[1]); if (a > 0) player_add_xp(&player, a); con_printf("XP level %d", player.xp_level); }
        else con_printf("usage: /xp <amount>");
    } else if (!strcmp(cmd, "heal")) {
        player.health = player.max_health; player.hunger = PLAYER_MAX_HUNGER;
        player.saturation = 5.0f; player.air = PLAYER_MAX_AIR;
        con_printf("Healed");
    } else if (!strcmp(cmd, "kill")) {
        if (argc >= 2 && !strcmp(argv[1], "mobs")) {
            int n = 0;
            if (entities) for (int i = 0; i < entities->count; i++)
                if (entities->e[i].active && entities->e[i].kind == EK_MOB) { entities->e[i].active = 0; n++; }
            con_printf("Removed %d mobs", n);
        } else { player_take_damage(&player, 1000.0f, DMG_VOID); con_printf("Killed player"); }
    } else if (!strcmp(cmd, "summon")) {
        int sp = species_from_name(argc >= 2 ? argv[1] : "");
        if (sp < 0) con_printf("usage: /summon <pig|cow|chicken|sheep|zombie|skeleton|creeper|spider>");
        else if (entities) {
            vec3 f = player_forward(&player);
            vec3 p = { player.pos.x + f.x * 3.0f, player.pos.y + 1.0f, player.pos.z + f.z * 3.0f };
            entity_spawn_mob(entities, sp, p);
            con_printf("Summoned %s", argv[1]);
        }
    } else {
        con_printf("Unknown command: %s  (try /help)", cmd);
    }
}

static void execute_console_line(const char *line) {
    if (line[0] != '/') {
        if (!line[0]) return;
        if (mp_active && net) {
            net_msg_t c; memset(&c, 0, sizeof c); c.op = NET_CHAT;
            snprintf(c.text, sizeof c.text, "%s", line);
            if (mp_is_host) {
                char hl[176];
                snprintf(hl, sizeof hl, "<%s> %s", player_name, line);
                snprintf(c.text, sizeof c.text, "%s", hl);
                net_send(net, -1, &c);
                console_print(console, hl);
            } else {
                net_send(net, -1, &c);
            }
        } else {
            console_print(console, line);
        }
        return;
    }
    char buf[256];
    snprintf(buf, sizeof buf, "%s", line + 1);
    char *argv[16];
    int argc = tokenize(buf, argv, 16);
    if (argc == 0) return;
    int cmd_idx = -1;
    for (int i = 0; i < CMD_COUNT; i++) if (!strcmp(argv[0], CMD_NAMES[i])) { cmd_idx = i; break; }
    if (!world_perm_allowed(world, g_local_user, cmd_idx)) {
        console_print(console, "You don't have permission for that command");
        return;
    }
    run_command(argc, argv);
}

static void glfw_error_cb(int code, const char *desc) {
    fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

static int is_menu_state(void) {
    return game_state == GS_TITLE || game_state == GS_CREATE ||
           game_state == GS_SETTINGS || game_state == GS_PAUSED ||
           game_state == GS_INVENTORY || game_state == GS_TEXPACK ||
           game_state == GS_AUDIOPACK || game_state == GS_DEATH ||
           game_state == GS_CRAFTING || game_state == GS_FURNACE ||
           game_state == GS_FORGE || game_state == GS_ANVIL || game_state == GS_CHEST ||
           game_state == GS_BREWING || game_state == GS_MP_JOIN || game_state == GS_LOAD;
}

static void close_inventory(void) {
    for (int i = 0; i < 9; i++) return_slot_to_inventory(&craft_grid[i]);
    return_slot_to_inventory(&ui_cursor);
    recompute_armor();
    sync_selected();
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
        } else if (game_state == GS_LOAD) {
            game_state = GS_TITLE;
        } else if (game_state == GS_MP_JOIN) {
            if (mp_active) mp_disconnect();
            game_state = GS_TITLE;
        } else if (game_state == GS_SETTINGS) {
            settings_save();
            game_state = settings_return;
        } else if (game_state == GS_INVENTORY) {
            close_inventory();
        } else if (game_state == GS_CRAFTING || game_state == GS_FURNACE ||
                   game_state == GS_FORGE || game_state == GS_ANVIL || game_state == GS_CHEST ||
                   game_state == GS_BREWING) {
            close_station();
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
    if (game_state == GS_MP_JOIN && key == GLFW_KEY_BACKSPACE &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        char *fld = (field_focus == 0) ? join_ip : player_name;
        size_t L = strlen(fld);
        if (L > 0) fld[L - 1] = '\0';
    }
    if ((key == GLFW_KEY_F || key == GLFW_KEY_G) && action == GLFW_PRESS && game_state == GS_PLAYING) {
        fps_shown = !fps_shown;
    }
    if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
        settings.fullscreen = !settings.fullscreen;
        apply_fullscreen(settings.fullscreen);
        settings_save();
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
        inv_slot_t e = {0,0,0};
        for (int i = 0; i < 9; i++) craft_grid[i] = e;
        craft_dim = 2;
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
    if (codepoint < 0x20 || codepoint > 0x7e) return;
    char c = (char)codepoint;
    if (game_state == GS_MP_JOIN) {
        char *fld = (field_focus == 0) ? join_ip : player_name;
        size_t cap = (field_focus == 0) ? sizeof join_ip : sizeof player_name;
        size_t L = strlen(fld);
        if (L + 1 < cap) { fld[L] = c; fld[L + 1] = '\0'; }
        return;
    }
    if (game_state != GS_CREATE) return;
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
    if (game_state == GS_PLAYING && !console_is_open(console)) {
        if (button == GLFW_MOUSE_BUTTON_LEFT)  g_lmb_down = (action == GLFW_PRESS);
        if (button == GLFW_MOUSE_BUTTON_RIGHT) g_rmb_down = (action == GLFW_PRESS);
    }
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

    int survival = (world_gamemode(world) == GAMEMODE_SURVIVAL);

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        item_id selid = (item_id)player.selected_block;
        if (item_is_armor(selid)) {
            int slot = (int)item_get(selid)->armor_slot - 1;
            if (slot >= 0 && slot < 4) {
                inv_slot_t worn = armor_slots[slot];
                armor_slots[slot]  = hotbar[hotbar_sel];
                hotbar[hotbar_sel] = worn;
                recompute_armor();
                sync_selected();
                audio_play_pickup(audio);
            }
            return;
        }
    }

    vec3 eye = player_eye_pos(&player);
    vec3 dir = player_forward(&player);
    raycast_hit_t hit = raycast(world, eye, dir, REACH);

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        item_id sel = (item_id)player.selected_block;
        if (sel == ITEM_BOW && entities) {
            int creative = (world_gamemode(world) == GAMEMODE_CREATIVE);
            item_id arrow = creative ? ITEM_ARROW : inventory_take_arrow();
            if (arrow) {
                vec3 ep = player_eye_pos(&player), fd = player_forward(&player);
                vec3 spawn = vec3_add(ep, vec3_scale(fd, 0.4f));
                vec3 vel = vec3_scale(fd, 34.0f);
                entity_spawn_arrow(entities, spawn, vel, -2, arrow_tip_potion(arrow));
                audio_play_bow(audio);
                if (!creative && item_max_durability(ITEM_BOW) > 0) inventory_damage_selected(1);
            }
            return;
        }
        if (item_is_splash_potion(sel) && entities) {
            vec3 ep = player_eye_pos(&player), fd = player_forward(&player);
            vec3 spawn = vec3_add(ep, vec3_scale(fd, 0.4f));
            vec3 vel = vec3_scale(fd, 16.0f); vel.y += 2.0f;
            entity_spawn_thrown(entities, spawn, vel, sel, hotbar[hotbar_sel].durability);
            audio_play_bow(audio);
            inventory_consume_selected();
            return;
        }
        if (sel == ITEM_GLASS_BOTTLE) {
            for (float t = 0.0f; t < REACH; t += 0.25f) {
                int bx = (int)floorf(eye.x + dir.x * t);
                int by = (int)floorf(eye.y + dir.y * t);
                int bz = (int)floorf(eye.z + dir.z * t);
                if (block_is_water(world_get_block(world, bx, by, bz))) {
                    if (inventory_add(ITEM_WATER_BOTTLE, 1, 0)) {
                        inventory_consume_selected();
                        audio_play_splash(audio);
                    }
                    return;
                }
            }
        }
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT && entities) {
        float reach = REACH;
        if (hit.hit) {
            vec3 bc = { (float)hit.x + 0.5f, (float)hit.y + 0.5f, (float)hit.z + 0.5f };
            vec3 dd = vec3_sub(bc, eye);
            float bd = sqrtf(vec3_dot(dd, dd));
            if (bd < reach) reach = bd;
        }
        item_id held = (item_id)player.selected_block;
        int dmg = player_attack_damage(&player, held);
        int crit = (!player.grounded && player.vel.y < -0.1f);
        player_ctx_t pc = make_player_ctx();
        int hitres = entity_player_attack(entities, world, &pc, eye, dir, reach, dmg, 0.5f, crit);
        if (hitres) {
            if (item_max_durability(held) > 0) inventory_damage_selected(1);
            if (hitres == 2) ach(ACH_KILL_FIRST_MOB);
            return;
        }
    }

    if (!hit.hit) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        block_t broken = world_get_block(world, hit.x, hit.y, hit.z);
        if (broken == BLOCK_DEEPSTONE || broken == BLOCK_AIR) return;
        item_id held = (item_id)player.selected_block;
        world_set_block(world, hit.x, hit.y, hit.z, BLOCK_AIR);
        mp_send_block(hit.x, hit.y, hit.z, BLOCK_AIR);
        lighttex_mark_dirty(lighttex);
        audio_play_break(audio, broken);
        particle_block_break(particles, hit.x, hit.y, hit.z, broken);

        station_t *st = craft_station_get(hit.x, hit.y, hit.z);
        if (st) {
            inv_slot_t *slots = NULL; int ns = 0;
            if (st->type == ST_CHEST)        { slots = st->u.chest;        ns = 27; }
            else if (st->type == ST_FURNACE) { slots = st->u.furnace.slot; ns = 3;  }
            else if (st->type == ST_BREWING) { slots = st->u.brewing.slot;  ns = 5;  }
            for (int i = 0; i < ns; i++)
                if (!slot_empty(slots[i])) {
                    int added = inventory_add(slots[i].id, slots[i].count, slots[i].durability);
                    int rem = slots[i].count - added;
                    if (rem > 0 && entities) {
                        vec3 dp = { (float)hit.x + 0.5f, (float)hit.y + 0.5f, (float)hit.z + 0.5f };
                        entity_spawn_item(entities, dp, slots[i].id, rem, slots[i].durability);
                    }
                }
            craft_station_remove(hit.x, hit.y, hit.z);
        }
        if (broken == BLOCK_BED_FOOT || broken == BLOCK_BED_HEAD) {
            static const int OFF[6][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
            block_t want = (broken == BLOCK_BED_FOOT) ? BLOCK_BED_HEAD : BLOCK_BED_FOOT;
            for (int k = 0; k < 6; k++) {
                int ox = hit.x + OFF[k][0], oy = hit.y + OFF[k][1], oz = hit.z + OFF[k][2];
                if (world_get_block(world, ox, oy, oz) == want) { world_set_block(world, ox, oy, oz, BLOCK_AIR); break; }
            }
        }
        if (broken == BLOCK_WOOD)             ach(ACH_GET_WOOD);
        else if (broken == BLOCK_STONE)       ach(ACH_MINE_STONE);
        else if (broken == BLOCK_IRON_ORE)    ach(ACH_GET_IRON);
        else if (broken == BLOCK_DIAMOND_ORE) ach(ACH_DIAMONDS);

        if (survival) {
            item_drop_t drops[4];
            int nd = block_drops(broken, held, drops, 4);
            int picked = 0;
            for (int d = 0; d < nd; d++)
                picked += inventory_add(drops[d].id, drops[d].count, 0);
            if (picked) audio_play_pickup(audio);
            int xp = (broken == BLOCK_COAL_ORE) ? rand() % 3
                   : (broken == BLOCK_DIAMOND_ORE) ? 3 + rand() % 5 : 0;
            if (xp > 0) player_add_xp(&player, xp);
            if (item_max_durability(held) > 0) inventory_damage_selected(1);
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        block_t tb = world_get_block(world, hit.x, hit.y, hit.z);
        if (!player.crouching && open_station_for_block(tb, hit.x, hit.y, hit.z)) return;

        if (tb == BLOCK_BED_FOOT || tb == BLOCK_BED_HEAD) {
            player_set_respawn(&player, (float)hit.x + 0.5f, (float)hit.y, (float)hit.z + 0.5f);
            if (world_sun_dir_y(world) < 0.0f && world_daylight_cycle(world)) {
                world_time = world_dawn_time(world);
                ach(ACH_SLEEP);
            }
            audio_play_door(audio, 0);
            return;
        }

        item_id sel = (item_id)player.selected_block;
        if (sel == 0) return;
        const item_props_t *sp = item_get(sel);
        if (!sp->is_placeable) return;
        block_t place_block = item_is_block(sel) ? (block_t)sel : (block_t)sp->places_block;
        if (place_block == 0) return;
        int px = hit.x + hit.face_x;
        int py = hit.y + hit.face_y;
        int pz = hit.z + hit.face_z;
        if (world_get_block(world, px, py, pz) != BLOCK_AIR &&
            !block_is_water(world_get_block(world, px, py, pz))) return;
        float fminx = (float)px, fminy = (float)py, fminz = (float)pz;
        float fmaxx = fminx + 1.0f, fmaxy = fminy + 1.0f, fmaxz = fminz + 1.0f;
        float pminx = player.pos.x - 0.3f, pmaxx = player.pos.x + 0.3f;
        float pminy = player.pos.y, pmaxy = player.pos.y + 1.8f;
        float pminz = player.pos.z - 0.3f, pmaxz = player.pos.z + 0.3f;
        int overlap = !(fmaxx <= pminx || fminx >= pmaxx ||
                        fmaxy <= pminy || fminy >= pmaxy ||
                        fmaxz <= pminz || fminz >= pmaxz);
        if (overlap && block_render_class(place_block) == RCLASS_SOLID) return;

        if (place_block == BLOCK_BED_FOOT) {
            vec3 f = player_forward(&player);
            int hdx = 0, hdz = 0;
            if (fabsf(f.x) > fabsf(f.z)) hdx = f.x > 0 ? 1 : -1; else hdz = f.z > 0 ? 1 : -1;
            int hx = px + hdx, hz = pz + hdz;
            block_t hb = world_get_block(world, hx, py, hz);
            if (hb != BLOCK_AIR && !block_is_water(hb)) return;
            world_set_block(world, px, py, pz, BLOCK_BED_FOOT);
            world_set_block(world, hx, py, hz, BLOCK_BED_HEAD);
            mp_send_block(px, py, pz, BLOCK_BED_FOOT);
            mp_send_block(hx, py, hz, BLOCK_BED_HEAD);
            lighttex_mark_dirty(lighttex);
            audio_play_place(audio, BLOCK_BED_FOOT);
            if (survival) inventory_consume_selected();
            return;
        }

        world_set_block(world, px, py, pz, place_block);
        mp_send_block(px, py, pz, place_block);
        lighttex_mark_dirty(lighttex);
        audio_play_place(audio, place_block);
        if (place_block == BLOCK_TORCH) ach(ACH_PLANT_TORCH);
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
    if (game_state == GS_INVENTORY && world && world_gamemode(world) == GAMEMODE_CREATIVE) {
        if (yoff > 0.0)      palette_scroll_row--;
        else if (yoff < 0.0) palette_scroll_row++;
        if (palette_scroll_row < 0) palette_scroll_row = 0;
        return;
    }
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
    if (ui) ui_set_block_atlas(ui, atlas_tex);
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

static void begin_new_world(void) {
    field_name[0] = '\0';
    snprintf(field_seed, sizeof field_seed, "%u", random_seed());
    field_focus = 0;
    create_gamemode = GAMEMODE_CREATIVE;
    create_allow_commands = 1;
    create_difficulty = 2; create_keep_inv = 0; create_mob_spawn = 1;
    game_state = GS_CREATE;
}

static void draw_title_screen(float aspect, float mx, float my) {
    glClearColor(0.09f, 0.11f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_centered_text("BLOCKWORLDS", 0.11f, 0.10f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);
    draw_centered_text(GAME_VERSION, 0.215f, 0.024f, aspect, 0.5f, 0.6f, 0.78f, 1.0f);

    float cx = aspect * 0.5f;
    float bw = 0.46f, bh = 0.075f, by = 0.34f, bgap = 0.092f;
    rect_t bnew  = { cx - bw * 0.5f, by + 0.0f * bgap, bw, bh };
    rect_t bload = { cx - bw * 0.5f, by + 1.0f * bgap, bw, bh };
    rect_t bjoin = { cx - bw * 0.5f, by + 2.0f * bgap, bw, bh };
    rect_t bset  = { cx - bw * 0.5f, by + 3.0f * bgap, bw, bh };
    rect_t bquit = { cx - bw * 0.5f, by + 4.0f * bgap, bw, bh };

    if (do_button(bnew, "New World", aspect, mx, my))  begin_new_world();
    if (do_button(bload, "Load World", aspect, mx, my)) { scan_worlds(); game_state = GS_LOAD; }
    if (do_button(bjoin, "Join Server", aspect, mx, my)) { field_focus = 0; mp_status[0] = '\0'; game_state = GS_MP_JOIN; }
    if (do_button(bset, "Settings", aspect, mx, my)) { settings_return = GS_TITLE; game_state = GS_SETTINGS; }
    if (do_button(bquit, "Quit", aspect, mx, my)) glfwSetWindowShouldClose(g_win, 1);

    update_pack_availability();
    if (have_skins || have_audiopacks) {
        float pw = 0.26f, ph = 0.06f, pgap = 0.02f;
        if (have_skins && have_audiopacks) {
            rect_t bt = { cx - pw - pgap * 0.5f, 0.90f, pw, ph };
            rect_t ba = { cx + pgap * 0.5f,       0.90f, pw, ph };
            if (do_button(bt, "Texture Pack", aspect, mx, my)) { scan_skins(); game_state = GS_TEXPACK; }
            if (do_button(ba, "Audio Pack",   aspect, mx, my)) { scan_audio_packs(); game_state = GS_AUDIOPACK; }
        } else if (have_skins) {
            rect_t bt = { cx - pw * 0.5f, 0.90f, pw, ph };
            if (do_button(bt, "Texture Pack", aspect, mx, my)) { scan_skins(); game_state = GS_TEXPACK; }
        } else {
            rect_t ba = { cx - pw * 0.5f, 0.90f, pw, ph };
            if (do_button(ba, "Audio Pack", aspect, mx, my)) { scan_audio_packs(); game_state = GS_AUDIOPACK; }
        }
    }
}

static void draw_load_world_screen(float aspect, float mx, float my) {
    glClearColor(0.09f, 0.11f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_centered_text("LOAD WORLD", 0.08f, 0.07f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);
    draw_centered_text("select a world to play, Host to LAN, or X to delete", 0.18f, 0.024f,
                       aspect, 0.6f, 0.7f, 0.85f, 1.0f);

    float cx = aspect * 0.5f;
    float rw = 0.66f, rh = 0.072f, gap = 0.012f;
    float y = 0.26f;
    int shown = world_count < 8 ? world_count : 8;
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
        rect_t hostb = { r.x + r.w - 0.175f, r.y + (rh - 0.05f) * 0.5f, 0.11f, 0.05f };
        if (do_button(hostb, "Host", aspect, mx, my)) { mp_host_world(we->path); return; }
        rect_t del = { r.x + r.w - 0.055f, r.y + (rh - 0.05f) * 0.5f, 0.045f, 0.05f };
        if (do_button(del, "X", aspect, mx, my)) { rmtree(we->dir); scan_worlds(); return; }
        if (clicked) { enter_world_load(we->path); return; }
        y += rh + gap;
    }
    if (world_count == 0)
        draw_centered_text("no worlds yet - go back and pick New World", 0.42f, 0.026f,
                           aspect, 0.55f, 0.6f, 0.7f, 1.0f);

    rect_t bnew  = { cx - 0.31f, 0.88f, 0.30f, 0.075f };
    rect_t bback = { cx + 0.01f, 0.88f, 0.30f, 0.075f };
    if (do_button(bnew, "New World", aspect, mx, my)) begin_new_world();
    if (do_button(bback, "Back", aspect, mx, my)) game_state = GS_TITLE;
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

static void draw_mp_join_screen(float aspect, float mx, float my) {
    glClearColor(0.09f, 0.11f, 0.17f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw_centered_text("JOIN SERVER", 0.12f, 0.06f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);
    if (mp_awaiting_welcome) {
        draw_centered_text("Connecting to host...", 0.45f, 0.034f, aspect, 0.8f, 0.9f, 1.0f, 1.0f);
        return;
    }
    float cx = aspect * 0.5f, fw = 0.5f;
    rect_t fip  = { cx - fw * 0.5f, 0.30f, fw, 0.06f };
    rect_t fnm  = { cx - fw * 0.5f, 0.45f, fw, 0.06f };
    if (ui_click && rect_hit(fip, ui_click_x, ui_click_y)) field_focus = 0;
    if (ui_click && rect_hit(fnm, ui_click_x, ui_click_y)) field_focus = 1;
    draw_field(fip, "Host IP (or ip:port)", join_ip, field_focus == 0, aspect);
    draw_field(fnm, "Your name", player_name, field_focus == 1, aspect);
    rect_t bcon  = { cx - 0.31f, 0.60f, 0.30f, 0.075f };
    rect_t bback = { cx + 0.01f, 0.60f, 0.30f, 0.075f };
    if (do_button(bcon, "Connect", aspect, mx, my)) mp_join(join_ip);
    if (do_button(bback, "Back", aspect, mx, my)) game_state = GS_TITLE;
    if (mp_status[0]) {
        float tw = text_width(mp_status, 0.026f);
        text_draw(text, mp_status, cx - tw * 0.5f, 0.72f, 0.026f, 1.0f, 0.55f, 0.5f, 1.0f, aspect);
    }
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

    if (create_gamemode == GAMEMODE_SURVIVAL) {
        text_draw(text, "Difficulty", cx - fw * 0.5f, 0.705f, 0.024f, 0.7f, 0.78f, 0.9f, 1.0f, aspect);
        static const char *const DN[4] = { "Peaceful", "Easy", "Normal", "Hard" };
        float dw = (fw - 0.03f * 3.0f) / 4.0f;
        for (int i = 0; i < 4; i++) {
            rect_t db = { cx - fw * 0.5f + (float)i * (dw + 0.03f), 0.735f, dw, 0.05f };
            int sel = (create_difficulty == i);
            ui_draw_rect(ui, db.x, db.y, db.w, db.h,
                         sel ? 0.25f : 0.15f, sel ? 0.45f : 0.16f, sel ? 0.30f : 0.18f, 0.95f, aspect);
            float tw = text_width(DN[i], 0.020f);
            text_draw(text, DN[i], db.x + db.w * 0.5f - tw * 0.5f, db.y + 0.015f, 0.020f, 1, 1, 1, 1, aspect);
            if (ui_click && rect_hit(db, ui_click_x, ui_click_y)) create_difficulty = i;
        }
    }

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

static void settings_tab_button(rect_t r, const char *label, int tab, float aspect, float mx, float my) {
    int active = (settings_tab == tab);
    int clicked = button_box(r, aspect, mx, my);
    float sc = 0.030f;
    float tw = text_width(label, sc);
    float lr = active ? 0.65f : 0.88f, lg = active ? 0.85f : 0.88f, lb = 1.0f;
    text_draw(text, label, r.x + (r.w - tw) * 0.5f, r.y + (r.h - text_height(sc)) * 0.5f,
              sc, lr, lg, lb, 1.0f, aspect);
    if (active) ui_draw_rect(ui, r.x, r.y + r.h - 0.005f, r.w, 0.005f, 0.6f, 0.8f, 1.0f, 0.95f, aspect);
    if (clicked) settings_tab = tab;
}

static void draw_settings_screen(float aspect, float mx, float my) {
    glClearColor(0.08f, 0.10f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_centered_text("SETTINGS", 0.07f, 0.06f, aspect, 0.85f, 0.92f, 1.0f, 1.0f);

    float cx = aspect * 0.5f;
    static const char *TABS[4] = { "Video", "Controls", "Audio", "Game" };
    float tw = 0.215f, tg = 0.015f;
    float tx0 = cx - (4.0f * tw + 3.0f * tg) * 0.5f;
    for (int i = 0; i < 4; i++) {
        rect_t tr = { tx0 + (float)i * (tw + tg), 0.155f, tw, 0.06f };
        settings_tab_button(tr, TABS[i], i, aspect, mx, my);
    }

    int changed = 0, dm = 0, dp = 0;
    char val[32];
    float y = 0.28f, dy = 0.085f;

    if (settings_tab == 0) {
        snprintf(val, sizeof val, "%d", (int)(settings.fov + 0.5f));
        setting_row(y, "Field of view", val, aspect, mx, my, &dm, &dp);
        if (dm) { settings.fov -= 5.0f; changed = 1; } if (dp) { settings.fov += 5.0f; changed = 1; }
        y += dy;
        snprintf(val, sizeof val, "%d", settings.render_distance);
        setting_row(y, "Render distance", val, aspect, mx, my, &dm, &dp);
        if (dm) { settings.render_distance -= 1; changed = 1; } if (dp) { settings.render_distance += 1; changed = 1; }
        y += dy;
        setting_row(y, "Fullscreen", settings.fullscreen ? "on" : "off", aspect, mx, my, &dm, &dp);
        if (dm || dp) { settings.fullscreen = !settings.fullscreen; apply_fullscreen(settings.fullscreen); changed = 1; }
        y += dy;
        draw_centered_text("render distance applies on next world load", 0.66f, 0.020f,
                           aspect, 0.55f, 0.6f, 0.7f, 1.0f);
    } else if (settings_tab == 1) {
        snprintf(val, sizeof val, "%d", (int)(settings.mouse_sens * 10000.0f + 0.5f));
        setting_row(y, "Mouse sensitivity", val, aspect, mx, my, &dm, &dp);
        if (dm) { settings.mouse_sens -= 0.0005f; changed = 1; } if (dp) { settings.mouse_sens += 0.0005f; changed = 1; }
    } else if (settings_tab == 2) {
        struct { const char *name; float *v; } rows[5] = {
            { "Master", &settings.volume }, { "UI", &settings.vol_ui }, { "Mobs", &settings.vol_mobs },
            { "Player", &settings.vol_player }, { "Environment", &settings.vol_env },
        };
        for (int i = 0; i < 5; i++) {
            snprintf(val, sizeof val, "%d%%", (int)(*rows[i].v * 100.0f + 0.5f));
            setting_row(y, rows[i].name, val, aspect, mx, my, &dm, &dp);
            if (dm) { *rows[i].v -= 0.1f; changed = 1; } if (dp) { *rows[i].v += 0.1f; changed = 1; }
            y += dy;
        }
    } else {
        setting_row(y, "Debug overlay (G)", settings.fps_overlay ? "on" : "off", aspect, mx, my, &dm, &dp);
        if (dm || dp) { settings.fps_overlay = !settings.fps_overlay; changed = 1; }
        y += dy;
        snprintf(val, sizeof val, "%d", settings.mp_port);
        setting_row(y, "Multiplayer port", val, aspect, mx, my, &dm, &dp);
        if (dm) { settings.mp_port -= 1; changed = 1; } if (dp) { settings.mp_port += 1; changed = 1; }
        y += dy;
        draw_centered_text("port change applies to the next host/join", 0.66f, 0.020f,
                           aspect, 0.55f, 0.6f, 0.7f, 1.0f);
    }

    rect_t bback = { cx - 0.15f, 0.84f, 0.30f, 0.08f };
    if (do_button(bback, "Back", aspect, mx, my)) {
        settings_save();
        game_state = settings_return;
    }

    if (changed) {
        settings_clamp();
        settings_apply();
    }
}

static void craft_unlock_achievement(item_id out) {
    if (out == BLOCK_PLANKS)              ach(ACH_MAKE_PLANKS);
    else if (out == BLOCK_CRAFTING_TABLE) ach(ACH_CRAFTING_TABLE);
    else if (out == BLOCK_FURNACE)        ach(ACH_MAKE_FURNACE);
    else if (out == ITEM_BED)             ach(ACH_MAKE_BED);
    else if (item_is_tool(out))           ach(ACH_MAKE_TOOL);
}

static void draw_cursor_stack(float aspect, float mx, float my) {
    if (slot_empty(ui_cursor)) return;
    float s = 0.05f;
    draw_slot_contents(ui_cursor, mx - s * 0.5f, my - s * 0.5f, s, aspect);
}

static void draw_inv_panel(float aspect, float mx, float my) {
    float cx = aspect * 0.5f;
    float cell = 0.062f, gap = 0.010f;
    int cols = PACK_COLS;
    float grid_w = (float)cols * cell + (float)(cols - 1) * gap;
    float gx0 = cx - grid_w * 0.5f, gy0 = 0.50f;
    for (int i = 0; i < PACK_COUNT; i++) {
        int r = i / cols, c = i % cols;
        slot_widget(&pack[i], gx0 + (float)c * (cell + gap), gy0 + (float)r * (cell + gap),
                    cell, aspect, mx, my, 3);
    }
    float ax = gx0 - cell - 0.018f;
    for (int i = 0; i < 4; i++) {
        if (slot_widget(&armor_slots[i], ax, gy0 + (float)i * (cell + gap), cell, aspect, mx, my, 3))
            recompute_armor();
    }
    int n = HOTBAR_COUNT;
    float total = (float)n * cell + (float)(n - 1) * gap;
    float hx0 = cx - total * 0.5f, hy = 0.85f;
    for (int i = 0; i < n; i++) {
        float sx = hx0 + (float)i * (cell + gap);
        slot_widget(&hotbar[i], sx, hy, cell, aspect, mx, my, 3);
        if (i == hotbar_sel) {
            float t = 0.003f;
            ui_draw_rect(ui, sx - t, hy - t, cell + 2.0f * t, t, 1, 1, 1, 0.5f, aspect);
            ui_draw_rect(ui, sx - t, hy + cell, cell + 2.0f * t, t, 1, 1, 1, 0.5f, aspect);
            ui_draw_rect(ui, sx - t, hy - t, t, cell + 2.0f * t, 1, 1, 1, 0.5f, aspect);
            ui_draw_rect(ui, sx + cell, hy - t, t, cell + 2.0f * t, 1, 1, 1, 0.5f, aspect);
        }
    }
    recompute_armor();
    sync_selected();
}

static void draw_craft_area(int dim, int is_forge, float top, float aspect, float mx, float my) {
    float cx = aspect * 0.5f;
    float cell = 0.066f, gap = 0.010f;
    float grid_w = (float)dim * cell + (float)(dim - 1) * gap;
    float gx0 = cx - grid_w - 0.04f;
    for (int i = 0; i < dim * dim; i++) {
        int r = i / dim, c = i % dim;
        slot_widget(&craft_grid[i], gx0 + (float)c * (cell + gap), top + (float)r * (cell + gap),
                    cell, aspect, mx, my, 3);
    }
    float ry = top + grid_w * 0.5f - cell * 0.5f;
    float rx = gx0 + grid_w + 0.07f;
    text_draw(text, "\x1a", gx0 + grid_w + 0.018f, ry + cell * 0.28f, 0.04f, 1, 1, 1, 0.9f, aspect);
    const recipe_t *r = craft_match(craft_grid, dim, dim, is_forge);
    rect_t rr = { rx, ry, cell, cell };
    int hov = rect_hit(rr, mx, my);
    ui_draw_rect(ui, rx, ry, cell, cell, hov ? 0.34f : 0.18f, hov ? 0.30f : 0.16f, hov ? 0.18f : 0.14f, 0.95f, aspect);
    if (r) {
        inv_slot_t preview = make_slot(r->out_id, r->out_count);
        draw_slot_contents(preview, rx, ry, cell, aspect);
        if (ui_click && rect_hit(rr, ui_click_x, ui_click_y)) {
            int can = slot_empty(ui_cursor) ||
                      (ui_cursor.id == r->out_id && ui_cursor.count + r->out_count <= item_max_stack(r->out_id));
            if (can) {
                if (slot_empty(ui_cursor)) ui_cursor = make_slot(r->out_id, r->out_count);
                else ui_cursor.count = (int16_t)(ui_cursor.count + r->out_count);
                craft_consume_one(craft_grid, dim, dim, r);
                audio_play_ui_click(audio);
                craft_unlock_achievement(r->out_id);
            }
        }
    }
}

static void close_station(void) {
    for (int i = 0; i < 9; i++) return_slot_to_inventory(&craft_grid[i]);
    for (int i = 0; i < 2; i++) return_slot_to_inventory(&anvil_in[i]);
    return_slot_to_inventory(&ui_cursor);
    open_station = NULL;
    game_state = GS_PLAYING;
    glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    mouse_initialized = 0;
    recompute_armor();
    sync_selected();
}

static int open_station_for_block(block_t b, int x, int y, int z) {
    inv_slot_t e = {0, 0, 0};
    switch (b) {
        case BLOCK_CRAFTING_TABLE:
            for (int i = 0; i < 9; i++) craft_grid[i] = e;
            craft_dim = 3; game_state = GS_CRAFTING; break;
        case BLOCK_FORGE:
            for (int i = 0; i < 9; i++) craft_grid[i] = e;
            anvil_in[0] = e; anvil_in[1] = e;
            craft_dim = 3; game_state = GS_FORGE; break;
        case BLOCK_ANVIL:
            anvil_in[0] = e; anvil_in[1] = e; game_state = GS_ANVIL; break;
        case BLOCK_FURNACE:
            open_station = craft_station_get(x, y, z);
            if (!open_station || open_station->type != ST_FURNACE) open_station = craft_station_create(x, y, z, ST_FURNACE);
            game_state = GS_FURNACE; break;
        case BLOCK_CHEST:
            open_station = craft_station_get(x, y, z);
            if (!open_station || open_station->type != ST_CHEST) open_station = craft_station_create(x, y, z, ST_CHEST);
            game_state = GS_CHEST; break;
        case BLOCK_BREWING_STAND:
            open_station = craft_station_get(x, y, z);
            if (!open_station || open_station->type != ST_BREWING) open_station = craft_station_create(x, y, z, ST_BREWING);
            game_state = GS_BREWING; break;
        default: return 0;
    }
    glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    mouse_initialized = 0;
    return 1;
}

static void draw_crafting_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0, 0, aspect, 1.0f, 0, 0, 0, 0.55f, aspect);
    draw_centered_text("CRAFTING", 0.07f, 0.045f, aspect, 1, 1, 1, 1);
    draw_craft_area(craft_dim, 0, 0.15f, aspect, mx, my);
    draw_inv_panel(aspect, mx, my);
    draw_cursor_stack(aspect, mx, my);
}

static void draw_forge_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0, 0, aspect, 1.0f, 0, 0, 0, 0.55f, aspect);
    draw_centered_text("FORGE", 0.06f, 0.045f, aspect, 1, 1, 1, 1);
    draw_craft_area(3, 1, 0.12f, aspect, mx, my);
    float cx = aspect * 0.5f, cell = 0.062f;
    draw_centered_text("repair: 1 material = +25% (no XP)", 0.385f, 0.020f, aspect, 0.7f, 0.75f, 0.85f, 1.0f);
    slot_widget(&anvil_in[0], cx - 0.14f, 0.41f, cell, aspect, mx, my, 3);
    slot_widget(&anvil_in[1], cx - 0.04f, 0.41f, cell, aspect, mx, my, 3);
    inv_slot_t tool = anvil_in[0];
    int can = forge_repair(&tool, anvil_in[1]);
    float rx = cx + 0.10f, ry = 0.41f;
    rect_t rr = { rx, ry, cell, cell };
    int hov = rect_hit(rr, mx, my);
    ui_draw_rect(ui, rx, ry, cell, cell, hov ? 0.34f : 0.18f, 0.16f, 0.14f, 0.95f, aspect);
    if (can) {
        draw_slot_contents(tool, rx, ry, cell, aspect);
        if (ui_click && rect_hit(rr, ui_click_x, ui_click_y) && slot_empty(ui_cursor)) {
            ui_cursor = tool; ui_cursor.count = 1;
            anvil_in[0].count--; if (anvil_in[0].count <= 0) { inv_slot_t e = {0,0,0}; anvil_in[0] = e; }
            anvil_in[1].count--; if (anvil_in[1].count <= 0) { inv_slot_t e = {0,0,0}; anvil_in[1] = e; }
            audio_play_anvil(audio);
        }
    }
    draw_inv_panel(aspect, mx, my);
    draw_cursor_stack(aspect, mx, my);
}

static void draw_anvil_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0, 0, aspect, 1.0f, 0, 0, 0, 0.55f, aspect);
    draw_centered_text("ANVIL", 0.06f, 0.045f, aspect, 1, 1, 1, 1);
    float cx = aspect * 0.5f, cell = 0.075f;
    slot_widget(&anvil_in[0], cx - 0.24f, 0.20f, cell, aspect, mx, my, 3);
    slot_widget(&anvil_in[1], cx - 0.11f, 0.20f, cell, aspect, mx, my, 3);
    inv_slot_t out; int cost = 0;
    int ok = anvil_combine(anvil_in[0], anvil_in[1], &out, &cost);
    float rx = cx + 0.12f, ry = 0.20f;
    rect_t rr = { rx, ry, cell, cell };
    int hov = rect_hit(rr, mx, my);
    ui_draw_rect(ui, rx, ry, cell, cell, hov ? 0.34f : 0.18f, hov ? 0.30f : 0.16f, hov ? 0.18f : 0.14f, 0.95f, aspect);
    if (ok) {
        draw_slot_contents(out, rx, ry, cell, aspect);
        char cs[32]; snprintf(cs, sizeof cs, "Cost: %d lvl", cost);
        int afford = player.xp_level >= cost;
        text_draw(text, cs, rx - 0.02f, ry + cell + 0.012f, 0.022f,
                  afford ? 0.5f : 1.0f, afford ? 1.0f : 0.4f, 0.4f, 1.0f, aspect);
        if (ui_click && rect_hit(rr, ui_click_x, ui_click_y) && slot_empty(ui_cursor) && afford) {
            ui_cursor = out;
            anvil_in[0].count--; if (anvil_in[0].count <= 0) { inv_slot_t e = {0,0,0}; anvil_in[0] = e; }
            anvil_in[1].count--; if (anvil_in[1].count <= 0) { inv_slot_t e = {0,0,0}; anvil_in[1] = e; }
            player_remove_levels(&player, cost);
            audio_play_anvil(audio);
        }
    }
    draw_inv_panel(aspect, mx, my);
    draw_cursor_stack(aspect, mx, my);
}

static void draw_furnace_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0, 0, aspect, 1.0f, 0, 0, 0, 0.55f, aspect);
    draw_centered_text("FURNACE", 0.06f, 0.045f, aspect, 1, 1, 1, 1);
    if (open_station) {
        furnace_state_t *fs = &open_station->u.furnace;
        float cx = aspect * 0.5f, cell = 0.075f;
        float ix = cx - 0.20f;
        slot_widget(&fs->slot[0], ix, 0.15f, cell, aspect, mx, my, 3);
        slot_widget(&fs->slot[1], ix, 0.32f, cell, aspect, mx, my, 3);
        float lit = fs->burn_max > 0.0f ? fs->burn_left / fs->burn_max : 0.0f;
        ui_draw_rect(ui, ix, 0.285f, cell, 0.012f, 0.15f, 0.15f, 0.15f, 0.8f, aspect);
        ui_draw_rect(ui, ix, 0.285f, cell * lit, 0.012f, 1.0f, 0.55f, 0.12f, 0.95f, aspect);
        float smelt_t = 9.0f / world_smelt_mult(world);
        float cook = smelt_t > 0.0f ? fs->cook_progress / smelt_t : 0.0f;
        if (cook > 1.0f) cook = 1.0f;
        ui_draw_rect(ui, ix + cell + 0.02f, 0.205f, 0.10f, 0.012f, 0.15f, 0.15f, 0.15f, 0.8f, aspect);
        ui_draw_rect(ui, ix + cell + 0.02f, 0.205f, 0.10f * cook, 0.012f, 0.95f, 0.9f, 0.4f, 0.95f, aspect);
        float ox = cx + 0.13f, oy = 0.20f;
        rect_t orr = { ox, oy, cell, cell };
        int hov = rect_hit(orr, mx, my);
        ui_draw_rect(ui, ox, oy, cell, cell, hov ? 0.30f : 0.16f, hov ? 0.32f : 0.16f, hov ? 0.40f : 0.20f, 0.95f, aspect);
        draw_slot_contents(fs->slot[2], ox, oy, cell, aspect);
        if (ui_click && rect_hit(orr, ui_click_x, ui_click_y) && !slot_empty(fs->slot[2])) {
            int taken = 0;
            if (slot_empty(ui_cursor)) { ui_cursor = fs->slot[2]; inv_slot_t e = {0,0,0}; fs->slot[2] = e; taken = 1; }
            else if (ui_cursor.id == fs->slot[2].id &&
                     ui_cursor.count + fs->slot[2].count <= item_max_stack(ui_cursor.id)) {
                ui_cursor.count = (int16_t)(ui_cursor.count + fs->slot[2].count);
                inv_slot_t e = {0,0,0}; fs->slot[2] = e; taken = 1;
            }
            if (taken) {
                int xp = (int)fs->xp_bank;
                if (xp > 0) { player_add_xp(&player, xp); fs->xp_bank -= (float)xp; }
                if (ui_cursor.id == ITEM_IRON_INGOT) ach(ACH_SMELT_IRON);
                audio_play_ui_click(audio);
            }
        }
    }
    draw_inv_panel(aspect, mx, my);
    draw_cursor_stack(aspect, mx, my);
}

static void draw_chest_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0, 0, aspect, 1.0f, 0, 0, 0, 0.55f, aspect);
    draw_centered_text("CHEST", 0.06f, 0.045f, aspect, 1, 1, 1, 1);
    if (open_station) {
        float cx = aspect * 0.5f, cell = 0.062f, gap = 0.010f;
        int cols = 9;
        float gw = (float)cols * cell + (float)(cols - 1) * gap;
        float gx0 = cx - gw * 0.5f, gy0 = 0.13f;
        for (int i = 0; i < 27; i++) {
            int r = i / cols, c = i % cols;
            slot_widget(&open_station->u.chest[i], gx0 + (float)c * (cell + gap),
                        gy0 + (float)r * (cell + gap), cell, aspect, mx, my, 3);
        }
    }
    draw_inv_panel(aspect, mx, my);
    draw_cursor_stack(aspect, mx, my);
}

static void draw_brewing_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0, 0, aspect, 1.0f, 0, 0, 0, 0.55f, aspect);
    draw_centered_text("BREWING STAND", 0.06f, 0.045f, aspect, 1, 1, 1, 1);
    if (open_station && open_station->type == ST_BREWING) {
        brewing_state_t *bs = &open_station->u.brewing;
        float cx = aspect * 0.5f, cell = 0.072f;
        slot_widget(&bs->slot[0], cx - cell * 0.5f, 0.14f, cell, aspect, mx, my, 3);
        slot_widget(&bs->slot[1], cx - 0.24f,       0.30f, cell, aspect, mx, my, 3);
        slot_widget(&bs->slot[2], cx - cell * 1.7f, 0.40f, cell, aspect, mx, my, 3);
        slot_widget(&bs->slot[3], cx - cell * 0.5f, 0.44f, cell, aspect, mx, my, 3);
        slot_widget(&bs->slot[4], cx + cell * 0.7f, 0.40f, cell, aspect, mx, my, 3);
        float bprog = bs->time_max > 0.0f ? 1.0f - bs->time_left / bs->time_max : 0.0f;
        ui_draw_rect(ui, cx - cell * 0.5f, 0.265f, cell, 0.012f, 0.15f, 0.15f, 0.15f, 0.8f, aspect);
        ui_draw_rect(ui, cx - cell * 0.5f, 0.265f, cell * bprog, 0.012f, 0.72f, 0.42f, 0.92f, 0.95f, aspect);
        char fl[24];
        snprintf(fl, sizeof fl, "fuel %d", bs->fuel);
        text_draw(text, fl, cx - 0.24f, 0.265f, 0.018f, 0.85f, 0.8f, 0.95f, 1.0f, aspect);
    }
    draw_inv_panel(aspect, mx, my);
    draw_cursor_stack(aspect, mx, my);
}

static void draw_death_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0.0f, 0.0f, aspect, 1.0f, 0.45f, 0.0f, 0.0f, 0.45f, aspect);
    draw_centered_text("YOU DIED", 0.28f, 0.10f, aspect, 1.0f, 0.82f, 0.82f, 1.0f);
    char score[32];
    snprintf(score, sizeof score, "Score: %d", player.xp_level);
    draw_centered_text(score, 0.42f, 0.032f, aspect, 0.9f, 0.9f, 0.9f, 1.0f);

    float cx = aspect * 0.5f;
    rect_t brespawn = { cx - 0.18f, 0.54f, 0.36f, 0.08f };
    rect_t btitle   = { cx - 0.18f, 0.64f, 0.36f, 0.08f };
    if (do_button(brespawn, "Respawn", aspect, mx, my)) {
        player_respawn(&player, world);
        game_state = GS_PLAYING;
        glfwSetInputMode(g_win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        mouse_initialized = 0;
        prev_underwater = -1;
        fall_peak_vel = 0.0f; fall_was_grounded = 1;
    }
    if (do_button(btitle, "Title", aspect, mx, my)) quit_to_title(1);
}

static void draw_inventory_screen(float aspect, float mx, float my) {
    render_world_scene(aspect);
    ui_draw_rect(ui, 0.0f, 0.0f, aspect, 1.0f, 0.0f, 0.0f, 0.0f, 0.55f, aspect);
    int survival = (world_gamemode(world) == GAMEMODE_SURVIVAL);
    draw_centered_text(survival ? "INVENTORY" : "BLOCKS", 0.06f, 0.045f, aspect, 1.0f, 1.0f, 1.0f, 1.0f);
    float cx = aspect * 0.5f;

    if (survival) {
        draw_craft_area(2, 0, 0.16f, aspect, mx, my);
        draw_inv_panel(aspect, mx, my);
        draw_cursor_stack(aspect, mx, my);
    } else {
        float cell = 0.075f, gap = 0.012f;
        int cols = 8;
        int total_rows = (PALETTE_COUNT + cols - 1) / cols;
        int vis_rows = 6;
        if (vis_rows > total_rows) vis_rows = total_rows;
        float grid_w = (float)cols * cell + (float)(cols - 1) * gap;
        float gx0 = cx - grid_w * 0.5f, gy0 = 0.16f;
        float panel_h = (float)vis_rows * (cell + gap);

        int max_row = total_rows - vis_rows; if (max_row < 0) max_row = 0;
        if (palette_scroll_row < 0) palette_scroll_row = 0;
        if (palette_scroll_row > max_row) palette_scroll_row = max_row;

        ui_draw_rect(ui, gx0 - 0.012f, gy0 - 0.012f, grid_w + 0.024f, panel_h + 0.012f,
                     0.08f, 0.09f, 0.12f, 0.6f, aspect);

        int hovered = -1;
        for (int vr = 0; vr < vis_rows; vr++) {
            int r = palette_scroll_row + vr;
            for (int c = 0; c < cols; c++) {
                int i = r * cols + c;
                if (i >= PALETTE_COUNT) break;
                float bx = gx0 + (float)c * (cell + gap);
                float by = gy0 + (float)vr * (cell + gap);
                rect_t cr = { bx, by, cell, cell };
                int hov = rect_hit(cr, mx, my);
                ui_draw_rect(ui, bx, by, cell, cell,
                             hov ? 0.30f : 0.16f, hov ? 0.32f : 0.16f, hov ? 0.40f : 0.20f, 0.95f, aspect);
                float inset = cell * 0.10f;
                draw_item_icon(PALETTE[i], bx + inset, by + inset, cell - 2.0f * inset, aspect);
                if (hov) hovered = i;
                if (ui_click && rect_hit(cr, ui_click_x, ui_click_y)) {
                    audio_play_ui_click(audio);
                    hotbar[hotbar_sel] = make_slot(PALETTE[i], item_max_stack(PALETTE[i]));
                    sync_selected();
                }
            }
        }
        if (total_rows > vis_rows) {
            float sb_x = gx0 + grid_w + 0.016f;
            ui_draw_rect(ui, sb_x, gy0, 0.010f, panel_h, 0.15f, 0.15f, 0.18f, 0.7f, aspect);
            float thumb_h = panel_h * (float)vis_rows / (float)total_rows;
            float thumb_y = gy0 + (panel_h - thumb_h) * (float)palette_scroll_row / (float)max_row;
            ui_draw_rect(ui, sb_x, thumb_y, 0.010f, thumb_h, 0.5f, 0.55f, 0.65f, 0.9f, aspect);
        }
        if (hovered >= 0)
            draw_centered_text(block_name(PALETTE[hovered]),
                               gy0 + panel_h + 0.018f, 0.026f, aspect, 0.8f, 0.85f, 0.95f, 1.0f);
        draw_centered_text("scroll for more", gy0 + panel_h + 0.052f, 0.018f, aspect, 0.5f, 0.55f, 0.65f, 1.0f);

        float slot = 0.07f, pad = 0.01f;
        float total_w = (float)HOTBAR_COUNT * slot + (float)(HOTBAR_COUNT + 1) * pad;
        float hx0 = cx - total_w * 0.5f, hy = 0.80f;
        for (int i = 0; i < HOTBAR_COUNT; i++) {
            float sx = hx0 + pad + (float)i * (slot + pad);
            rect_t sr = { sx, hy, slot, slot };
            int sel = (i == hotbar_sel);
            ui_draw_rect(ui, sx, hy, slot, slot,
                         sel ? 0.30f : 0.15f, sel ? 0.32f : 0.15f, sel ? 0.40f : 0.18f, 0.9f, aspect);
            draw_slot_contents(hotbar[i], sx, hy, slot, aspect);
            char num[2] = { (char)(i < 9 ? '1' + i : '0'), 0 };
            text_draw(text, num, sx + 0.005f, hy + 0.004f, 0.022f, 1.0f, 1.0f, 1.0f, 0.85f, aspect);
            if (ui_click && rect_hit(sr, ui_click_x, ui_click_y)) hotbar_select(i);
            if (ui_rclick && rect_hit(sr, ui_rclick_x, ui_rclick_y)) {
                inv_slot_t e = {0,0,0}; hotbar[i] = e; sync_selected();
            }
        }
    }

    const char *hint = survival
        ? "drag items  |  left-click pick up/place, right-click split  |  craft in the 2x2 grid"
        : "left-click a block to fill the selected slot   right-click a slot to clear";
    draw_centered_text(hint, 0.95f, 0.018f, aspect, 0.6f, 0.65f, 0.75f, 1.0f);
    rect_t bdone = { cx - 0.12f, 0.92f, 0.24f, 0.05f };
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

    GLFWwindow *win = glfwCreateWindow(1280, 720, "BlockWorlds " GAME_VERSION, NULL, NULL);
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
    item_atlas_tex = texture_create_item_atlas();
    hud_atlas_tex  = texture_create_hud_atlas();
    ui_set_block_atlas(ui, atlas_tex);

    text = text_create();
    if (!text) { glfwTerminate(); return EXIT_FAILURE; }

    console = console_create(text, ui);
    if (!console) { glfwTerminate(); return EXIT_FAILURE; }
    console_set_commands(console, CMD_NAMES, CMD_USAGE, CMD_COUNT);

    falling = falling_create();
    particles = particle_create();
    if (particles) particle_set_atlas(particles, hud_atlas_tex);
    craft_stations_init();
    world_section_io_t st_io = { craft_stations_save, craft_stations_load };
    world_set_station_io(st_io);
    entities = entity_create();
    world_section_io_t ent_io = { entity_save, entity_load };
    world_set_entity_io(ent_io);
    world_section_io_t pl_io = { mp_players_save, mp_players_load };
    world_set_players_io(pl_io);
    net_global_init();
    toasts = toast_create();
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
    if (entities) entity_set_fx(entities, particles, audio);

    settings_apply();
    if (settings.fullscreen) apply_fullscreen(1);

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
    depth_atlas_loc = glGetUniformLocation(depth_prog, "u_atlas");

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
    water_depth_loc        = glGetUniformLocation(water_prog, "u_scene_depth");
    water_screen_loc       = glGetUniformLocation(water_prog, "u_screen");
    water_near_loc         = glGetUniformLocation(water_prog, "u_near");
    water_far_loc          = glGetUniformLocation(water_prog, "u_far");

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

        if (net) net_poll(net, mp_on_msg, NULL);

        int width, height;
        glfwGetFramebufferSize(win, &width, &height);
        float aspect = (height > 0) ? (float)width / (float)height : 1.0f;

        float mx, my;
        cursor_ui_pos(win, &mx, &my);

        if (game_state == GS_TITLE) {
            draw_title_screen(aspect, mx, my);
        } else if (game_state == GS_LOAD) {
            draw_load_world_screen(aspect, mx, my);
        } else if (game_state == GS_MP_JOIN) {
            draw_mp_join_screen(aspect, mx, my);
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
            int survival = (world_gamemode(world) == GAMEMODE_SURVIVAL);
            player_set_input_enabled(!console_is_open(console));
            player_update(&player, win, world, (double)dt);
            world_update_streaming(world, player.pos.x, player.pos.z);

            if (mp_active && net) {
                mp_state_timer += dt;
                if (mp_state_timer > 0.08) {
                    mp_state_timer = 0.0;
                    net_msg_t s; memset(&s, 0, sizeof s);
                    s.op = NET_STATE; s.player_id = mp_my_id;
                    s.x = player.pos.x; s.y = player.pos.y; s.z = player.pos.z;
                    s.yaw = player.yaw; s.pitch = player.pitch;
                    net_send(net, -1, &s);
                }
                if (mp_is_host) {
                    mp_time_timer += dt;
                    if (mp_time_timer > 2.0) {
                        mp_time_timer = 0.0;
                        net_msg_t t; memset(&t, 0, sizeof t);
                        t.op = NET_TIME; t.world_time = world_time;
                        net_send(net, -1, &t);
                    }
                }
                for (int i = 0; i < NET_MAX_PEERS; i++) {
                    remote_player_t *r = &remote_players[i];
                    if (!r->active) continue;
                    if (r->move_timer > 0.0f) { r->anim += 9.0f * dt; r->move_timer -= dt; }
                }
            }

            if (falling_update(falling, world, dt) > 0) audio_play_block_land(audio);
            particle_update(particles, world, dt);
            craft_stations_tick(world, dt, world_smelt_mult(world));
            toast_update(toasts, dt);
            {
                player_ctx_t pc = make_player_ctx();
                entity_update(entities, world, &pc, dt);
                static float spawn_acc = 0.0f;
                spawn_acc += dt;
                if (spawn_acc >= 1.0f) {
                    spawn_acc = 0.0f;
                    int is_night = world_sun_dir_y(world) < 0.0f;
                    entity_spawn_pass(entities, world, player.pos, is_night,
                                      world_spawn_rate(world), world_difficulty(world),
                                      world_mob_spawning(world));
                }
            }
            world_tick_water(world, 8);

            static float step_accum   = 0.0f;
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
                        if (gspeed > 5.5f)
                            particle_footstep(particles, player.pos.x, player.pos.y, player.pos.z, ground);
                    }
                    if (survival) player_add_exhaustion(&player, gspeed > 5.5f ? 0.2f : 0.08f);
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
                if (!fall_was_grounded && !player.in_water) {
                    audio_play_land(audio, -fall_peak_vel);
                    if (-fall_peak_vel > 6.0f) {
                        block_t lg = world_get_block(world, (int)floorf(player.pos.x),
                            (int)floorf(player.pos.y - 0.1f), (int)floorf(player.pos.z));
                        particle_land_dust(particles, player.pos.x, player.pos.y, player.pos.z, lg, -fall_peak_vel);
                    }
                    if (survival && world_fall_damage(world)) {
                        float fdist = (fall_peak_vel * fall_peak_vel) / (2.0f * 32.0f);
                        float fdmg  = fdist - 3.0f;
                        if (fdmg > 0.0f) player_take_damage(&player, fdmg, DMG_FALL);
                    }
                }
                fall_peak_vel = 0.0f;
            } else if (player.vel.y < fall_peak_vel) {
                fall_peak_vel = player.vel.y;
            }
            fall_was_grounded = player.grounded;

            vec3 eye = player_eye_pos(&player);
            block_t eye_block = world_get_block(world, (int)floorf(eye.x), (int)floorf(eye.y), (int)floorf(eye.z));
            int underwater = block_is_water(eye_block);
            if (prev_underwater == 0 && underwater) {
                audio_play_splash(audio); water_enter_time = now;
                particle_water_splash(particles, player.pos.x, (float)floorf(player.pos.y) + 0.9f, player.pos.z);
            }
            if (prev_underwater == 1 && !underwater) audio_play_exit_water(audio);
            prev_underwater = underwater;

            if (survival) {
                item_id selid = (item_id)player.selected_block;
                int can_consume = item_is_food(selid) &&
                                  (player.hunger < PLAYER_MAX_HUNGER || item_is_potion(selid));
                if (g_rmb_down && !player.is_dead && can_consume) {
                    if (!player.eating) {
                        player_begin_eat(&player, selid);
                        player.eat_meta = item_is_potion(selid) ? (uint8_t)hotbar[hotbar_sel].durability : 0;
                        audio_play_eat(audio);
                    }
                    if (player.eating) {
                        static float eat_pfx = 0.0f;
                        eat_pfx += dt;
                        if (eat_pfx > 0.12f) {
                            eat_pfx = 0.0f;
                            vec3 ep = player_eye_pos(&player), ef = player_forward(&player);
                            particle_eat(particles, ep.x + ef.x * 0.4f, ep.y - 0.25f, ep.z + ef.z * 0.4f, selid);
                        }
                    }
                    if (player_eat_tick(&player, dt)) {
                        int was_potion = item_is_drink_potion(selid);
                        inventory_consume_selected();
                        if (was_potion) inventory_add(ITEM_GLASS_BOTTLE, 1, 0);
                        audio_play_burp(audio);
                    }
                } else if (player.eating) {
                    player_cancel_eat(&player);
                }
                int prev_level = player.xp_level;
                player_survival_update(&player, world, dt);
                if (player.xp_level > prev_level) { audio_play_levelup(audio); ach(ACH_LEVEL_UP); }
                if (player.is_dead) {
                    audio_play_hurt(audio);
                    if (!world_keep_inventory(world)) clear_inventory();
                    game_state = GS_DEATH;
                    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    mouse_initialized = 0;
                }
            }

            if (world_daylight_cycle(world)) {
                world_time += dt / DAY_LENGTH_SEC;
                if (world_time >= 1.0f) world_time -= 1.0f;
            }

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
            if (survival) draw_survival_hud(aspect);
            if (fps_shown) draw_debug_overlay(aspect);
            toast_render(toasts, ui, text, hud_atlas_tex, aspect);
            if (console_is_open(console)) {
                console_render(console, aspect);
            } else {
                ui_draw_crosshair(ui, aspect);
                console_render_feed(console, aspect);
            }
        } else if (game_state == GS_DEATH) {
            draw_death_screen(aspect, mx, my);
        } else if (game_state == GS_CRAFTING) {
            draw_crafting_screen(aspect, mx, my);
        } else if (game_state == GS_FURNACE) {
            draw_furnace_screen(aspect, mx, my);
        } else if (game_state == GS_FORGE) {
            draw_forge_screen(aspect, mx, my);
        } else if (game_state == GS_ANVIL) {
            draw_anvil_screen(aspect, mx, my);
        } else if (game_state == GS_CHEST) {
            draw_chest_screen(aspect, mx, my);
        } else if (game_state == GS_BREWING) {
            draw_brewing_screen(aspect, mx, my);
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
            rect_t resume   = { cx - 0.23f, 0.295f, 0.46f, 0.075f };
            rect_t blan     = { cx - 0.23f, 0.378f, 0.46f, 0.075f };
            rect_t bfull    = { cx - 0.23f, 0.461f, 0.46f, 0.075f };
            rect_t bset     = { cx - 0.23f, 0.544f, 0.46f, 0.075f };
            rect_t savequit = { cx - 0.23f, 0.627f, 0.46f, 0.075f };
            rect_t quitns   = { cx - 0.23f, 0.710f, 0.46f, 0.075f };
            if (do_button(resume, "Resume", aspect, mx, my)) {
                game_state = GS_PLAYING;
                glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                mouse_initialized = 0;
            }
            if (mp_is_host) {
                char lbl[40];
                snprintf(lbl, sizeof lbl, "Hosting on :%d", settings.mp_port);
                ui_draw_rect(ui, blan.x, blan.y, blan.w, blan.h, 0.12f, 0.20f, 0.14f, 0.95f, aspect);
                float tw = text_width(lbl, 0.030f);
                text_draw(text, lbl, blan.x + (blan.w - tw) * 0.5f, blan.y + 0.026f, 0.030f,
                          0.6f, 1.0f, 0.7f, 1.0f, aspect);
            } else if (mp_active) {
                ui_draw_rect(ui, blan.x, blan.y, blan.w, blan.h, 0.12f, 0.14f, 0.20f, 0.95f, aspect);
                float tw = text_width("Connected", 0.030f);
                text_draw(text, "Connected", blan.x + (blan.w - tw) * 0.5f, blan.y + 0.026f, 0.030f,
                          0.7f, 0.8f, 1.0f, 1.0f, aspect);
            } else if (do_button(blan, "Open to LAN", aspect, mx, my)) {
                if (mp_open_to_lan()) {
                    game_state = GS_PLAYING;
                    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    mouse_initialized = 0;
                }
            }
            if (do_button(bfull, settings.fullscreen ? "Fullscreen: On" : "Fullscreen: Off", aspect, mx, my)) {
                settings.fullscreen = !settings.fullscreen;
                apply_fullscreen(settings.fullscreen);
                settings_save();
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
            g_fps = (float)((double)fps_frame_count / fps_window_dt);
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
    entity_destroy(entities);
    toast_destroy(toasts);
    particle_destroy(particles);
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
