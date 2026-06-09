
#include "craft.h"
#include "registry.h"
#include "world.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define P  BLOCK_PLANKS
#define S  ITEM_STICK
#define C  BLOCK_COBBLESTONE
#define I  ITEM_IRON_INGOT
#define D  ITEM_DIAMOND
#define _0 0

#define PICK(out, M)  { (out), 1, RECIPE_SHAPED, 3, 3, { (M),(M),(M),  _0,S,_0,  _0,S,_0 }, 0, 1, 0, 0 }
#define AXE(out, M)   { (out), 1, RECIPE_SHAPED, 3, 3, { (M),(M),_0,  (M),S,_0,  _0,S,_0 }, 0, 1, 1, 0 }
#define SHOVEL(out, M){ (out), 1, RECIPE_SHAPED, 1, 3, { (M),S,S }, 0, 1, 0, 0 }
#define SWORD(out, M) { (out), 1, RECIPE_SHAPED, 1, 3, { (M),(M),S }, 0, 1, 0, 0 }
#define HOE(out, M)   { (out), 1, RECIPE_SHAPED, 3, 3, { (M),(M),_0,  _0,S,_0,  _0,S,_0 }, 0, 1, 1, 0 }

#define HELMET(out, M){ (out), 1, RECIPE_SHAPED, 3, 2, { (M),(M),(M),  (M),_0,(M) }, 0, 1, 0, 0 }
#define CHESTP(out, M){ (out), 1, RECIPE_SHAPED, 3, 3, { (M),_0,(M),  (M),(M),(M),  (M),(M),(M) }, 0, 1, 0, 0 }
#define LEGS(out, M)  { (out), 1, RECIPE_SHAPED, 3, 3, { (M),(M),(M),  (M),_0,(M),  (M),_0,(M) }, 0, 1, 0, 0 }
#define BOOTS(out, M) { (out), 1, RECIPE_SHAPED, 3, 2, { (M),_0,(M),  (M),_0,(M) }, 0, 1, 0, 0 }

