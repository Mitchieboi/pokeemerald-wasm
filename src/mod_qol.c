#include "global.h"
#if WASM
#include "mod/mod_config.h"
#include "mod/mod_qol.h"
#include "event_data.h"
#include "overworld.h"
#include "script.h"
#include "constants/flags.h"
#include "constants/vars.h"
#include "constants/maps.h"
#include "constants/heal_locations.h"

static inline bool8 Qol(u8 bit)
{
    return (ModGetSettings()->qol & bit) != 0;
}

bool8 ModQolSkipIntro(void)
{
    return Qol(MOD_QOL_SKIP_INTRO);
}

bool8 ModQolInstantText(void)
{
    return Qol(MOD_QOL_INSTANT_TEXT);
}

bool8 ModQolAutoRun(void)
{
    return Qol(MOD_QOL_AUTO_RUN);
}

void ModQolApplyNewGameDefaults(void)
{
    const struct ModSettings *settings = ModGetSettings();

    if (settings->qol & MOD_QOL_BATTLE_ANIMS_OFF)
        gSaveBlock2Ptr->optionsBattleSceneOff = TRUE;
    if (settings->qol & MOD_QOL_SET_STYLE)
        gSaveBlock2Ptr->optionsBattleStyle = OPTIONS_BATTLE_STYLE_SET;
    // Running Shoes from the first step; the Running Shoes event later is harmless.
    if (settings->qol & MOD_QOL_AUTO_RUN)
        FlagSet(FLAG_SYS_B_DASH);
}

// Fast start: skip the Littleroot errands (truck ride, Mom, wall clock, TV report,
// meeting the rival) and drop the player on Route 101 where Prof. Birch is being
// chased. Every flag and variable the vanilla scripts set along that path is mirrored
// here so the rest of the game plays out unchanged.
void ModQolSkipLittlerootIntro(void)
{
    bool8 male = gSaveBlock2Ptr->playerGender == MALE;

    if (!ModQolSkipIntro())
        return;

    // InsideOfTruck_EventScript_SetIntroFlags{Male,Female}, LittlerootTown_EventScript_StepOffTruck,
    // Littleroot rival's house 1F/2F MeetRival and YoureNewNeighbor.
    if (male)
    {
        SetLastHealLocationWarp(HEAL_LOCATION_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_SIBLING);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F_POKE_BALL);
        VarSet(VAR_LITTLEROOT_HOUSES_STATE_BRENDAN, 2); // met the rival's mom
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_MAY);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_2F_POKE_BALL);
        FlagClear(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_BEDROOM);
    }
    else
    {
        SetLastHealLocationWarp(HEAL_LOCATION_LITTLEROOT_TOWN_MAYS_HOUSE_2F);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_TRUCK);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_MOM);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_SIBLING);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_2F_POKE_BALL);
        VarSet(VAR_LITTLEROOT_HOUSES_STATE_MAY, 2); // met the rival's mom
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_BRENDAN);
        FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F_POKE_BALL);
        FlagClear(FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_BEDROOM);
    }

    // LittlerootTown_OnTransition / LittlerootTown_EventScript_GoInsideWithMom
    FlagSet(FLAG_VISITED_LITTLEROOT_TOWN);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_MOM_OUTSIDE);
    FlagClear(FLAG_HIDE_LITTLEROOT_TOWN_FAT_MAN);
    // PlayersHouse_2F_EventScript_WallClock (movers leave once the clock is set)
    FlagSet(FLAG_SET_WALL_CLOCK);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_PLAYERS_HOUSE_VIGOROTH_1);
    FlagSet(FLAG_HIDE_LITTLEROOT_TOWN_PLAYERS_HOUSE_VIGOROTH_2);
    // PlayersHouse_1F_EventScript_WatchGymBroadcast / SetWatchedBroadcast
    FlagSet(FLAG_SYS_TV_HOME);
    VarSet(VAR_LITTLEROOT_INTRO_STATE, 7);
    // Met the rival's mom and the rival, and the twin at the town exit already asked us
    // to go and see what is happening (LittlerootTown_EventScript_GoSaveBirchTrigger
    // sets 2; leaving it at 1 would replay her line on the first trip out of town).
    FlagSet(FLAG_MET_RIVAL_MOM);
    VarSet(VAR_LITTLEROOT_RIVAL_STATE, 3);
    VarSet(VAR_LITTLEROOT_TOWN_STATE, 2);

    // Route 101. Vanilla hides the map name popup on the first frame (state 0 -> 1) and
    // starts the rescue from the triggers at (10,19)/(11,19) once the player steps on
    // them. The player is warped onto the trigger tile, so ModQolTryStartFastStartScene()
    // runs the same script on the first overworld frame instead.
    FlagSet(FLAG_HIDE_MAP_NAME_POPUP);
    VarSet(VAR_ROUTE101_STATE, 1);
    ModGetSave()->flags |= MOD_SAVEF_FAST_START_PENDING;
    SetWarpDestination(MAP_GROUP(MAP_ROUTE101), MAP_NUM(MAP_ROUTE101), WARP_ID_NONE, 10, 19);
    WarpIntoMap();
}

extern const u8 Route101_EventScript_StartBirchRescue[];

bool8 ModQolFastStartPending(void)
{
    struct ModSave *save = ModGetSave();
    return save != NULL && (save->flags & MOD_SAVEF_FAST_START_PENDING) != 0;
}

bool8 ModQolTryStartFastStartScene(void)
{
    struct ModSave *save = ModGetSave();

    if (!ModQolFastStartPending())
        return FALSE;
    if (gSaveBlock1Ptr->location.mapGroup != MAP_GROUP(MAP_ROUTE101)
     || gSaveBlock1Ptr->location.mapNum != MAP_NUM(MAP_ROUTE101))
        return FALSE;
    save->flags &= ~MOD_SAVEF_FAST_START_PENDING;
    ScriptContext_SetupScript(Route101_EventScript_StartBirchRescue);
    return TRUE;
}

#endif // WASM
