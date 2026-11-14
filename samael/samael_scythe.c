/*
 * SAMAEL'S SCYTHE, WITHOUT LUA (port of samaelscripts/chars/samael.lua, the melee swing).
 *
 * WHAT IT DOES (the same numbers as the mod):
 *   - while a shoot button is held, Samael swings the scythe by itself (the mod's default:
 *     swingAutomatically, no Chocolate Milk), once every calcSwingDelay frames:
 *         delay = max(floor(min(MaxFireDelay, 50)), 2)                    (samael.lua:2405)
 *   - the hitbox is a circle of size 42 (scale 1) centred on the player plus an offset in the aimed
 *     direction (samael.lua:2679, GetHitboxPosOffset):
 *         x = -40 * sin(a), y = 35 * cos(a), a = angle of (dir rotated -90 degrees)
 *   - it lives 6 frames (ScytheHitboxUpdate: removed past FrameCount 6) and hits each enemy once a swing
 *   - damage = the player's Damage * kScytheMeleeDamageMult (1.0)             (samael.lua:2713)
 *
 * WHAT IT USES, all already measured and used in the port (mods.c, spawner.c, statsplus.c):
 *   Game 0xac3b90, players list Game+0x25c40 (begin at [2], end at [3]), room Game+0x21550, the room's
 *   entities room+0x1950 (array +0x78, count +0x84), entity type +0x38, exists +0x1c2, dead +0x1c3,
 *   flags +0x1b8 (friendly = bit 29), vtable +0x90 "active enemy" and +0x58 Remove, NPC hit points
 *   +0x354, player: type +0x1738, controller +0x19ec, position +0x310, Damage +0x1844, MaxFireDelay
 *   +0x1834, ActionHeld 0x671c5c(0, action, controller), the update frame counter Game+0x1af34c.
 *
 * WHAT IS NOT MEASURED YET (and so is NOT guessed here):
 *   - Entity::TakeDamage. Until it is measured the damage goes straight to the enemy's hit points
 *     (+0x354) and an enemy that reaches zero is Removed the way the spawner's CLEAR ENEMIES does (vtable
 *     +0x58): no death animation, no drops, no flash. VT_TAKE_DAMAGE / VT_KILL below are where the real calls go.
 *   - the enemy's size and position as ENTITIES (only the player's +0x310 is measured): the report build
 *     logs the first enemies it sees so the guess can be checked (see ISAAC_DIAG lines).
 *   - the scythe's picture, its projectile (full charge), the multi-scythe, Mom's Knife, Ludovico and the
 *     other synergies. Those are other pieces of the mod, not this one.
 */
#include <math.h>
#include <stdint.h>
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
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_trace_readable(const void *p, unsigned long n);
int shim_sticks_in_game(void);

/* ---- the character's slot: the id of <player id="41"> in players.xml ---- */
#define SAMAEL_TYPE         41

/* ---- the game (all measured, see the header) ---- */
#define GAME_PTR            0xac3b90
#define GAME_PLAYER_LIST    0x25c40
#define GAME_ROOM           0x21550
#define GAME_FRAME          0x1af34c
#define ROOM_ENTITIES       0x1950
#define LIST_ARRAY          0x78
#define LIST_COUNT          0x84
#define ENT_TYPE            0x38
#define ENT_EXISTS          0x1c2
#define ENT_DEAD            0x1c3
#define ENT_FLAGS           0x1b8
#define ENT_POSITION        0x310       /* the player's is measured; an NPC is an Entity too (checked by the log) */
#define NPC_HP              0x354
#define FRIENDLY            (1ull << 29)
#define VT_REMOVE           0x58
#define VT_ACTIVE_ENEMY     0x90

#define PLAYER_TYPE         0x1738
#define PLAYER_CONTROLLER   0x19ec
#define PLAYER_POSITION     0x310
#define PLAYER_DAMAGE       0x1844
#define PLAYER_FIRE_DELAY   0x1834
#define FN_ACTION_HELD      0x671c5c    /* (0, action, controller) */

/* ButtonAction numbering: DROP 11 and MAP 13 are what the port already reads with this call */
#define ACTION_SHOOT_LEFT   4
#define ACTION_SHOOT_RIGHT  5
#define ACTION_SHOOT_UP     6
#define ACTION_SHOOT_DOWN   7

