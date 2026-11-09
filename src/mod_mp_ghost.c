#include "global.h"
#if WASM
#include "mod/mod_config.h"
#include "mod/mod_mp_ghost.h"
#include "mod/mod_nuzlocke.h"
#include "battle.h"
#include "battle_setup.h"
#include "data.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "field_screen_effect.h"
#include "gpu_regs.h"
#include "main.h"
#include "money.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"
#include "sprite.h"
#include "string_util.h"
#include "constants/battle.h"
#include "constants/trainers.h"
#include "constants/characters.h"
#include "constants/songs.h"

void ModMpStartBattleTask(u16 song); // battle_setup.c

struct ModMpGhostStage gModMpGhostStage;

static u8 sState;
static u8 sResult;
static u8 sLevelMode, sLevelArg;
static u8 sName[PLAYER_NAME_LENGTH + 1];
static u8 sGender;
static u32 sTrainerId;
static struct Pokemon sTeam[PARTY_SIZE];
static u8 sTeamCount;

static struct Pokemon sSavedParty[PARTY_SIZE];
static u32 sSavedMoney;
static struct ItemSlot sSavedItems[BAG_ITEMS_COUNT];
static struct ItemSlot sSavedBalls[BAG_POKEBALLS_COUNT];
static struct ItemSlot sSavedBerries[BAG_BERRIES_COUNT];

static const u8 sText_GoodBattle[] = _("That was a great battle!");

static void SetLevel(struct Pokemon *mon, u8 level)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    u32 exp = gExperienceTables[gSpeciesInfo[species].growthRate][level];
    SetMonData(mon, MON_DATA_EXP, &exp);
    CalculateMonStats(mon);
}

static void ApplyLevelRule(struct Pokemon *mon, u8 levelMode, u8 levelArg)
{
    u8 level = GetMonData(mon, MON_DATA_LEVEL);
    if (levelArg < MIN_LEVEL || levelArg > MAX_LEVEL)
        return;
    if (levelMode == MOD_MP_GHOST_LEVELS_SET && level != levelArg)
        SetLevel(mon, levelArg);
    else if (levelMode == MOD_MP_GHOST_LEVELS_CAP && level > levelArg)
        SetLevel(mon, levelArg);
}

static void Heal(struct Pokemon *mon)
{
    u16 hp = GetMonData(mon, MON_DATA_MAX_HP);
    u32 status = STATUS1_NONE;
    SetMonData(mon, MON_DATA_HP, &hp);
    SetMonData(mon, MON_DATA_STATUS, &status);
    MonRestorePP(mon);
}

static bool8 CanBattle(struct Pokemon *mon)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    return species != SPECIES_NONE && species < NUM_SPECIES
        && !GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG)
        && !GetMonData(mon, MON_DATA_IS_EGG)
        && !ModNuzlockeMonIsDead(mon);
}

bool8 ModMpRequestGhostBattle(void)
{
    u32 i;

    if (sState == MOD_MP_GHOST_BATTLE)
        return FALSE;
    sTeamCount = 0;
    for (i = 0; i < gModMpGhostStage.count && i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &sTeam[sTeamCount];
        memcpy(mon, gModMpGhostStage.mons[i], sizeof(struct Pokemon));
        if (CanBattle(mon))
            sTeamCount++;
    }
    if (sTeamCount == 0)
        return FALSE;
    memset(sName, EOS, sizeof(sName));
    ModAsciiToGameString(sName, (const char *)gModMpGhostStage.name, PLAYER_NAME_LENGTH);
    sGender = gModMpGhostStage.gender ? FEMALE : MALE;
    sTrainerId = gModMpGhostStage.trainerId;
    sLevelMode = gModMpGhostStage.levelMode;
    sLevelArg = gModMpGhostStage.levelArg;
    sResult = 0;
    sState = MOD_MP_GHOST_PENDING;
    return TRUE;
}

void ModMpCancelGhostBattle(void)
{
    if (sState == MOD_MP_GHOST_PENDING)
        sState = MOD_MP_GHOST_IDLE;
}

u32 ModMpGhostBattleState(void)
{
    return sState;
}

u32 ModMpTakeGhostBattleResult(void)
{
    u32 result = sResult;
    if (sState != MOD_MP_GHOST_DONE)
        return 0;
    sState = MOD_MP_GHOST_IDLE;
    sResult = 0;
    return result;
}

