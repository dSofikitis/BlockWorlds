
#include "registry.h"
#include "survival.h"

#include <stdlib.h>
#include <string.h>

const float tool_speed[5] = { 1.0f, 2.0f, 4.0f, 6.0f, 8.0f };

#define BLK(nm, ix, iy, hard, cls, lt, htool, htier, drop)                     \
    { .name=(nm), .rkind=RKIND_BLOCK_CUBE, .icon_tx=(ix), .icon_ty=(iy),       \
      .max_stack=64, .is_placeable=1, .hardness=(hard), .block_class=(cls),    \
      .light=(lt), .harvest_tool=(htool), .harvest_tier=(htier), .drop_id=(drop) }

#define BILLBOARD(nm, ix, iy, hard, lt, drop)                                  \
    { .name=(nm), .rkind=RKIND_CUTOUT_BILLBOARD, .icon_tx=(ix), .icon_ty=(iy), \
      .max_stack=64, .is_placeable=1, .hardness=(hard), .block_class=RCLASS_CUTOUT, \
      .light=(lt), .harvest_tool=TOOL_NONE, .harvest_tier=TIER_NONE, .drop_id=(drop) }

#define MAT(nm, ix, iy)                                                        \
    { .name=(nm), .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=(ix), .icon_ty=(iy), .max_stack=64 }

#define FOOD(nm, ix, iy, hung, sat)                                            \
    { .name=(nm), .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=(ix), .icon_ty=(iy),   \
      .max_stack=64, .is_food=1, .food_hunger=(hung), .food_sat=(sat), .eat_ticks=32 }

#define POTION(nm, ix, iy, eff, ticks, amp)                                    \
    { .name=(nm), .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=(ix), .icon_ty=(iy),   \
      .max_stack=1, .is_food=1, .eat_ticks=24, .food_effect=(eff),             \
      .food_effect_ticks=(ticks), .food_effect_chance=100, .food_effect_amp=(amp) }

#define SPLASH(nm, ix, iy)                                                     \
    { .name=(nm), .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=(ix), .icon_ty=(iy), .max_stack=1 }

#define TOOL(nm, cls, tier, dur, dmg, ix, iy)                                  \
    { .name=(nm), .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=(ix), .icon_ty=(iy),   \
      .max_stack=1, .tool_class=(cls), .tool_tier=(tier), .durability=(dur), .attack_damage=(dmg) }

#define ARMOR(nm, slot, pts, dur, ix, iy)                                      \
    { .name=(nm), .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=(ix), .icon_ty=(iy),   \
      .max_stack=1, .armor_slot=(slot), .armor_points=(pts), .armor_durability=(dur) }

