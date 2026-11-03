/*
 * Marked on the touch screen: a button of our own in the overlay's row.
 *
 * Marked fires by itself at a target on the floor, and the only way to stop it
 * is the drop action. 0x516fa8, while the target exists (player+0x1798), asks
 * 0x671c6c(manager, 11, player+0x19ec) - 0x8b6600, the device's vtable+0x50,
 * IsActionTriggered - and on a yes removes the target (vtable+0x58, then
 * 0x82eac4(ptr, 0)). Moving the shooting stick again brings a new target.
 * On a keyboard that is Ctrl. On the touch overlay the drop action comes only
 * from two invisible zones over the HUD (0x86122c..0x86125c, the ItemButtons
 * at overlay+0x437d8 and +0x439a0, lit by a trinket and by an active item), so
 * a player with neither has no way to stop firing at all. The owner asked for
 * a button in the row (12.09.2026); this is it, and it is OUR addition.
 *
 * It is made to look like the game's own item button, whose drawing was read
 * in the image:
 *
 *   0x86410c  the button's picture: a 72 x 64 render target (made at 0x863fd8)
 *             into which the HUD draws the item (0x6e863c) at 36,32, scale 2
 *   0x8642c0  the button's draw: that picture, scaled into the button's box,
 *             colour (1,1,1,0.5) - and (0.5,0.5,0.5,0.5) while it is held
 *   0x860318  the row: the box is (72, 64) * f and the gap 3 * f, so the
 *             picture fills the box exactly
 *
 * So the item, 32 pixels square, is drawn at twice its size times f - which is
 * the box's width over 36 - centred on the box, at half opacity, darker while
 * held. There is nothing else in the game's picture of a passive item, so one
 * sprite gives the same result as the render target without making one.
 *
 * Where it stands: the row's left end. The game lays out its lists from the
 * first button's centre and puts the bomb after them, beside the stick
 * (0x860488); hud.c moves the whole row so its right end sits beside the ring.
 * A button in front of the first one leaves that right end, the bomb and the
 * gap to the stick exactly as they are. With no button of the game's in the
 * row, ours alone ends where the row would end.
 *
 * What it does: a finger put down on it is eaten, and on the next overlay walk
 * (0x861d1c, hook E) the drop action is published once with a strength of one,
 * right after the overlay's own zero - the same moment and the same call as the
 * card drop in pocket.c. One frame up, then the overlay's zero again: a single
 * trigger, which is what the game asks for. Other readers of action 11 see the
 * same press; Tainted Forgotten's lift triggers on it too.
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
unsigned long shim_pocket_overlay_at(void);
int shim_sticks_in_game(void);
float shim_screen_width(void);
float shim_screen_height(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
void *shim_str_assign(void *self, const char *s);
void shim_str_dtor(void *self);
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf);

#define AREA               0xabec64     /* the fitted picture: width, height, x, y in pixels */
#define TOUCH_PLAYER       0x85fa3c     /* the player whose device is the screen, or none */
#define FN_HAS_COLLECTIBLE 0x4eeb74     /* (player, id, ignore modifiers) */
#define ITEM_MARKED        394
#define ITEM_EYE_OCCULT    572          /* only named in the report line */
#define INPUT_SYSTEM       0xac5528     /* the object itself, not a pointer to it */
#define FN_PRESS           0x8b6960     /* (input, action, strength, ctx) */
#define INPUT_DROP         11

#define OVERLAY_DRAWN      0x004        /* byte: the overlay draws this frame (0x861e08) */
#define OVERLAY_LIVE       0x684        /* byte: the overlay is up (0x85f8d4) */


#define FN_SPRITE_NEW      0x89c16c     /* Sprite::Sprite(this) */
#define FN_SPRITE_LOAD     0x89fe24     /* Sprite::Load(this, std::string const *, bool force) */
#define FN_SPRITE_FRAME    0x89eab8     /* Sprite::SetFrame(this, const char *animation, int frame) */
#define FN_SPRITE_RENDER   0x89e4c0     /* Sprite::Render(this, pos, clampTop, clampBottom) */
#define ZERO_VECTOR        0x95e7a0
#define SPRITE_BYTES       0x158
#define SPRITE_COLOUR      0xe8         /* four floats, multiplied into every layer */
#define SPRITE_SCALE       0xdc         /* two floats */
#define SPRITE_LOADED      0x149

