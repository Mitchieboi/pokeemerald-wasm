#include "global.h"
#if WASM
#include "mod/mod_battle_hud.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_interface.h"
#include "battle_main.h"
#include "battle_message.h"
#include "bg.h"
#include "gpu_regs.h"
#include "menu.h"
#include "palette.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_summary_screen.h"
#include "safari_zone.h"
#include "sprite.h"
#include "string_util.h"
#include "strings.h"
#include "text.h"
#include "window.h"
#include "data.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/moves.h"
#include "constants/pokemon.h"
#include "constants/rgb.h"
#include "constants/species.h"

#define HEX(h) RGB(((h) >> 19) & 31, ((h) >> 11) & 31, ((h) >> 3) & 31)

// ---------------------------------------------------------------------------
// Palettes
// ---------------------------------------------------------------------------

// Health card sprite palette (TAG_HEALTHBOX_PAL, and a second copy for battlers 2/3).
enum
{
    C_TRANS,   // 0
    C_BORDER,  // 1 light frame
    C_FILL,    // 2 slate
    C_TEXT,    // 3 white
    C_SHADOW,  // 4 near-black text shadow
    C_DIM,     // 5 secondary text
    C_TRACK,   // 6 bar track
    C_EXP,     // 7 exp bar
    C_PINK,    // 8 female
    C_BLUE,    // 9 male
    C_SLOT0,   // 10..12: even battler (type 1, type 2, status)
    C_SLOT0_T2,
    C_SLOT0_ST,
    C_SLOT1,   // 13..15: odd battler
    C_SLOT1_T2,
    C_SLOT1_ST,
};

const u16 gModHudCardPalette[16] =
{
    [C_TRANS] = 0,
    [C_BORDER] = HEX(0xeef1f4),
    [C_FILL] = HEX(0x2a3140),
    [C_TEXT] = HEX(0xf4f6f8),
    [C_SHADOW] = HEX(0x141820),
    [C_DIM] = HEX(0xaab4c2),
    [C_TRACK] = HEX(0x151a22),
    [C_EXP] = HEX(0x4fa3ff),
    [C_PINK] = HEX(0xff8aa8),
    [C_BLUE] = HEX(0x7fc0ff),
    [C_SLOT0] = HEX(0x9a9a7a), [C_SLOT0_T2] = HEX(0x9a9a7a), [C_SLOT0_ST] = HEX(0xe85a5a),
    [C_SLOT1] = HEX(0x9a9a7a), [C_SLOT1_T2] = HEX(0x9a9a7a), [C_SLOT1_ST] = HEX(0xe85a5a),
};

// Health bar sprite palette (TAG_HEALTHBAR_PAL)
enum { B_TRANS, B_BORDER, B_TRACK, B_GREEN, B_YELLOW, B_RED, B_SHINE };
const u16 gModHudBarPalette[16] =
{
    [B_TRANS] = 0,
    [B_BORDER] = HEX(0x141820),
    [B_TRACK] = HEX(0x151a22),
    [B_GREEN] = HEX(0x5ad87a),
    [B_YELLOW] = HEX(0xe4c022),
    [B_RED] = HEX(0xe85a5a),
    [B_SHINE] = HEX(0x8ff0a8),
};

// BG palette 0: message window text (fg 1, shadow 6, bg 15) and the panel frame tiles.
enum { P0_FRAME_DIVIDER = 13, P0_FRAME_BORDER = 14, P0_FRAME_FILL = 15 };
const u16 gModHudTextboxPalette[16] =
{
    [0] = 0,
    [1] = HEX(0xf4f6f8),   // message text
    [2] = HEX(0xff6b6b),   // (stock: red)
    [3] = HEX(0x4fa3ff),   // (stock: blue)
    [4] = HEX(0x6a8bbd),
    [5] = HEX(0xffffff),
    [6] = HEX(0x141820),   // message text shadow
    [7] = HEX(0xd5cdd5),
    [8] = HEX(0x414a7b),
    [9] = HEX(0x4a4152),
    [10] = HEX(0x838b8b),
    [11] = HEX(0x6a415a),
    [12] = HEX(0xac6a62),
    [P0_FRAME_DIVIDER] = HEX(0x49536a),
    [P0_FRAME_BORDER] = HEX(0xeef1f4),
    [P0_FRAME_FILL] = HEX(0x2a3140),
};

// BG palette 5: menus. 13/14/15 keep the stock meaning (dark text / light bg / shadow)
// because the yes/no box and level-up box still use them.
enum
{
    M_BORDER = 1, M_FILL, M_TEXT, M_SHADOW, M_DIM, M_CHIP, M_DIVIDER, M_SEL_TEXT, M_TYPE, M_SEL_FILL, M_PP_LOW, M_PP_NONE,
    M_STD_TEXT = 13, M_STD_BG = 14, M_STD_SHADOW = 15,
};
const u16 gModHudMenuPalette[16] =
{
    [0] = 0,
    [M_BORDER] = HEX(0xeef1f4),
    [M_FILL] = HEX(0x2a3140),
    [M_TEXT] = HEX(0xf4f6f8),
    [M_SHADOW] = HEX(0x141820),
    [M_DIM] = HEX(0xaab4c2),
    [M_CHIP] = HEX(0x3a4356),
    [M_DIVIDER] = HEX(0x49536a),
    [M_SEL_TEXT] = HEX(0x20262f),
    [M_TYPE] = HEX(0x9a9a7a),
    [M_SEL_FILL] = HEX(0xeef1f4),
    [M_PP_LOW] = HEX(0xe4c022),
    [M_PP_NONE] = HEX(0xe85a5a),
    [M_STD_TEXT] = HEX(0x4a4a4a),
    [M_STD_BG] = HEX(0xffffff),
    [M_STD_SHADOW] = HEX(0xd5d5cd),
};

