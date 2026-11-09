#include "global.h"
#if WASM
#include "mod/mod_mp_ghost.h"
#include "mod/mod_mp_link.h"
#include "battle.h"
#include "battle_setup.h"
#include "battle_gfx_sfx_util.h"
#include "battle_util2.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "link.h"
#include "m4a.h"
#include "main.h"
#include "overworld.h"
#include "pokemon.h"
#include "script.h"
#include "sound.h"
#include "string_util.h"
#include "task.h"
#include "window.h"
#include "constants/battle.h"
#include "constants/songs.h"
#include "constants/trainers.h"

#define OUT_LEN 64
#define IN_LEN 64

void ModMpStartBattleTask(u16 song); // battle_setup.c

struct BlockQueue
{
    struct ModMpLinkPacket items[IN_LEN];
    u8 head, len;
};

struct ModMpLinkPacket gModMpLinkIn;

static u8 sState;
static u8 sResult;
static u8 sId;
static u8 sLevelMode, sLevelArg;
static bool8 sOpen;            // the game's link is "open" (between OpenLink and CloseLink)
static bool8 sAbort;
static bool8 sPartnerHello;
static struct LinkPlayer sPartner;

static struct ModMpLinkPacket sOut[OUT_LEN];
static u8 sOutHead, sOutLen;
static struct ModMpLinkPacket sOutCurrent;
static struct BlockQueue sBlocks[2]; // per multiplayer id, including our own (the link echoes)

static bool8 sStandbyWaiting, sCloseWaiting;
static u8 sRemoteStandby, sRemoteClose;

static bool8 Push(struct BlockQueue *q, const struct ModMpLinkPacket *p)
{
    if (q->len >= IN_LEN)
        return FALSE;
    q->items[(q->head + q->len) % IN_LEN] = *p;
    q->len++;
    return TRUE;
}

static bool8 Send(u8 type, const void *data, u16 size)
{
    struct ModMpLinkPacket *p;
    if (sOutLen >= OUT_LEN || size > sizeof(p->data))
        return FALSE;
    p = &sOut[(sOutHead + sOutLen) % OUT_LEN];
    p->type = type;
    p->pad = 0;
    p->size = size;
    if (size)
        memcpy(p->data, data, size);
    sOutLen++;
    return TRUE;
}

bool8 ModMpLinkActive(void)
{
    return sState == MOD_MP_LINK_WAITING || sState == MOD_MP_LINK_BATTLE;
}

u8 ModMpLinkMultiplayerId(void)
{
    return sId;
}

u32 ModMpLinkState(void)
{
    return sState;
}

u32 ModMpTakeLinkResult(void)
{
    u32 result = sResult;
    if (sState != MOD_MP_LINK_DONE)
        return 0;
    sState = MOD_MP_LINK_IDLE;
    sResult = 0;
    return result;
}

static void InitLocalPlayer(void)
{
    gLinkType = LINKTYPE_BATTLE;
    LocalLinkPlayerToBlock(); // fills gLocalLinkPlayer
    gLocalLinkPlayer.id = sId;
}

void ModMpLinkOpen(u8 multiplayerId, u8 levelMode, u8 levelArg)
{
    memset(sBlocks, 0, sizeof(sBlocks));
    sOutHead = sOutLen = 0;
    sId = multiplayerId & 1;
    sLevelMode = levelMode;
    sLevelArg = levelArg;
    sOpen = FALSE;
    sAbort = FALSE;
    sPartnerHello = FALSE;
    sStandbyWaiting = sCloseWaiting = FALSE;
    sRemoteStandby = sRemoteClose = 0;
    sResult = 0;
    sState = MOD_MP_LINK_WAITING;
    InitLocalPlayer();
    Send(MOD_MP_LINK_PACKET_HELLO, &gLocalLinkPlayer, sizeof(gLocalLinkPlayer));
}

struct ModMpLinkPacket *ModMpLinkPopOut(void)
{
    if (sOutLen == 0)
        return NULL;
    sOutCurrent = sOut[sOutHead];
    sOutHead = (sOutHead + 1) % OUT_LEN;
    sOutLen--;
    return &sOutCurrent;
}

bool8 ModMpLinkPushIn(void)
{
    struct ModMpLinkPacket *p = &gModMpLinkIn;
    if (!ModMpLinkActive())
        return TRUE; // nothing to do with it; drop
    switch (p->type)
    {
    case MOD_MP_LINK_PACKET_BLOCK:
        if (p->size > sizeof(p->data))
            return TRUE;
        return Push(&sBlocks[sId ^ 1], p);
    case MOD_MP_LINK_PACKET_STANDBY:
        sRemoteStandby++;
        return TRUE;
    case MOD_MP_LINK_PACKET_CLOSE:
        sRemoteClose++;
        return TRUE;
    case MOD_MP_LINK_PACKET_HELLO:
        memcpy(&sPartner, p->data, sizeof(sPartner));
        sPartner.id = sId ^ 1;
        sPartnerHello = TRUE;
        return TRUE;
    }
    return TRUE;
}

// ---------------------------------------------------------------- link.c replacements

