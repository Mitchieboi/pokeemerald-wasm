#ifndef GUARD_MOD_MP_EVENTS_H
#define GUARD_MOD_MP_EVENTS_H

// Game <-> host events for multiplayer runs (Soul Link, trades).
//
// Outbound: the game queues what happened (encounters, gifts, deaths, wipes); the host drains
// them with ModMpPopOutEvent() and reports them to the server.
//
// Inbound: the host writes an event into gModMpInStage and calls ModMpQueueInEvent(). Events
// are applied in the overworld when the player is free to move, in order, once per save:
// ModSave.mpLastSeq remembers the last applied lobby event and the game saves after applying,
// so reloading an older save re-applies what that save hadn't seen yet.

// Outbound types
#define MOD_MPEV_ENCOUNTER 1 // first encounter in an area resolved: key = mapsec
#define MOD_MPEV_STARTER 2   // the first Pokémon received
#define MOD_MPEV_GIFT 3      // a gift Pokémon: key = mapGroup << 8 | mapNum
#define MOD_MPEV_DEATH 4
#define MOD_MPEV_WIPE 5

#define MOD_MPEV_RESULT_CAUGHT 1
#define MOD_MPEV_RESULT_FAILED 2

struct ModMpOutEvent
{
    /*0x00*/ u8 type;
    /*0x01*/ u8 result;
    /*0x02*/ u16 key;
    /*0x04*/ u32 pid;
    /*0x08*/ u16 species;
    /*0x0A*/ u8 nickname[11];      // game string, EOS-terminated
    /*0x15*/ u8 speciesName[11];
}; /* size = 0x20 */

// Inbound types
#define MOD_MPIN_KILL 1     // release `pid` (Soul Link partner died / link broken); text = message line
#define MOD_MPIN_RUN_OVER 2 // the group's run is over (Soul Link wipe policy "group")
#define MOD_MPIN_TRADE 3    // trade: `pid2` (ours) leaves, `mon` (theirs, pid) arrives; text = partner name

struct ModMpInEvent
{
    /*0x00*/ u32 seq;
    /*0x04*/ u8 type;
    /*0x05*/ u8 pad[3];
    /*0x08*/ u32 pid;
    /*0x0C*/ u32 pid2;
    /*0x10*/ u8 text[48];          // ASCII
    /*0x40*/ u8 mon[100];          // struct Pokemon
}; /* size = 0xA4 */

extern struct ModMpInEvent gModMpInStage;

struct ModMpOutEvent *ModMpPopOutEvent(void);
bool8 ModMpQueueInEvent(void);
u32 ModMpLastSeq(void);
u32 ModMpInQueueLength(void);

// Hooks
void ModMpEventsOnEncounter(u8 result, u16 mapsec, struct Pokemon *mon);
void ModMpEventsOnGiveMon(struct Pokemon *mon);
void ModMpEventsOnDeath(struct Pokemon *mon);
void ModMpEventsOnWipe(void);
// Overworld input hook (field_control_avatar.c): TRUE if it took the frame.
bool8 ModMpTryApplyEvents(void);

// Soul Link team sync "strict": no wild encounters while the team is out of sync.
void ModMpSetWildBlocked(u32 blocked);
bool8 ModMpWildBlocked(void);

// Mon introspection for trades and PvP. `where`: 0..5 party slot.
struct ModMpMonInfo
{
    /*0x00*/ u32 pid;
    /*0x04*/ u16 species;
    /*0x06*/ u8 level;
    /*0x07*/ u8 flags;             // bit0 egg, bit1 dead, bit2 holding an item, bit3 shiny
    /*0x08*/ u8 nickname[11];
    /*0x13*/ u8 speciesName[11];
    /*0x1E*/ u8 types[2];
}; /* size = 0x20 */
struct ModMpMonInfo *ModMpMonInfo(u32 where);
struct Pokemon *ModMpExportMon(u32 where);

#endif // GUARD_MOD_MP_EVENTS_H
