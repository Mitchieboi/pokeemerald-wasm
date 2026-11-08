#include "global.h"
#if WASM
#include "mod/mod_ui_types.h"
#include "sprite.h"
#include "decompress.h"
#include "graphics.h"
#include "pokemon.h"
#include "battle.h"
#include "battle_interface.h"
#include "battle_main.h"
#include "battle_util.h"
#include "battle_anim.h"
#include "constants/battle.h"
#include "constants/pokemon.h"

#define TYPE_ICON_COUNT (NUMBER_OF_MON_TYPES + 5) // + contest categories in the sheet
#define TYPE_ICON_TILE_BYTES 0x100               // 32x16 4bpp = 8 tiles
#define TAG_TYPE_GFX_BASE 0xD7C0                 // + type
#define TAG_TYPE_PAL_BASE 0xD7F0                 // + palette index (0..2)

static u8 sGfx[TYPE_ICON_COUNT * TYPE_ICON_TILE_BYTES];
static u16 sPal[3 * 16];
static bool8 sDecompressed;
static struct SpriteTemplate sTemplates[TYPE_ICON_COUNT];

// Which of the three icon palettes each type uses (mirrors the summary screen).
static const u8 sTypePalette[TYPE_ICON_COUNT] =
{
    [TYPE_NORMAL] = 0, [TYPE_FIGHTING] = 0, [TYPE_FLYING] = 1, [TYPE_POISON] = 1, [TYPE_GROUND] = 0,
    [TYPE_ROCK] = 0, [TYPE_BUG] = 2, [TYPE_GHOST] = 1, [TYPE_STEEL] = 0, [TYPE_MYSTERY] = 2,
    [TYPE_FIRE] = 0, [TYPE_WATER] = 1, [TYPE_GRASS] = 2, [TYPE_ELECTRIC] = 0, [TYPE_PSYCHIC] = 1,
    [TYPE_ICE] = 1, [TYPE_DRAGON] = 2, [TYPE_DARK] = 0,
    [NUMBER_OF_MON_TYPES + 0] = 0, [NUMBER_OF_MON_TYPES + 1] = 1, [NUMBER_OF_MON_TYPES + 2] = 1,
    [NUMBER_OF_MON_TYPES + 3] = 2, [NUMBER_OF_MON_TYPES + 4] = 0,
};

static const struct OamData sOam =
{
    .y = 0,
    .affineMode = ST_OAM_AFFINE_OFF,
    .objMode = ST_OAM_OBJ_NORMAL,
    .mosaic = FALSE,
    .bpp = ST_OAM_4BPP,
    .shape = SPRITE_SHAPE(32x16),
    .x = 0,
    .matrixNum = 0,
    .size = SPRITE_SIZE(32x16),
    .tileNum = 0,
    .priority = 1,
    .paletteNum = 0,
    .affineParam = 0,
};

static const union AnimCmd sAnim[] = { ANIMCMD_FRAME(0, 0), ANIMCMD_END };
static const union AnimCmd *const sAnims[] = { sAnim };

static void Decompress(void)
{
    if (sDecompressed)
        return;
    LZDecompressWram(gMoveTypes_Gfx, sGfx);
    LZDecompressWram(gMoveTypes_Pal, sPal);
    sDecompressed = TRUE;
}

void ModTypeIcons_Reset(void)
{
    // Tiles and palettes live in the sprite allocator; a scene that calls
    // ResetSpriteData()/FreeAllSpritePalettes() drops them, and we re-check the
    // tags before every create, so nothing else to do here.
}

static bool8 EnsureLoaded(u8 type)
{
    struct SpriteSheet sheet;
    struct SpritePalette palette;
    u8 pal = sTypePalette[type];

    Decompress();
    if (GetSpriteTileStartByTag(TAG_TYPE_GFX_BASE + type) == 0xFFFF)
    {
        sheet.data = sGfx + type * TYPE_ICON_TILE_BYTES;
        sheet.size = TYPE_ICON_TILE_BYTES;
        sheet.tag = TAG_TYPE_GFX_BASE + type;
        if (LoadSpriteSheet(&sheet) == 0)
            return FALSE;
    }
    if (IndexOfSpritePaletteTag(TAG_TYPE_PAL_BASE + pal) == 0xFF)
    {
        palette.data = sPal + pal * 16;
        palette.tag = TAG_TYPE_PAL_BASE + pal;
        if (LoadSpritePalette(&palette) == 0xFF)
            return FALSE;
    }
    return TRUE;
}

u8 ModTypeIcon_Create(u8 type, s16 x, s16 y, u8 subpriority, u8 oamPriority)
{
    u8 spriteId;

    if (type >= TYPE_ICON_COUNT)
        return SPRITE_NONE;
    if (!EnsureLoaded(type))
        return SPRITE_NONE;

    sTemplates[type].tileTag = TAG_TYPE_GFX_BASE + type;
    sTemplates[type].paletteTag = TAG_TYPE_PAL_BASE + sTypePalette[type];
    sTemplates[type].oam = &sOam;
    sTemplates[type].anims = sAnims;
    sTemplates[type].images = NULL;
    sTemplates[type].affineAnims = gDummySpriteAffineAnimTable;
    sTemplates[type].callback = SpriteCallbackDummy;

    spriteId = CreateSprite(&sTemplates[type], x, y, subpriority);
    if (spriteId != MAX_SPRITES)
        gSprites[spriteId].oam.priority = oamPriority;
    return spriteId == MAX_SPRITES ? SPRITE_NONE : spriteId;
}

void ModTypeIcon_Destroy(u8 *spriteId)
{
    if (*spriteId != SPRITE_NONE && *spriteId < MAX_SPRITES)
    {
        if (gSprites[*spriteId].inUse)
            DestroySprite(&gSprites[*spriteId]);
        *spriteId = SPRITE_NONE;
    }
}