/* ---- the mod's constants (samael.lua:20-49) ---- */
#define MIN_SWING_DELAY     2
#define MAX_SWING_DELAY     50
#define MELEE_DAMAGE_MULT   1.0f
#define HITBOX_SIZE         42.0f
#define HITBOX_OFFSET_X     40.0f
#define HITBOX_OFFSET_Y     35.0f
#define HITBOX_FRAMES       6
#define ENEMY_PAD           20.0f       /* an enemy's own radius: not measured, a middle value */

/* ENTITY VTABLE SLOTS (byte offsets inside the vtable, like VT_REMOVE above). 0 = not measured yet, and the damage
 * falls back to the old hit-point write. A virtual call is used on purpose: bosses and armoured enemies override
 * TakeDamage, and a direct call to the base function would skip those overrides.
 *   VT_TAKE_DAMAGE: bool TakeDamage(this, float damage, u64 damage_flags, EntityRef *source, int countdown)
 *   VT_KILL:        void Kill(this)
 * The report build logs "vtable type N +0x...." lines with the first slots of up to 3 enemy types (as offsets from
 * the image base) so the two slots can be found in isaac.arm64. */
/* VT_TAKE_DAMAGE = 0x50, measured in isaac.arm64: the function at this slot (0x82ebd8, the same one in 9 of the
 * vtables of the entity family that uses type +0x38 / flags +0x1b8 / position +0x310, i.e. this port's Entity) takes
 * (this, s0 = damage, x1 = flags, x2 = EntityRef *source, w3 = countdown) and starts with the "should not be called
 * for Entity Player" check on the type at +0x38. The slot is the same for every subclass that overrides it.
 * VT_KILL stays 0: slot 0x58 (what VT_REMOVE calls) sets the dead byte +0x1c3 and runs the death routine, and the
 * real TakeDamage reaches it by itself when the hit points run out. To go back to the old behaviour put 0 in
 * VT_TAKE_DAMAGE. */
#define VT_TAKE_DAMAGE      0x50
#define VT_KILL             0
#define VT_DUMP_SLOTS       48

#define SLOTS               4
#define HIT_MAX             64

/* ---- the scythe's picture: an effect with the mod's own sprite (samael_scythe.anm2, "Swing", 11 frames) ----
 * Game::Spawn(game, type, variant, pos, velocity, spawner 0, subtype, seed, 0) is the call spawner.c uses; the
 * effect (1000.6620) has four records in entities2.xml, one per direction (subtype 0 down, 1 right, 2 up, 3 left),
 * each with the sprite already turned the way the mod turns it. samael_guadana_visual.py puts those records and
 * files into the APK; without them this must not be spawned, so the verifier checks both go together. */
#define FN_SPAWN            0x88fcbc
#define FN_RANDOM           0x7f1cc0
#define ZERO_VECTOR         0x95e7a0
#define FX_TYPE             1000
#define FX_VARIANT          6620
#define FX_FRAMES           11
#define FX_FOLLOW_PLAYER    1           /* the picture is moved to the player's position every frame (see fx_tick) */

typedef struct {
    int cooldown;                       /* frames until the next swing */
    int swing_left;                     /* frames the hitbox still lives */
    float dir_x, dir_y;                 /* the direction the swing was started in */
    unsigned long hit[HIT_MAX];         /* enemies already hit in this swing */
    int hit_n;
    int last_frame;
    int said;
    unsigned long fx;                   /* the scythe's picture, while it lives */
    unsigned long fx_room;
    int fx_left;
    int last_swing_frame;               /* only for the log: frames between two swings */
} slot_t;

static slot_t g_slot[SLOTS];

static int action_held(unsigned long b, unsigned long p, int action) {
    return ((int (*)(unsigned long, int, int)) (b + FN_ACTION_HELD))(
        0, action, *(const int *) (p + PLAYER_CONTROLLER)) & 1;
}

static int swing_delay(unsigned long p) {
    float d = *(const float *) (p + PLAYER_FIRE_DELAY);
    if (!(d == d)) d = 10.0f;
    if (d > (float) MAX_SWING_DELAY) d = (float) MAX_SWING_DELAY;
    int n = (int) floorf(d);
    return n < MIN_SWING_DELAY ? MIN_SWING_DELAY : n;
}