static const u16 sTypeColors[NUMBER_OF_MON_TYPES] =
{
    [TYPE_NORMAL] = HEX(0x9a9a7a), [TYPE_FIGHTING] = HEX(0xb8382c), [TYPE_FLYING] = HEX(0x8f84d8),
    [TYPE_POISON] = HEX(0x9a4aa3), [TYPE_GROUND] = HEX(0xd1a84a), [TYPE_ROCK] = HEX(0xa89248),
    [TYPE_BUG] = HEX(0x8fa82a), [TYPE_GHOST] = HEX(0x5e5a93), [TYPE_STEEL] = HEX(0x9ea3b4),
    [TYPE_MYSTERY] = HEX(0x6a7a6a), [TYPE_FIRE] = HEX(0xe8702a), [TYPE_WATER] = HEX(0x4f8bdc),
    [TYPE_GRASS] = HEX(0x5fb34a), [TYPE_ELECTRIC] = HEX(0xe4c022), [TYPE_PSYCHIC] = HEX(0xe85a8a),
    [TYPE_ICE] = HEX(0x7fc7d6), [TYPE_DRAGON] = HEX(0x6a4ae0), [TYPE_DARK] = HEX(0x5a4a3e),
};

static const u16 sStatusColors[] =
{
    HEX(0xb05ab8), // PSN
    HEX(0xe4c022), // PAR
    HEX(0xaab4c2), // SLP
    HEX(0x7fc7d6), // FRZ
    HEX(0xe8702a), // BRN
};
static const u8 sText_PSN[] = _("PSN");
static const u8 sText_PAR[] = _("PAR");
static const u8 sText_SLP[] = _("SLP");
static const u8 sText_FRZ[] = _("FRZ");
static const u8 sText_BRN[] = _("BRN");
static const u8 sText_HP[] = _("HP");
static const u8 sText_EXP[] = _("EXP");
static const u8 sText_Slash[] = _("/");
static const u8 sText_PP[] = _("PP ");
static const u8 sText_POW[] = _("POW ");
static const u8 sText_ACC[] = _("ACC ");
static const u8 sText_Percent[] = _("%");
static const u8 sText_NoValue[] = _("--");
static const u8 sText_Dash[] = _("-");
static const u8 sText_SwitchWhich[] = _("Switch which?");
static const u8 sText_Fight[] = _("FIGHT");
static const u8 sText_Bag[] = _("BAG");
static const u8 sText_Pokemon[] = _("POKéMON");
static const u8 sText_Run[] = _("RUN");
static const u8 sText_Ball[] = _("BALL");
static const u8 sText_Pokeblock[] = _("{POKEBLOCK}");
static const u8 sText_GoNear[] = _("GO NEAR");
static const u8 sText_SafariBallsTitle[] = _("SAFARI BALLS");
static const u8 sText_Left[] = _("Left: ");

// ---------------------------------------------------------------------------
// HP bar art (procedural): rows 1..5 of each 8x8 tile hold a 1px-bordered bar.
// ---------------------------------------------------------------------------

static u8 sBarTiles[3][9][32];
static u8 sBarCapTiles[64];
static bool8 sBarTilesBuilt;

static void SetTilePixel(u8 *tile, u32 x, u32 y, u32 color)
{
    u8 *p = &tile[y * 4 + x / 2];
    if (x & 1)
        *p = (*p & 0x0F) | (color << 4);
    else
        *p = (*p & 0xF0) | color;
}

static void BuildBarTiles(void)
{
    u32 c, f, x, y;
    static const u8 fillColor[3] = { B_GREEN, B_YELLOW, B_RED };

    if (sBarTilesBuilt)
        return;
    for (c = 0; c < 3; c++)
    {
        for (f = 0; f <= 8; f++)
        {
            u8 *t = sBarTiles[c][f];
            for (y = 0; y < 32; y++)
                t[y] = 0;
            for (x = 0; x < 8; x++)
            {
                SetTilePixel(t, x, 1, B_BORDER);
                SetTilePixel(t, x, 5, B_BORDER);
                for (y = 2; y <= 4; y++)
                    SetTilePixel(t, x, y, x < f ? fillColor[c] : B_TRACK);
                if (x < f)
                    SetTilePixel(t, x, 2, B_SHINE);
            }
        }
    }
    // cap tiles: tile 0 empty, tile 1 has the bar's left edge in its last column
    for (y = 0; y < 64; y++)
        sBarCapTiles[y] = 0;
    for (y = 1; y <= 5; y++)
        SetTilePixel(&sBarCapTiles[32], 7, y, B_BORDER);
    sBarTilesBuilt = TRUE;
}

const u8 *ModHud_HpBarTile(u8 barElementId, u8 filled)
{
    u32 c = 0;
    BuildBarTiles();
    if (barElementId == MODHUD_GFX_HP_BAR_YELLOW)
        c = 1;
    else if (barElementId == MODHUD_GFX_HP_BAR_RED)
        c = 2;
    if (filled > 8)
        filled = 8;
    return sBarTiles[c][filled];
}

const u8 *ModHud_HpBarCapTiles(void)
{
    BuildBarTiles();
    return sBarCapTiles;
}

