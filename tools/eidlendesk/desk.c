/*
 * THE EID LENGTH DESK (07.10.2026): how tall can the walk-up panel get.
 *
 * The real eid.c in this unit (src_eid.c, `long` widened for Windows), the
 * game's functions answered at their own addresses inside a fake image: a jump
 * to a stub here. The player is walked up to a pickup lying on the floor - the
 * very road the phone takes, shim_eid_between_frames -> nearest -> the
 * modifiers -> panel_layout - so the height is the port's own, not a copy of
 * it. Text is measured with the real advances of the description faces (the
 * .fnt files the APK carries), the sum the engine's 0x8bdec8 makes.
 *
 * Modes:
 *   case LANG KIND ID CHAR [golden] [own=ID,ID,...] [all]
 *        one panel, piece by piece (validation against a screenshot)
 *   sweep OUT.csv [LANG]
 *        every entry of every language, four tiers (bare, char, syn3, all - see sweep)
 *   typical RUNS OUT.csv LANG   (DESK_CHAR=18 DESK_OWN=584: a scenario on top)
 *        random runs: what a player with an ordinary inventory sees
 *   DESK_PAGES=1 sweep OUT.csv [LANG]
 *        the same sweep, and every panel's pages (eid.c, PAGES, hotfix 1.72) cut for
 *        3 devices x 3 canvases x 2 HUD offsets x 3 MOVE Y and checked: inside the
 *        canvas, and the rows end to end the whole description
 *   turns
 *        turning the pages: a finger, R3, Map with a shot, the resets
 *   hints
 *        every language has its hints, and every letter is in its face
 */
#include <windows.h>
#include <stdarg.h>
#include "src_eid.c"

int desk_clock_gettime(int clock, struct timespec *ts) { (void) clock; timespec_get(ts, TIME_UTC); return 0; }

#ifndef ISAAC_ROOT                  /* the project root, ending in "/"; the build passes it */
#define ISAAC_ROOT "../../"
#endif
#define ASSETS ISAAC_ROOT "port/app/src/main/assets/"

/* ------------------------------------------------------------------ the log: the description laid out */
static char g_last_text[8192];
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void) prio; (void) tag;
    if (strncmp(fmt, "eid: kind %u id %u -> %s: %s", 28)) return 0;
    va_list a;
    va_start(a, fmt);
    (void) va_arg(a, unsigned);
    (void) va_arg(a, unsigned);
    (void) va_arg(a, const char *);
    const char *text = va_arg(a, const char *);
    va_end(a);
    snprintf(g_last_text, sizeof(g_last_text), "%s", text ? text : "");
    return 0;
}

/* ------------------------------------------------------------------ the fake image */
static unsigned char *g_img;
#define IMG_SIZE 0xB00000
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

static unsigned char *g_game, *g_mgr, *g_me, *g_ent, *g_elist;
static unsigned long long g_player_vec[4], g_ent_vec[4];
static int *g_counts;
static int g_pill_effect[16];

