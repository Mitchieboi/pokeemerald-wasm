#include "global.h"
#if WASM
#include "mod/mod_config.h"
#include "mod/mod_qol.h"
#include "main.h"
#include "task.h"
#include "sprite.h"
#include "palette.h"
#include "window.h"
#include "overworld.h"
#include "save.h"
#include "constants/characters.h"
#include "constants/rgb.h"

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

// Written by the host before AgbMain(). Lives in .data (non-zero initialiser)
// so nothing on the C side ever clears it.
struct ModConfig gModConfig = { .magic = 0, .version = MOD_CONFIG_VERSION, .size = sizeof(struct ModConfig) };

static const struct ModSettings sNoSettings = {0};

STATIC_ASSERT(sizeof(struct ModSettings) == 0x10, ModSettingsSize);
STATIC_ASSERT(sizeof(struct ModConfig) == 0x24, ModConfigSize);
STATIC_ASSERT(sizeof(struct ModSave) == MOD_SAVE_SIZE, ModSaveSize);
STATIC_ASSERT(sizeof(((struct SaveBlock1 *)0)->unused_3598) == MOD_SAVE_SIZE, ModSaveFits);

u32 ModLayout(u32 key)
{
    switch (key)
    {
    case 0: return MOD_CONFIG_VERSION;
    case 1: return sizeof(struct ModConfig);
    case 2: return offsetof(struct ModConfig, autoStart);
    case 3: return offsetof(struct ModConfig, gender);
    case 4: return offsetof(struct ModConfig, playerNameAscii);
    case 5: return offsetof(struct ModConfig, settings);
    case 6: return sizeof(struct ModSettings);
    case 7: return offsetof(struct ModSettings, seed);
    case 8: return offsetof(struct ModSettings, randStarters);
    case 9: return offsetof(struct ModSettings, randWild);
    case 10: return offsetof(struct ModSettings, randTrainers);
    case 11: return offsetof(struct ModSettings, randFlags);
    case 12: return offsetof(struct ModSettings, nuzlocke);
    case 13: return offsetof(struct ModSettings, qol);
    case 14: return sizeof(struct ModSave);
    case 15: return offsetof(struct SaveBlock1, unused_3598);
    case 16: return offsetof(struct ModSave, settings);
    case 17: return offsetof(struct ModSave, encounterUsed);
    case 18: return offsetof(struct ModSave, runState);
    case 19: return offsetof(struct ModSave, deaths);
    case 20: return offsetof(struct SaveBlock2, playerName);
    case 21: return offsetof(struct SaveBlock2, playerGender);
    case 22: return offsetof(struct SaveBlock2, playTimeHours);
    case 23: return MOD_SAVE_MAGIC;
    case 24: return MOD_CONFIG_MAGIC;
    case 25: return offsetof(struct ModConfig, entropy);
    default: return 0xFFFFFFFF;
    }
}

static struct ModSave *ModSaveRaw(void)
{
    if (gSaveBlock1Ptr == NULL)
        return NULL;
    return (struct ModSave *)gSaveBlock1Ptr->unused_3598;
}

bool8 ModHasSave(void)
{
    struct ModSave *save = ModSaveRaw();
    return save != NULL && save->magic == MOD_SAVE_MAGIC && save->size == sizeof(struct ModSave);
}

struct ModSave *ModGetSave(void)
{
    return ModHasSave() ? ModSaveRaw() : NULL;
}

const struct ModSettings *ModGetSettings(void)
{
    struct ModSave *save = ModGetSave();
    return save != NULL ? &save->settings : &sNoSettings;
}

static bool8 ModConfigValid(void)
{
    return gModConfig.magic == MOD_CONFIG_MAGIC
        && gModConfig.version == MOD_CONFIG_VERSION
        && gModConfig.size == sizeof(struct ModConfig);
}

void ModOnNewGame(void)
{
    struct ModSave *save = ModSaveRaw();
    u32 i;

    if (save == NULL)
        return;
    for (i = 0; i < sizeof(struct ModSave); i++)
        ((u8 *)save)[i] = 0;
    save->magic = MOD_SAVE_MAGIC;
    save->version = MOD_SAVE_VERSION;
    save->size = sizeof(struct ModSave);
    if (ModConfigValid())
        save->settings = gModConfig.settings;
    save->runState = (save->settings.nuzlocke & MOD_NUZ_ENABLED) ? MOD_RUN_ACTIVE : MOD_RUN_NONE;

    ModQolApplyNewGameDefaults();
    ModQolSkipLittlerootIntro();
}

