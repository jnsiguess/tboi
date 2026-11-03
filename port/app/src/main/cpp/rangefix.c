/*
 * RANGE FIX, WITHOUT LUA (04.10.2026).
 *
 * Two PC mods by Cuerzor, merged into one switch on MODS at the owner's word:
 * "Range fix for Bones & Sword" (3033566163, 1.1) and "Range fix for Bag of
 * Crafting" (3033565322, 1.1). Both do one thing, in MC_POST_KNIFE_UPDATE on
 * a knife of subtype 4 - the hitbox a melee swing spawns - on its first
 * update (FrameCount 0):
 *
 *   scale = TargetPosition.X * factor / 450 + 1     (Bones & Sword only)
 *   TargetPosition = (4, 0)
 *   Scale *= scale; SpriteScale *= scale           (Bones & Sword only)
 *   the "parent" - a knife of the same variant, subtype 0, same spawner
 *   (Isaac.FindByType(8, variant, 0)); the BOC mod tries knife.Parent first -
 *   Position = parent.Position + FromAngle(Rotation) * 8, Velocity = 0
 *
 * Bones & Sword covers variants 1, 2, 3, 9, 10, 11 (bone club, scythe, donkey
 * jawbone, notched axe, spirit sword, tech sword) with "Enabled" and "Range
 * Factor" (5), and variant 4 (the bag) with "Enabled for BOC" and "Range
 * Factor for BOC" (0). The BOC mod is that last part with factor 0 and the
 * Parent lookup. MERGED: Bones & Sword whole, its four settings in
 * [rangefix]; the anchor is its search, and when the search finds nothing,
 * knife.Parent - the BOC mod's - so no hitbox is left unplaced.
 *
 * WHY IT IS A FIX, measured in the image. The swing (0x72c1a0..0x72c2ac, the
 * held knife's update) spawns the hitbox with 0x88fcbc(game, 8, variant, pos,
 * 0, spawner, 4, seed), makes the holder its Parent (0x82bafc) and writes its
 * +0x308 = (distance, 0), the distance being 4 for a holder of base range and
 * (range - 260) * k + 4 above it (player+0x1854). Every update of a subtype-4
 * knife (0x727a3c..0x728750) then pulls it towards anchor + FromAngle(+0x428)
 * * (+0x308).x - the anchor [+0x528] or the Parent - by setting its velocity.
 * So with high range the swing lands far out and misses what stands close.
 * +0x308 is the mod's TargetPosition: the mod puts it back to the base 4.
 *
 * THE FIELDS (Entity_Knife, vtable 0xa8ea70, Init 0x726be8):
 *   +0x38 type, +0x3c variant, +0x40 subtype, +0x1c2 alive
 *   +0x12c SpriteScale (the sprite at +0x50, its scale at +0xdc; the knife's
 *          collision radius +0x344 is SpriteScale.x * the config's, 0x72841c)
 *   +0x2f4 the game frame of its Init (0x82bd0c)
 *   +0x308 TargetPosition, +0x310 Position, +0x334 Velocity
 *   +0x390 Parent, +0x3a8 SpawnerEntity
 *   +0x420 Scale (1.0 at Init 0x726cfc; what a knife's tear gets, 0x72ed5c)
 *   +0x428 Rotation (the swing writes holder rotation + offset there)
 *
 * WHEN: right after the knife's own Update - the Lua callback's place. The
 * Update is slot 4 of the vtable (0x727704, only ever called through it);
 * that slot is ours from load (as rumble.c takes the motor slots), and ours
 * calls the game's first. FrameCount 0 is "the first Update this hitbox
 * has": a hitbox done is remembered by pointer and Init frame, and only one
 * at most two frames old is new.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"
#include "modset.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *, const char *);
int shim_tweaks_mod(int i);

#define MOD_RANGEFIX       6                /* shim_tweaks_mod: the MODS row before EID */

#define VT_KNIFE           0xa8ea70
#define VT_UPDATE          0x20             /* slot 4 */
#define FN_KNIFE_UPDATE    0x727704

#define GAME_PTR           0xac3b90
#define GAME_ROOM          0x21550
#define GAME_FRAME         0x1af34c
#define ROOM_ENTITIES      0x1950
#define LIST_ARRAY         0x78
#define LIST_COUNT         0x84

#define E_TYPE             0x38
#define E_VARIANT          0x3c
#define E_SUBTYPE          0x40
#define E_SPRITE_SCALE     0x12c
#define E_ALIVE            0x1c2
#define E_INIT_FRAME       0x2f4
#define E_POSITION         0x310
#define E_VELOCITY         0x334
#define E_PARENT           0x390
#define E_SPAWNER          0x3a8
#define K_TARGET           0x308
#define K_SCALE            0x420
#define K_ROTATION         0x428

#define TYPE_KNIFE         8
#define SUBTYPE_HITBOX     4
#define VARIANT_BAG        4

typedef void (*update_fn)(unsigned long);
static update_fn g_update;                  /* the game's Entity_Knife::Update */

/* the hitboxes done: pointer and Init frame, the last few */
#define DONE 32
static struct { unsigned long knife; int born; } g_done[DONE];
static int g_done_next;