/* ------------------------------------------------------------------ the font, from the .fnt the APK carries */
typedef struct { short adv[65536]; } advances_t;
static void st_font_load(unsigned char *obj, const char *path, void *x) {
    (void) x;
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    char full[512];
    snprintf(full, sizeof(full), ASSETS "%s", leaf);
    FILE *f = fopen(full, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc((size_t) n);
    fread(d, 1, (size_t) n, f); fclose(f);
    advances_t *adv = calloc(1, sizeof(advances_t));
    unsigned short *glyphs = (unsigned short *) (obj + 0x50);
    for (int i = 0; i < 65536; i++) glyphs[i] = 0xffff;
    long at = 4;
    int count = 0;
    while (at + 5 <= n) {
        int t = d[at];
        unsigned len; memcpy(&len, d + at + 1, 4);
        const unsigned char *b = d + at + 5;
        if (t == 2) { unsigned short lh; memcpy(&lh, b, 2); *(unsigned short *) (obj + 0x14) = lh; }
        if (t == 4) {
            for (unsigned k = 0; k + 20 <= len; k += 20) {
                unsigned id; short xa;
                memcpy(&id, b + k, 4); memcpy(&xa, b + k + 16, 2);
                if (id < 65536) { adv->adv[id] = xa; glyphs[id] = (unsigned short) (count++ & 0x7fff); }
            }
        }
        at += 5 + len;
    }
    free(d);
    *(void **) (obj + 0x38) = adv;
    *(void **) (obj + 0x40) = adv;
}
static unsigned next_cp(const char **pp) {
    const unsigned char *p = (const unsigned char *) *pp;
    unsigned c = p[0];
    int n = 1;
    if (c >= 0xf0 && p[1] && p[2] && p[3]) { c = ((c & 7u) << 18) | ((p[1] & 63u) << 12) | ((p[2] & 63u) << 6) | (p[3] & 63u); n = 4; }
    else if (c >= 0xe0 && p[1] && p[2]) { c = ((c & 15u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u); n = 3; }
    else if (c >= 0xc0 && p[1]) { c = ((c & 31u) << 6) | (p[1] & 63u); n = 2; }
    *pp += n;
    return c;
}
static int st_measure(unsigned char *font, const char *s) {
    advances_t *adv = *(advances_t **) (font + 0x40);
    if (!adv || !s) return 0;
    int w = 0;
    while (*s) { unsigned cp = next_cp(&s); if (cp < 65536) w += adv->adv[cp]; }
    return w;
}

/* ------------------------------------------------------------------ stubs at the game's addresses */
/* the game's accessor answers from the list, as 0x4da574 does (co-op: more than one) */
static unsigned long long st_player(unsigned long long list, int i) { (void) list; return g_player_vec[i >= 0 && i < 4 ? i : 0]; }
static void st_noop(void) { }
/* the game's "is this action held" (0x671c5c): the actions the test holds, as bits */
static unsigned g_actions;
/* co-op (coop mode): every device its own held actions, g_actions_of[controller] */
static int g_per_device;
static unsigned g_actions_of[16];
static int st_action_held(unsigned long long input, int action, int controller) {
    (void) input;
    if (action < 0 || action >= 32) return 0;
    if (g_per_device) return controller >= 0 && controller < 16 && ((g_actions_of[controller] >> action) & 1);
    return (g_actions >> action) & 1;
}
static void st_sprite_load(unsigned char *s, const void *path, int force) { (void) path; (void) force; s[0x149] = 1; }
static unsigned st_curses(unsigned long long game) { (void) game; return 0; }
static int st_pill_effect(unsigned long long pool, int colour, unsigned long long player) {
    (void) pool; (void) player;
    return (colour >= 0 && colour < 16) ? g_pill_effect[colour] : -1;
}
static int st_grid_index(unsigned long long room, const void *pos) { (void) room; (void) pos; return -1; }

/* ------------------------------------------------------------------ the port's other files, as stubs */
void report_line(const char *kind, const char *text) { (void) kind; (void) text; }
int shim_report_active(void) { return 0; }
/* the player's device class (devices.c): DEVICE_SCREEN by default, as on the phone */
static int g_device_kind = DEVICE_SCREEN;
/* co-op (coop mode): a kind and a pad slot per device id */
static int g_kind_of[16], g_slot_of[16];
int shim_device_kind(int id) {
    if (g_per_device) return id >= 0 && id < 16 ? g_kind_of[id] : DEVICE_NONE;
    return g_device_kind;
}
int shim_device_pad_slot(int id) { return g_per_device && id >= 0 && id < 16 ? g_slot_of[id] : -1; }
static unsigned g_slot_buttons[4];
unsigned shim_pad_slot_buttons(int slot) { return slot >= 0 && slot < 4 ? g_slot_buttons[slot] : 0; }
/* coop.c's "a player of his own": all of them, unless a test says one is not (Esau) */
static unsigned long long g_not_own;
int shim_coop_own_player(unsigned long long p) { return p && p != g_not_own; }
static unsigned long long g_craft_player;
unsigned long long shim_eid_craft_player(void) { return g_craft_player; }
int shim_eid_craft_in_battle(void) { return 0; }
int shim_eid_craft_text(char *out, unsigned long long room, unsigned *stamp) { (void) out; (void) room; (void) stamp; return 0; }
int shim_eid_glitch_name(int id, char *out, unsigned long long n, int *q) { (void) id; (void) out; (void) n; (void) q; return 0; }
int shim_eid_glitch_text(int id, char *out, unsigned long long room) { (void) id; (void) out; (void) room; return 0; }
int shim_eid_glitch_own_text(int id, char *out, unsigned long long n) { (void) id; (void) out; (void) n; return 0; }
static int g_items_up;
int shim_items_open(void) { return g_items_up; }
/* the canvas's height (screen.c) and the pad's buttons (pad.c): the pages' world */
static float g_canvas = 270.0f;
float shim_screen_height(void) { return g_canvas; }
static unsigned g_pad_buttons;
unsigned shim_pad_buttons(void) { return g_pad_buttons; }
int shim_sticks_in_game(void) { return 1; }
static int g_eid_switch = 1;
int shim_tweaks_mod(int i) { (void) i; return g_eid_switch; }
int shim_port_length(const char *rel, const char *anim) { (void) rel; (void) anim; return 1000; }
void *shim_str_assign(void *self, const char *s) { (void) s; return self; }
void shim_str_dtor(void *self) { (void) self; }
int shim_trace_readable(const void *p, unsigned long long n) { (void) n; return p != NULL; }
void shim_pack_add(const char *path, const void *data, long long length) { (void) path; (void) data; (void) length; }
void shim_pack_port_file(const char *asset, const char *path) { (void) asset; (void) path; }
int shim_pack_has(const char *path) {
    const char *leaf = strrchr(path, '/');
    char full[512];
    snprintf(full, sizeof(full), ASSETS "%s", leaf ? leaf + 1 : path);
    FILE *f = fopen(full, "rb");
    if (f) fclose(f);
    return f != NULL;
}
/* the page's defaults (modset.c), the owner's own MOVE (x=0, y=0 in his port-touch.ini) */
static int g_ms_y;
int shim_modset_int(int i) {
    switch (i) {
        case MS_EID_QUALITY: return 1;
        case MS_EID_TRANSFORM: return 1;
        case MS_EID_STAT_ICONS: return 0;
        case MS_EID_HEALTHUP: return 1;
        case MS_EID_DISTANCE: return 5;
        case MS_EID_SHOP_CARDS: return 0;
        case MS_EID_SHOP_PILLS: return 1;
        case MS_EID_SHOP_SOULS: return 1;
        case MS_EID_UNREACHABLE: return 0;
        case MS_EID_FLIGHT: return 1;
        case MS_EID_UNIDENTIFIED: return 0;
        case MS_EID_SPINDOWN: return 3;
        case MS_EID_X: return 0;
        case MS_EID_Y: return g_ms_y;
        default: return 0;
    }
}
float shim_modset_float(int i) { (void) i; return 1.0f; }

/* the asset manager, from the assets folder */
struct AAsset { void *data; long long n; };
AAsset *AAssetManager_open(AAssetManager *mgr, const char *name, int mode) {
    (void) mgr; (void) mode;
    char full[512];
    snprintf(full, sizeof(full), ASSETS "%s", name);
    FILE *f = fopen(full, "rb");
    if (!f) return NULL;
    AAsset *a = calloc(1, sizeof(AAsset));
    fseek(f, 0, SEEK_END); a->n = ftell(f); fseek(f, 0, SEEK_SET);
    a->data = malloc((size_t) a->n);
    fread(a->data, 1, (size_t) a->n, f); fclose(f);
    return a;
}
const void *AAsset_getBuffer(AAsset *a) { return a->data; }
long long AAsset_getLength64(AAsset *a) { return a->n; }
void AAsset_close(AAsset *a) { (void) a; }

/* ------------------------------------------------------------------ the world */
static void image_up(void) {
    g_img = VirtualAlloc(NULL, IMG_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    g_game = calloc(1, 0x260000);
    g_mgr = calloc(1, 0x200000);
    g_me = calloc(1, 0x3000);
    g_ent = calloc(1, 0x1000);
    g_elist = calloc(1, 0x100);
    g_counts = calloc(MAX_ITEM_ID + 16, sizeof(int));
    *(unsigned long long *) (g_img + GAME_PTR) = (unsigned long long) g_game;
    *(unsigned long long *) (g_img + MANAGER) = (unsigned long long) g_mgr;
    g_player_vec[0] = (unsigned long long) g_me;
    *(unsigned long long *) (g_game + GAME_PLAYERS + LIST_BEGIN) = (unsigned long long) &g_player_vec[0];
    *(unsigned long long *) (g_game + GAME_PLAYERS + LIST_END) = (unsigned long long) &g_player_vec[1];
    *(float *) (g_mgr + HUD_OFFSET_F) = 1.0f;
    *(unsigned long long *) (g_me + PLAYER_OWNED) = (unsigned long long) g_counts;
    float *pos = (float *) (g_me + ENT_POSITION); pos[0] = 320.0f; pos[1] = 280.0f;
    float *epos = (float *) (g_ent + ENT_POSITION); epos[0] = 320.0f; epos[1] = 300.0f;
    g_ent[ENT_EXISTS] = 1;
    g_ent_vec[0] = (unsigned long long) g_ent;
    *(unsigned long long *) (g_elist + LIST_ITEMS) = (unsigned long long) g_ent_vec;
    *(unsigned *) (g_elist + LIST_COUNT) = 1;
    /* Isaac's start, for Consolation Prize: speed 1, tears 10, damage 3.5, range 6.5 tiles; 0/1/0 */
    *(float *) (g_me + PLAYER_MOVE_SPEED) = 1.0f;
    *(float *) (g_me + PLAYER_FIRE_DELAY) = 10.0f;
    *(float *) (g_me + PLAYER_DAMAGE) = 3.5f;
    *(float *) (g_me + PLAYER_TEAR_RANGE) = 260.0f;
    *(int *) (g_me + PLAYER_BOMBS) = 1;
    for (int c = 1; c < 15; c++) g_game[ITEM_POOL + PILL_IDENTIFIED + c] = 1;
    thunk(FN_PLAYER, (void *) st_player);
    thunk(FN_TEXT_WIDTH, (void *) st_measure);
    thunk(FN_FONT_LOAD, (void *) st_font_load);
    thunk(FN_FONT_INIT, (void *) st_noop);
    thunk(FN_SPRITE_NEW, (void *) st_noop);
    thunk(FN_SPRITE_LOAD, (void *) st_sprite_load);
    thunk(FN_SPRITE_FRAME, (void *) st_noop);
    thunk(FN_SPRITE_RENDER, (void *) st_noop);
    thunk(FN_SPRITE_REPLACE, (void *) st_noop);
    thunk(FN_SPRITE_GRAPHICS, (void *) st_noop);
    thunk(FN_GET_CURSES, (void *) st_curses);
    thunk(FN_PILL_EFFECT, (void *) st_pill_effect);
    thunk(FN_GRID_INDEX, (void *) st_grid_index);
    thunk(FN_KCOLOR, (void *) st_noop);
    thunk(FN_DRAWSTR, (void *) st_noop);
    thunk(FN_DRAW_SCALED, (void *) st_noop);
    thunk(FN_ACTION_HELD, (void *) st_action_held);
    shim_eid_open((void *) 1);
    shim_eid_entity_list((unsigned long long) g_elist);
}

/* the game's language number for a database language, where the game has one (0 otherwise) */
static int game_language_of(const char *code) {
    if (!strcmp(code, "ja_jp")) return 2;
    if (!strcmp(code, "fr")) return 3;
    if (!strcmp(code, "spa")) return 4;
    if (!strcmp(code, "de")) return 5;
    if (!strcmp(code, "ru")) return 10;
    if (!strcmp(code, "ko_kr")) return 11;
    if (!strcmp(code, "zh_cn")) return 13;
    return 0;
}
static void set_language(const char *code) {
    *(int *) (g_mgr + LANGUAGE) = game_language_of(code);
    shim_eid_language_pin(code);
}

/* ------------------------------------------------------------------ one state of the run */
typedef struct {
    int character;
    int difficulty, stage, stage_type;
    float luck;
    int trinket[2];                 /* with the golden bit as the game keeps it */
    int pocket_card[4], pocket_pill[4];     /* a card id; a pill EFFECT (+1, as the database counts) */
    int active[4];
} run_t;

static void run_apply(const run_t *r) {
    *(int *) (g_me + PLAYER_TYPE) = r->character;
    *(int *) (g_game + GAME_DIFFICULTY) = r->difficulty;
    *(int *) (g_game + GAME_STAGE) = r->stage;
    *(int *) (g_game + GAME_STAGE_TYPE) = r->stage_type;
    *(float *) (g_me + PLAYER_LUCK) = r->luck;
    int *tr = (int *) (g_me + PLAYER_TRINKETS);
    tr[0] = r->trinket[0]; tr[1] = r->trinket[1];
    int *pk = (int *) (g_me + PLAYER_POCKET);
    for (int i = 0; i < 4; i++) { pk[i * 2] = 0; pk[i * 2 + 1] = -1; }
    int slot = 0;
    for (int i = 0; i < 4 && slot < 4; i++)
        if (r->pocket_card[i] > 0) { pk[slot * 2] = r->pocket_card[i]; pk[slot * 2 + 1] = POCKET_CARD; slot++; }
    /* pocket pills: colours 2..5, each made to hold the wanted effect */
    for (int i = 0; i < 4 && slot < 4; i++)
        if (r->pocket_pill[i] > 0) {
            int colour = 2 + i;
            g_pill_effect[colour] = r->pocket_pill[i] - 1;
            pk[slot * 2] = colour; pk[slot * 2 + 1] = POCKET_PILL; slot++;
        }
    for (int i = 0; i < 4; i++)
        *(int *) (g_me + PLAYER_ACTIVES + i * ACTIVE_STRIDE) = r->active[i];
}

/* what lies on the floor */
static void floor_thing(unsigned kind, unsigned id, int golden) {
    int *ident = (int *) (g_ent + ENT_TYPE);
    ident[0] = PICKUP_TYPE;
    switch (kind) {
        case EID_COLLECTIBLE: ident[1] = VAR_COLLECTIBLE; ident[2] = (int) id; break;
        case EID_TRINKET: ident[1] = VAR_TRINKET; ident[2] = (int) id | (golden ? TRINKET_GOLDEN : 0); break;
        case EID_CARD: ident[1] = VAR_CARD; ident[2] = (int) id; break;
        case EID_PILL: case EID_HORSEPILL:
            ident[1] = VAR_PILL;
            g_pill_effect[1] = (int) id - 1;
            ident[2] = 1 | (kind == EID_HORSEPILL ? PILL_HORSE : 0);
            break;
        case EID_DICE: ident[0] = EFFECT_TYPE; ident[1] = VAR_DICE_FLOOR; ident[2] = (int) id - 1; break;
    }
}

typedef struct {
    float height;                   /* g_height: from the panel's corner to under its last line */
    float top;                      /* the corner's y (shim_eid_panel_place) */
    int pieces;
    int text_len;                   /* the description's bytes as laid out */
    int wrap_cap;                   /* a block wrapped to MAX_WRAP lines (the rest is lost) */
} panel_out_t;

static int wrap_capped(const char *text) {
    if (!text || !*text) return 0;
    char trimmed[4096];
    drop_stat_icons(text, trimmed, sizeof(trimmed));
    const char *p = trimmed;
    while (*p) {
        const char *end = strchr(p, '#');
        size_t n = end ? (size_t) (end - p) : strlen(p);
        char block[1200];
        if (n >= sizeof(block)) n = sizeof(block) - 1;
        memcpy(block, p, n); block[n] = 0;
        p += n; if (*p == '#') p++;
        char plain[1200];
        take_indents(block, plain, sizeof(plain));
        float text_at = M_TEXT_OFFSET;
        size_t len = 0;
        const char *head = leading_icon(plain, &len);
        if (head && holds_big(head, len)) text_at = M_TEXT_BIG;
        char lines[MAX_WRAP][320];
        if (fit_to_width(plain, M_BOX - (text_at - M_TEXT_OFFSET), lines) >= MAX_WRAP) return 1;
    }
    return 0;
}

static panel_out_t lay(unsigned kind, unsigned id, int golden) {
    panel_out_t o;
    memset(&o, 0, sizeof(o));
    floor_thing(kind, id, golden);
    g_forget = 1;
    g_last_text[0] = 0;
    shim_eid_between_frames();
    o.height = g_page_count > 1 ? g_full_height : g_height;    /* the whole panel, paged or not */
    float x, y;
    shim_eid_panel_place(&x, &y);
    o.top = y;
    o.pieces = g_page_count > 1 ? g_full_n : g_pieces;
    o.text_len = (int) strlen(g_last_text);
    o.wrap_cap = wrap_capped(g_last_text);
    return o;
}

/* ------------------------------------------------------------------ owning */
static void own_none(void) { memset(g_counts, 0, (MAX_ITEM_ID + 1) * sizeof(int)); }
static void own_all(void) {
    for (int i = 0; i <= MAX_ITEM_ID; i++) g_counts[i] = item_row(i, 0) ? 1 : 0;
}

/* the items whose holding can change this entry's panel */
static int relevant_items(unsigned kind, unsigned id, int *out, int max) {
    int n = 0;
    unsigned want = kind == EID_HORSEPILL ? EID_PILL : kind;
    #define ADD(x) do { int v_ = (x); int dup_ = 0; for (int k_ = 0; k_ < n; k_++) if (out[k_] == v_) dup_ = 1; \
                        if (!dup_ && n < max && v_ > 0 && v_ <= MAX_ITEM_ID) out[n++] = v_; } while (0)
    for (int i = 0; i < EID_RULES; i++) {
        const eid_rule_t *r = &EID_RULE[i];
        if (r->kind != (int) want || (r->id >= 0 && (unsigned) r->id != id)) continue;
        if (r->test == RULE_OWNS && r->own_kind == EID_COLLECTIBLE) ADD(r->value);
        if (r->test == RULE_LIST && r->arg1 >= 0 && r->arg1 < EID_LISTS)
            for (const short *p = EID_LIST[r->arg1]; *p >= 0; p++) ADD(*p);
        if (r->test == RULE_MULTI_ACTIVE) ADD(ITEM_SCHOOLBAG);
        if (r->test == RULE_CHARGE && r->value) ADD(r->value);
        if (r->test == RULE_RESTOCK) ADD(376);
    }
    static const int MODS[] = { ITEM_SPINDOWN, ITEM_CAR_BATTERY, ITEM_MOMS_BOX, ITEM_BLANK_CARD, ITEM_CLEAR_RUNE,
                                ITEM_PLACEBO, ITEM_BOOK_OF_VIRTUES, ITEM_ABYSS, ITEM_FALSE_PHD, ITEM_PHD,
                                ITEM_LUCKY_FOOT, ITEM_VIRGO };
    for (unsigned i = 0; i < sizeof(MODS) / sizeof(MODS[0]); i++) ADD(MODS[i]);
    for (int i = 0; i < EID_FEATHER_ITEMS_COUNT; i++) ADD(EID_FEATHER_ITEMS[i]);
    #undef ADD
    return n;
}

/* the trinkets, cards and pills this entry's conditions ask about; and what else the run decides */
typedef struct {
    int trinkets[16], ntr;
    int cards[16], ncards;
    int pills[16], npills;
    int greedy, stagey, charge[4][2], ncharge;
    int has_stage[16], nstage;
} needs_t;

static void needs_of(unsigned kind, unsigned id, needs_t *nd) {
    memset(nd, 0, sizeof(*nd));
    unsigned want = kind == EID_HORSEPILL ? EID_PILL : kind;
    for (int i = 0; i < EID_RULES; i++) {
        const eid_rule_t *r = &EID_RULE[i];
        if (r->kind != (int) want || (r->id >= 0 && (unsigned) r->id != id)) continue;
        if (r->test == RULE_OWNS && r->own_kind == EID_TRINKET && nd->ntr < 16) nd->trinkets[nd->ntr++] = r->value;
        if (r->test == RULE_OWNS && r->own_kind == EID_CARD && nd->ncards < 16) nd->cards[nd->ncards++] = r->value;
        if (r->test == RULE_OWNS && r->own_kind == EID_PILL && nd->npills < 16) nd->pills[nd->npills++] = r->value;
        if (r->test == RULE_GREED || r->test == RULE_HARD || r->test == RULE_SHOP || r->test == RULE_RESTOCK) nd->greedy = 1;
        if (r->test == RULE_STAGE_EQ || r->test == RULE_STAGE_GT || r->test == RULE_SHOP) {
            nd->stagey = 1;
            int s[3] = { r->arg1, r->arg1 + 1, 0 };
            for (int k = 0; k < 3; k++) {
                int dup = 0;
                for (int j = 0; j < nd->nstage; j++) if (nd->has_stage[j] == s[k]) dup = 1;
                if (!dup && nd->nstage < 16 && s[k] >= 0 && s[k] <= 13) nd->has_stage[nd->nstage++] = s[k];
            }
        }
        if (r->test == RULE_HELD_CHARGE && nd->ncharge < 4) {
            nd->charge[nd->ncharge][0] = r->arg1; nd->charge[nd->ncharge][1] = r->arg2; nd->ncharge++;
        }
    }
    if (kind == EID_COLLECTIBLE && id == ITEM_PANDORAS_BOX) nd->stagey = 1;
}

/* an active of this charge and kind, for the HELD_CHARGE conditions */
static int active_like(int charges, int ckind) {
    for (int i = 1; i <= EID_ACTIVE_MAX; i++) {
        if (EID_ACTIVE_KIND[i] < 0 || !item_row(i, 0)) continue;
        if (charges >= 0 && EID_ACTIVE_CHARGES[i] != charges) continue;
        if (ckind >= 0 && EID_ACTIVE_KIND[i] != ckind) continue;
        return i;
    }
    return 0;
}

/* the run every "rich" state starts from: what this entry's conditions ask to be held */
static void rich_run(const needs_t *nd, run_t *r) {
    memset(r, 0, sizeof(*r));
    r->stage = 1;
    r->luck = 2.0f;
    for (int i = 0; i < 2 && i < nd->ntr; i++) r->trinket[i] = nd->trinkets[i];
    for (int i = 0; i < 4 && i < nd->ncards; i++) r->pocket_card[i] = nd->cards[i];
    for (int i = 0, k = nd->ncards; i < nd->npills && k < 4; i++, k++) r->pocket_pill[i] = nd->pills[i];
    r->active[0] = 105;                            /* the D6 in hand */
    if (nd->ncharge) {
        int a = active_like(nd->charge[0][0], nd->charge[0][1]);
        if (a) r->active[0] = a;
    }
}

/* the bottom of the panel on the canvas: its corner plus its height, less the last line's air */
static float bottom_of(const panel_out_t *o) { return o->top + o->height - 1.0f; }

/* every character, every difficulty and floor that matters: the tallest bottom */
typedef struct { panel_out_t o; float bottom; int character, difficulty, stage; } best_t;

static best_t tallest_over_run(unsigned kind, unsigned id, int golden, const needs_t *nd, run_t base) {
    best_t best;
    memset(&best, 0, sizeof(best));
    best.bottom = -1e9f;
    int diffs = nd->greedy ? 4 : 1;
    int stages = nd->stagey ? (nd->nstage ? nd->nstage : 13) : 1;
    for (int c = 0; c < EID_PLAYER_KINDS; c++)
        for (int d = 0; d < diffs; d++)
            for (int s = 0; s < stages; s++) {
                run_t r = base;
                r.character = c;
                r.difficulty = d;
                r.stage = nd->stagey ? (nd->nstage ? nd->has_stage[s] : s + 1) : 1;
                run_apply(&r);
                panel_out_t o = lay(kind, id, golden);
                float b = bottom_of(&o);
                if (b > best.bottom) { best.o = o; best.bottom = b; best.character = c; best.difficulty = d; best.stage = r.stage; }
            }
    return best;
}

/* ------------------------------------------------------------------ the entries of a language */
static const unsigned KINDS[] = { EID_COLLECTIBLE, EID_TRINKET, EID_CARD, EID_PILL, EID_HORSEPILL, EID_DICE };

static int ids_of(unsigned kind, unsigned *out, int max) {
    const unsigned char *lang = g_db + g_en_at;
    unsigned sections = rd32(lang);
    int n = 0;
    for (unsigned s = 0; s < sections; s++) {
        const unsigned char *sec = lang + 4 + s * 12;
        if (rd32(sec) != kind) continue;
        unsigned count = rd32(sec + 4);
        const unsigned char *rows = g_db + rd32(sec + 8);
        for (unsigned r = 0; r < count && n < max; r++) out[n++] = rd32(rows + r * 12);
    }
    return n;
}

static void csv_text(FILE *f, const char *s) {
    fputc('"', f);
    for (; s && *s; s++) { if (*s == '"') fputc('"', f); fputc(*s, f); }
    fputc('"', f);
}

static int g_greedy_search = 1;

/*
 * THE TIERS a panel is measured in, for each entry:
 *   bare    Isaac, nothing held, luck 0 - the description alone
 *   char    nothing held, every character (its own lines, its heart type,
 *           Tainted Isaac's and Tainted ???'s extra HUD row that lowers the panel)
 *   syn3    the three held things that make it tallest, added one by one
 *           (greedy), luck 1, every character - "a run with a few synergies"
 *   all     everything held, then items dropped while that makes it taller
 *           (a description some item REPLACES by a shorter one) - the ceiling
 */
typedef struct { int type, id; } cand_t;          /* type 0 a collectible, 1 a trinket, 2 a card, 3 a pill */

static void cand_apply(const cand_t *c, int on, run_t *r) {
    if (c->type == 0) { g_counts[c->id] = on ? 1 : 0; return; }
    if (!on) return;
    if (c->type == 1) { if (!r->trinket[0]) r->trinket[0] = c->id; else r->trinket[1] = c->id; }
    if (c->type == 2) { for (int i = 0; i < 4; i++) if (!r->pocket_card[i]) { r->pocket_card[i] = c->id; break; } }
    if (c->type == 3) { for (int i = 0; i < 4; i++) if (!r->pocket_pill[i]) { r->pocket_pill[i] = c->id; break; } }
}

static float at_chars(unsigned kind, unsigned id, int golden, const needs_t *nd, run_t base,
                      const int *chars, int nchars) {
    float best = -1e9f;
    int diffs = nd->greedy ? 4 : 1;
    int stages = nd->stagey ? (nd->nstage ? nd->nstage : 13) : 1;
    for (int ci = 0; ci < nchars; ci++)
        for (int d = 0; d < diffs; d++)
            for (int s = 0; s < stages; s++) {
                run_t r = base;
                r.character = chars[ci]; r.difficulty = d;
                r.stage = nd->stagey ? (nd->nstage ? nd->has_stage[s] : s + 1) : 1;
                run_apply(&r);
                panel_out_t o = lay(kind, id, golden);
                if (bottom_of(&o) > best) best = bottom_of(&o);
            }
    return best;
}

/*
 * THE PAGES CHECKED (hotfix 1.72): the panel just laid out, cut again for every
 * canvas a phone gives (270, 300 on 16:10, 360 on 4:3), HUD Offset 1 and 0 and
 * MOVE Y -40, 0, +60; every page composed and measured; and the pages' rows put
 * end to end must be the whole description, piece for piece, nothing lost and
 * nothing twice.
 */
static long long g_pv_cases, g_pv_paged, g_pv_pages, g_pv_over, g_pv_cover, g_pv_whole, g_pv_unpageable;
static int g_pv_most_pages;
static float g_pv_worst = -1e9f;
static char g_pv_worst_at[200];

static int same_piece(const piece_t *a, const piece_t *b) {
    return a->is_icon == b->is_icon && a->row == b->row && a->px == b->px && a->slot == b->slot
        && !strcmp(a->is_icon ? a->icon.name : a->text, b->is_icon ? b->icon.name : b->text);
}

static void verify_pages(const char *what) {
    static const float CANVASES[] = { 270.0f, 300.0f, 360.0f };
    static const float HUDS[] = { 1.0f, 0.0f };
    static const int MOVES[] = { 0, -40, 60 };
    float hud_was = *(float *) (g_mgr + HUD_OFFSET_F);
    int y_was = g_ms_y;
    float canvas_was = g_canvas;
    int device_was = g_device_kind;
    static const int DEVICES[] = { DEVICE_SCREEN, DEVICE_PAD, DEVICE_KEYBOARD };   /* three hints, three widths */
    for (int di = 0; di < 3; di++)
    for (int ci = 0; ci < 3; ci++)
        for (int hi = 0; hi < 2; hi++)
            for (int mi = 0; mi < 3; mi++) {
                g_device_kind = DEVICES[di];
                g_canvas = CANVASES[ci];
                *(float *) (g_mgr + HUD_OFFSET_F) = HUDS[hi];
                g_ms_y = MOVES[mi];
                pages_tick();
                if (g_page_count <= 0) continue;            /* the bag's screen, or nothing */
                g_pv_cases++;
                float x, top;
                shim_eid_panel_place(&x, &top);
                float room = g_canvas - top;
                if (g_page_count == 1) {
                    if (g_full_height > room + 0.01f) g_pv_unpageable++;   /* no rows to cut */
                    int same = g_pieces == g_full_n;
                    for (int i = 0; same && i < g_full_n; i++)
                        same = same_piece(&g_piece[i], &g_full[i]) && g_piece[i].py == g_full[i].py;
                    if (!same) g_pv_whole++;
                    continue;
                }
                g_pv_paged++;
                g_pv_pages += g_page_count;
                if (g_page_count > g_pv_most_pages) g_pv_most_pages = g_page_count;
                int ok = g_page_start[0] == 0 && g_page_start[g_page_count] == g_rows;
                for (int p = 0; p < g_page_count; p++) ok = ok && g_page_start[p + 1] > g_page_start[p];
                int at = 0;                                    /* the next body piece of the whole */
                while (at < g_full_n && g_full[at].row < 0) at++;
                for (int p = 0; p < g_page_count; p++) {
                    g_page = p;
                    page_compose(p);
                    float over = top + g_height - g_canvas;
                    if (over > 0.01f) {
                        g_pv_over++;
                        if (over > g_pv_worst) {
                            g_pv_worst = over;
                            snprintf(g_pv_worst_at, sizeof(g_pv_worst_at), "%s %s, canvas %.0f hud %.0f move %d, page %d/%d",
                                     what, g_name, (double) g_canvas, (double) HUDS[hi], g_ms_y, p + 1, g_page_count);
                        }
                    }
                    for (int i = 0; i < g_pieces; i++) {
                        if (g_piece[i].row < 0) continue;      /* the header, the "more" lines */
                        if (at >= g_full_n || !same_piece(&g_piece[i], &g_full[at])) { ok = 0; break; }
                        at++;
                        while (at < g_full_n && g_full[at].row < 0) at++;
                    }
                }
                if (at != g_full_n) ok = 0;
                if (!ok) g_pv_cover++;
                g_page = 0;
                page_compose(0);
            }
    g_canvas = canvas_was;
    g_device_kind = device_was;
    *(float *) (g_mgr + HUD_OFFSET_F) = hud_was;
    g_ms_y = y_was;
    pages_tick();
}

static void put_best(FILE *out, unsigned kind, unsigned id, int golden, const best_t *b, run_t base) {
    run_t r = base;
    r.character = b->character; r.difficulty = b->difficulty; r.stage = b->stage;
    run_apply(&r);
    panel_out_t o = lay(kind, id, golden);
    if (getenv("DESK_PAGES")) verify_pages(g_lang_code);
    fprintf(out, ",%.1f,%.1f,%.1f,%d,%d,%d,%d,", o.height, o.top, bottom_of(&o), b->character, o.text_len, o.pieces, o.wrap_cap);
    csv_text(out, g_last_text);
}

static int sweep(const char *path, const char *only_lang) {
    FILE *out = fopen(path, "wb");
    if (!out) return 1;
    fprintf(out, "lang,kind,id,golden,name");
    static const char *const TIER[4] = { "bare", "char", "syn3", "all" };
    for (int t = 0; t < 4; t++)
        fprintf(out, ",%s_h,%s_top,%s_bottom,%s_char,%s_len,%s_pieces,%s_wrapcap,%s_text", TIER[t], TIER[t], TIER[t],
                TIER[t], TIER[t], TIER[t], TIER[t], TIER[t]);
    fprintf(out, ",syn3_held\n");
    int langs = shim_eid_language_count();
    for (int li = 0; li < langs; li++) {
        const char *code = shim_eid_language_code(li);
        if (only_lang && strcmp(only_lang, code)) continue;
        set_language(code);
        DWORD t0 = GetTickCount();
        int done = 0;
        for (unsigned ki = 0; ki < sizeof(KINDS) / sizeof(KINDS[0]); ki++) {
            unsigned kind = KINDS[ki];
            static unsigned ids[2048];
            int n = ids_of(kind, ids, 2048);
            for (int ii = 0; ii < n; ii++) {
                unsigned id = ids[ii];
                if (kind == EID_COLLECTIBLE && !item_row((int) id, 0)) continue;
                for (int golden = 0; golden <= (kind == EID_TRINKET ? 1 : 0); golden++) {
                    needs_t nd;
                    needs_of(kind, id, &nd);
                    const char *name = shim_eid_name(kind == EID_HORSEPILL ? EID_PILL : kind, id);
                    fprintf(out, "%s,%u,%u,%d,", code, kind, id, golden);
                    csv_text(out, name ? name : "");

                    /* bare */
                    run_t bare;
                    memset(&bare, 0, sizeof(bare));
                    bare.stage = 1;
                    own_none();
                    best_t b0;
                    memset(&b0, 0, sizeof(b0));
                    b0.stage = 1;
                    put_best(out, kind, id, golden, &b0, bare);

                    /* char */
                    own_none();
                    best_t b1 = tallest_over_run(kind, id, golden, &nd, bare);
                    put_best(out, kind, id, golden, &b1, bare);

                    /* syn3: candidates are what the conditions and the modifiers ask about */
                    cand_t cand[128];
                    int nc = 0;
                    int rel[96], nrel = relevant_items(kind, id, rel, 96);
                    for (int k = 0; k < nrel && nc < 128; k++) cand[nc++] = (cand_t) { 0, rel[k] };
                    for (int k = 0; k < nd.ntr && nc < 128; k++) cand[nc++] = (cand_t) { 1, nd.trinkets[k] };
                    for (int k = 0; k < nd.ncards && nc < 128; k++) cand[nc++] = (cand_t) { 2, nd.cards[k] };
                    for (int k = 0; k < nd.npills && nc < 128; k++) cand[nc++] = (cand_t) { 3, nd.pills[k] };
                    run_t syn = bare;
                    syn.luck = 1.0f;
                    syn.active[0] = 105;
                    if (nd.ncharge) { int a = active_like(nd.charge[0][0], nd.charge[0][1]); if (a) syn.active[0] = a; }
                    own_none();
                    int chosen[3], nchosen = 0;
                    int probe[3] = { 0, 21, b1.character };
                    float now = at_chars(kind, id, golden, &nd, syn, probe, 3);
                    for (int step = 0; step < 3; step++) {
                        int pick = -1; float pick_h = now;
                        for (int k = 0; k < nc; k++) {
                            int used = 0;
                            for (int j = 0; j < nchosen; j++) if (chosen[j] == k) used = 1;
                            if (used) continue;
                            run_t r = syn;
                            cand_apply(&cand[k], 1, &r);
                            float h = at_chars(kind, id, golden, &nd, r, probe, 3);
                            cand_apply(&cand[k], 0, &r);
                            if (h > pick_h + 0.01f) { pick = k; pick_h = h; }
                        }
                        if (pick < 0) break;
                        chosen[nchosen++] = pick;
                        cand_apply(&cand[pick], 1, &syn);
                        now = pick_h;
                    }
                    best_t b2 = tallest_over_run(kind, id, golden, &nd, syn);
                    put_best(out, kind, id, golden, &b2, syn);

                    /* all */
                    run_t rich;
                    rich_run(&nd, &rich);
                    own_all();
                    best_t b3 = tallest_over_run(kind, id, golden, &nd, rich);
                    if (g_greedy_search) {
                        int dropped[96] = { 0 }, any = 0, improved = 1;
                        while (improved) {
                            improved = 0;
                            for (int k = 0; k < nrel; k++) {
                                if (dropped[k]) continue;
                                g_counts[rel[k]] = 0;
                                run_t r = rich;
                                r.character = b3.character; r.difficulty = b3.difficulty; r.stage = b3.stage;
                                run_apply(&r);
                                panel_out_t o = lay(kind, id, golden);
                                if (bottom_of(&o) > b3.bottom + 0.01f) { dropped[k] = 1; any = improved = 1; b3.bottom = bottom_of(&o); }
                                else g_counts[rel[k]] = 1;
                            }
                        }
                        if (any) { best_t again = tallest_over_run(kind, id, golden, &nd, rich); if (again.bottom >= b3.bottom) b3 = again; }
                    }
                    put_best(out, kind, id, golden, &b3, rich);

                    /* what syn3 held */
                    char held[256] = "";
                    static const char T[4] = { 'c', 't', 'k', 'p' };
                    for (int j = 0; j < nchosen; j++) {
                        size_t at = strlen(held);
                        snprintf(held + at, sizeof(held) - at, "%c%d ", T[cand[chosen[j]].type], cand[chosen[j]].id);
                    }
                    fputc(',', out);
                    csv_text(out, held);
                    fputc('\n', out);
                    done++;
                }
            }
        }
        fprintf(stderr, "%-6s %d entries, %lu ms\n", code, done, (unsigned long) (GetTickCount() - t0));
        fflush(out);
    }
    fclose(out);
    if (getenv("DESK_PAGES")) {
        fprintf(stderr, "pages: %lld panels checked, %lld paged (%lld pages, at most %d on one); "
                        "a page past the canvas: %lld; rows lost, doubled or out of order: %lld; "
                        "a short panel not shown whole as before: %lld; too tall with no rows to cut: %lld\n",
                g_pv_cases, g_pv_paged, g_pv_pages, g_pv_most_pages, g_pv_over, g_pv_cover, g_pv_whole, g_pv_unpageable);
        if (g_pv_over) fprintf(stderr, "pages: the worst page is %.1f past, %s\n", (double) g_pv_worst, g_pv_worst_at);
        return (g_pv_over || g_pv_cover || g_pv_whole) ? 1 : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ one panel, piece by piece */
static int one_case(int argc, char **argv) {
    /* case LANG KIND ID CHAR [golden] [own=1,2,3] [all] [hud=F] [luck=F] */
    if (argc < 6) return 2;
    set_language(argv[2]);
    unsigned kind = (unsigned) atoi(argv[3]), id = (unsigned) atoi(argv[4]);
    run_t r;
    memset(&r, 0, sizeof(r));
    r.character = atoi(argv[5]);
    r.stage = 1;
    r.active[0] = 0;
    int golden = 0;
    own_none();
    for (int i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "golden")) golden = 1;
        else if (!strcmp(argv[i], "all")) own_all();
        else if (!strncmp(argv[i], "own=", 4)) {
            for (char *p = argv[i] + 4; *p; ) { int v = atoi(p); if (v > 0 && v <= MAX_ITEM_ID) g_counts[v] = 1;
                                                  while (*p && *p != ',') p++; if (*p) p++; }
        } else if (!strncmp(argv[i], "hud=", 4)) *(float *) (g_mgr + HUD_OFFSET_F) = (float) atof(argv[i] + 4);
        else if (!strncmp(argv[i], "luck=", 5)) r.luck = (float) atof(argv[i] + 5);
        else if (!strncmp(argv[i], "trinket=", 8)) r.trinket[0] = atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "canvas=", 7)) g_canvas = (float) atof(argv[i] + 7);
    }
    run_apply(&r);
    panel_out_t o = lay(kind, id, golden);
    /* what is dumped: the page the port shows (page 0 by default), page=N, or the whole panel as before pages */
    for (int i = 6; i < argc; i++) {
        if (!strncmp(argv[i], "page=", 5) && g_page_count > 1) {
            int p = atoi(argv[i] + 5);
            if (p >= 0 && p < g_page_count) { g_page = p; page_compose(p); }
        } else if (!strcmp(argv[i], "whole") && g_page_count > 1) {
            memcpy(g_piece, g_full, sizeof(piece_t) * (size_t) g_full_n);
            g_pieces = g_full_n;
            g_height = g_full_height;
        }
    }
    printf("pages %d, shown %d, page height %.1f\n", g_page_count, g_page + 1, (double) g_height);
    printf("lang %s  %s  top %.1f  height %.1f  bottom %.1f  pieces %d  text %d bytes  wrapcap %d\n",
           g_lang_code, g_name, o.top, o.height, bottom_of(&o), o.pieces, o.text_len, o.wrap_cap);
    printf("text: %s\n", g_last_text);
    for (int i = 0; i < g_pieces; i++) {
        const piece_t *pc = &g_piece[i];
        printf("  %3d  y %6.1f (screen %6.1f)  x %6.1f  %s%s\n", i, (double) pc->py, (double) (o.top + pc->py),
               (double) pc->px, pc->is_icon ? "ICON " : "", pc->is_icon ? pc->icon.name : pc->text);
    }
    /* the same pieces for a renderer (tools/eidlendesk/mockups.py): tab-separated, one a line */
    printf("T\t%.1f\t%.1f\n", o.top, g_height);
    for (int i = 0; i < g_pieces; i++) {
        const piece_t *pc = &g_piece[i];
        int key = 0;
        const char *gfx = "";
        if (pc->is_icon && pc->slot >= 0 && pc->slot < ITEM_SLOTS) {
            key = g_item_key[(int) pc->slot];
            const char *g = item_gfx(key > 0 ? key : -key, key < 0);
            if (g) gfx = g;
        }
        printf("P\t%d\t%.3f\t%.3f\t%.4f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%s\t%d\t%d\t%s\t%s\n", pc->is_icon,
               (double) pc->px, (double) pc->py, (double) pc->scale, (double) pc->r, (double) pc->g, (double) pc->b,
               (double) pc->a, pc->is_icon ? sheet_for(&pc->icon) : -1, pc->is_icon && pc->icon.anim ? pc->icon.anim : "",
               pc->is_icon ? pc->icon.frame : 0, key, gfx, pc->is_icon ? pc->icon.name : pc->text);
    }
    return 0;
}

