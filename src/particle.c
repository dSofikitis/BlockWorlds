#include "glcompat.h"

#include "particle.h"
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FX_ATLAS_TILES 8
#define FX_TILE_UV     (1.0f / 8.0f)
#define FX_ROW         6
#define GRAVITY        20.0f

#define TILE_SHARD   0
#define TILE_DUST    1
#define TILE_SPLASH  2
#define TILE_CRIT    3
#define TILE_SMOKE   4
#define TILE_FLAME   5
#define TILE_BUBBLE  6
#define TILE_STAR    7

typedef struct {
    vec3   pos;
    vec3   vel;
    float  life;
    float  max_life;
    float  size0, size1;
    float  cr, cg, cb;
    float  a0;
    float  gravity_scale;
    float  drag;
    uint8_t tile;
    uint8_t kind;
    uint8_t additive;
    uint8_t collide;
} particle_t;

#define FLOATS_PER_VERT  9
#define VERTS_PER_QUAD   6
#define FLOATS_PER_QUAD  (FLOATS_PER_VERT * VERTS_PER_QUAD)

struct particle_system_s {
    particle_t parts[PARTICLE_MAX];
    int        count;

    GLuint prog;
    GLint  mvp_loc, atlas_loc;
    GLuint vao, vbo;

    GLuint atlas_tex;

    float  verts[PARTICLE_MAX * FLOATS_PER_QUAD];
    uint32_t rng;
};

static uint32_t lcg_next(particle_system_t *ps) {
    ps->rng = ps->rng * 1664525u + 1013904223u;
    return ps->rng;
}

static float frand(particle_system_t *ps) {
    return (float)(lcg_next(ps) >> 8) * (1.0f / 16777216.0f);
}

static float srand_unit(particle_system_t *ps) {
    return frand(ps) * 2.0f - 1.0f;
}

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "particle: failed to open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    buf[r] = '\0';
    fclose(f);
    return buf;
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "particle shader compile: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint load_program(const char *vp, const char *fp) {
    char *vs = read_text(vp);
    char *fs = read_text(fp);
    if (!vs || !fs) { free(vs); free(fs); return 0; }
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    free(vs); free(fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v); glDeleteShader(f);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

