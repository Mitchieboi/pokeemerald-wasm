#include "global.h"
#if WASM
#include "mod/mod_mp.h"
#include "event_object_movement.h"
#include "field_camera.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "graphics.h"
#include "main.h"
#include "overworld.h"
#include "script.h"
#include "sprite.h"
#include "battle.h"
#include "constants/battle.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"

#define MP_QUEUE_LEN 8
#define MP_TELEPORT_DISTANCE 3 // tiles behind before a remote player jumps instead of walking
#define MP_SPRITE_MAGIC 0x4D50 // "MP" in sprite data[1] so stale sprite ids are never mistaken for ours
#define MP_PAL_TAG_BRENDAN 0x7A01
#define MP_PAL_TAG_MAY 0x7A02

u8 GetRunningDirectionAnimNum(u8 direction);
u8 GetPlayerAvatarGraphicsIdByStateIdAndGender(u8 state, u8 gender);

#define sMpMagic data[1]
#define sMpSlot data[2]

struct MpStep
{
    s16 x, y;
    u8 facing;
    u8 framesPerStep;
};

struct MpRemote
{
    bool8 active;
    u8 gender;
    u8 mapGroup, mapNum;   // map of the newest position
    u8 avatar;
    u8 elevation;
    u8 facing;
    u8 spriteId;           // MAX_SPRITES when not drawn
    u8 drawnAvatar;
    u8 drawnGender;
    s16 x, y;              // tile the sprite stands on (or is leaving)
    // step in progress
    u8 stepDir;
    u8 stepFrame;
    u8 stepFrames;
    u8 stepPixels;
    s16 stepToX, stepToY;
    // queued steps on the same map
    struct MpStep queue[MP_QUEUE_LEN];
    u8 queueHead, queueLen;
};

static struct MpRemote sRemotes[MOD_MP_MAX_REMOTES];
static struct ModMpLocal sLocal;
static bool8 sSeenOverworld;
static u8 sLocalMapGroup = 0xFF, sLocalMapNum = 0xFF;

static void SpriteCB_MpRemote(struct Sprite *sprite)
{
    struct MpRemote *r = &sRemotes[sprite->sMpSlot];
    SetObjectSubpriorityByElevation(r->elevation, sprite, 1);
}

static bool8 IsOurSprite(struct MpRemote *r, u32 slot)
{
    struct Sprite *sprite;
    if (r->spriteId >= MAX_SPRITES)
        return FALSE;
    sprite = &gSprites[r->spriteId];
    return sprite->inUse && sprite->callback == SpriteCB_MpRemote && sprite->sMpMagic == MP_SPRITE_MAGIC && sprite->sMpSlot == (s16)slot;
}

static u8 AvatarState(u8 avatar)
{
    switch (avatar)
    {
    case MOD_MP_AVATAR_MACH_BIKE: return PLAYER_AVATAR_STATE_MACH_BIKE;
    case MOD_MP_AVATAR_ACRO_BIKE: return PLAYER_AVATAR_STATE_ACRO_BIKE;
    case MOD_MP_AVATAR_SURF: return PLAYER_AVATAR_STATE_SURFING;
    case MOD_MP_AVATAR_UNDERWATER: return PLAYER_AVATAR_STATE_UNDERWATER;
    default: return PLAYER_AVATAR_STATE_NORMAL;
    }
}

static void FreeUnusedPalettes(void)
{
    u32 i;
    bool8 brendan = FALSE, may = FALSE;
    for (i = 0; i < MOD_MP_MAX_REMOTES; i++)
    {
        if (!IsOurSprite(&sRemotes[i], i))
            continue;
        if (sRemotes[i].drawnGender == FEMALE) may = TRUE;
        else brendan = TRUE;
    }
    if (!brendan) FreeSpritePaletteByTag(MP_PAL_TAG_BRENDAN);
    if (!may) FreeSpritePaletteByTag(MP_PAL_TAG_MAY);
}

static void Despawn(struct MpRemote *r, u32 slot)
{
    if (IsOurSprite(r, slot))
    {
        // The tiles were allocated by CreateSprite for this sprite's images.
        DestroySprite(&gSprites[r->spriteId]);
    }
    r->spriteId = MAX_SPRITES;
}