/* ------------------------------------------------------------------ ordinary runs */
/*
 * A TYPICAL RUN, drawn at random: a character a player can pick, 8 to 20
 * collectibles, a trinket, a card half the time, luck -1..3, a floor 1..10,
 * hard mode one run in five. Every entry is then walked up to in that run.
 * Rows: one per (run, entry) whose panel passes 270, and a summary per run.
 */
static unsigned g_rng = 12345;
static unsigned rnd(unsigned n) { g_rng = g_rng * 1103515245u + 12345u; return ((g_rng >> 8) & 0xffffff) % n; }

static int typical(int runs, const char *path, const char *lang) {
    FILE *out = fopen(path, "wb");
    if (!out) return 1;
    set_language(lang);
    static const int PICKABLE[] = { 0,1,2,3,4,5,6,7,8,9,10,13,14,15,16,18,19,
                                    21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37 };
    int nitems = 0;
    static int items[MAX_ITEM_ID + 1];
    for (int i = 1; i <= MAX_ITEM_ID; i++) if (item_row(i, 0)) items[nitems++] = i;
    static unsigned ids[6][2048];
    int nids[6];
    for (unsigned ki = 0; ki < 6; ki++) nids[ki] = ids_of(KINDS[ki], ids[ki], 2048);
    long long walkups = 0, over[4] = { 0 };
    float thresholds[4] = { 270.0f, 300.0f, 360.0f, 282.0f };
    fprintf(out, "run,character,held,kind,id,golden,bottom,height\n");
    for (int run = 0; run < runs; run++) {
        run_t r;
        memset(&r, 0, sizeof(r));
        r.character = PICKABLE[rnd(sizeof(PICKABLE) / sizeof(PICKABLE[0]))];
        r.stage = 1 + (int) rnd(10);
        r.difficulty = rnd(5) == 0 ? 1 : 0;
        r.luck = (float) ((int) rnd(5) - 1);
        r.trinket[0] = 1 + (int) rnd(189);
        if (rnd(2)) r.pocket_card[0] = 1 + (int) rnd(97);
        r.active[0] = 105;
        own_none();
        int held = 8 + (int) rnd(13);
        for (int k = 0; k < held; k++) g_counts[items[rnd((unsigned) nitems)]] = 1;
        /* a scenario on top of the random run: DESK_CHAR=18 (Bethany), DESK_OWN=584,59 (held as well) */
        if (getenv("DESK_CHAR")) r.character = atoi(getenv("DESK_CHAR"));
        if (getenv("DESK_OWN"))
            for (const char *p = getenv("DESK_OWN"); *p; ) {
                int v = atoi(p);
                if (v > 0 && v <= MAX_ITEM_ID) g_counts[v] = 1;
                while (*p && *p != ',') p++;
                if (*p) p++;
            }
        run_apply(&r);
        for (unsigned ki = 0; ki < 6; ki++) {
            unsigned kind = KINDS[ki];
            for (int ii = 0; ii < nids[ki]; ii++) {
                unsigned id = ids[ki][ii];
                if (kind == EID_COLLECTIBLE && !item_row((int) id, 0)) continue;
                for (int golden = 0; golden <= (kind == EID_TRINKET ? 1 : 0); golden++) {
                    panel_out_t o = lay(kind, id, golden);
                    float b = bottom_of(&o);
                    walkups++;
                    for (int t = 0; t < 4; t++) if (b > thresholds[t]) over[t]++;
                    if (b > 270.0f)
                        fprintf(out, "%d,%d,%d,%u,%u,%d,%.1f,%.1f\n", run, r.character, held, kind, id, golden, b, o.height);
                }
            }
        }
    }
    fprintf(stderr, "%s: %d runs, %lld walk-ups; past 270: %lld (%.2f%%), past 300: %lld, past 360: %lld; still past 270 at HUD offset 0 (bottom > 282): %lld\n",
            lang, runs, walkups, over[0], 100.0 * (double) over[0] / (double) walkups, over[1], over[2], over[3]);
    fclose(out);
    return 0;
}

