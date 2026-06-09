#pragma once

#include <stdint.h>
#include "chunk.h"

typedef uint16_t item_id;

#define ITEM_ID_MAX 1024

#define ITEM_STICK         256
#define ITEM_COAL          257
#define ITEM_CHARCOAL      258
#define ITEM_RAW_IRON      259
#define ITEM_IRON_INGOT    260
#define ITEM_DIAMOND       261
#define ITEM_LEATHER       262
#define ITEM_FEATHER       263
#define ITEM_BONE          264
#define ITEM_STRING        265
#define ITEM_GUNPOWDER     266
#define ITEM_WHEAT         267
#define ITEM_SEEDS         268
#define ITEM_FLINT         269
#define ITEM_ARROW         270
#define ITEM_BOWL          271
#define ITEM_SPIDER_EYE    272
#define ITEM_ROTTEN_FLESH  273
#define ITEM_BED           274

#define ITEM_GLASS_BOTTLE         275
#define ITEM_WATER_BOTTLE         276
#define ITEM_NETHER_WART          277
#define ITEM_BLAZE_ROD            278
#define ITEM_BLAZE_POWDER         279
#define ITEM_FERMENTED_SPIDER_EYE 280
#define ITEM_MAGMA_CREAM          281
#define ITEM_GLISTERING_MELON     282
#define ITEM_GHAST_TEAR           283
#define ITEM_SUGAR                284
#define ITEM_PUFFERFISH           285
#define ITEM_AWKWARD_POTION       286
#define ITEM_REDSTONE             287
#define ITEM_ARROW_POISON         298
#define ITEM_ARROW_HARMING        299

#define ITEM_APPLE           288
#define ITEM_BREAD           289
#define ITEM_RAW_PORK        290
#define ITEM_COOKED_PORK     291
#define ITEM_RAW_BEEF        292
#define ITEM_COOKED_BEEF     293
#define ITEM_RAW_CHICKEN     294
#define ITEM_COOKED_CHICKEN  295
#define ITEM_RAW_MUTTON      296
#define ITEM_COOKED_MUTTON   297

#define ITEM_WOOD_PICKAXE    320
#define ITEM_WOOD_AXE        321
#define ITEM_WOOD_SHOVEL     322
#define ITEM_WOOD_SWORD      323
#define ITEM_WOOD_HOE        324
#define ITEM_STONE_PICKAXE   328
#define ITEM_STONE_AXE       329
#define ITEM_STONE_SHOVEL    330
#define ITEM_STONE_SWORD     331
#define ITEM_STONE_HOE       332
#define ITEM_IRON_PICKAXE    336
#define ITEM_IRON_AXE        337
#define ITEM_IRON_SHOVEL     338
#define ITEM_IRON_SWORD      339
#define ITEM_IRON_HOE        340
#define ITEM_DIAMOND_PICKAXE 344
#define ITEM_DIAMOND_AXE     345
#define ITEM_DIAMOND_SHOVEL  346
#define ITEM_DIAMOND_SWORD   347
#define ITEM_DIAMOND_HOE     348
#define ITEM_BOW             352

#define ITEM_LEATHER_HELMET  360
#define ITEM_LEATHER_CHEST   361
#define ITEM_LEATHER_LEGS    362
#define ITEM_LEATHER_BOOTS   363
#define ITEM_DIAMOND_HELMET  364
#define ITEM_DIAMOND_CHEST   365
#define ITEM_DIAMOND_LEGS    366
#define ITEM_DIAMOND_BOOTS   367
#define ITEM_IRON_HELMET     368
#define ITEM_IRON_CHEST      369
#define ITEM_IRON_LEGS       370
#define ITEM_IRON_BOOTS      371

#define ITEM_POTION_REGEN        376
#define ITEM_POTION_SWIFTNESS    377
#define ITEM_POTION_STRENGTH     378
#define ITEM_POTION_HEALING      379
#define ITEM_POTION_POISON       380
#define ITEM_POTION_FIRE_RES     381
#define ITEM_POTION_WATER_BREATH 382
#define ITEM_POTION_HARMING      383
#define ITEM_POTION_SLOWNESS     384
#define ITEM_POTION_WEAKNESS     385
#define ITEM_POTION_RESISTANCE   386
#define POTION_DRINK_FIRST       376
#define POTION_DRINK_LAST        386
#define POTION_SPLASH_OFFSET     16

