#include "player.h"
#include "world.h"
#include "chunk.h"
#include "registry.h"
#include "survival.h"
#include <math.h>
#include <stdlib.h>

#define HALF_WIDTH    0.3f
#define HEIGHT_STAND  1.8f
#define HEIGHT_CROUCH 1.5f
#define EYE_STAND     1.6f
#define EYE_CROUCH    1.3f

#define WALK_SPEED      4.5f
#define SPRINT_MULT     1.43f
#define CROUCH_SPEED    1.5f
#define FLY_SPEED      11.0f
#define FLY_DOWN_SPEED  5.5f

#define STEP_UP_HEIGHT       1.0f
#define STEP_JUMP_VEL        6.5f
#define STEP_JUMP_VEL_WATER  4.5f
#define STEP_GRAVITY        16.0f
#define STEP_JUMP_TIME       0.45f

#define GRAVITY         32.0f
#define JUMP_VEL         8.4f
#define TERMINAL_VEL    78.4f

#define WATER_SPEED_MULT     0.6f
#define WATER_GRAVITY_MULT   0.4f
#define WATER_TERMINAL_MULT  0.2f
#define WATER_SWIM_UP_VEL    3.6f

#define DOUBLE_TAP_WIN  0.3
#define MOUSE_SENS      0.0025f
#define PITCH_LIMIT     1.553343f

#define ICE_ACCEL       5.0f
#define ICE_DECEL       1.6f

#define EPSILON 1e-4f

static float g_mouse_sens = MOUSE_SENS;
static int   g_allow_fly  = 1;
static int   g_input_enabled = 1;

void player_set_sensitivity(float s) { g_mouse_sens = s; }
void player_set_allow_fly(int allow) { g_allow_fly = allow; }
void player_set_input_enabled(int e) { g_input_enabled = e; }

static int block_is_solid(block_t b) {
    return !block_passable(b);
}

static float player_height(const player_t *p) {
    return p->crouching ? HEIGHT_CROUCH : HEIGHT_STAND;
}

static float player_eye_height(const player_t *p) {
    return p->crouching ? EYE_CROUCH : EYE_STAND;
}

static int aabb_intersects(vec3 pos, float h, const world_t *world) {
    int min_x = (int)floorf(pos.x - HALF_WIDTH);
    int max_x = (int)ceilf (pos.x + HALF_WIDTH) - 1;
    int min_y = (int)floorf(pos.y);
    int max_y = (int)ceilf (pos.y + h) - 1;
    int min_z = (int)floorf(pos.z - HALF_WIDTH);
    int max_z = (int)ceilf (pos.z + HALF_WIDTH) - 1;

    for (int y = min_y; y <= max_y; y++)
        for (int z = min_z; z <= max_z; z++)
            for (int x = min_x; x <= max_x; x++)
                if (block_is_solid(world_get_block(world, x, y, z))) return 1;
    return 0;
}

static int player_in_water(const player_t *p, float h, const world_t *world) {
    int min_x = (int)floorf(p->pos.x - HALF_WIDTH);
    int max_x = (int)ceilf (p->pos.x + HALF_WIDTH) - 1;
    int min_y = (int)floorf(p->pos.y);
    int max_y = (int)ceilf (p->pos.y + h) - 1;
    int min_z = (int)floorf(p->pos.z - HALF_WIDTH);
    int max_z = (int)ceilf (p->pos.z + HALF_WIDTH) - 1;
    for (int y = min_y; y <= max_y; y++)
        for (int z = min_z; z <= max_z; z++)
            for (int x = min_x; x <= max_x; x++)
                if (block_is_water(world_get_block(world, x, y, z))) return 1;
    return 0;
}

static int feet_supported(const player_t *p, const world_t *world) {
    int min_x = (int)floorf(p->pos.x - HALF_WIDTH);
    int max_x = (int)ceilf (p->pos.x + HALF_WIDTH) - 1;
    int min_z = (int)floorf(p->pos.z - HALF_WIDTH);
    int max_z = (int)ceilf (p->pos.z + HALF_WIDTH) - 1;
    int y = (int)floorf(p->pos.y - 0.05f);

    for (int z = min_z; z <= max_z; z++)
        for (int x = min_x; x <= max_x; x++)
            if (block_is_solid(world_get_block(world, x, y, z))) return 1;
    return 0;
}

