/*
 * THE SPAWNER DESK (07.10.2026): the real spawner.c in this unit (spawner_stats.c
 * in its own, stats_tu.c), the game's functions answered at their own addresses
 * inside a fake image: a jump to a stub here. The stubs are a MODEL of the game
 * - they show what the port asks of the game (which function, which numbers),
 * not what the game does with it; what the game does is the game's.
 *
 * Text is measured with the real advances of the description faces
 * (tables.inc), so the layout checks are the card's own.
 */
#include <windows.h>
#include "src_spawner.c"
#include "tables.inc"

int desk_clock_gettime(int clock, struct timespec *ts) {
    (void) clock;
    static long long fake_ms;
    fake_ms += 0;
    timespec_get(ts, TIME_UTC);
    return 0;
}

/* ------------------------------------------------------------------ checks */
static int g_fail, g_pass;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------------ the fake image */
static unsigned char *g_img;
#define IMG_SIZE 0xB00000

/* a 5-byte jmp at the game's address (some are 8 apart: AddCard, AddPill) to a slot far away that jumps on */
static int g_slots;
static void thunk(unsigned long long off, void *fn) {
    unsigned char *slot = g_img + 0xA80000 + (unsigned long long) g_slots++ * 16;
    slot[0] = 0x48; slot[1] = 0xB8;                /* mov rax, imm64 */
    memcpy(slot + 2, &fn, 8);
    slot[10] = 0xFF; slot[11] = 0xE0;              /* jmp rax */
    unsigned char *p = g_img + off;
    int rel = (int) (slot - (p + 5));
    p[0] = 0xE9;                                   /* jmp rel32 */
    memcpy(p + 1, &rel, 4);
}

unsigned long long shim_image_base(void) { return (unsigned long long) g_img; }

/* game memory */
static unsigned char *g_game, *g_room, *g_manager, *g_node, *g_dialog_mem;
#define PLAYERS 4
static unsigned char *g_player[PLAYERS];
static int g_players_n = 1;

/* ------------------------------------------------------------------ the log of what was asked */
static char g_calls[64][128];
static int g_ncalls;
static void call(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    if (getenv("DESK_TRACE")) { va_list b; va_copy(b, a); printf("      "); vprintf(fmt, b); printf("\n"); va_end(b); }
    if (g_ncalls < 64) vsnprintf(g_calls[g_ncalls++], 128, fmt, a);
    va_end(a);
}
static void calls_clear(void) { g_ncalls = 0; }
static int calls_count(const char *prefix) {
    int n = 0;
    for (int i = 0; i < g_ncalls; i++) if (!strncmp(g_calls[i], prefix, strlen(prefix))) n++;
    return n;
}
static const char *calls_find(const char *prefix) {
    for (int i = 0; i < g_ncalls; i++) if (!strncmp(g_calls[i], prefix, strlen(prefix))) return g_calls[i];
    return NULL;
}

/* ------------------------------------------------------------------ stubs at the game's addresses */
typedef struct { float x, y; } v2;
static unsigned char g_desk_spawned[64][0x600];
static int g_desk_spawned_n;

