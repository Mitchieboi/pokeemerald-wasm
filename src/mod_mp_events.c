#include "global.h"
#if WASM
#include "mod/mod_config.h"
#include "mod/mod_mp_events.h"
#include "mod/mod_mp_ghost.h"
#include "mod/mod_nuzlocke.h"
#include "data.h"
#include "event_data.h"
#include "field_message_box.h"
#include "main.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "save.h"
#include "script.h"
#include "string_util.h"
#include "task.h"
#include "constants/species.h"
#include "constants/characters.h"

#define OUT_QUEUE_LEN 16
#define IN_QUEUE_LEN 8

void ModMpStartTradeScene(u8 partyIndex); // trade.c

struct ModMpInEvent gModMpInStage;

static struct ModMpOutEvent sOut[OUT_QUEUE_LEN];
static u8 sOutHead, sOutLen;
static struct ModMpOutEvent sOutCurrent;

static struct ModMpInEvent sIn[IN_QUEUE_LEN];
static u8 sInHead, sInLen;

static bool8 sWildBlocked;
static u32 sTradeSceneSeq;    // a trade scene is running for this event; finish it back in the field
static bool8 sMessageActive;

static const u8 sText_SoulKill[] = _("{STR_VAR_1}\p{STR_VAR_2} was released\nby the SOUL LINK…\p");
static const u8 sText_SoulKillLast[] = _("{STR_VAR_1}\p{STR_VAR_2} was your last POKéMON.\nYour SOUL LINK run is over.\p");
static const u8 sText_RunOver[] = _("{STR_VAR_1}\pThe SOUL LINK run is over,\nbut you can keep playing.\p");
static const u8 sText_TradeArrived[] = _("{STR_VAR_1} sent over\n{STR_VAR_2}!\p");

// ---------------------------------------------------------------- outbound

static struct ModMpOutEvent *PushOut(u8 type)
{
    struct ModMpOutEvent *e;
    if (sOutLen == OUT_QUEUE_LEN)
    {
        // Drop the oldest; the host drains every frame, so this only happens if it isn't listening.
        sOutHead = (sOutHead + 1) % OUT_QUEUE_LEN;
        sOutLen--;
    }
    e = &sOut[(sOutHead + sOutLen) % OUT_QUEUE_LEN];
    sOutLen++;
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->nickname[0] = EOS;
    e->speciesName[0] = EOS;
    return e;
}

static void FillMon(struct ModMpOutEvent *e, struct Pokemon *mon)
{
    u8 name[POKEMON_NAME_BUFFER_SIZE];
    e->pid = GetMonData(mon, MON_DATA_PERSONALITY);
    e->species = GetMonData(mon, MON_DATA_SPECIES);
    GetMonData(mon, MON_DATA_NICKNAME, name);
    name[POKEMON_NAME_LENGTH] = EOS;
    StringCopy(e->nickname, name);
    if (e->species < NUM_SPECIES)
        StringCopy(e->speciesName, gSpeciesNames[e->species]);
}

struct ModMpOutEvent *ModMpPopOutEvent(void)
{
    if (sOutLen == 0)
        return NULL;
    sOutCurrent = sOut[sOutHead];
    sOutHead = (sOutHead + 1) % OUT_QUEUE_LEN;
    sOutLen--;
    return &sOutCurrent;
}

void ModMpEventsOnEncounter(u8 result, u16 mapsec, struct Pokemon *mon)
{
    struct ModMpOutEvent *e = PushOut(MOD_MPEV_ENCOUNTER);
    e->result = result;
    e->key = mapsec;
    if (mon != NULL && result == MOD_MPEV_RESULT_CAUGHT)
        FillMon(e, mon);
}

void ModMpEventsOnGiveMon(struct Pokemon *mon)
{
    struct ModSave *save = ModGetSave();
    struct ModMpOutEvent *e;

    if (save != NULL && !(save->mpFlags & MOD_MPF_STARTER_REPORTED))
    {
        save->mpFlags |= MOD_MPF_STARTER_REPORTED;
        e = PushOut(MOD_MPEV_STARTER);
    }
    else
    {
        e = PushOut(MOD_MPEV_GIFT);
        e->key = (gSaveBlock1Ptr->location.mapGroup << 8) | gSaveBlock1Ptr->location.mapNum;
    }
    e->result = MOD_MPEV_RESULT_CAUGHT;
    FillMon(e, mon);
}

void ModMpEventsOnDeath(struct Pokemon *mon)
{
    FillMon(PushOut(MOD_MPEV_DEATH), mon);
}

