#ifndef GUARD_MOD_QOL_H
#define GUARD_MOD_QOL_H

// Quality-of-life options (see MOD_QOL_* in mod_config.h). All read the settings
// stored in the current save, so they are no-ops for saves without mod data.

bool8 ModQolSkipIntro(void);
bool8 ModQolInstantText(void);
bool8 ModQolAutoRun(void);

// Applied once from ModOnNewGame().
void ModQolApplyNewGameDefaults(void);
// Fast start (MOD_QOL_SKIP_INTRO): skip the Littleroot errands and start on Route 101
// at the Birch rescue. Called from ModOnNewGame() after NewGameInitData's truck warp.
void ModQolSkipLittlerootIntro(void);
// Called every overworld frame before the map's ON_FRAME table: runs the Birch rescue
// script once after a fast-start warp. Returns TRUE if it started a script.
bool8 ModQolTryStartFastStartScene(void);
bool8 ModQolFastStartPending(void);

#endif // GUARD_MOD_QOL_H