bool8 ModMpGhostBattleActive(void)
{
    return sState == MOD_MP_GHOST_BATTLE;
}

// Player battles change nothing: the party, money and bag are put back afterwards.
void ModMpSnapshotPlayer(void)
{
    u32 i;
    for (i = 0; i < PARTY_SIZE; i++)
        sSavedParty[i] = gPlayerParty[i];
    sSavedMoney = GetMoney(&gSaveBlock1Ptr->money);
    memcpy(sSavedItems, gSaveBlock1Ptr->bagPocket_Items, sizeof(sSavedItems));
    memcpy(sSavedBalls, gSaveBlock1Ptr->bagPocket_PokeBalls, sizeof(sSavedBalls));
    memcpy(sSavedBerries, gSaveBlock1Ptr->bagPocket_Berries, sizeof(sSavedBerries));
}

void ModMpRestorePlayer(void)
{
    u32 i;
    for (i = 0; i < PARTY_SIZE; i++)
        gPlayerParty[i] = sSavedParty[i];
    CalculatePlayerPartyCount();
    SetMoney(&gSaveBlock1Ptr->money, sSavedMoney);
    memcpy(gSaveBlock1Ptr->bagPocket_Items, sSavedItems, sizeof(sSavedItems));
    memcpy(gSaveBlock1Ptr->bagPocket_PokeBalls, sSavedBalls, sizeof(sSavedBalls));
    memcpy(gSaveBlock1Ptr->bagPocket_Berries, sSavedBerries, sizeof(sSavedBerries));
}

// Both sides start fresh; fallen Nuzlocke Pokémon stay fallen.
void ModMpPreparePartyForBattle(struct Pokemon *party, u8 levelMode, u8 levelArg)
{
    u32 i;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &party[i];
        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE || GetMonData(mon, MON_DATA_IS_EGG) || ModNuzlockeMonIsDead(mon))
            continue;
        ApplyLevelRule(mon, levelMode, levelArg);
        Heal(mon);
    }
}

void ModMpReturnToFieldAfterBattle(void)
{
    CpuFill16(0, (void *)(BG_PLTT), BG_PLTT_SIZE);
    ResetOamRange(0, 128);
    gFieldCallback = FieldCB_ReturnToFieldNoScriptCheckMusic;
    SetMainCallback2(CB2_ReturnToField);
}

static void CB2_EndGhostBattle(void)
{
    sResult = gBattleOutcome ? gBattleOutcome : B_OUTCOME_DREW;
    ModMpRestorePlayer();
    sState = MOD_MP_GHOST_DONE;
    ModMpReturnToFieldAfterBattle();
}

bool8 ModMpTryStartGhostBattle(void)
{
    u32 i;

    if (sState != MOD_MP_GHOST_PENDING)
        return FALSE;

    ModMpSnapshotPlayer();
    ModMpPreparePartyForBattle(gPlayerParty, sLevelMode, sLevelArg);
    ZeroEnemyPartyMons();
    for (i = 0; i < sTeamCount; i++)
        gEnemyParty[i] = sTeam[i];
    ModMpPreparePartyForBattle(gEnemyParty, sLevelMode, sLevelArg);

    sState = MOD_MP_GHOST_BATTLE;
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_SECRET_BASE;
    gTrainerBattleOpponent_A = TRAINER_SECRET_BASE;
    gMain.savedCallback = CB2_EndGhostBattle;
    LockPlayerFieldControls();
    FreezeObjectEvents();
    StopPlayerAvatar();
    ModMpStartBattleTask(MUS_VS_RIVAL);
    return TRUE;
}

void ModMpFillGhostSecretBase(void)
{
    struct SecretBase *base = gBattleResources->secretBase;
    u32 i;

    memset(base, 0, sizeof(*base));
    for (i = 0; i < PLAYER_NAME_LENGTH; i++)
        base->trainerName[i] = sName[i];
    base->gender = sGender;
    for (i = 0; i < TRAINER_ID_LENGTH; i++)
        base->trainerId[i] = sTrainerId >> (i * 8);
    base->language = GAME_LANGUAGE;
    // party.levels[0] = 0: no prize money
}

u8 ModMpGhostFacilityClass(void)
{
    return gBattleResources->secretBase->gender == FEMALE ? FACILITY_CLASS_MAY : FACILITY_CLASS_BRENDAN;
}

const u8 *ModMpGhostLoseText(void)
{
    return sText_GoodBattle;
}
#endif