// ---------------------------------------------------------------------------
// Card geometry
// ---------------------------------------------------------------------------

#define CARD_W 112
#define CARD_COLS (CARD_W / 8)
#define CARD_ROWS_MAX 6
#define PILL_Y 20 // card row of the type pills (11 px tall)
#define BAR_LEFT 6 // card x of the health bar sprite's left pixel (64 px wide)

struct CardLayout
{
    u8 height;      // 44 singles player (64x64 sprites), 32 otherwise (64x32 sprites: never more)
    u8 otherTile;   // tile offset of the right-hand sprite (64 for the 64x64 singles player box, else 32)
    bool8 hpNumbers;
    bool8 exp;
};

static s16 sHpCur[MAX_BATTLERS_COUNT];
static s16 sHpMax[MAX_BATTLERS_COUNT];
static u8 sMenuTypeColorLoaded = 0xFF;

// Note: all health box sprites are created from a 64x32 template, so sprite->y is
// 16 px above the box top even for the 64x64 singles-player box. Card row 0 is
// sprite row 0 in every layout.
static void GetLayout(u8 battler, struct CardLayout *l)
{
    bool8 player = GetBattlerSide(battler) == B_SIDE_PLAYER;
    if (!IsDoubleBattle() && player)
    {
        l->height = 44; l->otherTile = 64; l->hpNumbers = TRUE; l->exp = TRUE;
    }
    else
    {
        l->height = 32; l->otherTile = 32; l->exp = FALSE;
        l->hpNumbers = player && gBattleSpritesDataPtr->battlerData[battler].hpNumbersNoBars;
    }
}

void ModHud_HealthboxCoords(u8 battler, s16 *x, s16 *y)
{
    if (!IsDoubleBattle())
    {
        if (GetBattlerSide(battler) != B_SIDE_PLAYER)
            *x = 44, *y = 30;   // card 12..124 x 14..45
        else
            *x = 158, *y = 82;  // card 126..238 x 66..109
    }
    else
    {
        switch (GetBattlerPosition(battler))
        {
        case B_POSITION_PLAYER_LEFT:     *x = 158, *y = 62; break;  // 46..77
        case B_POSITION_PLAYER_RIGHT:    *x = 158, *y = 96; break;  // 80..111
        case B_POSITION_OPPONENT_LEFT:   *x = 44,  *y = 17; break;  // 1..32
        case B_POSITION_OPPONENT_RIGHT:  *x = 44,  *y = 51; break;  // 35..66
        }
    }
}

void ModHud_HealthbarOffset(u8 battler, s16 *dx, s16 *dy)
{
    // The bar sprite's subsprites span x-16..x+48 and start at y (32x8 OAM, subsprite y 0).
    // Tile 0 lands on card x BAR_LEFT; tile rows 1..5 land on card rows 14..18.
    *dx = -16 + BAR_LEFT;
    *dy = -3;
}

// ---------------------------------------------------------------------------
// Window-backed drawing
// ---------------------------------------------------------------------------

static const struct WindowTemplate sCardWindowTemplate = { .bg = 0, .tilemapLeft = 0, .tilemapTop = 0, .width = CARD_COLS, .height = CARD_ROWS_MAX, .paletteNum = 0, .baseBlock = 0 };

static void Text(u8 win, u8 font, s16 x, s16 y, u8 bg, u8 fg, u8 shadow, const u8 *str)
{
    // Glyph backgrounds are transparent so text never wipes frames, pills or bars.
    u8 colors[3] = { 0, fg, shadow };
    (void)bg;
    AddTextPrinterParameterized4(win, font, x, y, 0, 0, colors, TEXT_SKIP_DRAW, str);
}

static void Rect(u8 win, u8 color, s16 x, s16 y, s16 w, s16 h)
{
    if (w <= 0 || h <= 0)
        return;
    FillWindowPixelRect(win, PIXEL_FILL(color), x, y, w, h);
}

// 1px-rounded filled rectangle
static void Pill(u8 win, u8 color, s16 x, s16 y, s16 w, s16 h)
{
    Rect(win, color, x + 1, y, w - 2, h);
    Rect(win, color, x, y + 1, w, h - 2);
}

static void Frame(u8 win, u8 fill, u8 border, s16 w, s16 h)
{
    Rect(win, fill, 1, 1, w - 2, h - 2);
    Rect(win, border, 1, 0, w - 2, 1);
    Rect(win, border, 1, h - 1, w - 2, 1);
    Rect(win, border, 0, 1, 1, h - 2);
    Rect(win, border, w - 1, 1, 1, h - 2);
}

// Draws a type pill with the small font; returns the pill width.
static u8 TypePill(u8 win, u8 type, s16 x, s16 y, u8 fillIndex, u8 textIndex, u8 shadowIndex)
{
    const u8 *name = gTypeNames[type];
    u8 w = GetStringWidth(FONT_SMALL, name, 0) + 8;
    Pill(win, fillIndex, x, y, w, 11);
    Text(win, FONT_SMALL, x + 4, y >= 2 ? y - 2 : 0, fillIndex, textIndex, shadowIndex, name);
    return w;
}

