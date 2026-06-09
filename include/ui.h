#pragma once

#include "mat4.h"

typedef struct ui_s ui_t;

ui_t *ui_create(void);
void  ui_destroy(ui_t *u);

void ui_draw_crosshair(const ui_t *u, float aspect);

void ui_draw_block_outline(const ui_t *u, int x, int y, int z, mat4 pv);

void ui_draw_screen_tint(const ui_t *u, float r, float g, float b, float a);

void ui_draw_underwater(const ui_t *u, float aspect, float time, float fade);

void ui_draw_rect(const ui_t *u, float x, float y, float w, float h,
                  float r, float g, float b, float a, float aspect);

void ui_draw_atlas_icon(const ui_t *u, int tx, int ty,
                        float x, float y, float size, float aspect);

void ui_draw_atlas_icon_n(const ui_t *u, unsigned int tex, int tiles_per_row, int tx, int ty,
                          float x, float y, float size, float aspect);
void ui_draw_atlas_icon_tinted(const ui_t *u, unsigned int tex, int tiles_per_row, int tx, int ty,
                               float x, float y, float size,
                               float r, float g, float b, float a, float aspect);
void ui_set_block_atlas(ui_t *u, unsigned int tex);

void ui_capture_screen(ui_t *u, int w, int h);
void ui_draw_blur(const ui_t *u, float darken);