#define FN_SPRITE_REPLACE  0x8a0228     /* Sprite::ReplaceSpritesheet(this, int layer, std::string const *) */
#define FN_SPRITE_GRAPHICS 0x8a0284     /* Sprite::LoadGraphics(this, 0) */

#define BUTTON_ANM2        "gfx/port_marked_button.anm2"
#define ITEM_PICTURE       "gfx/items/collectibles/collectibles_394_marked.png"
#define BUTTON_ANIMATION   "Idle"
#define PICTURE_UNITS      36.0f        /* the box's width at a scale of one: 72 over 2 */
/*
 * The game's item buttons are drawn at the touch controls' one opacity - a half
 * of the game's own (0x86431c), the player's since the editor's slider (loader.c
 * touch_alpha_words) - and ours with them.
 */
float shim_touch_alpha_live(void);
#define BUTTON_ALPHA       (shim_touch_alpha_live())
#define HELD_SHADE         0.5f         /* 0x864314 */
#define STALE_FRAMES       2            /* a row not laid out since then is not shown */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static long g_frame;                    /* counted between frames */
static long g_placed_at = -100;         /* the frame the row last placed the button */
static int g_wanted;                    /* the touch player carries Marked */
static float g_cx, g_cy, g_w, g_h;      /* the box, in the picture's units, as the row's */
static int g_id = -1;                   /* the finger on the button */
static int g_press;                     /* a press waiting to be published */

/* ------------------------------------------------------------- the row */

/*
 * The frame thread, from the row's layout: does the button take part.
 *
 * Asked the way the game itself asks at 0x516f78..0x516fd8 (27.09.2026), not
 * by item: the player's weapon (player+0x1760) answers through its vtable
 * +0x68 with the kind of target it fires at, and the drop action removes that
 * target only when the answer is not zero and player+0x2678 is not 2. The
 * answers, one function per weapon class:
 *
 *   Brimstone 0x6673c8, Laser 0x664154    Eye of the Occult -> 153, Marked -> 30
 *   Tears 0x7d99e4, Bombs 0x79126c,       Marked -> 30, Marked with the Eye -> 153,
 *   Fetus 0x773930, Knife 0x74c4cc,       the Eye alone -> 0
 *   TechX 0x7efc68, Lung 0x7757e4
 *   Rockets 0x74cf44 (Epic Fetus)         Marked -> 30
 *   Bone, Ludovico, Urn of Souls,         0 always
 *   Spirit Sword, Umbilical Whip
 *
 * So the Eye of the Occult brings the button only with Brimstone and the
 * lasers - the synergies where it, too, fires at a target of its own that Ctrl
 * drops on a keyboard - and Marked brings it only where it has a target at all.
 */
#define PLAYER_WEAPON      0x1760       /* the first weapon, as 0x516f7c reads it */
#define WEAPON_TARGET      0x68         /* vtable slot: the kind of target, 0 for none */
#define PLAYER_NO_DROP     0x2678       /* 2: the target stays whatever is pressed */

int shim_marked_wanted(void) {
    unsigned long b = shim_image_base();
    unsigned long overlay = shim_pocket_overlay_at();
    if (!b || !overlay || !shim_sticks_in_game()) return 0;
    if (!*(const unsigned char *) (overlay + OVERLAY_LIVE)) return 0;
    unsigned long p = ((unsigned long (*)(void)) (b + TOUCH_PLAYER))();
    if (!p) return 0;
    if (*(const int *) (p + PLAYER_NO_DROP) == 2) return 0;
    unsigned long weapon = *(const unsigned long *) (p + PLAYER_WEAPON);
    if (!weapon) return 0;
    unsigned long vtable = *(const unsigned long *) weapon;
    int target = ((int (*)(unsigned long)) *(const unsigned long *) (vtable + WEAPON_TARGET))(weapon);
    static int told_target = -1;
    if (target != told_target && shim_report_active()) {
        told_target = target;
        char line[120];
        snprintf(line, sizeof(line), "the weapon's target kind is %d (Marked %d, Eye of the Occult %d)",
                 target, ((int (*)(unsigned long, int, int)) (b + FN_HAS_COLLECTIBLE))(p, ITEM_MARKED, 0),
                 ((int (*)(unsigned long, int, int)) (b + FN_HAS_COLLECTIBLE))(p, ITEM_EYE_OCCULT, 0));
        report_line("marked", line);
    }
    return target != 0;
}

