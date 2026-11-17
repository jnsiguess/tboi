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

#include <stdarg.h>
/* FIX10: LOG() only reaches logcat. These go to the report file (isaac-start.txt). A note made before the report is attached
 * (the table patch runs at start-up) waits in g_note and is flushed by shim_samael_between_frames. */
static char g_note[6][220];
static int  g_note_n;
static void note(const char *fmt, ...) {
    if (g_note_n >= 6) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_note[g_note_n++], sizeof g_note[0], fmt, ap);
    va_end(ap);
}
static void report_now(const char *fmt, ...) {
#if ISAAC_DIAG
    char buf[240];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (shim_report_active()) report_line("samael", buf);
#else
    (void) fmt;
#endif
}
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
/* FIX6: where the scythe's PICTURE sits against the player. samael.lua:1999 CalcScythePosOffset(angle):
 *     x = -10 * sin(a),  y = 11 * cos(a) - 9,  a = angle of (dir rotated -90)   (same angle as the hitbox)
 * i.e. down (0, +2), right (+10, -9), up (0, -20), left (-10, -9). The mod puts the scythe at player + this offset
 * (UpdateScythePosition), never at the player's centre; until fix5 the picture was at the centre. */
#define SPRITE_OFFSET_X     10.0f
#define SPRITE_OFFSET_Y     11.0f
#define SPRITE_OFFSET_BASE  -9.0f
/* FIX6: the mod snaps the swing direction to the four cardinal ones (samael.lua:2155 DirectionalVector(
 * GetDirectionFromVector(dir))) unless Marked / Analog Stick / a knife weapon. Put 0 to keep diagonal swings. */
#define SWING_CARDINAL      1
/* FIX7: the picture lagged behind the player while walking. The scythe code runs between frames, once per update, and
 * writes the picture's position from where the player IS then; the player moves again during the next update, so the
 * picture is drawn one update behind (up to ~8 units at full speed). The mod has no lag (it sets the scythe from the
 * player's position inside the update). The fix adds the player's last step (position now minus position one update
 * ago) so the picture lands where the player will be. 1.0 = one whole update ahead (what a one-update lag needs),
 * 0.5 = half, 0.0 = off (fix6). FX_LEAD_MAX drops the step when the player jumped (a room change). */
#define FX_LEAD             1.0f
#define FX_LEAD_MAX         14.0f
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

/* FIX10 - TEST OF THE WIDENED TABLE: one pedestal with collectible 733 per run, only if the game already has a configuration
 * for that id (items.xml patched by samael_items_datos.py). Measured: the configuration container is [base+0xac2428] + 0x55f30
 * (three vectors, the first one holds the collectibles); 0x73f7f4(container, id) returns the ItemConfig* or 0;
 * 0x4eeb74(player, id, flag) is HasCollectible. */
#define SAMAEL_ITEM_TEST     1
#define SAMAEL_TEST_ID       733
#define ITEM_CONTAINER_PTR   0xac2428
#define ITEM_CONTAINER_OFF   0x55f30
#define FN_GET_CONFIG        0x73f7f4
#define FN_HAS_COLLECTIBLE   0x4eeb74
#define FN_GET_TRINKET       0x73f83c       /* (container, id) -> the trinket's ItemConfig*, from the vector at container+0x18 */
/* FIX11: the mod's items, as samael_items_datos.py writes them: collectibles 733..748, trinkets 190..201 */
#define SAMAEL_FIRST_ITEM    733
#define SAMAEL_N_ITEMS       16
#define SAMAEL_FIRST_TRINKET 190
#define SAMAEL_N_TRINKETS    12
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
/* FIX8: how many updates the picture lives. The anm2 "Swing" has 11 frames but its SwingEnd event is at frame 5, where the
 * hitbox ends (6 frames). Frames 3-10 of the mod's own sprite sit BEHIND the player (aiming up they end on the lower
 * right, below the feet; aiming down, on the upper left): that is the follow-through before the mod starts its next
 * swing. 6 = the picture lives as long as the hitbox (frames 0-5). 11 = the whole animation (fix7). 3 = only the arc
 * in front of the player (frames 0-2). */
#define FX_FRAMES           6
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
    float ppx, ppy;                     /* FIX7: the player's position at the previous update */
    float vx, vy;                       /* FIX7: the player's last step (one update) */
    int pvalid;
    int test_done, test_spawned, test_has;   /* FIX10 */
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

