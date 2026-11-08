#ifndef GUARD_MOD_NUZLOCKE_H
#define GUARD_MOD_NUZLOCKE_H

// Nuzlocke rules enforced in-game (see MOD_NUZ_* in mod_config.h):
//  - a party Pokémon that faints is dead: it is released after the battle
//  - only the first wild encounter per area (MAPSEC) may be caught
//  - caught Pokémon must be nicknamed
//  - optional dupes clause, shiny clause, level cap and "wipe = run over"

bool8 ModNuzlockeActive(void);

// Battle lifecycle (battle_main.c)
void ModNuzlockeOnBattleStart(void);
void ModNuzlockeOnBattleEnd(void);
// A player-side battler fainted (battle_script_commands.c, Cmd_tryfaintmon)
void ModNuzlockeOnPlayerMonFainted(u8 partyIndex);
// Ball throw gate (Cmd_handleballthrow): TRUE when the throw must be blocked
bool8 ModNuzlockeShouldBlockBall(void);
// Skip the "give a nickname?" prompt for caught Pokémon
bool8 ModNuzlockeForceNickname(void);

// Items
bool8 ModNuzlockeBlocksItem(u16 item);
// Current level cap (MAX_LEVEL when inactive)
u8 ModNuzlockeLevelCap(void);
// Clamp an EXP gain so `mon` cannot pass the level cap
s32 ModNuzlockeClampExp(struct Pokemon *mon, s32 exp);

// Field poison: mark dead / process deaths outside of battle (field_poison.c)
void ModNuzlockeOnFieldFaint(u8 partyIndex);
void ModNuzlockeAfterFieldPoison(bool8 allFainted);

// Overworld: show queued death messages; returns TRUE if it took the frame
bool8 ModNuzlockeTryShowDeathMessages(void);

// Introspection for the host
u16 ModNuzlockeDeaths(void);
u8 ModNuzlockeRunState(void);
bool8 ModNuzlockeAreaUsed(u16 mapsec);

#endif // GUARD_MOD_NUZLOCKE_H
