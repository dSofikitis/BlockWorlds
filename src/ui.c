#include "glcompat.h"

#include "ui.h"
#include "vec3.h"
#include "texture.h"

#include <stdio.h>
#include <stdlib.h>

struct ui_s {
    GLuint crosshair_prog;
    GLint  crosshair_color_loc;
    GLint  crosshair_aspect_loc;

    GLuint crosshair_vao, crosshair_vbo;
    int    crosshair_vert_count;

    GLuint tint_vao, tint_vbo;
    int    tint_vert_count;

    GLuint outline_prog;
    GLint  outline_mvp_loc;
    GLuint outline_vao, outline_vbo;
    int    outline_vert_count;

    GLuint underwater_prog;
    GLint  underwater_time_loc;
    GLint  underwater_aspect_loc;
    GLint  underwater_fade_loc;

    GLuint panel_prog;
    GLint  panel_aspect_loc, panel_color_loc;

    GLuint icon_prog;
    GLint  icon_aspect_loc, icon_atlas_loc, icon_tint_loc;
    GLuint block_atlas;

    GLuint blur_prog;
    GLint  blur_texel_loc, blur_darken_loc, blur_scene_loc;

    GLuint quad_vao, quad_vbo;

    GLuint capture_tex;
    int    capture_w, capture_h;
};

static char *ui_read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ui: failed to open %s\n", path); return NULL; }
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