const item_props_t ITEM_PROPS[ITEM_ID_MAX] = {
    [BLOCK_AIR]   = { .name="Air", .max_stack=64, .block_class=RCLASS_EMPTY },
    [BLOCK_STONE] = BLK("Stone", 0,0, 1.5f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_WOOD, BLOCK_COBBLESTONE),
    [BLOCK_DIRT]  = BLK("Dirt", 1,0, 0.5f, RCLASS_SOLID, 0, TOOL_SHOVEL, TIER_NONE, 0),
    [BLOCK_GRASS] = BLK("Grass Block", 2,0, 0.6f, RCLASS_SOLID, 0, TOOL_SHOVEL, TIER_NONE, BLOCK_DIRT),
    [BLOCK_WATER] = { .name="Water", .rkind=RKIND_BLOCK_CUBE, .icon_tx=0, .icon_ty=1, .max_stack=64,
                      .hardness=-1.0f, .block_class=RCLASS_TRANSLUCENT },
    [BLOCK_WATER_SHALLOW] = { .name="Water", .rkind=RKIND_BLOCK_CUBE, .icon_tx=0, .icon_ty=1, .max_stack=64,
                      .hardness=-1.0f, .block_class=RCLASS_TRANSLUCENT },
    [BLOCK_GRAVEL]= BLK("Gravel", 1,1, 0.6f, RCLASS_SOLID, 0, TOOL_SHOVEL, TIER_NONE, 0),
    [BLOCK_DEEPSTONE] = { .name="Deepstone", .rkind=RKIND_BLOCK_CUBE, .icon_tx=2, .icon_ty=1,
                          .max_stack=64, .hardness=-1.0f, .block_class=RCLASS_SOLID },
    [BLOCK_SAND]  = { .name="Sand", .rkind=RKIND_BLOCK_CUBE, .icon_tx=3, .icon_ty=1, .max_stack=64,
                      .is_placeable=1, .hardness=0.5f, .block_class=RCLASS_SOLID,
                      .harvest_tool=TOOL_SHOVEL, .smelts_to=BLOCK_GLASS, .smelt_xp_milli=100 },
    [BLOCK_SNOW]  = BLK("Snow", 0,2, 0.5f, RCLASS_SOLID, 0, TOOL_SHOVEL, TIER_NONE, 0),
    [BLOCK_WOOD]  = { .name="Wood", .rkind=RKIND_BLOCK_CUBE, .icon_tx=3, .icon_ty=2, .max_stack=64,
                      .is_placeable=1, .hardness=2.0f, .block_class=RCLASS_SOLID,
                      .harvest_tool=TOOL_AXE, .smelts_to=ITEM_CHARCOAL, .smelt_xp_milli=150,
                      .fuel_burn_secs=13.5f },
    [BLOCK_LEAVES]= { .name="Leaves", .rkind=RKIND_BLOCK_CUBE, .icon_tx=0, .icon_ty=3, .max_stack=64,
                      .is_placeable=1, .hardness=0.2f, .block_class=RCLASS_CUTOUT },
    [BLOCK_COAL_ORE]    = BLK("Coal Ore", 1,3, 3.0f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_WOOD, ITEM_COAL),
    [BLOCK_IRON_ORE]    = { .name="Iron Ore", .rkind=RKIND_BLOCK_CUBE, .icon_tx=2, .icon_ty=3, .max_stack=64,
                            .is_placeable=1, .hardness=3.0f, .block_class=RCLASS_SOLID,
                            .harvest_tool=TOOL_PICKAXE, .harvest_tier=TIER_STONE, .drop_id=ITEM_RAW_IRON,
                            .smelts_to=ITEM_IRON_INGOT, .smelt_xp_milli=700 },
    [BLOCK_DIAMOND_ORE] = { .name="Diamond Ore", .rkind=RKIND_BLOCK_CUBE, .icon_tx=3, .icon_ty=3, .max_stack=64,
                            .is_placeable=1, .hardness=3.0f, .block_class=RCLASS_SOLID,
                            .harvest_tool=TOOL_PICKAXE, .harvest_tier=TIER_IRON, .drop_id=ITEM_DIAMOND },
    [BLOCK_GLOWSTONE]   = BLK("Glowstone", 4,0, 0.3f, RCLASS_SOLID, 15, TOOL_NONE, TIER_NONE, 0),
    [BLOCK_PLANKS]      = { .name="Planks", .rkind=RKIND_BLOCK_CUBE, .icon_tx=4, .icon_ty=1, .max_stack=64,
                            .is_placeable=1, .hardness=2.0f, .block_class=RCLASS_SOLID,
                            .harvest_tool=TOOL_AXE, .fuel_burn_secs=13.5f },
    [BLOCK_QUARTZ]      = BLK("Quartz", 4,2, 0.8f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_WOOD, 0),
    [BLOCK_CONCRETE]    = BLK("Concrete", 4,3, 1.8f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_WOOD, 0),
    [BLOCK_GLASS]       = { .name="Glass", .rkind=RKIND_BLOCK_CUBE, .icon_tx=0, .icon_ty=4, .max_stack=64,
                            .is_placeable=1, .hardness=0.3f, .block_class=RCLASS_TRANSLUCENT },

    [BLOCK_TORCH]          = { .name="Torch", .rkind=RKIND_CUTOUT_BILLBOARD, .icon_tx=5, .icon_ty=0,
                               .max_stack=64, .is_placeable=1, .hardness=0.0f,
                               .block_class=RCLASS_CUTOUT, .light=14 },
    [BLOCK_CRAFTING_TABLE] = BLK("Crafting Table", 6,0, 2.5f, RCLASS_SOLID, 0, TOOL_AXE, TIER_NONE, 0),
    [BLOCK_FURNACE]        = BLK("Furnace", 8,0, 3.5f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_WOOD, 0),
    [BLOCK_FORGE]          = BLK("Forge", 10,0, 3.5f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_WOOD, 0),
    [BLOCK_ANVIL]          = BLK("Anvil", 11,0, 3.5f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_WOOD, 0),
    [BLOCK_CHEST]          = BLK("Chest", 12,0, 2.5f, RCLASS_SOLID, 0, TOOL_AXE, TIER_NONE, 0),
    [BLOCK_BED_FOOT]       = { .name="Bed", .rkind=RKIND_CUTOUT_BILLBOARD, .icon_tx=13, .icon_ty=0,
                               .max_stack=1, .is_placeable=1, .hardness=0.2f,
                               .block_class=RCLASS_CUTOUT, .drop_id=ITEM_BED },
    [BLOCK_BED_HEAD]       = { .name="Bed", .rkind=RKIND_CUTOUT_BILLBOARD, .icon_tx=14, .icon_ty=0,
                               .max_stack=1, .hardness=0.2f, .block_class=RCLASS_CUTOUT },
    [BLOCK_FARMLAND]       = BLK("Farmland", 15,0, 0.5f, RCLASS_SOLID, 0, TOOL_SHOVEL, TIER_NONE, BLOCK_DIRT),
    [BLOCK_WHEAT_CROP]     = { .name="Wheat", .rkind=RKIND_CUTOUT_BILLBOARD, .icon_tx=5, .icon_ty=1,
                               .max_stack=64, .hardness=0.0f, .block_class=RCLASS_CUTOUT },
    [BLOCK_SAPLING]        = { .name="Sapling", .rkind=RKIND_CUTOUT_BILLBOARD, .icon_tx=6, .icon_ty=1,
                               .max_stack=64, .is_placeable=1, .hardness=0.0f, .block_class=RCLASS_CUTOUT },
    [BLOCK_IRON_BLOCK]     = BLK("Block of Iron", 7,1, 5.0f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_STONE, 0),
    [BLOCK_DIAMOND_BLOCK]  = BLK("Block of Diamond", 8,1, 5.0f, RCLASS_SOLID, 0, TOOL_PICKAXE, TIER_IRON, 0),
    [BLOCK_COAL_BLOCK]     = { .name="Block of Coal", .rkind=RKIND_BLOCK_CUBE, .icon_tx=9, .icon_ty=1,
                               .max_stack=64, .is_placeable=1, .hardness=5.0f, .block_class=RCLASS_SOLID,
                               .harvest_tool=TOOL_PICKAXE, .harvest_tier=TIER_WOOD, .fuel_burn_secs=720.0f },
    [BLOCK_COBBLESTONE]    = { .name="Cobblestone", .rkind=RKIND_BLOCK_CUBE, .icon_tx=10, .icon_ty=1,
                               .max_stack=64, .is_placeable=1, .hardness=2.0f, .block_class=RCLASS_SOLID,
                               .harvest_tool=TOOL_PICKAXE, .harvest_tier=TIER_WOOD,
                               .smelts_to=BLOCK_STONE, .smelt_xp_milli=100 },
    [BLOCK_WOOL]           = BLK("Wool", 11,1, 0.8f, RCLASS_SOLID, 0, TOOL_NONE, TIER_NONE, 0),
    [BLOCK_LEAVES_PINE]    = { .name="Pine Leaves", .rkind=RKIND_BLOCK_CUBE, .icon_tx=12, .icon_ty=1,
                               .max_stack=64, .is_placeable=1, .hardness=0.2f, .block_class=RCLASS_CUTOUT },
    [BLOCK_LEAVES_ACACIA]  = { .name="Acacia Leaves", .rkind=RKIND_BLOCK_CUBE, .icon_tx=13, .icon_ty=1,
                               .max_stack=64, .is_placeable=1, .hardness=0.2f, .block_class=RCLASS_CUTOUT },
    [BLOCK_LEAVES_SWAMP]   = { .name="Swamp Leaves", .rkind=RKIND_BLOCK_CUBE, .icon_tx=14, .icon_ty=1,
                               .max_stack=64, .is_placeable=1, .hardness=0.2f, .block_class=RCLASS_CUTOUT },
    [BLOCK_LEAVES_JUNGLE]  = { .name="Jungle Leaves", .rkind=RKIND_BLOCK_CUBE, .icon_tx=15, .icon_ty=1,
                               .max_stack=64, .is_placeable=1, .hardness=0.2f, .block_class=RCLASS_CUTOUT },
    [BLOCK_TALL_GRASS]     = { .name="Grass", .rkind=RKIND_CUTOUT_BILLBOARD, .icon_tx=5, .icon_ty=2,
                               .max_stack=64, .is_placeable=1, .hardness=0.0f, .block_class=RCLASS_CUTOUT },
    [BLOCK_BREWING_STAND]  = { .name="Brewing Stand", .rkind=RKIND_BLOCK_CUBE, .icon_tx=6, .icon_ty=2,
                               .max_stack=64, .is_placeable=1, .hardness=0.5f, .block_class=RCLASS_SOLID,
                               .harvest_tool=TOOL_PICKAXE, .harvest_tier=TIER_NONE, .drop_id=BLOCK_BREWING_STAND },
    [BLOCK_ICE]            = { .name="Ice", .rkind=RKIND_BLOCK_CUBE, .icon_tx=1, .icon_ty=4, .max_stack=64,
                               .is_placeable=1, .hardness=0.5f, .block_class=RCLASS_TRANSLUCENT,
                               .harvest_tool=TOOL_PICKAXE, .harvest_tier=TIER_NONE, .drop_id=0 },

    [ITEM_STICK]        = { .name="Stick", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=0, .icon_ty=0,
                            .max_stack=64, .fuel_burn_secs=4.5f },
    [ITEM_COAL]         = { .name="Coal", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=1, .icon_ty=0,
                            .max_stack=64, .fuel_burn_secs=72.0f },
    [ITEM_CHARCOAL]     = { .name="Charcoal", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=2, .icon_ty=0,
                            .max_stack=64, .fuel_burn_secs=72.0f },
    [ITEM_RAW_IRON]     = { .name="Raw Iron", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=3, .icon_ty=0,
                            .max_stack=64, .smelts_to=ITEM_IRON_INGOT, .smelt_xp_milli=700 },
    [ITEM_IRON_INGOT]   = MAT("Iron Ingot", 4,0),
    [ITEM_DIAMOND]      = MAT("Diamond", 5,0),
    [ITEM_LEATHER]      = MAT("Leather", 6,0),
    [ITEM_FEATHER]      = MAT("Feather", 7,0),
    [ITEM_BONE]         = MAT("Bone", 8,0),
    [ITEM_STRING]       = MAT("String", 9,0),
    [ITEM_GUNPOWDER]    = MAT("Gunpowder", 10,0),
    [ITEM_WHEAT]        = MAT("Wheat", 11,0),
    [ITEM_SEEDS]        = MAT("Seeds", 12,0),
    [ITEM_FLINT]        = MAT("Flint", 13,0),
    [ITEM_ARROW]        = MAT("Arrow", 14,0),
    [ITEM_BOWL]         = { .name="Bowl", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=15, .icon_ty=0,
                            .max_stack=64, .fuel_burn_secs=13.5f },
    [ITEM_SPIDER_EYE]   = { .name="Spider Eye", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=0, .icon_ty=1,
                            .max_stack=64, .is_food=1, .food_hunger=2, .food_sat=3.2f, .eat_ticks=32,
                            .food_effect=EFFECT_POISON, .food_effect_ticks=80, .food_effect_chance=100 },
    [ITEM_ROTTEN_FLESH] = { .name="Rotten Flesh", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=1, .icon_ty=1,
                            .max_stack=64, .is_food=1, .food_hunger=4, .food_sat=0.0f, .eat_ticks=32,
                            .food_effect=EFFECT_HUNGER, .food_effect_ticks=600, .food_effect_chance=80 },
    [ITEM_BED]          = { .name="Bed", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=2, .icon_ty=1,
                            .max_stack=1, .is_placeable=1, .places_block=BLOCK_BED_FOOT },

    [ITEM_GLASS_BOTTLE] = MAT("Glass Bottle", 3,1),
    [ITEM_WATER_BOTTLE] = { .name="Water Bottle", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=4, .icon_ty=1, .max_stack=1 },
    [ITEM_NETHER_WART]  = MAT("Nether Wart", 5,1),
    [ITEM_BLAZE_ROD]    = { .name="Blaze Rod", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=6, .icon_ty=1,
                            .max_stack=64, .fuel_burn_secs=120.0f },
    [ITEM_BLAZE_POWDER] = MAT("Blaze Powder", 7,1),
    [ITEM_FERMENTED_SPIDER_EYE] = MAT("Fermented Spider Eye", 8,1),
    [ITEM_MAGMA_CREAM]  = MAT("Magma Cream", 9,1),
    [ITEM_GLISTERING_MELON] = MAT("Glistering Melon", 10,1),
    [ITEM_GHAST_TEAR]   = MAT("Ghast Tear", 11,1),
    [ITEM_SUGAR]        = MAT("Sugar", 12,1),
    [ITEM_PUFFERFISH]   = MAT("Pufferfish", 13,1),
    [ITEM_AWKWARD_POTION] = { .name="Awkward Potion", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=14, .icon_ty=1, .max_stack=1 },
    [ITEM_REDSTONE]       = MAT("Redstone", 15,1),
    [ITEM_ARROW_POISON]   = MAT("Poison Arrow", 14,0),
    [ITEM_ARROW_HARMING]  = MAT("Harming Arrow", 14,0),

    [ITEM_APPLE]          = FOOD("Apple", 0,2, 4, 2.4f),
    [ITEM_BREAD]          = FOOD("Bread", 1,2, 5, 6.0f),
    [ITEM_RAW_PORK]       = { .name="Raw Porkchop", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=2, .icon_ty=2,
                              .max_stack=64, .is_food=1, .food_hunger=3, .food_sat=1.8f, .eat_ticks=32,
                              .smelts_to=ITEM_COOKED_PORK, .smelt_xp_milli=350 },
    [ITEM_COOKED_PORK]    = FOOD("Cooked Porkchop", 3,2, 8, 12.8f),
    [ITEM_RAW_BEEF]       = { .name="Raw Beef", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=4, .icon_ty=2,
                              .max_stack=64, .is_food=1, .food_hunger=3, .food_sat=1.8f, .eat_ticks=32,
                              .smelts_to=ITEM_COOKED_BEEF, .smelt_xp_milli=350 },
    [ITEM_COOKED_BEEF]    = FOOD("Steak", 5,2, 8, 12.8f),
    [ITEM_RAW_CHICKEN]    = { .name="Raw Chicken", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=6, .icon_ty=2,
                              .max_stack=64, .is_food=1, .food_hunger=2, .food_sat=1.2f, .eat_ticks=32,
                              .food_effect=EFFECT_POISON, .food_effect_ticks=600, .food_effect_chance=30,
                              .smelts_to=ITEM_COOKED_CHICKEN, .smelt_xp_milli=350 },
    [ITEM_COOKED_CHICKEN] = FOOD("Cooked Chicken", 7,2, 6, 7.2f),
    [ITEM_RAW_MUTTON]     = { .name="Raw Mutton", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=8, .icon_ty=2,
                              .max_stack=64, .is_food=1, .food_hunger=2, .food_sat=1.2f, .eat_ticks=32,
                              .smelts_to=ITEM_COOKED_MUTTON, .smelt_xp_milli=350 },
    [ITEM_COOKED_MUTTON]  = FOOD("Cooked Mutton", 9,2, 6, 9.6f),

    [ITEM_WOOD_PICKAXE]    = TOOL("Wooden Pickaxe", TOOL_PICKAXE, TIER_WOOD, 60, 2, 0,3),
    [ITEM_WOOD_AXE]        = TOOL("Wooden Axe", TOOL_AXE, TIER_WOOD, 60, 7, 1,3),
    [ITEM_WOOD_SHOVEL]     = TOOL("Wooden Shovel", TOOL_SHOVEL, TIER_WOOD, 60, 3, 2,3),
    [ITEM_WOOD_SWORD]      = TOOL("Wooden Sword", TOOL_SWORD, TIER_WOOD, 60, 8, 3,3),
    [ITEM_WOOD_HOE]        = TOOL("Wooden Hoe", TOOL_HOE, TIER_WOOD, 60, 1, 4,3),
    [ITEM_STONE_PICKAXE]   = TOOL("Stone Pickaxe", TOOL_PICKAXE, TIER_STONE, 132, 3, 0,4),
    [ITEM_STONE_AXE]       = TOOL("Stone Axe", TOOL_AXE, TIER_STONE, 132, 9, 1,4),
    [ITEM_STONE_SHOVEL]    = TOOL("Stone Shovel", TOOL_SHOVEL, TIER_STONE, 132, 4, 2,4),
    [ITEM_STONE_SWORD]     = TOOL("Stone Sword", TOOL_SWORD, TIER_STONE, 132, 10, 3,4),
    [ITEM_STONE_HOE]       = TOOL("Stone Hoe", TOOL_HOE, TIER_STONE, 132, 1, 4,4),
    [ITEM_IRON_PICKAXE]    = TOOL("Iron Pickaxe", TOOL_PICKAXE, TIER_IRON, 251, 4, 0,5),
    [ITEM_IRON_AXE]        = TOOL("Iron Axe", TOOL_AXE, TIER_IRON, 251, 11, 1,5),
    [ITEM_IRON_SHOVEL]     = TOOL("Iron Shovel", TOOL_SHOVEL, TIER_IRON, 251, 5, 2,5),
    [ITEM_IRON_SWORD]      = TOOL("Iron Sword", TOOL_SWORD, TIER_IRON, 251, 12, 3,5),
    [ITEM_IRON_HOE]        = TOOL("Iron Hoe", TOOL_HOE, TIER_IRON, 251, 1, 4,5),
    [ITEM_DIAMOND_PICKAXE] = TOOL("Diamond Pickaxe", TOOL_PICKAXE, TIER_DIAMOND, 1562, 5, 0,6),
    [ITEM_DIAMOND_AXE]     = TOOL("Diamond Axe", TOOL_AXE, TIER_DIAMOND, 1562, 13, 1,6),
    [ITEM_DIAMOND_SHOVEL]  = TOOL("Diamond Shovel", TOOL_SHOVEL, TIER_DIAMOND, 1562, 6, 2,6),
    [ITEM_DIAMOND_SWORD]   = TOOL("Diamond Sword", TOOL_SWORD, TIER_DIAMOND, 1562, 14, 3,6),
    [ITEM_DIAMOND_HOE]     = TOOL("Diamond Hoe", TOOL_HOE, TIER_DIAMOND, 1562, 1, 4,6),
    [ITEM_BOW]             = { .name="Bow", .rkind=RKIND_FLAT_ITEM_ICON, .icon_tx=5, .icon_ty=3,
                               .max_stack=1, .durability=385 },

    [ITEM_LEATHER_HELMET] = ARMOR("Leather Helmet", EQ_HELMET, 1, 55, 0,7),
    [ITEM_LEATHER_CHEST]  = ARMOR("Leather Tunic", EQ_CHEST, 3, 80, 1,7),
    [ITEM_LEATHER_LEGS]   = ARMOR("Leather Pants", EQ_LEGS, 2, 75, 2,7),
    [ITEM_LEATHER_BOOTS]  = ARMOR("Leather Boots", EQ_BOOTS, 1, 65, 3,7),
    [ITEM_IRON_HELMET]    = ARMOR("Iron Helmet", EQ_HELMET, 2, 165, 0,8),
    [ITEM_IRON_CHEST]     = ARMOR("Iron Chestplate", EQ_CHEST, 6, 240, 1,8),
    [ITEM_IRON_LEGS]      = ARMOR("Iron Leggings", EQ_LEGS, 5, 225, 2,8),
    [ITEM_IRON_BOOTS]     = ARMOR("Iron Boots", EQ_BOOTS, 2, 195, 3,8),
    [ITEM_DIAMOND_HELMET] = ARMOR("Diamond Helmet", EQ_HELMET, 3, 363, 0,9),
    [ITEM_DIAMOND_CHEST]  = ARMOR("Diamond Chestplate", EQ_CHEST, 8, 528, 1,9),
    [ITEM_DIAMOND_LEGS]   = ARMOR("Diamond Leggings", EQ_LEGS, 6, 495, 2,9),
    [ITEM_DIAMOND_BOOTS]  = ARMOR("Diamond Boots", EQ_BOOTS, 3, 429, 3,9),

    [ITEM_POTION_REGEN]        = POTION("Potion of Regeneration", 0,4, EFFECT_REGENERATION, 900, 0),
    [ITEM_POTION_SWIFTNESS]    = POTION("Potion of Swiftness", 1,4, EFFECT_SPEED, 3600, 0),
    [ITEM_POTION_STRENGTH]     = POTION("Potion of Strength", 2,4, EFFECT_STRENGTH, 3600, 0),
    [ITEM_POTION_HEALING]      = POTION("Potion of Healing", 3,4, EFFECT_INSTANT_HEALTH, 1, 1),
    [ITEM_POTION_POISON]       = POTION("Potion of Poison", 4,4, EFFECT_POISON, 900, 0),
    [ITEM_POTION_FIRE_RES]     = POTION("Potion of Fire Resistance", 5,4, EFFECT_FIRE_RESISTANCE, 3600, 0),
    [ITEM_POTION_WATER_BREATH] = POTION("Potion of Water Breathing", 6,4, EFFECT_WATER_BREATHING, 3600, 0),
    [ITEM_POTION_HARMING]      = POTION("Potion of Harming", 7,4, EFFECT_INSTANT_DAMAGE, 1, 0),
    [ITEM_POTION_SLOWNESS]     = POTION("Potion of Slowness", 8,4, EFFECT_SLOWNESS, 1800, 0),
    [ITEM_POTION_WEAKNESS]     = POTION("Potion of Weakness", 9,4, EFFECT_WEAKNESS, 1800, 0),
    [ITEM_POTION_RESISTANCE]   = POTION("Potion of Resistance", 10,4, EFFECT_RESISTANCE, 3600, 0),

    [ITEM_POTION_REGEN        + POTION_SPLASH_OFFSET] = SPLASH("Splash Regeneration", 0,5),
    [ITEM_POTION_SWIFTNESS    + POTION_SPLASH_OFFSET] = SPLASH("Splash Swiftness", 1,5),
    [ITEM_POTION_STRENGTH     + POTION_SPLASH_OFFSET] = SPLASH("Splash Strength", 2,5),
    [ITEM_POTION_HEALING      + POTION_SPLASH_OFFSET] = SPLASH("Splash Healing", 3,5),
    [ITEM_POTION_POISON       + POTION_SPLASH_OFFSET] = SPLASH("Splash Poison", 4,5),
    [ITEM_POTION_FIRE_RES     + POTION_SPLASH_OFFSET] = SPLASH("Splash Fire Resistance", 5,5),
    [ITEM_POTION_WATER_BREATH + POTION_SPLASH_OFFSET] = SPLASH("Splash Water Breathing", 6,5),
    [ITEM_POTION_HARMING      + POTION_SPLASH_OFFSET] = SPLASH("Splash Harming", 7,5),
    [ITEM_POTION_SLOWNESS     + POTION_SPLASH_OFFSET] = SPLASH("Splash Slowness", 8,5),
    [ITEM_POTION_WEAKNESS     + POTION_SPLASH_OFFSET] = SPLASH("Splash Weakness", 9,5),
    [ITEM_POTION_RESISTANCE   + POTION_SPLASH_OFFSET] = SPLASH("Splash Resistance", 10,5),
};