particle_system_t *particle_create(void) {
    particle_system_t *ps = calloc(1, sizeof *ps);
    if (!ps) return NULL;

    ps->rng = 0x9e3779b9u;

    ps->prog = load_program("shaders/particle.vert", "shaders/particle.frag");
    if (!ps->prog) { free(ps); return NULL; }
    ps->mvp_loc   = glGetUniformLocation(ps->prog, "u_mvp");
    ps->atlas_loc = glGetUniformLocation(ps->prog, "u_atlas");

    glGenVertexArrays(1, &ps->vao);
    glGenBuffers(1, &ps->vbo);
    glBindVertexArray(ps->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ps->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof ps->verts, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          FLOATS_PER_VERT * (GLsizei)sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          FLOATS_PER_VERT * (GLsizei)sizeof(float),
                          (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                          FLOATS_PER_VERT * (GLsizei)sizeof(float),
                          (void *)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    return ps;
}

void particle_destroy(particle_system_t *ps) {
    if (!ps) return;
    glDeleteVertexArrays(1, &ps->vao);
    glDeleteBuffers(1, &ps->vbo);
    glDeleteProgram(ps->prog);
    free(ps);
}

void particle_set_atlas(particle_system_t *ps, unsigned int hud_atlas_tex) {
    if (ps) ps->atlas_tex = (GLuint)hud_atlas_tex;
}

int particle_active_count(const particle_system_t *ps) {
    if (!ps) return 0;
    int n = 0;
    for (int i = 0; i < ps->count; i++) if (ps->parts[i].life > 0.0f) n++;
    return n;
}

static particle_t *alloc_part(particle_system_t *ps) {
    for (int i = 0; i < PARTICLE_MAX; i++) {
        if (ps->parts[i].life <= 0.0f) {
            if (i >= ps->count) ps->count = i + 1;
            return &ps->parts[i];
        }
    }
    return NULL;
}

static void emit(particle_system_t *ps,
                 vec3 pos, vec3 vel,
                 float life, float size0, float size1,
                 float r, float g, float b, float a0,
                 float gravity_scale, float drag,
                 uint8_t tile, uint8_t kind,
                 uint8_t additive, uint8_t collide) {
    particle_t *p = alloc_part(ps);
    if (!p) return;
    p->pos = pos;
    p->vel = vel;
    p->life = life;
    p->max_life = life;
    p->size0 = size0;
    p->size1 = size1;
    p->cr = r; p->cg = g; p->cb = b;
    p->a0 = a0;
    p->gravity_scale = gravity_scale;
    p->drag = drag;
    p->tile = tile;
    p->kind = kind;
    p->additive = additive;
    p->collide = collide;
}

static int is_solid(const world_t *world, float x, float y, float z) {
    block_t b = world_get_block(world, (int)floorf(x), (int)floorf(y), (int)floorf(z));
    return b != BLOCK_AIR && !block_is_water(b);
}

void particle_update(particle_system_t *ps, const world_t *world, float dt) {
    if (!ps) return;

    for (int i = 0; i < ps->count; i++) {
        particle_t *p = &ps->parts[i];
        if (p->life <= 0.0f) continue;

        p->life -= dt;
        if (p->life <= 0.0f) { p->life = 0.0f; continue; }

        p->vel.y -= GRAVITY * p->gravity_scale * dt;
        float damp = 1.0f - p->drag * dt;
        if (damp < 0.0f) damp = 0.0f;
        p->vel.x *= damp;
        p->vel.y *= damp;
        p->vel.z *= damp;

        vec3 next = {
            p->pos.x + p->vel.x * dt,
            p->pos.y + p->vel.y * dt,
            p->pos.z + p->vel.z * dt,
        };

        if (p->collide && world) {
            if (is_solid(world, next.x, p->pos.y, p->pos.z)) {
                p->vel.x = 0.0f;
                next.x = p->pos.x;
            }
            if (is_solid(world, p->pos.x, p->pos.y, next.z)) {
                p->vel.z = 0.0f;
                next.z = p->pos.z;
            }
            if (is_solid(world, p->pos.x, next.y, p->pos.z)) {
                if (p->vel.y < 0.0f) {
                    p->vel.y = 0.0f;
                    p->vel.x *= 0.5f;
                    p->vel.z *= 0.5f;
                }
                next.y = p->pos.y;
            }
        }

        p->pos = next;
    }

    int hi = 0;
    for (int i = 0; i < ps->count; i++) if (ps->parts[i].life > 0.0f) hi = i + 1;
    ps->count = hi;
}

static void push_vert(float *v, int *vc, vec3 p, float u, float w,
                      float r, float g, float b, float a) {
    int base = *vc * FLOATS_PER_VERT;
    v[base + 0] = p.x;
    v[base + 1] = p.y;
    v[base + 2] = p.z;
    v[base + 3] = u;
    v[base + 4] = w;
    v[base + 5] = r;
    v[base + 6] = g;
    v[base + 7] = b;
    v[base + 8] = a;
    (*vc)++;
}

static int build_quad(const particle_t *p, float *v, int *vc,
                      vec3 cam_right, vec3 cam_up, float light) {
    float t = (p->max_life > 0.0f) ? (1.0f - p->life / p->max_life) : 1.0f;
    float size = p->size0 + (p->size1 - p->size0) * t;
    float half = size * 0.5f;

    float fade = (t < 0.7f) ? 1.0f : (1.0f - (t - 0.7f) / 0.3f);
    if (fade < 0.0f) fade = 0.0f;
    float a = p->a0 * fade;

    float r = p->cr * light;
    float g = p->cg * light;
    float b = p->cb * light;

    vec3 rx = vec3_scale(cam_right, half);
    vec3 uy = vec3_scale(cam_up, half);

    vec3 bl = { p->pos.x - rx.x - uy.x, p->pos.y - rx.y - uy.y, p->pos.z - rx.z - uy.z };
    vec3 br = { p->pos.x + rx.x - uy.x, p->pos.y + rx.y - uy.y, p->pos.z + rx.z - uy.z };
    vec3 tr = { p->pos.x + rx.x + uy.x, p->pos.y + rx.y + uy.y, p->pos.z + rx.z + uy.z };
    vec3 tl = { p->pos.x - rx.x + uy.x, p->pos.y - rx.y + uy.y, p->pos.z - rx.z + uy.z };

    float u0 = (float)p->tile * FX_TILE_UV;
    float u1 = u0 + FX_TILE_UV;
    float w0 = (float)FX_ROW * FX_TILE_UV;
    float w1 = w0 + FX_TILE_UV;

    push_vert(v, vc, bl, u0, w1, r, g, b, a);
    push_vert(v, vc, br, u1, w1, r, g, b, a);
    push_vert(v, vc, tr, u1, w0, r, g, b, a);
    push_vert(v, vc, bl, u0, w1, r, g, b, a);
    push_vert(v, vc, tr, u1, w0, r, g, b, a);
    push_vert(v, vc, tl, u0, w0, r, g, b, a);
    return VERTS_PER_QUAD;
}

void particle_render(const particle_system_t *ps, mat4 pv, vec3 cam_right, vec3 cam_up,
                     vec3 sun_dir, float sun_strength, float ambient) {
    if (!ps || ps->count == 0 || ps->atlas_tex == 0) return;

    float sun_up = (sun_dir.y > 0.0f) ? sun_dir.y : 0.0f;
    float light = ambient + sun_strength * sun_up;
    if (light < 0.25f) light = 0.25f;
    if (light > 1.0f)  light = 1.0f;

    float *v = (float *)ps->verts;
    int normal_vc = 0;
    int additive_vc = 0;

    for (int i = 0; i < ps->count; i++) {
        const particle_t *p = &ps->parts[i];
        if (p->life <= 0.0f || p->additive) continue;
        build_quad(p, v, &normal_vc, cam_right, cam_up, light);
    }
    int additive_start = normal_vc;
    for (int i = 0; i < ps->count; i++) {
        const particle_t *p = &ps->parts[i];
        if (p->life <= 0.0f || !p->additive) continue;
        build_quad(p, v, &additive_vc, cam_right, cam_up, 1.0f);
    }
    int total_vc = additive_start + additive_vc;
    if (total_vc == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, ps->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)((size_t)total_vc * FLOATS_PER_VERT * sizeof(float)), v);

    glUseProgram(ps->prog);
    glUniformMatrix4fv(ps->mvp_loc, 1, GL_FALSE, pv.m);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ps->atlas_tex);
    glUniform1i(ps->atlas_loc, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    glBindVertexArray(ps->vao);

    if (normal_vc > 0) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, normal_vc);
    }
    if (additive_vc > 0) {
        glBlendFunc(GL_ONE, GL_ONE);
        glDrawArrays(GL_TRIANGLES, additive_start, additive_vc);
    }

    glBindVertexArray(0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

void particle_block_break(particle_system_t *ps, int bx, int by, int bz, block_t b) {
    if (!ps) return;
    float r, g, bl;
    block_avg_color(b, &r, &g, &bl);
    int n = 12 + (int)(lcg_next(ps) % 7u);
    for (int i = 0; i < n; i++) {
        vec3 pos = {
            (float)bx + 0.2f + frand(ps) * 0.6f,
            (float)by + 0.2f + frand(ps) * 0.6f,
            (float)bz + 0.2f + frand(ps) * 0.6f,
        };
        vec3 vel = {
            srand_unit(ps) * 2.2f,
            1.0f + frand(ps) * 3.0f,
            srand_unit(ps) * 2.2f,
        };
        float life = 0.6f + frand(ps) * 0.4f;
        emit(ps, pos, vel, life, 0.16f, 0.10f,
             r, g, bl, 1.0f, 1.0f, 0.4f,
             TILE_SHARD, PK_SHARD, 0, 1);
    }
}

void particle_block_hit(particle_system_t *ps, int bx, int by, int bz,
                        int fx, int fy, int fz, block_t b) {
    if (!ps) return;
    float r, g, bl;
    block_avg_color(b, &r, &g, &bl);
    vec3 face = {
        (float)bx + 0.5f + (float)fx * 0.55f,
        (float)by + 0.5f + (float)fy * 0.55f,
        (float)bz + 0.5f + (float)fz * 0.55f,
    };
    int n = 3 + (int)(lcg_next(ps) % 3u);
    for (int i = 0; i < n; i++) {
        vec3 pos = {
            face.x + srand_unit(ps) * 0.25f,
            face.y + srand_unit(ps) * 0.25f,
            face.z + srand_unit(ps) * 0.25f,
        };
        vec3 vel = {
            (float)fx * 1.5f + srand_unit(ps) * 0.8f,
            (float)fy * 1.5f + frand(ps) * 1.2f,
            (float)fz * 1.5f + srand_unit(ps) * 0.8f,
        };
        float life = 0.3f + frand(ps) * 0.3f;
        emit(ps, pos, vel, life, 0.11f, 0.06f,
             r, g, bl, 1.0f, 1.0f, 0.6f,
             TILE_SHARD, PK_SHARD, 0, 1);
    }
}

void particle_footstep(particle_system_t *ps, float x, float y, float z, block_t ground) {
    if (!ps) return;
    float r, g, bl;
    block_avg_color(ground, &r, &g, &bl);
    int n = 2 + (int)(lcg_next(ps) % 3u);
    for (int i = 0; i < n; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.2f, y + 0.05f, z + srand_unit(ps) * 0.2f };
        vec3 vel = { srand_unit(ps) * 0.5f, 0.3f + frand(ps) * 0.4f, srand_unit(ps) * 0.5f };
        float life = 0.3f + frand(ps) * 0.25f;
        emit(ps, pos, vel, life, 0.14f, 0.30f,
             r, g, bl, 0.6f, 0.25f, 1.6f,
             TILE_DUST, PK_DUST, 0, 0);
    }
}