const recipe_t RECIPES[] = {
    { BLOCK_PLANKS, 4, RECIPE_SHAPELESS, 0, 0, { BLOCK_WOOD }, 1, 0, 0, 0 },
    { ITEM_STICK, 4, RECIPE_SHAPED, 1, 2, { P, P }, 0, 0, 0, 0 },
    { BLOCK_CRAFTING_TABLE, 1, RECIPE_SHAPED, 2, 2, { P, P, P, P }, 0, 0, 0, 0 },
    { BLOCK_TORCH, 4, RECIPE_SHAPED, 1, 2, { ITEM_COAL, S }, 0, 0, 0, 0 },

    { ITEM_BOWL, 4, RECIPE_SHAPED, 3, 2, { P, _0, P,  _0, P, _0 }, 0, 1, 0, 0 },
    { BLOCK_FURNACE, 1, RECIPE_SHAPED, 3, 3, { C, C, C,  C, _0, C,  C, C, C }, 0, 1, 0, 0 },
    { BLOCK_FORGE, 1, RECIPE_SHAPED, 3, 3, { I, I, I,  I, C, I,  I, I, I }, 0, 1, 0, 0 },
    { BLOCK_ANVIL, 1, RECIPE_SHAPED, 3, 3,
      { BLOCK_IRON_BLOCK, BLOCK_IRON_BLOCK, BLOCK_IRON_BLOCK,  _0, I, _0,  I, I, I }, 0, 1, 0, 0 },
    { BLOCK_CHEST, 1, RECIPE_SHAPED, 3, 3, { P, P, P,  P, _0, P,  P, P, P }, 0, 1, 0, 0 },
    { ITEM_BED, 1, RECIPE_SHAPED, 3, 2,
      { BLOCK_WOOL, BLOCK_WOOL, BLOCK_WOOL,  P, P, P }, 0, 1, 0, 0 },
    { ITEM_BREAD, 1, RECIPE_SHAPED, 3, 1, { ITEM_WHEAT, ITEM_WHEAT, ITEM_WHEAT }, 0, 1, 0, 0 },
    { BLOCK_IRON_BLOCK, 1, RECIPE_SHAPED, 3, 3, { I, I, I,  I, I, I,  I, I, I }, 0, 1, 0, 0 },
    { BLOCK_COAL_BLOCK, 1, RECIPE_SHAPED, 3, 3,
      { ITEM_COAL, ITEM_COAL, ITEM_COAL,  ITEM_COAL, ITEM_COAL, ITEM_COAL,
        ITEM_COAL, ITEM_COAL, ITEM_COAL }, 0, 1, 0, 0 },
    { BLOCK_DIAMOND_BLOCK, 1, RECIPE_SHAPED, 3, 3, { D, D, D,  D, D, D,  D, D, D }, 0, 1, 0, 0 },
    { ITEM_BOW, 1, RECIPE_SHAPED, 3, 3,
      { _0, S, ITEM_STRING,  S, _0, ITEM_STRING,  _0, S, ITEM_STRING }, 0, 1, 0, 0 },
    { ITEM_ARROW, 4, RECIPE_SHAPED, 1, 3, { ITEM_FLINT, S, ITEM_FEATHER }, 0, 1, 0, 0 },

    PICK(ITEM_WOOD_PICKAXE,    P),
    PICK(ITEM_STONE_PICKAXE,   C),
    PICK(ITEM_IRON_PICKAXE,    I),
    PICK(ITEM_DIAMOND_PICKAXE, D),

    AXE(ITEM_WOOD_AXE,    P),
    AXE(ITEM_STONE_AXE,   C),
    AXE(ITEM_IRON_AXE,    I),
    AXE(ITEM_DIAMOND_AXE, D),

    SHOVEL(ITEM_WOOD_SHOVEL,    P),
    SHOVEL(ITEM_STONE_SHOVEL,   C),
    SHOVEL(ITEM_IRON_SHOVEL,    I),
    SHOVEL(ITEM_DIAMOND_SHOVEL, D),

    SWORD(ITEM_WOOD_SWORD,    P),
    SWORD(ITEM_STONE_SWORD,   C),
    SWORD(ITEM_IRON_SWORD,    I),
    SWORD(ITEM_DIAMOND_SWORD, D),

    HOE(ITEM_WOOD_HOE,    P),
    HOE(ITEM_STONE_HOE,   C),
    HOE(ITEM_IRON_HOE,    I),
    HOE(ITEM_DIAMOND_HOE, D),

    HELMET(ITEM_LEATHER_HELMET, ITEM_LEATHER),
    HELMET(ITEM_IRON_HELMET,    ITEM_IRON_INGOT),
    HELMET(ITEM_DIAMOND_HELMET, ITEM_DIAMOND),

    CHESTP(ITEM_LEATHER_CHEST, ITEM_LEATHER),
    CHESTP(ITEM_IRON_CHEST,    ITEM_IRON_INGOT),
    CHESTP(ITEM_DIAMOND_CHEST, ITEM_DIAMOND),

    LEGS(ITEM_LEATHER_LEGS, ITEM_LEATHER),
    LEGS(ITEM_IRON_LEGS,    ITEM_IRON_INGOT),
    LEGS(ITEM_DIAMOND_LEGS, ITEM_DIAMOND),

    BOOTS(ITEM_LEATHER_BOOTS, ITEM_LEATHER),
    BOOTS(ITEM_IRON_BOOTS,    ITEM_IRON_INGOT),
    BOOTS(ITEM_DIAMOND_BOOTS, ITEM_DIAMOND),

    { ITEM_GLASS_BOTTLE, 3, RECIPE_SHAPED, 3, 2,
      { BLOCK_GLASS, _0, BLOCK_GLASS,  _0, BLOCK_GLASS, _0 }, 0, 1, 0, 0 },
    { ITEM_BLAZE_POWDER, 2, RECIPE_SHAPELESS, 0, 0, { ITEM_GUNPOWDER, ITEM_COAL }, 2, 0, 0, 0 },
    { ITEM_BLAZE_ROD, 1, RECIPE_SHAPED, 1, 2, { ITEM_BLAZE_POWDER, ITEM_BLAZE_POWDER }, 0, 1, 0, 0 },
    { BLOCK_BREWING_STAND, 1, RECIPE_SHAPED, 3, 2,
      { _0, ITEM_BLAZE_POWDER, _0,  C, C, C }, 0, 1, 0, 0 },
    { ITEM_SUGAR, 1, RECIPE_SHAPELESS, 0, 0, { ITEM_WHEAT }, 1, 0, 0, 0 },
    { ITEM_FERMENTED_SPIDER_EYE, 1, RECIPE_SHAPELESS, 0, 0,
      { ITEM_SPIDER_EYE, ITEM_SUGAR }, 2, 0, 0, 0 },
    { ITEM_NETHER_WART, 1, RECIPE_SHAPELESS, 0, 0, { ITEM_ROTTEN_FLESH, ITEM_BONE }, 2, 0, 0, 0 },
    { ITEM_MAGMA_CREAM, 1, RECIPE_SHAPELESS, 0, 0, { ITEM_BLAZE_POWDER, ITEM_COAL }, 2, 0, 0, 0 },
    { ITEM_GLISTERING_MELON, 1, RECIPE_SHAPELESS, 0, 0, { ITEM_APPLE, ITEM_IRON_INGOT }, 2, 0, 0, 0 },
    { ITEM_GHAST_TEAR, 1, RECIPE_SHAPELESS, 0, 0, { ITEM_DIAMOND, ITEM_BONE }, 2, 0, 0, 0 },
    { ITEM_PUFFERFISH, 1, RECIPE_SHAPELESS, 0, 0, { ITEM_STRING, ITEM_SPIDER_EYE }, 2, 0, 0, 0 },
    { ITEM_ARROW_POISON,  4, RECIPE_SHAPELESS, 0, 0, { ITEM_ARROW, ITEM_POTION_POISON }, 2, 0, 0, 0 },
    { ITEM_ARROW_HARMING, 4, RECIPE_SHAPELESS, 0, 0, { ITEM_ARROW, ITEM_POTION_HARMING }, 2, 0, 0, 0 },
};