static u8 AsciiToGameChar(char c)
{
    if (c >= 'A' && c <= 'Z') return CHAR_A + (c - 'A');
    if (c >= 'a' && c <= 'z') return CHAR_a + (c - 'a');
    if (c >= '0' && c <= '9') return CHAR_0 + (c - '0');
    switch (c)
    {
    case ' ': return CHAR_SPACE;
    case '!': return CHAR_EXCL_MARK;
    case '?': return CHAR_QUESTION_MARK;
    case '.': return CHAR_PERIOD;
    case '-': return CHAR_HYPHEN;
    case ',': return CHAR_COMMA;
    case '/': return CHAR_SLASH;
    case '\'': return CHAR_SGL_QUOTE_RIGHT;
    case '"': return CHAR_DBL_QUOTE_LEFT;
    case '&': return CHAR_AMPERSAND;
    case '+': return CHAR_PLUS;
    case ':': return CHAR_COLON;
    case ';': return CHAR_SEMICOLON;
    case '%': return CHAR_PERCENT;
    case '(': return CHAR_LEFT_PAREN;
    case ')': return CHAR_RIGHT_PAREN;
    case '<': return CHAR_LESS_THAN;
    case '>': return CHAR_GREATER_THAN;
    case '*': return CHAR_MULT_SIGN;
    case '_': return CHAR_UNDERSCORE;
    default: return EOS; // unsupported
    }
}

u32 ModAsciiToGameString(u8 *dest, const char *ascii, u32 maxLen)
{
    u32 n = 0;
    while (*ascii != '\0' && n < maxLen)
    {
        u8 ch = AsciiToGameChar(*ascii++);
        if (ch != EOS)
            dest[n++] = ch;
    }
    dest[n] = EOS;
    return n;
}

static u8 sAutoStartMode;

static void CB2_ModAutoStart(void)
{
    // Mirror the state the main menu leaves behind before handing over.
    ResetTasks();
    ResetSpriteData();
    FreeAllSpritePalettes();
    ResetPaletteFade();
    FreeAllWindowBuffers();
    gPlttBufferUnfaded[0] = RGB_BLACK;
    gPlttBufferFaded[0] = RGB_BLACK;

    // Vanilla seeds the RNG (and the lower trainer id) from Timer 1 when the player
    // confirms their name in the Birch speech, which we skip. Seed from host entropy.
    REG_TM1CNT_L = gModConfig.entropy;
    SeedRngAndSetTrainerId();

    if (sAutoStartMode == MOD_AUTOSTART_NEW_GAME)
    {
        char ascii[MOD_PLAYER_NAME_ASCII_LEN];
        u32 i;

        gSaveBlock2Ptr->playerGender = gModConfig.gender == FEMALE ? FEMALE : MALE;
        for (i = 0; i < MOD_PLAYER_NAME_ASCII_LEN - 1; i++)
            ascii[i] = gModConfig.playerNameAscii[i];
        ascii[MOD_PLAYER_NAME_ASCII_LEN - 1] = '\0';
        if (ModAsciiToGameString(gSaveBlock2Ptr->playerName, ascii, PLAYER_NAME_LENGTH) == 0)
            ModAsciiToGameString(gSaveBlock2Ptr->playerName, "MAY", PLAYER_NAME_LENGTH);
        SetMainCallback2(CB2_NewGame);
    }
    else
    {
        SetMainCallback2(CB2_ContinueSavedGame);
    }
}

bool8 ModTryAutoStart(void)
{
    if (!ModConfigValid())
        return FALSE;
    switch (gModConfig.autoStart)
    {
    case MOD_AUTOSTART_NEW_GAME:
        break;
    case MOD_AUTOSTART_CONTINUE:
        if (gSaveFileStatus != SAVE_STATUS_OK && gSaveFileStatus != SAVE_STATUS_ERROR)
            return FALSE;
        break;
    default:
        return FALSE;
    }
    sAutoStartMode = gModConfig.autoStart;
    SetMainCallback2(CB2_ModAutoStart);
    return TRUE;
}

#endif // WASM