/* CalcScythePosOffset: the picture's offset from the player */
static void sprite_offset(float dx, float dy, float *ox, float *oy) {
    float a = atan2f(-dx, dy);
    *ox = -SPRITE_OFFSET_X * sinf(a);
    *oy = SPRITE_OFFSET_Y * cosf(a) + SPRITE_OFFSET_BASE;
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
    float sox, soy;
    sprite_offset(s->dir_x, s->dir_y, &sox, &soy);
    const float *ppos = (const float *) (player + PLAYER_POSITION);
    float at[2] = { ppos[0] + sox + FX_LEAD * s->vx, ppos[1] + soy + FX_LEAD * s->vy };                 /* FIX6: player + CalcScythePosOffset */
    unsigned long e = spawn(game, FX_TYPE, FX_VARIANT, at,
                            (const float *) (b + ZERO_VECTOR), 0, sub, seed, 0);
    if (e && in_room(game, e)) {
        s->fx = e;
        s->fx_room = *(const unsigned long *) (game + GAME_ROOM);
        s->fx_left = FX_FRAMES;
        LOG("scythe: picture spawned (subtype %d)", sub);
#if ISAAC_DIAG
        /* FIX5 diagnostic: where the picture is against the player and the hitbox, at the moment it is spawned */
        static int fxsaid;
        if (fxsaid < 16 && shim_report_active() && shim_trace_readable((const void *) (e + ENT_POSITION), 8)) {
            fxsaid++;
            const float *fp = (const float *) (e + ENT_POSITION);
            const float *pp = (const float *) (player + PLAYER_POSITION);
            float ox, oy;
            hitbox_offset(s->dir_x, s->dir_y, &ox, &oy);
            char line[240];
            snprintf(line, sizeof(line), "fx7 spawn: sub %d dir (%.2f, %.2f) player (%.1f, %.1f) fx (%.1f, %.1f) hitbox centre (%.1f, %.1f)",
                     sub, (double) s->dir_x, (double) s->dir_y, (double) pp[0], (double) pp[1], (double) fp[0], (double) fp[1],
                     (double) (pp[0] + ox), (double) (pp[1] + oy));
            report_line("scythe", line);
        }
#endif
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
#if ISAAC_DIAG
        /* FIX5 diagnostic: does the game move the picture by itself between our writes? (value BEFORE we overwrite it) */
        static int ticksaid;
        if (ticksaid < 30 && s->fx_left >= FX_FRAMES - 2 && shim_report_active()) {
            ticksaid++;
            char line[200];
            snprintf(line, sizeof(line), "fx7 tick: left %d fx before (%.1f, %.1f) player now (%.1f, %.1f) step (%.1f, %.1f)", s->fx_left,
                     (double) fp[0], (double) fp[1], (double) pp[0], (double) pp[1], (double) s->vx, (double) s->vy);
            report_line("scythe", line);
        }
#endif
        float fox, foy;
        sprite_offset(s->dir_x, s->dir_y, &fox, &foy);
        fp[0] = pp[0] + fox + FX_LEAD * s->vx; fp[1] = pp[1] + foy + FX_LEAD * s->vy;                   /* FIX6: player + CalcScythePosOffset */
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
#if SAMAEL_ITEM_TEST
/*
 * FIX11 - SHOWCASE: at frame 90 of every run with Samael, a row of pedestals with the 16 new collectibles and a row of the 12
 * new trinkets in the starting room, so names, pictures and descriptions can be checked at a glance. A thing is only spawned
 * if the game already has its configuration (that is asked first, so a missing one is reported instead of crashing).
 * Switch it off (SAMAEL_ITEM_TEST 0) once the objects are checked.
 */
static void item_test(unsigned long b, unsigned long game, unsigned long p, slot_t *s, int frame) {
    if (!s->test_done && frame >= 90) {
        s->test_done = 1;                                   /* one try per run, whatever happens */
        unsigned long root = *(const unsigned long *) (b + ITEM_CONTAINER_PTR);
        if (!root) { report_now("showcase: the game has no configuration container yet"); return; }
        unsigned long cont = root + ITEM_CONTAINER_OFF;
        if (!shim_trace_readable((const void *) cont, 0x40)) { report_now("showcase: container %#lx is not readable", cont); return; }
        long size_c = (long) ((*(const unsigned long *) (cont + 8) - *(const unsigned long *) cont) / 8);
        long size_t_ = (long) ((*(const unsigned long *) (cont + 0x20) - *(const unsigned long *) (cont + 0x18)) / 8);
        report_now("showcase: collectible vector holds %ld entries, trinket vector %ld", size_c, size_t_);
        unsigned long (*get_c)(unsigned long, int) = (unsigned long (*)(unsigned long, int)) (b + FN_GET_CONFIG);
        unsigned long (*get_t)(unsigned long, int) = (unsigned long (*)(unsigned long, int)) (b + FN_GET_TRINKET);
        unsigned long (*spawn)(unsigned long, int, int, const float *, const float *, unsigned long, int, int, int) =
            (unsigned long (*)(unsigned long, int, int, const float *, const float *, unsigned long, int, int, int)) (b + FN_SPAWN);
        int seed = ((int (*)(void)) (b + FN_RANDOM))();
        int ok_c = 0, ok_t = 0, miss = 0, k;
        for (k = 0; k < SAMAEL_N_ITEMS; k++) {
            int id = SAMAEL_FIRST_ITEM + k;
            if (!get_c(cont, id)) { miss++; report_now("showcase: no configuration for collectible %d", id); continue; }
            float at[2] = { 140.0f + 40.0f * (float) (k % 8), 190.0f + 40.0f * (float) (k / 8) };
            if (spawn(game, 5, 100, at, (const float *) (b + ZERO_VECTOR), 0, id, seed + k, 0)) ok_c++;
        }
        for (k = 0; k < SAMAEL_N_TRINKETS; k++) {
            int id = SAMAEL_FIRST_TRINKET + k;
            if (!get_t(cont, id)) { miss++; report_now("showcase: no configuration for trinket %d", id); continue; }
            float at[2] = { 160.0f + 40.0f * (float) (k % 6), 290.0f + 40.0f * (float) (k / 6) };
            if (spawn(game, 5, 350, at, (const float *) (b + ZERO_VECTOR), 0, id, seed + 100 + k, 0)) ok_t++;
        }
        s->test_spawned = ok_c + ok_t;
        report_now("showcase: %d collectibles and %d trinkets spawned, %d without configuration", ok_c, ok_t, miss);
    }
    if (s->test_spawned && frame % 15 == 0) {                /* which new collectibles the player now owns */
        unsigned mask = 0;
        for (int k = 0; k < SAMAEL_N_ITEMS; k++)
            if (((int (*)(unsigned long, int, int)) (b + FN_HAS_COLLECTIBLE))(p, SAMAEL_FIRST_ITEM + k, 1)) mask |= 1u << k;
        if (mask != (unsigned) s->test_has) { s->test_has = (int) mask; report_now("showcase: owned collectibles mask %#x", mask); }
    }
}
#endif

void shim_samael_between_frames(void) {
#if ISAAC_DIAG
    if (g_note_n && shim_report_active()) {                 /* FIX10: notes made before the report existed */
        for (int k = 0; k < g_note_n; k++) report_line("samael", g_note[k]);
        g_note_n = 0;
    }
#endif
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

        {   /* FIX7: the player's last step */
            const float *pp0 = (const float *) (p + PLAYER_POSITION);
            float dx = 0.0f, dy = 0.0f;
            if (s->pvalid) {
                dx = pp0[0] - s->ppx; dy = pp0[1] - s->ppy;
                if (fabsf(dx) > FX_LEAD_MAX || fabsf(dy) > FX_LEAD_MAX) { dx = 0.0f; dy = 0.0f; }
            }
            s->vx = dx; s->vy = dy;
            s->ppx = pp0[0]; s->ppy = pp0[1]; s->pvalid = 1;
        }

#if SAMAEL_ITEM_TEST
        item_test(b, game, p, s, frame);
#endif
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
#if SWING_CARDINAL
            /* FIX6: the mod's DirectionalVector(GetDirectionFromVector(dir)): the dominant axis wins (a tie goes to
             * the horizontal one, like the picture's subtype below), so hitbox, offset and picture always agree */
            if (fabsf(s->dir_x) >= fabsf(s->dir_y)) { s->dir_y = 0.0f; s->dir_x = s->dir_x > 0.0f ? 1.0f : -1.0f; }
            else                                    { s->dir_x = 0.0f; s->dir_y = s->dir_y > 0.0f ? 1.0f : -1.0f; }
#endif
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
/*
 * FIX9 - ROOM FOR NEW COLLECTIBLES AND TRINKETS (an experiment: tables only, no new item yet).
 *
 * Measured in isaac.arm64 (all offsets from the image base). The game keeps its item configurations in ONE object made of
 * three std::vector<ItemConfig*>: collectibles (+0x0), trinkets (+0x18) and "others" (+0x30). Three places size them with
 * `resize(N)` written as an immediate, and the constructor also zeroes them with a loop bounded by the same number:
 *   0x737eac mov w1,#733   0x737ec4 cmp x8,#733  (loop: 733 pointers)   <- constructor
 *   0x737ed4 mov w1,#190   0x737eec cmp x8,#0x5f0 (loop: 190 * 8 bytes)
 *   0x7384a8 mov w1,#733   0x7384b4 mov w1,#190                          <- a second reset
 *   0x738680 mov w1,#733   0x73868c mov w1,#190                          <- the items.xml loader
 * The lookup by id (0x73f7f4 and the two next to it) compares the id with (end-begin)/8, the REAL size, and the player's
 * per-item counter (a vector<int> at player+0x1ab8, filled at 0x4eba7c) is resized from that same size, so both grow on
 * their own. The 733 compared at 0x7ea590, 0x7eb068, 0x7ed3dc, 0x8a26c8, 0x7025b4, 0x66b3b8, 0x66b6a4 and 0x66b9a4 are
 * left alone ON PURPOSE: they probably guard fixed-size arrays (the saved data), and widening them could write outside.
 *
 * All the words are checked first; if ANY differs, nothing is written (a half-patched table would be worse).
 */
#define SAMAEL_ITEMS_WIDEN      1       /* 0 = leave the item tables alone */
#define SAMAEL_NEW_COLLECTIBLES 16      /* ids 733..748 */
#define SAMAEL_NEW_TRINKETS     12      /* ids 190..201 */

static uint32_t a64_mov_w1(unsigned n)   { return 0x52800001u | (n << 5); }          /* mov w1,#n   (n < 65536) */
static uint32_t a64_cmp_x8(unsigned n)   { return 0xF100011Fu | (n << 10); }         /* cmp x8,#n   (n < 4096)  */

static void samael_items_widen(unsigned char *base) {
#if SAMAEL_ITEMS_WIDEN
    const unsigned oc = 733, ot = 190;
    const unsigned nc = oc + SAMAEL_NEW_COLLECTIBLES, nt = ot + SAMAEL_NEW_TRINKETS;
    struct { unsigned off; uint32_t old_w, new_w; } e[] = {
        { 0x737eac, a64_mov_w1(oc),     a64_mov_w1(nc)     },
        { 0x7384a8, a64_mov_w1(oc),     a64_mov_w1(nc)     },
        { 0x738680, a64_mov_w1(oc),     a64_mov_w1(nc)     },
        { 0x737ec4, a64_cmp_x8(oc),     a64_cmp_x8(nc)     },
        { 0x737ed4, a64_mov_w1(ot),     a64_mov_w1(nt)     },
        { 0x7384b4, a64_mov_w1(ot),     a64_mov_w1(nt)     },
        { 0x73868c, a64_mov_w1(ot),     a64_mov_w1(nt)     },
        { 0x737eec, a64_cmp_x8(ot * 8), a64_cmp_x8(nt * 8) },
    };
    const int n = (int) (sizeof e / sizeof e[0]);
    int already = 0, i;
    for (i = 0; i < n; i++) {
        uint32_t cur = *(uint32_t *) (base + e[i].off);
        if (cur == e[i].new_w) { already++; continue; }
        if (cur != e[i].old_w) {
            LOG("samael: item tables NOT widened - %#x holds %08x, expected %08x", e[i].off, (unsigned) cur, (unsigned) e[i].old_w);
            note("item tables NOT widened - %#x holds %08x, expected %08x", e[i].off, (unsigned) cur, (unsigned) e[i].old_w);
            return;
        }
    }
    if (already == n) { LOG("samael: item tables were already widened"); note("item tables were already widened"); return; }
    for (i = 0; i < n; i++) *(uint32_t *) (base + e[i].off) = e[i].new_w;
    LOG("samael: item tables widened: collectibles %u -> %u, trinkets %u -> %u (8 words)", oc, nc, ot, nt);
    note("item tables widened: collectibles %u -> %u, trinkets %u -> %u (8 words)", oc, nc, ot, nt);
#else
    (void) base;
#endif
}

void shim_samael_slots_install(unsigned char *base) {
    samael_items_widen(base);
    uint32_t *w = (uint32_t *) (base + 0x7855dc);
    if (*w != 0x52800521u && *w != 0x52800541u && *w != 0x528007A1u) {
        LOG("samael: 0x7855dc holds %08x - the character table is left alone", (unsigned) *w);
        return;
    }
    *w = 0x52800541u;
    LOG("samael: character table resized to 42 (Samael is id 41)");
}