static void BlitCard(u8 win, u8 healthboxSpriteId, const struct CardLayout *l, u8 rowFirst, u8 rowCount)
{
    u8 *src = (u8 *)GetWindowAttribute(win, WINDOW_TILE_DATA);
    u32 mainTile = gSprites[healthboxSpriteId].oam.tileNum;
    u32 r, c;

    for (r = rowFirst; r < rowFirst + rowCount; r++)
    {
        for (c = 0; c < CARD_COLS; c++)
        {
            u32 tile = (c < 8) ? mainTile + r * 8 + c : mainTile + l->otherTile + r * 8 + (c - 8);
            CpuCopy32(src + (r * CARD_COLS + c) * TILE_SIZE_4BPP, (void *)(OBJ_VRAM0 + tile * TILE_SIZE_4BPP), TILE_SIZE_4BPP);
        }
    }
}

static u8 SlotIndex(u8 battler)
{
    return (battler & 1) ? C_SLOT1 : C_SLOT0;
}

static void SetCardColor(u8 healthboxSpriteId, u8 index, u16 color)
{
    u16 offset = OBJ_PLTT_ID(gSprites[healthboxSpriteId].oam.paletteNum) + index;
    LoadPalette(&color, offset, PLTT_SIZEOF(1));
}

static const u8 *StatusText(u32 status, u8 *colorOut)
{
    if (status & STATUS1_SLEEP) { *colorOut = 2; return sText_SLP; }
    if (status & STATUS1_PSN_ANY) { *colorOut = 0; return sText_PSN; }
    if (status & STATUS1_BURN) { *colorOut = 4; return sText_BRN; }
    if (status & STATUS1_FREEZE) { *colorOut = 3; return sText_FRZ; }
    if (status & STATUS1_PARALYSIS) { *colorOut = 1; return sText_PAR; }
    return NULL;
}

static void DrawHpNumbers(u8 win, u8 battler)
{
    u8 text[16];
    u8 *p;
    p = ConvertIntToDecimalStringN(text, sHpCur[battler], STR_CONV_MODE_LEFT_ALIGN, 3);
    p = StringCopy(p, sText_Slash);
    ConvertIntToDecimalStringN(p, sHpMax[battler], STR_CONV_MODE_LEFT_ALIGN, 3);
    Text(win, FONT_SMALL, CARD_W - 5 - GetStringWidth(FONT_SMALL, text, 0), 9, C_FILL, C_TEXT, C_SHADOW, text);
}

static void DrawExpRow(u8 win, u32 filled64)
{
    u32 f = (filled64 * 75 + 32) / 64;
    // everything here stays inside card rows 4-5 (px rows 32..43) so they can be redrawn alone
    Text(win, FONT_SMALL, 5, 31, C_FILL, C_DIM, C_SHADOW, sText_EXP);
    Rect(win, C_TRACK, 24, 36, 77, 5);
    if (f > 75)
        f = 75;
    Rect(win, C_EXP, 25, 37, f, 3);
}

static u32 CurrentExpFilled(struct Pokemon *mon)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    u8 level = GetMonData(mon, MON_DATA_LEVEL);
    u32 exp, lo, hi;
    if (level >= MAX_LEVEL || species == SPECIES_NONE || species >= NUM_SPECIES)
        return 0;
    exp = GetMonData(mon, MON_DATA_EXP);
    lo = gExperienceTables[gSpeciesInfo[species].growthRate][level];
    hi = gExperienceTables[gSpeciesInfo[species].growthRate][level + 1];
    if (hi <= lo || exp <= lo)
        return 0;
    if (exp >= hi)
        return 64;
    return (exp - lo) * 64 / (hi - lo);
}

void ModHud_InitBattle(void)
{
    u32 i;
    for (i = 0; i < MAX_BATTLERS_COUNT; i++)
        sHpCur[i] = sHpMax[i] = 0;
    sMenuTypeColorLoaded = 0xFF;
}

void ModHud_OnHealthboxCreated(u8 healthboxSpriteId, u8 battler)
{
    u8 barSpriteId = gSprites[healthboxSpriteId].data[5];
    u8 otherSpriteId = gSprites[healthboxSpriteId].oam.affineParam;
    u32 barTile = gSprites[barSpriteId].oam.tileNum;
    struct CardLayout l;

    // wipe the stock health box art from both box sprites (main + right-hand, contiguous)
    GetLayout(battler, &l);
    CpuFill32(0, (void *)(OBJ_VRAM0 + gSprites[healthboxSpriteId].oam.tileNum * TILE_SIZE_4BPP), 2 * l.otherTile * TILE_SIZE_4BPP);

    // bar sprite: left cap in tiles 0-1, tile 8 (opponent's extra 8x8) blank
    CpuCopy32(ModHud_HpBarCapTiles(), (void *)(OBJ_VRAM0 + barTile * TILE_SIZE_4BPP), 64);
    CpuFill32(0, (void *)(OBJ_VRAM0 + (barTile + 8) * TILE_SIZE_4BPP), TILE_SIZE_4BPP);

    // battlers 2/3 use the second card palette so each battler owns its type/status colours
    if (battler >= 2)
    {
        u8 pal = IndexOfSpritePaletteTag(TAG_MODHUD_CARD_PAL_2);
        if (pal != 0xFF)
        {
            gSprites[healthboxSpriteId].oam.paletteNum = pal;
            gSprites[otherSpriteId].oam.paletteNum = pal;
        }
    }
}