/* the frame thread, from the row's layout (hud.c): where the button stands this frame */
void shim_marked_place(int wanted, float cx, float cy, float w, float h) {
    static int told;
    pthread_mutex_lock(&g_lock);
    g_wanted = wanted && w > 0.0f && h > 0.0f;
    g_cx = cx;
    g_cy = cy;
    g_w = w;
    g_h = h;
    g_placed_at = g_frame;
    int now = g_wanted;
    pthread_mutex_unlock(&g_lock);
    if (now && told < 4 && shim_report_active()) {
        told++;
        char line[160];
        snprintf(line, sizeof(line), "the button stands at %.1f,%.1f, %.1f x %.1f", (double) cx,
                 (double) cy, (double) w, (double) h);
        report_line("marked", line);
    }
}

/* under the lock */
static int showing(void) {
    return g_wanted && g_frame - g_placed_at <= STALE_FRAMES;
}

/* ------------------------------------------------------------- the finger */

/*
 * A touch, from screen pixels into the picture's units, which the row is laid
 * out in: the button is drawn from hook O, inside the overlay's widget pass
 * (0x861df8 at 0x671c08), while the viewport is still the picture - the sticks
 * alone get the whole window, after it (screen.c, "one finger, two spaces").
 */
int shim_screen_finger_units(float x, float y, float *u, float *v);

static int to_field(float x, float y, float *gx, float *gy) {
    return shim_screen_finger_units(x, y, gx, gy);
}

/*
 * A touch, before it reaches the queue (uikit.c). Answers 1 if it is ours: the
 * whole finger, from the moment it lands on the button until it is lifted, so
 * the stick under a thumb that slides off never starts moving.
 */
int shim_marked_touch(int id, int phase, float x, float y) {
    int answer = 0;
    pthread_mutex_lock(&g_lock);
    if (phase == 0) {
        float gx, gy;
        if (g_id < 0 && showing() && to_field(x, y, &gx, &gy)
            && fabsf(gx - g_cx) <= g_w * 0.5f && fabsf(gy - g_cy) <= g_h * 0.5f) {
            g_id = id;
            g_press = 1;
            answer = 1;
        }
    } else if (id == g_id) {
        answer = 1;
        if (phase >= 3) g_id = -1;          /* ended or cancelled */
    }
    pthread_mutex_unlock(&g_lock);
    return answer;
}

/* the overlay's walk over the pressed widgets (hook E): the press, once */
void shim_marked_publish(unsigned long ctx) {
    pthread_mutex_lock(&g_lock);
    int press = g_press && g_wanted;
    g_press = 0;
    pthread_mutex_unlock(&g_lock);
    if (!press) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    ((void (*)(unsigned long, int, float, unsigned long)) (b + FN_PRESS))(
        b + INPUT_SYSTEM, INPUT_DROP, 1.0f, ctx);
    static int told;
    if (told < 12 && shim_report_active()) {
        told++;
        report_line("marked", "pressed: the drop action is published for one frame");
    }
}

/* ------------------------------------------------------------- the drawing */