const int RECIPE_COUNT = (int)(sizeof(RECIPES) / sizeof(RECIPES[0]));

#undef P
#undef S
#undef C
#undef I
#undef D
#undef _0
#undef PICK
#undef AXE
#undef SHOVEL
#undef SWORD
#undef HOE
#undef HELMET
#undef CHESTP
#undef LEGS
#undef BOOTS

static int slot_empty(const inv_slot_t *s) {
    return s->id == 0 || s->count <= 0;
}

static int shaped_match_pattern(const inv_slot_t *grid, int gw, int gh,
                                const item_id *pat, int pw, int ph) {
    if (pw > gw || ph > gh) return 0;
    for (int oy = 0; oy + ph <= gh; ++oy) {
        for (int ox = 0; ox + pw <= gw; ++ox) {
            int ok = 1;
            for (int gy = 0; gy < gh && ok; ++gy) {
                for (int gx = 0; gx < gw; ++gx) {
                    const inv_slot_t *cell = &grid[gy * gw + gx];
                    int px = gx - ox, py = gy - oy;
                    item_id want = 0;
                    if (px >= 0 && px < pw && py >= 0 && py < ph)
                        want = pat[py * pw + px];
                    if (want == 0) {
                        if (!slot_empty(cell)) { ok = 0; break; }
                    } else {
                        if (slot_empty(cell) || cell->id != want) { ok = 0; break; }
                    }
                }
            }
            if (ok) return 1;
        }
    }
    return 0;
}

static int shapeless_match(const inv_slot_t *grid, int gw, int gh, const recipe_t *r) {
    int total = gw * gh;
    int matched[CRAFT_GRID_MAX * CRAFT_GRID_MAX];
    int ncells = 0;
    for (int i = 0; i < total; ++i) {
        matched[i] = 0;
        if (!slot_empty(&grid[i])) ++ncells;
    }
    if (ncells != (int)r->n_ing) return 0;
    for (int k = 0; k < (int)r->n_ing; ++k) {
        item_id want = r->cell[k];
        int found = 0;
        for (int i = 0; i < total; ++i) {
            if (matched[i] || slot_empty(&grid[i])) continue;
            if (grid[i].id == want) { matched[i] = 1; found = 1; break; }
        }
        if (!found) return 0;
    }
    return 1;
}

const recipe_t *craft_match(const inv_slot_t *grid, int gw, int gh, int is_forge) {
    if (!grid || gw <= 0 || gh <= 0) return NULL;
    if (gw * gh > CRAFT_GRID_MAX * CRAFT_GRID_MAX) return NULL;

    for (int ri = 0; ri < RECIPE_COUNT; ++ri) {
        const recipe_t *r = &RECIPES[ri];
        if (r->requires_3x3 && (gw < 3 || gh < 3)) continue;
        if (r->forge_only && !is_forge) continue;

        if (r->kind == RECIPE_SHAPELESS) {
            if (shapeless_match(grid, gw, gh, r)) return r;
            continue;
        }

        if (shaped_match_pattern(grid, gw, gh, r->cell, r->w, r->h)) return r;
        if (r->mirror_ok) {
            item_id mir[CRAFT_GRID_MAX];
            for (int y = 0; y < r->h; ++y)
                for (int x = 0; x < r->w; ++x)
                    mir[y * r->w + x] = r->cell[y * r->w + (r->w - 1 - x)];
            if (shaped_match_pattern(grid, gw, gh, mir, r->w, r->h)) return r;
        }
    }
    return NULL;
}

