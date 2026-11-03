/*
 * TWO PC MODS, WITHOUT LUA.
 *
 * The port has no Lua, so the mods the owner plays with on the PC are made
 * again here from what the game already keeps (16.09.2026). Each is switched
 * in Tweaks (shim_tweaks_mod). Both draw inside the game's own HUD pass
 * (trace.c, mark 'X'), so whatever covers the HUD covers them too.
 *
 * BOSS RUSH WAVE COUNTER ("Boss Rush Wave Counter", Aaron Cohen). The mod
 * counts waves by watching the number of living bosses go from none to some.
 * The game does not need watching: Boss Rush is an Ambush, and the Ambush keeps
 * its own count (Game+0x1cf458):
 *
 *   +0x30  int   the number of waves, from ambush.xml (15), lowered by the
 *                game itself if its boss list runs out - so it is read, not
 *                written into the code
 *   +0x3c  int   the current wave: 0 before the first, raised by Ambush::Update
 *                (0x633e9c) as each wave is spawned, left at the last when done
 *
 * The room is Boss Rush when its type, [room+0x10], is 17 (Ambush::StartChallenge
 * 0x633044 picks the Boss Rush music by the same test). The picture is the
 * mod's own bar (gfx/port_wavebar.anm2, 64 x 32), the text the terminus font the
 * game loads at startup (Manager+0x56728), at the mod's size of 0.7.
 *
 * PLANETARIUM CHANCE ("Planetarium Chance"). The mod asks Level:GetPlanetariumChance
 * when a floor begins, keeps the number for the whole floor, and prints it as a
 * percentage under the stats with the planetarium icon (hudstats2.anm2 frame 8)
 * at half opacity, with a green or red "+x.x%" sliding out when it changed.
 * The game's function is 0x6d03a4(Game) -> float: read-only (the level
 * generator draws its RNG itself, after the call), the chance the NEXT floor
 * would get, which is exactly the mod's number. On an Ascent there are no
 * planetariums, and the mod shows zero - the Ascent is byte Game+0x1cf406 bit 0
 * on stages 1 to 6.
 *
 * WHERE IT GOES. The game's stats column is drawn by 0x6efac4(stats, &place),
 * and the place it is handed already carries everything the HUD worked out
 * before it - the HUD offset, the coin, bomb, key and charge rows, the row of
 * difficulty and challenge icons. So the place is taken from that call
 * (trace.c, mark '3') instead of redoing the sum. Below it the column is
 * 8 rows (7 with Duality, stats+0x2f8, where the two deal rows merge), 12
 * apart with one player and 14 when the second slot (stats+0x228) is filled -
 * the next free row is where the line goes. The text sits at +16, +1 and the
 * column's own lift (-1, or -5 with two players), in the column's font. The
 * column only exists with Found HUD on (Manager+0x55f02, the byte 0x6efb18
 * tests) and is skipped on daily runs; a column not drawn lately means no line.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include "diag.h"
#include "modset.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_tweaks_mod(int i);
int shim_eid_hud_showing(void);
int shim_eid_seed_on(int id);
int shim_picture_ready(unsigned char *sprite, signed char *state, const char *anm2);
void shim_picture_draw(unsigned char *sprite, const char *anim, int frame,
                       float x, float y, float alpha);
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

#define MOD_PLANETARIUM 1                   /* shim_tweaks_mod: dance, planet, waves, bars, marks */
#define MOD_WAVES       2

#define MANAGER_PTR     0xac2428
#define GAME_PTR        0xac3b90
#define DESIGN_CANVAS   0xac24d8
#define GAME_ROOM       0x21550
#define ROOM_TYPE       0x10
#define ROOM_BOSS_RUSH  17
#define GAME_AMBUSH     0x1cf458
#define AMBUSH_WAVES    0x30
#define AMBUSH_WAVE     0x3c
#define GAME_STAGE      0x0
#define GAME_STAGE_TYPE 0x4
/*
 * Game+0x2157c is NOT written by Level::Init (the old label said so): the image
 * writes it at 0x621270, 0x646350 and 0x733c48, the last a plain copy of
 * Game+0x21558, the index of the room being played. So it can change between
 * the rooms of one floor, and the planetarium chance below may be asked again
 * within a floor. Left so (02.10.2026, only the label is corrected): the
 * game's own function answers each time, so the number shown is the game's.
 */
#define GAME_ROOM_INDEX_COPY 0x2157c
#define GAME_ASCENT     0x1cf406
#define PERSISTENT_ACHIEVEMENTS 0x204f8      /* Manager+0x204d0+0x28 */
#define ACHIEVEMENT_PLANETARIUM 406
#define GAME_ROOMS      0x18
#define GAME_ROOM_COUNT 0x21510
#define ROOMS_MAX       507
#define ROOM_BYTES      0x100
#define ROOM_CONFIG     0x10
#define CONFIG_TYPE     0x8
#define ROOM_TREASURE   4
#define SEED_NO_HUD     10

#define FN_PLANETARIUM  0x6d03a4
#define FN_KCOLOR       0x8a34f4
#define FN_DRAW_SCALED  0x8be42c            /* (font, text, colour, box, centre, x, y, sx, sy) */
#define FN_TEXT_WIDTH   0x8bdec8
#define FONT_TERMINUS   0x56728
#define FONT_LUA_MINI   0xf68b8             /* loaded at 0x66f5cc, the stats' own font */

#define SPRITE_BYTES    0x158

static unsigned long game_now(unsigned long b) {
    return *(const unsigned long *) (b + GAME_PTR);
}

static unsigned long manager_now(unsigned long b) {
    return *(const unsigned long *) (b + MANAGER_PTR);
}

static void write_text(unsigned long b, unsigned long font, const char *text,
                       float x, float y, float scale, float r, float g, float bl, float a) {
    unsigned char colour[32];
    memset(colour, 0, sizeof(colour));
    ((void (*)(void *, float, float, float, float)) (b + FN_KCOLOR))(colour, r, g, bl, a);
    ((void (*)(unsigned long, const char *, void *, int, int, float, float, float, float))
        (b + FN_DRAW_SCALED))(font, text, colour, 0, 0, x, y, scale, scale);
}

static float text_width(unsigned long b, unsigned long font, const char *text) {
    return (float) ((int (*)(unsigned long, const char *)) (b + FN_TEXT_WIDTH))(font, text);
}

void shim_mods_open_assets(void *manager) {
    if (!manager) return;
    shim_asset_bring_in(manager, "port_wavebar.anm2", "gfx", "port_wavebar.anm2");
    shim_asset_bring_in(manager, "port_wavebar.png", "gfx", "port_wavebar.png");
    shim_asset_bring_in(manager, "port_completion_widget.png", "gfx", "port_completion_widget.png");
    shim_asset_bring_in(manager, "port_completion_widget_pause.png", "gfx",
                        "port_completion_widget_pause.png");
    void shim_mods_open_dance_assets(void *manager);
    shim_mods_open_dance_assets(manager);
    void shim_bossbars_open_assets(void *manager);
    shim_bossbars_open_assets(manager);
}

/* ------------------------------------------------- completion mark reminder */
/*
 * "Completion Mark Reminder" has no code at all: it swaps the two sheets of the
 * completion widget - the note on the character screen and on the pause paper -
 * for ones with every mark printed faintly on the paper, so the missing ones
 * show. Its Repentance set (resources-dlc3) keeps our sheets' layout exactly and
 * changes only the six papers; the icons are untouched.
 *
 * THE SWITCH. The first build answered the game's request for the widget sheet
 * with the mod's, in the pack. The game keeps a texture it has loaded under its
 * path, so switching the mod off changed nothing: the marks stayed (the owner,
 * 18.09.2026). Now the sheets keep their own paths and the widgets are pointed
 * at one or the other, the way the game itself points the pause widget at its
 * own sheet: 0x7e9dfc(widget, pause) loads gfx/ui/completion_widget.anm2 into
 * widget+8 and, for the pause, replaces the sheet of all twelve layers
 * (0x8a0228) and loads the graphics again. The same two calls, with the mod's
 * sheet or the game's, whenever the switch or the widget changes. Two
 * widgets: the character screen's at menu+0x1021b0+0x5d8 (0x5e5524), the
 * pause's at Game+0x10dd20+0x80cd0 (0x8a11a4).
 */
#define MOD_MARKS 4

#define MENU_PTR            0xac2490
#define MENU_WIDGET         (0x1021b0 + 0x5d8)
#define GAME_PAUSE_WIDGET   (0x10dd20 + 0x80cd0)
#define WIDGET_SPRITE       8
#define WIDGET_LAYERS       12
#define FN_SPRITE_REPLACE   0x8a0228        /* (sprite, layer, std::string const *) */
#define FN_SPRITE_GRAPHICS  0x8a0284        /* (sprite, 0) */
#define SPRITE_LOADED_AT    0x149

void shim_str_assign(void *s, const char *text);
void shim_str_dtor(void *s);

static void widget_sheet(unsigned long b, unsigned long sprite, const char *path) {
    void (*replace)(unsigned long, int, const void *) =
        (void (*)(unsigned long, int, const void *)) (b + FN_SPRITE_REPLACE);
    for (int layer = 0; layer < WIDGET_LAYERS; layer++) {
        long long raw[3];
        unsigned char *text = (unsigned char *) raw;
        text[23] = 0;
        shim_str_assign(text, path);
        replace(sprite, layer, text);
        shim_str_dtor(text);
    }
    ((void (*)(unsigned long, int)) (b + FN_SPRITE_GRAPHICS))(sprite, 0);
}

/*
 * The mod's sheets are drawn for the game's own widget: twelve layers, the
 * number the game's pause code replaces too. A completion_widget.anm2 the game
 * loads with another count (a user mod's) is not that layout - the switch then
 * leaves the widget alone, and the report says why (plan 30.09, M8).
 */