static void DrawCardInternal(u8 healthboxSpriteId, struct Pokemon *mon, bool8 refreshHp)
{
    struct CardLayout l;
    u8 battler = gSprites[healthboxSpriteId].data[6];
    u8 win;
    u8 nickname[POKEMON_NAME_LENGTH + 1];
    u8 text[16];
    u8 *p;
    u16 species;
    u8 gender, slot, statusColor;
    const u8 *statusStr;
    u32 status;
    u8 types[2];
    s16 x;

    GetLayout(battler, &l);
    species = GetMonData(mon, MON_DATA_SPECIES);
    win = AddWindow(&sCardWindowTemplate);
    FillWindowPixelBuffer(win, PIXEL_FILL(C_TRANS));
    Frame(win, C_FILL, C_BORDER, CARD_W, l.height);

    // name + gender
    GetMonData(mon, MON_DATA_NICKNAME, nickname);
    StringGet_Nickname(nickname);
    Text(win, FONT_NORMAL, 5, 0, C_FILL, C_TEXT, C_SHADOW, nickname);
    x = 5 + GetStringWidth(FONT_NORMAL, nickname, 0) + 1;
    gender = GetMonGender(mon);
    if ((species == SPECIES_NIDORAN_F || species == SPECIES_NIDORAN_M) && StringCompare(nickname, gSpeciesNames[species]) == 0)
        gender = MON_GENDERLESS;
    if (gender == MON_MALE)
        Text(win, FONT_SMALL, x, 1, C_FILL, C_BLUE, C_SHADOW, gText_MaleSymbol);
    else if (gender == MON_FEMALE)
        Text(win, FONT_SMALL, x, 1, C_FILL, C_PINK, C_SHADOW, gText_FemaleSymbol);

    // level
    text[0] = CHAR_EXTRA_SYMBOL;
    text[1] = CHAR_LV_2;
    ConvertIntToDecimalStringN(text + 2, GetMonData(mon, MON_DATA_LEVEL), STR_CONV_MODE_LEFT_ALIGN, 3);
    Text(win, FONT_SMALL, CARD_W - 5 - GetStringWidth(FONT_SMALL, text, 0), 1, C_FILL, C_DIM, C_SHADOW, text);

    // HP label or status (letters on rows 13..19, centred on the bar)
    slot = SlotIndex(battler);
    status = GetMonData(mon, MON_DATA_STATUS);
    statusStr = StatusText(status, &statusColor);
    if (statusStr != NULL)
    {
        SetCardColor(healthboxSpriteId, slot + 2, sStatusColors[statusColor]);
        Text(win, FONT_SMALL, 4, 9, C_FILL, slot + 2, C_SHADOW, statusStr);
    }
    else
    {
        Text(win, FONT_SMALL, 5, 9, C_FILL, C_DIM, C_SHADOW, sText_HP);
    }
    // right cap of the bar (the bar sprite's body ends at card x 69) unless the bar is hidden
    if (!(IsDoubleBattle() && l.hpNumbers))
        Rect(win, C_SHADOW, BAR_LEFT + 64, 14, 1, 5);

    // HP numbers (singles player, or doubles player with bars toggled off)
    if (refreshHp)
    {
        sHpCur[battler] = GetMonData(mon, MON_DATA_HP);
        sHpMax[battler] = GetMonData(mon, MON_DATA_MAX_HP);
    }
    if (l.hpNumbers)
        DrawHpNumbers(win, battler);

    // caught marker for wild opponents already in the Pokédex
    if (GetBattlerSide(battler) == B_SIDE_OPPONENT && !(gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_WALLY_TUTORIAL))
     && species != SPECIES_NONE && species < NUM_SPECIES
     && GetSetPokedexFlag(SpeciesToNationalPokedexNum(species), FLAG_GET_CAUGHT))
    {
        s16 bx = CARD_W - 5 - GetStringWidth(FONT_SMALL, text, 0) - 9;
        Pill(win, C_BORDER, bx, 5, 6, 6);
        Rect(win, C_PINK, bx + 1, 6, 4, 2);
        Rect(win, C_SHADOW, bx + 1, 8, 4, 1);
    }

    // type pills
    if (species != SPECIES_NONE && species < NUM_SPECIES && !GetMonData(mon, MON_DATA_IS_EGG))
    {
        u8 n = 1;
        types[0] = gSpeciesInfo[species].types[0];
        types[1] = gSpeciesInfo[species].types[1];
        if (types[1] != types[0])
            n = 2;
        x = 5;
        SetCardColor(healthboxSpriteId, slot, sTypeColors[types[0]]);
        x += TypePill(win, types[0], x, PILL_Y, slot, C_TEXT, C_SHADOW) + 3;
        if (n == 2)
        {
            SetCardColor(healthboxSpriteId, slot + 1, sTypeColors[types[1]]);
            TypePill(win, types[1], x, PILL_Y, slot + 1, C_TEXT, C_SHADOW);
        }
    }

    if (l.exp)
        DrawExpRow(win, CurrentExpFilled(mon));

    BlitCard(win, healthboxSpriteId, &l, 0, (l.height + 7) / 8); // 4 rows (64x32) or 6 rows (64x64)
    RemoveWindow(win);
}

void ModHud_DrawCard(u8 healthboxSpriteId, struct Pokemon *mon)
{
    DrawCardInternal(healthboxSpriteId, mon, TRUE);
}

static struct Pokemon *BattlerMon(u8 battler)
{
    if (GetBattlerSide(battler) == B_SIDE_PLAYER)
        return &gPlayerParty[gBattlerPartyIndexes[battler]];
    return &gEnemyParty[gBattlerPartyIndexes[battler]];
}