void craft_consume_one(inv_slot_t *grid, int gw, int gh, const recipe_t *r) {
    if (!grid || !r || gw <= 0 || gh <= 0) return;
    int total = gw * gh;
    if (r->kind == RECIPE_SHAPELESS) {
        int matched[CRAFT_GRID_MAX * CRAFT_GRID_MAX];
        for (int i = 0; i < total && i < CRAFT_GRID_MAX * CRAFT_GRID_MAX; ++i)
            matched[i] = 0;
        for (int k = 0; k < (int)r->n_ing; ++k) {
            item_id want = r->cell[k];
            for (int i = 0; i < total; ++i) {
                if (matched[i] || slot_empty(&grid[i])) continue;
                if (grid[i].id == want) {
                    matched[i] = 1;
                    if (--grid[i].count <= 0) { grid[i].id = 0; grid[i].count = 0; }
                    break;
                }
            }
        }
        return;
    }
    for (int i = 0; i < total; ++i) {
        if (slot_empty(&grid[i])) continue;
        if (--grid[i].count <= 0) { grid[i].id = 0; grid[i].count = 0; }
    }
}

#define STATION_BUCKETS 8192

static station_t g_stations[STATION_BUCKETS];
static uint8_t   g_used[STATION_BUCKETS];
static int       g_station_count;

static uint64_t pack_key(int x, int y, int z) {
    uint64_t ux = (uint64_t)((uint32_t)(x + (1 << 20)) & 0x1FFFFFu);
    uint64_t uy = (uint64_t)((uint32_t)(y + (1 << 20)) & 0x1FFFFFu);
    uint64_t uz = (uint64_t)((uint32_t)(z + (1 << 20)) & 0x1FFFFFu);
    return ux | (uy << 21) | (uz << 42);
}

static uint32_t hash_key(uint64_t k) {
    k ^= k >> 33;
    k *= 0xFF51AFD7ED558CCDull;
    k ^= k >> 33;
    k *= 0xC4CEB9FE1A85EC53ull;
    k ^= k >> 33;
    return (uint32_t)(k & 0xFFFFFFFFu);
}

void craft_stations_init(void) {
    memset(g_stations, 0, sizeof(g_stations));
    memset(g_used, 0, sizeof(g_used));
    g_station_count = 0;
}

station_t *craft_station_get(int x, int y, int z) {
    uint64_t key = pack_key(x, y, z);
    uint32_t h = hash_key(key) & (STATION_BUCKETS - 1);
    for (int probe = 0; probe < STATION_BUCKETS; ++probe) {
        uint32_t i = (h + (uint32_t)probe) & (STATION_BUCKETS - 1);
        if (g_used[i] == 0) return NULL;
        if (g_used[i] == 1) {
            station_t *s = &g_stations[i];
            if (s->x == x && s->y == y && s->z == z) return s;
        }
    }
    return NULL;
}

station_t *craft_station_create(int x, int y, int z, station_type_t type) {
    station_t *existing = craft_station_get(x, y, z);
    if (existing) {
        memset(existing, 0, sizeof(*existing));
        existing->x = x; existing->y = y; existing->z = z;
        existing->type = (uint8_t)type;
        existing->active = (type != ST_NONE) ? 1 : 0;
        return existing;
    }
    uint64_t key = pack_key(x, y, z);
    uint32_t h = hash_key(key) & (STATION_BUCKETS - 1);
    int first_tomb = -1;
    for (int probe = 0; probe < STATION_BUCKETS; ++probe) {
        uint32_t i = (h + (uint32_t)probe) & (STATION_BUCKETS - 1);
        if (g_used[i] == 1) continue;
        if (g_used[i] == 2) { if (first_tomb < 0) first_tomb = (int)i; continue; }
        uint32_t dst = (first_tomb >= 0) ? (uint32_t)first_tomb : i;
        station_t *s = &g_stations[dst];
        memset(s, 0, sizeof(*s));
        s->x = x; s->y = y; s->z = z;
        s->type = (uint8_t)type;
        s->active = (type != ST_NONE) ? 1 : 0;
        g_used[dst] = 1;
        ++g_station_count;
        return s;
    }
    return NULL;
}