int shim_port_layer_count(const char *rel);
int shim_port_length(const char *rel, const char *anim);

const char *shim_usermods_peek(const char *path) __attribute__((weak));
const char *shim_pack_root(void);

static int widget_is_the_games(void) {
    static int known = -2;
    if (known == -2) {
        int n = shim_port_layer_count("gfx/ui/completion_widget.anm2");
        known = n == WIDGET_LAYERS;
        /*
         * A user mod's widget (plan 30.09, M9): the port's marked sheets are drawn
         * for the game's layout, so the reminder leaves a mod's widget alone.
         */
        if (known && shim_usermods_peek) {
            char full[600], own[2][600];
            snprintf(full, sizeof(full), "%srepentance-resources/data/gfx/ui/completion_widget.anm2", shim_pack_root());
            /* ... unless the mod brings the reminder's two sheets cut for its own widget (M9: the port's sheet
               applies only if the mod brings its own; usermods.c tells the MODS page which it is) */
            snprintf(own[0], sizeof(own[0]), "%sgfx/port_completion_widget.png", shim_pack_root());
            snprintf(own[1], sizeof(own[1]), "%sgfx/port_completion_widget_pause.png", shim_pack_root());
            if (shim_usermods_peek(full) && shim_usermods_peek(own[0]) && shim_usermods_peek(own[1])) {
                report_line("port layer", "completion_widget.anm2 comes from a user mod with its own marked sheets - "
                            "Completion Mark Reminder uses them");
            } else if (shim_usermods_peek(full)) {
                known = 0;
                report_line("port layer", "completion_widget.anm2 comes from a user mod - Completion Mark Reminder "
                            "leaves the widget alone");
                return known;
            }
        }
        if (n >= 0 && !known) {
            char line[160];
            snprintf(line, sizeof(line), "completion_widget.anm2 has %d layers, not %d - Completion Mark Reminder "
                     "leaves the widget alone", n, WIDGET_LAYERS);
            report_line("port layer", line);
        }
    }
    return known;
}

static void marks_between_frames(unsigned long b) {
    static const char *const SHEET[2][2] = {
        { "gfx/ui/completion_widget.png", "gfx/port_completion_widget.png" },
        { "gfx/ui/completion_widget_pause.png", "gfx/port_completion_widget_pause.png" },
    };
    static unsigned long was_sprite[2];
    static int was_on[2] = { -1, -1 };
    int on = shim_tweaks_mod(MOD_MARKS) != 0;
    if (on && !widget_is_the_games()) on = 0;
    unsigned long menu = *(const unsigned long *) (b + MENU_PTR), game = game_now(b);
    unsigned long sprite[2] = {
        menu ? menu + MENU_WIDGET + WIDGET_SPRITE : 0,
        game ? game + GAME_PAUSE_WIDGET + WIDGET_SPRITE : 0,
    };
    for (int w = 0; w < 2; w++) {
        unsigned long s = sprite[w];
        if (!s || !*(const unsigned char *) (s + SPRITE_LOADED_AT)) continue;
        if (s == was_sprite[w] && on == was_on[w]) continue;
        /* a new widget is the game's own until it is told otherwise */
        if (s != was_sprite[w] && !on) { was_sprite[w] = s; was_on[w] = 0; continue; }
        widget_sheet(b, s, SHEET[w][on]);
        was_sprite[w] = s;
        was_on[w] = on;
        LOG("marks: the %s widget wears the %s sheet", w ? "pause" : "menu", on ? "mod's" : "game's");
    }
}

/* ------------------------------------------------------- boss rush waves */
/*
 * Where the mod stands it: three presets, each the bar's middle, then moved by
 * the owner's x and y ([wavecounter] in port-touch.ini, modset.c). The text is
 * (barscale x 6) right of the bar's middle and (barscale x 22) above it, plus
 * its own nudge, centred in a box 7 wide, at 0.7 x textscale, in its colour.
 *
 *   default    (390, 29) of a 480 screen, just left of the minimap: the bar is
 *              64 wide about its middle, so it ends at 422, and the PC minimap
 *              begins at about 425 (the mod's own screenshot). The iOS minimap
 *              stands 30 further left - its frame is drawn from (width - 85,
 *              16) plus the HUD offset's (24, -14) * (1 - offset), 0x578624 -
 *              so a bar at the PC's place covered it (owner, eighth round).
 *              Here the bar keeps the mod's distance from the frame instead:
 *              35 left of it, and it follows the HUD offset as the frame does.
 *              Left of the map the phone has what the PC has not - the round
 *              pause button (489, 32) - so the bar also stands 3 left of it
 *              (the owner: on neither the map nor the pause button). The
 *              twins' button is not dodged: the player puts it where he likes,
 *              and one put under the bar is his to see (the owner, 24.09.2026).
 *   foundhud   (29, 237): the bottom-left, under the found HUD - 33 from the
 *              bottom of a 270 screen, kept from the bottom
 *   bossbar    over the game's boss bar: 104 left of the middle while a boss
 *              is alive, the middle when none is; the mod's 304 is on a screen
 *              taller than the canvas, here it is 34 from the bottom (I)
 */
#define MAP_FROM_RIGHT    85.0f             /* 0x578628: the frame's left, from the right edge */
#define MAP_TOP           16.0f
#define MAP_SHIFT_X       24.0f             /* 0x578510: the HUD offset's push */
#define MAP_SHIFT_Y       (-14.0f)
#define MANAGER_HUD_OFFSET 0x55f0c          /* float: 1 is flush */
#define WAVE_LEFT_OF_MAP  35.0f             /* the mod's 390 against the PC frame's 425 */
#define WAVE_BELOW_MAP    28.0f             /* the mod's 29 against the frame's 16, and 15 lower -
                                               the owner's own place (24.09.2026) */
#define WAVE_DRAWN_LEFT   29.0f             /* port_wavebar.png: the drawing about its point */
#define WAVE_DRAWN_RIGHT  30.0f
#define WAVE_DRAWN_TOP    27.0f
#define WAVE_DRAWN_BOTTOM 4.0f
#define WAVE_BUTTON_GAP   3.0f
#define OVERLAY_PAUSE     0x22ce0           /* the overlay's round pause Button (hud.c) */
#define BUTTON_LIVE_AT    0x0c
#define BUTTON_MIDDLE     0x2020c           /* a Button's box: middle, then whole size */
#define BUTTON_SIZE_AT    0x20214

unsigned long shim_pocket_overlay_at(void);
int shim_eid_readable(const void *p, unsigned long n);

/* moves the bar left of the pause button, when it is up and the bar would cover it */
static float wave_clear_pause(float x, float y, float scale) {
    unsigned long overlay = shim_pocket_overlay_at();
    if (!overlay) return x;
    unsigned long w = overlay + OVERLAY_PAUSE;
    if (!shim_eid_readable((const void *) w, BUTTON_SIZE_AT + 8)) return x;
    if (!*(const unsigned char *) (w + BUTTON_LIVE_AT)) return x;
    const float *c = (const float *) (w + BUTTON_MIDDLE), *sz = (const float *) (w + BUTTON_SIZE_AT);
    /*
     * A pause button the player moved (the touch editor, hotfix 1.71): the HUD is
     * drawn while the Button still holds the game's own place - the layout writes it
     * every update, and ours goes in at the Button's draw and hit test (hud.c) - so
     * the place is asked of hud.c, the same one those two write.
     */
    float moved[2];
    int shim_hud_pause_placed(float *x, float *y);
    if (shim_hud_pause_placed(&moved[0], &moved[1])) c = moved;
    if (!isfinite(c[0]) || !isfinite(c[1]) || !(sz[0] > 0.0f) || !(sz[1] > 0.0f)) return x;
    float left = c[0] - sz[0] * 0.5f, right = c[0] + sz[0] * 0.5f;
    float top = c[1] - sz[1] * 0.5f, bottom = c[1] + sz[1] * 0.5f;
    if (y - WAVE_DRAWN_BOTTOM * scale < top || y - WAVE_DRAWN_TOP * scale > bottom) return x;
    if (x + WAVE_DRAWN_RIGHT * scale <= left - WAVE_BUTTON_GAP || x - WAVE_DRAWN_LEFT * scale >= right) return x;
    return left - WAVE_BUTTON_GAP - WAVE_DRAWN_RIGHT * scale;
}
#define WAVE_HUD_X        29.0f
#define WAVE_HUD_FROM_BOTTOM 33.0f
#define WAVE_BOSS_LEFT    104.0f
#define WAVE_BOSS_FROM_BOTTOM 34.0f
#define WAVE_TEXT_DX      6.0f
#define WAVE_TEXT_DY      -22.0f
#define WAVE_TEXT_BOX     7.0f
#define WAVE_TEXT_SCALE   0.7f
#define ROOM_BOSSES_ALIVE 0x1a60            /* two ints, summed: the bosses alive */
#define SPRITE_SCALE      0xdc

