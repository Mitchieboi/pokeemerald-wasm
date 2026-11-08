#include "global.h"
#if WASM
#include "mod/mod_config.h"
#include "mod/mod_nuzlocke.h"
#include "mod/mod_randomizer.h"
#include "battle.h"
#include "pokemon.h"
#include "pokedex.h"
#include "pokemon_storage_system.h"
#include "event_data.h"
#include "field_message_box.h"
#include "script.h"
#include "task.h"
#include "string_util.h"
#include "item.h"
#include "constants/items.h"
#include "constants/item_effects.h"
#include "constants/battle.h"
#include "constants/flags.h"
#include "constants/species.h"

// Dead-flag storage: bit 0 of the (checksummed, encrypted) unused ribbon nibble.
#define DEAD_FLAG 0x1

// Per-battle state
static bool8 sWildBattle;
static bool8 sFirstEncounter;   // this battle is the area's first encounter (catch allowed, consumes the area)
static bool8 sDupeEncounter;    // dupes clause: species family already caught, does not count
static bool8 sShinyEncounter;
static u8 sBattleMapsec;

// Death message queue (shown back in the overworld)
static u8 sPendingNames[PARTY_SIZE][POKEMON_NAME_LENGTH + 1];
static u8 sPendingCount;

static const u8 sText_Fallen[] = _("{STR_VAR_1} has fallen…\pIt was released.\p\n");
static const u8 sText_RunOver[] = _("Your whole team was wiped out.\pThe NUZLOCKE run is over,\nbut you can keep playing.\p\n");
static bool8 sPendingRunOver;

// Level cap by badge count (gym leader ace levels), then Elite Four, then none.
static const u8 sLevelCaps[] = { 15, 19, 24, 29, 31, 33, 42, 46, 58 };

bool8 ModNuzlockeActive(void)
{
    struct ModSave *save = ModGetSave();
    return save != NULL && (save->settings.nuzlocke & MOD_NUZ_ENABLED) && save->runState == MOD_RUN_ACTIVE;
}

static bool8 Clause(u8 bit)
{
    return (ModGetSettings()->nuzlocke & bit) != 0;
}

static bool8 IsWildBattle(void)
{
    return !(gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER
                                 | BATTLE_TYPE_EREADER_TRAINER | BATTLE_TYPE_TRAINER_HILL | BATTLE_TYPE_WALLY_TUTORIAL
                                 | BATTLE_TYPE_FIRST_BATTLE | BATTLE_TYPE_RECORDED | BATTLE_TYPE_RECORDED_LINK
                                 | BATTLE_TYPE_SECRET_BASE));
}

// Battles in which a faint counts as a death.
static bool8 IsDeathBattle(void)
{
    return !(gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_EREADER_TRAINER
                                 | BATTLE_TYPE_TRAINER_HILL | BATTLE_TYPE_WALLY_TUTORIAL | BATTLE_TYPE_FIRST_BATTLE
                                 | BATTLE_TYPE_RECORDED | BATTLE_TYPE_RECORDED_LINK | BATTLE_TYPE_SAFARI));
}

static bool8 AreaUsed(struct ModSave *save, u16 mapsec)
{
    if (mapsec / 8 >= MOD_ENCOUNTER_BITMAP_BYTES)
        return FALSE;
    return (save->encounterUsed[mapsec / 8] >> (mapsec % 8)) & 1;
}

static void SetAreaUsed(struct ModSave *save, u16 mapsec)
{
    if (mapsec / 8 < MOD_ENCOUNTER_BITMAP_BYTES)
        save->encounterUsed[mapsec / 8] |= 1 << (mapsec % 8);
}

bool8 ModNuzlockeAreaUsed(u16 mapsec)
{
    struct ModSave *save = ModGetSave();
    return save != NULL && AreaUsed(save, mapsec);
}

static bool8 FamilyCaught(u16 species)
{
    u16 root = ModSpeciesFamilyRoot(species);
    u16 s;
    for (s = 1; s < NUM_SPECIES; s++)
    {
        if (ModSpeciesIsValid(s) && ModSpeciesFamilyRoot(s) == root
         && GetSetPokedexFlag(SpeciesToNationalPokedexNum(s), FLAG_GET_CAUGHT))
            return TRUE;
    }
    return FALSE;
}

static bool8 IsDead(struct Pokemon *mon)
{
    return GetMonData(mon, MON_DATA_SPECIES) != SPECIES_NONE
        && (GetMonData(mon, MON_DATA_UNUSED_RIBBONS) & DEAD_FLAG) != 0;
}

