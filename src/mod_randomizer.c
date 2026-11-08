#include "global.h"
#if WASM
#include "mod/mod_config.h"
#include "mod/mod_randomizer.h"
#include "pokemon.h"
#include "random.h"
#include "constants/species.h"
#include "constants/layouts.h"

extern const struct Evolution gEvolutionTable[][EVOS_PER_MON];

// --- species metadata -------------------------------------------------------

static bool8 sMetaBuilt;
static u16 sPreEvo[NUM_SPECIES];
static u8 sStage[NUM_SPECIES];
static u8 sChainLength[NUM_SPECIES];

static const u16 sLegendaries[] =
{
    SPECIES_ARTICUNO, SPECIES_ZAPDOS, SPECIES_MOLTRES, SPECIES_MEWTWO, SPECIES_MEW,
    SPECIES_RAIKOU, SPECIES_ENTEI, SPECIES_SUICUNE, SPECIES_LUGIA, SPECIES_HO_OH, SPECIES_CELEBI,
    SPECIES_REGIROCK, SPECIES_REGICE, SPECIES_REGISTEEL, SPECIES_LATIAS, SPECIES_LATIOS,
    SPECIES_KYOGRE, SPECIES_GROUDON, SPECIES_RAYQUAZA, SPECIES_JIRACHI, SPECIES_DEOXYS,
};

bool8 ModSpeciesIsValid(u16 species)
{
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return FALSE;
    if (species >= SPECIES_OLD_UNOWN_B && species <= SPECIES_OLD_UNOWN_Z)
        return FALSE;
    return TRUE;
}

bool8 ModSpeciesIsLegendary(u16 species)
{
    u32 i;
    for (i = 0; i < ARRAY_COUNT(sLegendaries); i++)
        if (sLegendaries[i] == species)
            return TRUE;
    return FALSE;
}

// Longest evolution path below `species` (0 for a species that cannot evolve).
static u8 MaxDepthBelow(u16 species, u8 guard)
{
    u8 best = 0;
    u32 i;
    if (guard > 4)
        return 0;
    for (i = 0; i < EVOS_PER_MON; i++)
    {
        u16 target = gEvolutionTable[species][i].targetSpecies;
        if (gEvolutionTable[species][i].method != 0 && ModSpeciesIsValid(target))
        {
            u8 depth = 1 + MaxDepthBelow(target, guard + 1);
            if (depth > best)
                best = depth;
        }
    }
    return best;
}

static void BuildMeta(void)
{
    u16 s;
    u32 i;

    if (sMetaBuilt)
        return;
    for (s = 0; s < NUM_SPECIES; s++)
    {
        sPreEvo[s] = SPECIES_NONE;
        sStage[s] = 0;
        sChainLength[s] = 1;
    }
    for (s = 1; s < NUM_SPECIES; s++)
    {
        if (!ModSpeciesIsValid(s))
            continue;
        for (i = 0; i < EVOS_PER_MON; i++)
        {
            u16 target = gEvolutionTable[s][i].targetSpecies;
            if (gEvolutionTable[s][i].method != 0 && ModSpeciesIsValid(target) && sPreEvo[target] == SPECIES_NONE)
                sPreEvo[target] = s;
        }
    }
    for (s = 1; s < NUM_SPECIES; s++)
    {
        u16 root = s;
        u8 stage = 0;
        if (!ModSpeciesIsValid(s))
            continue;
        while (sPreEvo[root] != SPECIES_NONE && stage < 4)
        {
            root = sPreEvo[root];
            stage++;
        }
        sStage[s] = stage;
        sChainLength[s] = 1 + MaxDepthBelow(root, 0);
    }
    sMetaBuilt = TRUE;
}

u8 ModSpeciesStage(u16 species)
{
    BuildMeta();
    return ModSpeciesIsValid(species) ? sStage[species] : 0;
}

u8 ModSpeciesChainLength(u16 species)
{
    BuildMeta();
    return ModSpeciesIsValid(species) ? sChainLength[species] : 1;
}

u16 ModSpeciesPreEvolution(u16 species)
{
    BuildMeta();
    return ModSpeciesIsValid(species) ? sPreEvo[species] : SPECIES_NONE;
}

u16 ModSpeciesFamilyRoot(u16 species)
{
    u8 guard = 0;
    BuildMeta();
    if (!ModSpeciesIsValid(species))
        return species;
    while (sPreEvo[species] != SPECIES_NONE && guard++ < 4)
        species = sPreEvo[species];
    return species;
}

// --- tables -------------------------------------------------------------------

