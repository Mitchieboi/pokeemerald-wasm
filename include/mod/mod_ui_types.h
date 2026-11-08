#ifndef GUARD_MOD_UI_TYPES_H
#define GUARD_MOD_UI_TYPES_H

// Shared Pokémon type-icon sprites ("type cards") for screens that only show a
// name: battle health boxes and the party menu. Uses the summary screen's
// type icon graphics (gMoveTypes_Gfx / gMoveTypes_Pal); each icon is a 32x16
// 4bpp sprite loaded on demand with its own tile tag, and the three icon
// palettes are allocated through the sprite palette allocator so they never
// clash with a scene's fixed palette slots.

// Loads the tiles/palette for `type` if needed and creates the sprite.
// Returns SPRITE_NONE if VRAM or a palette slot could not be allocated.
u8 ModTypeIcon_Create(u8 type, s16 x, s16 y, u8 subpriority, u8 oamPriority);
void ModTypeIcon_Destroy(u8 *spriteId);
// Forget cached load state (call when a scene resets sprite data).
void ModTypeIcons_Reset(void);

// Party menu: icons for one slot at absolute screen coordinates.
void ModTypeIcons_PartySlot(u8 slot, u16 species, s16 x, s16 y, u8 spacing);
void ModTypeIcons_ResetParty(void);

#endif // GUARD_MOD_UI_TYPES_H