static unsigned long long st_player_at(unsigned long long mgr, int i) { (void) mgr; return i < g_players_n ? (unsigned long long) g_player[i] : 0; }
static v2 st_free_spot(unsigned long long room, const float *pos, float step, int a, int b) {
    (void) room; (void) a; (void) b;
    call("FindFree step %.0f", step);
    v2 r = { pos[0] + step, pos[1] };
    return r;
}
static int st_random(void) { static unsigned s = 12345; s = s * 1103515245u + 12345u; return (int) (s >> 1); }
static unsigned long long st_spawn(unsigned long long game, int type, int variant, const float *pos, const float *vel,
                                   unsigned long long spawner, int subtype, int seed, int z) {
    (void) game; (void) pos; (void) vel; (void) seed; (void) z;
    call("Spawn %d.%d.%d spawner %s", type, variant, subtype, spawner ? "player" : "none");
    if (g_desk_spawned_n >= 64) return 0;
    unsigned char *e = g_desk_spawned[g_desk_spawned_n++];
    memset(e, 0, 0x600);
    *(int *) (e + 0x38) = type; *(int *) (e + 0x3c) = variant; *(int *) (e + 0x40) = subtype;
    return (unsigned long long) e;
}
static int g_collide_takes = 1;
static int st_collide(unsigned long long pickup, unsigned long long other, int low) {
    call("Collide %d.%d.%d low %d", *(int *) (pickup + 0x38), *(int *) (pickup + 0x3c), *(int *) (pickup + 0x40), low);
    if (g_collide_takes && other) *(unsigned char *) (pickup + 0x1c3) = 1;
    return 1;
}
static int st_force_pill(unsigned long long pool, int effect) {
    call("ForcePill pool %s effect %d", pool == (unsigned long long) g_game + 0x242c0 ? "ok" : "BAD", effect);
    return 1 + effect % 13;
}
static void st_add_pill(unsigned long long p, int colour) { (void) p; call("AddPill %d", colour); }
static void st_add_card(unsigned long long p, int card) { (void) p; call("AddCard %d", card); }
static void st_add_collectible(unsigned long long p, int id, int ch, int first, int slot, int var) {
    (void) p; call("AddCollectible %d charge %d first %d slot %d var %d", id, ch, first, slot, var);
}
static void st_add_trinket(unsigned long long p, int id, int first) {
    call("AddTrinket %d first %d", id, first);
    int *t = (int *) (p + 0x1ab0);
    if (!t[0]) t[0] = id; else if (!t[1]) t[1] = id;
}
static int st_trinket_slots(unsigned long long p) { (void) p; return 1; }
static void st_set_read_only(unsigned long long d, int on) { (void) d; call("SetReadOnly %d", on); *((unsigned char *) d + 1) = 1; }
static void st_play_sound(unsigned long long m, int id, int a, int b, float v, float p) { (void) m; (void) a; (void) b; (void) v; (void) p; call("Sound %d", id); }
static void st_dialog_fill(void *d, unsigned long long f, void *cb, float *size) { (void) d; (void) f; (void) cb; (void) size; }
static int st_dialog_add(unsigned long long m, void *d) { (void) m; (void) d; return 5; }
static void st_dialog_free(void *d) { (void) d; }
static void st_dialog_close(unsigned long long m, int *id) { (void) m; (void) id; call("DialogClose"); }
static int g_pressed[64], g_held[64];
static int st_menu_action(unsigned long long in, int action, int dev, void *x) { (void) in; (void) dev; (void) x; return action < 64 ? g_pressed[action] : 0; }
static int st_menu_held(unsigned long long in, int action, int dev, void *x) { (void) in; (void) dev; (void) x; return action < 64 ? g_held[action] : 0; }
static int st_has_item(unsigned long long p, int id, int ignore) { (void) p; (void) id; (void) ignore; return 0; }

/* the stats file's game calls */
/* the model of EvaluateItems for damage alone: 3.5 times D8's multiplier, at least 0.5 */
static void st_evaluate(unsigned long long p) {
    float *m = (float *) (p + 0x1db4), *dmg = (float *) (p + 0x1844);
    if (*m == 0.0f) *m = 1.0f;
    *dmg = 3.5f * *m;
    if (*dmg < 0.5f) *dmg = 0.5f;
    *(unsigned *) (p + 0x1958) = 0;
}