void ModHud_SetHpText(u8 healthboxSpriteId, s16 value, u8 maxOrCurrent)
{
    struct CardLayout l;
    u8 battler = gSprites[healthboxSpriteId].data[6];
    s16 *slot = (maxOrCurrent == HP_CURRENT) ? &sHpCur[battler] : &sHpMax[battler];

    GetLayout(battler, &l);
    if (*slot == value)
        return;
    *slot = value;
    if (!l.hpNumbers)
        return;
    // The numbers straddle two tile rows that also hold the pills, so redraw the whole
    // card with the cached (animated) HP values rather than the party data.
    DrawCardInternal(healthboxSpriteId, BattlerMon(battler), FALSE);
}

void ModHud_DrawExpPixels(u8 healthboxSpriteId, u32 filled64)
{
    struct CardLayout l;
    u8 battler = gSprites[healthboxSpriteId].data[6];
    u8 win;

    GetLayout(battler, &l);
    if (!l.exp)
        return;
    win = AddWindow(&sCardWindowTemplate);
    FillWindowPixelBuffer(win, PIXEL_FILL(C_TRANS));
    Frame(win, C_FILL, C_BORDER, CARD_W, l.height);
    DrawExpRow(win, filled64);
    BlitCard(win, healthboxSpriteId, &l, 4, 2); // card rows 4-5 hold the EXP row and the bottom border
    RemoveWindow(win);
}

void ModHud_DrawSafariCard(u8 healthboxSpriteId)
{
    struct CardLayout l = { .height = 44, .otherTile = 64, .hpNumbers = FALSE, .exp = FALSE };
    u8 win = AddWindow(&sCardWindowTemplate);
    u8 text[16];
    u8 *p;

    FillWindowPixelBuffer(win, PIXEL_FILL(C_TRANS));
    Frame(win, C_FILL, C_BORDER, CARD_W, l.height);
    Text(win, FONT_NORMAL, 5, 0, C_FILL, C_TEXT, C_SHADOW, sText_SafariBallsTitle);
    p = StringCopy(text, sText_Left);
    ConvertIntToDecimalStringN(p, gNumSafariBalls, STR_CONV_MODE_LEFT_ALIGN, 2);
    Text(win, FONT_SMALL, 5, 16, C_FILL, C_DIM, C_SHADOW, text);
    BlitCard(win, healthboxSpriteId, &l, 0, 6);
    RemoveWindow(win);
}

// ---------------------------------------------------------------------------
// Bottom panel: frame tiles + tilemap
// ---------------------------------------------------------------------------

#define FRAME_TILE_BASE 0x40
enum { FT_FILL, FT_TOP, FT_BOTTOM, FT_LEFT, FT_RIGHT, FT_TL, FT_TR, FT_BL, FT_BR, FT_DIV, FT_DIV_TOP, FT_DIV_BOTTOM, FT_COUNT };

static void BuildFrameTile(u8 *tile, u32 kind)
{
    u32 x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
        {
            u32 c = P0_FRAME_FILL;
            bool8 top = (kind == FT_TOP || kind == FT_TL || kind == FT_TR || kind == FT_DIV_TOP) && y == 0;
            bool8 bottom = (kind == FT_BOTTOM || kind == FT_BL || kind == FT_BR || kind == FT_DIV_BOTTOM) && y == 7;
            bool8 left = (kind == FT_LEFT || kind == FT_TL || kind == FT_BL) && x == 0;
            bool8 right = (kind == FT_RIGHT || kind == FT_TR || kind == FT_BR) && x == 7;
            if (top || bottom || left || right)
                c = P0_FRAME_BORDER;
            if ((kind == FT_DIV || kind == FT_DIV_TOP || kind == FT_DIV_BOTTOM) && x == 7 && !top && !bottom)
                c = P0_FRAME_DIVIDER;
            // rounded corners: the very corner pixel is transparent
            if ((kind == FT_TL && x == 0 && y == 0) || (kind == FT_TR && x == 7 && y == 0)
             || (kind == FT_BL && x == 0 && y == 7) || (kind == FT_BR && x == 7 && y == 7))
                c = 0;
            SetTilePixel(tile, x, y, c);
        }
}

static void FrameBlock(u8 top, bool8 divider)
{
    u16 base = FRAME_TILE_BASE;
    FillBgTilemapBufferRect(0, base + FT_FILL, 1, top + 1, 28, 4, 0);
    FillBgTilemapBufferRect(0, base + FT_TOP, 1, top, 28, 1, 0);
    FillBgTilemapBufferRect(0, base + FT_BOTTOM, 1, top + 5, 28, 1, 0);
    FillBgTilemapBufferRect(0, base + FT_LEFT, 0, top + 1, 1, 4, 0);
    FillBgTilemapBufferRect(0, base + FT_RIGHT, 29, top + 1, 1, 4, 0);
    FillBgTilemapBufferRect(0, base + FT_TL, 0, top, 1, 1, 0);
    FillBgTilemapBufferRect(0, base + FT_TR, 29, top, 1, 1, 0);
    FillBgTilemapBufferRect(0, base + FT_BL, 0, top + 5, 1, 1, 0);
    FillBgTilemapBufferRect(0, base + FT_BR, 29, top + 5, 1, 1, 0);
    if (divider)
    {
        FillBgTilemapBufferRect(0, base + FT_DIV, 15, top + 1, 1, 4, 0);
        FillBgTilemapBufferRect(0, base + FT_DIV_TOP, 15, top, 1, 1, 0);
        FillBgTilemapBufferRect(0, base + FT_DIV_BOTTOM, 15, top + 5, 1, 1, 0);
    }
}

