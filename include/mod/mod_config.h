#ifndef GUARD_MOD_CONFIG_H
#define GUARD_MOD_CONFIG_H

// MultiplayerEmerald mod configuration.
//
// The host (web app) writes a `struct ModConfig` into the exported global
// `gModConfig` after instantiating the module and before calling AgbMain().
// When a new game starts, the settings are copied into the save file
// (SaveBlock1.unused_3598, see `struct ModSave`), which is the source of truth
// from then on. Every field is little-endian and the layout is frozen per
// MOD_CONFIG_VERSION; the host checks it against ModLayout() at boot.

#define MOD_CONFIG_MAGIC 0x3152454D // "MER1"
#define MOD_CONFIG_VERSION 1
#define MOD_SAVE_MAGIC 0x5653454D   // "MESV"
#define MOD_SAVE_VERSION 1
#define MOD_SAVE_SIZE 0x180         // sizeof(SaveBlock1.unused_3598)
#define MOD_PLAYER_NAME_ASCII_LEN 8 // 7 chars + NUL

// gModConfig.autoStart
#define MOD_AUTOSTART_NONE 0      // vanilla boot: copyright, intro, title, main menu
#define MOD_AUTOSTART_NEW_GAME 1  // boot straight into a new game using name/gender below
#define MOD_AUTOSTART_CONTINUE 2  // boot straight into the saved game

// ModSettings.randStarters / randWild / randTrainers
#define MOD_RAND_ENABLED (1 << 0)
#define MOD_RAND_POWER_BALANCE (1 << 1) // replacement has the same evolutionary status
#define MOD_RAND_MATCH_TYPES (1 << 2)   // replacement shares a type

// ModSettings.randFlags
#define MOD_RANDF_ALLOW_LEGENDARIES (1 << 0)

// ModSettings.nuzlocke
#define MOD_NUZ_ENABLED (1 << 0)
#define MOD_NUZ_DUPES_CLAUSE (1 << 1)
#define MOD_NUZ_SHINY_CLAUSE (1 << 2)
#define MOD_NUZ_LEVEL_CAP (1 << 3)
#define MOD_NUZ_WIPE_IS_GAME_OVER (1 << 4)

// ModSettings.qol
#define MOD_QOL_SKIP_INTRO (1 << 0)
#define MOD_QOL_INSTANT_TEXT (1 << 1)
#define MOD_QOL_AUTO_RUN (1 << 2)
#define MOD_QOL_BATTLE_ANIMS_OFF (1 << 3)
#define MOD_QOL_SET_STYLE (1 << 4)

// ModSave.runState
#define MOD_RUN_NONE 0
#define MOD_RUN_ACTIVE 1
#define MOD_RUN_FAILED 2

#define MOD_ENCOUNTER_BITMAP_BYTES 32 // 256 bits >= MAPSEC_COUNT (214)

struct ModSettings
{
    /*0x00*/ u32 seed;
    /*0x04*/ u8 randStarters;
    /*0x05*/ u8 randWild; // wild encounters, static encounters, gifts, eggs, in-game trades
    /*0x06*/ u8 randTrainers;
    /*0x07*/ u8 randFlags;
    /*0x08*/ u8 nuzlocke;
    /*0x09*/ u8 qol;
    /*0x0A*/ u8 reserved[6];
}; /* size = 0x10 */

struct ModConfig
{
    /*0x00*/ u32 magic;
    /*0x04*/ u16 version;
    /*0x06*/ u16 size;
    /*0x08*/ u8 autoStart;
    /*0x09*/ u8 gender; // MALE / FEMALE
    /*0x0A*/ u8 playerNameAscii[MOD_PLAYER_NAME_ASCII_LEN];
    /*0x12*/ u16 entropy; // host-provided randomness used to seed the game's RNG / trainer id
    /*0x14*/ struct ModSettings settings;
}; /* size = 0x24 */

struct ModSave
{
    /*0x00*/ u32 magic;
    /*0x04*/ u16 version;
    /*0x06*/ u16 size;
    /*0x08*/ struct ModSettings settings;
    /*0x18*/ u8 encounterUsed[MOD_ENCOUNTER_BITMAP_BYTES]; // Nuzlocke: one bit per MAPSEC
    /*0x38*/ u8 runState;
    /*0x39*/ u8 pad;
    /*0x3A*/ u16 deaths;
    /*0x3C*/ u32 flags;
    /*0x40*/ u8 reserved[MOD_SAVE_SIZE - 0x40];
}; /* size = 0x180 */

extern struct ModConfig gModConfig;

// Layout introspection for the host (see ModLayout keys in mod_config.c).
u32 ModLayout(u32 key);

// Called at the end of NewGameInitData(): persist gModConfig.settings into the save.
void ModOnNewGame(void);

// Called from the boot sequence once the save has been loaded. Returns TRUE if it
// took over the main callback (new game / continue), FALSE for the vanilla flow.
bool8 ModTryAutoStart(void);

// Settings of the running save (all zero when the save has no mod data).
const struct ModSettings *ModGetSettings(void);
// Mod save area, or NULL when the save has no (valid) mod data.
struct ModSave *ModGetSave(void);
bool8 ModHasSave(void);

// ASCII -> game text, truncating to `maxLen` characters, EOS-terminated.
// Unsupported characters are skipped. Returns the number of characters written.
u32 ModAsciiToGameString(u8 *dest, const char *ascii, u32 maxLen);

#endif // GUARD_MOD_CONFIG_H