static unsigned char g_wavebar[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_wavebar_state;

static void waves_draw(unsigned long b, unsigned long game, unsigned long manager) {
    if (!shim_tweaks_mod(MOD_WAVES)) return;
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!room || *(const int *) (room + ROOM_TYPE) != ROOM_BOSS_RUSH) return;
    int wave = *(const int *) (game + GAME_AMBUSH + AMBUSH_WAVE);
    int waves = *(const int *) (game + GAME_AMBUSH + AMBUSH_WAVES);
    {   /* PROBE, report builds: what the counter reads in Boss Rush (the owner saw none) */
        static int said, was_wave = -2, was_waves = -2;
        if (said < 20 && (wave != was_wave || waves != was_waves) && shim_report_active()) {
            said++;
            was_wave = wave;
            was_waves = waves;
            char line[96];
            snprintf(line, sizeof(line), "boss rush: wave %d of %d, started %d, bar picture %s", wave, waves,
                     *(const unsigned char *) (game + GAME_AMBUSH + 0x38),
                     g_wavebar_state > 0 ? "loaded" : g_wavebar_state < 0 ? "FAILED" : "not yet");
            report_line("mods", line);
        }
    }
    if (waves <= 0) return;

    const float *canvas = (const float *) (b + DESIGN_CANVAS);
    float adjust_x = (float) shim_modset_int(MS_WAVES_X), adjust_y = (float) shim_modset_int(MS_WAVES_Y);
    float x, y;
    switch (shim_modset_int(MS_WAVES_PRESET)) {
    case MS_WAVES_FOUND_HUD:
        x = WAVE_HUD_X + adjust_x;
        y = canvas[1] - WAVE_HUD_FROM_BOTTOM - adjust_y;
        break;
    case MS_WAVES_BOSS_BAR: {
        const int *alive = (const int *) (room + ROOM_BOSSES_ALIVE);
        /* the mod drops the sideways move when no boss is up, and so does this */
        x = alive[0] + alive[1] >= 1 ? canvas[0] * 0.5f - WAVE_BOSS_LEFT + adjust_x : canvas[0] * 0.5f;
        y = canvas[1] - WAVE_BOSS_FROM_BOTTOM - adjust_y;
        break;
    }
    default: {
        float push = 1.0f - *(const float *) (manager + MANAGER_HUD_OFFSET);
        float scale = (float) shim_modset_int(MS_WAVES_BAR_SCALE) / 100.0f;
        y = MAP_TOP + MAP_SHIFT_Y * push + WAVE_BELOW_MAP;
        x = canvas[0] - MAP_FROM_RIGHT + MAP_SHIFT_X * push - WAVE_LEFT_OF_MAP;
        x = wave_clear_pause(x, y, scale) + adjust_x;
        y -= adjust_y;
    }
    }

    float bar_scale = (float) shim_modset_int(MS_WAVES_BAR_SCALE) / 100.0f;
    if (shim_picture_ready(g_wavebar, &g_wavebar_state, "gfx/port_wavebar.anm2")) {
        float *scale = (float *) (g_wavebar + SPRITE_SCALE);
        scale[0] = scale[1] = bar_scale;
        shim_picture_draw(g_wavebar, "Idle", 0, x, y, 1.0f);
        scale[0] = scale[1] = 1.0f;
    }

    char text[16];
    snprintf(text, sizeof(text), "%d/%d", wave, waves);
    unsigned long font = manager + FONT_TERMINUS;
    float text_scale = WAVE_TEXT_SCALE * (float) shim_modset_int(MS_WAVES_TEXT_SCALE) / 100.0f;
    float middle = x + bar_scale * WAVE_TEXT_DX + WAVE_TEXT_BOX * 0.5f;
    float w = text_width(b, font, text) * text_scale;
    float r, g, bl;
    shim_modset_rgb(MS_WAVES_COLOUR, &r, &g, &bl);
    write_text(b, font, text, middle - w * 0.5f,
               y + (float) shim_modset_int(MS_WAVES_TEXT_NUDGE) + bar_scale * WAVE_TEXT_DY,
               text_scale, r, g, bl, 1.0f);
}

/* ------------------------------------------------------ planetarium chance */
#define PLANET_FRAME     8                  /* hudstats2.anm2 "Idle": the planetarium */
#define PLANET_ALPHA     0.5f
#define PLANET_TEXT_DX   16.0f
#define PLANET_TEXT_DY   1.0f
#define PLANET_DELTA_DX  46.0f
#define DELTA_FADE_FROM  2.9f               /* the mod's own numbers: alpha, less 0.01 a frame */
#define DELTA_FADE_STEP  0.01f