static const item_props_t INVALID_PROPS = { .name = "?", .max_stack = 64 };

const item_props_t *item_get(item_id id) {
    if (id < ITEM_ID_MAX && ITEM_PROPS[id].name) return &ITEM_PROPS[id];
    return &INVALID_PROPS;
}

const char *item_name(item_id id)       { return item_get(id)->name; }

static void norm_name(const char *s, char *out, size_t n) {
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 1 < n; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == ' ' || c == '-') c = '_';
        out[j++] = c;
    }
    out[j] = 0;
}

item_id item_from_name(const char *s) {
    char want[48]; norm_name(s, want, sizeof want);
    if (want[0] == 0) return 0;
    for (int pass = 0; pass < 2; pass++) {
        int lo = pass == 0 ? 256 : 0;
        int hi = pass == 0 ? ITEM_ID_MAX : 256;
        for (int id = lo; id < hi; id++) {
            if (!ITEM_PROPS[id].name) continue;
            char key[48]; norm_name(ITEM_PROPS[id].name, key, sizeof key);
            if (!strcmp(key, want)) return (item_id)id;
        }
    }
    return 0;
}
int  item_max_stack(item_id id)         { int s = item_get(id)->max_stack; return s > 0 ? s : 64; }
tool_class_t item_tool_class(item_id id){ return item_get(id)->tool_class; }
tool_tier_t  item_tool_tier(item_id id) { return item_get(id)->tool_tier; }
int  item_is_food(item_id id)           { return item_get(id)->is_food; }
int  item_is_armor(item_id id)          { return item_get(id)->armor_slot != EQ_NONE; }
int  item_is_tool(item_id id)           { const item_props_t *p = item_get(id);
                                          return p->tool_class != TOOL_NONE || id == ITEM_BOW; }