static unsigned char g_sprite[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_sprite_state;      /* 0 untried, 1 ready, -1 no good */

static int sprite_ready(void) {
    if (g_sprite_state) return g_sprite_state > 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    g_sprite_state = -1;                /* one attempt, whatever happens */
    memset(g_sprite, 0, sizeof(g_sprite));
    ((void (*)(void *)) (b + FN_SPRITE_NEW))(g_sprite);
    long long raw[3];
    unsigned char *path = (unsigned char *) raw;
    path[23] = 0;
    shim_str_assign(path, BUTTON_ANM2);
    ((void (*)(void *, const void *, int)) (b + FN_SPRITE_LOAD))(g_sprite, path, 1);
    shim_str_dtor(path);
    /*
     * The picture itself goes in by the road eid.c already proved: the item's
     * file, named from the resources' root, into layer 0. A path written
     * inside the .anm2 is resolved beside the .anm2, and ours lives in the
     * port's assets rather than among the game's gfx.
     */
    if (g_sprite[SPRITE_LOADED]) {
        path[23] = 0;
        shim_str_assign(path, ITEM_PICTURE);
        ((void (*)(void *, int, const void *)) (b + FN_SPRITE_REPLACE))(g_sprite, 0, path);
        shim_str_dtor(path);
        ((void (*)(void *, int)) (b + FN_SPRITE_GRAPHICS))(g_sprite, 0);
        g_sprite_state = 1;
    }
    if (shim_report_active()) {
        char line[120];
        snprintf(line, sizeof(line), "%s %s", BUTTON_ANM2, g_sprite_state > 0 ? "loaded" : "NOT loaded");
        report_line("marked", line);
    }
    return g_sprite_state > 0;
}

/* the overlay's draw, at its door (hook O) */
void shim_marked_draw(void) {
    unsigned long b = shim_image_base();
    unsigned long overlay = shim_pocket_overlay_at();
    if (!b || !overlay || !*(const unsigned char *) (overlay + OVERLAY_DRAWN)) return;
    pthread_mutex_lock(&g_lock);
    int shown = showing(), held = g_id >= 0;
    float cx = g_cx, cy = g_cy, w = g_w;
    pthread_mutex_unlock(&g_lock);
    if (!shown || !sprite_ready()) return;

    ((void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME))(g_sprite, BUTTON_ANIMATION, 0);
    float *colour = (float *) (g_sprite + SPRITE_COLOUR);
    float shade = held ? HELD_SHADE : 1.0f;
    colour[0] = shade;
    colour[1] = shade;
    colour[2] = shade;
    colour[3] = BUTTON_ALPHA;
    float *scale = (float *) (g_sprite + SPRITE_SCALE);
    scale[0] = w / PICTURE_UNITS;
    scale[1] = w / PICTURE_UNITS;
    float pos[2] = { cx, cy };
    ((void (*)(void *, const float *, const void *, const void *)) (b + FN_SPRITE_RENDER))(
        g_sprite, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));
}

/* ------------------------------------------------------------- the rest */

/*
 * WHAT WAS HERE, AND WHY IT IS NOT.
 *
 * A diagnostic that handed the player things so that there would be something
 * to look at: the bag and two actives, to see the row of touch buttons at its
 * widest, and sixty passive items - every seventh of the 426 in the game's own
 * items.xml - so the item mod's list was long enough to scroll and be judged
 * (12-14.09.2026). All of it stood behind shim_report_active(), so a clean
 * build never reached it.
 *
 * Taken out whole at the owner's word (14.09.2026): a gate is not the same
 * thing as absence, and nothing in this port may put an item into a run that
 * the run did not earn. What is left in this file only watches and reports.
 * If the row or the scrolling ever wants testing again, a real run tests them
 * better than sixty planted items.
 *
 * The addresses it used, kept here because they cost nothing and were dearly
 * bought: 0x500588 AddCollectible(player, id, charge, firstTime, slot, var),
 * 0x505b04 SetPocketActiveItem, 0x515170 AddCard, 0x515178 AddPill,
 * 0x4f0ca0 AddBombs, 0x4da574 Player(manager, index), the player manager at
 * Game+0x25c40.
 */

void shim_marked_between_frames(void) {
    pthread_mutex_lock(&g_lock);
    g_frame++;
    pthread_mutex_unlock(&g_lock);
}

/* the button's animation, into the resource table beside the game's own gfx */
void shim_marked_open(void *manager) {
    if (!manager) return;
    shim_asset_bring_in(manager, "port_marked_button.anm2", "gfx", "port_marked_button.anm2");
    LOG("marked: the button's animation is in the resource table");
}