static unsigned char g_stats[SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_stats_state;

static int g_floor_known;
static int g_floor_key[3];                  /* stage, stage type, start room */
static float g_chance = -1.0f;              /* percent, for the floor */
static float g_delta;
static float g_delta_alpha;

#define STATS_SLOT_TWO   0x228
#define STATS_DUALITY    0x2f8
#define MGR_FOUND_HUD    0x55f02

static unsigned g_draws;                    /* our own draw count */
static unsigned g_stats_draw;               /* the draw the column was last seen at */
static float g_stats_x, g_next_y, g_text_lift;

/* the column is being drawn: its place, and how far down it reaches */
void shim_mods_stats_seen(unsigned long stats, unsigned long place) {
    if (!stats || !place) return;
    const float *p = (const float *) place;
    int two = *(const unsigned long *) (stats + STATS_SLOT_TWO) != 0;
    int rows = *(const unsigned char *) (stats + STATS_DUALITY) ? 7 : 8;
    g_stats_x = p[0];
    g_next_y = p[1] + (float) rows * (two ? 14.0f : 12.0f);
    g_text_lift = two ? -5.0f : -1.0f;
    g_stats_draw = g_draws;
    void shim_statsplus_seen(unsigned long stats, unsigned long place);
    shim_statsplus_seen(stats, place);
}

static void planet_floor(unsigned long b, unsigned long game) {
    int key[3] = { *(const int *) (game + GAME_STAGE), *(const int *) (game + GAME_STAGE_TYPE),
                   *(const int *) (game + GAME_ROOM_INDEX_COPY) };
    if (g_floor_known && !memcmp(key, g_floor_key, sizeof(key))) return;
    memcpy(g_floor_key, key, sizeof(key));
    g_floor_known = 1;

    float chance = ((float (*)(unsigned long)) (b + FN_PLANETARIUM))(game);
    int stage = key[0];
    if ((*(const unsigned char *) (game + GAME_ASCENT) & 1) && stage >= 1 && stage <= 6)
        chance = 0.0f;
    /*
     * No planetariums before they are unlocked: the mod asks whether Telescope
     * Lens is available, and its items.xml entry names achievement 406, "The
     * Planetarium" - a byte of the persistent data's achievements
     * (PersistentGameData::Unlocked 0x7027f8 reads the same byte).
     */
    unsigned long manager = manager_now(b);
    if (manager && !*(const unsigned char *) (manager + PERSISTENT_ACHIEVEMENTS + ACHIEVEMENT_PLANETARIUM))
        chance = 0.0f;
    /*
     * The mod's other zero: a run whose first floor has no treasure room. The
     * floor's rooms are Game+0x18, 0x100 each, Game+0x21510 of them; a room's
     * config is at +0x10 and its type at config+8, 4 the treasure room - the
     * fields the room entry at 0x5a56e0 reads to count treasure rooms visited.
     */
    if (stage == 1) {
        int rooms = *(const int *) (game + GAME_ROOM_COUNT), treasure = 0;
        for (int i = 0; i < rooms && i < ROOMS_MAX && !treasure; i++) {
            unsigned long config = *(const unsigned long *) (game + GAME_ROOMS + (unsigned long) i * ROOM_BYTES
                                                             + ROOM_CONFIG);
            if (config && *(const int *) (config + CONFIG_TYPE) == ROOM_TREASURE) treasure = 1;
        }
        if (!treasure) chance = 0.0f;
    }
    if (!(chance >= 0.0f)) chance = 0.0f;
    if (chance > 1.0f) chance = 1.0f;
    float percent = chance * 100.0f;

    if (g_chance >= 0.0f && percent != g_chance) {
        g_delta = percent - g_chance;
        g_delta_alpha = DELTA_FADE_FROM;
    }
    g_chance = percent;
    if (shim_report_active()) {
        char line[96];
        snprintf(line, sizeof(line), "planetarium chance %.1f%% on stage %d type %d",
                 (double) percent, key[0], key[1]);
        report_line("mods", line);
    }
}

static void planet_draw(unsigned long b, unsigned long game, unsigned long manager) {
    if (!shim_tweaks_mod(MOD_PLANETARIUM)) return;
    if (!*(const unsigned char *) (manager + MGR_FOUND_HUD)) return;
    planet_floor(b, game);
    if (g_chance < 0.0f || g_draws - g_stats_draw > 2) return;

    float x = g_stats_x, y = g_next_y;
    /* the planetarium is frame 8 of the game's "Idle": a hudstats2.anm2 with fewer has no icon to show */
    static int icon = -2;
    if (icon == -2) icon = shim_port_length("gfx/ui/hudstats2.anm2", "Idle") > PLANET_FRAME;
    if (icon && shim_picture_ready(g_stats, &g_stats_state, "gfx/ui/hudstats2.anm2"))
        shim_picture_draw(g_stats, "Idle", PLANET_FRAME, x, y, PLANET_ALPHA);

    char text[16];
    unsigned long font = manager + FONT_LUA_MINI;
    snprintf(text, sizeof(text), "%.1f%%", (double) g_chance);
    write_text(b, font, text, x + PLANET_TEXT_DX, y + PLANET_TEXT_DY + g_text_lift, 1.0f,
               1.0f, 1.0f, 1.0f, PLANET_ALPHA);

    if (g_delta_alpha > 0.0f) {
        float a = g_delta_alpha > 1.0f ? 1.0f : g_delta_alpha;
        snprintf(text, sizeof(text), "%+.1f%%", (double) g_delta);
        int up = g_delta > 0.0f;
        write_text(b, font, text, x + PLANET_DELTA_DX, y + PLANET_TEXT_DY + g_text_lift, 1.0f,
                   up ? 0.0f : 1.0f, up ? 1.0f : 0.0f, 0.0f, a * PLANET_ALPHA);
        g_delta_alpha -= DELTA_FADE_STEP;
    }
}

/* ----------------------------------------------------- unlocks: a report probe */
/*
 * item 21 (Apollyon) and item 22 (Greedier), read out of the image (23.09.2026) and
 * now watched on the device, because the rest can only be answered by the
 * owner's own save.
 *
 * WHAT THE IMAGE SAYS.
 *
 *   PersistentGameData = Manager+0x204d0: a dirty byte at +0, READ-ONLY at +1,
 *   638 achievement bytes from +0x28, 496 counters of 4 bytes from +0x2a8.
 *   TryUnlock 0x702644(data, id) does NOTHING while +1 is set - that one byte
 *   silences every unlock in the game, and 0x7027a0 is its setter ("Setting
 *   PersistentGameData ReadOnly to %s"). A run started from an entered seed
 *   sets it (0x88b5b0), and so do a few other places.
 *
 *   Completion marks: 0x702928(mark, character) reads the table copied to
 *   0xac3bf8 at startup (from 0x95c024, 0x90 bytes a character): 18 entries of
 *   { counter id, achievement id } each. A mark is written by 0x701e28(data,
 *   counter, 1) - the same read-only gate - and the loop at 0x7023c4 derives
 *   the unlocks from the marks on every load, over the 34 characters listed at
 *   0x957238.
 *
 *   APOLLYON is character 15, and his row is full: counters 214, 233, 252,
 *   271, 290, 309, 328, 347, 366, 385, 404, 438, 472 and achievements 416..427
 *   with 470, 471 and 489. He is in the list at 0x957238 (13th of 34), and the
 *   writer takes any character up to 40. Nothing in the image leaves him out.
 *
 *   GREEDIER is achievement 341 and the greed machine is counter 115
 *   (data+0x474). It is unlocked in the machine at 0x6a0e00 once the count
 *   reaches 499, and again on every load (0x700a38) when count % 1000 > 499 or
 *   achievement 249 is already up.
 *
 * So both work unless the data is read-only or the numbers are not what the
 * owner expects. Those two are what this probe reports.
 */
#define PGD_OFFSET        0x204d0
#define PGD_READ_ONLY     1
#define PGD_ACHIEVEMENTS  0x28
#define PGD_COUNTERS      0x2a8
#define COUNTER_DONATIONS 115
#define COUNTER_BEGGAR_MACHINE 20           /* the ordinary donation machine: 0x69de60 reads data+0x2f8, 0x69de70 adds 1 */
#define MGR_SAVE_SLOT     0x204c8           /* the slot the save came from (0x672990) */
#define ACH_GREEDIER      341
#define ACH_GREED_665     249               /* the next machine tier; it also opens Greedier */
#define APOLLYON          15

/*
 * WHY THE DATA IS READ-ONLY (23.09.2026, every caller of 0x7027a0 read). The
 * run start decides it, the way out of the run (0x88d7ac) clears it:
 *
 *   New run  0x88b144   forbidden || challenge != 0
 *   Daily    0x88b610   always
 *   Continue 0x88ba54   forbidden || victory laps > 0
 *   Rerun    0x88bf40   forbidden
 *   0x88c2e0            forbidden
 *   Victory lap 0x88c604  always
 *
 * forbidden = 0x88b5b0(game): the seed's fixed byte (Game+0x25d40 - raised by
 * a typed seed, 0x5e7264, by both challenge launchers 0x5db448 / 0x77dc74, by
 * the daily and by a start from launch arguments; it travels inside the seed
 * and so into a continued run), or an active special seed from the set at
 * 0xac1fd8 (25 of them, built at 0x56ff40), or the Rerun byte Game+0x1cf43c
 * (0x88bfcc, saved with the run and restored at 0x88bba8), or 0x672828 - a
 * stub that answers 0 on iOS, and in the port the spawner's run mark
 * (01.10.2026, loader.c platform_forbids_ours, spawner.c).
 *
 * Lifted for a moment and put back: the win and loss streaks (0x672b5c,
 * 0x672cbc - only when the run is not forbidden), a challenge's own reward
 * (0x894d14), the victory lap achievements 321 / 360 / 337 (0x5a0be8), the end
 * of a daily (0x88d310).
 */
#define GAME_CHALLENGE    0x1cf438
#define GAME_LAPS         0x1cf700
#define GAME_SEED_FIXED   0x25d40
#define GAME_RERUN        0x1cf43c
#define FN_RUN_FORBIDDEN  0x88b5b0          /* (game): a pure test, reads only */

/* Apollyon's row, as the table at 0x95c024 has it: the counter of each mark */
static const short APOLLYON_MARKS[] = {
    214, 233, 252, 271, 290, 309, 328, 347, 385, 366, 404, 438, 472
};
/* and what those marks unlock */
static const short APOLLYON_UNLOCKS[] = {
    416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 470, 471, 489
};

static unsigned long persistent_data(unsigned long b) {
    unsigned long manager = *(const unsigned long *) (b + MANAGER_PTR);
    return manager ? manager + PGD_OFFSET : 0;
}

static int achievement_up(unsigned long data, int id) {
    return *(const unsigned char *) (data + PGD_ACHIEVEMENTS + id) != 0;
}

static int counter_of(unsigned long data, int id) {
    return *(const int *) (data + PGD_COUNTERS + 4 * id);
}

/* the report, once and then again whenever any of it moves */
static void unlocks_probe(unsigned long b) {
    if (!shim_report_active()) return;
    unsigned long data = persistent_data(b);
    if (!data) return;
    int read_only = *(const unsigned char *) (data + PGD_READ_ONLY);
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    int challenge = game ? *(const int *) (game + GAME_CHALLENGE) : -1;
    int laps = game ? *(const int *) (game + GAME_LAPS) : -1;
    int fixed = game ? *(const unsigned char *) (game + GAME_SEED_FIXED) : -1;
    int rerun = game ? *(const unsigned char *) (game + GAME_RERUN) : -1;
    int forbidden = game ? ((int (*)(unsigned long)) (b + FN_RUN_FORBIDDEN))(game) & 1 : -1;
    int donations = counter_of(data, COUNTER_DONATIONS);
    int greedier = achievement_up(data, ACH_GREEDIER);
    /*
     * And (01.10.2026, plan item A6): the ordinary machine's count beside the
     * greed one, so the two are never mixed up; what the load rule 0x700a14
     * would do with this file (Greedier when count % 1000 > 499 or 249 is up);
     * the save slot; and whether the port counts the run out (spawner.c).
     */
    int beggar = counter_of(data, COUNTER_BEGGAR_MACHINE);
    int rule = donations % 1000 > 499 || achievement_up(data, ACH_GREED_665);
    int slot = *(const int *) (data - PGD_OFFSET + MGR_SAVE_SLOT);
    int shim_run_marked_now(void);
    int run_marked = game ? shim_run_marked_now() : -1;
    int marks = 0, unlocks = 0;
    for (unsigned i = 0; i < sizeof(APOLLYON_MARKS) / sizeof(APOLLYON_MARKS[0]); i++)
        if (counter_of(data, APOLLYON_MARKS[i])) marks++;
    for (unsigned i = 0; i < sizeof(APOLLYON_UNLOCKS) / sizeof(APOLLYON_UNLOCKS[0]); i++)
        if (achievement_up(data, APOLLYON_UNLOCKS[i])) unlocks++;

    static int said, was_read_only = -1, was_donations = -1, was_greedier = -1;
    static int was_marks = -1, was_unlocks = -1, was_challenge = -2, was_laps = -2, was_forbidden = -2;
    static int was_fixed = -2, was_rerun = -2;
    static int was_beggar = -1, was_slot = -2, was_run_marked = -2;
    if (read_only == was_read_only && donations == was_donations && greedier == was_greedier
        && marks == was_marks && unlocks == was_unlocks && challenge == was_challenge
        && laps == was_laps && forbidden == was_forbidden && fixed == was_fixed && rerun == was_rerun
        && beggar == was_beggar && slot == was_slot && run_marked == was_run_marked) return;
    if (said >= 24) return;
    said++;
    was_read_only = read_only; was_donations = donations; was_greedier = greedier;
    was_marks = marks; was_unlocks = unlocks;
    was_challenge = challenge; was_laps = laps; was_forbidden = forbidden;
    was_fixed = fixed; was_rerun = rerun;
    was_beggar = beggar; was_slot = slot; was_run_marked = run_marked;

    char line[420];
    int n = snprintf(line, sizeof(line),
                     "slot %d, read-only %d (challenge %d, laps %d, fixed seed %d, rerun %d, spawner mark %d, "
                     "forbidden %d) | greed machine %d, Greedier %s (249 %s, load rule %s), donation machine %d | "
                     "Apollyon marks %d of %d, unlocks %d of %d |",
                     slot, read_only, challenge, laps, fixed, rerun, run_marked, forbidden, donations,
                     greedier ? "up" : "down", achievement_up(data, ACH_GREED_665) ? "up" : "down",
                     rule ? "grants" : "no", beggar, marks,
                     (int) (sizeof(APOLLYON_MARKS) / sizeof(APOLLYON_MARKS[0])), unlocks,
                     (int) (sizeof(APOLLYON_UNLOCKS) / sizeof(APOLLYON_UNLOCKS[0])));
    for (unsigned i = 0; i < sizeof(APOLLYON_MARKS) / sizeof(APOLLYON_MARKS[0])
                         && n < (int) sizeof(line) - 6; i++)
        n += snprintf(line + n, sizeof(line) - (size_t) n, " %d", counter_of(data, APOLLYON_MARKS[i]));
    report_line("unlocks", line);
}

/* ------------------------------------------------------- specialist dance */
/*
 * "The Specialist for Good Items". A good item on a pedestal in the room - one
 * of quality 4, which is also what the mod's short list of favourites comes to
 * by default - and every player puts on a dancing costume while the Specialist
 * theme plays. Since the mod's 1.5.1 ("Taint to the Party") every tainted
 * character has a dance of its own and the tainted have remixes of their own
 * (Tainted Cain the crackhouse cut, Tainted Eden a glitched one, the rest the
 * "b" remix) - heard only with "always play Default Music" off, see
 * dance_track_of and modset.c. No good item left, and
 * the costumes come off and the room's own music comes back. Under Curse of
 * the Blind nothing starts and nothing stops, as in the mod.
 *
 * THE COSTUMES AND THE MUSIC ARE THE GAME'S OWN KIND. Both tables are full:
 * the null-costume table is sized to 132 with two gaps, and an id past it is
 * written through without a bounds check; music.xml fills ids 1..118 of a table
 * of 119. So nothing is added to the game's files. Instead the game's own
 * loaders read two small files of ours in the mode they have for mods, which
 * appends in file order (tools/mod_specialist.py writes them):
 *
 *   0x73a790(ItemConfig, path, mod)   costumes: ids from the table's size on
 *                                     (0x73ad64: id = (end - begin) / 8, then
 *                                     push_back; the mod pointer is never
 *                                     read for type "none")
 *   0x75fc64(MusicManager, path, 1)   tracks: the same rule (0x76087c: id =
 *                                     size / 0x70, then push_back)
 *
 * once, after the game's loader has read its last table (WHEN, below). Our
 * first ids are the sizes measured right before our load - 132 and 119 on
 * this build, but whatever they are. Then:
 *
 *   0x73f864(ItemConfig, id)          the null item of an id, bounds-checked
 *   0x4ff060(player, item, 0)         put a costume on (a copy replaces a copy)
 *   0x4ff5c4(player, item)            take it off
 *   0x7613c0(MusicManager, id)        play a track at the music volume
 *   [MusicManager+0x3c8]              the track playing now
 *   0x59a070(room)                    the room's own music again
 *
 * WHAT THE [specialist] SECTION CHANGES (modset.c): the quality that starts
 * it, quality ignored, the lists of items that always and never do, the music,
 * the default track, whether it always plays, Tainted Eden's reduced flashing,
 * and dancing under Curse of the Blind - the mod's own options, its defaults.
 *
 * WHICH ITEMS COUNT. The room's entities (room+0x1950: array +0x78, count
 * +0x84): type 5, variant 100, an item in it, alive (+0x1c2). A pedestal the
 * game shows as a question mark does not count - the alternate floors' blind
 * treasure pairs carry that as a byte of their own (+0x562), which is what the
 * mod tries to guess from positions. Quality is the item config's +0xc8.
 */
#define MOD_DANCE        0

#define MGR_ITEM_CONFIG  0x55f30
#define MGR_MUSIC        0x55a60
#define CONFIG_NULLS     0x30               /* begin, end of the null item table */
#define MUSIC_TRACKS     0x3d8              /* begin, end, 0x70 a track */
#define MUSIC_NOW        0x3c8
#define FN_LOAD_COSTUMES 0x73a790
#define FN_LOAD_MUSIC    0x75fc64
#define FN_NULL_ITEM     0x73f864
#define FN_COLLECTIBLE   0x73f7f4
#define FN_ADD_COSTUME   0x4ff060
#define FN_REMOVE_COSTUME 0x4ff5c4
#define FN_PLAY_MUSIC    0x7613c0
#define FN_ROOM_MUSIC    0x59a070
#define FN_CURSES        0x6d02c4
#define CURSE_BLIND      0x40
#define GAME_PLAYER_LIST 0x25c40
#define PLAYER_TYPE      0x1738
#define PLAYER_OWNED     0x1ab8
#define ROOM_ENTITIES    0x1950
#define LIST_ARRAY       0x78
#define LIST_COUNT       0x84
#define ENTITY_TYPE      0x38
#define ENTITY_ALIVE     0x1c2
#define PICKUP_BLIND     0x562
#define ITEM_QUALITY     0xc8
#define ITEM_BINKY       438

/*
 * The order of tools/mod_specialist.py COSTUMES, then Binky. The first 22 keep
 * the ids they had before the mod's tainted update (1.5.1, "Taint to the
 * Party"); the tainted dances follow in the mod's costumes2.xml order.
 */
enum {
    C_ISAAC, C_MAGDALENE, C_CAIN, C_JUDAS, C_DARK_JUDAS, C_XXX, C_EVE, C_SAMSON,
    C_AZAZEL, C_LAZARUS, C_LAZARUS2, C_EDEN, C_LOST, C_LILITH, C_KEEPER, C_APOLLYON,
    C_FORGOR_BONE, C_FORGOR_SOUL, C_BETHANY, C_JACOB, C_ESAU, C_T_CAIN,
    C_T_APOLLYON, C_T_AZAZEL, C_T_BETHANY, C_T_BLUEBABY, C_T_EDEN, C_T_EVE,
    C_T_FORGOR_BONE, C_T_FORGOR_SOUL, C_T_ISAAC, C_T_JACOB, C_T_JACOB2, C_T_JUDAS,
    C_T_KEEPER, C_T_LAZARUS, C_T_LAZARUS2, C_T_LILITH, C_T_LOST, C_T_MAGDALENE,
    C_T_SAMSON,
    C_BINKY,
    C_COUNT
};

/* the order of tools/mod_specialist.py TRACKS, the mod's music.xml */
enum { T_SPECIALIST, T_SPECIALIST_B, T_PAINCIALIST, T_GLITCHALIST, T_COUNT };

/*
 * Player type -> costume: the mod's danceCostumes table (main.lua), entry for
 * entry. Every type 0..40 has its own dance now; the mod's DEFAULT (Isaac) is
 * left for a type past the table.
 */
static const signed char COSTUME_OF[41] = {
    C_ISAAC, C_MAGDALENE, C_CAIN, C_JUDAS, C_XXX, C_EVE, C_SAMSON, C_AZAZEL,
    C_LAZARUS, C_EDEN, C_LOST, C_LAZARUS2, C_DARK_JUDAS, C_LILITH, C_KEEPER, C_APOLLYON,
    C_FORGOR_BONE, C_FORGOR_SOUL, C_BETHANY, C_JACOB, C_ESAU,
    /* 21.. the tainted: PLAYER_ISAAC_B .. PLAYER_THESOUL_B */
    C_T_ISAAC, C_T_MAGDALENE, C_T_CAIN, C_T_JUDAS, C_T_BLUEBABY, C_T_EVE, C_T_SAMSON, C_T_AZAZEL,
    C_T_LAZARUS, C_T_EDEN, C_T_LOST, C_T_LILITH, C_T_KEEPER, C_T_APOLLYON, C_T_FORGOR_BONE,
    C_T_BETHANY, C_T_JACOB, C_T_LAZARUS2, C_T_JACOB2, C_T_FORGOR_SOUL,
};

#define TYPE_EDEN          9
#define TYPE_SOUL          17
#define TYPE_ESAU          20
#define TYPE_TAINTED_FIRST 21
#define TYPE_TAINTED_CAIN  23
#define TYPE_TAINTED_EDEN  30
#define TYPE_TAINTED_LAST  40
#define TYPE_TAINTED_SOUL  40

/*
 * The costume a player of this type dances: the mod's DoCostume. Tainted Eden
 * with ReduceFlashing dances regular Eden's dance instead of the flickering one.
 */
static int dance_costume_of(int type) {
    if (type < 0 || type >= (int) sizeof(COSTUME_OF)) return C_ISAAC;
    if (type == TYPE_TAINTED_EDEN && shim_modset_int(MS_DANCE_REDUCE_FLASHING)) return COSTUME_OF[TYPE_EDEN];
    return COSTUME_OF[type];
}

/*
 * The track a player of this type asks for, or -1: the mod's DoCostume music.
 * The Forgotten's Soul, Esau and Tainted Soul ask for none (their other half
 * does). With "always play Default Music" every one gets the default track;
 * otherwise Tainted Cain the sad cut, Tainted Eden the glitched one, the other
 * tainted (21..40) the tainted remix, and the rest the default. The mod's
 * custom tracks (s_<character> in its music.xml) have no place here: the port
 * reads no Lua and no extra music.xml - nothing to choose from.
 */
static int dance_track_of(int type) {
    if (type == TYPE_SOUL || type == TYPE_ESAU || type == TYPE_TAINTED_SOUL) return -1;
    int def = shim_modset_int(MS_DANCE_DEFAULT_MUSIC);
    if (def < 0 || def >= T_COUNT) def = T_SPECIALIST;
    if (shim_modset_int(MS_DANCE_ALWAYS_DEFAULT)) return def;
    if (type == TYPE_TAINTED_CAIN) return T_PAINCIALIST;
    if (type == TYPE_TAINTED_EDEN) return T_GLITCHALIST;
    if (type >= TYPE_TAINTED_FIRST && type <= TYPE_TAINTED_LAST) return T_SPECIALIST_B;
    return def;
}

#define DANCERS 8

static int g_dance_state;                   /* 0 not yet, 1 loaded, -1 refused */
static int g_costume_first, g_track_first;  /* measured at the load, not assumed */
static unsigned long g_our_item[C_COUNT];   /* the null items of our costumes, once loaded */
static int g_dancing, g_music_ours;
static unsigned long g_dancer[DANCERS];     /* the players we dressed */
static unsigned long g_dancer_item[DANCERS][2];
static unsigned long g_dance_room;

const char *shim_pack_root(void);

static void dance_bring_in(AAssetManager *mgr, const char *dir) {
    char asset_dir[96], folder[96];
    snprintf(asset_dir, sizeof(asset_dir), "port_specialist%s%s", *dir ? "/" : "", dir);
    snprintf(folder, sizeof(folder), "gfx/port_specialist%s%s", *dir ? "/" : "", dir);
    AAssetDir *d = AAssetManager_openDir(mgr, asset_dir);
    if (!d) return;
    const char *leaf;
    int n = 0;
    while ((leaf = AAssetDir_getNextFileName(d)) != NULL) {
        char name[160];
        snprintf(name, sizeof(name), "%s/%s", asset_dir, leaf);
        shim_asset_bring_in(mgr, name, folder, leaf);
        n++;
    }
    AAssetDir_close(d);
    LOG("mods: %d files of %s are in the resource table", n, asset_dir);
}

static size_t table_count(unsigned long at, size_t size) {
    const unsigned long *v = (const unsigned long *) at;
    return v[1] >= v[0] ? (size_t) ((v[1] - v[0]) / size) : 0;
}

/*
 * WHEN THE GAME HAS READ ITS TABLES (plan item 4.10.5, 02.10.2026). The load used
 * to wait for the costume table to stand at exactly 132 and the music table at
 * 119: one number meant both "the game has read its files" and "our ids start
 * here", so a table of any other size switched the dance off for good (bigger)
 * or kept it waiting forever without a word (smaller).
 *
 * Now the two are apart. WHEN is the game's own order: the Repentance loader
 * 0x66eaa0 (engine setup 0x885fb4 -> 0x66e6d4 -> 0x66e88c, once) reads
 * music.xml first (0x66ead0), costumes2.xml in the middle (0x66ed80) and
 * bossportraits.xml as its last table (0x66f2f8); after it come only fonts
 * (0x8bd4f4), 0x66de20 (more fonts) and the DLC menu sprites (0x57a7d8) -
 * nothing that touches the costume or music table. So the ask for
 * repentance-resources/.../bossportraits.xml, seen at the head of
 * shim_map_path (uikit.c) before any mod swaps the path, is the marker. The
 * setup is called straight from the shell (0x5180 blr, the edition table
 * 0x59ac) on the thread that runs the frames, so the first between-frames
 * after the marker comes after the setup has returned.
 *
 * WHERE OUR IDS START is measured right before our own load: whatever size
 * the game's tables have then is where the mod-mode loaders put us.
 */
static volatile int g_marker_asks;          /* written by the file-call thread, read between frames */
static volatile int g_marker_tid;

void shim_mods_path_asked(const char *p) {
    static const char LEAF[] = "/bossportraits.xml";
    if (!p) return;
    size_t n = strlen(p), k = sizeof(LEAF) - 1;
    if (n < k || strcasecmp(p + n - k, LEAF) || !strstr(p, "repentance-resources")) return;
    if (__atomic_load_n(&g_marker_asks, __ATOMIC_ACQUIRE) == 0) g_marker_tid = gettid();
    __atomic_fetch_add(&g_marker_asks, 1, __ATOMIC_RELEASE);
}

/* once, after the marker: the game's loaders read our two files in their mod mode */
static int dance_load(unsigned long b, unsigned long manager, unsigned long game) {
    if (g_dance_state) return g_dance_state > 0;
    if (!__atomic_load_n(&g_marker_asks, __ATOMIC_ACQUIRE)) {
#if ISAAC_DIAG
        /* a run cannot be up before the setup: the marker was missed, say so once */
        static int said;
        if (game && !said && shim_report_active()) {
            said = 1;
            report_line("dance", "a run is up and bossportraits.xml was never asked - the dance waits");
        }
#else
        (void) game;
#endif
        return 0;
    }
    unsigned long cfg = manager + MGR_ITEM_CONFIG, music = manager + MGR_MUSIC;
    size_t nulls_before = table_count(cfg + CONFIG_NULLS, 8);
    size_t tracks_before = table_count(music + MUSIC_TRACKS, 0x70);
    char path[1024];
    /*
     * The mod the loader thinks it reads for. For type "none" it is only asked
     * whether it is there (0x73ad58); the other types look ids up in two maps
     * of it, at +0x108 and +0x120 (0x73ad3c, 0x73b420). Our file holds only
     * "none", but the stand-in carries two valid EMPTY libc++ maps all the same
     * - begin pointing at the end node, no root, size 0 - so no path through
     * the loader can find garbage there.
     */
    static unsigned long mod_stand_in[0x200 / sizeof(unsigned long)];
    for (unsigned at = 0x108; at <= 0x120; at += 0x18) {
        unsigned long *map = (unsigned long *) ((char *) mod_stand_in + at);
        map[0] = (unsigned long) &map[1];
        map[1] = 0;
        map[2] = 0;
    }
    /*
     * RELATIVE paths, as the game's own "costumes.xml" is handed over: the
     * loader opens through the resource system (0x8dfb00 -> 0x8a8d08), which
     * puts the root in front itself. Handed the whole path it found nothing,
     * opened nothing and said nothing - the owner's first report showed the
     * tables untouched, 132 and 119 (18.09.2026). The description font went
     * through exactly this once before (eid.c, font_open).
     */
    snprintf(path, sizeof(path), "gfx/port_specialist/costumes.xml");
    ((int (*)(unsigned long, const char *, void *)) (b + FN_LOAD_COSTUMES))(cfg, path, mod_stand_in);
    snprintf(path, sizeof(path), "gfx/port_specialist/music.xml");
    ((int (*)(unsigned long, const char *, int)) (b + FN_LOAD_MUSIC))(music, path, 1);

    size_t nulls = table_count(cfg + CONFIG_NULLS, 8);
    size_t tracks = table_count(music + MUSIC_TRACKS, 0x70);
    g_costume_first = (int) nulls_before;
    g_track_first = (int) tracks_before;
    g_dance_state = (nulls == nulls_before + C_COUNT && tracks == tracks_before + T_COUNT) ? 1 : -1;
    if (g_dance_state > 0) {
        /* ours, by pointer: the sweep below knows our costumes without trusting an id */
        unsigned long (*null_item)(unsigned long, int) = (unsigned long (*)(unsigned long, int)) (b + FN_NULL_ITEM);
        for (int k = 0; k < C_COUNT; k++) g_our_item[k] = null_item(cfg, g_costume_first + k);
    }
    LOG("mods: the dance costumes and tracks are %s (costumes %zu -> %zu, tracks %zu -> %zu)",
        g_dance_state > 0 ? "loaded" : "NOT loaded", nulls_before, nulls, tracks_before, tracks);
    if (shim_report_active()) {
        char line[160];
        snprintf(line, sizeof(line), "dance tables: costumes %zu -> %zu, tracks %zu -> %zu, %s",
                 nulls_before, nulls, tracks_before, tracks, g_dance_state > 0 ? "loaded" : "refused");
        report_line("mods", line);
#if ISAAC_DIAG
        snprintf(line, sizeof(line), "marker: bossportraits.xml asked %d times, first on thread %d; "
                 "the load on thread %d, ids from %d and %d",
                 __atomic_load_n(&g_marker_asks, __ATOMIC_ACQUIRE), g_marker_tid, gettid(),
                 g_costume_first, g_track_first);
        report_line("dance", line);
#endif
    }
    return g_dance_state > 0;
}

/* is there a good item on a pedestal the player can see */
static int good_item_here(unsigned long b, unsigned long manager, unsigned long room) {
    unsigned long list = room + ROOM_ENTITIES;
    const unsigned long *items = *(const unsigned long *const *) (list + LIST_ARRAY);
    unsigned n = *(const unsigned *) (list + LIST_COUNT);
    if (!items) return 0;
    unsigned long cfg = manager + MGR_ITEM_CONFIG;
    for (unsigned i = 0; i < n; i++) {
        unsigned long e = items[i];
        if (!e || !*(const unsigned char *) (e + ENTITY_ALIVE)) continue;
        const int *tvs = (const int *) (e + ENTITY_TYPE);
        if (tvs[0] != 5 || tvs[1] != 100 || tvs[2] <= 0) continue;
        if (*(const unsigned char *) (e + PICKUP_BLIND)) continue;
        /* the mod's order: a listed favourite always, a banned one never, then quality */
        if (shim_modset_listed(MS_DANCE_NEVER, tvs[2])) continue;
        if (shim_modset_listed(MS_DANCE_ALWAYS, tvs[2])) return 1;
        if (shim_modset_int(MS_DANCE_IGNORE_QUALITY)) continue;
        unsigned long item = ((unsigned long (*)(unsigned long, int)) (b + FN_COLLECTIBLE))(cfg, tvs[2]);
        if (item && *(const int *) (item + ITEM_QUALITY) >= shim_modset_int(MS_DANCE_QUALITY)) return 1;
    }
    return 0;
}

static int players_of(unsigned long game, const unsigned long **begin) {
    const unsigned long *list = (const unsigned long *) (game + GAME_PLAYER_LIST);
    *begin = (const unsigned long *) list[2];
    if (!list[2] || list[3] < list[2]) return 0;
    int n = (int) ((list[3] - list[2]) / 8);
    return n > DANCERS ? DANCERS : n;
}

/*
 * IS THIS PLAYER STILL IN THE RUN (13.10.2026: "exit during the dance and it
 * crashes"). Every way out of a run ends it through 0x672830, which for a run
 * (state 2) calls the Game's end 0x88d7ac, and that empties the player manager
 * Game+0x25c40 with 0x5671e8: each player is taken out of the vector
 * (+0x10..+0x18) and out of the room's list (0x764298), then deleted through
 * its vtable (+8). Exit to the menu, save & quit and the game over do it right
 * before the state goes to 3 (0x6716dc, 0x6716f8); a restart (R, the pad's
 * combination, the held pause) does it at the head of the new run's start
 * (0x671394), where the state is written 2 again (0x6713e4) - so for the frame
 * code the state never leaves 2 on a restart. The dancers we kept were
 * walked after that (costume_frame) before the run was asked about: a deleted
 * player's costume vector, its freed sprites, and Play (0x89e82c) on them.
 *
 * So a player we dressed is touched only while the vector holds it - the
 * game's own word on who is alive, asked here on the frame thread, between
 * frames, with no game code in between. The whole vector, not the first
 * DANCERS: a dancer can move down it when someone before him leaves.
 */
#define PLAYERS_SANE 64

static int in_run(unsigned long game, unsigned long p) {
    const unsigned long *list = (const unsigned long *) (game + GAME_PLAYER_LIST);
    const unsigned long *v = (const unsigned long *) list[2];
    if (!p || !v || list[3] < list[2]) return 0;
    unsigned long n = (list[3] - list[2]) / 8;
    if (n > PLAYERS_SANE) n = PLAYERS_SANE;
    for (unsigned long k = 0; k < n; k++) if (v[k] == p) return 1;
    return 0;
}

#define ENTITY_SPRITE    0x50

/*
 * THE COSTUME STANDS STILL WHEN PUT ON MID-WALK (the owner, 18.09.2026: with
 * the stick held on entering the room the player froze in the dance's first
 * pose until the stick was let go). Measured on the fifth pass (19.09.2026):
 * the player's own walk loop was playing the whole time - the freeze is the
 * costume's. The dance lives in the costume's HeadDown (449 frames, a loop,
 * the head layer); its Walk* are one blank frame each. Every frame the game
 * plays the head animation of the facing on every costume (0x2fcdd0 picks
 * HeadUp/HeadLeft/HeadRight/HeadDown by direction), and the costume has no
 * HeadUp: Play with force 0 (0x89e82c main, 0x89ec38 overlay) finds nothing
 * and leaves the costume as the change left it - HeadDown at frame 0, not
 * playing. Facing down again is what plays it: the same name, a loop, and
 * stopped is the one case force 0 restarts. So while the dance is on, every
 * frame, a dancer's own costume sprites whose named loop stands still are
 * played again by that same rule - nothing else is touched, and a costume
 * that dances already is left alone (Play does nothing while it plays).
 *
 * Sprite (0x158 bytes at the head of a 0x178 costume record; the player's
 * are at +0x1588..+0x1590, the null item pointer at record +0x158): two
 * animation states, the main at +0x30 and the overlay at +0x60, each with
 * the animation record at +8 (name std::string at +0, Loop byte at +0x48),
 * the frame float at +0x20 and the playing byte at +0x24.
 */
#define PLAYER_COSTUMES     0x1588          /* begin, end at +8 */
#define COSTUME_RECORD      0x178
#define COSTUME_ITEM        0x158
#define COSTUME_FLAGS       0x160           /* bytes to +0x177, printed by the probe */
#define PLAYER_LAYER_TABLE  0x15a0          /* 15 entries of 0x10: costume index, kind byte at +0xc */
#define PLAYER_LAYERS       15
#define STATE_MAIN          0x30
#define STATE_OVERLAY       0x60
#define STATE_ANIMATION     0x08
#define STATE_FRAME         0x20
#define STATE_PLAYING       0x24
#define ANIMATION_LOOP      0x48
#define FN_SPRITE_PLAY      0x89e82c        /* (sprite, name, force) */
#define FN_SPRITE_PLAY_OVER 0x89ec38        /* (sprite, name, force) on the overlay state */

static const char *animation_name(const unsigned char *anim) {
    if (!anim) return "(none)";
    return (anim[23] & 0x80) ? *(const char *const *) anim : (const char *) anim;
}

/* the state's animation name, loop flag, playing byte and frame, in one piece */
static int state_text(char *out, size_t n, unsigned long sprite, unsigned long state) {
    const unsigned char *anim = *(const unsigned char *const *) (sprite + state + STATE_ANIMATION);
    return snprintf(out, n, "%s loop %d playing %d frame %.1f", animation_name(anim),
                    anim ? anim[ANIMATION_LOOP] : -1, *(const unsigned char *) (sprite + state + STATE_PLAYING),
                    *(const float *) (sprite + state + STATE_FRAME));
}

/* PROBE, report builds: every costume of the player, and the layer table */
static void costume_probe(unsigned long player, const char *when) {
    if (!shim_report_active()) return;
    static int said;
    if (said >= 40) return;
    unsigned long begin = *(const unsigned long *) (player + PLAYER_COSTUMES);
    unsigned long end = *(const unsigned long *) (player + PLAYER_COSTUMES + 8);
    char line[400];
    int n = snprintf(line, sizeof(line), "%s: body main ", when);
    n += state_text(line + n, sizeof(line) - (size_t) n, player + ENTITY_SPRITE, STATE_MAIN);
    n += snprintf(line + n, sizeof(line) - (size_t) n, "; overlay ");
    state_text(line + n, sizeof(line) - (size_t) n, player + ENTITY_SPRITE, STATE_OVERLAY);
    report_line("dance", line);
    said++;
    n = snprintf(line, sizeof(line), "%s: layers", when);
    for (int i = 0; i < PLAYER_LAYERS && n < (int) sizeof(line) - 12; i++) {
        const unsigned char *entry = (const unsigned char *) (player + PLAYER_LAYER_TABLE + 0x10 * i);
        n += snprintf(line + n, sizeof(line) - (size_t) n, " %d:%d/%d", i, *(const int *) entry, entry[0xc]);
    }
    report_line("dance", line);
    said++;
    int index = 0;
    for (unsigned long c = begin; c && end >= c + COSTUME_RECORD && said < 40; c += COSTUME_RECORD, index++) {
        unsigned long item = *(const unsigned long *) (c + COSTUME_ITEM);
        n = snprintf(line, sizeof(line), "%s: costume %d item %d flags", when, index,
                     item ? *(const int *) (item + 4) : -1);
        for (int i = 0; i < 0x18 && n < (int) sizeof(line) - 4; i++)
            n += snprintf(line + n, sizeof(line) - (size_t) n, "%s%02x", i % 4 ? "" : " ",
                          ((const unsigned char *) c)[COSTUME_FLAGS + i]);
        n += snprintf(line + n, sizeof(line) - (size_t) n, " main ");
        n += state_text(line + n, sizeof(line) - (size_t) n, c, STATE_MAIN);
        n += snprintf(line + n, sizeof(line) - (size_t) n, "; overlay ");
        state_text(line + n, sizeof(line) - (size_t) n, c, STATE_OVERLAY);
        report_line("dance", line);
        said++;
    }
}

static int g_probe_left;                    /* frames of the probe after a dance starts */

/* a named loop standing still in this state is played again, by the game's own rule */
static int play_if_standing(unsigned long b, unsigned long sprite, unsigned long state, unsigned long fn) {
    const unsigned char *anim = *(const unsigned char *const *) (sprite + state + STATE_ANIMATION);
    if (!anim || !anim[ANIMATION_LOOP] || *(const unsigned char *) (sprite + state + STATE_PLAYING)) return 0;
    ((void (*)(unsigned long, const char *, int)) (b + fn))(sprite, animation_name(anim), 0);
    return 1;
}

static void costume_frame(unsigned long b) {
    static int said;
    for (int i = 0; i < DANCERS; i++) {
        unsigned long p = g_dancer[i];
        if (!p) continue;
        unsigned long begin = *(const unsigned long *) (p + PLAYER_COSTUMES);
        unsigned long end = *(const unsigned long *) (p + PLAYER_COSTUMES + 8);
        for (unsigned long c = begin; c && end >= c + COSTUME_RECORD; c += COSTUME_RECORD) {
            unsigned long item = *(const unsigned long *) (c + COSTUME_ITEM);
            if (!item || (item != g_dancer_item[i][0] && item != g_dancer_item[i][1])) continue;
            int main_played = play_if_standing(b, c, STATE_MAIN, FN_SPRITE_PLAY);
            int over_played = play_if_standing(b, c, STATE_OVERLAY, FN_SPRITE_PLAY_OVER);
            if ((main_played || over_played) && shim_report_active() && said < 20) {
                said++;
                char line[400];
                int n = snprintf(line, sizeof(line), "a costume's loop stood still: played the %s again; main ",
                                 main_played && over_played ? "main and overlay" : main_played ? "main" : "overlay");
                n += state_text(line + n, sizeof(line) - (size_t) n, c, STATE_MAIN);
                n += snprintf(line + n, sizeof(line) - (size_t) n, "; overlay ");
                state_text(line + n, sizeof(line) - (size_t) n, c, STATE_OVERLAY);
                report_line("dance", line);
            }
        }
    }
}

/*
 * OUR COSTUME IS WORN ONLY WHILE WE DANCE IT (plan item 4.10.5, the save). The run
 * save keeps every costume a player wears: StoreGameState 0x54fac4 writes the
 * count to gsp+0x218 (0x550238) and the first 8 bytes of each costume's item
 * - type, id - to the vector at gsp+0x220 (0x550254..0x550274), for every
 * player of Game+0x25c40 and the second form at player+0x2588 (the save loop
 * 0x569b68..0x569c30). RestoreGameState 0x5252c8 puts each back
 * (0x52554c..0x5255e4): type 0, a null item, through 0x73f864 - so a dance
 * costume saved mid-dance (exit to the menu, the app sent away) comes back on
 * Continue with nobody to take it off. Under Curse of the Blind, with the
 * dance switched off, or on a form that is not in the list, it stayed for
 * good. Every frame of a run, a costume of ours on a player we are not
 * dancing right now is taken off; when the room still holds a good item the
 * dance starts again on the same pass, as it would have.
 */
#define PLAYER_OTHER_FORM 0x2588            /* Tainted Lazarus's other half, saved as its own record */
#define ITEM_ID           4

static int ours_worn_here(unsigned long p, unsigned long item) {
    if (!g_dancing) return 0;
    for (int i = 0; i < DANCERS; i++)
        if (g_dancer[i] == p && (g_dancer_item[i][0] == item || g_dancer_item[i][1] == item)) return 1;
    return 0;
}

/* the first costume of ours on this player that is not being danced, or 0 */
static unsigned long stray_costume(unsigned long p) {
    unsigned long begin = *(const unsigned long *) (p + PLAYER_COSTUMES);
    unsigned long end = *(const unsigned long *) (p + PLAYER_COSTUMES + 8);
    for (unsigned long c = begin; c && end >= c + COSTUME_RECORD; c += COSTUME_RECORD) {
        unsigned long item = *(const unsigned long *) (c + COSTUME_ITEM);
        if (!item) continue;
        int k = *(const int *) (item + ITEM_ID) - g_costume_first;
        if (k < 0 || k >= C_COUNT || g_our_item[k] != item) continue;
        if (!ours_worn_here(p, item)) return item;
    }
    return 0;
}

static void dance_sweep(unsigned long b, unsigned long game) {
    const unsigned long *players;
    int n = players_of(game, &players);
    for (int i = 0; i < n; i++) {
        unsigned long forms[2] = { players[i], players[i] ? *(const unsigned long *) (players[i] + PLAYER_OTHER_FORM) : 0 };
        for (int f = 0; f < 2; f++) {
            unsigned long p = forms[f];
            if (!p) continue;
            /* the remove rebuilds the list: look again from the start, a few at most */
            for (int round = 0; round < 4; round++) {
                unsigned long item = stray_costume(p);
                if (!item) break;
                ((void (*)(unsigned long, unsigned long)) (b + FN_REMOVE_COSTUME))(p, item);
#if ISAAC_DIAG
                static int said;
                if (shim_report_active() && said < 8) {
                    said++;
                    char line[160];
                    snprintf(line, sizeof(line), "a dance costume came back with the run: costume %d taken off "
                             "player %d%s", *(const int *) (item + ITEM_ID), i, f ? " (other form)" : "");
                    report_line("dance", line);
                }
#endif
            }
        }
    }
}

static void dance_start(unsigned long b, unsigned long game, unsigned long manager) {
    if (shim_report_active()) report_line("mods", "the dance starts");
    unsigned long cfg = manager + MGR_ITEM_CONFIG, music = manager + MGR_MUSIC;
    unsigned long (*null_item)(unsigned long, int) = (unsigned long (*)(unsigned long, int)) (b + FN_NULL_ITEM);
    void (*add)(unsigned long, unsigned long, int) = (void (*)(unsigned long, unsigned long, int)) (b + FN_ADD_COSTUME);
    const unsigned long *players;
    int n = players_of(game, &players);
    int track = -1;
    for (int i = 0; i < DANCERS; i++) { g_dancer[i] = 0; g_dancer_item[i][0] = g_dancer_item[i][1] = 0; }
    for (int i = 0; i < n; i++) {
        unsigned long p = players[i];
        if (!p) continue;
        g_dancer[i] = p;
        int type = *(const int *) (p + PLAYER_TYPE);
        int costume = dance_costume_of(type);
        unsigned long item = null_item(cfg, g_costume_first + costume);
        if (item) add(p, item, 0);
        g_dancer_item[i][0] = item;
        const int *owned = *(const int *const *) (p + PLAYER_OWNED);
        if (owned && owned[ITEM_BINKY] > 0) {
            unsigned long binky = null_item(cfg, g_costume_first + C_BINKY);
            if (binky) add(p, binky, 0);
            g_dancer_item[i][1] = binky;
        }
        /* as in the mod, the last player who asks for a track has it played */
        int asked = dance_track_of(type);
        if (asked >= 0) track = g_track_first + asked;
#if ISAAC_DIAG
        if (shim_report_active()) {
            char line[160];
            snprintf(line, sizeof(line), "player %d of type %d dances costume %d (id %d), asks for track %d",
                     i, type, costume, g_costume_first + costume, asked < 0 ? -1 : g_track_first + asked);
            report_line("dance", line);
        }
#endif
        costume_probe(p, "put on");
    }
    g_probe_left = 90;
    if (track >= 0 && shim_modset_int(MS_DANCE_MUSIC) && *(const int *) (music + MUSIC_NOW) != track) {
        ((void (*)(unsigned long, int)) (b + FN_PLAY_MUSIC))(music, track);
        g_music_ours = 1;
    }
    g_dancing = 1;
}

static void dance_stop(unsigned long b, unsigned long game, int restore_music) {
    for (int i = 0; i < DANCERS; i++) {
        unsigned long p = g_dancer[i];
        g_dancer[i] = 0;
        if (!p) continue;
        int still = in_run(game, p);        /* only a player who is still in the run */
        for (int k = 0; k < 2 && still; k++)
            if (g_dancer_item[i][k])
                ((void (*)(unsigned long, unsigned long)) (b + FN_REMOVE_COSTUME))(p, g_dancer_item[i][k]);
    }
    if (g_music_ours && restore_music) {
        unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
        if (room) ((void (*)(unsigned long)) (b + FN_ROOM_MUSIC))(room);
    }
    g_music_ours = 0;
    g_dancing = 0;
}

/* does this live player wear this costume (the record's item, as costume_frame finds it) */
static int wears(unsigned long p, unsigned long item) {
    unsigned long begin = *(const unsigned long *) (p + PLAYER_COSTUMES);
    unsigned long end = *(const unsigned long *) (p + PLAYER_COSTUMES + 8);
    for (unsigned long c = begin; c && end >= c + COSTUME_RECORD; c += COSTUME_RECORD)
        if (*(const unsigned long *) (c + COSTUME_ITEM) == item) return 1;
    return 0;
}

/*
 * Dancers the run no longer holds are forgotten and never touched (in_run).
 * So is one the run holds who no longer wears the dance we put on him: the
 * allocator hands a deleted player's address to the next player made, so a
 * restart can put a NEW player at a dancer's old address - alive, in the run,
 * and not ours (the sixth pass, 19.09.2026: the costume is in the vector right
 * after the add, so a dancer we dressed always wears it). A player who left on
 * his own - a co-op player gone - leaves the rest dancing. When none is left,
 * the run's players were swapped under the dance (a restart: new players, the
 * state still 2): the dance ended without us, and the next pass starts it
 * afresh if the room still asks for it. Its music is still ours only while
 * one of our tracks is what the music manager plays (+0x3c8); then the
 * dance's end still puts the room's back.
 */
static void dance_cast_check(unsigned long game, unsigned long manager) {
    if (!g_dancing) return;
    int left = 0, gone = 0;
    for (int i = 0; i < DANCERS; i++) {
        unsigned long p = g_dancer[i];
        if (!p) continue;
        if (in_run(game, p) && (!g_dancer_item[i][0] || wears(p, g_dancer_item[i][0]))) { left++; continue; }
        g_dancer[i] = 0;
        g_dancer_item[i][0] = g_dancer_item[i][1] = 0;
        gone++;
    }
    if (!gone || left) return;
    int now = *(const int *) (manager + MGR_MUSIC + MUSIC_NOW);
    if (now < g_track_first || now >= g_track_first + T_COUNT) g_music_ours = 0;
    g_dancing = 0;
    g_probe_left = 0;
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[160];
        snprintf(line, sizeof(line), "all %d dancers left the run under the dance; track now %d, ours %d",
                 gone, now, g_music_ours);
        report_line("dance", line);
    }
#endif
}