int  item_sword_damage(item_id id)      { return item_get(id)->attack_damage; }
item_id item_smelts_to(item_id id)      { return item_get(id)->smelts_to; }
float   item_fuel_secs(item_id id)      { return item_get(id)->fuel_burn_secs; }

int item_max_durability(item_id id) {
    const item_props_t *p = item_get(id);
    if (p->durability) return p->durability;
    if (p->armor_durability) return p->armor_durability;
    return 0;
}

item_id item_repair_material(item_id id) {
    if (item_is_tool(id)) {
        switch (item_get(id)->tool_tier) {
            case TIER_WOOD:    return BLOCK_PLANKS;
            case TIER_STONE:   return BLOCK_COBBLESTONE;
            case TIER_IRON:    return ITEM_IRON_INGOT;
            case TIER_DIAMOND: return ITEM_DIAMOND;
            default:           return 0;
        }
    }
    if (item_is_armor(id)) {
        if (id >= ITEM_LEATHER_HELMET && id <= ITEM_LEATHER_BOOTS) return ITEM_LEATHER;
        if (id >= ITEM_DIAMOND_HELMET && id <= ITEM_DIAMOND_BOOTS) return ITEM_DIAMOND;
        if (id >= ITEM_IRON_HELMET    && id <= ITEM_IRON_BOOTS)    return ITEM_IRON_INGOT;
    }
    return 0;
}

