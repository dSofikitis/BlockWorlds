#pragma once

#include "text.h"
#include "ui.h"

typedef struct console_s console_t;

console_t *console_create(text_t *text, ui_t *ui);
void       console_destroy(console_t *c);

void console_set_commands(console_t *c, const char *const *names,
                          const char *const *usages, int count);

int  console_is_open(const console_t *c);
void console_open(console_t *c, const char *prefill);
void console_close(console_t *c);

void console_char(console_t *c, unsigned int codepoint);
int  console_key(console_t *c, int key, int action, char *out, int out_cap);

void console_print(console_t *c, const char *line);
void console_clear(console_t *c);

void console_render(const console_t *c, float aspect);
void console_render_feed(const console_t *c, float aspect);