void particle_land_dust(particle_system_t *ps, float x, float y, float z,
                        block_t ground, float strength) {
    if (!ps) return;
    float r, g, bl;
    block_avg_color(ground, &r, &g, &bl);
    if (strength < 0.1f) strength = 0.1f;
    if (strength > 3.0f) strength = 3.0f;
    int n = 6 + (int)(strength * 6.0f);
    for (int i = 0; i < n; i++) {
        float ang = frand(ps) * 6.2831853f;
        float spd = (0.8f + frand(ps) * 1.2f) * strength;
        vec3 pos = { x + cosf(ang) * 0.2f, y + 0.05f, z + sinf(ang) * 0.2f };
        vec3 vel = { cosf(ang) * spd, 0.2f + frand(ps) * 0.4f, sinf(ang) * spd };
        float life = 0.35f + frand(ps) * 0.35f;
        emit(ps, pos, vel, life, 0.16f, 0.40f * strength,
             r, g, bl, 0.7f, 0.2f, 1.8f,
             TILE_DUST, PK_DUST, 0, 0);
    }
}

void particle_water_splash(particle_system_t *ps, float x, float y, float z) {
    if (!ps) return;
    for (int i = 0; i < 10; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.25f, y + 0.05f, z + srand_unit(ps) * 0.25f };
        vec3 vel = { srand_unit(ps) * 1.6f, 2.0f + frand(ps) * 2.5f, srand_unit(ps) * 1.6f };
        float life = 0.4f + frand(ps) * 0.4f;
        emit(ps, pos, vel, life, 0.12f, 0.08f,
             0.55f, 0.70f, 0.95f, 0.9f, 1.0f, 0.2f,
             TILE_SPLASH, PK_SPLASH, 0, 0);
    }
}