static inline int item_is_drink_potion(item_id id) {
    return id >= POTION_DRINK_FIRST && id <= POTION_DRINK_LAST;
}
static inline int item_is_splash_potion(item_id id) {
    return id >= POTION_DRINK_FIRST + POTION_SPLASH_OFFSET &&
           id <= POTION_DRINK_LAST  + POTION_SPLASH_OFFSET;
}
static inline int item_is_potion(item_id id) {
    return item_is_drink_potion(id) || item_is_splash_potion(id);
}
static inline item_id potion_base(item_id id) {
    return item_is_splash_potion(id) ? (item_id)(id - POTION_SPLASH_OFFSET) : id;
}

static inline int      potion_meta_amp(uint16_t m)     { return (int)(m & 0x3u); }
static inline int      potion_meta_durtier(uint16_t m) { return (int)((m >> 2) & 0x7u); }
static inline uint16_t potion_meta_make(int amp, int durtier) {
    return (uint16_t)(((unsigned)amp & 0x3u) | (((unsigned)durtier & 0x7u) << 2));
}

typedef enum { RCLASS_EMPTY, RCLASS_SOLID, RCLASS_CUTOUT, RCLASS_TRANSLUCENT } render_class_t;
typedef enum { RKIND_BLOCK_CUBE, RKIND_CUTOUT_BILLBOARD, RKIND_FLAT_ITEM_ICON } render_kind_t;
typedef enum { TOOL_NONE, TOOL_PICKAXE, TOOL_AXE, TOOL_SHOVEL, TOOL_SWORD, TOOL_HOE } tool_class_t;
typedef enum { TIER_NONE, TIER_WOOD, TIER_STONE, TIER_IRON, TIER_DIAMOND } tool_tier_t;
typedef enum { EQ_NONE, EQ_HELMET, EQ_CHEST, EQ_LEGS, EQ_BOOTS } armor_slot_t;

#define RF_EMISSIVE 1.0f
#define RF_ALPHA70  2.0f
#define RF_ALPHA45  4.0f
#define RF_CUTOUT   8.0f
#define RF_SHALLOW  16.0f

typedef struct { item_id id; int16_t count; uint16_t durability; } inv_slot_t;

typedef struct {
    const char   *name;
    render_kind_t rkind;
    int16_t       icon_tx, icon_ty;
    int16_t       max_stack;
    int8_t        is_placeable;
    item_id       places_block;
    float         hardness;
    uint8_t       block_class;
    uint8_t       light;
    tool_class_t  harvest_tool;
    tool_tier_t   harvest_tier;
    item_id       drop_id;
    uint8_t       drop_min, drop_max;
    tool_class_t  tool_class;
    tool_tier_t   tool_tier;
    uint16_t      durability;
    uint8_t       attack_damage;
    armor_slot_t  armor_slot;
    uint8_t       armor_points;
    uint16_t      armor_durability;
    int8_t        is_food;
    uint8_t       food_hunger;
    float         food_sat;
    uint8_t       eat_ticks;
    uint8_t       food_effect;
    uint16_t      food_effect_ticks;
    uint8_t       food_effect_chance;
    uint8_t       food_effect_amp;
    item_id       smelts_to;
    float         fuel_burn_secs;
    uint16_t      smelt_xp_milli;
} item_props_t;

extern const item_props_t ITEM_PROPS[ITEM_ID_MAX];
extern const float tool_speed[5];

typedef struct { item_id id; int count; } item_drop_t;

const item_props_t *item_get(item_id id);

static inline int   item_is_block(item_id id) { return id < BLOCK_COUNT && id != BLOCK_AIR; }
static inline int   item_is_item (item_id id) { return id >= 256 && id < ITEM_ID_MAX; }

static inline int   block_passable(block_t b) {
    return b == BLOCK_AIR || block_is_water(b) || b == BLOCK_TORCH ||
           b == BLOCK_SAPLING || b == BLOCK_WHEAT_CROP || b == BLOCK_TALL_GRASS;
}

const char *item_name(item_id id);
item_id item_from_name(const char *s);
int  item_max_stack(item_id id);
int  item_max_durability(item_id id);
int  item_is_tool(item_id id);
int  item_is_armor(item_id id);
int  item_is_food(item_id id);
tool_class_t item_tool_class(item_id id);
tool_tier_t  item_tool_tier(item_id id);
int  item_sword_damage(item_id id);
item_id item_repair_material(item_id id);

float block_break_seconds(block_t b, item_id held_tool, int creative);
int   block_drops(block_t b, item_id held_tool, item_drop_t *out, int max_out);
float block_render_flags(block_t b);
int   block_light_emission(block_t b);
void  block_avg_color(block_t b, float *r, float *g, float *bb);
render_class_t block_render_class(block_t b);
item_id item_smelts_to(item_id id);
float   item_fuel_secs(item_id id);