static GLuint ui_compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "ui shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint ui_load_program(const char *vp, const char *fp) {
    char *vs = ui_read_text(vp);
    char *fs = ui_read_text(fp);
    if (!vs || !fs) { free(vs); free(fs); return 0; }
    GLuint v = ui_compile(GL_VERTEX_SHADER,   vs);
    GLuint f = ui_compile(GL_FRAGMENT_SHADER, fs);
    free(vs); free(fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v); glDeleteShader(f);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "ui program link failed: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static void build_crosshair(ui_t *u) {
    const float thick = 0.003f;
    const float len   = 0.018f;
    float verts[] = {
        -len, -thick,
         len, -thick,
         len,  thick,
        -len, -thick,
         len,  thick,
        -len,  thick,

        -thick, -len,
         thick, -len,
         thick,  len,
        -thick, -len,
         thick,  len,
        -thick,  len,
    };
    u->crosshair_vert_count = 12;

    glGenVertexArrays(1, &u->crosshair_vao);
    glGenBuffers(1, &u->crosshair_vbo);
    glBindVertexArray(u->crosshair_vao);
    glBindBuffer(GL_ARRAY_BUFFER, u->crosshair_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

static void build_tint(ui_t *u) {
    float verts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    u->tint_vert_count = 6;

    glGenVertexArrays(1, &u->tint_vao);
    glGenBuffers(1, &u->tint_vbo);
    glBindVertexArray(u->tint_vao);
    glBindBuffer(GL_ARRAY_BUFFER, u->tint_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

static void build_outline(ui_t *u) {
    const float e = -0.003f;
    const float f =  1.0f + 0.003f;
    float v[] = {
        e,e,e,  f,e,e,
        f,e,e,  f,e,f,
        f,e,f,  e,e,f,
        e,e,f,  e,e,e,
        e,f,e,  f,f,e,
        f,f,e,  f,f,f,
        f,f,f,  e,f,f,
        e,f,f,  e,f,e,
        e,e,e,  e,f,e,
        f,e,e,  f,f,e,
        f,e,f,  f,f,f,
        e,e,f,  e,f,f,
    };
    u->outline_vert_count = 24;

    glGenVertexArrays(1, &u->outline_vao);
    glGenBuffers(1, &u->outline_vbo);
    glBindVertexArray(u->outline_vao);
    glBindBuffer(GL_ARRAY_BUFFER, u->outline_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

static void build_ui_quad(ui_t *u) {
    glGenVertexArrays(1, &u->quad_vao);
    glGenBuffers(1, &u->quad_vbo);
    glBindVertexArray(u->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, u->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(6 * 4 * sizeof(float)), NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

static void build_capture_tex(ui_t *u) {
    glGenTextures(1, &u->capture_tex);
    glBindTexture(GL_TEXTURE_2D, u->capture_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

ui_t *ui_create(void) {
    ui_t *u = calloc(1, sizeof *u);
    if (!u) return NULL;

    u->crosshair_prog = ui_load_program("shaders/ui.vert", "shaders/ui.frag");
    if (!u->crosshair_prog) { free(u); return NULL; }
    u->crosshair_color_loc  = glGetUniformLocation(u->crosshair_prog, "u_color");
    u->crosshair_aspect_loc = glGetUniformLocation(u->crosshair_prog, "u_aspect");
    build_crosshair(u);
    build_tint(u);

    u->outline_prog = ui_load_program("shaders/outline.vert", "shaders/outline.frag");
    if (!u->outline_prog) { glDeleteProgram(u->crosshair_prog); free(u); return NULL; }
    u->outline_mvp_loc = glGetUniformLocation(u->outline_prog, "u_mvp");
    build_outline(u);

    u->underwater_prog = ui_load_program("shaders/underwater.vert", "shaders/underwater.frag");
    if (!u->underwater_prog) {
        glDeleteProgram(u->crosshair_prog);
        glDeleteProgram(u->outline_prog);
        free(u);
        return NULL;
    }
    u->underwater_time_loc   = glGetUniformLocation(u->underwater_prog, "u_time");
    u->underwater_aspect_loc = glGetUniformLocation(u->underwater_prog, "u_aspect");
    u->underwater_fade_loc   = glGetUniformLocation(u->underwater_prog, "u_fade");

    u->panel_prog = ui_load_program("shaders/panel.vert", "shaders/panel.frag");
    if (!u->panel_prog) {
        glDeleteProgram(u->crosshair_prog);
        glDeleteProgram(u->outline_prog);
        glDeleteProgram(u->underwater_prog);
        free(u);
        return NULL;
    }
    u->panel_aspect_loc = glGetUniformLocation(u->panel_prog, "u_aspect");
    u->panel_color_loc  = glGetUniformLocation(u->panel_prog, "u_color");

    u->icon_prog = ui_load_program("shaders/icon.vert", "shaders/icon.frag");
    if (!u->icon_prog) {
        glDeleteProgram(u->crosshair_prog);
        glDeleteProgram(u->outline_prog);
        glDeleteProgram(u->underwater_prog);
        glDeleteProgram(u->panel_prog);
        free(u);
        return NULL;
    }
    u->icon_aspect_loc = glGetUniformLocation(u->icon_prog, "u_aspect");
    u->icon_atlas_loc  = glGetUniformLocation(u->icon_prog, "u_atlas");
    u->icon_tint_loc   = glGetUniformLocation(u->icon_prog, "u_tint");

    u->blur_prog = ui_load_program("shaders/blur.vert", "shaders/blur.frag");
    if (!u->blur_prog) {
        glDeleteProgram(u->crosshair_prog);
        glDeleteProgram(u->outline_prog);
        glDeleteProgram(u->underwater_prog);
        glDeleteProgram(u->panel_prog);
        glDeleteProgram(u->icon_prog);
        free(u);
        return NULL;
    }
    u->blur_texel_loc  = glGetUniformLocation(u->blur_prog, "u_texel");
    u->blur_darken_loc = glGetUniformLocation(u->blur_prog, "u_darken");
    u->blur_scene_loc  = glGetUniformLocation(u->blur_prog, "u_scene");

    build_ui_quad(u);
    build_capture_tex(u);

    return u;
}

void ui_destroy(ui_t *u) {
    if (!u) return;
    glDeleteVertexArrays(1, &u->crosshair_vao);
    glDeleteBuffers(1, &u->crosshair_vbo);
    glDeleteVertexArrays(1, &u->tint_vao);
    glDeleteBuffers(1, &u->tint_vbo);
    glDeleteProgram(u->crosshair_prog);
    glDeleteVertexArrays(1, &u->outline_vao);
    glDeleteBuffers(1, &u->outline_vbo);
    glDeleteProgram(u->outline_prog);
    glDeleteProgram(u->underwater_prog);
    glDeleteVertexArrays(1, &u->quad_vao);
    glDeleteBuffers(1, &u->quad_vbo);
    glDeleteTextures(1, &u->capture_tex);
    glDeleteProgram(u->panel_prog);
    glDeleteProgram(u->icon_prog);
    glDeleteProgram(u->blur_prog);
    free(u);
}

void ui_draw_crosshair(const ui_t *u, float aspect) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(u->crosshair_prog);
    glUniform4f(u->crosshair_color_loc, 1.0f, 1.0f, 1.0f, 0.85f);
    glUniform1f(u->crosshair_aspect_loc, aspect);
    glBindVertexArray(u->crosshair_vao);
    glDrawArrays(GL_TRIANGLES, 0, u->crosshair_vert_count);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void ui_draw_screen_tint(const ui_t *u, float r, float g, float b, float a) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(u->crosshair_prog);
    glUniform4f(u->crosshair_color_loc, r, g, b, a);
    glUniform1f(u->crosshair_aspect_loc, 1.0f);
    glBindVertexArray(u->tint_vao);
    glDrawArrays(GL_TRIANGLES, 0, u->tint_vert_count);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void ui_draw_underwater(const ui_t *u, float aspect, float time, float fade) {
    glDisable(GL_DEPTH_TEST);
    glUseProgram(u->underwater_prog);
    glUniform1f(u->underwater_time_loc,   time);
    glUniform1f(u->underwater_aspect_loc, aspect);
    glUniform1f(u->underwater_fade_loc,   fade);
    glBindVertexArray(u->tint_vao);
    glDrawArrays(GL_TRIANGLES, 0, u->tint_vert_count);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void ui_draw_block_outline(const ui_t *u, int x, int y, int z, mat4 pv) {
    mat4 model = mat4_translate((vec3){(float)x, (float)y, (float)z});
    mat4 mvp = mat4_multiply(pv, model);
    glUseProgram(u->outline_prog);
    glUniformMatrix4fv(u->outline_mvp_loc, 1, GL_FALSE, mvp.m);
    glBindVertexArray(u->outline_vao);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINES, 0, u->outline_vert_count);
    glBindVertexArray(0);
}

void ui_draw_rect(const ui_t *u, float x, float y, float w, float h,
                  float r, float g, float b, float a, float aspect) {
    float v[24] = {
        x,     y,     0.0f, 0.0f,
        x + w, y,     0.0f, 0.0f,
        x + w, y + h, 0.0f, 0.0f,
        x,     y,     0.0f, 0.0f,
        x + w, y + h, 0.0f, 0.0f,
        x,     y + h, 0.0f, 0.0f,
    };
    glDisable(GL_DEPTH_TEST);
    glUseProgram(u->panel_prog);
    glUniform1f(u->panel_aspect_loc, aspect);
    glUniform4f(u->panel_color_loc, r, g, b, a);
    glBindVertexArray(u->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, u->quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof v, v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void ui_set_block_atlas(ui_t *u, unsigned int tex) { u->block_atlas = tex; }

void ui_draw_atlas_icon_tinted(const ui_t *u, unsigned int tex, int tiles_per_row, int tx, int ty,
                               float x, float y, float size,
                               float r, float g, float b, float a, float aspect) {
    float n  = (float)tiles_per_row;
    float u0 = (float)tx / n,   u1 = (float)(tx + 1) / n;
    float vt = (float)ty / n,   vb = (float)(ty + 1) / n;
    float v[24] = {
        x,        y,        u0, vt,
        x + size, y,        u1, vt,
        x + size, y + size, u1, vb,
        x,        y,        u0, vt,
        x + size, y + size, u1, vb,
        x,        y + size, u0, vb,
    };
    glDisable(GL_DEPTH_TEST);
    glUseProgram(u->icon_prog);
    glUniform1f(u->icon_aspect_loc, aspect);
    glUniform1i(u->icon_atlas_loc, 0);
    glUniform4f(u->icon_tint_loc, r, g, b, a);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(u->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, u->quad_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof v, v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void ui_draw_atlas_icon_n(const ui_t *u, unsigned int tex, int tiles_per_row, int tx, int ty,
                          float x, float y, float size, float aspect) {
    ui_draw_atlas_icon_tinted(u, tex, tiles_per_row, tx, ty, x, y, size,
                              1.0f, 1.0f, 1.0f, 1.0f, aspect);
}

void ui_draw_atlas_icon(const ui_t *u, int tx, int ty,
                        float x, float y, float size, float aspect) {
    ui_draw_atlas_icon_n(u, u->block_atlas, ATLAS_TILES, tx, ty, x, y, size, aspect);
}

void ui_capture_screen(ui_t *u, int w, int h) {
    u->capture_w = w;
    u->capture_h = h;
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, u->capture_tex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, w, h, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
}

void ui_draw_blur(const ui_t *u, float darken) {
    if (u->capture_w <= 0 || u->capture_h <= 0) return;
    glDisable(GL_DEPTH_TEST);
    glUseProgram(u->blur_prog);
    glUniform2f(u->blur_texel_loc, 1.0f / (float)u->capture_w, 1.0f / (float)u->capture_h);
    glUniform1f(u->blur_darken_loc, darken);
    glUniform1i(u->blur_scene_loc, 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, u->capture_tex);
    glBindVertexArray(u->tint_vao);
    glDrawArrays(GL_TRIANGLES, 0, u->tint_vert_count);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
}