bool8 ModMpLinkSendBlock(const void *src, u16 size)
{
    struct ModMpLinkPacket p;
    if (size > sizeof(p.data))
        return FALSE;
    p.type = MOD_MP_LINK_PACKET_BLOCK;
    p.pad = 0;
    p.size = size;
    memcpy(p.data, src, size);
    // Like the cable, every player (us included) receives what we send.
    if (!Send(MOD_MP_LINK_PACKET_BLOCK, src, size))
        return FALSE;
    Push(&sBlocks[sId], &p);
    return TRUE;
}

// Hand the next queued block to each player slot the game has finished with.
void ModMpLinkDeliver(void)
{
    u32 i;
    for (i = 0; i < 2; i++)
    {
        struct BlockQueue *q = &sBlocks[i];
        if (gBlockReceivedStatus[i] || q->len == 0)
            continue;
        memcpy(gBlockRecvBuffer[i], q->items[q->head].data, q->items[q->head].size);
        q->head = (q->head + 1) % IN_LEN;
        q->len--;
        gBlockReceivedStatus[i] = TRUE;
    }
}

bool8 ModMpLinkTaskFinished(void)
{
    return !sStandbyWaiting && !sCloseWaiting;
}

void ModMpLinkSetStandby(void)
{
    if (sStandbyWaiting || sCloseWaiting)
        return;
    sStandbyWaiting = TRUE;
    Send(MOD_MP_LINK_PACKET_STANDBY, NULL, 0);
}

void ModMpLinkSetClose(void)
{
    if (sStandbyWaiting || sCloseWaiting)
        return;
    sCloseWaiting = TRUE;
    Send(MOD_MP_LINK_PACKET_CLOSE, NULL, 0);
}

void ModMpLinkOpenLink(void)
{
    s32 i;
    InitLocalPlayer();
    for (i = 0; i < MAX_LINK_PLAYERS; i++)
        gBlockReceivedStatus[i] = FALSE;
    gLinkPlayers[sId] = gLocalLinkPlayer;
    ConvertLinkPlayerName(&gLinkPlayers[sId]);
    gLinkPlayers[sId ^ 1] = sPartner;
    ConvertLinkPlayerName(&gLinkPlayers[sId ^ 1]);
    sOpen = TRUE;
    gReceivedRemoteLinkPlayers = TRUE;
}

void ModMpLinkCloseLink(void)
{
    sOpen = FALSE;
    gReceivedRemoteLinkPlayers = FALSE;
}

// ---------------------------------------------------------------- battle

static void CB2_EndLinkBattle(void)
{
    sResult = gBattleOutcome ? gBattleOutcome : B_OUTCOME_DREW;
    gBattleTypeFlags &= ~BATTLE_TYPE_LINK_IN_BATTLE;
    ModMpRestorePlayer();
    sState = MOD_MP_LINK_DONE;
    sOpen = FALSE;
    gReceivedRemoteLinkPlayers = FALSE;
    Overworld_ResetMapMusic();
    ModMpReturnToFieldAfterBattle();
}

bool8 ModMpTryStartLinkBattle(void)
{
    if (sState != MOD_MP_LINK_WAITING || !sPartnerHello)
        return FALSE;
    if (sAbort)
    {
        sResult = B_OUTCOME_DREW;
        sState = MOD_MP_LINK_DONE;
        return FALSE;
    }
    ModMpSnapshotPlayer();
    ModMpPreparePartyForBattle(gPlayerParty, sLevelMode, sLevelArg);
    sState = MOD_MP_LINK_BATTLE;
    gBattleTypeFlags = BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER;
    gTrainerBattleOpponent_A = TRAINER_LINK_OPPONENT;
    gMain.savedCallback = CB2_EndLinkBattle;
    LockPlayerFieldControls();
    FreezeObjectEvents();
    StopPlayerAvatar();
    ModMpStartBattleTask(MUS_VS_TRAINER);
    return TRUE;
}

void ModMpLinkAbort(void)
{
    sAbort = TRUE;
    if (sState == MOD_MP_LINK_WAITING)
    {
        sResult = B_OUTCOME_DREW;
        sState = MOD_MP_LINK_DONE;
    }
}

void ModMpLinkTick(void)
{
    if (!ModMpLinkActive())
        return;
    if (sStandbyWaiting && sRemoteStandby)
    {
        sRemoteStandby--;
        sStandbyWaiting = FALSE;
    }
    if (sCloseWaiting && sRemoteClose)
    {
        sRemoteClose--;
        sCloseWaiting = FALSE;
        gBattleTypeFlags &= ~BATTLE_TYPE_LINK_IN_BATTLE;
        ModMpLinkCloseLink();
    }
    if (sOpen)
        ModMpLinkDeliver();

    // The partner left mid-battle: leave too, once the battle has been set up.
    if (sAbort && sState == MOD_MP_LINK_BATTLE && gBattleResources != NULL)
    {
        m4aSongNumStop(SE_LOW_HEALTH);
        gBattleOutcome = B_OUTCOME_DREW;
        gMain.inBattle = FALSE;
        if (gBattleTypeFlags & BATTLE_TYPE_LINK_IN_BATTLE)
            gMain.callback1 = gPreBattleCallback1;
        FreeAllWindowBuffers();
        FreeBattleResources();
        FreeBattleSpritesData();
        FreeMonSpritesGfx();
        ResetTasks();
        SetMainCallback2(CB2_EndLinkBattle);
    }
}
#endif