/* GetHitboxPosOffset: angle of (dir rotated -90) is atan2(-dx, dy) */
static void hitbox_offset(float dx, float dy, float *ox, float *oy) {
    float a = atan2f(-dx, dy);
    *ox = -HITBOX_OFFSET_X * sinf(a);
    *oy = HITBOX_OFFSET_Y * cosf(a);
}

static int already_hit(const slot_t *s, unsigned long e) {
    for (int i = 0; i < s->hit_n; i++) if (s->hit[i] == e) return 1;
    return 0;
}

static int is_enemy(unsigned long e) {
    if (!shim_trace_readable((const void *) e, ENT_FLAGS + 8) || !shim_trace_readable((const void *) (e + ENT_POSITION), 8))
        return 0;
    if (!*(const unsigned char *) (e + ENT_EXISTS) || *(const unsigned char *) (e + ENT_DEAD)) return 0;
    int t = *(const int *) (e + ENT_TYPE);
    if (t < 10 || t >= 1000) return 0;
    if (*(const unsigned long long *) (e + ENT_FLAGS) & FRIENDLY) return 0;
    const unsigned long *vt = *(const unsigned long *const *) e;
    if (!vt || !shim_trace_readable(vt, VT_ACTIVE_ENEMY + 8)) return 0;
    return ((int (*)(unsigned long)) vt[VT_ACTIVE_ENEMY / 8])(e) & 1;
}

static void hurt(unsigned long b, unsigned long e, unsigned long player, float damage) {
    (void) b; (void) player;
    const unsigned long *vt = *(const unsigned long *const *) e;
#if VT_TAKE_DAMAGE
    /* the game's own damage path: death animation, drops, flash, knockback, room clear */
    if (vt && shim_trace_readable(vt, VT_TAKE_DAMAGE + 8)) {
        unsigned char source[128] __attribute__((aligned(16)));
        memset(source, 0, sizeof(source));              /* an empty EntityRef, like EntityRef(nil) in Lua */
        ((unsigned (*)(unsigned long, float, unsigned long long, void *, int)) vt[VT_TAKE_DAMAGE / 8])(e, damage, 0ull, source, 0);
        return;
    }
#endif
    /* fallback while the slots are not measured: hit points by hand, and Kill (or Remove) at zero */
    float *hp = (float *) (e + NPC_HP);
    if (!shim_trace_readable(hp, 4) || !(*hp == *hp)) return;
    *hp -= damage;
    if (*hp <= 0.0f) {
        if (!vt) return;
#if VT_KILL
        if (shim_trace_readable(vt, VT_KILL + 8)) { ((void (*)(unsigned long)) vt[VT_KILL / 8])(e); return; }
#endif
        ((void (*)(unsigned long)) vt[VT_REMOVE / 8])(e);
    }
}

static unsigned long room_items(unsigned long game, const unsigned long **items, unsigned *n) {
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!room || !shim_trace_readable((const void *) (room + ROOM_ENTITIES), LIST_COUNT + 4)) return 0;
    unsigned long list = room + ROOM_ENTITIES;
    *items = *(const unsigned long *const *) (list + LIST_ARRAY);
    *n = *(const unsigned *) (list + LIST_COUNT);
    if (!*items || *n > 4096 || !shim_trace_readable(*items, (unsigned long) *n * 8)) return 0;
    return room;
}

/* is `e` still one of this room's entities? (the game frees them when the room changes) */
static int in_room(unsigned long game, unsigned long e) {
    const unsigned long *items; unsigned n;
    if (!room_items(game, &items, &n)) return 0;
    for (unsigned i = 0; i < n; i++) if (items[i] == e) return 1;
    return 0;
}

static void fx_remove(unsigned long game, slot_t *s) {
    if (s->fx && in_room(game, s->fx)) {
        const unsigned long *vt = *(const unsigned long *const *) s->fx;
        if (vt && shim_trace_readable(vt, VT_REMOVE + 8)) ((void (*)(unsigned long)) vt[VT_REMOVE / 8])(s->fx);
    }
    s->fx = 0;
}