static int melee_variant(int v) {
    return v == 1 || v == 2 || v == 3 || v == 9 || v == 10 || v == 11;
}

/* Isaac.FindByType(8, variant, 0) whose SpawnerEntity is the knife's, the first; else 0 */
static unsigned long holder_of(unsigned long b, unsigned long knife, int variant) {
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    unsigned long room = game ? *(const unsigned long *) (game + GAME_ROOM) : 0;
    if (!room) return 0;
    unsigned long list = room + ROOM_ENTITIES;
    const unsigned long *items = *(const unsigned long *const *) (list + LIST_ARRAY);
    unsigned n = *(const unsigned *) (list + LIST_COUNT);
    if (!items) return 0;
    unsigned long spawner = *(const unsigned long *) (knife + E_SPAWNER);
    if (!spawner) return 0;                 /* GetPtrHash(nil) in the mod: no match */
    for (unsigned i = 0; i < n; i++) {
        unsigned long e = items[i];
        if (!e || !*(const unsigned char *) (e + E_ALIVE)) continue;
        const int *tvs = (const int *) (e + E_TYPE);
        if (tvs[0] != TYPE_KNIFE || tvs[1] != variant || tvs[2] != 0) continue;
        if (*(const unsigned long *) (e + E_SPAWNER) == spawner) return e;
    }
    return 0;
}

static void fix(unsigned long b, unsigned long knife, int variant, int factor) {
    float *target = (float *) (knife + K_TARGET);
    float was = target[0];
    float scale = was * ((float) factor / 450.0f) + 1.0f;
    target[0] = 4.0f;
    target[1] = 0.0f;
    *(float *) (knife + K_SCALE) *= scale;
    float *sprite_scale = (float *) (knife + E_SPRITE_SCALE);
    sprite_scale[0] *= scale;
    sprite_scale[1] *= scale;

    unsigned long anchor = holder_of(b, knife, variant);
    int by = anchor ? 1 : 0;
    if (!anchor) { anchor = *(const unsigned long *) (knife + E_PARENT); by = anchor ? 2 : 0; }
    if (anchor) {
        const float *at = (const float *) (anchor + E_POSITION);
        float r = *(const float *) (knife + K_ROTATION) * (float) (M_PI / 180.0);
        float *pos = (float *) (knife + E_POSITION), *vel = (float *) (knife + E_VELOCITY);
        pos[0] = at[0] + cosf(r) * 8.0f;
        pos[1] = at[1] + sinf(r) * 8.0f;
        vel[0] = vel[1] = 0.0f;
    }
#if ISAAC_DIAG
    static int said;
    if (shim_report_active() && said < 12) {
        said++;
        char line[200];
        snprintf(line, sizeof(line), "range fix: knife variant %d: target %.1f -> 4, scale x%.2f (now %.2f, sprite %.2f), "
                 "anchored to %s", variant, (double) was, (double) scale, (double) *(const float *) (knife + K_SCALE),
                 (double) sprite_scale[0], by == 1 ? "its holder" : by == 2 ? "its parent" : "nothing");
        report_line("mods", line);
    }
#else
    (void) by;
#endif
}

static void knife_update(unsigned long knife) {
    g_update(knife);
    if (!shim_tweaks_mod(MOD_RANGEFIX)) return;
    if (*(const int *) (knife + E_SUBTYPE) != SUBTYPE_HITBOX) return;
    int variant = *(const int *) (knife + E_VARIANT), on, factor;
    if (melee_variant(variant)) {
        on = shim_modset_int(MS_RANGE_ENABLED);
        factor = shim_modset_int(MS_RANGE_FACTOR);
    } else if (variant == VARIANT_BAG) {
        on = shim_modset_int(MS_RANGE_BOC_ENABLED);
        factor = shim_modset_int(MS_RANGE_BOC_FACTOR);
    } else {
        return;
    }
    if (!on) return;
    unsigned long b = shim_image_base();
    unsigned long game = b ? *(const unsigned long *) (b + GAME_PTR) : 0;
    if (!game) return;
    /* FrameCount 0: its first Update - new to us, and born this frame or the last */
    int born = *(const int *) (knife + E_INIT_FRAME);
    int age = *(const int *) (game + GAME_FRAME) - born;
    if (age < 0 || age > 2) return;
    for (int i = 0; i < DONE; i++)
        if (g_done[i].knife == knife && g_done[i].born == born) return;
    g_done[g_done_next].knife = knife;
    g_done[g_done_next].born = born;
    g_done_next = (g_done_next + 1) % DONE;
    fix(b, knife, variant, factor);
}

/* at load, before the image is closed: the knife's Update slot is ours */
void shim_rangefix_install(unsigned char *base) {
    unsigned long *slot = (unsigned long *) (base + VT_KNIFE + VT_UPDATE);
    if (*slot == (unsigned long) knife_update) return;
    if (*slot != (unsigned long) base + FN_KNIFE_UPDATE) {
        LOG("range fix: the knife's update slot holds %lx, not the game's - left alone",
            *slot - (unsigned long) base);
        return;
    }
    g_update = (update_fn) (base + FN_KNIFE_UPDATE);
    *slot = (unsigned long) knife_update;
    LOG("range fix: the knife's update slot is ours");
}