static void PlaceSprite(struct Sprite *sprite, s16 x, s16 y)
{
    s16 px = x + MAP_OFFSET, py = y + MAP_OFFSET;
    SetSpritePosToOffsetMapCoords(&px, &py, 8, 16);
    sprite->x = px;
    sprite->y = py + sprite->centerToCornerVecY;
    sprite->x2 = 0;
    sprite->y2 = 0;
}

static void Spawn(struct MpRemote *r, u32 slot)
{
    u8 gfxId = GetPlayerAvatarGraphicsIdByStateIdAndGender(AvatarState(r->avatar), r->gender == FEMALE ? FEMALE : MALE);
    struct SpritePalette pal;
    u8 palIndex, spriteId;
    struct Sprite *sprite;

    pal.data = r->gender == FEMALE ? gObjectEventPal_May : gObjectEventPal_Brendan;
    pal.tag = r->gender == FEMALE ? MP_PAL_TAG_MAY : MP_PAL_TAG_BRENDAN;
    palIndex = LoadSpritePalette(&pal);
    if (palIndex == 0xFF)
        return;

    spriteId = CreateVirtualObject(gfxId, 0xF0 + slot, r->x, r->y, r->elevation, r->facing);
    if (spriteId >= MAX_SPRITES)
        return;
    sprite = &gSprites[spriteId];
    sprite->callback = SpriteCB_MpRemote;
    sprite->sMpMagic = MP_SPRITE_MAGIC;
    sprite->sMpSlot = slot;
    sprite->oam.paletteNum = palIndex;
    r->spriteId = spriteId;
    r->drawnAvatar = r->avatar;
    r->drawnGender = r->gender;
    r->stepFrames = 0;
    StartSpriteAnim(sprite, GetFaceDirectionAnimNum(r->facing));
}

static u8 DirectionTo(s16 dx, s16 dy)
{
    if (dx > 0) return DIR_EAST;
    if (dx < 0) return DIR_WEST;
    if (dy > 0) return DIR_SOUTH;
    return DIR_NORTH;
}

static u8 StepAnim(struct MpRemote *r, u8 dir)
{
    if (r->stepFrames <= 4)
        return GetMoveDirectionFastestAnimNum(dir);
    if (r->stepFrames <= 8)
        return r->avatar == MOD_MP_AVATAR_WALK ? GetRunningDirectionAnimNum(dir) : GetMoveDirectionFastAnimNum(dir);
    return GetMoveDirectionAnimNum(dir);
}

static void StartNextStep(struct MpRemote *r)
{
    struct MpStep *s;
    s16 dx, dy;

    while (r->queueLen)
    {
        s = &r->queue[r->queueHead];
        r->queueHead = (r->queueHead + 1) % MP_QUEUE_LEN;
        r->queueLen--;
        dx = s->x - r->x;
        dy = s->y - r->y;
        r->facing = s->facing;
        if (dx == 0 && dy == 0)
            continue; // a turn on the spot
        r->stepDir = DirectionTo(dx, dy);
        r->stepToX = s->x;
        r->stepToY = s->y;
        r->stepFrame = 0;
        r->stepPixels = 0;
        // Catch up when steps pile up (a faster game, or network jitter).
        r->stepFrames = s->framesPerStep ? s->framesPerStep : 16;
        if (r->queueLen >= 2 && r->stepFrames > 4)
            r->stepFrames /= 2;
        return;
    }
}

