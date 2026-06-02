#pragma once

#include <GLFW/glfw3.h>

#include "vec3.h"

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