/*
 * THE PAGES' WORDS (hotfix 1.72): every language of the database has its row of
 * hints (eid.c, PAGE_HINT), and every letter of the hints and of the recipes'
 * "more" line is in the face that language is drawn with.
 */
static int hint_letters(const char *s, unsigned long long font, const char *code, const char *which) {
    const unsigned short *glyphs = (const unsigned short *) (font + 0x50);
    int bad = 0;
    for (const char *p = s; *p; ) {
        if (p[0] == '{' && p[1] == '{') { const char *e = strstr(p, "}}"); if (e) { p = e + 2; continue; } }
        unsigned cp = next_cp(&p);
        if (cp < 65536 && glyphs[cp] == 0xffff && cp != ' ') {
            printf("  %s %s: U+%04X is not in its face\n", code, which, cp);
            bad++;
        }
    }
    return bad;
}

static int hints(void) {
    int bad = 0, langs = shim_eid_language_count();
    for (int li = 0; li < langs; li++) {
        const char *code = shim_eid_language_code(li);
        set_language(code);
        int row = -1;
        for (int i = 0; i < COUNT(PAGE_HINT); i++) if (!strcmp(PAGE_HINT[i].lang, code)) row = i;
        if (row < 0) { printf("  %s: no row of hints\n", code); bad++; continue; }
        unsigned long long font = panel_font((unsigned long long) g_mgr);
        bad += hint_letters(PAGE_HINT[row].touch, font, code, "touch");
        bad += hint_letters(PAGE_HINT[row].pad, font, code, "pad");
        bad += hint_letters(PAGE_HINT[row].keys, font, code, "keys");
        const char *more = text_of(EID_PHRASE, PHRASE_MORE);
        bad += hint_letters(more ? more : "", font, code, "more");
        printf("%-6s more \"%s\" | %s | %s | %s\n", code, more ? more : "(none)", PAGE_HINT[row].touch,
               PAGE_HINT[row].pad, PAGE_HINT[row].keys);
    }
    printf("hints: %d languages, %d letters missing\n", langs, bad);
    return bad != 0;
}