static void MarkDead(struct Pokemon *mon)
{
    u8 val = GetMonData(mon, MON_DATA_UNUSED_RIBBONS) | DEAD_FLAG;
    SetMonData(mon, MON_DATA_UNUSED_RIBBONS, &val);
}

static void ClearDead(struct Pokemon *mon)
{
    u8 val = GetMonData(mon, MON_DATA_UNUSED_RIBBONS) & ~DEAD_FLAG;
    SetMonData(mon, MON_DATA_UNUSED_RIBBONS, &val);
}

void ModNuzlockeOnBattleStart(void)
{
    struct ModSave *save = ModGetSave();

    sWildBattle = FALSE;
    sFirstEncounter = FALSE;
    sDupeEncounter = FALSE;
    sShinyEncounter = FALSE;
    if (!ModNuzlockeActive())
        return;

    sWildBattle = IsWildBattle();
    if (!sWildBattle)
        return;

    sBattleMapsec = gMapHeader.regionMapSectionId;
    sShinyEncounter = IsMonShiny(&gEnemyParty[0]);
    if (Clause(MOD_NUZ_DUPES_CLAUSE) && FamilyCaught(GetMonData(&gEnemyParty[0], MON_DATA_SPECIES)))
        sDupeEncounter = TRUE;
    sFirstEncounter = !AreaUsed(save, sBattleMapsec) && !sDupeEncounter;
}

bool8 ModNuzlockeShouldBlockBall(void)
{
    if (!ModNuzlockeActive() || !sWildBattle)
        return FALSE;
    if (sShinyEncounter && Clause(MOD_NUZ_SHINY_CLAUSE))
        return FALSE;
    return !sFirstEncounter;
}

bool8 ModNuzlockeForceNickname(void)
{
    return ModNuzlockeActive();
}

void ModNuzlockeOnPlayerMonFainted(u8 partyIndex)
{
    if (!ModNuzlockeActive() || !IsDeathBattle() || partyIndex >= PARTY_SIZE)
        return;
    MarkDead(&gPlayerParty[partyIndex]);
}

// TRUE when the player has at least one usable Pokémon and all of them have fainted.
static bool8 AllPartyFainted(void)
{
    u32 i;
    bool8 anyMon = FALSE;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        if (GetMonData(mon, MON_DATA_SPECIES) == SPECIES_NONE || GetMonData(mon, MON_DATA_IS_EGG))
            continue;
        anyMon = TRUE;
        if (GetMonData(mon, MON_DATA_HP) != 0)
            return FALSE;
    }
    return anyMon;
}

static void OnWipe(struct ModSave *save)
{
    u32 i;
    // Releasing the whole team would leave the player with nothing, so the team
    // survives; the run is marked failed when that rule is on.
    for (i = 0; i < PARTY_SIZE; i++)
        if (IsDead(&gPlayerParty[i]))
            ClearDead(&gPlayerParty[i]);
    if (Clause(MOD_NUZ_WIPE_IS_GAME_OVER))
    {
        save->runState = MOD_RUN_FAILED;
        sPendingRunOver = TRUE;
    }
}

static void ProcessDeaths(struct ModSave *save)
{
    u32 i;
    bool8 released = FALSE;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        if (!IsDead(mon))
            continue;
        if (sPendingCount < PARTY_SIZE)
            GetMonData(mon, MON_DATA_NICKNAME, sPendingNames[sPendingCount++]);
        if (save->deaths < 0xFFFF)
            save->deaths++;
        ZeroMonData(mon);
        released = TRUE;
    }
    if (released)
    {
        CompactPartySlots();
        CalculatePlayerPartyCount();
    }
}

void ModNuzlockeOnBattleEnd(void)
{
    struct ModSave *save = ModGetSave();

    if (!ModNuzlockeActive())
        return;

    if (sWildBattle && sFirstEncounter && !(sShinyEncounter && Clause(MOD_NUZ_SHINY_CLAUSE)))
    {
        switch (gBattleOutcome)
        {
        case B_OUTCOME_WON:
        case B_OUTCOME_LOST:
        case B_OUTCOME_DREW:
        case B_OUTCOME_RAN:
        case B_OUTCOME_PLAYER_TELEPORTED:
        case B_OUTCOME_MON_FLED:
        case B_OUTCOME_CAUGHT:
        case B_OUTCOME_MON_TELEPORTED:
            SetAreaUsed(save, sBattleMapsec);
            break;
        }
    }

    if (!IsDeathBattle())
        return;

    if (gBattleOutcome == B_OUTCOME_LOST || gBattleOutcome == B_OUTCOME_DREW || AllPartyFainted())
        OnWipe(save);
    else
        ProcessDeaths(save);
}

