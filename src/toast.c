
#include "toast.h"

#include <stdlib.h>
#include <string.h>

#include "ui.h"
#include "text.h"

#define TOAST_LIFE      3.5f
#define TOAST_SLIDE     0.3f

typedef struct {
    char  title[48];
    char  subtitle[48];
    float timer;
    float life;
    int   active;
} toast_t;

struct toast_system_s {
    toast_t  queue[TOAST_MAX_QUEUE];
    int      head;
    int      tail;
    int      count;
    uint32_t bits;
};

static const struct { const char *title; const char *subtitle; } ACH_TABLE[ACH_COUNT] = {
    [ACH_GET_WOOD]       = { "Got Wood!",            "Punch a tree" },
    [ACH_MAKE_PLANKS]    = { "Planks",               "Crafted planks" },
    [ACH_CRAFTING_TABLE] = { "Crafting",             "Made a crafting table" },
    [ACH_MAKE_TOOL]      = { "Tooled Up",            "Crafted a tool" },
    [ACH_MINE_STONE]     = { "Got Stone",            "Mined cobblestone" },
    [ACH_GET_IRON]       = { "Iron!",                "Found raw iron" },
    [ACH_SMELT_IRON]     = { "Hot Topic",            "Smelted an iron ingot" },
    [ACH_MAKE_FURNACE]   = { "Furnace",              "Built a furnace" },
    [ACH_KILL_FIRST_MOB] = { "Monster Hunter",       "Defeated a mob" },
    [ACH_PLANT_TORCH]    = { "Let There Be Light",   "Placed a torch" },
    [ACH_MAKE_BED]       = { "Sweet Dreams",         "Crafted a bed" },
    [ACH_SLEEP]          = { "Good Night",           "Slept through the night" },
    [ACH_DIAMONDS]       = { "DIAMONDS!",            "Mined a diamond" },
    [ACH_LEVEL_UP]       = { "Level Up",             "Gained an XP level" },
};

toast_system_t *toast_create(void) {
    toast_system_t *ts = (toast_system_t *)calloc(1, sizeof(*ts));
    return ts;
}

void toast_destroy(toast_system_t *ts) {
    free(ts);
}

static void copy_field(char *dst, size_t cap, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void toast_push(toast_system_t *ts, const char *title, const char *subtitle) {
    if (!ts) return;
    if (ts->count >= TOAST_MAX_QUEUE) return;

    toast_t *t = &ts->queue[ts->tail];
    copy_field(t->title,    sizeof t->title,    title);
    copy_field(t->subtitle, sizeof t->subtitle, subtitle);
    t->timer  = 0.0f;
    t->life   = TOAST_LIFE;
    t->active = 1;

    ts->tail = (ts->tail + 1) % TOAST_MAX_QUEUE;
    ts->count++;
}

int toast_unlock(toast_system_t *ts, int achievement_id) {
    if (!ts) return 0;
    if (achievement_id < 0 || achievement_id >= ACH_COUNT) return 0;

    uint32_t mask = (uint32_t)1u << achievement_id;
    if (ts->bits & mask) return 0;
    ts->bits |= mask;

    toast_push(ts, ACH_TABLE[achievement_id].title, ACH_TABLE[achievement_id].subtitle);
    return 1;
}

int toast_is_unlocked(const toast_system_t *ts, int achievement_id) {
    if (!ts) return 0;
    if (achievement_id < 0 || achievement_id >= ACH_COUNT) return 0;
    return (ts->bits & ((uint32_t)1u << achievement_id)) ? 1 : 0;
}

uint32_t toast_bits(const toast_system_t *ts) {
    return ts ? ts->bits : 0u;
}

void toast_set_bits(toast_system_t *ts, uint32_t bits) {
    if (ts) ts->bits = bits;
}

void toast_update(toast_system_t *ts, float dt) {
    if (!ts || ts->count <= 0) return;

    toast_t *t = &ts->queue[ts->head];
    t->timer += dt;
    if (t->timer >= t->life) {
        t->active = 0;
        ts->head = (ts->head + 1) % TOAST_MAX_QUEUE;
        ts->count--;
        if (ts->count > 0) {
            ts->queue[ts->head].timer = 0.0f;
        }
    }
}

static float slide_progress(const toast_t *t) {
    float p;
    if (t->timer < TOAST_SLIDE) {
        p = t->timer / TOAST_SLIDE;
    } else if (t->timer > t->life - TOAST_SLIDE) {
        p = (t->life - t->timer) / TOAST_SLIDE;
    } else {
        p = 1.0f;
    }
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p * p * (3.0f - 2.0f * p);
}

void toast_render(const toast_system_t *ts, void *ui_p, void *text_p,
                  unsigned int hud_atlas, float aspect) {
    if (!ts || ts->count <= 0) return;

    const toast_t *t = &ts->queue[ts->head];
    if (!t->active) return;

    ui_t   *ui   = (ui_t *)ui_p;
    text_t *text = (text_t *)text_p;

    float prog = slide_progress(t);

    const float ph = 0.085f;
    const float icon = ph - 0.028f;
    const float title_scale = 0.030f;
    const float sub_scale   = 0.020f;
    const float lpad = 0.016f, gap = 0.018f, rpad = 0.022f;

    float title_w = text_width(t->title, title_scale);
    float sub_w   = text_width(t->subtitle, sub_scale);
    float content_w = title_w > sub_w ? title_w : sub_w;
    float pw = lpad + icon + gap + content_w + rpad;
    if (pw < 0.26f) pw = 0.26f;

    const float cx = aspect * 0.5f;
    const float px = cx - pw * 0.5f;

    const float y_shown  = 0.035f;
    const float y_hidden = -ph - 0.02f;
    const float py = y_hidden + (y_shown - y_hidden) * prog;

    float a = prog;

    ui_draw_rect(ui, px, py, pw, ph, 0.06f, 0.06f, 0.09f, 0.82f * a, aspect);
    const float inset = 0.006f;
    ui_draw_rect(ui, px + inset, py + inset, pw - 2.0f * inset, ph - 2.0f * inset,
                 0.12f, 0.12f, 0.16f, 0.70f * a, aspect);
    ui_draw_rect(ui, px, py, pw, 0.006f, 0.95f, 0.82f, 0.25f, 0.85f * a, aspect);

    const float ix = px + lpad;
    const float iy = py + (ph - icon) * 0.5f;
    ui_draw_atlas_icon_n(ui, hud_atlas, 8, 1, 5, ix, iy, icon, aspect);

    const float tx = ix + icon + gap;
    text_draw(text, t->title, tx, py + 0.014f, title_scale,
              1.0f, 0.92f, 0.45f, a, aspect);
    text_draw(text, t->subtitle, tx, py + 0.014f + text_height(title_scale) + 0.006f,
              sub_scale, 0.82f, 0.86f, 0.95f, a, aspect);
}