void craft_station_remove(int x, int y, int z) {
    uint64_t key = pack_key(x, y, z);
    uint32_t h = hash_key(key) & (STATION_BUCKETS - 1);
    for (int probe = 0; probe < STATION_BUCKETS; ++probe) {
        uint32_t i = (h + (uint32_t)probe) & (STATION_BUCKETS - 1);
        if (g_used[i] == 0) return;
        if (g_used[i] == 1) {
            station_t *s = &g_stations[i];
            if (s->x == x && s->y == y && s->z == z) {
                memset(s, 0, sizeof(*s));
                g_used[i] = 2;
                if (g_station_count > 0) --g_station_count;
                return;
            }
        }
    }
}

int craft_stations_count(void) {
    return g_station_count;
}

station_t *craft_station_at_index(int idx) {
    if (idx < 0) return NULL;
    int seen = 0;
    for (int i = 0; i < STATION_BUCKETS; ++i) {
        if (g_used[i] != 1) continue;
        if (seen == idx) return &g_stations[i];
        ++seen;
    }
    return NULL;
}

int craft_station_is_lit(int x, int y, int z) {
    station_t *s = craft_station_get(x, y, z);
    if (!s) return 0;
    if (s->type == ST_FURNACE) return s->u.furnace.burn_left > 0.0f;
    if (s->type == ST_FORGE)   return s->u.forge.burn_left > 0.0f;
    return 0;
}

static int out_can_accept(const inv_slot_t *out, item_id id) {
    if (out->id == 0 || out->count <= 0) return 1;
    if (out->id != id) return 0;
    return out->count < item_max_stack(id);
}

static void out_add_one(inv_slot_t *out, item_id id) {
    if (out->id == 0 || out->count <= 0) {
        out->id = id;
        out->count = 1;
        out->durability = 0;
    } else {
        out->count = (int16_t)(out->count + 1);
    }
}

static int brew_upgrade_bottle(inv_slot_t *bot, item_id ing, int apply) {
    if (slot_empty(bot) || !item_is_drink_potion(bot->id)) return 0;
    if (!item_get(bot->id)->food_effect) return 0;
    if (ing == BLOCK_GLOWSTONE) {
        int amp = potion_meta_amp(bot->durability);
        if (amp >= 2) return 0;
        if (apply) bot->durability = potion_meta_make(amp + 1, potion_meta_durtier(bot->durability));
        return 1;
    }
    if (ing == ITEM_REDSTONE) {
        int dt = potion_meta_durtier(bot->durability);
        if (dt >= 3) return 0;
        if (apply) bot->durability = potion_meta_make(potion_meta_amp(bot->durability), dt + 1);
        return 1;
    }
    return 0;
}

