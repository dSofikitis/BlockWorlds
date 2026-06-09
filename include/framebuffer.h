#pragma once

typedef struct framebuffer_s framebuffer_t;

framebuffer_t *framebuffer_create_depth(int w, int h);
framebuffer_t *framebuffer_create_depth_raw(int w, int h);
framebuffer_t *framebuffer_create_color(int w, int h);
void           framebuffer_destroy(framebuffer_t *fb);

void framebuffer_bind(const framebuffer_t *fb);
void framebuffer_unbind(int screen_w, int screen_h);

unsigned int framebuffer_color_tex(const framebuffer_t *fb);
unsigned int framebuffer_depth_tex(const framebuffer_t *fb);
int          framebuffer_width(const framebuffer_t *fb);
int          framebuffer_height(const framebuffer_t *fb);