void ModMpEventsOnWipe(void)
{
    PushOut(MOD_MPEV_WIPE);
}

// ---------------------------------------------------------------- inbound

bool8 ModMpQueueInEvent(void)
{
    if (sInLen == IN_QUEUE_LEN)
        return FALSE;
    sIn[(sInHead + sInLen) % IN_QUEUE_LEN] = gModMpInStage;
    sInLen++;
    return TRUE;
}

u32 ModMpInQueueLength(void)
{
    return sInLen + (sTradeSceneSeq != 0);
}

u32 ModMpLastSeq(void)
{
    struct ModSave *save = ModGetSave();
    return save != NULL ? save->mpLastSeq : 0;
}

void ModMpSetWildBlocked(u32 blocked)
{
    sWildBlocked = blocked != 0;
}

bool8 ModMpWildBlocked(void)
{
    return sWildBlocked;
}

// Where a Pokémon with this personality is: party slot 0..5, 100 + box * IN_BOX_COUNT + pos, or -1.
static s32 FindPid(u32 pid)
{
    s32 i, box, pos;
    for (i = 0; i < PARTY_SIZE; i++)
        if (GetMonData(&gPlayerParty[i], MON_DATA_SPECIES) != SPECIES_NONE && GetMonData(&gPlayerParty[i], MON_DATA_PERSONALITY) == pid)
            return i;
    for (box = 0; box < TOTAL_BOXES_COUNT; box++)
        for (pos = 0; pos < IN_BOX_COUNT; pos++)
            if (GetBoxMonDataAt(box, pos, MON_DATA_SPECIES) != SPECIES_NONE && GetBoxMonDataAt(box, pos, MON_DATA_PERSONALITY) == pid)
                return 100 + box * IN_BOX_COUNT + pos;
    return -1;
}

static void RemoveAt(s32 where)
{
    if (where < 0)
        return;
    if (where < PARTY_SIZE)
    {
        ZeroMonData(&gPlayerParty[where]);
        CompactPartySlots();
        CalculatePlayerPartyCount();
    }
    else
    {
        where -= 100;
        ZeroBoxMonAt(where / IN_BOX_COUNT, where % IN_BOX_COUNT);
    }
}

static u32 UsablePartyCount(void)
{
    u32 i, n = 0;
    for (i = 0; i < PARTY_SIZE; i++)
        if (GetMonData(&gPlayerParty[i], MON_DATA_SPECIES) != SPECIES_NONE && !GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG))
            n++;
    return n;
}

static void Commit(u32 seq)
{
    struct ModSave *save = ModGetSave();
    if (save == NULL)
        return;
    if (seq > save->mpLastSeq)
        save->mpLastSeq = seq;
    TrySavingData(SAVE_NORMAL);
}

static void ShowMessage(const u8 *template)
{
    StringExpandPlaceholders(gStringVar4, template);
    ShowFieldMessage(gStringVar4);
    LockPlayerFieldControls();
    sMessageActive = TRUE;
}

static void ApplyKill(struct ModMpInEvent *ev)
{
    struct ModSave *save = ModGetSave();
    s32 where = FindPid(ev->pid);
    u8 name[POKEMON_NAME_BUFFER_SIZE];

    if (where < 0)
    {
        Commit(ev->seq);
        return;
    }
    if (where < PARTY_SIZE)
        GetMonData(&gPlayerParty[where], MON_DATA_NICKNAME, name);
    else
        GetBoxMonData(GetBoxedMonPtr((where - 100) / IN_BOX_COUNT, (where - 100) % IN_BOX_COUNT), MON_DATA_NICKNAME, name);
    name[POKEMON_NAME_LENGTH] = EOS;
    ModAsciiToGameString(gStringVar1, (const char *)ev->text, 40);
    StringCopy_Nickname(gStringVar2, name);

    if (where < PARTY_SIZE && UsablePartyCount() <= 1)
    {
        // Releasing the last Pokémon would leave the player stuck; the run ends instead.
        if (save != NULL)
            save->runState = MOD_RUN_FAILED;
        Commit(ev->seq);
        ShowMessage(sText_SoulKillLast);
        return;
    }
    RemoveAt(where);
    if (save != NULL && save->deaths < 0xFFFF)
        save->deaths++;
    Commit(ev->seq);
    ShowMessage(sText_SoulKill);
}

static void ApplyRunOver(struct ModMpInEvent *ev)
{
    struct ModSave *save = ModGetSave();
    if (save != NULL)
        save->runState = MOD_RUN_FAILED;
    ModAsciiToGameString(gStringVar1, (const char *)ev->text, 40);
    Commit(ev->seq);
    ShowMessage(sText_RunOver);
}