static block_t ground_block_under(const player_t *p, const world_t *world) {
    int min_x = (int)floorf(p->pos.x - HALF_WIDTH);
    int max_x = (int)ceilf (p->pos.x + HALF_WIDTH) - 1;
    int min_z = (int)floorf(p->pos.z - HALF_WIDTH);
    int max_z = (int)ceilf (p->pos.z + HALF_WIDTH) - 1;
    int y = (int)floorf(p->pos.y - 0.05f);

    for (int z = min_z; z <= max_z; z++)
        for (int x = min_x; x <= max_x; x++) {
            block_t b = world_get_block(world, x, y, z);
            if (block_is_solid(b)) return b;
        }
    return BLOCK_AIR;
}

void player_init(player_t *p, vec3 spawn) {
    p->pos = spawn;
    p->vel = (vec3){0.0f, 0.0f, 0.0f};
    p->yaw = 0.0f;
    p->pitch = 0.0f;
    p->mode = PLAYER_WALKING;
    p->grounded = 0;
    p->crouching = 0;
    p->last_space_press = -1.0;
    p->selected_block = BLOCK_AIR;
    p->step_up_timer = 0.0f;

    p->health = PLAYER_MAX_HEALTH;  p->max_health = PLAYER_MAX_HEALTH;
    p->hunger = PLAYER_MAX_HUNGER;  p->saturation = 5.0f; p->exhaustion = 0.0f;
    p->air = PLAYER_MAX_AIR;        p->armor_points = 0;
    p->is_dead = 0;
    p->invuln_timer = 0.0f; p->damage_flash_timer = 0.0f; p->last_damage_amount = 0.0f; p->death_anim_timer = 0.0f;
    p->xp_level = 0; p->xp_total = 0; p->xp_progress = 0.0f;
    status_clear(p);
    p->has_respawn = 0; p->respawn_x = 0.0f; p->respawn_y = 0.0f; p->respawn_z = 0.0f;
    p->tick_accum = 0.0f;
    p->eating = 0; p->eat_item = 0; p->eat_timer = 0.0f;
}

void player_add_look(player_t *p, float dx, float dy) {
    p->yaw   += dx * g_mouse_sens;
    p->pitch -= dy * g_mouse_sens;
    if (p->pitch >  PITCH_LIMIT) p->pitch =  PITCH_LIMIT;
    if (p->pitch < -PITCH_LIMIT) p->pitch = -PITCH_LIMIT;
}

int player_on_space_press(player_t *p) {
    double now = glfwGetTime();
    int double_tapped = (now - p->last_space_press) < DOUBLE_TAP_WIN;
    p->last_space_press = now;

    if (double_tapped && g_allow_fly) {
        p->mode = (p->mode == PLAYER_FLYING) ? PLAYER_WALKING : PLAYER_FLYING;
        p->vel.y = 0.0f;
        return 0;
    }

    if (p->mode == PLAYER_WALKING && p->grounded) {
        p->vel.y = JUMP_VEL;
        p->grounded = 0;
        return 1;
    }
    return 0;
}

vec3 player_eye_pos(const player_t *p) {
    return (vec3){ p->pos.x, p->pos.y + player_eye_height(p), p->pos.z };
}

vec3 player_forward(const player_t *p) {
    return (vec3){
         sinf(p->yaw) * cosf(p->pitch),
         sinf(p->pitch),
        -cosf(p->yaw) * cosf(p->pitch),
    };
}