static void UpdateRemote(struct MpRemote *r, u32 slot)
{
    struct Sprite *sprite;
    u8 target;

    if (!r->active || r->mapGroup != sLocalMapGroup || r->mapNum != sLocalMapNum)
    {
        Despawn(r, slot);
        return;
    }
    if (IsOurSprite(r, slot) && (r->drawnAvatar != r->avatar || r->drawnGender != r->gender))
        Despawn(r, slot);
    if (!IsOurSprite(r, slot))
    {
        r->spriteId = MAX_SPRITES;
        Spawn(r, slot);
        if (r->spriteId >= MAX_SPRITES)
            return;
    }
    sprite = &gSprites[r->spriteId];

    if (r->stepFrames == 0)
        StartNextStep(r);

    if (r->stepFrames)
    {
        r->stepFrame++;
        target = (16 * r->stepFrame) / r->stepFrames;
        if (target > 16) target = 16;
        while (r->stepPixels < target)
        {
            switch (r->stepDir)
            {
            case DIR_EAST: sprite->x++; break;
            case DIR_WEST: sprite->x--; break;
            case DIR_SOUTH: sprite->y++; break;
            default: sprite->y--; break;
            }
            r->stepPixels++;
        }
        StartSpriteAnimIfDifferent(sprite, StepAnim(r, r->stepDir));
        if (r->stepFrame >= r->stepFrames)
        {
            r->x = r->stepToX;
            r->y = r->stepToY;
            r->stepFrames = 0;
            if (r->queueLen == 0)
                StartSpriteAnim(sprite, GetFaceDirectionAnimNum(r->facing));
        }
    }
    else
    {
        StartSpriteAnimIfDifferent(sprite, GetFaceDirectionAnimNum(r->facing));
    }
}

void ModMpOverworldTick(void)
{
    u32 i;

    sSeenOverworld = TRUE;
    sLocal.frame++;
    if (gSaveBlock1Ptr->location.mapGroup != sLocalMapGroup || gSaveBlock1Ptr->location.mapNum != sLocalMapNum)
    {
        // New map (warp or walking over a connection): everyone is re-placed in the new coordinates.
        for (i = 0; i < MOD_MP_MAX_REMOTES; i++)
            Despawn(&sRemotes[i], i);
        sLocalMapGroup = gSaveBlock1Ptr->location.mapGroup;
        sLocalMapNum = gSaveBlock1Ptr->location.mapNum;
    }
    for (i = 0; i < MOD_MP_MAX_REMOTES; i++)
        UpdateRemote(&sRemotes[i], i);
    FreeUnusedPalettes();
}

struct ModMpLocal *ModMpLocalState(void)
{
    struct ObjectEvent *player;
    u8 flags;

    if (!sSeenOverworld || gSaveBlock1Ptr == NULL)
    {
        sLocal.state = MOD_MP_STATE_NONE;
        return &sLocal;
    }

    sLocal.battle = 0;
    if (gMain.inBattle)
    {
        sLocal.state = MOD_MP_STATE_BATTLE;
        if (gBattleTypeFlags & BATTLE_TYPE_LINK)
            sLocal.battle = MOD_MP_BATTLE_LINK;
        else if (gBattleTypeFlags & BATTLE_TYPE_TRAINER)
            sLocal.battle = MOD_MP_BATTLE_TRAINER;
        else
            sLocal.battle = MOD_MP_BATTLE_WILD;
    }
    else if (gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic)
    {
        sLocal.state = (ArePlayerFieldControlsLocked() || ScriptContext_IsEnabled()) ? MOD_MP_STATE_BUSY : MOD_MP_STATE_FIELD;
    }
    else
    {
        sLocal.state = MOD_MP_STATE_MENU;
    }

    // Position: only meaningful in the overworld; otherwise keep the last known one.
    if (sLocal.state == MOD_MP_STATE_FIELD || sLocal.state == MOD_MP_STATE_BUSY)
    {
        player = &gObjectEvents[gPlayerAvatar.objectEventId];
        flags = gPlayerAvatar.flags;
        sLocal.mapGroup = gSaveBlock1Ptr->location.mapGroup;
        sLocal.mapNum = gSaveBlock1Ptr->location.mapNum;
        sLocal.x = player->currentCoords.x - MAP_OFFSET;
        sLocal.y = player->currentCoords.y - MAP_OFFSET;
        sLocal.facing = player->facingDirection;
        sLocal.elevation = player->currentElevation;
        sLocal.moving = gPlayerAvatar.tileTransitionState != 0;
        sLocal.dash = (flags & PLAYER_AVATAR_FLAG_DASH) != 0;
        if (flags & PLAYER_AVATAR_FLAG_MACH_BIKE) sLocal.avatar = MOD_MP_AVATAR_MACH_BIKE;
        else if (flags & PLAYER_AVATAR_FLAG_ACRO_BIKE) sLocal.avatar = MOD_MP_AVATAR_ACRO_BIKE;
        else if (flags & PLAYER_AVATAR_FLAG_SURFING) sLocal.avatar = MOD_MP_AVATAR_SURF;
        else if (flags & PLAYER_AVATAR_FLAG_UNDERWATER) sLocal.avatar = MOD_MP_AVATAR_UNDERWATER;
        else sLocal.avatar = MOD_MP_AVATAR_WALK;
        sLocal.mapsec = gMapHeader.regionMapSectionId;
    }
    return &sLocal;
}

