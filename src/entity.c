
#include "entity.h"
#include "physics.h"
#include "registry.h"
#include "survival.h"
#include "world.h"
#include "particle.h"
#include "audio.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DMG_MOB_MELEE 1
#define DMG_ARROW     2
#define DMG_EXPLOSION 3

#ifndef BLOCK_AIR
#define BLOCK_AIR 0
#endif
#ifndef BLOCK_DEEPSTONE
#define BLOCK_DEEPSTONE 6
#endif

#define AI_TICK_HZ      10.0f
#define AI_TICK_DT      (1.0f / AI_TICK_HZ)

#define ITEM_PICKUP_DELAY   0.5f
#define ITEM_PICKUP_RANGE   1.3f
#define XP_ATTRACT_RANGE    4.0f
#define XP_PICKUP_RANGE     1.0f
#define ITEM_LIFETIME       300.0f
#define ORB_LIFETIME        300.0f

#define HOSTILE_CAP   24
#define PASSIVE_CAP   16
#define SPAWN_RING_MIN  24.0f
#define SPAWN_RING_MAX  64.0f
#define SPAWN_CANDIDATES 8

#define GRAVITY        28.0f
#define ITEM_HALF_W    0.125f
#define ITEM_HEIGHT    0.25f

const mob_def_t MOB_DEFS[SP_COUNT] = {
     { 10.0f, 3.6f, 0.0f,  0.0f,  0.0f, 0.9f, 0.9f,  0.0f, 0, 1, 3, 0, 1, 0, 2, 4 },
     { 10.0f, 3.6f, 0.0f,  0.0f,  0.0f, 0.9f, 1.3f,  0.0f, 0, 1, 3, 0, 1, 0, 2, 4 },
     {  4.0f, 3.2f, 0.0f,  0.0f,  0.0f, 0.4f, 0.7f,  0.0f, 0, 1, 3, 0, 1, 0, 2, 4 },
     {  8.0f, 3.6f, 0.0f,  0.0f,  0.0f, 0.9f, 1.3f,  0.0f, 0, 1, 3, 0, 1, 0, 2, 4 },
     { 20.0f, 3.5f, 3.0f,  1.2f, 16.0f, 0.6f, 1.95f, 0.0f, 1, 5, 5, 1, 0, 1, 0, 0 },
     { 20.0f, 3.5f, 4.0f, 16.0f, 16.0f, 0.6f, 1.99f, 0.0f, 1, 5, 5, 1, 0, 1, 0, 0 },
     { 20.0f, 3.5f, 0.0f,  3.0f, 16.0f, 0.6f, 1.7f,  0.0f, 1, 5, 5, 1, 0, 1, 0, 0 },
     { 16.0f, 4.5f, 2.0f,  1.4f, 16.0f, 1.4f, 0.9f,  0.0f, 1, 5, 5, 1, 0, 1, 0, 0 },
};

static entity_system_t *g_es = NULL;

static uint32_t lcg_next(uint32_t *s) {
    *s = *s * 1664525u + 1013904223u;
    return *s;
}
static float rng_f(uint32_t *s) {
    return (float)(lcg_next(s) >> 8) / (float)(1u << 24);
}
static float rng_range(uint32_t *s, float lo, float hi) {
    return lo + (hi - lo) * rng_f(s);
}
static int rng_int(uint32_t *s, int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int)(lcg_next(s) % span);
}