void particle_eat(particle_system_t *ps, float x, float y, float z, item_id food) {
    if (!ps) return;
    float r = 0.55f, g = 0.40f, bl = 0.22f;
    if (item_is_block(food)) block_avg_color((block_t)food, &r, &g, &bl);
    int n = 4 + (int)(lcg_next(ps) % 3u);
    for (int i = 0; i < n; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.12f, y, z + srand_unit(ps) * 0.12f };
        vec3 vel = { srand_unit(ps) * 0.8f, -0.6f - frand(ps) * 0.8f, srand_unit(ps) * 0.8f };
        float life = 0.4f + frand(ps) * 0.3f;
        emit(ps, pos, vel, life, 0.10f, 0.06f,
             r, g, bl, 1.0f, 1.0f, 0.3f,
             TILE_SHARD, PK_CRUMB, 0, 0);
    }
}

void particle_crit(particle_system_t *ps, float x, float y, float z) {
    if (!ps) return;
    for (int i = 0; i < 6; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.3f, y + frand(ps) * 0.6f, z + srand_unit(ps) * 0.3f };
        vec3 vel = { srand_unit(ps) * 0.6f, 0.8f + frand(ps) * 0.8f, srand_unit(ps) * 0.6f };
        float life = 0.4f + frand(ps) * 0.3f;
        emit(ps, pos, vel, life, 0.14f, 0.05f,
             1.0f, 0.95f, 0.55f, 1.0f, 0.1f, 0.6f,
             TILE_CRIT, PK_CRIT, 1, 0);
    }
}

void particle_hit(particle_system_t *ps, float x, float y, float z) {
    if (!ps) return;
    for (int i = 0; i < 6; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.25f, y + srand_unit(ps) * 0.3f, z + srand_unit(ps) * 0.25f };
        vec3 vel = { srand_unit(ps) * 1.4f, 0.6f + frand(ps) * 1.0f, srand_unit(ps) * 1.4f };
        float life = 0.3f + frand(ps) * 0.25f;
        emit(ps, pos, vel, life, 0.13f, 0.07f,
             0.85f, 0.18f, 0.16f, 0.95f, 0.6f, 0.8f,
             TILE_DUST, PK_HIT, 0, 0);
    }
}