void ModMpRemoteUpdate(u32 slot, u32 gender, u32 mapGroup, u32 mapNum, s32 x, s32 y, u32 facing, u32 avatar, u32 elevation, u32 framesPerStep)
{
    struct MpRemote *r;
    struct MpStep *last;
    s16 lastX, lastY;
    bool8 teleport;

    if (slot >= MOD_MP_MAX_REMOTES)
        return;
    r = &sRemotes[slot];
    if (facing < DIR_SOUTH || facing > DIR_EAST)
        facing = DIR_SOUTH;

    if (r->queueLen)
    {
        last = &r->queue[(r->queueHead + r->queueLen - 1) % MP_QUEUE_LEN];
        lastX = last->x;
        lastY = last->y;
    }
    else if (r->stepFrames)
    {
        lastX = r->stepToX;
        lastY = r->stepToY;
    }
    else
    {
        lastX = r->x;
        lastY = r->y;
    }

    teleport = !r->active || r->mapGroup != mapGroup || r->mapNum != mapNum
        || (abs(x - lastX) + abs(y - lastY)) > 1
        || r->queueLen >= MP_TELEPORT_DISTANCE;

    r->gender = gender;
    r->avatar = avatar;
    r->elevation = elevation;
    if (teleport)
    {
        if (r->active && IsOurSprite(r, slot) && (r->mapGroup != mapGroup || r->mapNum != mapNum))
            Despawn(r, slot);
        r->active = TRUE;
        r->mapGroup = mapGroup;
        r->mapNum = mapNum;
        r->x = x;
        r->y = y;
        r->facing = facing;
        r->queueLen = 0;
        r->stepFrames = 0;
        if (IsOurSprite(r, slot))
            PlaceSprite(&gSprites[r->spriteId], x, y);
        return;
    }

    r->queue[(r->queueHead + r->queueLen) % MP_QUEUE_LEN] = (struct MpStep){ .x = x, .y = y, .facing = facing, .framesPerStep = framesPerStep };
    r->queueLen++;
}

void ModMpRemoteRemove(u32 slot)
{
    if (slot >= MOD_MP_MAX_REMOTES)
        return;
    Despawn(&sRemotes[slot], slot);
    memset(&sRemotes[slot], 0, sizeof(sRemotes[slot]));
    sRemotes[slot].spriteId = MAX_SPRITES;
}

void ModMpRemoteClearAll(void)
{
    u32 i;
    for (i = 0; i < MOD_MP_MAX_REMOTES; i++)
        ModMpRemoteRemove(i);
}

u32 ModMpRemoteScreenPos(u32 slot)
{
    struct MpRemote *r;
    struct Sprite *sprite;
    s32 x, y;

    if (slot >= MOD_MP_MAX_REMOTES)
        return 0;
    r = &sRemotes[slot];
    if (!r->active || !IsOurSprite(r, slot) || gMain.callback2 != CB2_Overworld)
        return 0;
    sprite = &gSprites[r->spriteId];
    if (sprite->invisible)
        return 0;
    x = sprite->x + sprite->x2 + gSpriteCoordOffsetX;
    y = sprite->y + sprite->y2 + sprite->centerToCornerVecY + gSpriteCoordOffsetY;
    return ((u32)(x + 1024) << 16) | (u32)(y + 1024);
}
#endif