void craft_stations_tick(world_t *world, float dt, float smelt_mult) {
    (void)world;
    if (dt <= 0.0f) return;
    if (smelt_mult <= 0.0f) smelt_mult = 1.0f;
    float need = 9.0f / smelt_mult;

    for (int i = 0; i < STATION_BUCKETS; ++i) {
        if (g_used[i] != 1) continue;
        station_t *s = &g_stations[i];

        if (s->type == ST_FURNACE) {
            furnace_state_t *f = &s->u.furnace;
            inv_slot_t *in  = &f->slot[0];
            inv_slot_t *fuel = &f->slot[1];
            inv_slot_t *out = &f->slot[2];

            int in_smeltable = (!slot_empty(in)) && item_smelts_to(in->id) != 0;
            item_id result = in_smeltable ? item_smelts_to(in->id) : 0;
            int out_ok = in_smeltable && out_can_accept(out, result);

            if (f->burn_left <= 0.0f && in_smeltable && out_ok &&
                !slot_empty(fuel) && item_fuel_secs(fuel->id) > 0.0f) {
                float secs = item_fuel_secs(fuel->id);
                if (--fuel->count <= 0) { fuel->id = 0; fuel->count = 0; }
                f->burn_max = secs;
                f->burn_left = secs;
            }

            if (f->burn_left > 0.0f) {
                f->burn_left -= dt;
                if (f->burn_left < 0.0f) f->burn_left = 0.0f;

                if (in_smeltable && out_ok) {
                    f->cook_progress += dt;
                    if (f->cook_progress >= need) {
                        out_add_one(out, result);
                        const item_props_t *ip = item_get(in->id);
                        if (--in->count <= 0) { in->id = 0; in->count = 0; }
                        f->xp_bank += (float)ip->smelt_xp_milli / 1000.0f;
                        f->cook_progress -= need;
                        if (f->cook_progress < 0.0f) f->cook_progress = 0.0f;
                    }
                } else {
                    f->cook_progress = 0.0f;
                }
            } else {
                f->cook_progress = 0.0f;
            }
        } else if (s->type == ST_FORGE) {
            forge_state_t *fo = &s->u.forge;
            if (fo->burn_left > 0.0f) {
                fo->burn_left -= dt;
                if (fo->burn_left < 0.0f) fo->burn_left = 0.0f;
            }
        } else if (s->type == ST_BREWING) {
            brewing_state_t *bs = &s->u.brewing;
            inv_slot_t *ing  = &bs->slot[0];
            inv_slot_t *fuel = &bs->slot[1];

            int brewable = 0;
            if (!slot_empty(ing)) {
                for (int k = 2; k < 5; ++k) {
                    if (slot_empty(&bs->slot[k])) continue;
                    if (brew_result(bs->slot[k].id, ing->id) != 0 ||
                        brew_upgrade_bottle(&bs->slot[k], ing->id, 0)) { brewable = 1; break; }
                }
            }

            if (bs->fuel == 0 && brewable && !slot_empty(fuel) && fuel->id == ITEM_BLAZE_POWDER) {
                if (--fuel->count <= 0) { fuel->id = 0; fuel->count = 0; }
                bs->fuel = 20;
            }

            if (brewable && bs->fuel > 0) {
                if (bs->time_max <= 0.0f) { bs->time_max = 20.0f; bs->time_left = 20.0f; }
                bs->time_left -= dt;
                if (bs->time_left <= 0.0f) {
                    int brewed = 0;
                    for (int k = 2; k < 5; ++k) {
                        if (slot_empty(&bs->slot[k])) continue;
                        item_id r = brew_result(bs->slot[k].id, ing->id);
                        if (r) { bs->slot[k].id = r; bs->slot[k].count = 1; bs->slot[k].durability = 0; brewed = 1; }
                        else if (brew_upgrade_bottle(&bs->slot[k], ing->id, 1)) brewed = 1;
                    }
                    if (brewed) {
                        if (--ing->count <= 0) { ing->id = 0; ing->count = 0; }
                        if (bs->fuel > 0) bs->fuel--;
                    }
                    bs->time_left = 0.0f; bs->time_max = 0.0f;
                }
            } else {
                bs->time_left = 0.0f; bs->time_max = 0.0f;
            }
        }
    }
}

item_id brew_result(item_id in, item_id ing) {
    if (in == ITEM_WATER_BOTTLE && ing == ITEM_NETHER_WART)          return ITEM_AWKWARD_POTION;
    if (in == ITEM_WATER_BOTTLE && ing == ITEM_FERMENTED_SPIDER_EYE) return ITEM_POTION_WEAKNESS;
    if (in == ITEM_AWKWARD_POTION) {
        switch (ing) {
            case ITEM_SUGAR:            return ITEM_POTION_SWIFTNESS;
            case ITEM_BLAZE_POWDER:     return ITEM_POTION_STRENGTH;
            case ITEM_GHAST_TEAR:       return ITEM_POTION_REGEN;
            case ITEM_MAGMA_CREAM:      return ITEM_POTION_FIRE_RES;
            case ITEM_SPIDER_EYE:       return ITEM_POTION_POISON;
            case ITEM_GLISTERING_MELON: return ITEM_POTION_HEALING;
            case ITEM_PUFFERFISH:       return ITEM_POTION_WATER_BREATH;
            default: break;
        }
    }
    if (ing == ITEM_FERMENTED_SPIDER_EYE) {
        switch (in) {
            case ITEM_POTION_HEALING:   return ITEM_POTION_HARMING;
            case ITEM_POTION_POISON:    return ITEM_POTION_HARMING;
            case ITEM_POTION_SWIFTNESS: return ITEM_POTION_SLOWNESS;
            case ITEM_POTION_STRENGTH:  return ITEM_POTION_WEAKNESS;
            case ITEM_POTION_REGEN:     return ITEM_POTION_POISON;
            default: break;
        }
    }
    if (ing == ITEM_GUNPOWDER && item_is_drink_potion(in))
        return (item_id)(in + POTION_SPLASH_OFFSET);
    return 0;
}

