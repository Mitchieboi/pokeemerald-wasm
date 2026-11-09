#ifndef GUARD_MOD_MP_LINK_H
#define GUARD_MOD_MP_LINK_H

// Live battles between two players over the network. The game's own link battle runs unchanged;
// the link layer underneath it (link.c: SendBlock, GetBlockReceivedStatus, the standby and
// close-link handshakes, …) is carried by packets the host relays through the server instead of
// the serial port. Player 0 is the link master and runs the battle; player 1 follows it.
//
// The host opens a session with ModMpLinkOpen(), pumps packets both ways (ModMpLinkPopOut /
// gModMpLinkIn + ModMpLinkPushIn), and the battle starts once the player is free to walk around
// and the partner has said hello. As with ghost battles, the party, money and bag are put back
// afterwards and nobody dies.

#define MOD_MP_LINK_PACKET_BLOCK 1   // SendBlock data
#define MOD_MP_LINK_PACKET_STANDBY 2 // SetLinkStandbyCallback barrier
#define MOD_MP_LINK_PACKET_CLOSE 3   // SetCloseLinkCallback barrier
#define MOD_MP_LINK_PACKET_HELLO 4   // struct LinkPlayer of the sender

#define MOD_MP_LINK_IDLE 0
#define MOD_MP_LINK_WAITING 1        // session open, battle not started
#define MOD_MP_LINK_BATTLE 2
#define MOD_MP_LINK_DONE 3

struct ModMpLinkPacket
{
    /*0x00*/ u8 type;
    /*0x01*/ u8 pad;
    /*0x02*/ u16 size;
    /*0x04*/ u8 data[0x100];
}; /* size = 0x104 */

extern struct ModMpLinkPacket gModMpLinkIn;

void ModMpLinkOpen(u8 multiplayerId, u8 levelMode, u8 levelArg);
struct ModMpLinkPacket *ModMpLinkPopOut(void);
bool8 ModMpLinkPushIn(void);
// The partner is gone: leave the battle (or don't start it). The outcome is a draw.
void ModMpLinkAbort(void);
u32 ModMpLinkState(void);
u32 ModMpTakeLinkResult(void);

// link.c, while a session is open
bool8 ModMpLinkActive(void);
u8 ModMpLinkMultiplayerId(void);
bool8 ModMpLinkSendBlock(const void *src, u16 size);
void ModMpLinkDeliver(void);
bool8 ModMpLinkTaskFinished(void);
void ModMpLinkSetStandby(void);
void ModMpLinkSetClose(void);
void ModMpLinkOpenLink(void);
void ModMpLinkCloseLink(void);

// Hooks
bool8 ModMpTryStartLinkBattle(void); // overworld input (via ModMpTryApplyEvents)
void ModMpLinkTick(void);             // every frame (VBlank)

#endif // GUARD_MOD_MP_LINK_H