render_class_t block_render_class(block_t b) {
    switch (b) {
        case BLOCK_AIR:    return RCLASS_EMPTY;
        case BLOCK_WATER:
        case BLOCK_WATER_SHALLOW:
        case BLOCK_GLASS:
        case BLOCK_ICE:    return RCLASS_TRANSLUCENT;
        case BLOCK_LEAVES:
        case BLOCK_LEAVES_PINE:
        case BLOCK_LEAVES_ACACIA:
        case BLOCK_LEAVES_SWAMP:
        case BLOCK_LEAVES_JUNGLE:
        case BLOCK_TALL_GRASS:
        case BLOCK_TORCH:
        case BLOCK_SAPLING:
        case BLOCK_WHEAT_CROP:
        case BLOCK_BED_FOOT:
        case BLOCK_BED_HEAD: return RCLASS_CUTOUT;
        default:             return RCLASS_SOLID;
    }
}

void block_avg_color(block_t b, float *r, float *g, float *bb) {
    float cr = 0.5f, cg = 0.5f, cb = 0.5f;
    switch (b) {
        case BLOCK_STONE:        cr=0.51f; cg=0.51f; cb=0.51f; break;
        case BLOCK_DIRT:         cr=0.53f; cg=0.37f; cb=0.20f; break;
        case BLOCK_GRASS:        cr=0.35f; cg=0.62f; cb=0.30f; break;
        case BLOCK_GRAVEL:       cr=0.46f; cg=0.44f; cb=0.42f; break;
        case BLOCK_DEEPSTONE:    cr=0.28f; cg=0.28f; cb=0.30f; break;
        case BLOCK_SAND:         cr=0.85f; cg=0.79f; cb=0.55f; break;
        case BLOCK_SNOW:         cr=0.92f; cg=0.95f; cb=0.98f; break;
        case BLOCK_WOOD:         cr=0.45f; cg=0.33f; cb=0.18f; break;
        case BLOCK_LEAVES:       cr=0.25f; cg=0.50f; cb=0.20f; break;
        case BLOCK_LEAVES_PINE:  cr=0.12f; cg=0.29f; cb=0.20f; break;
        case BLOCK_LEAVES_ACACIA:cr=0.54f; cg=0.60f; cb=0.23f; break;
        case BLOCK_LEAVES_SWAMP: cr=0.29f; cg=0.35f; cb=0.16f; break;
        case BLOCK_LEAVES_JUNGLE:cr=0.12f; cg=0.48f; cb=0.16f; break;
        case BLOCK_TALL_GRASS:   cr=0.35f; cg=0.66f; cb=0.30f; break;
        case BLOCK_COAL_ORE:     cr=0.30f; cg=0.30f; cb=0.31f; break;
        case BLOCK_IRON_ORE:     cr=0.60f; cg=0.52f; cb=0.44f; break;
        case BLOCK_DIAMOND_ORE:  cr=0.42f; cg=0.66f; cb=0.66f; break;
        case BLOCK_GLOWSTONE:    cr=0.95f; cg=0.85f; cb=0.45f; break;
        case BLOCK_PLANKS:       cr=0.66f; cg=0.50f; cb=0.31f; break;
        case BLOCK_QUARTZ:       cr=0.92f; cg=0.90f; cb=0.86f; break;
        case BLOCK_CONCRETE:     cr=0.55f; cg=0.55f; cb=0.56f; break;
        case BLOCK_GLASS:        cr=0.80f; cg=0.89f; cb=0.93f; break;
        case BLOCK_WATER:        cr=0.10f; cg=0.30f; cb=0.55f; break;
        case BLOCK_WATER_SHALLOW:cr=0.22f; cg=0.55f; cb=0.62f; break;
        case BLOCK_ICE:          cr=0.78f; cg=0.88f; cb=0.96f; break;
        case BLOCK_CRAFTING_TABLE: cr=0.55f; cg=0.40f; cb=0.24f; break;
        case BLOCK_FURNACE: case BLOCK_FORGE: cr=0.40f; cg=0.40f; cb=0.42f; break;
        case BLOCK_ANVIL:        cr=0.30f; cg=0.30f; cb=0.32f; break;
        case BLOCK_CHEST:        cr=0.55f; cg=0.40f; cb=0.22f; break;
        case BLOCK_IRON_BLOCK:   cr=0.85f; cg=0.85f; cb=0.87f; break;
        case BLOCK_DIAMOND_BLOCK:cr=0.42f; cg=0.84f; cb=0.82f; break;
        case BLOCK_COAL_BLOCK:   cr=0.12f; cg=0.12f; cb=0.13f; break;
        case BLOCK_COBBLESTONE:  cr=0.46f; cg=0.46f; cb=0.47f; break;
        case BLOCK_WOOL:         cr=0.93f; cg=0.93f; cb=0.92f; break;
        case BLOCK_FARMLAND:     cr=0.36f; cg=0.24f; cb=0.13f; break;
        default: break;
    }
    if (r) *r = cr; if (g) *g = cg; if (bb) *bb = cb;
}

