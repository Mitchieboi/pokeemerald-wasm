#include "global.h"
#if WASM
#include "mod/mod_config.h"
#include "mod/mod_qol.h"
#include "event_data.h"
#include "constants/flags.h"

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

#endif // WASM
