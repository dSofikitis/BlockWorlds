#pragma once

typedef struct text_s text_t;

text_t *text_create(void);
void    text_destroy(text_t *t);

void text_draw(text_t *t, const char *str, float x, float y, float scale,
               float r, float g, float b, float a, float aspect);

float text_width(const char *str, float scale);
float text_height(float scale);