void ModHud_LoadPanel(void)
{
    static u8 ALIGNED(4) tiles[FT_COUNT][32];
    u32 k;
    for (k = 0; k < FT_COUNT; k++)
        BuildFrameTile(tiles[k], k);
    CpuCopy32(tiles, (void *)(BG_CHAR_ADDR(0) + FRAME_TILE_BASE * TILE_SIZE_4BPP), sizeof(tiles));
    // the stock tilemap stacks the three panel states at rows 14, 34 and 54
    FrameBlock(14, FALSE);
    FrameBlock(34, TRUE);
    FrameBlock(54, FALSE);
}

void ModHud_LoadPanelPalettes(void)
{
    LoadPalette(gModHudTextboxPalette, BG_PLTT_ID(0), PLTT_SIZE_4BPP);
    LoadPalette(gModHudMenuPalette, BG_PLTT_ID(5), PLTT_SIZE_4BPP);
    sMenuTypeColorLoaded = 0xFF;
}

// ---------------------------------------------------------------------------
// Bottom panel: action menu, move chips, move info
// ---------------------------------------------------------------------------

static void Finish(u8 windowId)
{
    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

void ModHud_DrawActionMenu(u8 selected, bool8 safari)
{
    static const u8 xs[4] = { 4, 56, 4, 56 };
    static const u8 ys[4] = { 0, 0, 16, 16 };
    const u8 *labels[4];
    u32 i;

    if (safari)
    {
        labels[0] = sText_Ball; labels[1] = sText_Pokeblock; labels[2] = sText_GoNear; labels[3] = sText_Run;
    }
    else
    {
        labels[0] = sText_Fight; labels[1] = sText_Bag; labels[2] = sText_Pokemon; labels[3] = sText_Run;
    }
    FillWindowPixelBuffer(B_WIN_ACTION_MENU, PIXEL_FILL(M_FILL));
    for (i = 0; i < 4; i++)
    {
        if (i == selected)
        {
            u8 w = GetStringWidth(FONT_NORMAL, labels[i], 0) + 8;
            Pill(B_WIN_ACTION_MENU, M_SEL_FILL, xs[i] - 4, ys[i] + 1, w, 13);
            Text(B_WIN_ACTION_MENU, FONT_NORMAL, xs[i], ys[i], M_SEL_FILL, M_SEL_TEXT, M_SEL_FILL, labels[i]);
        }
        else
        {
            Text(B_WIN_ACTION_MENU, FONT_NORMAL, xs[i], ys[i], M_FILL, M_TEXT, M_SHADOW, labels[i]);
        }
    }
    Finish(B_WIN_ACTION_MENU);
}

// The fight panel is one 48-row window (B_WIN_MOD_INFO). Row budget, top to bottom:
//   0        top edge of the panel
//   2..11    move chips (text printed at y 0 -> glyph rows 4..10)
//   13..23   type pill + PP / POW / ACC (text at y 11 -> rows 15..21)
//   25..36   description line 1 (printed at y 22)
//   36..47   description line 2 (printed at y 33)
//   47       bottom edge of the panel, drawn last
// FONT_SMALL glyphs span print y + 3 (tall letters such as "t") to y + 13 (descenders)
// plus one shadow row, so an 11 px line pitch keeps the lines apart and the second
// line's descenders end on row 46, inside the window.
#define CHIP_W 54
#define CHIP_PITCH 56
#define CHIP_Y 2
#define CHIP_H 10
#define CHIPS_ROWS 13          // rows 0..12 belong to the chips row
#define INFO_PILL_Y 13
#define INFO_TEXT_Y 11
#define DESC_Y1 22
#define DESC_Y2 33
#define PANEL_BOTTOM 47

void ModHud_DrawMoveChips(const u16 *moves, u8 selected, u8 swapTarget)
{
    u32 i;

    FillWindowPixelRect(B_WIN_MOD_INFO, PIXEL_FILL(M_FILL), 0, 0, 224, CHIPS_ROWS);
    Rect(B_WIN_MOD_INFO, M_BORDER, 0, 0, 224, 1); // top edge of the panel
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        s16 cx = 1 + i * CHIP_PITCH;
        bool8 sel = (i == selected);
        bool8 target = (i == swapTarget);
        u8 fill = sel ? M_SEL_FILL : M_CHIP;
        u8 fg = sel ? M_SEL_TEXT : M_TEXT;
        u8 sh = sel ? M_SEL_FILL : M_SHADOW;
        const u8 *name;
        u8 font = FONT_SMALL;
        s16 tw;

        Pill(B_WIN_MOD_INFO, fill, cx, CHIP_Y, CHIP_W, CHIP_H);
        if (target && !sel)
        {
            // outlined chip for the move being swapped with
            Rect(B_WIN_MOD_INFO, M_BORDER, cx + 1, CHIP_Y, CHIP_W - 2, 1);
            Rect(B_WIN_MOD_INFO, M_BORDER, cx + 1, CHIP_Y + CHIP_H - 1, CHIP_W - 2, 1);
            Rect(B_WIN_MOD_INFO, M_BORDER, cx, CHIP_Y + 1, 1, CHIP_H - 2);
            Rect(B_WIN_MOD_INFO, M_BORDER, cx + CHIP_W - 1, CHIP_Y + 1, 1, CHIP_H - 2);
        }
        if (moves[i] == MOVE_NONE)
        {
            Text(B_WIN_MOD_INFO, FONT_SMALL, cx + CHIP_W / 2 - 2, 0, fill, M_DIM, sh, sText_Dash);
            continue;
        }
        name = gMoveNames[moves[i]];
        tw = GetStringWidth(font, name, 0);
        if (tw > CHIP_W - 6)
        {
            font = FONT_SMALL_NARROW;
            tw = GetStringWidth(font, name, 0);
        }
        Text(B_WIN_MOD_INFO, font, cx + (CHIP_W - tw) / 2, 0, fill, fg, sh, name);
    }
    Finish(B_WIN_MOD_INFO);
}

