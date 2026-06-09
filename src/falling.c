#include "glcompat.h"

#include "falling.h"
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_FALLING   512
#define GRAVITY        18.0f
#define TERMINAL_VEL   10.0f

typedef struct {
    float x, y, z;
    float vel_y;
    block_t type;
    int active;
} falling_block_t;

struct falling_system_s {
    falling_block_t entities[MAX_FALLING];
    int count;

    GLuint prog;
    GLint  mvp_loc, sun_dir_loc, sun_strength_loc, ambient_loc, color_loc;
    GLuint vao, vbo;
    int    vert_count;
};

static void block_color(block_t b, float *r, float *g, float *bl) {
    switch (b) {
        case BLOCK_SAND:   *r = 0.86f; *g = 0.78f; *bl = 0.52f; break;
        case BLOCK_GRAVEL: *r = 0.55f; *g = 0.55f; *bl = 0.50f; break;
        case BLOCK_DIRT:   *r = 0.55f; *g = 0.39f; *bl = 0.20f; break;
        default:           *r = 0.80f; *g = 0.80f; *bl = 0.80f; break;
    }
}

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "falling: failed to open %s\n", path); return NULL; }
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
        fprintf(stderr, "falling shader compile: %s\n", log);
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

static const float CUBE_VERTS[] = {
    1,0,0,  1,0,0,
    1,1,0,  1,0,0,
    1,1,1,  1,0,0,
    1,0,0,  1,0,0,
    1,1,1,  1,0,0,
    1,0,1,  1,0,0,
    0,0,0, -1,0,0,
    0,0,1, -1,0,0,
    0,1,1, -1,0,0,
    0,0,0, -1,0,0,
    0,1,1, -1,0,0,
    0,1,0, -1,0,0,
    0,1,0,  0,1,0,
    1,1,0,  0,1,0,
    1,1,1,  0,1,0,
    0,1,0,  0,1,0,
    1,1,1,  0,1,0,
    0,1,1,  0,1,0,
    0,0,0,  0,-1,0,
    1,0,0,  0,-1,0,
    1,0,1,  0,-1,0,
    0,0,0,  0,-1,0,
    1,0,1,  0,-1,0,
    0,0,1,  0,-1,0,
    0,0,1,  0,0,1,
    1,0,1,  0,0,1,
    1,1,1,  0,0,1,
    0,0,1,  0,0,1,
    1,1,1,  0,0,1,
    0,1,1,  0,0,1,
    0,0,0,  0,0,-1,
    0,1,0,  0,0,-1,
    1,1,0,  0,0,-1,
    0,0,0,  0,0,-1,
    1,1,0,  0,0,-1,
    1,0,0,  0,0,-1,
};

falling_system_t *falling_create(void) {
    falling_system_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;

    s->prog = load_program("shaders/falling.vert", "shaders/falling.frag");
    if (!s->prog) { free(s); return NULL; }
    s->mvp_loc          = glGetUniformLocation(s->prog, "u_mvp");
    s->sun_dir_loc      = glGetUniformLocation(s->prog, "u_sun_dir");
    s->sun_strength_loc = glGetUniformLocation(s->prog, "u_sun_strength");
    s->ambient_loc      = glGetUniformLocation(s->prog, "u_ambient");
    s->color_loc        = glGetUniformLocation(s->prog, "u_color");

    glGenVertexArrays(1, &s->vao);
    glGenBuffers(1, &s->vbo);
    glBindVertexArray(s->vao);
    glBindBuffer(GL_ARRAY_BUFFER, s->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof CUBE_VERTS, CUBE_VERTS, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    s->vert_count = (int)(sizeof CUBE_VERTS / (sizeof(float) * 6));
    return s;
}

void falling_destroy(falling_system_t *s) {
    if (!s) return;
    glDeleteVertexArrays(1, &s->vao);
    glDeleteBuffers(1, &s->vbo);
    glDeleteProgram(s->prog);
    free(s);
}

void falling_spawn(falling_system_t *s, int x, int y, int z, block_t type) {
    for (int i = 0; i < MAX_FALLING; i++) {
        if (!s->entities[i].active) {
            s->entities[i] = (falling_block_t){
                .x = (float)x, .y = (float)y, .z = (float)z,
                .vel_y = 0.0f, .type = type, .active = 1,
            };
            if (i >= s->count) s->count = i + 1;
            return;
        }
    }
}

int falling_active_count(const falling_system_t *s) {
    int n = 0;
    for (int i = 0; i < s->count; i++) if (s->entities[i].active) n++;
    return n;
}

static int is_passable_for_falling(block_t b) {
    return b == BLOCK_AIR || block_is_water(b);
}

int falling_update(falling_system_t *s, world_t *world, float dt) {
    int order[MAX_FALLING];
    int n_ordered = 0;
    int landings = 0;
    for (int i = 0; i < s->count; i++) if (s->entities[i].active) order[n_ordered++] = i;
    for (int i = 1; i < n_ordered; i++) {
        int key = order[i];
        float ky = s->entities[key].y;
        int j = i - 1;
        while (j >= 0 && s->entities[order[j]].y > ky) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    for (int oi = 0; oi < n_ordered; oi++) {
        falling_block_t *fb = &s->entities[order[oi]];
        if (!fb->active) continue;

        fb->vel_y -= GRAVITY * dt;
        if (fb->vel_y < -TERMINAL_VEL) fb->vel_y = -TERMINAL_VEL;
        float new_y = fb->y + fb->vel_y * dt;

        int ix = (int)floorf(fb->x);
        int iz = (int)floorf(fb->z);

        int landed = 0;
        int current_int = (int)floorf(fb->y);
        int new_int     = (int)floorf(new_y);
        for (int yy = current_int - 1; yy >= new_int - 1 && yy >= 0; yy--) {
            block_t b = world_get_block(world, ix, yy, iz);
            if (is_passable_for_falling(b)) continue;
            float top_of_block = (float)(yy + 1);
            if (new_y > top_of_block) continue;
            fb->y = top_of_block;
            world_set_block(world, ix, yy + 1, iz, fb->type);
            fb->active = 0;
            landed = 1;
            landings++;
            break;
        }
        if (!landed) {
            fb->y = new_y;
            if (fb->y < 0.0f) {
                fb->active = 0;
            }
        }
    }

    int hi = 0;
    for (int i = 0; i < s->count; i++) if (s->entities[i].active) hi = i + 1;
    s->count = hi;
    return landings;
}

void falling_render(const falling_system_t *s, mat4 pv,
                    vec3 sun_dir, float sun_strength, float ambient) {
    if (!s) return;
    glUseProgram(s->prog);
    glUniform3f(s->sun_dir_loc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform1f(s->sun_strength_loc, sun_strength);
    glUniform1f(s->ambient_loc, ambient);
    glBindVertexArray(s->vao);
    for (int i = 0; i < s->count; i++) {
        const falling_block_t *fb = &s->entities[i];
        if (!fb->active) continue;
        mat4 model = mat4_translate((vec3){fb->x, fb->y, fb->z});
        mat4 mvp = mat4_multiply(pv, model);
        glUniformMatrix4fv(s->mvp_loc, 1, GL_FALSE, mvp.m);
        float r, g, b;
        block_color(fb->type, &r, &g, &b);
        glUniform3f(s->color_loc, r, g, b);
        glDrawArrays(GL_TRIANGLES, 0, s->vert_count);
    }
    glBindVertexArray(0);
}
