#include <GLFW/glfw3.h>

#include "console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CON_INPUT_MAX   200
#define CON_LINE_MAX    200
#define CON_HISTORY_MAX 64
#define CON_SUGGEST_MAX 8
#define CON_VISIBLE     10

struct console_s {
    text_t *text;
    ui_t   *ui;

    int  open;
    char input[CON_INPUT_MAX];

    char   history[CON_HISTORY_MAX][CON_LINE_MAX];
    double stamp[CON_HISTORY_MAX];
    int    hist_head;
    int    hist_count;

    const char *const *cmd_names;
    const char *const *cmd_usages;
    int  cmd_count;

    char tab_prefix[CON_INPUT_MAX];
    int  tab_active;
    int  tab_index;
};

console_t *console_create(text_t *text, ui_t *ui) {
    console_t *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    c->text = text;
    c->ui   = ui;
    return c;
}

void console_destroy(console_t *c) {
    free(c);
}

void console_set_commands(console_t *c, const char *const *names,
                          const char *const *usages, int count) {
    c->cmd_names  = names;
    c->cmd_usages = usages;
    c->cmd_count  = count;
}

int console_is_open(const console_t *c) { return c->open; }

void console_open(console_t *c, const char *prefill) {
    c->open = 1;
    c->tab_active = 0;
    c->tab_index = 0;
    c->input[0] = '\0';
    if (prefill) {
        size_t i = 0;
        for (; i + 1 < sizeof c->input && prefill[i]; i++) c->input[i] = prefill[i];
        c->input[i] = '\0';
    }
}

void console_close(console_t *c) {
    c->open = 0;
    c->input[0] = '\0';
    c->tab_active = 0;
}

void console_clear(console_t *c) {
    c->hist_head = 0;
    c->hist_count = 0;
}

void console_print(console_t *c, const char *line) {
    int idx;
    if (c->hist_count < CON_HISTORY_MAX) {
        idx = (c->hist_head + c->hist_count) % CON_HISTORY_MAX;
        c->hist_count++;
    } else {
        idx = c->hist_head;
        c->hist_head = (c->hist_head + 1) % CON_HISTORY_MAX;
    }
    snprintf(c->history[idx], CON_LINE_MAX, "%s", line ? line : "");
    c->stamp[idx] = glfwGetTime();
}

void console_render_feed(const console_t *c, float aspect) {
    double now = glfwGetTime();
    const double feed_time = 8.0, feed_fade = 1.0;
    float lineH = 0.030f;
    float baseY = 0.84f;
    for (int j = 0; j < 8 && j < c->hist_count; j++) {
        int hidx = (c->hist_head + c->hist_count - 1 - j) % CON_HISTORY_MAX;
        double age = now - c->stamp[hidx];
        if (age > feed_time) break;
        float alpha = 1.0f;
        if (age > feed_time - feed_fade) alpha = (float)((feed_time - age) / feed_fade);
        if (alpha < 0.0f) alpha = 0.0f;
        float ly = baseY - (float)j * lineH;
        float tw = text_width(c->history[hidx], 0.024f);
        ui_draw_rect(c->ui, 0.010f, ly, tw + 0.012f, lineH, 0.0f, 0.0f, 0.0f, 0.30f * alpha, aspect);
        text_draw(c->text, c->history[hidx], 0.016f, ly + 0.005f, 0.024f,
                  0.92f, 0.94f, 0.97f, alpha, aspect);
    }
}

void console_char(console_t *c, unsigned int codepoint) {
    if (!c->open) return;
    if (codepoint < 0x20 || codepoint > 0x7e) return;
    size_t L = strlen(c->input);
    if (L + 1 < sizeof c->input) {
        c->input[L] = (char)codepoint;
        c->input[L + 1] = '\0';
    }
    c->tab_active = 0;
}

static int gather_idx(const console_t *c, const char *prefix, int *out, int max) {
    size_t plen = strlen(prefix);
    int n = 0;
    for (int i = 0; i < c->cmd_count && n < max; i++) {
        if (strncmp(c->cmd_names[i], prefix, plen) == 0) out[n++] = i;
    }
    return n;
}

static const char *live_prefix(const console_t *c) {
    if (c->tab_active) return c->tab_prefix;
    if (c->input[0] == '/' && strchr(c->input, ' ') == NULL) return c->input + 1;
    return NULL;
}

