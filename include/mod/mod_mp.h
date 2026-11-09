#ifndef GUARD_MOD_MP_H
#define GUARD_MOD_MP_H

// Multiplayer presence: the host (web app) reads the local player's state through
// ModMpLocalState() and feeds other players' positions in with ModMpRemoteUpdate().
// Remote players are drawn as sprites (not object events), so they never collide,
// never take an object event slot and never end up in a save.

#define MOD_MP_MAX_REMOTES 4

// ModMpLocal.state
#define MOD_MP_STATE_NONE 0      // not in the overworld yet (boot, title, new-game setup)
#define MOD_MP_STATE_FIELD 1     // walking around
#define MOD_MP_STATE_BUSY 2      // in the overworld but controls are locked (dialogue, scripts, start menu)
#define MOD_MP_STATE_BATTLE 3
#define MOD_MP_STATE_MENU 4      // full-screen menu (bag, party, summary, PC, Pokédex...)

// ModMpLocal.battle
#define MOD_MP_BATTLE_WILD 1
#define MOD_MP_BATTLE_TRAINER 2
#define MOD_MP_BATTLE_LINK 3

// ModMpLocal.avatar / ModMpRemoteUpdate avatar
#define MOD_MP_AVATAR_WALK 0
#define MOD_MP_AVATAR_MACH_BIKE 1
#define MOD_MP_AVATAR_ACRO_BIKE 2
#define MOD_MP_AVATAR_SURF 3
#define MOD_MP_AVATAR_UNDERWATER 4

struct ModMpLocal
{
    /*0x00*/ u8 state;
    /*0x01*/ u8 battle;
    /*0x02*/ u8 mapGroup;
    /*0x03*/ u8 mapNum;
    /*0x04*/ s16 x;
    /*0x06*/ s16 y;
    /*0x08*/ u8 facing;    // DIR_SOUTH..DIR_EAST (1..4)
    /*0x09*/ u8 avatar;
    /*0x0A*/ u8 elevation;
    /*0x0B*/ u8 dash;      // running shoes held
    /*0x0C*/ u8 mapsec;
    /*0x0D*/ u8 moving;
    /*0x0E*/ u16 frame;    // overworld frames seen (wraps)
}; /* size = 0x10 */

// Called every overworld frame from OverworldBasic().
void ModMpOverworldTick(void);

// Refreshes and returns the local player's state.
struct ModMpLocal *ModMpLocalState(void);

// Positions for other players. A new position adjacent to the last one is walked to in
// framesPerStep frames; anything further (or another map) is a teleport.
void ModMpRemoteUpdate(u32 slot, u32 gender, u32 mapGroup, u32 mapNum, s32 x, s32 y, u32 facing, u32 avatar, u32 elevation, u32 framesPerStep);
void ModMpRemoteRemove(u32 slot);
void ModMpRemoteClearAll(void);
// Screen position (top centre of the sprite) packed as ((x + 1024) << 16) | (y + 1024), or 0 when not drawn.
u32 ModMpRemoteScreenPos(u32 slot);

#endif // GUARD_MOD_MP_H