int block_light_emission(block_t b) {
    switch (b) {
        case BLOCK_GLOWSTONE: return 15;
        case BLOCK_TORCH:     return 14;
        default:              return 0;
    }
}

float block_render_flags(block_t b) {
    float f = 0.0f;
    if (block_light_emission(b) >= 14) f += RF_EMISSIVE;
    switch (b) {
        case BLOCK_WATER:     f += RF_ALPHA70; break;
        case BLOCK_WATER_SHALLOW: f += RF_ALPHA70 + RF_SHALLOW; break;
        case BLOCK_GLASS:     f += RF_ALPHA45; break;
        case BLOCK_ICE:       f += RF_ALPHA70; break;
        case BLOCK_LEAVES:
        case BLOCK_LEAVES_PINE:
        case BLOCK_LEAVES_ACACIA:
        case BLOCK_LEAVES_SWAMP:
        case BLOCK_LEAVES_JUNGLE:
        case BLOCK_TALL_GRASS:
        case BLOCK_TORCH:
        case BLOCK_SAPLING:
        case BLOCK_WHEAT_CROP:
        case BLOCK_BED_FOOT:
        case BLOCK_BED_HEAD:  f += RF_CUTOUT; break;
        default: break;
    }
    return f;
}

float block_break_seconds(block_t b, item_id held_tool, int creative) {
    if (creative) return 0.0f;
    const item_props_t *bp = item_get(b);
    float hardness = bp->hardness;
    if (hardness < 0.0f) return 1e9f;
    if (hardness == 0.0f) return 0.0f;
    const item_props_t *tp = item_get(held_tool);
    int proper = (tp->tool_class != TOOL_NONE && tp->tool_class == bp->harvest_tool);
    float mult = proper ? tool_speed[tp->tool_tier] : 1.0f;
    float t = hardness / mult;
    if (!proper && bp->harvest_tier > TIER_NONE) t *= 3.33f;
    return t;
}

