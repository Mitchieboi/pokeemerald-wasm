#ifndef GUARD_MOD_MP_GHOST_H
#define GUARD_MOD_MP_GHOST_H

// Ghost battles: battle an AI-controlled copy of another player's team. They're played like a
// secret base battle (a trainer battle that never whites out), and the player's party, money and
// bag are put back exactly as they were afterwards, so nothing is gained or lost — no deaths
// either. The host stages the opponent in gModMpGhostStage and calls ModMpRequestGhostBattle();
// the battle starts the next time the player is free to walk around.

#define MOD_MP_GHOST_LEVELS_AS_IS 0
#define MOD_MP_GHOST_LEVELS_SET 1 // every Pokémon (both sides) to levelArg
#define MOD_MP_GHOST_LEVELS_CAP 2 // Pokémon above levelArg (both sides) down to it

#define MOD_MP_GHOST_IDLE 0
#define MOD_MP_GHOST_PENDING 1
#define MOD_MP_GHOST_BATTLE 2
#define MOD_MP_GHOST_DONE 3

struct ModMpGhostStage
{
    /*0x00*/ u8 name[8];           // ASCII
    /*0x08*/ u8 gender;
    /*0x09*/ u8 count;
    /*0x0A*/ u8 levelMode;
    /*0x0B*/ u8 levelArg;
    /*0x0C*/ u32 trainerId;
    /*0x10*/ u8 mons[PARTY_SIZE][100]; // struct Pokemon
}; /* size = 0x268 */

extern struct ModMpGhostStage gModMpGhostStage;

// FALSE if the staged team has no Pokémon that can battle, or a battle is already under way.
bool8 ModMpRequestGhostBattle(void);
void ModMpCancelGhostBattle(void);
u32 ModMpGhostBattleState(void);
// The outcome (B_OUTCOME_*) once the state is DONE; reading it makes the state IDLE again.
u32 ModMpTakeGhostBattleResult(void);
bool8 ModMpGhostBattleActive(void);

// Shared with live battles (mod_mp_link.c)
void ModMpSnapshotPlayer(void);
void ModMpRestorePlayer(void);
void ModMpPreparePartyForBattle(struct Pokemon *party, u8 levelMode, u8 levelArg);
void ModMpReturnToFieldAfterBattle(void);

// Hooks
bool8 ModMpTryStartGhostBattle(void);  // overworld input (via ModMpTryApplyEvents)
void ModMpFillGhostSecretBase(void);    // battle resources allocated (battle_util2.c)
u8 ModMpGhostFacilityClass(void);
const u8 *ModMpGhostLoseText(void);

#endif // GUARD_MOD_MP_GHOST_H