void ModNuzlockeOnFieldFaint(u8 partyIndex)
{
    if (ModNuzlockeActive() && partyIndex < PARTY_SIZE)
        MarkDead(&gPlayerParty[partyIndex]);
}

void ModNuzlockeAfterFieldPoison(bool8 allFainted)
{
    struct ModSave *save = ModGetSave();
    if (!ModNuzlockeActive())
        return;
    if (allFainted)
        OnWipe(save);
    else
        ProcessDeaths(save);
}

// --- items and level cap ---------------------------------------------------------

static bool8 IsReviveItem(u16 item)
{
    const u8 *effect;
    if (item == ITEM_SACRED_ASH)
        return TRUE;
    if (item < ITEM_POTION || item > LAST_BERRY_INDEX)
        return FALSE;
    effect = gItemEffectTable[item - ITEM_POTION];
    return effect != NULL && (effect[4] & ITEM4_REVIVE) != 0;
}

bool8 ModNuzlockeBlocksItem(u16 item)
{
    return ModNuzlockeActive() && IsReviveItem(item);
}

u8 ModNuzlockeLevelCap(void)
{
    u32 badges = 0;
    if (!ModNuzlockeActive() || !Clause(MOD_NUZ_LEVEL_CAP))
        return MAX_LEVEL;
    if (FlagGet(FLAG_SYS_GAME_CLEAR))
        return MAX_LEVEL;
    if (FlagGet(FLAG_BADGE01_GET)) badges++;
    if (FlagGet(FLAG_BADGE02_GET)) badges++;
    if (FlagGet(FLAG_BADGE03_GET)) badges++;
    if (FlagGet(FLAG_BADGE04_GET)) badges++;
    if (FlagGet(FLAG_BADGE05_GET)) badges++;
    if (FlagGet(FLAG_BADGE06_GET)) badges++;
    if (FlagGet(FLAG_BADGE07_GET)) badges++;
    if (FlagGet(FLAG_BADGE08_GET)) badges++;
    return sLevelCaps[badges];
}

s32 ModNuzlockeClampExp(struct Pokemon *mon, s32 exp)
{
    u8 cap = ModNuzlockeLevelCap();
    u16 species;
    u32 current, atCap;

    if (cap >= MAX_LEVEL || exp <= 0)
        return exp;
    species = GetMonData(mon, MON_DATA_SPECIES);
    current = GetMonData(mon, MON_DATA_EXP);
    atCap = gExperienceTables[gSpeciesInfo[species].growthRate][cap];
    if (current >= atCap)
        return 0;
    if (current + exp > atCap)
        return atCap - current;
    return exp;
}

// --- overworld messages -------------------------------------------------------------

#define tState data[0]
#define tIndex data[1]

static void Task_ShowDeathMessages(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    switch (tState)
    {
    case 0:
        if (tIndex < sPendingCount)
        {
            StringCopy(gStringVar1, sPendingNames[tIndex]);
            ShowFieldMessage(sText_Fallen);
            tState = 1;
        }
        else if (sPendingRunOver)
        {
            sPendingRunOver = FALSE;
            ShowFieldMessage(sText_RunOver);
            tState = 1;
        }
        else
        {
            sPendingCount = 0;
            UnlockPlayerFieldControls();
            DestroyTask(taskId);
        }
        break;
    case 1:
        if (IsFieldMessageBoxHidden())
        {
            HideFieldMessageBox();
            tIndex++;
            tState = 0;
        }
        break;
    }
}

#undef tState
#undef tIndex

bool8 ModNuzlockeTryShowDeathMessages(void)
{
    if (sPendingCount == 0 && !sPendingRunOver)
        return FALSE;
    if (FuncIsActiveTask(Task_ShowDeathMessages))
        return TRUE;
    LockPlayerFieldControls();
    CreateTask(Task_ShowDeathMessages, 80);
    return TRUE;
}

u16 ModNuzlockeDeaths(void)
{
    struct ModSave *save = ModGetSave();
    return save != NULL ? save->deaths : 0;
}

u8 ModNuzlockeRunState(void)
{
    struct ModSave *save = ModGetSave();
    return save != NULL ? save->runState : MOD_RUN_NONE;
}

#endif // WASM