void player_update(player_t *p, GLFWwindow *win, const world_t *world, double dt) {
    if (dt > 0.1) dt = 0.1;
    float ft = (float)dt;

    int en     = g_input_enabled;
    int kW     = en && glfwGetKey(win, GLFW_KEY_W)             == GLFW_PRESS;
    int kA     = en && glfwGetKey(win, GLFW_KEY_A)             == GLFW_PRESS;
    int kS     = en && glfwGetKey(win, GLFW_KEY_S)             == GLFW_PRESS;
    int kD     = en && glfwGetKey(win, GLFW_KEY_D)             == GLFW_PRESS;
    int kSpace = en && glfwGetKey(win, GLFW_KEY_SPACE)         == GLFW_PRESS;
    int kShift = en && glfwGetKey(win, GLFW_KEY_LEFT_SHIFT)    == GLFW_PRESS;
    int kCtrl  = en && glfwGetKey(win, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS;

    int want_crouch = (p->mode == PLAYER_WALKING) && kShift;
    if (!want_crouch && p->crouching) {
        if (aabb_intersects(p->pos, HEIGHT_STAND, world)) want_crouch = 1;
    }
    p->crouching = want_crouch;
    float h = player_height(p);

    vec3 walk_fwd = { sinf(p->yaw), 0.0f, -cosf(p->yaw) };
    vec3 right    = { cosf(p->yaw), 0.0f,  sinf(p->yaw) };

    int in_water = player_in_water(p, h, world);
    p->in_water = in_water;
    float step_vel = in_water ? STEP_JUMP_VEL_WATER : STEP_JUMP_VEL;

    int can_sprint = (p->hunger > 6);
    float h_speed;
    if (p->mode == PLAYER_FLYING)      h_speed = kCtrl ? FLY_SPEED * SPRINT_MULT : FLY_SPEED;
    else if (p->crouching)             h_speed = CROUCH_SPEED;
    else                               h_speed = (kCtrl && can_sprint) ? WALK_SPEED * SPRINT_MULT : WALK_SPEED;
    if (in_water) h_speed *= WATER_SPEED_MULT;
    {
        int spd = status_level(p, EFFECT_SPEED);
        int slo = status_level(p, EFFECT_SLOWNESS);
        h_speed *= 1.0f + 0.20f * (float)spd - 0.15f * (float)slo;
        if (h_speed < 0.0f) h_speed = 0.0f;
    }

    vec3 wish = {0.0f, 0.0f, 0.0f};
    if (kW) wish = vec3_add(wish, walk_fwd);
    if (kS) wish = vec3_sub(wish, walk_fwd);
    if (kD) wish = vec3_add(wish, right);
    if (kA) wish = vec3_sub(wish, right);

    float wlen = sqrtf(wish.x * wish.x + wish.z * wish.z);
    if (wlen > 1e-4f) {
        wish.x /= wlen;
        wish.z /= wlen;
    }

    int on_ice = p->mode == PLAYER_WALKING && !in_water && p->grounded &&
                 ground_block_under(p, world) == BLOCK_ICE;
    if (on_ice) {
        float tgt_x = wish.x * h_speed, tgt_z = wish.z * h_speed;
        float rate  = (wlen > 1e-4f) ? ICE_ACCEL : ICE_DECEL;
        float t = rate * ft;
        if (t > 1.0f) t = 1.0f;
        p->vel.x += (tgt_x - p->vel.x) * t;
        p->vel.z += (tgt_z - p->vel.z) * t;
    } else {
        p->vel.x = wish.x * h_speed;
        p->vel.z = wish.z * h_speed;
    }

    if (p->mode == PLAYER_FLYING) {
        if      (kSpace) p->vel.y =  FLY_SPEED;
        else if (kShift) p->vel.y = -FLY_DOWN_SPEED;
        else             p->vel.y =  0.0f;
    } else if (in_water) {
        if (kSpace) {
            p->vel.y = WATER_SWIM_UP_VEL;
        } else {
            p->vel.y -= GRAVITY * WATER_GRAVITY_MULT * ft;
            float term = TERMINAL_VEL * WATER_TERMINAL_MULT;
            if (p->vel.y < -term) p->vel.y = -term;
            if (p->vel.y >  term) p->vel.y =  term;
        }
    } else {
        float g = (p->step_up_timer > 0.0f) ? STEP_GRAVITY : GRAVITY;
        p->vel.y -= g * ft;
        if (p->vel.y < -TERMINAL_VEL) p->vel.y = -TERMINAL_VEL;
        if (p->step_up_timer > 0.0f) p->step_up_timer -= ft;
        if (p->grounded) p->step_up_timer = 0.0f;
    }

    int can_step_up = p->mode == PLAYER_WALKING && !p->crouching;

    int sneak_was_grounded = p->grounded && p->crouching && p->mode == PLAYER_WALKING;
    vec3 sneak_pre_pos = p->pos;

    float dx = p->vel.x * ft;
    if (dx != 0.0f) {
        vec3 t = p->pos;
        t.x += dx;
        if (aabb_intersects(t, h, world)) {
            vec3 up = t;
            up.y += STEP_UP_HEIGHT;
            if (can_step_up && p->grounded && !aabb_intersects(up, h, world)) {
                if (p->vel.y < step_vel * 0.5f) {
                    p->vel.y = step_vel;
                    p->grounded = 0;
                    p->step_up_timer = STEP_JUMP_TIME;
                }
                if (dx > 0) {
                    int b = (int)floorf(t.x + HALF_WIDTH);
                    p->pos.x = (float)b - HALF_WIDTH - EPSILON;
                } else {
                    int b = (int)floorf(t.x - HALF_WIDTH);
                    p->pos.x = (float)(b + 1) + HALF_WIDTH + EPSILON;
                }
            } else if (can_step_up && !p->grounded && !aabb_intersects(up, h, world)) {
                if (dx > 0) {
                    int b = (int)floorf(t.x + HALF_WIDTH);
                    p->pos.x = (float)b - HALF_WIDTH - EPSILON;
                } else {
                    int b = (int)floorf(t.x - HALF_WIDTH);
                    p->pos.x = (float)(b + 1) + HALF_WIDTH + EPSILON;
                }
            } else {
                if (dx > 0) {
                    int b = (int)floorf(t.x + HALF_WIDTH);
                    p->pos.x = (float)b - HALF_WIDTH - EPSILON;
                } else {
                    int b = (int)floorf(t.x - HALF_WIDTH);
                    p->pos.x = (float)(b + 1) + HALF_WIDTH + EPSILON;
                }
                p->vel.x = 0.0f;
            }
        } else {
            p->pos.x = t.x;
        }
    }

    float dz = p->vel.z * ft;
    if (dz != 0.0f) {
        vec3 t = p->pos;
        t.z += dz;
        if (aabb_intersects(t, h, world)) {
            vec3 up = t;
            up.y += STEP_UP_HEIGHT;
            if (can_step_up && p->grounded && !aabb_intersects(up, h, world)) {
                if (p->vel.y < step_vel * 0.5f) {
                    p->vel.y = step_vel;
                    p->grounded = 0;
                    p->step_up_timer = STEP_JUMP_TIME;
                }
                if (dz > 0) {
                    int b = (int)floorf(t.z + HALF_WIDTH);
                    p->pos.z = (float)b - HALF_WIDTH - EPSILON;
                } else {
                    int b = (int)floorf(t.z - HALF_WIDTH);
                    p->pos.z = (float)(b + 1) + HALF_WIDTH + EPSILON;
                }
            } else if (can_step_up && !p->grounded && !aabb_intersects(up, h, world)) {
                if (dz > 0) {
                    int b = (int)floorf(t.z + HALF_WIDTH);
                    p->pos.z = (float)b - HALF_WIDTH - EPSILON;
                } else {
                    int b = (int)floorf(t.z - HALF_WIDTH);
                    p->pos.z = (float)(b + 1) + HALF_WIDTH + EPSILON;
                }
            } else {
                if (dz > 0) {
                    int b = (int)floorf(t.z + HALF_WIDTH);
                    p->pos.z = (float)b - HALF_WIDTH - EPSILON;
                } else {
                    int b = (int)floorf(t.z - HALF_WIDTH);
                    p->pos.z = (float)(b + 1) + HALF_WIDTH + EPSILON;
                }
                p->vel.z = 0.0f;
            }
        } else {
            p->pos.z = t.z;
        }
    }

    if (sneak_was_grounded) {
        int min_x = (int)floorf(p->pos.x - HALF_WIDTH);
        int max_x = (int)ceilf (p->pos.x + HALF_WIDTH) - 1;
        int min_z = (int)floorf(p->pos.z - HALF_WIDTH);
        int max_z = (int)ceilf (p->pos.z + HALF_WIDTH) - 1;
        int by = (int)floorf(p->pos.y - 0.05f);
        int supported = 0;
        for (int zz = min_z; zz <= max_z && !supported; zz++)
            for (int xx = min_x; xx <= max_x && !supported; xx++)
                if (block_is_solid(world_get_block(world, xx, by, zz))) supported = 1;
        if (!supported) {
            p->pos.x = sneak_pre_pos.x;
            p->pos.z = sneak_pre_pos.z;
            p->vel.x = 0.0f;
            p->vel.z = 0.0f;
        }
    }

    float dy = p->vel.y * ft;
    if (dy != 0.0f) {
        vec3 t = p->pos;
        t.y += dy;
        if (aabb_intersects(t, h, world)) {
            if (dy > 0) {
                int b = (int)floorf(t.y + h);
                p->pos.y = (float)b - h - EPSILON;
            } else {
                int b = (int)floorf(t.y);
                p->pos.y = (float)(b + 1) + EPSILON;
            }
            p->vel.y = 0.0f;
        } else {
            p->pos.y = t.y;
        }
    }

    p->grounded = feet_supported(p, world) && (p->vel.y <= 0.01f);
}

void status_clear(player_t *p) {
    for (int i = 0; i < MAX_STATUS_EFFECTS; i++) { p->effects[i].id = 0; p->effects[i].amplifier = 0; p->effects[i].ticks_remaining = 0; }
}
int status_has(const player_t *p, int id) {
    for (int i = 0; i < MAX_STATUS_EFFECTS; i++)
        if (p->effects[i].id == id && p->effects[i].ticks_remaining > 0) return 1;
    return 0;
}
int status_level(const player_t *p, int id) {
    for (int i = 0; i < MAX_STATUS_EFFECTS; i++)
        if (p->effects[i].id == id && p->effects[i].ticks_remaining > 0) return p->effects[i].amplifier + 1;
    return 0;
}
void status_add(player_t *p, int id, int ticks, int amp) {
    if (id <= 0 || id >= EFFECT_COUNT) return;
    if (id == EFFECT_INSTANT_HEALTH) { player_heal(p, (float)(4 * (amp + 1))); return; }
    if (id == EFFECT_INSTANT_DAMAGE) { player_take_damage(p, (float)(3 * (amp + 1)), DMG_MAGIC); return; }
    for (int i = 0; i < MAX_STATUS_EFFECTS; i++)
        if (p->effects[i].id == id) {
            p->effects[i].ticks_remaining = (uint16_t)ticks;
            if (amp > p->effects[i].amplifier) p->effects[i].amplifier = (uint8_t)amp;
            return;
        }
    for (int i = 0; i < MAX_STATUS_EFFECTS; i++)
        if (p->effects[i].id == 0) {
            p->effects[i].id = (uint8_t)id;
            p->effects[i].ticks_remaining = (uint16_t)ticks;
            p->effects[i].amplifier = (uint8_t)amp;
            return;
        }
}
static void status_tick(player_t *p) {
    for (int i = 0; i < MAX_STATUS_EFFECTS; i++) {
        if (p->effects[i].id == 0) continue;
        if (p->effects[i].ticks_remaining > 0) p->effects[i].ticks_remaining--;
        if (p->effects[i].ticks_remaining == 0) { p->effects[i].id = 0; p->effects[i].amplifier = 0; }
    }
}

void player_heal(player_t *p, float amount) {
    if (p->is_dead || amount <= 0.0f) return;
    p->health += amount;
    if (p->health > p->max_health) p->health = p->max_health;
}
void player_add_exhaustion(player_t *p, float e) {
    p->exhaustion += e;
    if (p->exhaustion > 40.0f) p->exhaustion = 40.0f;
}
void player_on_death(player_t *p) {
    if (p->is_dead) return;
    p->is_dead = 1;
    p->health = 0.0f;
    p->death_anim_timer = 0.0f;
}

static int iframe_type(damage_type_t t) {
    return t == DMG_MOB_MELEE || t == DMG_ARROW || t == DMG_EXPLOSION ||
           t == DMG_PLAYER || t == DMG_GENERIC || t == DMG_FIRE || t == DMG_FALL;
}

void player_take_damage(player_t *p, float dmg, damage_type_t type) {
    if (p->is_dead || dmg <= 0.0f) return;
    if (iframe_type(type) && p->invuln_timer > 0.0f) return;

    float out = dmg;
    if (type != DMG_FALL && type != DMG_DROWN && type != DMG_SUFFOCATE &&
        type != DMG_VOID && type != DMG_STARVE && type != DMG_MAGIC) {
        float a = (float)p->armor_points;
        float capped = a * 0.2f;
        float def = a - dmg * 0.5f;
        if (def > capped) capped = def;
        if (capped < 0.0f) capped = 0.0f;
        if (capped > 20.0f) capped = 20.0f;
        out = dmg * (1.0f - capped / 25.0f);
    }
    if (type != DMG_VOID) {
        int res = status_level(p, EFFECT_RESISTANCE);
        out *= 1.0f - 0.20f * (float)res;
    }
    if (out < 0.0f) out = 0.0f;

    p->health -= out;
    p->last_damage_amount = out;
    p->damage_flash_timer = 0.4f;
    if (iframe_type(type)) p->invuln_timer = 0.5f;
    player_add_exhaustion(p, 0.1f);
    if (p->health <= 0.0f) { p->health = 0.0f; player_on_death(p); }
}

void player_apply_knockback(player_t *p, vec3 dir, float strength) {
    float len = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (len > 1e-4f) { dir.x /= len; dir.z /= len; } else { dir.x = 0.0f; dir.z = 0.0f; }
    p->vel.x += dir.x * strength * 8.0f;
    p->vel.z += dir.z * strength * 8.0f;
    if (p->vel.y < 5.0f) p->vel.y = 5.0f;
    p->grounded = 0;
}

int player_attack_damage(const player_t *p, item_id held) {
    int base = item_sword_damage(held);
    if (base <= 0) base = 2;
    base += 3 * status_level(p, EFFECT_STRENGTH);
    base -= 4 * status_level(p, EFFECT_WEAKNESS);
    return base < 0 ? 0 : base;
}

int player_xp_for_level(int level) {
    if (level < 0) level = 0;
    if (level <= 15) return 2 * level + 7;
    if (level <= 30) return 5 * level - 38;
    return 9 * level - 158;
}
static int total_xp_for_level(int level) {
    int t = 0;
    for (int i = 0; i < level; i++) t += player_xp_for_level(i);
    return t;
}
static void player_recompute_xp(player_t *p) {
    if (p->xp_total < 0) p->xp_total = 0;
    int lvl = 0, rem = p->xp_total;
    while (rem >= player_xp_for_level(lvl)) { rem -= player_xp_for_level(lvl); lvl++; }
    p->xp_level = lvl;
    int need = player_xp_for_level(lvl);
    p->xp_progress = need > 0 ? (float)rem / (float)need : 0.0f;
}
void player_add_xp(player_t *p, int amount) {
    if (amount <= 0) return;
    p->xp_total += amount;
    player_recompute_xp(p);
}
void player_remove_levels(player_t *p, int levels) {
    int target = p->xp_level - levels;
    if (target < 0) target = 0;
    p->xp_total = total_xp_for_level(target);
    player_recompute_xp(p);
}

void player_begin_eat(player_t *p, item_id food) {
    if (p->eating) return;
    const item_props_t *fp = item_get(food);
    if (!fp->is_food) return;
    p->eating = 1;
    p->eat_item = food;
    p->eat_timer = (float)(fp->eat_ticks ? fp->eat_ticks : 32) / TICKS_PER_SEC;
}
void player_cancel_eat(player_t *p) { p->eating = 0; p->eat_item = 0; p->eat_timer = 0.0f; }
int player_eat_tick(player_t *p, float dt) {
    if (!p->eating) return 0;
    p->eat_timer -= dt;
    if (p->eat_timer > 0.0f) return 0;
    const item_props_t *fp = item_get(p->eat_item);
    p->hunger += fp->food_hunger;
    if (p->hunger > PLAYER_MAX_HUNGER) p->hunger = PLAYER_MAX_HUNGER;
    p->saturation += fp->food_sat;
    if (p->saturation > (float)p->hunger) p->saturation = (float)p->hunger;
    if (fp->food_effect && (rand() % 100) < fp->food_effect_chance) {
        int amp = fp->food_effect_amp + potion_meta_amp(p->eat_meta);
        if (amp > 3) amp = 3;
        int ticks = (int)fp->food_effect_ticks + (int)fp->food_effect_ticks * potion_meta_durtier(p->eat_meta) / 2;
        status_add(p, fp->food_effect, ticks, amp);
    }
    p->eat_meta = 0;
    p->eating = 0;
    p->eat_item = 0;
    return 1;
}

void player_set_respawn(player_t *p, float x, float y, float z) {
    p->has_respawn = 1; p->respawn_x = x; p->respawn_y = y; p->respawn_z = z;
}
void player_respawn(player_t *p, const world_t *world) {
    p->is_dead = 0;
    p->health = p->max_health;
    p->hunger = PLAYER_MAX_HUNGER;
    p->saturation = 5.0f;
    p->exhaustion = 0.0f;
    p->air = PLAYER_MAX_AIR;
    status_clear(p);
    player_cancel_eat(p);
    p->invuln_timer = 1.5f;
    p->vel = (vec3){0.0f, 0.0f, 0.0f};
    float rx, ry, rz;
    if (p->has_respawn) {
        rx = p->respawn_x; ry = p->respawn_y; rz = p->respawn_z;
    } else {
        int sx = 0, sy = 0, sz = 0;
        world_get_spawn(world, &sx, &sy, &sz);
        if (sx == 0 && sy == 0 && sz == 0) {
            int gh = world_height_at(world_seed(world), 0, 0);
            if (gh < SEA_LEVEL) gh = SEA_LEVEL;
            rx = 0.5f; ry = (float)(gh + 2); rz = 0.5f;
        } else {
            rx = (float)sx + 0.5f; ry = (float)sy; rz = (float)sz + 0.5f;
        }
    }
    p->pos = (vec3){rx, ry, rz};
}

static void survival_tick(player_t *p, const world_t *world, long tick) {
    if (p->is_dead) return;
    status_tick(p);

    int regen = status_level(p, EFFECT_REGENERATION);
    if (regen > 0) {
        int iv = 50 >> (regen - 1); if (iv < 1) iv = 1;
        if (tick % iv == 0) player_heal(p, 1.0f);
    }
    int poison = status_level(p, EFFECT_POISON);
    if (poison > 0 && p->health > 1.0f) {
        int iv = 25 >> (poison - 1); if (iv < 1) iv = 1;
        if (tick % iv == 0) { p->health -= 1.0f; if (p->health < 1.0f) p->health = 1.0f; }
    }

    if (world_hunger(world)) {
        if (p->exhaustion >= 4.0f) {
            p->exhaustion -= 4.0f;
            if (p->saturation > 0.0f) { p->saturation -= 1.0f; if (p->saturation < 0.0f) p->saturation = 0.0f; }
            else if (p->hunger > 0) p->hunger--;
        }
        if (world_natural_regen(world) && p->hunger >= 18 && p->health < p->max_health) {
            if (tick % 40 == 0) { player_heal(p, 1.0f); player_add_exhaustion(p, 0.6f); }
        }
        if (p->hunger == 0) {
            int diff = world_difficulty(world);
            float floor_hp = (diff == DIFF_HARD) ? 0.0f : (diff == DIFF_NORMAL) ? 1.0f : (diff == DIFF_EASY) ? 10.0f : 20.0f;
            if (p->health > floor_hp && tick % 80 == 0) {
                p->health -= 1.0f;
                if (p->health < floor_hp) p->health = floor_hp;
                if (p->health <= 0.0f) { p->health = 0.0f; player_on_death(p); }
            }
        }
    } else if (p->health < p->max_health && tick % 40 == 0) {
        player_heal(p, 1.0f);
    }

    int eye_y = (int)floorf(p->pos.y + (p->crouching ? EYE_CROUCH : EYE_STAND));
    block_t eyeb = world_get_block(world, (int)floorf(p->pos.x), eye_y, (int)floorf(p->pos.z));
    if (block_is_water(eyeb) && !status_has(p, EFFECT_WATER_BREATHING)) {
        if (p->air > 0) p->air--;
        else if (tick % 20 == 0) player_take_damage(p, 2.0f, DMG_DROWN);
    } else {
        p->air += 4;
        if (p->air > PLAYER_MAX_AIR) p->air = PLAYER_MAX_AIR;
    }
}

void player_survival_update(player_t *p, const world_t *world, float dt) {
    static long s_tick = 0;
    if (p->invuln_timer > 0.0f)       p->invuln_timer -= dt;
    if (p->damage_flash_timer > 0.0f) p->damage_flash_timer -= dt;
    if (p->is_dead)                   p->death_anim_timer += dt;
    p->tick_accum += dt * TICKS_PER_SEC;
    int guard = 0;
    while (p->tick_accum >= 1.0f && guard < 10) {
        p->tick_accum -= 1.0f;
        s_tick++;
        survival_tick(p, world, s_tick);
        guard++;
    }
}
