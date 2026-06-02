#include "player.h"
#include "world.h"
#include "chunk.h"
#include <math.h>

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

#define EPSILON 1e-4f

static float g_mouse_sens = MOUSE_SENS;
static int   g_allow_fly  = 1;
static int   g_input_enabled = 1;

void player_set_sensitivity(float s) { g_mouse_sens = s; }
void player_set_allow_fly(int allow) { g_allow_fly = allow; }
void player_set_input_enabled(int e) { g_input_enabled = e; }

static int block_is_solid(block_t b) {
    return b != BLOCK_AIR && b != BLOCK_WATER;
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
                if (world_get_block(world, x, y, z) == BLOCK_WATER) return 1;
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

    float h_speed;
    if (p->mode == PLAYER_FLYING)      h_speed = kCtrl ? FLY_SPEED * SPRINT_MULT : FLY_SPEED;
    else if (p->crouching)             h_speed = CROUCH_SPEED;
    else                               h_speed = kCtrl ? WALK_SPEED * SPRINT_MULT : WALK_SPEED;
    if (in_water) h_speed *= WATER_SPEED_MULT;

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
    p->vel.x = wish.x * h_speed;
    p->vel.z = wish.z * h_speed;

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