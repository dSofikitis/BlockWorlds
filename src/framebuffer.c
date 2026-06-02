#include "glcompat.h"

#include "framebuffer.h"

#include <stdio.h>
#include <stdlib.h>

struct framebuffer_s {
    GLuint fbo;
    GLuint color_tex;
    GLuint depth_tex;
    int    w, h;
};

static int fb_complete(const char *what) {
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "framebuffer: %s incomplete (0x%x)\n", what, (unsigned)st);
        return 0;
    }
    return 1;
}

static GLuint make_depth_texture(int w, int h, int compare) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    if (compare) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

framebuffer_t *framebuffer_create_depth(int w, int h) {
    framebuffer_t *fb = calloc(1, sizeof *fb);
    if (!fb) return NULL;
    fb->w = w;
    fb->h = h;
    fb->depth_tex = make_depth_texture(w, h, 1);

    glGenFramebuffers(1, &fb->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb->depth_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    int ok = fb_complete("depth target");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { framebuffer_destroy(fb); return NULL; }
    return fb;
}

framebuffer_t *framebuffer_create_depth_raw(int w, int h) {
    framebuffer_t *fb = calloc(1, sizeof *fb);
    if (!fb) return NULL;
    fb->w = w;
    fb->h = h;
    fb->depth_tex = make_depth_texture(w, h, 0);

    glGenFramebuffers(1, &fb->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb->depth_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    int ok = fb_complete("raw depth target");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { framebuffer_destroy(fb); return NULL; }
    return fb;
}

framebuffer_t *framebuffer_create_color(int w, int h) {
    framebuffer_t *fb = calloc(1, sizeof *fb);
    if (!fb) return NULL;
    fb->w = w;
    fb->h = h;

    glGenTextures(1, &fb->color_tex);
    glBindTexture(GL_TEXTURE_2D, fb->color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    fb->depth_tex = make_depth_texture(w, h, 0);

    glGenFramebuffers(1, &fb->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb->color_tex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb->depth_tex, 0);
    int ok = fb_complete("color target");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) { framebuffer_destroy(fb); return NULL; }
    return fb;
}

void framebuffer_destroy(framebuffer_t *fb) {
    if (!fb) return;
    if (fb->color_tex) glDeleteTextures(1, &fb->color_tex);
    if (fb->depth_tex) glDeleteTextures(1, &fb->depth_tex);
    if (fb->fbo)       glDeleteFramebuffers(1, &fb->fbo);
    free(fb);
}

void framebuffer_bind(const framebuffer_t *fb) {
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glViewport(0, 0, fb->w, fb->h);
}

void framebuffer_unbind(int screen_w, int screen_h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w, screen_h);
}

unsigned int framebuffer_color_tex(const framebuffer_t *fb) { return fb->color_tex; }
unsigned int framebuffer_depth_tex(const framebuffer_t *fb) { return fb->depth_tex; }
int          framebuffer_width(const framebuffer_t *fb)     { return fb->w; }
int          framebuffer_height(const framebuffer_t *fb)    { return fb->h; }