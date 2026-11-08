#ifndef GUARD_MOD_BATTLE_HUD_H
#define GUARD_MOD_BATTLE_HUD_H

// Custom "Slate" battle HUD (WASM only).
//
// Health cards: the stock health box sprites (64x32 or 64x64 left part, 64x32/64x64
// right part, plus the 64x8 health bar sprite) are kept, but their tile contents are
// rendered from scratch: frame, name, gender, level, HP label/status, HP numbers,
// type pills and the EXP bar. The stock HP bar animation still writes the bar sprite's
// tiles; it just uses procedurally generated bar art.
//
// Bottom panel: a Slate frame replaces the stock text box tilemap, and the fight menu
// becomes one row of move chips plus an info card (type, PP, power, accuracy,
// description).

#include "sprite.h"

#define TAG_MODHUD_CARD_PAL_2 0xD7A1 // second card palette (battlers 2 and 3 in doubles)

// Element ids of the stock gHealthboxElementsGfxTable (private enum in battle_interface.c).
#define MODHUD_GFX_HP_BAR_GREEN  3
#define MODHUD_GFX_HP_BAR_YELLOW 47
#define MODHUD_GFX_HP_BAR_RED    56

// --- palettes / graphics ---------------------------------------------------------
extern const u16 gModHudCardPalette[16];     // OBJ palette for the health box sprites (TAG_HEALTHBOX_PAL)
extern const u16 gModHudBarPalette[16];      // OBJ palette for the health bar sprite (TAG_HEALTHBAR_PAL)
extern const u16 gModHudTextboxPalette[16];  // BG palette 0 (message window + frame)
extern const u16 gModHudMenuPalette[16];     // BG palette 5 (menus, chips, info card)

// HP bar art: returns the 8x8 tile for `filled` (0..8) pixels in the given colour
// (HEALTHBOX_GFX_HP_BAR_GREEN/YELLOW/RED element ids), and the left cap tiles.
const u8 *ModHud_HpBarTile(u8 barElementId, u8 filled);
const u8 *ModHud_HpBarCapTiles(void); // 2 tiles (64 bytes) for the bar sprite's tiles 0-1

// --- health cards ----------------------------------------------------------------
void ModHud_InitBattle(void);
void ModHud_OnHealthboxCreated(u8 healthboxSpriteId, u8 battler);
void ModHud_DrawCard(u8 healthboxSpriteId, struct Pokemon *mon);
void ModHud_SetHpText(u8 healthboxSpriteId, s16 value, u8 maxOrCurrent);
void ModHud_DrawExpPixels(u8 healthboxSpriteId, u32 filled64);
void ModHud_DrawSafariCard(u8 healthboxSpriteId);
void ModHud_HealthboxCoords(u8 battler, s16 *x, s16 *y);
void ModHud_HealthbarOffset(u8 battler, s16 *dx, s16 *dy);

// --- bottom panel -------------------------------------------------------------------
void ModHud_LoadPanel(void);          // after the stock textbox tiles/tilemap are copied
void ModHud_LoadPanelPalettes(void);  // after the stock BG palettes 0 and 5 are loaded
void ModHud_DrawActionMenu(u8 selected, bool8 safari);
void ModHud_DrawMoveChips(const u16 *moves, u8 selected, u8 swapTarget);
void ModHud_DrawMoveInfo(u16 move, u8 pp, u8 maxPp);
void ModHud_DrawSwitchPrompt(void);
void ModHud_ClearFightMenu(void);

#endif // GUARD_MOD_BATTLE_HUD_H