/* the health model: what the port asks; the model only clamps a little, so a call is seen to happen */
#define F(p, o) (*(int *) ((p) + (o)))
static int ht_of(int type) {                 /* the game's table, hearts.md (verified by the image, see stats test) */
    switch (type) {
    case 4: case 12: case 17: case 24: case 25: case 35: case 36: return 1;
    case 10: case 31: case 40: return 2;
    case 14: case 33: return 3;
    case 16: return 4;
    default: return 0;
    }
}
static int st_health_type(unsigned long long p) { return ht_of(F(p, 0x1738)); }
static unsigned long long soul_of(unsigned long long p) { return *(unsigned long long *) (p + 0x2460); }
static void st_add_max(unsigned long long p, int n, int ik) { call("AddMaxHearts %d %d", n, ik); F(p, 0x16b8) += n; if (F(p, 0x16b8) < 0) F(p, 0x16b8) = 0; if (F(p, 0x16b8) > 24) F(p, 0x16b8) = 24; }
static void st_add_red(unsigned long long p, int n, int flag) { call("AddHearts %d %d", n, flag); if (F(p, 0x1738) == 17) p = soul_of(p); F(p, 0x16bc) += n; if (F(p, 0x16bc) < 0) F(p, 0x16bc) = 0; }
static void st_add_soul(unsigned long long p, int n) { call("AddSoulHearts %d", n); if (F(p, 0x1738) == 16) p = soul_of(p); F(p, 0x16c4) += n; if (F(p, 0x16c4) < 0) F(p, 0x16c4) = 0; }
static void st_add_black(unsigned long long p, int n) {
    call("AddBlackHearts %d", n);
    if (F(p, 0x1738) == 16) p = soul_of(p);
    int at = (F(p, 0x16c4) + 1) / 2;
    for (int i = 0; i < (n + 1) / 2; i++) *(unsigned *) (p + 0x16c8) |= 1u << (at + i);
    F(p, 0x16c4) += n;
}
static void st_add_bone(unsigned long long p, int n) { call("AddBoneHearts %d", n); if (F(p, 0x1738) == 17) p = soul_of(p); F(p, 0x2450) += n; }
static void st_add_broken(unsigned long long p, int n) { call("AddBrokenHearts %d", n); F(p, 0x2470) += n; }
static void st_add_eternal(unsigned long long p, int n) { call("AddEternalHearts %d", n); F(p, 0x16c0) += n; if (F(p, 0x16c0) > 1) F(p, 0x16c0) = 1; if (F(p, 0x16c0) < 0) F(p, 0x16c0) = 0; }
static void st_add_golden(unsigned long long p, int n) { call("AddGoldenHearts %d", n); F(p, 0x1dc4) += n; }
static void st_add_rotten(unsigned long long p, int n, int flag) { call("AddRottenHearts %d %d", n, flag); F(p, 0x2474) += n / 2; F(p, 0x16bc) += n; }
static void st_add_soulcharge(unsigned long long p, int n) { call("AddSoulCharge %d", n); F(p, 0x2478) += n; }
static void st_add_blood(unsigned long long p, int n) { call("AddBloodCharge %d", n); F(p, 0x247c) += n; }
static void st_add_coins(unsigned long long p, int n) { call("AddCoins %d", n); F(p, 0x16e0) += n; if (F(p, 0x16e0) > 99) F(p, 0x16e0) = 99; if (F(p, 0x16e0) < 0) F(p, 0x16e0) = 0; }
static void st_add_bombs(unsigned long long p, int n) { call("AddBombs %d", n); F(p, 0x16dc) += n; if (F(p, 0x16dc) > 99) F(p, 0x16dc) = 99; }
static void st_add_keys(unsigned long long p, int n) { call("AddKeys %d", n); F(p, 0x16d4) += n; if (F(p, 0x16d4) > 99) F(p, 0x16d4) = 99; }
static void st_full_charge(unsigned long long p, int slot, int force) { (void) p; call("FullCharge slot %d force %d", slot, force); }

