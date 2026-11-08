#ifndef GUARD_MOD_QOL_H
#define GUARD_MOD_QOL_H

// Quality-of-life options (see MOD_QOL_* in mod_config.h). All read the settings
// stored in the current save, so they are no-ops for saves without mod data.

bool8 ModQolSkipIntro(void);
bool8 ModQolInstantText(void);
bool8 ModQolAutoRun(void);

// Applied once from ModOnNewGame().
void ModQolApplyNewGameDefaults(void);

#endif // GUARD_MOD_QOL_H
