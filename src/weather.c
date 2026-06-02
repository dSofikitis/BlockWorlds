#include "glcompat.h"

#include "weather.h"
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PARTICLES 4000

#define BOX_XZ   22.0f
#define BOX_UP   15.0f
#define BOX_DOWN 12.0f
#define BOX_SPAN (BOX_UP + BOX_DOWN)

typedef struct {
    float x, y, z;
    float phase;
} particle_t;

struct weather_s {
    particle_t parts[MAX_PARTICLES];
    int        mode;
    float      time;
    float      live;

    GLuint prog;
    GLint  mvp_loc, color_loc, point_size_loc, round_loc;
    GLuint vao, vbo;

    float  verts[MAX_PARTICLES * 2 * 4];
    int    vert_count;
};

static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "weather: failed to open %s\n", path); return NULL; }
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
        fprintf(stderr, "weather shader compile: %s\n", log);
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

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static float smoothstep_unit(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

weather_t *weather_create(void) {
    weather_t *w = calloc(1, sizeof *w);
    if (!w) return NULL;

    w->prog = load_program("shaders/weather.vert", "shaders/weather.frag");
    if (!w->prog) { free(w); return NULL; }
    w->mvp_loc        = glGetUniformLocation(w->prog, "u_mvp");
    w->color_loc      = glGetUniformLocation(w->prog, "u_color");
    w->point_size_loc = glGetUniformLocation(w->prog, "u_point_size");
    w->round_loc      = glGetUniformLocation(w->prog, "u_round");

    for (int i = 0; i < MAX_PARTICLES; i++) {
        w->parts[i].x     = (frand() * 2.0f - 1.0f) * BOX_XZ;
        w->parts[i].y     = frand() * BOX_SPAN - BOX_DOWN;
        w->parts[i].z     = (frand() * 2.0f - 1.0f) * BOX_XZ;
        w->parts[i].phase = frand() * 6.2831853f;
    }

    glGenVertexArrays(1, &w->vao);
    glGenBuffers(1, &w->vbo);
    glBindVertexArray(w->vao);
    glBindBuffer(GL_ARRAY_BUFFER, w->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof w->verts, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    w->mode = WEATHER_CLEAR;
    return w;
}

void weather_destroy(weather_t *w) {
    if (!w) return;
    glDeleteVertexArrays(1, &w->vao);
    glDeleteBuffers(1, &w->vbo);
    glDeleteProgram(w->prog);
    free(w);
}

void weather_set(weather_t *w, int mode) { if (w) w->mode = mode; }
int  weather_mode(const weather_t *w)    { return w ? w->mode : WEATHER_CLEAR; }

static float wrap_centered(float v, float half) {
    float span = 2.0f * half;
    return v - span * floorf((v + half) / span);
}

void weather_update(weather_t *w, vec3 cam, float dt, float intensity, uint32_t seed) {
    if (!w) return;
    w->time += dt;

    float k = 1.0f - expf(-3.0f * dt);
    w->live += (intensity - w->live) * k;

    if (w->mode == WEATHER_CLEAR || w->live < 0.005f) { w->vert_count = 0; return; }

    int snow = (w->mode == WEATHER_SNOW);
    float fall  = snow ? 4.5f : 30.0f;
    float windx = snow ? 1.2f : 3.2f;
    float windz = snow ? 0.8f : 1.4f;

    int live_n = (int)(MAX_PARTICLES * w->live);
    if (live_n > MAX_PARTICLES) live_n = MAX_PARTICLES;

    float *vp = w->verts;
    int    vc = 0;

    for (int i = 0; i < live_n; i++) {
        particle_t *p = &w->parts[i];
        float swayx = snow ? sinf(w->time * 1.3f + p->phase) * 1.1f : 0.0f;
        float swayz = snow ? cosf(w->time * 1.1f + p->phase) * 1.1f : 0.0f;

        p->y -= fall * dt;
        p->x += (windx + swayx) * dt;
        p->z += (windz + swayz) * dt;

        float rx = wrap_centered(p->x - cam.x, BOX_XZ);
        float rz = wrap_centered(p->z - cam.z, BOX_XZ);
        p->x = cam.x + rx;
        p->z = cam.z + rz;
        float ry = p->y - cam.y + BOX_DOWN;
        ry -= BOX_SPAN * floorf(ry / BOX_SPAN);
        ry -= BOX_DOWN;
        p->y = cam.y + ry;

        if (ry < 3.0f) {
            int surf = world_height_at(seed, (int)floorf(p->x), (int)floorf(p->z));
            if (p->y < (float)surf) continue;
        }

        float dist = sqrtf(rx * rx + rz * rz);
        float edge = 1.0f - smoothstep_unit((dist - BOX_XZ * 0.6f) / (BOX_XZ * 0.4f));
        float base = snow ? 0.85f : 0.40f;
        float alpha = base * w->live * edge;
        if (alpha < 0.01f) continue;

        if (snow) {
            vp[vc * 4 + 0] = p->x;
            vp[vc * 4 + 1] = p->y;
            vp[vc * 4 + 2] = p->z;
            vp[vc * 4 + 3] = alpha;
            vc++;
        } else {
            float sx = windx * 0.04f, sz = windz * 0.04f;
            vp[vc * 4 + 0] = p->x;
            vp[vc * 4 + 1] = p->y;
            vp[vc * 4 + 2] = p->z;
            vp[vc * 4 + 3] = alpha;
            vc++;
            vp[vc * 4 + 0] = p->x + sx;
            vp[vc * 4 + 1] = p->y + 1.3f;
            vp[vc * 4 + 2] = p->z + sz;
            vp[vc * 4 + 3] = alpha;
            vc++;
        }
    }

    w->vert_count = vc;
    if (vc > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, w->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)((size_t)vc * 4 * sizeof(float)), w->verts);
    }
}

void weather_render(const weather_t *w, mat4 pv, float day) {
    if (!w || w->mode == WEATHER_CLEAR || w->vert_count == 0) return;
    int snow = (w->mode == WEATHER_SNOW);

    glUseProgram(w->prog);
    glUniformMatrix4fv(w->mvp_loc, 1, GL_FALSE, pv.m);
    float bright = 0.45f + 0.55f * day;
    if (snow) glUniform3f(w->color_loc, 1.0f * bright, 1.0f * bright, 1.0f * bright);
    else      glUniform3f(w->color_loc, 0.62f * bright, 0.70f * bright, 0.85f * bright);
    glUniform1f(w->point_size_loc, snow ? 3.5f : 1.0f);
    glUniform1i(w->round_loc, snow ? 1 : 0);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glBindVertexArray(w->vao);
    if (snow) glDrawArrays(GL_POINTS, 0, w->vert_count);
    else      glDrawArrays(GL_LINES,  0, w->vert_count);
    glBindVertexArray(0);
    glDisable(GL_PROGRAM_POINT_SIZE);
}