static int slot_is_repairable(const inv_slot_t *s) {
    if (slot_empty(s)) return 0;
    return item_max_durability(s->id) > 0;
}

int anvil_combine(inv_slot_t left, inv_slot_t right, inv_slot_t *out, int *xp_cost) {
    if (!out || !xp_cost) return 0;
    if (!slot_is_repairable(&left)) return 0;

    int maxd = item_max_durability(left.id);
    int curd = (int)left.durability;
    if (curd > maxd) curd = maxd;

    item_id mat = item_repair_material(left.id);
    if (mat != 0 && !slot_empty(&right) && right.id == mat) {
        if (curd >= maxd) return 0;
        int restore = maxd / 4;
        if (restore < 1) restore = 1;
        int nd = curd + restore;
        if (nd > maxd) nd = maxd;
        *out = left;
        out->durability = (uint16_t)nd;
        out->count = 1;
        *xp_cost = 1;
        return 1;
    }

    if (!slot_empty(&right) && right.id == left.id && slot_is_repairable(&right)) {
        int curr = (int)right.durability;
        if (curr > maxd) curr = maxd;
        if (curd >= maxd && curr >= maxd) return 0;
        int bonus = maxd / 20;
        int combined = curd + curr + bonus;
        if (combined > maxd) combined = maxd;
        *out = left;
        out->durability = (uint16_t)combined;
        out->count = 1;
        *xp_cost = 2;
        return 1;
    }

    return 0;
}

int forge_repair(inv_slot_t *tool, inv_slot_t mat) {
    if (!tool) return 0;
    if (!slot_is_repairable(tool)) return 0;
    item_id rmat = item_repair_material(tool->id);
    if (rmat == 0 || slot_empty(&mat) || mat.id != rmat) return 0;

    int maxd = item_max_durability(tool->id);
    int curd = (int)tool->durability;
    if (curd > maxd) curd = maxd;
    if (curd >= maxd) return 0;
    int restore = maxd / 4;
    if (restore < 1) restore = 1;
    int nd = curd + restore;
    if (nd > maxd) nd = maxd;
    tool->durability = (uint16_t)nd;
    return 1;
}

static int write_u8(FILE *f, uint8_t v)  { return fwrite(&v, 1, 1, f) == 1; }
static int write_u16(FILE *f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF) };
    return fwrite(b, 1, 2, f) == 2;
}
static int write_i16(FILE *f, int16_t v) { return write_u16(f, (uint16_t)v); }
static int write_u32(FILE *f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                     (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 24) & 0xFF) };
    return fwrite(b, 1, 4, f) == 4;
}
static int write_i32(FILE *f, int32_t v) { return write_u32(f, (uint32_t)v); }
static int write_f32(FILE *f, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    return write_u32(f, u);
}

static int read_u8(FILE *f, uint8_t *v)   { return fread(v, 1, 1, f) == 1; }
static int read_u16(FILE *f, uint16_t *v) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    *v = (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
    return 1;
}
static int read_i16(FILE *f, int16_t *v) {
    uint16_t u;
    if (!read_u16(f, &u)) return 0;
    *v = (int16_t)u;
    return 1;
}
static int read_u32(FILE *f, uint32_t *v) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}
static int read_i32(FILE *f, int32_t *v) {
    uint32_t u;
    if (!read_u32(f, &u)) return 0;
    *v = (int32_t)u;
    return 1;
}
static int read_f32(FILE *f, float *v) {
    uint32_t u;
    if (!read_u32(f, &u)) return 0;
    memcpy(v, &u, sizeof(*v));
    return 1;
}

static int write_slot(FILE *f, const inv_slot_t *s) {
    return write_u16(f, s->id) && write_i16(f, s->count) && write_u16(f, s->durability);
}
static int read_slot(FILE *f, inv_slot_t *s) {
    return read_u16(f, &s->id) && read_i16(f, &s->count) && read_u16(f, &s->durability);
}

#define STATION_SAVE_CAP 4096