static u16 sMap[MOD_RAND_GROUP_COUNT][NUM_SPECIES];
static bool8 sTablesBuilt;
static u32 sBuiltSeed;
static u8 sBuiltGroupFlags[MOD_RAND_GROUP_COUNT];
static u8 sBuiltRandFlags;

static u8 GroupFlags(const struct ModSettings *settings, u8 group)
{
    switch (group)
    {
    case MOD_RAND_GROUP_STARTER: return settings->randStarters;
    case MOD_RAND_GROUP_WILD: return settings->randWild;
    case MOD_RAND_GROUP_TRAINER: return settings->randTrainers;
    default: return 0;
    }
}

static u32 NextRandom(u32 *state)
{
    *state = ISO_RANDOMIZE2(*state);
    return *state >> 16;
}

static bool8 SharesType(u16 a, u16 b)
{
    const u8 *ta = gSpeciesInfo[a].types;
    const u8 *tb = gSpeciesInfo[b].types;
    return ta[0] == tb[0] || ta[0] == tb[1] || ta[1] == tb[0] || ta[1] == tb[1];
}

static void BuildGroup(u8 group, u32 seed, u8 flags, bool8 allowLegendaries)
{
    u16 pool[NUM_SPECIES];
    u16 typed[NUM_SPECIES];
    u32 state = seed ^ (0x9E3779B9u * (group + 1)) ^ 0x6D2B79F5u;
    u16 s, c;

    for (s = 0; s < NUM_SPECIES; s++)
        sMap[group][s] = s;
    if (!(flags & MOD_RAND_ENABLED))
        return;

    for (s = 1; s < NUM_SPECIES; s++)
    {
        u32 poolCount = 0, typedCount = 0;
        if (!ModSpeciesIsValid(s))
            continue;
        for (c = 1; c < NUM_SPECIES; c++)
        {
            if (!ModSpeciesIsValid(c))
                continue;
            if (!allowLegendaries && ModSpeciesIsLegendary(c) && !ModSpeciesIsLegendary(s))
                continue;
            if ((flags & MOD_RAND_POWER_BALANCE) && (sStage[c] != sStage[s] || sChainLength[c] != sChainLength[s]))
                continue;
            pool[poolCount++] = c;
            if ((flags & MOD_RAND_MATCH_TYPES) && SharesType(c, s))
                typed[typedCount++] = c;
        }
        if ((flags & MOD_RAND_MATCH_TYPES) && typedCount > 0)
            sMap[group][s] = typed[NextRandom(&state) % typedCount];
        else if (poolCount > 0)
            sMap[group][s] = pool[NextRandom(&state) % poolCount];
    }
}

void ModRandomizerRebuild(void)
{
    const struct ModSettings *settings = ModGetSettings();
    u8 group;

    BuildMeta();
    for (group = 0; group < MOD_RAND_GROUP_COUNT; group++)
    {
        sBuiltGroupFlags[group] = GroupFlags(settings, group);
        BuildGroup(group, settings->seed, sBuiltGroupFlags[group], (settings->randFlags & MOD_RANDF_ALLOW_LEGENDARIES) != 0);
    }
    sBuiltSeed = settings->seed;
    sBuiltRandFlags = settings->randFlags;
    sTablesBuilt = TRUE;
}

static void EnsureTables(void)
{
    const struct ModSettings *settings = ModGetSettings();
    u8 group;

    if (!sTablesBuilt || sBuiltSeed != settings->seed || sBuiltRandFlags != settings->randFlags)
    {
        ModRandomizerRebuild();
        return;
    }
    for (group = 0; group < MOD_RAND_GROUP_COUNT; group++)
    {
        if (sBuiltGroupFlags[group] != GroupFlags(settings, group))
        {
            ModRandomizerRebuild();
            return;
        }
    }
}

bool8 ModRandomizerGroupEnabled(u8 group)
{
    return (GroupFlags(ModGetSettings(), group) & MOD_RAND_ENABLED) != 0;
}

u16 ModRandomizeSpecies(u8 group, u16 species)
{
    if (group >= MOD_RAND_GROUP_COUNT || !ModRandomizerGroupEnabled(group) || !ModSpeciesIsValid(species))
        return species;
    EnsureTables();
    return sMap[group][species];
}

u16 ModRandomizeWildEncounter(u16 species)
{
    if (gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PYRAMID_FLOOR
     || gMapHeader.mapLayoutId == LAYOUT_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_WILD_MONS)
        return species;
    return ModRandomizeSpecies(MOD_RAND_GROUP_WILD, species);
}

#endif // WASM