static void ApplyTrade(struct ModMpInEvent *ev)
{
    struct Pokemon *incoming = (struct Pokemon *)ev->mon;
    s32 ours = FindPid(ev->pid2);
    s32 theirs = FindPid(ev->pid);
    u8 name[POKEMON_NAME_BUFFER_SIZE];

    if (theirs >= 0)
    {
        // Already applied (a reload of a save made after the trade, or a repeat).
        RemoveAt(ours);
        Commit(ev->seq);
        return;
    }
    if (ours >= 0 && ours < PARTY_SIZE)
    {
        // Run the game's own trade scene: it swaps gPlayerParty[ours] with gEnemyParty[0] and
        // handles trade evolutions. We commit once we're back in the field.
        gEnemyParty[0] = *incoming;
        sTradeSceneSeq = ev->seq;
        ModMpStartTradeScene(ours);
        return;
    }
    RemoveAt(ours);
    GiveMonToPlayer(incoming);
    ModAsciiToGameString(gStringVar1, (const char *)ev->text, 20);
    GetMonData(incoming, MON_DATA_NICKNAME, name);
    name[POKEMON_NAME_LENGTH] = EOS;
    StringCopy_Nickname(gStringVar2, name);
    Commit(ev->seq);
    ShowMessage(sText_TradeArrived);
}

bool8 ModMpTryApplyEvents(void)
{
    struct ModMpInEvent ev;

    if (sMessageActive)
    {
        if (!IsFieldMessageBoxHidden())
            return TRUE;
        HideFieldMessageBox();
        UnlockPlayerFieldControls();
        sMessageActive = FALSE;
        return TRUE;
    }
    if (ModMpTryStartGhostBattle())
        return TRUE;
    if (sTradeSceneSeq)
    {
        u32 seq = sTradeSceneSeq;
        sTradeSceneSeq = 0;
        Commit(seq);
        return FALSE;
    }
    while (sInLen)
    {
        ev = sIn[sInHead];
        sInHead = (sInHead + 1) % IN_QUEUE_LEN;
        sInLen--;
        if (ev.seq <= ModMpLastSeq())
            continue;
        switch (ev.type)
        {
        case MOD_MPIN_KILL: ApplyKill(&ev); break;
        case MOD_MPIN_RUN_OVER: ApplyRunOver(&ev); break;
        case MOD_MPIN_TRADE: ApplyTrade(&ev); break;
        default: Commit(ev.seq); break;
        }
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------- introspection

static struct ModMpMonInfo sInfo;
static struct Pokemon sExport;

struct ModMpMonInfo *ModMpMonInfo(u32 where)
{
    struct Pokemon *mon;
    u8 name[POKEMON_NAME_BUFFER_SIZE];

    if (where >= PARTY_SIZE)
        return NULL;
    mon = &gPlayerParty[where];
    memset(&sInfo, 0, sizeof(sInfo));
    sInfo.species = GetMonData(mon, MON_DATA_SPECIES);
    if (sInfo.species == SPECIES_NONE)
        return NULL;
    sInfo.pid = GetMonData(mon, MON_DATA_PERSONALITY);
    sInfo.level = GetMonData(mon, MON_DATA_LEVEL);
    if (GetMonData(mon, MON_DATA_IS_EGG)) sInfo.flags |= 1;
    if (ModNuzlockeMonIsDead(mon)) sInfo.flags |= 2;
    if (GetMonData(mon, MON_DATA_HELD_ITEM)) sInfo.flags |= 4;
    if (IsMonShiny(mon)) sInfo.flags |= 8;
    GetMonData(mon, MON_DATA_NICKNAME, name);
    name[POKEMON_NAME_LENGTH] = EOS;
    StringCopy(sInfo.nickname, name);
    if (sInfo.species < NUM_SPECIES)
    {
        StringCopy(sInfo.speciesName, gSpeciesNames[sInfo.species]);
        sInfo.types[0] = gSpeciesInfo[sInfo.species].types[0];
        sInfo.types[1] = gSpeciesInfo[sInfo.species].types[1];
    }
    return &sInfo;
}

struct Pokemon *ModMpExportMon(u32 where)
{
    if (where >= PARTY_SIZE || GetMonData(&gPlayerParty[where], MON_DATA_SPECIES) == SPECIES_NONE)
        return NULL;
    sExport = gPlayerParty[where];
    return &sExport;
}
#endif