int craft_stations_save(void *fp) {
    FILE *f = (FILE *)fp;
    if (!f) return 0;

    uint32_t n = 0;
    for (int i = 0; i < STATION_BUCKETS && n < STATION_SAVE_CAP; ++i)
        if (g_used[i] == 1) ++n;

    if (!write_u32(f, n)) return 0;

    uint32_t written = 0;
    for (int i = 0; i < STATION_BUCKETS && written < n; ++i) {
        if (g_used[i] != 1) continue;
        station_t *s = &g_stations[i];
        if (!write_i32(f, s->x) || !write_i32(f, s->y) || !write_i32(f, s->z)) return 0;
        if (!write_u8(f, s->type)) return 0;

        if (s->type == ST_FURNACE) {
            const furnace_state_t *fu = &s->u.furnace;
            for (int k = 0; k < 3; ++k)
                if (!write_slot(f, &fu->slot[k])) return 0;
            if (!write_f32(f, fu->burn_left) || !write_f32(f, fu->burn_max) ||
                !write_f32(f, fu->cook_progress) || !write_f32(f, fu->xp_bank)) return 0;
        } else if (s->type == ST_FORGE) {
            const forge_state_t *fo = &s->u.forge;
            for (int k = 0; k < 9; ++k)
                if (!write_slot(f, &fo->grid[k])) return 0;
            if (!write_slot(f, &fo->repair_in)) return 0;
            if (!write_slot(f, &fo->repair_mat)) return 0;
            if (!write_f32(f, fo->burn_left) || !write_f32(f, fo->burn_max)) return 0;
        } else if (s->type == ST_CHEST) {
            for (int k = 0; k < 27; ++k)
                if (!write_slot(f, &s->u.chest[k])) return 0;
        } else if (s->type == ST_BREWING) {
            const brewing_state_t *bs = &s->u.brewing;
            for (int k = 0; k < 5; ++k)
                if (!write_slot(f, &bs->slot[k])) return 0;
            if (!write_f32(f, bs->time_left) || !write_f32(f, bs->time_max)) return 0;
            if (!write_u8(f, bs->fuel)) return 0;
        }
        ++written;
    }
    return 1;
}

int craft_stations_load(void *fp, unsigned version) {
    (void)version;
    FILE *f = (FILE *)fp;
    if (!f) return 0;

    uint32_t n = 0;
    if (!read_u32(f, &n)) return 0;
    if (n > STATION_SAVE_CAP) n = STATION_SAVE_CAP;

    for (uint32_t idx = 0; idx < n; ++idx) {
        int32_t x = 0, y = 0, z = 0;
        uint8_t type = 0;
        if (!read_i32(f, &x) || !read_i32(f, &y) || !read_i32(f, &z)) return 0;
        if (!read_u8(f, &type)) return 0;

        station_t *s = craft_station_create((int)x, (int)y, (int)z, (station_type_t)type);

        if (type == ST_FURNACE) {
            furnace_state_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            for (int k = 0; k < 3; ++k)
                if (!read_slot(f, &tmp.slot[k])) return 0;
            if (!read_f32(f, &tmp.burn_left) || !read_f32(f, &tmp.burn_max) ||
                !read_f32(f, &tmp.cook_progress) || !read_f32(f, &tmp.xp_bank)) return 0;
            if (s) s->u.furnace = tmp;
        } else if (type == ST_FORGE) {
            forge_state_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            for (int k = 0; k < 9; ++k)
                if (!read_slot(f, &tmp.grid[k])) return 0;
            if (!read_slot(f, &tmp.repair_in)) return 0;
            if (!read_slot(f, &tmp.repair_mat)) return 0;
            if (!read_f32(f, &tmp.burn_left) || !read_f32(f, &tmp.burn_max)) return 0;
            if (s) s->u.forge = tmp;
        } else if (type == ST_CHEST) {
            inv_slot_t tmp[27];
            memset(tmp, 0, sizeof(tmp));
            for (int k = 0; k < 27; ++k)
                if (!read_slot(f, &tmp[k])) return 0;
            if (s) memcpy(s->u.chest, tmp, sizeof(tmp));
        } else if (type == ST_BREWING) {
            brewing_state_t tmp;
            memset(&tmp, 0, sizeof(tmp));
            for (int k = 0; k < 5; ++k)
                if (!read_slot(f, &tmp.slot[k])) return 0;
            if (!read_f32(f, &tmp.time_left) || !read_f32(f, &tmp.time_max)) return 0;
            if (!read_u8(f, &tmp.fuel)) return 0;
            if (s) s->u.brewing = tmp;
        }
    }
    return 1;
}
