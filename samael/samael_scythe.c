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
 *     +0x58): no death animation, no drops, no flash. SCYTHE_TAKE_DAMAGE below is where the real call goes.
 *   - the enemy's size and position as ENTITIES (only the player's +0x310 is measured): the report build
 *     logs the first enemies it sees so the guess can be checked (see ISAAC_DIAG lines).
 *   - the scythe's picture, its projectile (full charge), the multi-scythe, Mom's Knife, Ludovico and the
 *     other synergies. Those are other pieces of the mod, not this one.
 */
#include <math.h>
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

/* the real call, when it is measured: (entity, damage, flags, source) - leave 0 until then */
#define SCYTHE_TAKE_DAMAGE  0

#define SLOTS               4
#define HIT_MAX             64

typedef struct {
    int cooldown;                       /* frames until the next swing */
    int swing_left;                     /* frames the hitbox still lives */
    float dir_x, dir_y;                 /* the direction the swing was started in */
    unsigned long hit[HIT_MAX];         /* enemies already hit in this swing */
    int hit_n;
    int last_frame;
    int said;
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
#if SCYTHE_TAKE_DAMAGE
    ((void (*)(unsigned long, float, unsigned long long, unsigned long)) (b + SCYTHE_TAKE_DAMAGE))(e, damage, 0, player);
#else
    float *hp = (float *) (e + NPC_HP);
    if (!shim_trace_readable(hp, 4) || !(*hp == *hp)) return;
    *hp -= damage;
    if (*hp <= 0.0f) {
        const unsigned long *vt = *(const unsigned long *const *) e;
        ((void (*)(unsigned long)) vt[VT_REMOVE / 8])(e);
    }
#endif
}

static void hit_pass(unsigned long b, unsigned long game, unsigned long player, slot_t *s, int index) {
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!room || !shim_trace_readable((const void *) (room + ROOM_ENTITIES), LIST_COUNT + 4)) return;
    unsigned long list = room + ROOM_ENTITIES;
    const unsigned long *items = *(const unsigned long *const *) (list + LIST_ARRAY);
    unsigned n = *(const unsigned *) (list + LIST_COUNT);
    if (!items || n > 4096 || !shim_trace_readable(items, (unsigned long) n * 8)) return;

    const float *pp = (const float *) (player + PLAYER_POSITION);
    float ox, oy;
    hitbox_offset(s->dir_x, s->dir_y, &ox, &oy);
    float cx = pp[0] + ox, cy = pp[1] + oy;
    float reach = HITBOX_SIZE + ENEMY_PAD;
    float damage = *(const float *) (player + PLAYER_DAMAGE) * MELEE_DAMAGE_MULT;

    for (unsigned i = 0; i < n; i++) {
        unsigned long e = items[i];
        if (!e || already_hit(s, e) || !is_enemy(e)) continue;
        const float *ep = (const float *) (e + ENT_POSITION);
        float dx = ep[0] - cx, dy = ep[1] - cy;
        if (!(dx == dx) || !(dy == dy)) continue;
#if ISAAC_DIAG
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
        hurt(b, e, player, damage);
    }
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

        if (s->cooldown > 0) s->cooldown--;
        if (s->swing_left > 0) {
            hit_pass(b, game, p, s, i);
            if (--s->swing_left == 0) s->hit_n = 0;
        }

        float ax = (float) action_held(b, p, ACTION_SHOOT_RIGHT) - (float) action_held(b, p, ACTION_SHOOT_LEFT);
        float ay = (float) action_held(b, p, ACTION_SHOOT_DOWN) - (float) action_held(b, p, ACTION_SHOOT_UP);
        int aiming = ax != 0.0f || ay != 0.0f;
        if (aiming && s->cooldown == 0 && s->swing_left == 0) {
            float len = sqrtf(ax * ax + ay * ay);
            s->dir_x = ax / len;
            s->dir_y = ay / len;
            s->swing_left = HITBOX_FRAMES;
            s->hit_n = 0;
            s->cooldown = swing_delay(p);
            hit_pass(b, game, p, s, i);                 /* the swing's first frame hits at once */
            s->swing_left--;
            LOG("scythe: swing of player %d, dir (%.2f, %.2f), delay %d", i, (double) s->dir_x, (double) s->dir_y, s->cooldown);
        }
    }
}