static void SetMenuTypeColor(u8 type)
{
    if (sMenuTypeColorLoaded == type)
        return;
    LoadPalette(&sTypeColors[type], BG_PLTT_ID(5) + M_TYPE, PLTT_SIZEOF(1));
    sMenuTypeColorLoaded = type;
}

void ModHud_DrawMoveInfo(u16 move, u8 pp, u8 maxPp)
{
    u8 text[24];
    u8 *p;
    s16 x = 2;
    u8 ppColor = M_TEXT;
    const u8 *desc;

    FillWindowPixelRect(B_WIN_MOD_INFO, PIXEL_FILL(M_FILL), 0, CHIPS_ROWS, 224, PANEL_BOTTOM + 1 - CHIPS_ROWS);
    if (move == MOVE_NONE || move >= MOVES_COUNT)
    {
        Rect(B_WIN_MOD_INFO, M_BORDER, 0, PANEL_BOTTOM, 224, 1);
        Finish(B_WIN_MOD_INFO);
        return;
    }

    SetMenuTypeColor(gBattleMoves[move].type);
    x += TypePill(B_WIN_MOD_INFO, gBattleMoves[move].type, x, INFO_PILL_Y, M_TYPE, M_TEXT, M_SHADOW) + 6;

    if (pp == 0) ppColor = M_PP_NONE;
    else if (pp * 4 <= maxPp) ppColor = M_PP_LOW;
    p = StringCopy(text, sText_PP);
    p = ConvertIntToDecimalStringN(p, pp, STR_CONV_MODE_LEFT_ALIGN, 2);
    p = StringCopy(p, sText_Slash);
    ConvertIntToDecimalStringN(p, maxPp, STR_CONV_MODE_LEFT_ALIGN, 2);
    Text(B_WIN_MOD_INFO, FONT_SMALL, x, INFO_TEXT_Y, M_FILL, ppColor, M_SHADOW, text);
    x += GetStringWidth(FONT_SMALL, text, 0) + 10;

    p = StringCopy(text, sText_POW);
    if (gBattleMoves[move].power == 0)
        StringCopy(p, sText_NoValue);
    else
        ConvertIntToDecimalStringN(p, gBattleMoves[move].power, STR_CONV_MODE_LEFT_ALIGN, 3);
    Text(B_WIN_MOD_INFO, FONT_SMALL, x, INFO_TEXT_Y, M_FILL, M_TEXT, M_SHADOW, text);
    x += GetStringWidth(FONT_SMALL, text, 0) + 10;

    p = StringCopy(text, sText_ACC);
    if (gBattleMoves[move].accuracy == 0)
        StringCopy(p, sText_NoValue);
    else
    {
        p = ConvertIntToDecimalStringN(p, gBattleMoves[move].accuracy, STR_CONV_MODE_LEFT_ALIGN, 3);
        StringCopy(p, sText_Percent);
    }
    Text(B_WIN_MOD_INFO, FONT_SMALL, x, INFO_TEXT_Y, M_FILL, M_TEXT, M_SHADOW, text);

    desc = gMoveDescriptionPointers[move - 1];
    {
        u8 line[40];
        u32 i = 0, n = 0;
        while (desc[i] != EOS && desc[i] != CHAR_NEWLINE && n < sizeof(line) - 1)
            line[n++] = desc[i++];
        line[n] = EOS;
        Text(B_WIN_MOD_INFO, FONT_SMALL, 2, DESC_Y1, M_FILL, M_TEXT, M_SHADOW, line);
        if (desc[i] == CHAR_NEWLINE)
        {
            i++; n = 0;
            while (desc[i] != EOS && desc[i] != CHAR_NEWLINE && n < sizeof(line) - 1)
                line[n++] = desc[i++];
            line[n] = EOS;
            Text(B_WIN_MOD_INFO, FONT_SMALL, 2, DESC_Y2, M_FILL, M_TEXT, M_SHADOW, line);
        }
    }
    // Drawn last so descender shadows on the last row do not notch the panel edge.
    Rect(B_WIN_MOD_INFO, M_BORDER, 0, PANEL_BOTTOM, 224, 1);
    Finish(B_WIN_MOD_INFO);
}

void ModHud_DrawSwitchPrompt(void)
{
    // Replaces the stats + description area; the chips row above stays visible.
    FillWindowPixelRect(B_WIN_MOD_INFO, PIXEL_FILL(M_FILL), 0, CHIPS_ROWS, 224, PANEL_BOTTOM + 1 - CHIPS_ROWS);
    Rect(B_WIN_MOD_INFO, M_BORDER, 0, PANEL_BOTTOM, 224, 1);
    Text(B_WIN_MOD_INFO, FONT_NORMAL, 2, CHIPS_ROWS + 8, M_FILL, M_TEXT, M_SHADOW, sText_SwitchWhich);
    Finish(B_WIN_MOD_INFO);
}

void ModHud_ClearFightMenu(void)
{
    ClearWindowTilemap(B_WIN_MOD_INFO);
    CopyBgTilemapBufferToVram(0);
}

#endif // WASM