void shim_mods_open_dance_assets(void *manager) {
    AAssetManager *mgr = (AAssetManager *) manager;
    if (!mgr) return;
    static const char *const DIRS[] = { "", "sheets", "sheets/eden", "sheets_tainted", "items", "items/sprites" };
    for (unsigned i = 0; i < sizeof(DIRS) / sizeof(DIRS[0]); i++) dance_bring_in(mgr, DIRS[i]);
}

/* between frames (gl.c), on the frame thread */
void shim_mods_between_frames(void) {
    { unsigned long b0 = shim_image_base(); if (b0) marks_between_frames(b0); }
    unsigned long b = shim_image_base();
    if (!b) return;
    void shim_bossbars_between_frames(void);
    shim_bossbars_between_frames();
    void shim_statsplus_between_frames(void);
    shim_statsplus_between_frames();
    unlocks_probe(b);
    unsigned long manager = manager_now(b), game = game_now(b);
    if (!manager) return;
    if (!dance_load(b, manager, game)) return;

    /*
     * THE RUN FIRST, THE DANCERS AFTER (13.10.2026). The costumes used to be
     * played and probed before the run was asked about, so the first pass
     * after an exit to the menu walked the deleted players (see in_run); and
     * a restart never leaves state 2, so there it went on every pass.
     */
    int shim_sticks_in_game(void);
    if (!game || !shim_sticks_in_game()) {
        /* the run is gone, and its players with it: forget, touch nothing */
        g_dancing = 0; g_music_ours = 0; g_dance_room = 0; g_probe_left = 0;
        memset(g_dancer, 0, sizeof(g_dancer));
        memset(g_dancer_item, 0, sizeof(g_dancer_item));
        return;
    }
    dance_cast_check(game, manager);        /* before anything of a dancer is read */
    if (g_dancing) costume_frame(b);
    if (g_probe_left > 0) {
        g_probe_left--;
        if (g_probe_left % 30 == 0 && g_dancer[0]) costume_probe(g_dancer[0], "later");
    }
    dance_sweep(b, game);                   /* before any return below: blind, switched off */
    unsigned long room = *(const unsigned long *) (game + GAME_ROOM);
    if (!room) return;
    /* our music outlives a dance whose players all left (dance_cast_check): it ends with the dance's end */
    int ours = g_dancing || g_music_ours;
    if (!shim_tweaks_mod(MOD_DANCE)) {
        if (ours) dance_stop(b, game, 1);
        return;
    }
    if (room != g_dance_room) {
        g_dance_room = room;
        if (ours) dance_stop(b, game, 0);   /* the new room brings its own music */
    }
    if (!shim_modset_int(MS_DANCE_BLIND) && (((unsigned (*)(unsigned long)) (b + FN_CURSES))(game) & CURSE_BLIND))
        return;
    int want = good_item_here(b, manager, room);
    if (want && !g_dancing) dance_start(b, game, manager);
    else if (!want && (g_dancing || g_music_ours)) dance_stop(b, game, 1);
}

/* ------------------------------------------------------------- the draw */
void shim_mods_draw(void) {
    unsigned long b = shim_image_base();
    g_draws++;
    if (!b || !shim_eid_hud_showing() || shim_eid_seed_on(SEED_NO_HUD)) return;
    unsigned long game = game_now(b), manager = manager_now(b);
    if (!game || !manager) return;
    waves_draw(b, game, manager);
    planet_draw(b, game, manager);
    void shim_bossbars_draw(void);
    shim_bossbars_draw();
    void shim_statsplus_draw(void);
    shim_statsplus_draw();
}