static void fx_spawn(unsigned long b, unsigned long game, unsigned long player, slot_t *s) {
    int sub = fabsf(s->dir_x) >= fabsf(s->dir_y) ? (s->dir_x > 0.0f ? 1 : 3) : (s->dir_y > 0.0f ? 0 : 2);
    unsigned long (*spawn)(unsigned long, int, int, const float *, const float *, unsigned long, int, int, int) =
        (unsigned long (*)(unsigned long, int, int, const float *, const float *, unsigned long, int, int, int))
            (b + FN_SPAWN);
    int seed = ((int (*)(void)) (b + FN_RANDOM))();
    unsigned long e = spawn(game, FX_TYPE, FX_VARIANT, (const float *) (player + PLAYER_POSITION),
                            (const float *) (b + ZERO_VECTOR), 0, sub, seed, 0);
    if (e && in_room(game, e)) {
        s->fx = e;
        s->fx_room = *(const unsigned long *) (game + GAME_ROOM);
        s->fx_left = FX_FRAMES;
        LOG("scythe: picture spawned (subtype %d)", sub);
    }
}

static void fx_tick(unsigned long game, unsigned long player, slot_t *s) {
    if (!s->fx) return;
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!room || room != s->fx_room || !in_room(game, s->fx)) { s->fx = 0; return; }   /* the room changed */
#if FX_FOLLOW_PLAYER
    /* spawned once at the player's position, the picture stayed behind when the player moved while the hitbox
     * (which is recomputed from the player every frame) did not: that read as a delay */
    if (shim_trace_readable((const void *) (s->fx + ENT_POSITION), 8)) {
        float *fp = (float *) (s->fx + ENT_POSITION);
        const float *pp = (const float *) (player + PLAYER_POSITION);
        fp[0] = pp[0]; fp[1] = pp[1];
    }
#endif
    if (--s->fx_left <= 0) fx_remove(game, s);
}

#if ISAAC_DIAG
/* first slots of an enemy's vtable as offsets from the image base, once per enemy type (3 at most) */
static void vtable_dump(unsigned long b, unsigned long e) {
    static int seen[3];
    static int seen_n;
    if (!shim_report_active() || seen_n >= 3) return;
    int t = *(const int *) (e + ENT_TYPE);
    for (int i = 0; i < seen_n; i++) if (seen[i] == t) return;
    const unsigned long *vt = *(const unsigned long *const *) e;
    if (!vt || !shim_trace_readable(vt, VT_DUMP_SLOTS * 8)) return;
    seen[seen_n++] = t;
    for (int i = 0; i < VT_DUMP_SLOTS; i += 4) {
        char line[200];
        snprintf(line, sizeof(line), "vtable type %d +0x%03x: %lx %lx %lx %lx", t, i * 8,
                 vt[i] - b, vt[i + 1] - b, vt[i + 2] - b, vt[i + 3] - b);
        report_line("scythe", line);
    }
}
#endif

static void hit_pass(unsigned long b, unsigned long game, unsigned long player, slot_t *s, int index) {
    const unsigned long *items; unsigned n;
    if (!room_items(game, &items, &n)) return;

    const float *pp = (const float *) (player + PLAYER_POSITION);
    float ox, oy;
    hitbox_offset(s->dir_x, s->dir_y, &ox, &oy);
    float cx = pp[0] + ox, cy = pp[1] + oy;
    float reach = HITBOX_SIZE + ENEMY_PAD;
    float damage = *(const float *) (player + PLAYER_DAMAGE) * MELEE_DAMAGE_MULT;
    /* the room's entity list can grow or move while an enemy dies (blood, drops, effects are spawned), so the
     * targets are collected first and hurt afterwards, each one checked again right before the hit */
    unsigned long todo[HIT_MAX];
    int todo_n = 0;

    for (unsigned i = 0; i < n; i++) {
        unsigned long e = items[i];
        if (!e || already_hit(s, e) || !is_enemy(e)) continue;
        const float *ep = (const float *) (e + ENT_POSITION);
        float dx = ep[0] - cx, dy = ep[1] - cy;
        if (!(dx == dx) || !(dy == dy)) continue;
#if ISAAC_DIAG
        vtable_dump(b, e);
        if (s->said < 6 && shim_report_active()) {
            s->said++;
            char line[200];
            snprintf(line, sizeof(line), "scythe: enemy type %d at (%.0f, %.0f) hp %.1f, hitbox at (%.0f, %.0f), player at (%.0f, %.0f)",
                     *(const int *) (e + ENT_TYPE), (double) ep[0], (double) ep[1], (double) *(const float *) (e + NPC_HP),
                     (double) cx, (double) cy, (double) pp[0], (double) pp[1]);
            report_line("scythe", line);
        }
#endif
        if (sqrtf(dx * dx + dy * dy) > reach) continue;
        if (s->hit_n < HIT_MAX) s->hit[s->hit_n++] = e;
        if (todo_n < HIT_MAX) todo[todo_n++] = e;
    }
    for (int i = 0; i < todo_n; i++)
        if (in_room(game, todo[i]) && is_enemy(todo[i])) hurt(b, todo[i], player, damage);
    (void) index;
}