/*
 * TURNING THE PAGES (hotfix 1.72), eid.c's own road: pages_tick between frames,
 * a finger's ask from items.c, R3 on a pad, Map with a shot on a keyboard.
 */
static int g_tf, g_tp;
#define TCHECK(c, ...) do { if (c) g_tp++; else { g_tf++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void tick(int drawn) {
    if (drawn) g_drawn_ns = pages_drawn_now();
    shim_eid_between_frames();
}

static void knife(void) {          /* Azazel at Mom's Knife, English, Brimstone held: two pages on 270 */
    set_language("en_us");
    run_t r;
    memset(&r, 0, sizeof(r));
    r.character = 7; r.stage = 1;
    own_none();
    g_counts[118] = 1;
    run_apply(&r);
    floor_thing(EID_COLLECTIBLE, 114, 0);
}

static int turns(void) {
    g_canvas = 270.0f;
    g_device_kind = DEVICE_SCREEN;
    knife();
    g_forget = 1;
    tick(1);
    TCHECK(g_page_count == 2 && g_page == 0, "the knife: two pages, the first shown (%d, %d)", g_page_count, g_page);
    TCHECK(shim_eid_pages_showing(), "the zone may live");
    /* a finger */
    /* a swipe right asks for the next (+1), a swipe left the previous; both go round (07.10.2026) */
    shim_eid_pages_touch_turn(1); tick(1);
    TCHECK(g_page == 1, "swipe right: page 2 (%d)", g_page);
    shim_eid_pages_touch_turn(1); tick(1);
    TCHECK(g_page == 0, "swipe right on the last: round to the first (%d)", g_page);
    shim_eid_pages_touch_turn(-1); tick(1);
    TCHECK(g_page == 1, "swipe left on the first: round to the last (%d)", g_page);
    shim_eid_pages_touch_turn(-1); tick(1);
    TCHECK(g_page == 0, "swipe left: page 1 (%d)", g_page);
    /* not on screen: nothing turns, and the ask is not kept for later */
    g_drawn_ns = 0;
    shim_eid_pages_touch_turn(1); tick(0);
    TCHECK(g_page == 0, "not drawn: no turn (%d)", g_page);
    TCHECK(!shim_eid_pages_showing(), "not drawn: no zone");
    tick(1);
    TCHECK(g_page == 0, "and the ask is gone (%d)", g_page);
    /* the inventory up: nothing turns */
    g_items_up = 1;
    shim_eid_pages_touch_turn(1); tick(1);
    TCHECK(g_page == 0, "the list up: no turn (%d)", g_page);
    g_items_up = 0;
    /* a pad: R3, round */
    g_device_kind = DEVICE_PAD;
    tick(1);
    g_pad_buttons = PAD_R3; tick(1);
    TCHECK(g_page == 1, "R3: page 2 (%d)", g_page);
    tick(1);
    TCHECK(g_page == 1, "R3 held: no more (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    g_pad_buttons = PAD_R3; tick(1);
    TCHECK(g_page == 0, "R3 on the last: round to the first (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    /* L3: the previous, round the other way (the owner, 07.10.2026) */
    g_pad_buttons = PAD_L3; tick(1);
    TCHECK(g_page == 1, "L3 on the first: round to the last (%d)", g_page);
    tick(1);
    TCHECK(g_page == 1, "L3 held: no more (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    g_pad_buttons = PAD_L3; tick(1);
    TCHECK(g_page == 0, "L3: back to page 1 (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    g_pad_buttons = PAD_R3 | PAD_L3; tick(1);
    TCHECK(g_page == 0, "both at once: one forward, one back (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    /* the pad, but the shots and the map do nothing */
    g_actions = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_DOWN); tick(1);
    TCHECK(g_page == 0, "a pad: Map+shot does nothing (%d)", g_page);
    g_actions = 0; tick(1);
    /* a keyboard: Map held with a shot */
    g_device_kind = DEVICE_KEYBOARD;
    tick(1);
    g_actions = 1u << ACTION_SHOOT_DOWN; tick(1);
    TCHECK(g_page == 0, "a shot without Map: nothing (%d)", g_page);
    g_actions = 1u << ACTION_MAP; tick(1);
    g_actions = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_DOWN); tick(1);
    TCHECK(g_page == 1, "Map + shoot down: page 2 (%d)", g_page);
    tick(1);
    TCHECK(g_page == 1, "held: no more (%d)", g_page);
    g_actions = 1u << ACTION_MAP; tick(1);
    g_actions = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_UP); tick(1);
    TCHECK(g_page == 0, "Map + shoot up: page 1 (%d)", g_page);
    g_actions = 1u << ACTION_MAP; tick(1);
    g_actions = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_UP); tick(1);
    TCHECK(g_page == 0, "shoot up on the first: stays (%d)", g_page);
    g_actions = 1u << ACTION_MAP; tick(1);
    g_actions = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_RIGHT); tick(1);
    TCHECK(g_page == 1, "Map + shoot right: page 2 (%d)", g_page);
    g_actions = 1u << ACTION_MAP; tick(1);
    g_actions = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_DOWN); tick(1);
    TCHECK(g_page == 0, "shoot down on the last: round (%d)", g_page);
    g_actions = 0; tick(1);
    /* the touch screen: R3 and the keys mean nothing */
    g_device_kind = DEVICE_SCREEN; tick(1);
    g_pad_buttons = PAD_R3; tick(1);
    TCHECK(g_page == 0, "a finger player: R3 nothing (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    g_pad_buttons = PAD_L3; tick(1);
    TCHECK(g_page == 0, "a finger player: L3 nothing (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    /* the same thing laid out anew (the inventory put away): the page is kept */
    shim_eid_pages_touch_turn(1); tick(1);
    TCHECK(g_page == 1, "page 2 again (%d)", g_page);
    shim_eid_pin(0, 0); tick(1);
    TCHECK(g_page == 1 && g_page_count == 2, "laid out anew: still page 2 (%d of %d)", g_page, g_page_count);
    /* another thing: back to its first page; a button already down turns nothing */
    g_device_kind = DEVICE_PAD;
    g_pad_buttons = PAD_R3;
    run_t b;
    memset(&b, 0, sizeof(b));
    b.character = 18; b.stage = 1;
    own_none();
    g_counts[584] = 1;
    run_apply(&b);
    floor_thing(EID_COLLECTIBLE, 352, 0);
    tick(1);
    TCHECK(g_page_count >= 2 && g_page == 0, "Bethany at Glass Cannon: its first page (%d of %d)", g_page, g_page_count);
    tick(1);
    TCHECK(g_page == 0, "R3 held from before: no turn (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    g_pad_buttons = PAD_R3; tick(1);
    TCHECK(g_page == 1, "pressed now: a turn (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    g_pad_buttons = PAD_L3; tick(1);
    TCHECK(g_page == 0, "L3 there: back (%d)", g_page);
    g_pad_buttons = 0; tick(1);
    /* the canvas grows (a 4:3 tablet, FULLSCREEN): cut again; on 360 it fits whole */
    g_canvas = 360.0f; tick(1);
    TCHECK(g_page_count <= 1 && g_height == g_full_height, "on 360 it fits: one page, whole (%d, %.1f / %.1f)",
           g_page_count, (double) g_height, (double) g_full_height);
    TCHECK(!shim_eid_pages_showing(), "and no zone");
    g_canvas = 270.0f; tick(1);
    TCHECK(g_page_count >= 2, "back on 270: pages again (%d)", g_page_count);
    /* walked away: no pages */
    *(unsigned char *) (g_ent + ENT_EXISTS) = 0; tick(1);
    TCHECK(g_page_count == 0 && !shim_eid_pages_showing(), "nothing near: no pages (%d)", g_page_count);
    *(unsigned char *) (g_ent + ENT_EXISTS) = 1;
    /* EID off: nothing */
    tick(1);
    g_eid_switch = 0; tick(1);
    TCHECK(!shim_eid_pages_showing(), "EID off: no zone");
    g_eid_switch = 1;
    printf("turns: %d passed, %d failed\n", g_tp, g_tf);
    return g_tf != 0;
}

/*
 * CO-OP (1.74, players' reports on 1.73: "EID works for nobody but the host").
 * Two players of their own on two devices, two things on the floor: the panel
 * describes the first player's nearest and, when he is near nothing, the
 * next player's (main.lua 1443-1497); the panel's player pages it from his own
 * device and the hint and the button pictures are his; the list's player's
 * transformations; one player of his own (Jacob and Esau) is single player.
 */
static unsigned char *g_me2, *g_ent2;
static int g_owned2[MAX_ITEM_ID + 16];

#define DEV_SCREEN  1                       /* the desk's device ids, kinds in g_kind_of */
#define DEV_PAD_A   2
#define DEV_PAD_B   3
#define DEV_KEYS    4

static void at(unsigned char *e, float x, float y) {
    float *p = (float *) (e + ENT_POSITION);
    p[0] = x; p[1] = y;
}
static void seat(unsigned char *p, int device) { *(int *) (p + PLAYER_DEVICE) = device; }
static void lying(unsigned char *e, unsigned id) {
    int *ident = (int *) (e + ENT_TYPE);
    ident[0] = PICKUP_TYPE; ident[1] = VAR_COLLECTIBLE; ident[2] = (int) id;
    e[ENT_EXISTS] = 1;
}
static void players(int n) {
    g_player_vec[0] = (unsigned long long) g_me;
    g_player_vec[1] = (unsigned long long) g_me2;
    *(unsigned long long *) (g_game + GAME_PLAYERS + LIST_END) = (unsigned long long) &g_player_vec[n];
}
static void coop_up(void) {
    g_me2 = calloc(1, 0x3000);
    g_ent2 = calloc(1, 0x1000);
    *(unsigned long long *) (g_me2 + PLAYER_OWNED) = (unsigned long long) g_owned2;
    *(int *) (g_me2 + PLAYER_TYPE) = 3;                 /* Judas */
    g_ent_vec[0] = (unsigned long long) g_ent;
    g_ent_vec[1] = (unsigned long long) g_ent2;
    *(unsigned *) (g_elist + LIST_COUNT) = 2;
    g_per_device = 1;
    g_kind_of[DEV_SCREEN] = DEVICE_SCREEN; g_slot_of[DEV_SCREEN] = -1;
    g_kind_of[DEV_PAD_A] = DEVICE_PAD;     g_slot_of[DEV_PAD_A] = 0;
    g_kind_of[DEV_PAD_B] = DEVICE_PAD;     g_slot_of[DEV_PAD_B] = 1;
    g_kind_of[DEV_KEYS] = DEVICE_KEYBOARD; g_slot_of[DEV_KEYS] = -1;
    players(2);
    seat(g_me, DEV_SCREEN);
    seat(g_me2, DEV_PAD_B);
}

#define FAR_X 2000.0f

static int coop(void) {
    coop_up();
    g_canvas = 270.0f;
    set_language("en_us");
    run_t r;
    memset(&r, 0, sizeof(r));
    r.character = 7; r.stage = 1;                      /* Azazel with Brimstone: Mom's Knife is two pages */
    own_none();
    g_counts[118] = 1;
    run_apply(&r);
    lying(g_ent, 1);                                   /* The Sad Onion */
    lying(g_ent2, 114);                                /* Mom's Knife */
    at(g_ent, 320.0f, 300.0f);
    at(g_ent2, 900.0f, 300.0f);

    /* descriptions: the first player's nearest first */
    at(g_me, 320.0f, 280.0f); at(g_me2, 900.0f, 280.0f);
    g_forget = 1; tick(1);
    TCHECK(g_paged_kind == EID_COLLECTIBLE && g_paged_id == 1, "both near: the first player's Sad Onion (%u)", g_paged_id);
    TCHECK(shim_eid_panel_player() == (unsigned long long) g_me, "the panel is the first player's");
    /* he walks away: the second player's Mom's Knife */
    at(g_me, FAR_X, 280.0f);
    tick(1);
    TCHECK(g_paged_id == 114 && g_name[0], "the first near nothing: the second player's Mom's Knife (%u)", g_paged_id);
    TCHECK(shim_eid_panel_player() == (unsigned long long) g_me2, "the panel is the second player's");
    TCHECK(g_page_count == 2, "two pages, as for anyone (%d)", g_page_count);
    TCHECK(g_paged_device == 1, "his device is a pad: the pad's hint (%d)", g_paged_device);
    /* the second player at the first one's thing */
    at(g_me2, 320.0f, 280.0f);
    tick(1);
    TCHECK(g_paged_id == 1 && shim_eid_panel_player() == (unsigned long long) g_me2,
           "the second player at the Sad Onion: his panel (%u)", g_paged_id);
    /* nobody near anything */
    at(g_me2, FAR_X, 0.0f);
    tick(1);
    TCHECK(!g_name[0] && g_page_count == 0, "nobody near: no panel");
    TCHECK(shim_eid_panel_player() == (unsigned long long) g_me, "and the panel's player is the first again");

    /* pages from the panel's player's own device */
    at(g_me2, 900.0f, 280.0f);
    tick(1); tick(1);
    TCHECK(g_paged_id == 114 && g_page == 0, "his knife, page 1 (%d)", g_page);
    g_slot_buttons[0] = PAD_R3; tick(1);
    TCHECK(g_page == 0, "R3 on the OTHER pad: no turn (%d)", g_page);
    g_slot_buttons[0] = 0; tick(1);
    g_slot_buttons[1] = PAD_R3; tick(1);
    TCHECK(g_page == 1, "R3 on his own pad: page 2 (%d)", g_page);
    g_slot_buttons[1] = 0; tick(1);
    g_slot_buttons[1] = PAD_L3; tick(1);
    TCHECK(g_page == 0, "L3 on his own pad: page 1 (%d)", g_page);
    g_slot_buttons[1] = 0; tick(1);
    /* a finger pages only player 0's panel (the owner, 11.10.2026): this one is the second player's */
    TCHECK(!shim_eid_panel_touchable(), "the second player's panel: no finger's zone");
    shim_eid_pages_touch_turn(1); tick(1);
    TCHECK(g_page == 0, "a swipe on his panel: no turn (%d)", g_page);
    /* his pad's picture for the hint's buttons */
    const eid_icon_t *select = NULL;
    for (int i = 0; i < COUNT(EID_ICONS); i++) if (!strcmp(EID_ICONS[i].name, "ButtonSelect")) select = &EID_ICONS[i];
    TCHECK(select && sheet_for(select) == SHEET_PAD, "his pad: the pad's pictures");

    /* the second player on the keyboard: Map with a shot on HIS keys */
    seat(g_me2, DEV_KEYS);
    tick(1);
    TCHECK(g_paged_device == 0, "his keyboard: the keys' hint (%d)", g_paged_device);
    TCHECK(select && sheet_for(select) == select->sheet, "his keyboard: the keys' pictures");
    g_actions_of[DEV_SCREEN] = (1u << ACTION_MAP); tick(1);
    g_actions_of[DEV_SCREEN] = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_DOWN); tick(1);
    TCHECK(g_page == 0, "Map + shot on the FIRST player's device: no turn (%d)", g_page);
    g_actions_of[DEV_SCREEN] = 0; tick(1);
    g_actions_of[DEV_KEYS] = (1u << ACTION_MAP); tick(1);
    g_actions_of[DEV_KEYS] = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_DOWN); tick(1);
    TCHECK(g_page == 1, "Map + shoot down on his keys: page 2 (%d)", g_page);
    g_actions_of[DEV_KEYS] = (1u << ACTION_MAP); tick(1);
    g_actions_of[DEV_KEYS] = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_UP); tick(1);
    TCHECK(g_page == 0, "Map + shoot up on his keys: page 1 (%d)", g_page);
    g_actions_of[DEV_KEYS] = 0; tick(1);

    /* the first player on the keyboard and the panel his: his keys turn it, the pad's R3 not */
    seat(g_me, DEV_KEYS); seat(g_me2, DEV_PAD_B);
    at(g_me, 900.0f, 280.0f); at(g_me2, FAR_X, 0.0f);
    tick(1); tick(1);
    TCHECK(shim_eid_panel_player() == (unsigned long long) g_me && g_paged_device == 0,
           "the first player on keys at the knife: his panel, the keys' hint (%d)", g_paged_device);
    int was = g_page;
    g_slot_buttons[1] = PAD_R3; tick(1);
    TCHECK(g_page == was, "the second player's R3: no turn (%d)", g_page);
    g_slot_buttons[1] = 0; tick(1);
    g_actions_of[DEV_KEYS] = (1u << ACTION_MAP); tick(1);
    g_actions_of[DEV_KEYS] = (1u << ACTION_MAP) | (1u << ACTION_SHOOT_RIGHT); tick(1);
    TCHECK(g_page != was, "his Map + shoot right: a turn (%d)", g_page);
    g_actions_of[DEV_KEYS] = 0; tick(1);
    /* the first player's (player 0's) panel takes a finger */
    TCHECK(shim_eid_panel_touchable(), "player 0's panel: a finger's zone");
    was = g_page;
    shim_eid_pages_touch_turn(1); tick(1);
    TCHECK(g_page != was, "a swipe on player 0's panel: a turn (%d)", g_page);
    seat(g_me, DEV_SCREEN);

    /* the list's player's transformations (items.c asks while his list is up) */
    own_none();
    memset(g_owned2, 0, sizeof(g_owned2));
    int form_item = EID_FORM_ITEMS[0].id, form = EID_FORM_ITEMS[0].form;
    ((int *) (g_me2 + PLAYER_FORMS))[form] = 2;
    const char *icon, *name;
    int have = 0, needed = 0;
    shim_eid_list_player((unsigned long long) g_me2);
    TCHECK(shim_eid_form(form, &icon, &name, &have, &needed) && have == 2,
           "his list: his transformation (%d/%d)", have, needed);
    shim_eid_list_player(0);
    have = 0;
    TCHECK(!shim_eid_form(form, &icon, &name, &have, &needed), "no list: the first player's, none (%d)", have);
    ((int *) (g_me2 + PLAYER_FORMS))[form] = 0;
    (void) form_item;

    /* one player of his own (Jacob and Esau): single player, Esau describes nothing */
    g_not_own = (unsigned long long) g_me2;
    at(g_me, FAR_X, 280.0f); at(g_me2, 900.0f, 280.0f);
    tick(1);
    TCHECK(!g_name[0], "Esau at the knife, Jacob near nothing: no panel, as in single player");
    at(g_me, 320.0f, 280.0f);
    tick(1);
    TCHECK(g_paged_id == 1 && shim_eid_panel_player() == (unsigned long long) g_me, "Jacob at the Sad Onion: his (%u)", g_paged_id);
    g_not_own = 0;
    /* two players on ONE device are one (the mod's coopMainPlayers: the first per ControllerIndex) */
    seat(g_me2, DEV_SCREEN);
    at(g_me, FAR_X, 280.0f);
    tick(1);
    TCHECK(!g_name[0], "a second player on the first one's device: not searched");
    seat(g_me2, DEV_PAD_B);
    tick(1);
    TCHECK(g_paged_id == 114, "on his own device again: searched (%u)", g_paged_id);
    /* only one player in the run */
    players(1);
    tick(1);
    TCHECK(!g_name[0], "one player, far: no panel");
    players(2);

    printf("coop: %d passed, %d failed\n", g_tp, g_tf);
    return g_tf != 0;
}

int main(int argc, char **argv) {
    image_up();
    if (argc >= 2 && !strcmp(argv[1], "coop")) return coop();
    if (argc >= 2 && !strcmp(argv[1], "turns")) return turns();
    if (argc >= 2 && !strcmp(argv[1], "hints")) return hints();
    if (argc >= 2 && !strcmp(argv[1], "case")) return one_case(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "typical"))
        return typical(argc >= 3 ? atoi(argv[2]) : 200, argc >= 4 ? argv[3] : "typical.csv", argc >= 5 ? argv[4] : "en_us");
    if (argc >= 2 && !strcmp(argv[1], "sweep")) {
        if (getenv("DESK_NO_GREEDY")) g_greedy_search = 0;
        return sweep(argc >= 3 ? argv[2] : "eidlen.csv", argc >= 4 ? argv[3] : NULL);
    }
    fprintf(stderr, "usage: case LANG KIND ID CHAR [golden] [own=..] [all] | sweep OUT.csv [LANG] | typical RUNS OUT.csv LANG\n");
    return 2;
}