static void do_autocomplete(console_t *c) {
    if (!c->tab_active) {
        if (c->input[0] != '/' || strchr(c->input, ' ') != NULL) return;
        snprintf(c->tab_prefix, sizeof c->tab_prefix, "%s", c->input + 1);
        c->tab_active = 1;
        c->tab_index = 0;
    }
    int idx[CON_SUGGEST_MAX];
    int n = gather_idx(c, c->tab_prefix, idx, CON_SUGGEST_MAX);
    if (n <= 0) { c->tab_active = 0; return; }
    int pick = idx[c->tab_index % n];
    snprintf(c->input, sizeof c->input, "/%s", c->cmd_names[pick]);
    c->tab_index++;
}

int console_key(console_t *c, int key, int action, char *out, int out_cap) {
    if (!c->open) return 0;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return 0;

    if (key == GLFW_KEY_BACKSPACE) {
        size_t L = strlen(c->input);
        if (L > 0) c->input[L - 1] = '\0';
        c->tab_active = 0;
        return 0;
    }
    if (action != GLFW_PRESS) return 0;

    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        if (c->input[0] != '\0' && out && out_cap > 0) {
            snprintf(out, (size_t)out_cap, "%s", c->input);
            console_close(c);
            return 1;
        }
        console_close(c);
        return 0;
    }
    if (key == GLFW_KEY_ESCAPE) {
        console_close(c);
        return 0;
    }
    if (key == GLFW_KEY_TAB) {
        do_autocomplete(c);
        return 0;
    }
    return 0;
}

void console_render(const console_t *c, float aspect) {
    if (!c->open) return;

    float ix = 0.02f;
    float iw = aspect - 0.04f;
    float ih = 0.05f;
    float iy = 0.92f;

    int idx[CON_SUGGEST_MAX];
    const char *prefix = live_prefix(c);
    int ns = prefix ? gather_idx(c, prefix, idx, CON_SUGGEST_MAX) : 0;

    float sh = 0.030f;
    float sug_top = iy - 0.012f - (float)ns * sh;
    for (int i = 0; i < ns; i++) {
        float ly = sug_top + (float)i * sh;
        int hl = (strcmp(c->cmd_names[idx[i]], c->input + 1) == 0);
        ui_draw_rect(c->ui, ix, ly, iw, sh,
                     hl ? 0.20f : 0.06f, hl ? 0.26f : 0.07f, hl ? 0.32f : 0.10f, 0.92f, aspect);
        text_draw(c->text, c->cmd_names[idx[i]], ix + 0.012f, ly + 0.004f, 0.024f,
                  1.0f, 1.0f, 1.0f, 1.0f, aspect);
        if (c->cmd_usages && c->cmd_usages[idx[i]]) {
            text_draw(c->text, c->cmd_usages[idx[i]], ix + 0.22f, ly + 0.005f, 0.020f,
                      0.55f, 0.62f, 0.72f, 1.0f, aspect);
        }
    }

    float lineH = 0.032f;
    float hbase = (ns > 0) ? sug_top - 0.008f : iy - 0.012f;
    for (int j = 0; j < CON_VISIBLE && j < c->hist_count; j++) {
        int hidx = (c->hist_head + c->hist_count - 1 - j) % CON_HISTORY_MAX;
        float ly = hbase - lineH - (float)j * lineH;
        if (ly < 0.04f) break;
        ui_draw_rect(c->ui, ix, ly, iw, lineH, 0.0f, 0.0f, 0.0f, 0.35f, aspect);
        text_draw(c->text, c->history[hidx], ix + 0.008f, ly + 0.005f, 0.024f,
                  0.9f, 0.92f, 0.95f, 1.0f, aspect);
    }

    ui_draw_rect(c->ui, ix, iy, iw, ih, 0.05f, 0.06f, 0.09f, 0.88f, aspect);
    ui_draw_rect(c->ui, ix, iy, iw, 0.003f, 0.5f, 0.6f, 0.8f, 0.9f, aspect);
    int blink = ((int)(glfwGetTime() * 2.0)) & 1;
    char shown[CON_INPUT_MAX + 8];
    snprintf(shown, sizeof shown, "> %s%s", c->input, blink ? "_" : "");
    text_draw(c->text, shown, ix + 0.012f, iy + (ih - 0.030f) * 0.5f, 0.030f,
              1.0f, 1.0f, 1.0f, 1.0f, aspect);
}