static float v_len(vec3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static float v_len_xz(vec3 v) { return sqrtf(v.x * v.x + v.z * v.z); }
static float dist_xz(vec3 a, vec3 b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}
static float dist3(vec3 a, vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

entity_system_t *entity_create(void) {
    entity_system_t *es = (entity_system_t *)calloc(1, sizeof(entity_system_t));
    if (!es) return NULL;
    es->count = 0;
    es->ps = NULL;
    es->audio = NULL;
    es->spawn_timer = 0.0f;
    es->rng = 0x1234567u;
    g_es = es;
    return es;
}

void entity_destroy(entity_system_t *es) {
    if (!es) return;
    if (g_es == es) g_es = NULL;
    free(es);
}

void entity_set_fx(entity_system_t *es, particle_system_t *ps, audio_t *a) {
    if (!es) return;
    es->ps = ps;
    es->audio = a;
}

static int alloc_slot(entity_system_t *es) {
    for (int i = 0; i < MAX_ENTITIES; ++i) {
        if (!es->e[i].active) {
            memset(&es->e[i], 0, sizeof(entity_t));
            es->e[i].active = 1;
            if (i + 1 > es->count) es->count = i + 1;
            return i;
        }
    }
    return -1;
}

static void free_slot(entity_system_t *es, int i) {
    if (i < 0 || i >= MAX_ENTITIES) return;
    es->e[i].active = 0;
    es->e[i].kind = EK_NONE;
    if (i + 1 == es->count) {
        int n = es->count;
        while (n > 0 && !es->e[n - 1].active) --n;
        es->count = n;
    }
}

void entity_spawn_mob(entity_system_t *es, int species, vec3 pos) {
    if (!es) return;
    if (species < 0 || species >= SP_COUNT) return;
    int i = alloc_slot(es);
    if (i < 0) return;
    entity_t *e = &es->e[i];
    const mob_def_t *d = &MOB_DEFS[species];
    e->kind = EK_MOB;
    e->species = (species_t)species;
    e->pos = pos;
    e->vel = (vec3){ 0.0f, 0.0f, 0.0f };
    e->health = d->max_hp;
    e->knockback_resist = d->knock_resist;
    e->ai = AI_IDLE;
    e->target_eid = -1;
    e->state_timer = rng_range(&es->rng, 0.5f, 2.5f);
    e->attack_cooldown = 0.0f;
    e->yaw = rng_range(&es->rng, 0.0f, 6.2831853f);
    e->head_yaw = e->yaw;
    e->wander_target = pos;
}

void entity_spawn_item(entity_system_t *es, vec3 pos, item_id id, int count, uint16_t durability) {
    if (!es) return;
    if (id == 0 || count <= 0) return;
    int i = alloc_slot(es);
    if (i < 0) return;
    entity_t *e = &es->e[i];
    e->kind = EK_ITEM;
    e->pos = pos;
    e->vel = (vec3){ rng_range(&es->rng, -1.5f, 1.5f), rng_range(&es->rng, 1.0f, 2.5f),
                     rng_range(&es->rng, -1.5f, 1.5f) };
    e->item_id_payload = id;
    e->item_count = (uint8_t)(count > 255 ? 255 : count);
    e->item_durability = durability;
    e->pickup_delay = ITEM_PICKUP_DELAY;
    e->bob_phase = rng_range(&es->rng, 0.0f, 6.2831853f);
}

void entity_spawn_thrown(entity_system_t *es, vec3 pos, vec3 vel, item_id potion_id, uint16_t meta) {
    if (!es) return;
    int i = alloc_slot(es);
    if (i < 0) return;
    entity_t *e = &es->e[i];
    e->kind = EK_THROWN;
    e->pos = pos;
    e->vel = vel;
    e->item_id_payload = potion_id;
    e->item_durability = meta;
    e->bob_phase = rng_range(&es->rng, 0.0f, 6.2831853f);
    e->shooter_eid = -1;
}

void entity_spawn_arrow(entity_system_t *es, vec3 pos, vec3 vel, int shooter_eid, item_id tip) {
    if (!es) return;
    int i = alloc_slot(es);
    if (i < 0) return;
    entity_t *a = &es->e[i];
    a->kind = EK_ARROW;
    a->pos = pos;
    a->vel = vel;
    a->yaw = atan2f(vel.x, vel.z);
    a->head_pitch = atan2f(vel.y, v_len_xz(vel));
    a->age = 0.0f;
    a->on_ground = 0;
    a->shooter_eid = (int32_t)shooter_eid;
    a->item_id_payload = tip;
}

void entity_spawn_xp(entity_system_t *es, vec3 pos, int amount) {
    if (!es) return;
    if (amount <= 0) return;
    int i = alloc_slot(es);
    if (i < 0) return;
    entity_t *e = &es->e[i];
    e->kind = EK_XP_ORB;
    e->pos = pos;
    e->vel = (vec3){ rng_range(&es->rng, -1.0f, 1.0f), rng_range(&es->rng, 1.0f, 2.0f),
                     rng_range(&es->rng, -1.0f, 1.0f) };
    e->xp_amount = (uint16_t)(amount > 65535 ? 65535 : amount);
    e->bob_phase = rng_range(&es->rng, 0.0f, 6.2831853f);
}

int entity_active_count(const entity_system_t *es) {
    if (!es) return 0;
    int n = 0;
    for (int i = 0; i < es->count; ++i)
        if (es->e[i].active) ++n;
    return n;
}

int entity_count_hostile(const entity_system_t *es) {
    if (!es) return 0;
    int n = 0;
    for (int i = 0; i < es->count; ++i) {
        const entity_t *e = &es->e[i];
        if (e->active && e->kind == EK_MOB && e->species >= SPECIES_FIRST_HOSTILE && e->ai != AI_DEAD)
            ++n;
    }
    return n;
}

static int count_passive(const entity_system_t *es) {
    int n = 0;
    for (int i = 0; i < es->count; ++i) {
        const entity_t *e = &es->e[i];
        if (e->active && e->kind == EK_MOB && e->species < SPECIES_FIRST_HOSTILE && e->ai != AI_DEAD)
            ++n;
    }
    return n;
}

static float diff_attack_mult(int difficulty) {
    switch (difficulty) {
        case 1:  return 0.5f;
        case 2:  return 1.0f;
        case 3:  return 1.5f;
        default: return 0.0f;
    }
}
static float diff_spawn_factor(int difficulty) {
    switch (difficulty) {
        case 1:  return 0.6f;
        case 2:  return 1.0f;
        case 3:  return 1.3f;
        default: return 0.0f;
    }
}

static int species_audio_cat(species_t sp) {
    return (int)sp;
}

static void roll_species_drops(entity_system_t *es, const entity_t *e) {
    vec3 p = e->pos;
    uint32_t *r = &es->rng;
    switch (e->species) {
        case SP_PIG:
            entity_spawn_item(es, p, ITEM_RAW_PORK, rng_int(r, 1, 3), 0);
            break;
        case SP_COW: {
            entity_spawn_item(es, p, ITEM_RAW_BEEF, rng_int(r, 1, 3), 0);
            int leather = rng_int(r, 0, 2);
            if (leather > 0) entity_spawn_item(es, p, ITEM_LEATHER, leather, 0);
            break;
        }
        case SP_CHICKEN: {
            entity_spawn_item(es, p, ITEM_RAW_CHICKEN, 1, 0);
            int feather = rng_int(r, 0, 2);
            if (feather > 0) entity_spawn_item(es, p, ITEM_FEATHER, feather, 0);
            break;
        }
        case SP_SHEEP:
            entity_spawn_item(es, p, ITEM_RAW_MUTTON, rng_int(r, 1, 2), 0);
            entity_spawn_item(es, p, BLOCK_WOOL, 1, 0);
            break;
        case SP_ZOMBIE: {
            int flesh = rng_int(r, 0, 2);
            if (flesh > 0) entity_spawn_item(es, p, ITEM_ROTTEN_FLESH, flesh, 0);
            if (rng_int(r, 1, 100) <= 3) entity_spawn_item(es, p, ITEM_IRON_INGOT, 1, 0);
            if (rng_int(r, 1, 100) <= 4) entity_spawn_item(es, p, ITEM_NETHER_WART, 1, 0);
            if (rng_int(r, 1, 100) <= 4) entity_spawn_item(es, p, ITEM_REDSTONE, rng_int(r, 1, 2), 0);
            break;
        }
        case SP_SKELETON: {
            int bone = rng_int(r, 0, 2);
            if (bone > 0) entity_spawn_item(es, p, ITEM_BONE, bone, 0);
            int arrow = rng_int(r, 0, 2);
            if (arrow > 0) entity_spawn_item(es, p, ITEM_ARROW, arrow, 0);
            if (rng_int(r, 1, 100) <= 3) entity_spawn_item(es, p, ITEM_BLAZE_ROD, 1, 0);
            if (rng_int(r, 1, 100) <= 4) entity_spawn_item(es, p, ITEM_REDSTONE, rng_int(r, 1, 2), 0);
            break;
        }
        case SP_CREEPER: {
            int gp = rng_int(r, 0, 2);
            if (gp > 0) entity_spawn_item(es, p, ITEM_GUNPOWDER, gp, 0);
            if (rng_int(r, 1, 100) <= 3) entity_spawn_item(es, p, ITEM_GHAST_TEAR, 1, 0);
            if (rng_int(r, 1, 100) <= 3) entity_spawn_item(es, p, ITEM_MAGMA_CREAM, 1, 0);
            break;
        }
        case SP_SPIDER: {
            int str = rng_int(r, 0, 2);
            if (str > 0) entity_spawn_item(es, p, ITEM_STRING, str, 0);
            int eye = rng_int(r, 0, 1);
            if (eye > 0) entity_spawn_item(es, p, ITEM_SPIDER_EYE, eye, 0);
            if (rng_int(r, 1, 100) <= 3) entity_spawn_item(es, p, ITEM_PUFFERFISH, 1, 0);
            if (rng_int(r, 1, 100) <= 2) entity_spawn_item(es, p, ITEM_GLISTERING_MELON, 1, 0);
            break;
        }
        default: break;
    }
}

static void mob_die(entity_system_t *es, entity_t *e, int give_drops) {
    if (e->ai == AI_DEAD) return;
    const mob_def_t *d = &MOB_DEFS[e->species];
    if (give_drops) {
        roll_species_drops(es, e);
        int xp = rng_int(&es->rng, d->xp_min, d->xp_max);
        if (xp > 0) entity_spawn_xp(es, e->pos, xp);
    }
    if (es->ps) particle_mob_death(es->ps, e->pos.x, e->pos.y, e->pos.z);
    if (es->audio) audio_play_mob_death(es->audio, species_audio_cat(e->species));
    e->ai = AI_DEAD;
    e->active = 1;
}

static void mob_take_damage(entity_system_t *es, entity_t *e, float dmg, vec3 src,
                            float kb, int crit) {
    if (e->ai == AI_DEAD) return;
    e->health -= dmg;
    e->hurt_flash = 0.4f;

    vec3 dir = vec3_sub(e->pos, src);
    dir.y = 0.0f;
    float l = v_len_xz(dir);
    if (l > 0.0001f) {
        dir.x /= l; dir.z /= l;
    } else {
        dir.x = 0.0f; dir.z = 0.0f;
    }
    float k = kb * (1.0f - e->knockback_resist);
    e->vel.x += dir.x * k * 8.0f;
    e->vel.z += dir.z * k * 8.0f;
    if (e->vel.y < 5.0f) e->vel.y = 5.0f;

    if (es->ps) {
        particle_hit(es->ps, e->pos.x, e->pos.y + MOB_DEFS[e->species].h * 0.5f, e->pos.z);
        if (crit) particle_crit(es->ps, e->pos.x, e->pos.y + MOB_DEFS[e->species].h * 0.7f, e->pos.z);
    }
    if (es->audio) audio_play_mob_hurt(es->audio, species_audio_cat(e->species));

    if (e->species < SPECIES_FIRST_HOSTILE) {
        e->ai = AI_FLEE;
        e->state_timer = rng_range(&es->rng, 3.0f, 5.0f);
    }

    if (e->health <= 0.0f) mob_die(es, e, 1);
}

static void creeper_explode(entity_system_t *es, entity_t *e, player_ctx_t *pc) {
    const float R = 3.0f;
    const float power = 3.0f;

    if (pc) {
        float dist = dist3(pc->pos, e->pos);
        if (dist < R) {
            float impact = (1.0f - dist / R);
            float mult = diff_attack_mult(pc->difficulty);
            float dmg = (impact * impact * 7.0f + impact) * 2.0f * power * mult;
            if (dmg > 24.0f) dmg = 24.0f;
            if (mult > 0.0f && dmg > 0.0f && pc->hurt_player)
                pc->hurt_player(dmg, DMG_EXPLOSION, e->pos);
        }
        if (pc->break_block) {
            int cx = (int)floorf(e->pos.x);
            int cy = (int)floorf(e->pos.y);
            int cz = (int)floorf(e->pos.z);
            int ri = (int)R;
            for (int dy = -ri; dy <= ri; ++dy)
                for (int dz = -ri; dz <= ri; ++dz)
                    for (int dx = -ri; dx <= ri; ++dx) {
                        float fd = sqrtf((float)(dx * dx + dy * dy + dz * dz));
                        if (fd > R) continue;
                        pc->break_block(cx + dx, cy + dy, cz + dz);
                    }
        }
    }

    if (es->ps) particle_explosion(es->ps, e->pos.x, e->pos.y, e->pos.z, R);
    if (es->audio) audio_play_explosion(es->audio);

    mob_die(es, e, 1);
}

static void steer_toward(entity_t *e, vec3 target, float speed) {
    vec3 d = vec3_sub(target, e->pos);
    d.y = 0.0f;
    float l = v_len_xz(d);
    if (l > 0.0001f) {
        e->vel.x = (d.x / l) * speed;
        e->vel.z = (d.z / l) * speed;
        e->yaw = atan2f(d.x, d.z);
        e->head_yaw = e->yaw;
    } else {
        e->vel.x = 0.0f;
        e->vel.z = 0.0f;
    }
}
static void steer_away(entity_t *e, vec3 from, float speed) {
    vec3 d = vec3_sub(e->pos, from);
    d.y = 0.0f;
    float l = v_len_xz(d);
    if (l > 0.0001f) {
        e->vel.x = (d.x / l) * speed;
        e->vel.z = (d.z / l) * speed;
        e->yaw = atan2f(d.x, d.z);
        e->head_yaw = e->yaw;
    }
}

static void maybe_jump(entity_t *e) {
    if (e->on_ground && (e->vel.x != 0.0f || e->vel.z != 0.0f)) {
        e->vel.y = 8.5f;
        e->on_ground = 0;
    }
}

static void mob_ai_tick(entity_system_t *es, world_t *world, player_ctx_t *pc,
                        entity_t *e, int day_is_night) {
    const mob_def_t *d = &MOB_DEFS[e->species];
    float speed = d->speed;
    vec3 ppos = pc ? pc->pos : e->pos;
    float pdist = pc ? dist3(e->pos, ppos) : 1e9f;
    int hostile = (e->species >= SPECIES_FIRST_HOSTILE);
    (void)day_is_night;

    if (!hostile) {
        switch (e->ai) {
            case AI_FLEE:
                if (pc) steer_away(e, ppos, speed * 1.3f);
                if (!aabb_overlaps_solid(e->pos, d->w * 0.5f, d->h, world)) {  }
                e->state_timer -= AI_TICK_DT;
                if (e->state_timer <= 0.0f) { e->ai = AI_IDLE; e->state_timer = rng_range(&es->rng, 1.0f, 3.0f); }
                break;
            case AI_WANDER: {
                steer_toward(e, e->wander_target, speed * 0.5f);
                if (dist_xz(e->pos, e->wander_target) < 1.0f || e->state_timer <= 0.0f) {
                    e->ai = AI_IDLE;
                    e->state_timer = rng_range(&es->rng, 1.0f, 4.0f);
                    e->vel.x = e->vel.z = 0.0f;
                }
                e->state_timer -= AI_TICK_DT;
                break;
            }
            case AI_IDLE:
            default:
                e->vel.x = e->vel.z = 0.0f;
                e->state_timer -= AI_TICK_DT;
                if (e->state_timer <= 0.0f) {
                    float ang = rng_range(&es->rng, 0.0f, 6.2831853f);
                    float r = rng_range(&es->rng, 3.0f, 8.0f);
                    e->wander_target = (vec3){ e->pos.x + cosf(ang) * r, e->pos.y,
                                               e->pos.z + sinf(ang) * r };
                    e->ai = AI_WANDER;
                    e->state_timer = rng_range(&es->rng, 3.0f, 6.0f);
                }
                break;
        }
        return;
    }

    int in_sight = (pc && pc->player_targetable && pdist < d->sight);

    if (e->species == SP_CREEPER) {
        switch (e->ai) {
            case AI_FUSE:
                e->vel.x = e->vel.z = 0.0f;
                e->state_timer -= AI_TICK_DT;
                if (pc && pdist > d->atk_range * 1.6f) {
                    e->ai = AI_CHASE;
                    e->state_timer = 0.0f;
                } else if (e->state_timer <= 0.0f) {
                    creeper_explode(es, e, pc);
                }
                break;
            case AI_CHASE:
                if (in_sight) {
                    steer_toward(e, ppos, speed);
                    if (pdist < d->atk_range) {
                        e->ai = AI_FUSE;
                        e->state_timer = 1.5f;
                        if (es->audio) audio_play_fuse(es->audio);
                    }
                } else {
                    e->ai = AI_IDLE;
                    e->state_timer = rng_range(&es->rng, 1.0f, 3.0f);
                }
                break;
            default:
                if (in_sight) {
                    e->ai = AI_CHASE;
                } else {
                    e->state_timer -= AI_TICK_DT;
                    if (e->state_timer <= 0.0f) {
                        float ang = rng_range(&es->rng, 0.0f, 6.2831853f);
                        float r = rng_range(&es->rng, 3.0f, 8.0f);
                        e->wander_target = (vec3){ e->pos.x + cosf(ang) * r, e->pos.y, e->pos.z + sinf(ang) * r };
                        steer_toward(e, e->wander_target, speed * 0.4f);
                        e->state_timer = rng_range(&es->rng, 2.0f, 5.0f);
                    }
                }
                break;
        }
        return;
    }

    if (e->species == SP_SKELETON) {
        if (in_sight) {
            float ideal = 8.0f;
            if (pdist < ideal - 1.0f) {
                steer_away(e, ppos, speed);
                e->ai = AI_STRAFE;
            } else if (pdist > ideal + 1.0f) {
                steer_toward(e, ppos, speed);
                e->ai = AI_CHASE;
            } else {
                e->vel.x = e->vel.z = 0.0f;
                e->ai = AI_STRAFE;
                vec3 dd = vec3_sub(ppos, e->pos);
                e->yaw = atan2f(dd.x, dd.z);
                e->head_yaw = e->yaw;
            }
            if (pc && e->attack_cooldown <= 0.0f && pdist < d->atk_range) {
                float mult = diff_attack_mult(pc->difficulty);
                e->attack_cooldown = rng_range(&es->rng, 1.5f, 2.5f);
                if (mult > 0.0f) {
                    if (es->audio) audio_play_bow(es->audio);
                    int shooter = (int)(e - es->e);
                    int ai = alloc_slot(es);
                    if (ai >= 0) {
                        entity_t *arr = &es->e[ai];
                        arr->kind = EK_ARROW;
                        arr->pos = (vec3){ e->pos.x, e->pos.y + d->h * 0.85f, e->pos.z };
                        vec3 av = vec3_sub(ppos, arr->pos);
                        av.y += pdist * 0.06f;
                        av.x += rng_range(&es->rng, -0.6f, 0.6f);
                        av.y += rng_range(&es->rng, -0.35f, 0.35f);
                        av.z += rng_range(&es->rng, -0.6f, 0.6f);
                        float al = v_len(av);
                        if (al > 0.0001f) arr->vel = vec3_scale(av, 28.0f / al);
                        arr->yaw = atan2f(arr->vel.x, arr->vel.z);
                        arr->head_pitch = atan2f(arr->vel.y, v_len_xz(arr->vel));
                        arr->age = 0.0f;
                        arr->on_ground = 0;
                        arr->shooter_eid = (int32_t)shooter;
                    }
                }
            }
        } else {
            e->ai = AI_IDLE;
            e->vel.x = e->vel.z = 0.0f;
        }
        return;
    }

    if (in_sight) {
        steer_toward(e, ppos, speed);
        e->ai = AI_CHASE;
        if (pc && pdist < d->atk_range) {
            e->ai = AI_ATTACK;
            if (e->attack_cooldown <= 0.0f) {
                float mult = diff_attack_mult(pc->difficulty);
                if (mult > 0.0f && pc->hurt_player)
                    pc->hurt_player(d->atk_dmg * mult, DMG_MOB_MELEE, e->pos);
                e->attack_cooldown = 1.0f;
            }
        }
    } else {
        e->state_timer -= AI_TICK_DT;
        if (e->state_timer <= 0.0f) {
            float ang = rng_range(&es->rng, 0.0f, 6.2831853f);
            float r = rng_range(&es->rng, 3.0f, 8.0f);
            e->wander_target = (vec3){ e->pos.x + cosf(ang) * r, e->pos.y, e->pos.z + sinf(ang) * r };
            steer_toward(e, e->wander_target, speed * 0.4f);
            e->ai = AI_WANDER;
            e->state_timer = rng_range(&es->rng, 2.0f, 5.0f);
        } else if (e->ai != AI_WANDER) {
            e->vel.x = e->vel.z = 0.0f;
            e->ai = AI_IDLE;
        }
    }
}

static int ray_aabb(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, float reach, float *tnear);

static void splash_burst(entity_system_t *es, player_ctx_t *pc, entity_t *e) {
    const item_props_t *pp = item_get(potion_base(e->item_id_payload));
    int eff = pp->food_effect;
    int amp = pp->food_effect_amp + potion_meta_amp(e->item_durability);
    if (amp > 3) amp = 3;
    int ticks = (int)pp->food_effect_ticks + (int)pp->food_effect_ticks * potion_meta_durtier(e->item_durability) / 2;
    const float R = 4.0f;

    if (es->ps) {
        particle_water_splash(es->ps, e->pos.x, e->pos.y, e->pos.z);
        particle_explosion(es->ps, e->pos.x, e->pos.y, e->pos.z, 1.2f);
    }
    if (es->audio) audio_play_explosion(es->audio);

    if (pc && pc->player_targetable && dist3(e->pos, pc->pos) < R && eff && pc->apply_effect)
        pc->apply_effect(eff, ticks, amp);

    for (int j = 0; j < es->count; ++j) {
        entity_t *m = &es->e[j];
        if (!m->active || m->kind != EK_MOB || m->ai == AI_DEAD) continue;
        if (dist3(e->pos, m->pos) >= R) continue;
        if (eff == EFFECT_INSTANT_DAMAGE) mob_take_damage(es, m, 6.0f * (float)(amp + 1), e->pos, 0.0f, 0);
        else if (eff == EFFECT_POISON)    mob_take_damage(es, m, 2.0f * (float)(amp + 1), e->pos, 0.0f, 0);
    }

    if (eff) {
        int ci = alloc_slot(es);
        if (ci >= 0) {
            entity_t *c = &es->e[ci];
            c->kind = EK_CLOUD;
            c->pos = e->pos;
            c->item_id_payload = e->item_id_payload;
            c->item_durability = e->item_durability;
            c->age = 0.0f; c->state_timer = 0.0f; c->bob_phase = 0.0f;
        }
    }
}

void entity_update(entity_system_t *es, world_t *world, player_ctx_t *pc, float dt) {
    if (!es || dt <= 0.0f) return;

    for (int i = 0; i < es->count; ++i) {
        entity_t *e = &es->e[i];
        if (!e->active) continue;

        e->age += dt;
        if (e->hurt_flash > 0.0f) e->hurt_flash -= dt;
        if (e->attack_cooldown > 0.0f) e->attack_cooldown -= dt;
        if (e->pickup_delay > 0.0f) e->pickup_delay -= dt;

        if (e->kind == EK_MOB && e->ai == AI_DEAD) {
            free_slot(es, i);
            continue;
        }

        switch (e->kind) {
            case EK_MOB: {
                const mob_def_t *d = &MOB_DEFS[e->species];

                e->bob_phase += dt;
                if (e->bob_phase >= AI_TICK_DT) {
                    e->bob_phase = 0.0f;
                    mob_ai_tick(es, world, pc, e, 0);
                }

                e->vel.y -= GRAVITY * dt;
                if (e->vel.y < -60.0f) e->vel.y = -60.0f;

                {
                    int fx = (int)floorf(e->pos.x), fz = (int)floorf(e->pos.z);
                    block_t feetb = world_get_block(world, fx, (int)floorf(e->pos.y + 0.1f), fz);
                    block_t headb = world_get_block(world, fx, (int)floorf(e->pos.y + d->h * 0.85f), fz);
                    int in_water = (block_is_water(feetb) || block_is_water(headb));
                    e->in_water = (uint8_t)in_water;
                    if (in_water) {
                        e->vel.x *= 0.5f;
                        e->vel.z *= 0.5f;
                        int sinker = (e->species == SP_ZOMBIE || e->species == SP_SKELETON ||
                                      e->species == SP_CREEPER);
                        if (sinker) {
                            if (block_is_water(headb)) {
                                e->health -= 2.0f * dt;
                                e->hurt_flash = 0.25f;
                                if (e->health <= 0.0f) { mob_die(es, e, 1); }
                            }
                        } else {
                            if (e->vel.y < 2.2f) e->vel.y += 22.0f * dt;
                            if (e->vel.y > 2.5f) e->vel.y = 2.5f;
                        }
                    }
                }

                aabb_move_t m;
                m.pos = e->pos;
                m.vel = e->vel;
                m.hit_x = m.hit_y = m.hit_z = 0;
                m.grounded = e->on_ground;
                aabb_move_resolve(&m, d->w * 0.5f, d->h, world, 1.0f, dt);
                e->pos = m.pos;
                e->vel = m.vel;
                e->on_ground = (uint8_t)m.grounded;

                if ((m.hit_x || m.hit_z) && e->on_ground) maybe_jump(e);

                e->anim_phase += v_len_xz(e->vel) * dt * 2.0f;

                if (pc) {
                    float pd = dist3(e->pos, pc->pos);
                    if (pd > 128.0f || (pd > 64.0f && e->age > 30.0f)) {
                        free_slot(es, i);
                        continue;
                    }
                }
                break;
            }
            case EK_ITEM: {
                e->bob_phase += dt * 2.0f;
                e->vel.y -= GRAVITY * dt;
                if (e->vel.y < -60.0f) e->vel.y = -60.0f;

                aabb_move_t m;
                m.pos = e->pos;
                m.vel = e->vel;
                m.hit_x = m.hit_y = m.hit_z = 0;
                m.grounded = 0;
                aabb_move_resolve(&m, ITEM_HALF_W, ITEM_HEIGHT, world, 0.0f, dt);
                e->pos = m.pos;
                e->vel = m.vel;
                e->on_ground = (uint8_t)m.grounded;
                if (e->on_ground) { e->vel.x *= 0.6f; e->vel.z *= 0.6f; }

                if (pc && e->pickup_delay <= 0.0f && e->item_count > 0 &&
                    dist3(e->pos, pc->pos) < ITEM_PICKUP_RANGE && pc->try_pickup) {
                    int taken = pc->try_pickup(e->item_id_payload, (int)e->item_count, e->item_durability);
                    if (taken > 0) {
                        if (es->audio) audio_play_pickup(es->audio);
                        if (taken >= e->item_count) { free_slot(es, i); continue; }
                        e->item_count = (uint8_t)(e->item_count - taken);
                    }
                }

                if (e->age > ITEM_LIFETIME) { free_slot(es, i); continue; }
                break;
            }
            case EK_XP_ORB: {
                e->bob_phase += dt * 3.0f;
                if (pc && dist3(e->pos, pc->pos) < XP_ATTRACT_RANGE) {
                    vec3 d = vec3_sub(pc->pos, e->pos);
                    float l = v_len(d);
                    if (l > 0.0001f) {
                        float s = 6.0f;
                        e->vel = vec3_scale(d, s / l);
                    }
                } else {
                    e->vel.y -= GRAVITY * dt;
                    if (e->vel.y < -60.0f) e->vel.y = -60.0f;
                }

                aabb_move_t m;
                m.pos = e->pos;
                m.vel = e->vel;
                m.hit_x = m.hit_y = m.hit_z = 0;
                m.grounded = 0;
                aabb_move_resolve(&m, ITEM_HALF_W, ITEM_HEIGHT, world, 0.0f, dt);
                e->pos = m.pos;
                e->vel = m.vel;

                if (pc && dist3(e->pos, pc->pos) < XP_PICKUP_RANGE) {
                    if (pc->grant_xp) pc->grant_xp((int)e->xp_amount);
                    if (es->ps) particle_xp_sparkle(es->ps, e->pos.x, e->pos.y, e->pos.z);
                    if (es->audio) audio_play_xp_pickup(es->audio);
                    free_slot(es, i);
                    continue;
                }

                if (e->age > ORB_LIFETIME) { free_slot(es, i); continue; }
                break;
            }
            case EK_ARROW: {
                if (e->on_ground) {
                    if (e->age > 6.0f) { free_slot(es, i); continue; }
                    break;
                }
                if (e->age > 12.0f) { free_slot(es, i); continue; }

                vec3 prev = e->pos;
                e->vel.y -= GRAVITY * 0.65f * dt;
                vec3 step = vec3_scale(e->vel, dt);
                float steplen = v_len(step);
                vec3 newpos = vec3_add(e->pos, step);
                vec3 rd = vec3_normalize(e->vel);

                e->yaw = atan2f(e->vel.x, e->vel.z);
                e->head_pitch = atan2f(e->vel.y, v_len_xz(e->vel));

                int hit_mob = -1; float best = steplen + 0.001f;
                if (e->shooter_eid < 0 || e->shooter_eid >= es->count) e->shooter_eid = -1;
                for (int j = 0; j < es->count; ++j) {
                    if (j == e->shooter_eid) continue;
                    entity_t *m = &es->e[j];
                    if (!m->active || m->kind != EK_MOB || m->ai == AI_DEAD) continue;
                    const mob_def_t *md = &MOB_DEFS[m->species];
                    float hw = md->w * 0.5f + 0.12f;
                    vec3 bmin = { m->pos.x - hw, m->pos.y, m->pos.z - hw };
                    vec3 bmax = { m->pos.x + hw, m->pos.y + md->h + 0.1f, m->pos.z + hw };
                    float t;
                    if (ray_aabb(prev, rd, bmin, bmax, steplen, &t) && t < best) { best = t; hit_mob = j; }
                }

                int hit_player = 0; float pbest = steplen + 0.001f;
                if (pc && pc->player_targetable && e->shooter_eid >= 0) {
                    vec3 bmin = { pc->pos.x - 0.32f, pc->pos.y,        pc->pos.z - 0.32f };
                    vec3 bmax = { pc->pos.x + 0.32f, pc->pos.y + 1.85f, pc->pos.z + 0.32f };
                    float t;
                    if (ray_aabb(prev, rd, bmin, bmax, steplen, &t)) { hit_player = 1; pbest = t; }
                }

                int tip_eff = 0, tip_ticks = 0, tip_amp = 0;
                if (item_is_potion(e->item_id_payload)) {
                    const item_props_t *tp = item_get(potion_base(e->item_id_payload));
                    tip_eff = tp->food_effect; tip_ticks = (int)tp->food_effect_ticks; tip_amp = tp->food_effect_amp;
                }

                if (hit_player && pbest <= best) {
                    if (pc->hurt_player) pc->hurt_player(4.0f, DMG_ARROW, e->pos);
                    if (tip_eff && pc->apply_effect) pc->apply_effect(tip_eff, tip_ticks, tip_amp);
                    free_slot(es, i);
                    continue;
                }
                if (hit_mob >= 0) {
                    mob_take_damage(es, &es->e[hit_mob], 3.0f, prev, 0.4f, 0);
                    if (tip_eff == EFFECT_INSTANT_DAMAGE) mob_take_damage(es, &es->e[hit_mob], 5.0f, prev, 0.0f, 0);
                    else if (tip_eff == EFFECT_POISON)    mob_take_damage(es, &es->e[hit_mob], 2.0f, prev, 0.0f, 0);
                    free_slot(es, i);
                    continue;
                }

                if (aabb_overlaps_solid(newpos, 0.05f, 0.05f, world)) {
                    e->vel = (vec3){ 0.0f, 0.0f, 0.0f };
                    e->on_ground = 1;
                    e->age = 0.0f;
                    break;
                }
                e->pos = newpos;
                break;
            }
            case EK_THROWN: {
                e->vel.y -= GRAVITY * 0.8f * dt;
                e->bob_phase += dt * 12.0f;
                vec3 newpos = vec3_add(e->pos, vec3_scale(e->vel, dt));

                int hit = aabb_overlaps_solid(newpos, 0.1f, 0.1f, world);
                if (!hit && pc && pc->player_targetable && e->age > 0.05f &&
                    dist3(newpos, pc->pos) < 0.8f) hit = 1;
                if (!hit) {
                    for (int j = 0; j < es->count && !hit; ++j) {
                        entity_t *m = &es->e[j];
                        if (!m->active || m->kind != EK_MOB || m->ai == AI_DEAD) continue;
                        if (dist3(newpos, m->pos) < 0.8f) hit = 1;
                    }
                }
                if (hit || e->age > 6.0f) {
                    e->pos = newpos;
                    splash_burst(es, pc, e);
                    free_slot(es, i);
                    continue;
                }
                e->pos = newpos;
                break;
            }
            case EK_CLOUD: {
                const float life = 6.0f, R = 3.0f;
                if (e->age > life) { free_slot(es, i); continue; }
                e->bob_phase += dt;
                if (e->bob_phase > 0.16f) {
                    e->bob_phase = 0.0f;
                    if (es->ps)
                        particle_water_splash(es->ps,
                            e->pos.x + rng_range(&es->rng, -R * 0.6f, R * 0.6f),
                            e->pos.y + 0.2f,
                            e->pos.z + rng_range(&es->rng, -R * 0.6f, R * 0.6f));
                }
                e->state_timer -= dt;
                if (e->state_timer <= 0.0f) {
                    e->state_timer = 0.9f;
                    const item_props_t *pp = item_get(potion_base(e->item_id_payload));
                    int eff = pp->food_effect;
                    int amp = pp->food_effect_amp + potion_meta_amp(e->item_durability);
                    if (amp > 3) amp = 3;
                    if (pc && pc->player_targetable && eff && pc->apply_effect &&
                        dist3(e->pos, pc->pos) < R)
                        pc->apply_effect(eff, 110, amp);
                    for (int j = 0; j < es->count; ++j) {
                        entity_t *m = &es->e[j];
                        if (!m->active || m->kind != EK_MOB || m->ai == AI_DEAD) continue;
                        if (dist3(e->pos, m->pos) >= R) continue;
                        if (eff == EFFECT_INSTANT_DAMAGE) mob_take_damage(es, m, 3.0f, e->pos, 0.0f, 0);
                        else if (eff == EFFECT_POISON)    mob_take_damage(es, m, 1.0f, e->pos, 0.0f, 0);
                    }
                }
                break;
            }
            default:
                free_slot(es, i);
                break;
        }
    }
}

static int ray_aabb(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, float reach, float *tnear) {
    float tmin = 0.0f, tmax = reach;
    const float *o = &ro.x, *dr = &rd.x;
    const float *mn = &bmin.x, *mx = &bmax.x;
    for (int a = 0; a < 3; ++a) {
        float oa = o[a], da = dr[a], lo = mn[a], hi = mx[a];
        if (fabsf(da) < 1e-6f) {
            if (oa < lo || oa > hi) return 0;
        } else {
            float inv = 1.0f / da;
            float t1 = (lo - oa) * inv;
            float t2 = (hi - oa) * inv;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return 0;
        }
    }
    *tnear = tmin;
    return 1;
}

int entity_player_attack(entity_system_t *es, world_t *world, player_ctx_t *pc,
                         vec3 eye, vec3 dir, float reach, int damage, float kb, int crit) {
    (void)world;
    if (!es) return 0;

    vec3 rd = vec3_normalize(dir);
    int best = -1;
    float best_t = reach + 1.0f;

    for (int i = 0; i < es->count; ++i) {
        entity_t *e = &es->e[i];
        if (!e->active || e->kind != EK_MOB || e->ai == AI_DEAD) continue;
        const mob_def_t *d = &MOB_DEFS[e->species];
        float hw = d->w * 0.5f + 0.1f;
        vec3 bmin = { e->pos.x - hw, e->pos.y, e->pos.z - hw };
        vec3 bmax = { e->pos.x + hw, e->pos.y + d->h + 0.1f, e->pos.z + hw };
        float t;
        if (ray_aabb(eye, rd, bmin, bmax, reach, &t)) {
            if (t < best_t) { best_t = t; best = i; }
        }
    }

    if (best < 0) return 0;

    entity_t *e = &es->e[best];
    float dmg = (float)damage;
    if (crit) dmg += (float)damage * 0.5f;

    vec3 src = pc ? pc->pos : eye;
    mob_take_damage(es, e, dmg, src, kb, crit);
    return (!e->active || e->ai == AI_DEAD || e->health <= 0.0f) ? 2 : 1;
}

void entity_spawn_pass(entity_system_t *es, world_t *world, vec3 player_pos,
                       int is_night, float spawn_rate, int difficulty, int mob_spawning) {
    if (!es || !world) return;
    if (!mob_spawning || difficulty <= 0) return;

    float diff_factor = diff_spawn_factor(difficulty);
    if (diff_factor <= 0.0f) return;
    float scale = spawn_rate * diff_factor;

    int hostile_cap = (int)((float)HOSTILE_CAP * scale);
    int passive_cap = (int)((float)PASSIVE_CAP * scale);

    int hostiles = entity_count_hostile(es);
    int passives = count_passive(es);

    uint32_t seed = world_seed(world);

    for (int attempt = 0; attempt < SPAWN_CANDIDATES; ++attempt) {
        if (hostiles >= hostile_cap && passives >= passive_cap) break;

        float ang = rng_range(&es->rng, 0.0f, 6.2831853f);
        float r = rng_range(&es->rng, SPAWN_RING_MIN, SPAWN_RING_MAX);
        int wx = (int)floorf(player_pos.x + cosf(ang) * r);
        int wz = (int)floorf(player_pos.z + sinf(ang) * r);
        int sy = world_height_at(seed, wx, wz);
        if (sy <= 0) continue;
        int gy = sy + 1;

        block_t ground = world_get_block(world, wx, sy, wz);
        if (ground == BLOCK_AIR || block_is_water(ground)) continue;

        if (world_get_block(world, wx, gy, wz) != BLOCK_AIR) continue;

        vec3 spos = { (float)wx + 0.5f, (float)gy, (float)wz + 0.5f };

        if (hostiles < hostile_cap) {
            int hy = -1, sky_occluded = 0;
            if (is_night) {
                hy = gy;
            } else {
                int depth = rng_int(&es->rng, 6, 50);
                int cy = sy - depth;
                if (cy > 2) {
                    block_t fl = world_get_block(world, wx, cy, wz);
                    block_t a1 = world_get_block(world, wx, cy + 1, wz);
                    block_t a2 = world_get_block(world, wx, cy + 2, wz);
                    if (fl != BLOCK_AIR && !block_is_water(fl) && a1 == BLOCK_AIR && a2 == BLOCK_AIR) {
                        hy = cy + 1; sky_occluded = 1;
                    }
                }
            }
            if (hy > 0 && world_block_light_at(world, wx, hy, wz) < 8 && (is_night || sky_occluded)) {
                vec3 hsp = { (float)wx + 0.5f, (float)hy, (float)wz + 0.5f };
                int sp = rng_int(&es->rng, (int)SPECIES_FIRST_HOSTILE, (int)SP_COUNT - 1);
                const mob_def_t *d = &MOB_DEFS[sp];
                if (!aabb_overlaps_solid(hsp, d->w * 0.5f, d->h, world)) {
                    entity_spawn_mob(es, sp, hsp);
                    ++hostiles;
                }
            }
        }
        if (!is_night && passives < passive_cap) {
            if (ground != BLOCK_GRASS) continue;
            int sp = rng_int(&es->rng, 0, (int)SPECIES_FIRST_HOSTILE - 1);
            const mob_def_t *d = &MOB_DEFS[sp];
            int herd = rng_int(&es->rng, d->herd_min > 0 ? d->herd_min : 2,
                               d->herd_max > 0 ? d->herd_max : 4);
            for (int h = 0; h < herd && passives < passive_cap; ++h) {
                float ox = rng_range(&es->rng, -2.0f, 2.0f);
                float oz = rng_range(&es->rng, -2.0f, 2.0f);
                int hx = (int)floorf(spos.x + ox);
                int hz = (int)floorf(spos.z + oz);
                int hsy = world_height_at(seed, hx, hz);
                if (world_get_block(world, hx, hsy, hz) != BLOCK_GRASS) continue;
                vec3 hp = { (float)hx + 0.5f, (float)(hsy + 1), (float)hz + 0.5f };
                if (!aabb_overlaps_solid(hp, d->w * 0.5f, d->h, world)) {
                    entity_spawn_mob(es, sp, hp);
                    ++passives;
                }
            }
        }
    }
}

static int write_u8(FILE *f, uint8_t v)  { return fputc((int)v, f) != EOF; }
static int write_u16(FILE *f, uint16_t v) {
    return write_u8(f, (uint8_t)(v & 0xFFu)) && write_u8(f, (uint8_t)((v >> 8) & 0xFFu));
}
static int write_u32(FILE *f, uint32_t v) {
    return write_u16(f, (uint16_t)(v & 0xFFFFu)) && write_u16(f, (uint16_t)((v >> 16) & 0xFFFFu));
}
static int write_i16(FILE *f, int16_t v)  { return write_u16(f, (uint16_t)v); }
static int write_f32(FILE *f, float v) {
    uint32_t u; memcpy(&u, &v, sizeof u); return write_u32(f, u);
}

static int read_u8(FILE *f, uint8_t *v)  { int c = fgetc(f); if (c == EOF) return 0; *v = (uint8_t)c; return 1; }
static int read_u16(FILE *f, uint16_t *v) {
    uint8_t a, b;
    if (!read_u8(f, &a) || !read_u8(f, &b)) return 0;
    *v = (uint16_t)((uint16_t)a | ((uint16_t)b << 8));
    return 1;
}
static int read_u32(FILE *f, uint32_t *v) {
    uint16_t a, b;
    if (!read_u16(f, &a) || !read_u16(f, &b)) return 0;
    *v = (uint32_t)a | ((uint32_t)b << 16);
    return 1;
}
static int read_i16(FILE *f, int16_t *v)  { uint16_t u; if (!read_u16(f, &u)) return 0; *v = (int16_t)u; return 1; }
static int read_f32(FILE *f, float *v) {
    uint32_t u; if (!read_u32(f, &u)) return 0; memcpy(v, &u, sizeof *v); return 1;
}

#define ENTITY_SAVE_CAP 512

int entity_save(void *fp) {
    FILE *f = (FILE *)fp;
    entity_system_t *es = g_es;
    if (!f) return 0;
    if (!es) { return write_u32(f, 0u); }

    int idx[MAX_ENTITIES];
    int n = 0;
    for (int i = 0; i < es->count; ++i) {
        entity_t *e = &es->e[i];
        if (!e->active) continue;
        if (e->kind == EK_MOB && e->ai == AI_DEAD) continue;
        if (e->kind == EK_MOB || e->kind == EK_ITEM) idx[n++] = i;
    }

    if (n > ENTITY_SAVE_CAP) {
        for (int a = 1; a < n; ++a) {
            int key = idx[a];
            float ka = es->e[key].age;
            int b = a - 1;
            while (b >= 0 && es->e[idx[b]].age < ka) { idx[b + 1] = idx[b]; --b; }
            idx[b + 1] = key;
        }
        fprintf(stderr, "[entity] save overflow: %d persistable, capping to %d\n", n, ENTITY_SAVE_CAP);
        n = ENTITY_SAVE_CAP;
    }

    if (!write_u32(f, (uint32_t)n)) return 0;
    for (int k = 0; k < n; ++k) {
        entity_t *e = &es->e[idx[k]];
        int16_t hp = (int16_t)((e->health > 32767.0f) ? 32767.0f :
                               (e->health < -32768.0f ? -32768.0f : e->health));
        if (!write_u8(f, (uint8_t)e->kind)) return 0;
        if (!write_u8(f, (uint8_t)e->species)) return 0;
        if (!write_f32(f, e->pos.x) || !write_f32(f, e->pos.y) || !write_f32(f, e->pos.z)) return 0;
        if (!write_f32(f, e->vel.x) || !write_f32(f, e->vel.y) || !write_f32(f, e->vel.z)) return 0;
        if (!write_f32(f, e->yaw)) return 0;
        if (!write_i16(f, hp)) return 0;
        if (e->kind == EK_MOB) {
            if (!write_u8(f, (uint8_t)e->ai)) return 0;
            if (!write_u8(f, 0u)) return 0;
        } else {
            if (!write_u8(f, 0u)) return 0;
            if (!write_u8(f, e->item_count)) return 0;
            if (!write_u16(f, e->item_id_payload)) return 0;
            if (!write_u16(f, e->item_durability)) return 0;
        }
    }
    return 1;
}

int entity_load(void *fp, unsigned version) {
    FILE *f = (FILE *)fp;
    entity_system_t *es = g_es;
    (void)version;
    if (!f) return 0;

    uint32_t n = 0;
    if (!read_u32(f, &n)) return 0;

    for (uint32_t k = 0; k < n; ++k) {
        uint8_t kind = 0, species = 0;
        float px, py, pz, vx, vy, vz, yaw;
        int16_t hp = 0;
        if (!read_u8(f, &kind) || !read_u8(f, &species)) return 0;
        if (!read_f32(f, &px) || !read_f32(f, &py) || !read_f32(f, &pz)) return 0;
        if (!read_f32(f, &vx) || !read_f32(f, &vy) || !read_f32(f, &vz)) return 0;
        if (!read_f32(f, &yaw)) return 0;
        if (!read_i16(f, &hp)) return 0;

        if (kind == EK_MOB) {
            uint8_t ai = 0, flags = 0;
            if (!read_u8(f, &ai) || !read_u8(f, &flags)) return 0;
            (void)flags;
            if (es && species < SP_COUNT) {
                entity_spawn_mob(es, (int)species, (vec3){ px, py, pz });
                for (int i = es->count - 1; i >= 0; --i) {
                    entity_t *e = &es->e[i];
                    if (e->active && e->kind == EK_MOB && e->pos.x == px && e->pos.y == py && e->pos.z == pz) {
                        e->vel = (vec3){ vx, vy, vz };
                        e->yaw = yaw;
                        e->head_yaw = yaw;
                        e->health = (float)hp;
                        if (ai < AI_DEAD) e->ai = (ai_state_t)ai; else e->ai = AI_IDLE;
                        break;
                    }
                }
            }
        } else if (kind == EK_ITEM) {
            uint8_t z0 = 0, count = 0;
            uint16_t id_payload = 0, dur = 0;
            if (!read_u8(f, &z0) || !read_u8(f, &count)) return 0;
            if (!read_u16(f, &id_payload) || !read_u16(f, &dur)) return 0;
            (void)z0;
            if (es && count > 0 && id_payload != 0) {
                entity_spawn_item(es, (vec3){ px, py, pz }, id_payload, (int)count, dur);
                for (int i = es->count - 1; i >= 0; --i) {
                    entity_t *e = &es->e[i];
                    if (e->active && e->kind == EK_ITEM && e->pos.x == px && e->pos.y == py && e->pos.z == pz) {
                        e->vel = (vec3){ vx, vy, vz };
                        break;
                    }
                }
            }
        } else {
            return 0;
        }
    }
    return 1;
}
