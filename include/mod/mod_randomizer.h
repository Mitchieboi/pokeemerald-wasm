#ifndef GUARD_MOD_RANDOMIZER_H
#define GUARD_MOD_RANDOMIZER_H

// Species randomizer. Each group has its own deterministic species -> species
// table derived from the save's seed (ModSettings.seed), so a run is stable
// across reloads without storing the tables. Species outside the normal range
// (SPECIES_NONE, the old Unown placeholders, eggs, graphics-only forms) pass
// through unchanged.

enum
{
    MOD_RAND_GROUP_STARTER,
    MOD_RAND_GROUP_WILD,    // wild, static and roaming encounters, gifts, eggs, in-game trades
    MOD_RAND_GROUP_TRAINER,
    MOD_RAND_GROUP_COUNT,
};

bool8 ModRandomizerGroupEnabled(u8 group);
u16 ModRandomizeSpecies(u8 group, u16 species);
// Wild-encounter variant: identity inside the Battle Pyramid / Battle Pike, which
// read the species back as a table index.
u16 ModRandomizeWildEncounter(u16 species);

// Species metadata (also used by the Nuzlocke dupes clause and by tests).
bool8 ModSpeciesIsValid(u16 species);
bool8 ModSpeciesIsLegendary(u16 species);
u8 ModSpeciesStage(u16 species);        // 0 = base form, 1 = first evolution, ...
u8 ModSpeciesChainLength(u16 species);  // 1 = single stage, 2 = two-stage family, 3 = three-stage family
u16 ModSpeciesPreEvolution(u16 species); // SPECIES_NONE for base forms
u16 ModSpeciesFamilyRoot(u16 species);

// Rebuild tables for a given seed/flags (tests); normal code paths call this lazily.
void ModRandomizerRebuild(void);

#endif // GUARD_MOD_RANDOMIZER_H