void particle_mob_death(particle_system_t *ps, float x, float y, float z) {
    if (!ps) return;
    for (int i = 0; i < 14; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.35f, y + 0.3f + frand(ps) * 0.6f, z + srand_unit(ps) * 0.35f };
        vec3 vel = { srand_unit(ps) * 0.6f, 0.3f + frand(ps) * 0.5f, srand_unit(ps) * 0.6f };
        float life = 0.6f + frand(ps) * 0.5f;
        emit(ps, pos, vel, life, 0.22f, 0.55f,
             0.85f, 0.85f, 0.88f, 0.8f, -0.05f, 0.6f,
             TILE_SMOKE, PK_POOF, 0, 0);
    }
}

void particle_explosion(particle_system_t *ps, float x, float y, float z, float radius) {
    if (!ps) return;
    if (radius < 0.5f) radius = 0.5f;
    int flashes = 3 + (int)(radius * 2.0f);
    for (int i = 0; i < flashes; i++) {
        vec3 pos = { x + srand_unit(ps) * radius * 0.3f,
                     y + srand_unit(ps) * radius * 0.3f,
                     z + srand_unit(ps) * radius * 0.3f };
        vec3 vel = { srand_unit(ps) * 1.0f, frand(ps) * 1.0f, srand_unit(ps) * 1.0f };
        float life = 0.15f + frand(ps) * 0.15f;
        emit(ps, pos, vel, life, radius * 0.9f, radius * 0.3f,
             1.0f, 0.9f, 0.55f, 1.0f, 0.0f, 1.0f,
             TILE_FLAME, PK_FLASH, 1, 0);
    }
    int smoke = 16 + (int)(radius * 10.0f);
    for (int i = 0; i < smoke; i++) {
        float ang = frand(ps) * 6.2831853f;
        float spd = (1.0f + frand(ps) * 2.5f) * radius;
        float vy  = 0.5f + frand(ps) * 2.0f;
        vec3 pos = { x + srand_unit(ps) * radius * 0.4f,
                     y + srand_unit(ps) * radius * 0.4f,
                     z + srand_unit(ps) * radius * 0.4f };
        vec3 vel = { cosf(ang) * spd, vy, sinf(ang) * spd };
        float life = 0.8f + frand(ps) * 0.8f;
        float gray = 0.25f + frand(ps) * 0.25f;
        emit(ps, pos, vel, life, radius * 0.5f, radius * 1.4f,
             gray, gray, gray, 0.85f, 0.05f, 0.8f,
             TILE_SMOKE, PK_SMOKE, 0, 0);
    }
}

void particle_xp_sparkle(particle_system_t *ps, float x, float y, float z) {
    if (!ps) return;
    int n = 2 + (int)(lcg_next(ps) % 2u);
    for (int i = 0; i < n; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.3f, y + frand(ps) * 0.6f, z + srand_unit(ps) * 0.3f };
        vec3 vel = { srand_unit(ps) * 0.4f, 0.4f + frand(ps) * 0.6f, srand_unit(ps) * 0.4f };
        float life = 0.5f + frand(ps) * 0.4f;
        emit(ps, pos, vel, life, 0.12f, 0.04f,
             0.45f, 1.0f, 0.40f, 1.0f, 0.0f, 0.5f,
             TILE_STAR, PK_XP, 1, 0);
    }
}

void particle_torch_flame(particle_system_t *ps, float x, float y, float z) {
    if (!ps) return;
    int n = 1 + (int)(lcg_next(ps) % 2u);
    for (int i = 0; i < n; i++) {
        vec3 pos = { x + srand_unit(ps) * 0.06f, y + frand(ps) * 0.1f, z + srand_unit(ps) * 0.06f };
        vec3 vel = { srand_unit(ps) * 0.15f, 0.4f + frand(ps) * 0.3f, srand_unit(ps) * 0.15f };
        float life = 0.4f + frand(ps) * 0.3f;
        emit(ps, pos, vel, life, 0.14f, 0.04f,
             1.0f, 0.55f, 0.15f, 1.0f, -0.05f, 0.4f,
             TILE_FLAME, PK_FLAME, 1, 0);
    }
}