static void image_up(void) {
    g_img = VirtualAlloc(NULL, IMG_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    g_game = calloc(1, 0x300000);
    g_room = calloc(1, 0x10000);
    g_manager = calloc(1, 0x210000);
    g_node = calloc(1, 0x100);
    g_dialog_mem = calloc(1, 0x200);
    for (int i = 0; i < PLAYERS; i++) g_player[i] = calloc(1, 0x3000);
    *(unsigned long long *) (g_img + 0xac3b90) = (unsigned long long) g_game;         /* GAME_PTR */
    *(unsigned long long *) (g_img + 0xac2428) = (unsigned long long) g_manager;      /* MANAGER_PTR */
    *(unsigned long long *) (g_game + 0x21550) = (unsigned long long) g_room;         /* GAME_ROOM */
    float *canvas = (float *) (g_img + 0xa9aff0);
    canvas[0] = 480.0f; canvas[1] = 270.0f;
    unsigned *area = (unsigned *) (g_img + 0xabec64);
    area[0] = 1920; area[1] = 1080; area[2] = 0; area[3] = 0;
    /* the dialog list: the manager is its head, one node, the dialog */
    *(unsigned long long *) (g_manager + 8) = (unsigned long long) g_node;
    *(unsigned long long *) (g_node + 8) = (unsigned long long) g_manager;
    *(unsigned long long *) (g_node + 0x10) = (unsigned long long) g_dialog_mem;
    *(int *) g_dialog_mem = 5;
    *(int *) (g_dialog_mem + 4) = 1;                                                  /* open */
    float *size = (float *) (g_dialog_mem + 0x40);
    size[0] = 420.0f; size[1] = 236.0f;
    /* the run key: slot, seed */
    *(int *) (g_manager + 0x204c8) = 1;
    *(unsigned *) (g_game + 0x25d44) = 777;
    for (int i = 0; i < PLAYERS; i++) {
        float *pos = (float *) (g_player[i] + 0x310);
        pos[0] = 320.0f; pos[1] = 280.0f;
        *(int *) (g_player[i] + 0x1738) = 0;
    }
    thunk(0x4da574, (void *) st_player_at);
    thunk(0x59de50, (void *) st_free_spot);
    thunk(0x7f1cc0, (void *) st_random);
    thunk(0x88fcbc, (void *) st_spawn);
    thunk(0x71e310, (void *) st_collide);
    thunk(0x87b440, (void *) st_force_pill);
    thunk(0x515178, (void *) st_add_pill);
    thunk(0x515170, (void *) st_add_card);
    thunk(0x500588, (void *) st_add_collectible);
    thunk(0x5039f8, (void *) st_add_trinket);
    thunk(0x508b9c, (void *) st_trinket_slots);
    thunk(0x7027a0, (void *) st_set_read_only);
    thunk(0x671c8c, (void *) st_play_sound);
    thunk(0x7628d4, (void *) st_dialog_fill);
    thunk(0x6644b4, (void *) st_dialog_add);
    thunk(0x67456c, (void *) st_dialog_free);
    thunk(0x66457c, (void *) st_dialog_close);
    thunk(0x8b6600, (void *) st_menu_action);
    thunk(0x8b64f8, (void *) st_menu_held);
    thunk(0x4eeb74, (void *) st_has_item);
    thunk(0x4f0ee4, (void *) st_evaluate);
    thunk(0x4eeae4, (void *) st_health_type);
    thunk(0x4ee8a0, (void *) st_add_max);
    thunk(0x4f0338, (void *) st_add_red);
    thunk(0x4ef2d0, (void *) st_add_soul);
    thunk(0x4ef078, (void *) st_add_black);
    thunk(0x4ef5c4, (void *) st_add_bone);
    thunk(0x5059ac, (void *) st_add_broken);
    thunk(0x4f0590, (void *) st_add_eternal);
    thunk(0x552ab8, (void *) st_add_golden);
    thunk(0x554794, (void *) st_add_rotten);
    thunk(0x4f08d0, (void *) st_add_soulcharge);
    thunk(0x4f04c4, (void *) st_add_blood);
    thunk(0x4f0a14, (void *) st_add_coins);
    thunk(0x4f0ca0, (void *) st_add_bombs);
    thunk(0x4f0d20, (void *) st_add_keys);
    thunk(0x504f64, (void *) st_full_charge);
}

/* ------------------------------------------------------------------ the port's other files, as stubs */
static int g_set;                                  /* the language set shown */
static FILE *g_report;

void report_line(const char *kind, const char *text) { if (g_report) fprintf(g_report, "%s: %s\n", kind, text); }
int shim_report_active(void) { return 1; }
int shim_tweaks_set_index(void) { return g_set; }
const char *shim_tweaks_set_suffix(void) { static const char *S[7] = { "en", "ru", "de", "es", "jp", "kr", "zh" }; return S[g_set]; }
int shim_tweaks_spawner(void) { return 1; }
int shim_sticks_in_game(void) { return 1; }
int shim_straps_pause_paper(void) { return 0; }
static int g_saves;
void shim_sticks_settings_save(void) { g_saves++; }
static int g_ime;
void shim_ime_show(int show) { g_ime = show; }
float shim_screen_width(void) { return 480.0f; }
float shim_screen_height(void) { return 270.0f; }
const char *shim_saves_dir(void) { return getenv("TEMP"); }
FILE *shim_safe_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int shim_safe_fclose(FILE *f) { return fclose(f); }
int shim_patch_word(unsigned long long off, unsigned a, unsigned b, unsigned now) { (void) off; (void) a; (void) b; (void) now; return 1; }
int shim_trace_add(unsigned long long off, const char *name) { (void) off; (void) name; return 0; }
int shim_trace_readable(const void *p, unsigned long long n) { (void) p; (void) n; return 1; }
const char *shim_eid_language_now(void) { return "en_us"; }
unsigned long long shim_eid_list_now(void) { return 0; }
int shim_eid_players(unsigned long long *out, int max) { int n = 0; for (int i = 0; i < g_players_n && i < max; i++) out[n++] = (unsigned long long) g_player[i]; return n; }
int shim_teleport_room_state(int type, int subtype) { (void) type; (void) subtype; return 1; }
int shim_teleport_floor_here(void) { return 0; }
void shim_teleport_ask_room(int t, int s) { (void) t; (void) s; }
void shim_teleport_ask_floor(int s, int t) { (void) s; (void) t; }
void shim_teleport_between_frames(void) { }
void shim_eid_marker_faded(const char *name, float x, float y, float scale, float alpha) { (void) name; (void) x; (void) y; (void) scale; (void) alpha; }
int shim_eid_item_picture(unsigned char *s, signed char *built, int id, int trinket) { (void) id; (void) trinket; *built = 1; s[0] = 1; return 1; }
void shim_eid_item_picture_draw(unsigned char *s, float x, float y, float alpha) { (void) s; (void) x; (void) y; (void) alpha; }
int shim_picture_ready(unsigned char *sprite, signed char *state, const char *anm2) { (void) sprite; (void) state; (void) anm2; return 1; }

/* pictures drawn: kept to check what went where */
typedef struct { char anim[16]; int frame; float x, y, a; } pic_t;
static pic_t g_pics[256];
static int g_npics;
void shim_picture_draw(unsigned char *sprite, const char *anim, int frame, float x, float y, float alpha) {
    (void) sprite;
    if (g_npics < 256) { pic_t *p = &g_pics[g_npics++]; snprintf(p->anim, 16, "%s", anim); p->frame = frame; p->x = x; p->y = y; p->a = alpha; }
}

/* the description database, from eid.bin */
static int g_eid_lang;                              /* the database's language: the set's own */
static const char *eid_name(unsigned k, int i) {
    const char *n = DESK_EID[i].name[g_eid_lang];
    return n ? n : DESK_EID[i].name[0];
}
int shim_eid_each_name(unsigned kind, void (*fn)(unsigned id, const char *name, const char *english, void *ctx), void *ctx) {
    int n = 0;
    for (unsigned i = 0; i < sizeof(DESK_EID) / sizeof(DESK_EID[0]); i++)
        if (DESK_EID[i].kind == kind) { fn(DESK_EID[i].id, DESK_EID[i].name[g_eid_lang], DESK_EID[i].name[0], ctx); n++; }
    return n;
}
const char *shim_eid_name_of(unsigned kind, unsigned id) {
    for (unsigned i = 0; i < sizeof(DESK_EID) / sizeof(DESK_EID[0]); i++)
        if (DESK_EID[i].kind == kind && DESK_EID[i].id == id) return eid_name(kind, (int) i);
    return NULL;
}

/* the font: the real advances, the line height of eid_default (11) */
static int adv_of(unsigned cp) {
    int face = DESK_FACE_OF_SET[g_set];
    const struct { unsigned cp; short adv; } *t;
    size_t n;
    if (face == 0) { t = (const void *) DESK_ADV0; n = sizeof(DESK_ADV0) / sizeof(DESK_ADV0[0]); }
    else if (face == 1) { t = (const void *) DESK_ADV1; n = sizeof(DESK_ADV1) / sizeof(DESK_ADV1[0]); }
    else { t = (const void *) DESK_ADV2; n = sizeof(DESK_ADV2) / sizeof(DESK_ADV2[0]); }
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t m = (lo + hi) / 2; if (t[m].cp < cp) lo = m + 1; else hi = m; }
    if (lo < n && t[lo].cp == cp) return t[lo].adv;
    return 0;                                       /* a character the face lacks is skipped (eid.c) */
}
float shim_eid_width_scaled(const char *text, float scale) {
    float w = 0.0f;
    const char *p = text;
    while (p && *p) w += (float) adv_of(next_cp(&p));
    return w * scale;
}
float shim_eid_line_height(void) { return 11.0f; }