/* between frames (mods.c): once per update frame, for every Samael in the run */
void shim_samael_between_frames(void) {
    unsigned long b = shim_image_base();
    if (!b || !shim_sticks_in_game()) { memset(g_slot, 0, sizeof(g_slot)); return; }
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return;
    const unsigned long *list = (const unsigned long *) (game + GAME_PLAYER_LIST);
    const unsigned long *players = (const unsigned long *) list[2];
    if (!players || list[3] < list[2]) return;
    int n = (int) ((list[3] - list[2]) / 8);
    if (n > SLOTS) n = SLOTS;
    int frame = *(const int *) (game + GAME_FRAME);

    for (int i = 0; i < n; i++) {
        unsigned long p = players[i];
        slot_t *s = &g_slot[i];
        if (!p || *(const int *) (p + PLAYER_TYPE) != SAMAEL_TYPE) { memset(s, 0, sizeof(*s)); continue; }
        if (frame == s->last_frame) continue;           /* one step per update frame, not per drawn frame */
        s->last_frame = frame;

        fx_tick(game, p, s);
        if (s->cooldown > 0) s->cooldown--;
        if (s->swing_left > 0) {
            hit_pass(b, game, p, s, i);
            if (--s->swing_left == 0) s->hit_n = 0;
        }

        float ax = (float) action_held(b, p, ACTION_SHOOT_RIGHT) - (float) action_held(b, p, ACTION_SHOOT_LEFT);
        float ay = (float) action_held(b, p, ACTION_SHOOT_DOWN) - (float) action_held(b, p, ACTION_SHOOT_UP);
        int aiming = ax != 0.0f || ay != 0.0f;
        /* a new swing does not wait for the old hitbox to die: in the mod every swing is its own hitbox, so with a
         * short delay (below HITBOX_FRAMES) the old condition "swing_left == 0" stretched the period to 6 frames */
        if (aiming && s->cooldown == 0) {
            float len = sqrtf(ax * ax + ay * ay);
            s->dir_x = ax / len;
            s->dir_y = ay / len;
            s->swing_left = HITBOX_FRAMES;
            s->hit_n = 0;
            s->cooldown = swing_delay(p);
            fx_remove(game, s);
            fx_spawn(b, game, p, s);                    /* the picture: the mod's scythe, in the aimed direction */
            hit_pass(b, game, p, s, i);                 /* the swing's first frame hits at once */
            s->swing_left--;
            LOG("scythe: swing of player %d at frame %d (+%d since the last), dir (%.2f, %.2f), delay %d", i, frame,
                frame - s->last_swing_frame, (double) s->dir_x, (double) s->dir_y, s->cooldown);
            s->last_swing_frame = frame;
        }
    }
}

/*
 * ROOM FOR SAMAEL IN THE CHARACTER TABLE (id 41).
 *
 * The game builds its character table with `resize(41)` (ids 0..40), so a <player id="41"> is never read and the
 * menu does not show it. Yama's "charslots" patch widens it to 61 (for Fiend Folio's twenty); Samael needs ONE more,
 * so this makes it 42: `mov w1,#41` -> `mov w1,#42` at 0x7855dc, i.e. 0x52800521 -> 0x52800541. (A 61 leaves twenty
 * empty slots in the menu.) The edit is made in the loader's start-up block where __TEXT is still open for writing
 * (written plainly, not through patch_word, like the other edits there). The word is checked first: the original,
 * 42 or the older 61 are accepted, anything else leaves the image alone.
 */
void shim_samael_slots_install(unsigned char *base) {
    uint32_t *w = (uint32_t *) (base + 0x7855dc);
    if (*w != 0x52800521u && *w != 0x52800541u && *w != 0x528007A1u) {
        LOG("samael: 0x7855dc holds %08x - the character table is left alone", (unsigned) *w);
        return;
    }
    *w = 0x52800541u;
    LOG("samael: character table resized to 42 (Samael is id 41)");
}