static int harvest_ok(block_t b, item_id held_tool) {
    const item_props_t *bp = item_get(b);
    if (bp->harvest_tier == TIER_NONE) return 1;
    const item_props_t *tp = item_get(held_tool);
    return tp->tool_class == bp->harvest_tool && tp->tool_tier >= bp->harvest_tier;
}

int block_drops(block_t b, item_id held_tool, item_drop_t *out, int max_out) {
    if (max_out <= 0) return 0;
    const item_props_t *bp = item_get(b);
    if (!harvest_ok(b, held_tool)) return 0;

    int n = 0;
    switch (b) {
        case BLOCK_LEAVES:
        case BLOCK_LEAVES_PINE:
        case BLOCK_LEAVES_ACACIA:
        case BLOCK_LEAVES_SWAMP:
        case BLOCK_LEAVES_JUNGLE:
            if ((rand() % 100) < 5 && n < max_out) { out[n].id = BLOCK_SAPLING; out[n].count = 1; n++; }
            if ((rand() % 100) < 2 && n < max_out) { out[n].id = ITEM_APPLE;    out[n].count = 1; n++; }
            return n;
        case BLOCK_TALL_GRASS:
            if ((rand() % 100) < 12 && n < max_out) { out[n].id = ITEM_SEEDS; out[n].count = 1; n++; }
            return n;
        case BLOCK_GRAVEL:
            out[0].id = ((rand() % 100) < 10) ? (item_id)ITEM_FLINT : (item_id)BLOCK_GRAVEL;
            out[0].count = 1;
            return 1;
        case BLOCK_WHEAT_CROP:
            out[0].id = ITEM_WHEAT; out[0].count = 1; n = 1;
            if (n < max_out) { out[n].id = ITEM_SEEDS; out[n].count = 1 + rand() % 2; n++; }
            return n;
        case BLOCK_GLASS:
        case BLOCK_BED_HEAD:
            return 0;
        default: break;
    }

    item_id drop = bp->drop_id ? bp->drop_id : (item_id)b;
    if (drop == BLOCK_AIR) return 0;
    int dmin = bp->drop_min ? bp->drop_min : 1;
    int dmax = bp->drop_max ? bp->drop_max : dmin;
    int count = (dmax > dmin) ? dmin + rand() % (dmax - dmin + 1) : dmin;
    out[0].id = drop;
    out[0].count = count;
    return 1;
}
