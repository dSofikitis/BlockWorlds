#pragma once

#include <GLFW/glfw3.h>

#include "vec3.h"
#include "survival.h"
#include "registry.h"

struct world_s;
typedef struct world_s world_t;

typedef enum {
    PLAYER_WALKING,
    PLAYER_FLYING,
} player_mode_t;

typedef struct {
    vec3   pos;
    vec3   vel;
    float  yaw, pitch;
    player_mode_t mode;
    int    grounded;
    int    crouching;
    int    in_water;
    double last_space_press;
    int    selected_block;
    float  step_up_timer;

    float  health, max_health;
    int    hunger;
    float  saturation, exhaustion;
    int    air;
    int    armor_points;
    int    is_dead;
    float  invuln_timer, damage_flash_timer, last_damage_amount, death_anim_timer;
    int    xp_level, xp_total;
    float  xp_progress;
    status_effect_t effects[MAX_STATUS_EFFECTS];
    int    has_respawn;
    float  respawn_x, respawn_y, respawn_z;
    float  tick_accum;
    int    eating;
    item_id eat_item;
    float  eat_timer;
    uint8_t eat_meta;
} player_t;

void player_init(player_t *p, vec3 spawn);

void player_add_look(player_t *p, float dx, float dy);
int player_on_space_press(player_t *p);

void player_set_sensitivity(float s);
void player_set_allow_fly(int allow);
void player_set_input_enabled(int e);

void player_update(player_t *p, GLFWwindow *win, const world_t *world, double dt);

vec3 player_eye_pos(const player_t *p);
vec3 player_forward(const player_t *p);

void player_take_damage(player_t *p, float dmg, damage_type_t type);
void player_heal(player_t *p, float amount);
void player_apply_knockback(player_t *p, vec3 dir, float strength);
int  player_attack_damage(const player_t *p, item_id held);
void player_add_exhaustion(player_t *p, float e);
void player_survival_update(player_t *p, const world_t *world, float dt);
int  player_xp_for_level(int level);
void player_add_xp(player_t *p, int amount);
void player_remove_levels(player_t *p, int levels);
void player_on_death(player_t *p);
void player_respawn(player_t *p, const world_t *world);
void player_set_respawn(player_t *p, float x, float y, float z);
void player_begin_eat(player_t *p, item_id food);
void player_cancel_eat(player_t *p);
int  player_eat_tick(player_t *p, float dt);
void status_add(player_t *p, int effect_id, int ticks, int amp);
int  status_has(const player_t *p, int effect_id);
int  status_level(const player_t *p, int effect_id);
void status_clear(player_t *p);