/* text written: kept with its place */
typedef struct { char text[160]; float x, y, w; float r, g, b, a; } txt_t;
static txt_t g_txt[512];
static int g_ntxt;
void shim_eid_write_scaled(const char *text, float x, float y, float scale, float r, float g, float b, float a) {
    if (g_ntxt < 512) {
        txt_t *t = &g_txt[g_ntxt++];
        snprintf(t->text, sizeof(t->text), "%s", text);
        t->x = x; t->y = y; t->w = shim_eid_width_scaled(text, scale); t->r = r; t->g = g; t->b = b; t->a = a;
    }
}
static const txt_t *txt_find(const char *needle) {
    for (int i = 0; i < g_ntxt; i++) if (strstr(g_txt[i].text, needle)) return &g_txt[i];
    return NULL;
}

/* ------------------------------------------------------------------ helpers */
static void frame(void) {
    g_ntxt = 0;
    g_npics = 0;
    shim_spawner_between_frames();
    shim_spawner_draw();
    memset(g_pressed, 0, sizeof(g_pressed));
}

static void press(int action) { g_pressed[action] = 1; frame(); }

static void open_card(int tab) {
    shim_spawner_open();
    frame();
    if (g_tab != tab) { g_want_tab = tab; frame(); }
}

static void close_card(void) { shim_spawner_close(); frame(); }

static void type_text(const char *s) {
    char one[8];
    for (const char *p = s; *p; ) {
        const char *q = p;
        next_cp(&q);
        snprintf(one, sizeof(one), "%.*s", (int) (q - p), p);
        shim_spawner_text(one);
        p = q;
    }
    frame();
}

static void clear_query(void) { while (g_queries[0][0]) shim_spawner_text(""); frame(); }

/* a tap at canvas units (x, y): down and up */
static void tap(float gx, float gy) {
    float x = gx * 4.0f, y = gy * 4.0f;
    shim_spawner_touch(1, 0, x, y);
    shim_spawner_touch(1, 3, x, y);
    frame();
}

#include "tests.c"