// --- battle health boxes --------------------------------------------------------

#define sHealthboxId data[0]
#define sOffsetX data[1]
#define sOffsetY data[2]
#define sBattler data[3]

static u8 sHealthboxIcons[MAX_BATTLERS_COUNT][2] = { {SPRITE_NONE, SPRITE_NONE}, {SPRITE_NONE, SPRITE_NONE}, {SPRITE_NONE, SPRITE_NONE}, {SPRITE_NONE, SPRITE_NONE} };

static void SpriteCB_FollowHealthbox(struct Sprite *sprite)
{
    struct Sprite *box;

    if (sprite->sBattler >= MAX_BATTLERS_COUNT || gHealthboxSpriteIds[sprite->sBattler] != sprite->sHealthboxId)
    {
        DestroySprite(sprite);
        return;
    }
    box = &gSprites[sprite->sHealthboxId];
    if (!box->inUse)
    {
        DestroySprite(sprite);
        return;
    }
    sprite->x = box->x + sprite->sOffsetX;
    sprite->y = box->y + sprite->sOffsetY;
    sprite->x2 = box->x2;
    sprite->y2 = box->y2;
    sprite->invisible = box->invisible;
    sprite->oam.priority = box->oam.priority;
}

static void HealthboxIconOffsets(u8 battler, u8 index, s16 *dx, s16 *dy)
{
    bool8 player = GetBattlerSide(battler) == B_SIDE_PLAYER;

    if (!IsDoubleBattle())
    {
        // Icons sit just above the box: left-aligned for the opponent, right-aligned for the player.
        *dy = player ? -24 : -19;
        *dx = player ? 30 + index * 34 : -20 + index * 34;
    }
    else
    {
        // Stacked boxes leave no room above/below: put icons beside the box, towards the centre.
        *dy = -4;
        *dx = player ? -96 + index * 34 : 76 + index * 34;
    }
}

void ModTypeIcons_UpdateHealthbox(u8 healthboxSpriteId, struct Pokemon *mon)
{
    u8 battler = gSprites[healthboxSpriteId].data[6]; // hMain_Battler
    u16 species;
    u8 types[2];
    u32 i, count;

    if (battler >= MAX_BATTLERS_COUNT)
        return;
    for (i = 0; i < 2; i++)
        ModTypeIcon_Destroy(&sHealthboxIcons[battler][i]);

    species = GetMonData(mon, MON_DATA_SPECIES);
    if (species == SPECIES_NONE || species >= NUM_SPECIES || GetMonData(mon, MON_DATA_IS_EGG))
        return;
    types[0] = gSpeciesInfo[species].types[0];
    types[1] = gSpeciesInfo[species].types[1];
    count = (types[0] == types[1]) ? 1 : 2;

    for (i = 0; i < count; i++)
    {
        s16 dx, dy;
        u8 id;
        HealthboxIconOffsets(battler, i, &dx, &dy);
        id = ModTypeIcon_Create(types[i], gSprites[healthboxSpriteId].x + dx, gSprites[healthboxSpriteId].y + dy, 0, gSprites[healthboxSpriteId].oam.priority);
        if (id == SPRITE_NONE)
            break;
        gSprites[id].sHealthboxId = healthboxSpriteId;
        gSprites[id].sOffsetX = dx;
        gSprites[id].sOffsetY = dy;
        gSprites[id].sBattler = battler;
        gSprites[id].invisible = gSprites[healthboxSpriteId].invisible;
        gSprites[id].callback = SpriteCB_FollowHealthbox;
        sHealthboxIcons[battler][i] = id;
    }
}

void ModTypeIcons_DestroyHealthbox(u8 healthboxSpriteId)
{
    u32 b, i;
    for (b = 0; b < MAX_BATTLERS_COUNT; b++)
        for (i = 0; i < 2; i++)
            if (sHealthboxIcons[b][i] != SPRITE_NONE && gSprites[sHealthboxIcons[b][i]].sHealthboxId == healthboxSpriteId)
                ModTypeIcon_Destroy(&sHealthboxIcons[b][i]);
}

void ModTypeIcons_ResetBattle(void)
{
    u32 b, i;
    for (b = 0; b < MAX_BATTLERS_COUNT; b++)
        for (i = 0; i < 2; i++)
            sHealthboxIcons[b][i] = SPRITE_NONE;
}

#undef sHealthboxId
#undef sOffsetX
#undef sOffsetY
#undef sBattler

// --- party menu ---------------------------------------------------------------------

static u8 sPartyIcons[PARTY_SIZE][2];

void ModTypeIcons_ResetParty(void)
{
    u32 s, i;
    for (s = 0; s < PARTY_SIZE; s++)
        for (i = 0; i < 2; i++)
            sPartyIcons[s][i] = SPRITE_NONE;
}

void ModTypeIcons_PartySlot(u8 slot, u16 species, s16 x, s16 y, u8 spacing)
{
    u8 types[2];
    u32 i, count;

    if (slot >= PARTY_SIZE)
        return;
    for (i = 0; i < 2; i++)
        ModTypeIcon_Destroy(&sPartyIcons[slot][i]);
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return;
    types[0] = gSpeciesInfo[species].types[0];
    types[1] = gSpeciesInfo[species].types[1];
    count = (types[0] == types[1]) ? 1 : 2;
    for (i = 0; i < count; i++)
    {
        u8 id = ModTypeIcon_Create(types[i], x + i * spacing, y, 1, 0);
        if (id == SPRITE_NONE)
            break;
        sPartyIcons[slot][i] = id;
    }
}

#endif // WASM
