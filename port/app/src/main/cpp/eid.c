/*
 * External Item Descriptions: what the thing on the pedestal actually does.
 *
 * The desktop game has a Lua mod for this - wofsauge's External Item
 * Descriptions - and iOS has emp0ry's native port of it, written as an
 * Objective-C dylib. Neither can be dropped into this port as it stands: the
 * first needs a Lua API the mobile build does not have, the second is an iOS
 * library that reads memory through Mach calls and draws through UIKit. What
 * transfers is the KNOWLEDGE in them - where the game keeps what, and what to
 * say about each item - and that is what this file uses.
 *
 * The offsets below come from that iOS port's own documentation, and they are
 * valid here for a reason worth writing down: its supported build is named by
 * the executable's LC_UUID, F4357753-A25F-30EE-BACF-63709F902895, and OUR
 * image carries exactly that UUID. Same binary, same layout, nothing to
 * re-derive. If the image is ever replaced, every offset here needs checking
 * again - the check itself is one line, and it is made at startup.
 *
 * WHERE THIS PORT DIFFERS, deliberately.
 *
 * The iOS mod has no access to the game's own structures, so it hunts for
 * entities by walking every readable region of the process - up to half a
 * gigabyte at a time - looking for pointers that match a known vtable. That is
 * the right answer for an injected tweak and the wrong one here: we load the
 * image ourselves and know where things are. EntityList::Update (0x76436c)
 * receives the live list as its first argument, keeps the entities in an array
 * at +0x78 with the count at +0x84, and runs every frame. One hook, one
 * pointer, no scanning at all.
 */
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include "modset.h"
#include "diag.h"
#include "devices.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
float shim_screen_width(void);
float shim_screen_height(void);

/* --------------------------------------------------------------- the font */

/*
 * EID prints with a font of its own, and that is the answer to a question two
 * wrong guesses did not settle: the desktop mod loads
 * resources/font/eid_default.fnt - PixelMplus10, a small pixel face with
 * lower case and Cyrillic - and none of the game's own fonts looks like it.
 * The game's are Team Meat's small caps; that is why every candidate came out
 * as "the same headline font".
 *
 * So the font travels with us. Its three files sit in the assets, they are put
 * into the resource table under their own names (pack.c), and the game's own
 * Font::Load reads them from there - the same road it uses for its own fonts,
 * with no special case in its code. The object is ours: 0x8bdd90 builds a font
 * in place and 0x8bd4f4 loads a path into it.
 */
#define MANAGER      0xac2428              /* the Manager pointer, the fonts live inside it */

/*
 * WHICH font, chosen from the settings file rather than guessed at twice more.
 *
 * The game keeps several font objects inside the Manager and the first two
 * tries picked the wrong ones - the headline face, three times too large. The
 * one that is certain is 0x196a48: 0x66dcec builds it (0x8bdd90) and loads
 * TeamMeatFontExtended into it (0x8bd4f4), which is the small pixel face with
 * Cyrillic that the stats are drawn in. The others are listed beside it so the
 * owner can settle the question in one sitting with `eidfont` in
 * port-touch.ini instead of a rebuild per candidate.
 */
static const unsigned long FONT_CANDIDATES[] = {
    0x196a48,       /* 0: TeamMeatFontExtended - loaded here by 0x66dcec */
    0x1769f8,       /* 1: the other one the HUD picks between */
    0x468,          /* 2: what the stats drawing is handed (Manager + 0x18 + 0x450) */
    0x176a48,       /* 3: the neighbour of 0, in case the pair is the other way round */
};
#define FONT_COUNT ((int) (sizeof(FONT_CANDIDATES) / sizeof(FONT_CANDIDATES[0])))

#define FN_FONT_INIT 0x8bdd90               /* Font::Font, in place */
#define FN_FONT_LOAD 0x8bd4f4               /* Font::Load(path) */
/*
 * How large a Font is, and this is what three crashes were actually about.
 *
 * text.c says it plainly: the object carries a glyph table of 65536 entries at
 * +0x50 - a quarter of a megabyte on its own. A kilobyte "with room to spare"
 * was nothing of the sort, and Font::Load wrote straight past the end of it.
 * Half a megabyte here, which is generous in the honest sense.
 */
#define FONT_BYTES   0x80000

static int g_font_choice = -1;              /* -1 ours, -2 a game file in our object, 0..3 the game's own */
static unsigned char g_font[FONT_BYTES] __attribute__((aligned(16)));

/*
 * And a second face, for the one language the first cannot draw at all.
 *
 * eid_default is PixelMplus10: 7363 glyphs - 95 Latin, 80 Cyrillic, 179 kana,
 * 6356 ideographs - and not ONE Hangul syllable. So a Korean description came
 * out as icons and numbers with the words simply missing, which is what the
 * owner saw. Japanese and Chinese were never affected: their characters are in
 * there.
 *
 * The mod has the same problem and solves it by shipping Korean faces of its
 * own and choosing one for that language (descriptions/ab+/ko_kr.lua, fonts[1]
 * = korean_yoonwebdotum10_shadow). This carries the same file: 3142 glyphs,
 * 2350 of them Hangul, line height 11 and base 9 - the same metrics as
 * eid_default, so the layout needs nothing. Measured against the Korean half
 * of eid.bin: 862 different characters are used there and the face has every
 * one of them.
 */
#define FONT_KOREAN "eid_korean_yoonwebdotum10_shadow"
#define HANGUL_TEST 0xac00u                 /* 가, the first syllable */


/*
 * AND THE SAME HOLE IN CHINESE, found the same way (16.09.2026).
 *
 * eid_default is a JAPANESE face. Its six thousand ideographs are the Japanese
 * set, and the simplified Chinese forms are different characters - so they are
 * simply not in it. Measured against the Chinese half of eid.bin by
 * tools/eid_glyphs.py: of the 1568 characters it uses, 474 are missing, and
 * they touch 2270 of its 3966 strings. A name made entirely of missing
 * characters draws as nothing at all, which is what the owner reported.
 *
 * The owner's first thought was that the bake had been cut short and a fuller
 * version of the same face existed. It does not: the .ttf the sheet was baked
 * from carries 7364 characters against the sheet's 7363, read straight out of
 * its cmap. The face has everything it ever had.
 *
 * So the mod's own Chinese face is carried, the one it lists first for zh_cn:
 * 12817 glyphs, every character our database uses except U+001F, which is the
 * mod's own separator and not text. No halftones, the same weight and the same
 * base as eid_default, so the panel's layout needs nothing. Its descriptor
 * names TWO sheets - its own and a small patch page - and both must travel.
 */
#define FONT_CHINESE "eid_cn_alt"
#define HANZI_TEST   0x4e1au                /* 业, simplified, absent from eid_default */

/*
 * AND VIETNAMESE, once every language the mod carries was put back (16.09.2026).
 * Measured the same way: 62 of the 170 characters Vietnamese uses are not in
 * eid_default - the stacked accents - and they touch 1734 of its 3964 strings.
 * The mod's own Vietnamese face, eid_vi_vn, has every one of them; it is packed
 * like eid_default (the letter in the colour channels, the outline in alpha)
 * and names two sheets.
 */
#define FONT_VIETNAMESE "eid_vi_vn"
#define VIET_TEST    0x1ea1u                /* ạ, absent from eid_default */

/*
 * AND ARABIC (the owner, 26.10.2026): the "Arabic EID" mod's Noto Sans Arabic
 * UI (OFL), made at the panel's own size by tools/eid_arabic_font.py - every
 * glyph of eid_default as it is, the Arabic letters on one sheet more. Its
 * descriptor names eid_default's two sheets (brought in above) and its own.
 *
 * Arabic letters JOIN, and the engine draws a line glyph by glyph: the rim of
 * each letter lands on the stroke of the one before and cuts every join. So
 * this face has a TWIN without the rim (eid_ar_plain, eid_borderless's glyphs
 * and the same letters), and a line is written twice - rimmed, then the twin
 * over it - so that every rim lies under every letter (draw_text).
 */
#define FONT_ARABIC  "eid_ar"
#define ALEF_TEST    0xfe8du                /* ا, the isolated alef */

/*
 * THE FACES OF THEIR OWN, in one table. Korean came first, Chinese copied it,
 * and a third copy for Vietnamese would have been a third place to get the
 * sheets or the fallback wrong - so each is a row: the database's language
 * code, the face, a character it must have (font_into checks the glyph table
 * for it), and every sheet its descriptor names, all of which must travel -
 * and, for a script whose letters join, the rimless twin written over it.
 */
typedef struct {
    const char *lang;
    const char *leaf;
    unsigned test;
    const char *sheets[3];
    const char *what;
    const char *twin;                       /* the same letters without the rim, or NULL */
    const char *twin_sheet;                 /* the twin's own sheet (the rest are eid_borderless's) */
} face_t;

static const face_t FACES[] = {
    { "ko_kr", FONT_KOREAN, HANGUL_TEST, { FONT_KOREAN "_0.png" }, "Korean", NULL, NULL },
    { "zh_cn", FONT_CHINESE, HANZI_TEST, { FONT_CHINESE "_0.png", "eid_cn_fix_0.png" }, "Chinese",
      NULL, NULL },
    { "vi", FONT_VIETNAMESE, VIET_TEST,
      { FONT_VIETNAMESE "_0.png", FONT_VIETNAMESE "_1.png" }, "Vietnamese", NULL, NULL },
    { "ar", FONT_ARABIC, ALEF_TEST, { "eid_arabic_02.png" }, "Arabic",
      FONT_ARABIC "_plain", "eid_arabic_plain.png" },
};
#define FACE_COUNT ((int) (sizeof(FACES) / sizeof(FACES[0])))

/*
 * THE MENU PAGES' FACES (tweaks.c, shim_menu_write): the game's
 * teammeatfont16bold, 20bold and 10 - as the port's own copies under names no
 * mod brings (tools/menu_fonts.py, the owner 05.10.2026). Asked by the game's
 * names they are what the mods make of them: Reflashed brings all five
 * teammeatfont* as ASCII capitals only, and every Cyrillic letter and umlaut
 * of the port's pages went blank. The game's own text keeps what the mods give
 * it, as on the PC; the port's text is the port's. The sheet is the copy's own
 * too: a sheet name the game has loaded already would come from its texture
 * cache, whoever's it was.
 */
static const char *const MENU_FONT[3] = { "port_menufont16bold", "port_menufont20bold", "port_menufont10" };

static unsigned char g_face[FACE_COUNT][FONT_BYTES] __attribute__((aligned(16)));
static signed char g_face_state[FACE_COUNT];    /* 0 untried, 1 loaded, -1 failed */
/* one twin is enough: Arabic is the only face that has one */
static unsigned char g_twin[FONT_BYTES] __attribute__((aligned(16)));
static signed char g_twin_state;
static int g_twin_of = -1;                      /* the face it was loaded for */
static int g_font_ready;
static int g_font_failed;                   /* fall back to the game's rather than to nothing */

void shim_pack_add(const char *path, const void *data, long length);
void shim_pack_port_file(const char *asset, const char *path);

/*
 * The resource table is keyed by the path the game asks for, and the game's
 * own font paths appear in it as "resources/data/font/...". Rather than work
 * out which prefix the lookup strips, the file is entered under every shape it
 * could be asked for - three table rows, a pointer each, and the question
 * stops mattering.
 */
static void bring_in(AAssetManager *mgr, const char *name, const char *folder, const char *leaf) {
    AAsset *a = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
    if (!a) { LOG("eid: %s is not in the assets", name); return; }
    const void *b = AAsset_getBuffer(a);
    long n = (long) AAsset_getLength64(a);
    if (!b || n <= 0) { LOG("eid: %s did not map", name); return; }
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", folder, leaf);
    shim_pack_add(path, b, n);
    /* the export's registry of the port's own files: the asset and its one canonical path (pack.c, M12) */
    shim_pack_port_file(name, path);
    snprintf(path, sizeof(path), "data/%s/%s", folder, leaf);
    shim_pack_add(path, b, n);
    snprintf(path, sizeof(path), "resources/data/%s/%s", folder, leaf);
    shim_pack_add(path, b, n);
    /* the asset is deliberately left open: the mapping has to outlive us */
}

/* the same road, for another file of the port that brings an asset of its own */
void shim_asset_bring_in(void *mgr, const char *name, const char *folder, const char *leaf) {
    bring_in((AAssetManager *) mgr, name, folder, leaf);
}

/*
 * Built once the image is up. The font is loaded lazily on the first draw
 * rather than here, because Font::Load reaches into the resource system and
 * that is not ready while the assets are still being enumerated.
 */
static void font_open(void) {
    unsigned long b = shim_image_base();
    if (g_font_ready || !b) return;
    g_font_ready = 1;                       /* one attempt, whatever happens */

    /*
     * Only if the file is really there. Handing Font::Load a path the resource
     * system cannot answer takes the process down with it, which is exactly
     * what happened the first time this was tried.
     */
    int shim_pack_has(const char *);
    if (!shim_pack_has("resources/data/font/eid_default.fnt")) {
        LOG("eid: the font is not in the resource table - the panel stays unstyled");
        g_font_failed = 1;
        if (shim_report_active()) report_line("eid", "the font was not registered");
        return;
    }
    /*
     * RELATIVE, and this is what cost a day.
     *
     * The image names its own fonts "font/TeamMeatFont10.fnt", and the
     * resource system builds the rest of the path itself. Handed an absolute
     * one it prefixes the root a second time, opens nothing, and - because
     * Font::Load reports nothing when the file is missing - leaves a silent,
     * empty object behind: the file handle at +0x38 is set, the glyph table at
     * +0x50 stays zero, and every string drawn with it is invisible. That is
     * exactly what "the panel does not show at all" was.
     *
     * The measurement that settled it loaded the GAME's own teammeatfont10
     * into this same object (`eidfont = -2`): with the absolute path its table
     * was as empty as ours, with the relative one it matched the game's font
     * entry for entry. So the fault was never the file and never the buffer.
     */
    char path[512];
    snprintf(path, sizeof(path), "font/%s",
             g_font_choice == -2 ? "teammeatfont10.fnt" : "eid_default.fnt");

    memset(g_font, 0, sizeof(g_font));
    ((void (*)(void *, const char *, void *)) (b + FN_FONT_LOAD))(g_font, path, NULL);

    /*
     * +0x38 is NOT a file handle, and believing that it was is how the silent
     * failure went unnoticed: 0x8bd544 allocates forty bytes there before the
     * file is even looked for (0x8bd548 stores it), so it is filled whether the
     * load worked or not. It is still worth checking - a null means the
     * allocation itself did not happen - but the real question is the glyph
     * table below.
     */
    if (!*(void *const *) (g_font + 0x38)) {
        g_font_failed = 1;
        LOG("eid: the font did not load - the game's own is used");
        if (shim_report_active()) report_line("eid", "the font did not load");
        return;
    }
    /*
     * Loaded is not the same as usable, so the object is asked one question
     * before it is trusted: the glyph table at +0x50 (text.c, one entry per
     * code point) has to answer for a letter. 65535 is "no such glyph", and a
     * table full of those is the silent failure above.
     */
    const unsigned short *glyphs = (const unsigned short *) (g_font + 0x50);
    if (glyphs['A'] == 0xffff && glyphs['a'] == 0xffff) {
        g_font_failed = 1;
        LOG("eid: the font has no glyphs - the game's own is used");
        if (shim_report_active()) report_line("eid", "the font has no glyphs");
        return;
    }
    LOG("eid: the description font is loaded ('A' -> %u)", glyphs['A']);
    if (shim_report_active()) {
        /*
         * One more question, and the last one that can be asked without eyes:
         * how wide is a line of text in this font. The engine's own measure
         * (0x8bdec8, named in text.c) walks the same glyphs the blitter does,
         * so a width above zero means the metrics are real and the string will
         * put pixels on the screen - not merely that a file was opened.
         */
        int (*measure)(void *, const char *) =
            (int (*)(void *, const char *)) (b + 0x8bdec8);
        unsigned long mgr = *(const unsigned long *) (b + MANAGER);
        char line[160];
        snprintf(line, sizeof(line),
                 "font loaded, 'A' -> %u 'a' -> %u, \"Sad Onion\" is %d wide (the game's own: %d)",
                 glyphs['A'], glyphs['a'], measure(g_font, "Sad Onion"),
                 mgr ? measure((void *) (mgr + FONT_CANDIDATES[0]), "Sad Onion") : -1);
        report_line("eid", line);
    }
}

/*
 * Font::Load into an object of ours, with the two checks that catch the silent
 * failure font_open() describes above: the allocation at +0x38, and a glyph in
 * the table at +0x50 for a character this face is supposed to have.
 */
static int font_into(void *object, const char *leaf, unsigned test) {
    unsigned long b = shim_image_base();
    int shim_pack_has(const char *);
    char path[512];
    snprintf(path, sizeof(path), "resources/data/font/%s.fnt", leaf);
    if (!b || !shim_pack_has(path)) {
        LOG("eid: %s is not in the resource table", leaf);
        return 0;
    }
    snprintf(path, sizeof(path), "font/%s.fnt", leaf);      /* relative, always */
    memset(object, 0, FONT_BYTES);
    ((void (*)(void *, const char *, void *)) (b + FN_FONT_LOAD))(object, path, NULL);
    if (!*(void *const *) ((unsigned char *) object + 0x38)) return 0;
    const unsigned short *glyphs = (const unsigned short *) ((unsigned char *) object + 0x50);
    return glyphs[test] != 0xffff;
}

/*
 * The face a language draws with, opened the first time that language is
 * drawn; nought when the language has none of its own or its face would not
 * load, and then the panel's own face is used.
 */
static unsigned long face_for(const char *lang) {
    for (int i = 0; i < FACE_COUNT; i++) {
        if (strcmp(lang, FACES[i].lang)) continue;
        if (!g_face_state[i]) {
            int ok = font_into(g_face[i], FACES[i].leaf, FACES[i].test);
            g_face_state[i] = ok ? 1 : -1;
            LOG("eid: the %s font %s", FACES[i].what, ok ? "is loaded" : "did not load");
            if (shim_report_active()) {
                char line[80];
                snprintf(line, sizeof(line), "the %s font %s", FACES[i].what,
                         ok ? "is loaded" : "did not load");
                report_line("eid", line);
            }
        }
        return g_face_state[i] > 0 ? (unsigned long) g_face[i] : 0;
    }
    return 0;
}

/* the rimless twin of the language's face, written over it; nought when it has none */
static unsigned long twin_for(const char *lang) {
    for (int i = 0; i < FACE_COUNT; i++) {
        if (strcmp(lang, FACES[i].lang) || !FACES[i].twin) continue;
        if (!face_for(lang)) return 0;          /* no face, no twin over it */
        if (g_twin_of != i) {
            g_twin_of = i;
            g_twin_state = font_into(g_twin, FACES[i].twin, FACES[i].test) ? 1 : -1;
            LOG("eid: the %s twin %s", FACES[i].what, g_twin_state > 0 ? "is loaded" : "did not load");
        }
        return g_twin_state > 0 ? (unsigned long) g_twin : 0;
    }
    return 0;
}

/* ------------------------------------------------------------ the database */

/*
 * eid.bin, built by tools/eid_pack.py from the upstream JSON. Flat and sorted,
 * so a lookup is a binary search and nothing is parsed at runtime; the file
 * lives in the APK's assets and is used straight out of the mapping.
 */
#include "eid_charge.h"
#include "eid_luck.h"
#include "eid_wisps.h"
#include "eid_rules.h"

#define EID_MAGIC 0x42444945u               /* "EIDB" */

enum {
    EID_COLLECTIBLE = 1,
    EID_TRINKET = 2,
    EID_CARD = 3,
    EID_PILL = 4,
    EID_HORSEPILL = 5,
    EID_DICE = 6,
    EID_SACRIFICE = 7,
    EID_BIRTHRIGHT = 8,                     /* one row per player type, tools/eid_more.py */
    EID_PHRASE = 9,                         /* the mod's own short phrases, by number */
    EID_GOLDEN = 10,                        /* what a golden trinket says instead */
    EID_GOLDEN_DATA = 11,                   /* and which of its numbers to double */
    EID_CONDITION = 12,                     /* the words the conditions print, by key */
    EID_RED_TO = 13,                        /* "+1 Health" -> "+1 Soul Heart", by heart */
    EID_PLURAL = 14,                        /* the language's own plural endings */
    EID_WISP_TEXT = 15,                     /* what Book of Virtues makes of an item */
    EID_LOCUST_TEXT = 16,                   /* and what Abyss makes of it */
    EID_PRIZE_WORD = 17,                    /* the stats and pickups Consolation Prize names */
    EID_GLITCH_TEXT = 18,                   /* GlitchedItemText, rows as eid_glitch.c asks */
    EID_GLITCHED = 40,                      /* not a table: a TMTRAINER item under the player */
};

#define TRINKET_GOLDEN 0x8000               /* the flag the game keeps in the subtype */

/* the phrases, in the order tools/eid_more.py writes them */
enum {
    PHRASE_GOLDEN = 0,
    PHRASE_TRIPLED = 1,
    PHRASE_QUADRUPLED = 2,
    PHRASE_VARIABLE_CHARGE = 3,
    PHRASE_RESULTS_WITH = 4,
    PHRASE_SPINDOWN_ERROR = 5,
    PHRASE_BLANK_CARD = 6,
    PHRASE_SINGLE_USE = 7,
    PHRASE_LUCK = 8,
    PHRASE_BLACK_FEATHER = 9,
    PHRASE_FALSE_PHD_DAMAGE = 10,
    PHRASE_FALSE_PHD_HORSE = 11,
    PHRASE_FALSE_PHD_HEART = 12,
    PHRASE_EXPERIMENTAL_PHD = 13,
    PHRASE_EXPERIMENTAL_FALSE = 14,
};

typedef struct { unsigned id, name, text; } entry_t;
static unsigned g_en_at;                    /* English, the fallback for the added tables */
typedef struct { unsigned kind, count, offset; } section_t;

static const unsigned char *g_db;           /* the whole file */
static long g_db_len;
static const char *g_strings;               /* the pool, inside the file */
static unsigned g_lang_at;                  /* the chosen language's sections */
static char g_lang_code[9];

static unsigned rd32(const unsigned char *p) {
    return (unsigned) p[0] | ((unsigned) p[1] << 8) |
           ((unsigned) p[2] << 16) | ((unsigned) p[3] << 24);
}

/*
 * The game names its language differently from the description database, and
 * the numbers are the game's own - measured, and measured twice.
 *
 * The setting is an int, printed by the options writer as "Language=%d"
 * (0x7ebd44) and fetched by 0x66de14 from Manager + 0x564f8; the same call
 * decides which of two fonts the game draws with (0x578490), which is what
 * proves the field. The numbers are NOT a position in a list: they are the ids
 * in the game's own string tables (data/stringtable.sta), and reading them
 * settled a false start - a table of language codes elsewhere in the image put
 * Russian at 7, while the game's own answer for Russian is 10.
 *
 *     0 English   2 Japanese   3 French   4 Spanish
 *     5 German   10 Russian   11 Korean  13 Chinese (Simple)
 *
 * The database uses its own spellings, and anything the game has but the
 * database has not falls back to English rather than to silence.
 */
#define LANGUAGE 0x564f8

static const char *db_code_for(int language) {
    switch (language) {
        case 0:  return "en_us";
        case 2:  return "ja_jp";
        case 3:  return "fr";
        case 4:  return "spa";
        case 5:  return "de";
        case 10: return "ru";
        case 11: return "ko_kr";
        case 13: return "zh_cn";
        default: return "en_us";
    }
}

/*
 * Whether a line may break between characters rather than only between words.
 * The mod keeps this as a list of languages with one entry in it (zh_cn); the
 * wrapper down in fit_to_width reads it. Chinese is written without spaces, so
 * without this a whole sentence is one word and simply runs off the panel.
 */
static int g_break_chars;

static void choose_language(const char *code) {
    if (!g_db || !code) return;
    unsigned langs = rd32(g_db + 8);
    const unsigned char *table = g_db + 16;
    for (unsigned i = 0; i < langs; i++) {
        const char *name = (const char *) (table + i * 12);
        if (!strncmp(name, code, 8)) {
            g_lang_at = rd32(table + i * 12 + 8);
            snprintf(g_lang_code, sizeof(g_lang_code), "%s", code);
            g_break_chars = !strcmp(code, "zh_cn");
            return;
        }
    }
    if (strcmp(code, "en_us")) choose_language("en_us");
}

/*
 * The owner can pin a language instead of following the game's: the port's
 * interface is Russian while the game itself is set to English, so "follow the
 * setting" is not always what is wanted. `eidlang` in port-touch.ini takes a
 * code the database knows ("ru", "en_us", "de"...); absent means follow.
 */
static char g_lang_pinned[9];
static int g_game_language = -1;        /* the game's own setting, once it has held still */
static const entry_t *lookup(unsigned kind, unsigned id);      /* below, with the database */
static int g_lang_changed;              /* the panel on screen is in another language */

/*
 * Back to following the game. Clearing the pin alone left the pinned language
 * in charge until the game's setting next changed - which it does not - so AUTO
 * showed whatever was pinned last (the owner, 18.09.2026).
 */
static void follow_the_game(void) {
    g_lang_pinned[0] = 0;
    if (g_db && g_game_language >= 0) choose_language(db_code_for(g_game_language));
    g_lang_changed = 1;
}

void shim_eid_language_pin(const char *code) {
    if (!code || !*code) { follow_the_game(); return; }
    /* the settings file hands the value with its line ending still attached */
    char clean[9];
    int n = 0;
    while (code[n] && n < (int) sizeof(clean) - 1 && code[n] > 0x20)
        { clean[n] = code[n]; n++; }
    clean[n] = 0;
    if (!n || !strcmp(clean, "auto")) { follow_the_game(); return; }
    snprintf(g_lang_pinned, sizeof(g_lang_pinned), "%s", clean);
    if (g_db) choose_language(g_lang_pinned);
    g_lang_changed = 1;
    /*
     * PROBE, report builds (the testers, 28.09.2026: "switching the language
     * makes the descriptions vanish"): what the pin resolved to, and whether
     * the first item has a name and a text in that table.
     */
    if (shim_report_active()) {
        const entry_t *e = lookup(1 /* EID_COLLECTIBLE, declared below */, 1);
        char line[200];
        snprintf(line, sizeof(line), "language pinned \"%s\" -> table \"%s\" at %#x; item 1: %s / %s",
                 clean, g_lang_code, g_lang_at,
                 e ? g_strings + e->name : "(no name)", e ? "text" : "(no text)");
        report_line("eid", line);
    }
}

/* the language the descriptions are shown in right now, pinned or followed */
const char *shim_eid_language_now(void) { return g_lang_code; }

/* "" when the language follows the game's own setting */
const char *shim_eid_language_pinned(void) { return g_lang_pinned; }

/* the languages the database actually carries, for the Tweaks page to offer */
int shim_eid_language_count(void) { return g_db ? (int) rd32(g_db + 8) : 0; }

const char *shim_eid_language_code(int i) {
    if (!g_db || i < 0 || i >= shim_eid_language_count()) return "";
    return (const char *) (g_db + 16 + (unsigned long) i * 12);
}

void shim_eid_language(int language) {
    if (g_lang_pinned[0]) { choose_language(g_lang_pinned); return; }
    const char *want = db_code_for(language);
    if (!strcmp(want, g_lang_code)) return;
    choose_language(want);
    LOG("eid: descriptions in %s", g_lang_code);
}

/*
 * Opened once, beside the resource pack. The asset is stored uncompressed, so
 * the mapping is the file itself and nothing is copied; if a future build ever
 * compresses it, getBuffer answers with nothing and we simply go without
 * descriptions rather than reading three megabytes into the heap behind the
 * owner's back.
 */
void shim_eid_open(void *manager) {
    AAssetManager *mgr = (AAssetManager *) manager;
    if (!mgr || g_db) return;
    /* the menu pages' faces, the port's own copies of the game's (MENU_FONT below), whatever the database does */
    for (int i = 0; i < 3; i++) {
        char leaf[64];
        snprintf(leaf, sizeof(leaf), "%s.fnt", MENU_FONT[i]);
        bring_in(mgr, leaf, "font", leaf);
        snprintf(leaf, sizeof(leaf), "%s_0.png", MENU_FONT[i]);
        bring_in(mgr, leaf, "font", leaf);
    }
    AAsset *asset = AAssetManager_open(mgr, "eid.bin", AASSET_MODE_BUFFER);
    if (!asset) { LOG("eid: eid.bin is not in the assets"); return; }
    const unsigned char *b = (const unsigned char *) AAsset_getBuffer(asset);
    long n = (long) AAsset_getLength64(asset);
    if (!b || n < 32 || rd32(b) != EID_MAGIC) {
        LOG("eid: eid.bin did not map, or is not a database");
        AAsset_close(asset);
        return;
    }
    /* the font travels with the descriptions */
    bring_in(mgr, "eid_default.fnt", "font", "eid_default.fnt");
    bring_in(mgr, "eid_default_0.png", "font", "eid_default_0.png");
    bring_in(mgr, "eid_default_1.png", "font", "eid_default_1.png");
    /* the same letters without their dark rim, for text on paper (spawner.c) */
    bring_in(mgr, "eid_borderless.fnt", "font", "eid_borderless.fnt");
    bring_in(mgr, "eid_borderless_0.png", "font", "eid_borderless_0.png");
    /* and the Korean face beside it, with the sheet its descriptor names */
    for (int i = 0; i < FACE_COUNT; i++) {
        char leaf[64];
        snprintf(leaf, sizeof(leaf), "%s.fnt", FACES[i].leaf);
        bring_in(mgr, leaf, "font", leaf);
        for (int k = 0; k < 3 && FACES[i].sheets[k]; k++)
            bring_in(mgr, FACES[i].sheets[k], "font", FACES[i].sheets[k]);
        if (FACES[i].twin) {
            snprintf(leaf, sizeof(leaf), "%s.fnt", FACES[i].twin);
            bring_in(mgr, leaf, "font", leaf);
            bring_in(mgr, FACES[i].twin_sheet, "font", FACES[i].twin_sheet);
        }
    }

    /*
     * And the four atlases the markers draw from, each an .anm2 beside its
     * .png. The game asks for a sprite sheet by the name written inside the
     * .anm2, resolved beside it, so both go in under the same folder.
     */
    static const char *const sheets[] = {
        "eid_inline_icons", "eid_transform_icons", "eid_cardspills", "eid_player_icons",
    };
    for (int i = 0; i < (int) (sizeof(sheets) / sizeof(sheets[0])); i++) {
        char leaf[64];
        snprintf(leaf, sizeof(leaf), "%s.anm2", sheets[i]);
        bring_in(mgr, leaf, "gfx", leaf);
        snprintf(leaf, sizeof(leaf), "%s.png", sheets[i]);
        bring_in(mgr, leaf, "gfx", leaf);
    }

    g_db = b;
    g_db_len = n;
    g_strings = (const char *) (b + rd32(b + 12));
    choose_language("en_us");
    g_en_at = g_lang_at;                    /* kept for the tables that fall back */
    LOG("eid: %u languages, %ld KB", rd32(b + 8), n / 1024);
}

/* the entry for one id in one section of one language, or nothing */
static const entry_t *lookup_in(unsigned at, unsigned kind, unsigned id) {
    if (!g_db || !at) return NULL;
    const unsigned char *lang = g_db + at;
    unsigned sections = rd32(lang);
    for (unsigned s = 0; s < sections; s++) {
        const unsigned char *sec = lang + 4 + s * 12;
        if (rd32(sec) != kind) continue;
        unsigned count = rd32(sec + 4);
        const unsigned char *rows = g_db + rd32(sec + 8);
        long lo = 0, hi = (long) count - 1;
        while (lo <= hi) {
            long mid = (lo + hi) / 2;
            unsigned got = rd32(rows + mid * 12);
            if (got == id) return (const entry_t *) (rows + mid * 12);
            if (got < id) lo = mid + 1; else hi = mid - 1;
        }
        return NULL;
    }
    return NULL;
}

static const entry_t *lookup(unsigned kind, unsigned id) {
    return lookup_in(g_lang_at, kind, id);
}

/*
 * Every named row of one kind, for the item spawner's search (spawner.c): the
 * name in the language the panel is set to, and the English name beside it, so
 * a player can find a thing by either. English is walked, because English has
 * every row; a language that lacks one gives NULL for its own name.
 */
int shim_eid_each_name(unsigned kind,
                       void (*fn)(unsigned id, const char *name, const char *english, void *ctx),
                       void *ctx) {
    if (!g_db || !g_en_at || !fn) return 0;
    const unsigned char *lang = g_db + g_en_at;
    unsigned sections = rd32(lang), n = 0;
    for (unsigned s = 0; s < sections; s++) {
        const unsigned char *sec = lang + 4 + s * 12;
        if (rd32(sec) != kind) continue;
        unsigned count = rd32(sec + 4);
        const unsigned char *rows = g_db + rd32(sec + 8);
        for (unsigned r = 0; r < count; r++) {
            const entry_t *en = (const entry_t *) (rows + r * 12);
            const entry_t *mine = g_lang_at != g_en_at ? lookup(kind, en->id) : en;
            fn(en->id, mine && g_strings[mine->name] ? g_strings + mine->name : NULL,
               g_strings + en->name, ctx);
            n++;
        }
    }
    return (int) n;
}

/*
 * AN EMPTY NAME IS ENGLISH (the testers, 28.09.2026: "when I change the
 * language, the descriptions stop appearing", and "only in the sacrifice
 * room"). The mod's names files say so in their first line - "if an entry is
 * missing or empty, the English name will be used instead" - and Portuguese,
 * both of them, has every one of its 732 item names empty; a dozen are empty
 * in most other languages. The panel is drawn under its name, and with the
 * name empty it was not drawn at all (draw_panel leaves on an empty g_name),
 * while the sacrifice room, headed by its own words, still was. So a name is
 * this language's when it has one, English otherwise, everywhere a name is
 * read.
 */
static const char *name_or_english(unsigned kind, unsigned id) {
    const entry_t *e = lookup(kind, id);
    if (!e || !g_strings[e->name]) {
        const entry_t *en = lookup_in(g_en_at, kind, id);
        if (en && g_strings[en->name]) e = en;
    }
    return e ? g_strings + e->name : NULL;
}

/* one row's name in the panel's language, falling back to English */
const char *shim_eid_name_of(unsigned kind, unsigned id) {
    return name_or_english(kind, id);
}

/*
 * The same, but falling back to English when this language has not got the
 * entry - which is what the mod does with everything it reads by name
 * (EID:getDescriptionEntry). Only the tables added by tools/eid_more.py need
 * it: the descriptions themselves were filled in language by language when the
 * database was packed.
 */
static const char *text_of(unsigned kind, unsigned id) {
    const entry_t *e = lookup(kind, id);
    if (!e) e = lookup_in(g_en_at, kind, id);
    return e ? g_strings + e->text : NULL;
}

const char *shim_eid_name(unsigned kind, unsigned id) {
    return name_or_english(kind, id);
}

const char *shim_eid_text(unsigned kind, unsigned id) {
    const entry_t *e = lookup(kind, id);
    return e ? g_strings + e->text : NULL;
}

/* ------------------------------------------------------- the game's memory */

/*
 * Offsets, all from the iOS port's NATIVE_LAYOUT.md and all verified there
 * against this exact executable.
 */
#define GAME_PTR        0xac3b90            /* the Game object, a global */
#define GAME_CURSE      0x0c                /* the level's curse mask; 0x40 is Blind */
#define GAME_PLAYERS    0x25c40             /* the player manager */
#define FN_PLAYER       0x4da574            /* (manager, index) -> Entity_Player */

#define ENT_TYPE        0x38                /* int type, then variant, then subtype */
#define ENT_EXISTS      0x1c2               /* byte: the entity is really there */
#define ENT_POSITION    0x310               /* two floats */

#define PICKUP_TYPE     5                   /* Entity type of every pickup */
#define VAR_PILL        70
#define VAR_COLLECTIBLE 100
#define VAR_PILL        70
#define SLOT_TYPE       6                   /* Entity type of the machines */
#define VAR_CRANE       16                  /* the crane game */
#define SLOT_PRIZE      0x570               /* the collectible it will hand over */
#define EFFECT_TYPE     1000                /* Entity type of every effect */
#define VAR_DICE_FLOOR  76
#define VAR_CARD_PORTAL 161                 /* the portals Card Reading opens */
#define VAR_SHOVEL      110                 /* either half of the broken shovel */

/*
 * How far the player may stand from a thing and still be told about it: five
 * grid squares, which is the mod's own MaxDistance, and a square is forty
 * world units (Room::GetGridPosition returns col*40+40). Without this the
 * nearest pedestal anywhere in the room was described, wherever the player
 * stood, which is not what the mod does.
 */
/* the mod's MaxDistance, five tiles by default; a row of EID's page */
#define REACH ((float) shim_modset_int(MS_EID_DISTANCE) * 40.0f)
#define VAR_CARD        300
#define VAR_TRINKET     350

#define LIST_ITEMS      0x78                /* the array of entities */
#define LIST_COUNT      0x84                /* how many */

static unsigned long g_list;                /* EntityList, met once a frame */

/* the hook on EntityList::Update: the live list, handed to us as it is used */
void shim_eid_entity_list(unsigned long list) { g_list = list; }

/* the crafting display and the glitched items, each in a file of its own */
#define ITEM_BAG_OF_CRAFTING 710
static char g_craft[4096];                  /* the crafting display's text, eid_craft.c */
int shim_eid_craft_text(char *out, unsigned long room, unsigned *stamp);
unsigned long shim_eid_craft_player(void);
int shim_eid_glitch_name(int id, char *out, unsigned long n, int *quality);
int shim_eid_glitch_text(int id, char *out, unsigned long room);
int shim_eid_glitch_own_text(int id, char *out, unsigned long n);

/*
 * DisplayGlitchedItemInfo: the mod's option, OFF by default - the full list of
 * what a glitched item does is close to cheating (the owner, 23.09.2026). Off,
 * the item is described by its own garbled text, as in the mod; `eidglitch=1`
 * in port-touch.ini turns the full description on.
 */
static int g_glitch_info;
void shim_eid_glitch_info(int on) { g_glitch_info = on != 0; }
int shim_eid_glitch_info_get(void) { return g_glitch_info; }

static int readable_ptr(const void *p, unsigned long n) {
    int shim_trace_readable(const void *, unsigned long);
    return shim_trace_readable(p, n);
}

/*
 * What is worth describing, and which of them is nearest the player.
 *
 * The iOS mod shows one description at a time - the nearest item - and that is
 * the right choice on a phone, where a room full of pedestals would bury the
 * screen. The same rule is kept here.
 */
/*
 * What the panel is about. `icon` is the marker for the picture drawn before
 * the name, which is not always the id: a pill is described by its effect but
 * pictured by its colour. `hidden` means the player is not allowed to know -
 * the panel then shows a question mark and nothing else.
 */
typedef struct {
    unsigned kind, id;
    char icon[32];
    int hidden;
    int bag;                                /* this is the crafting preview */
    int golden;                             /* a golden trinket, flag and all */
    float distance;
} found_t;

/*
 * A pill says nothing about itself: its subtype is a COLOUR, and which effect
 * a colour holds is decided per run and kept in the item pool. All of this was
 * measured in the image:
 *
 *   pool          Game + 0x242c0                (inlined everywhere, no getter)
 *   effects       pool + 0xa2c    int[15]       indexed by colour
 *   identified    pool + 0xa68    byte[15]      one byte per colour
 *   0x879bf0      ItemPool::GetPillEffect(pool, colour, player)
 *
 * The colour carries a flag: bit 11 (0x800) means a horse pill, and 14 is the
 * gold one. The game strips the flag the same way (0x71ccf0).
 *
 * The effect is asked for THROUGH the game's own call rather than read out of
 * the array, because the call is what applies PHD, Virgo, Lucky Foot and False
 * PHD - with those the same colour holds a different effect, and reading the
 * array would show the wrong one.
 *
 * A colour the player has not identified yet is passed over in silence: the
 * game itself does not tell the player what it is, and neither should we.
 */
#define GAME_OBJECT     0xac3b90
#define ITEM_POOL       0x242c0
#define PILL_IDENTIFIED 0xa68
#define FN_PILL_EFFECT  0x879bf0
#define PILL_COLOUR     0x7ff
#define PILL_HORSE      0x800

static unsigned long player_now(unsigned long b);

static int pill_effect_of(int colour) {
    unsigned long b = shim_image_base();
    if (!b || colour <= 0 || colour > 14) return -1;
    unsigned long game = *(const unsigned long *) (b + GAME_OBJECT);
    if (!game) return -1;
    unsigned long pool = game + ITEM_POOL;
    if (!readable_ptr((const void *) (pool + PILL_IDENTIFIED), 15)) return -1;
    /* -2 is "the player has not learnt this colour yet", which the mod names -
       unless the page asks for unidentified pills' effects (ShowUnidentifiedPill-
       Descriptions) */
    if (!*(const unsigned char *) (pool + PILL_IDENTIFIED + colour)
        && !shim_modset_int(MS_EID_UNIDENTIFIED)) return -2;
    return ((int (*)(unsigned long, int, unsigned long)) (b + FN_PILL_EFFECT))
               (pool, colour, player_now(b));
}

/*
 * Curse of the Blind, which the mod respects and so must we: under it the game
 * itself will not say what is on a pedestal, and a description that did would
 * take the curse apart.
 *
 * The curse mask is an int at Game + 0xc and the blind bit is 1 << 6. Two more
 * guards come with it, both on the pickup itself: +0x560 is set once the
 * player has walked over the item (the game has shown it by then, so it may be
 * described), and +0x562 is the "forced blind" flag the game sets on items it
 * hides for its own reasons.
 */
#define GAME_CURSES     0x0c
#define CURSE_BLIND     (1u << 6)
#define PICKUP_TOUCHED  0x560
#define PICKUP_BLIND    0x562

/*
 * Asked through the game's own getter rather than read out of the field: the
 * raw mask at Level + 0xc is only half the answer, because a seed effect can
 * force a curse on or off and 0x6d02c4 folds both in. The Level is the leading
 * part of the Game object, so the Game pointer IS the Level pointer - the
 * engine passes it that way itself (0x720fc4, the question-mark pedestal).
 */
#define FN_GET_CURSES 0x6d02c4

static int curse_blind(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_OBJECT);
    if (!game || !readable_ptr((const void *) (game + GAME_CURSES), 4)) return 0;
    unsigned mask = ((unsigned (*)(unsigned long)) (b + FN_GET_CURSES))(game);
    return (mask & CURSE_BLIND) != 0;
}

/*
 * Which floor this is, counted the way the game counts it. There is no
 * function to ask: every site works it out inline from the two ints at the
 * head of the Level, adding one on the Repentance path because an alt-path
 * floor sits a level deeper than its number says (0x6d7ebc).
 */
static int absolute_stage(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_OBJECT);
    if (!game || !readable_ptr((const void *) game, 8)) return 0;
    int stage = ((const int *) game)[0], type = ((const int *) game)[1];
    return stage + (((type & ~1) == 4) ? 1 : 0);
}

static int pickup_hidden(unsigned long e) {
    if (!readable_ptr((const void *) (e + PICKUP_BLIND), 1)) return 0;
    if (*(const unsigned char *) (e + PICKUP_BLIND)) return 1;
    /* DisableOnCurse, a row of EID's page: shown under the curse if asked */
    if (!curse_blind() || shim_modset_int(MS_EID_SHOW_BLIND)) return 0;
    return !*(const unsigned char *) (e + PICKUP_TOUCHED);
}

static int identity_of(unsigned long e, unsigned *kind, unsigned *id, int *golden) {
    *golden = 0;
    if (!readable_ptr((const void *) e, ENT_POSITION + 8)) return 0;
    if (!*(const unsigned char *) (e + ENT_EXISTS)) return 0;
    const int *ident = (const int *) (e + ENT_TYPE);

    /*
     * The dice room's floor is not a pickup at all - it is an effect, 1000.76,
     * whose subtype is the face. The mod reads it the same way and counts from
     * one where the entity counts from zero (main.lua, "Handle Dice Room
     * Floor").
     */
    if (ident[0] == EFFECT_TYPE && ident[1] == VAR_DICE_FLOOR) {
        int face = ident[2] + 1;
        if (face < 1 || face > 6) return 0;
        *kind = EID_DICE;
        *id = (unsigned) face;
        return 1;
    }

    /*
     * Card Reading opens three portals, and each one is the card it leads to
     * (main.lua: The Emperor, The Stars, The Moon, and The Hermit in
     * Repentance+). The mod heads them with the item's name; the card's own
     * name says more, and the description is the card's either way.
     */
    if (ident[0] == EFFECT_TYPE && ident[1] == VAR_CARD_PORTAL) {
        static const unsigned CARDS[] = { 18, 5, 19, 10 };
        int which = ident[2];
        if (which < 0 || which >= (int) (sizeof(CARDS) / sizeof(CARDS[0]))) return 0;
        *kind = EID_CARD;
        *id = CARDS[which];
        return 1;
    }

    /*
     * The crane game keeps the item it will give inside itself, so the mod can
     * say what is worth paying for. It is a slot entity (type 6, variant 16)
     * and the prize is an int at +0x570; anything outside a sane id means the
     * prize has not been decided yet.
     */
    if (ident[0] == SLOT_TYPE && ident[1] == VAR_CRANE) {
        if (!readable_ptr((const void *) (e + SLOT_PRIZE), 4)) return 0;
        int prize = *(const int *) (e + SLOT_PRIZE);
        if (prize <= 0 || prize > 4096) return 0;
        *kind = EID_COLLECTIBLE;
        *id = (unsigned) prize;
        return 1;
    }

    if (ident[0] != PICKUP_TYPE) return 0;
    int variant = ident[1], subtype = ident[2];
    /* a TMTRAINER item: its subtype is a negative id (eid_glitch.c) */
    if (variant == VAR_COLLECTIBLE && subtype < 0) {
        *kind = EID_GLITCHED;
        *id = (unsigned) subtype;
        return 1;
    }
    if (subtype <= 0) return 0;
    if (variant == VAR_PILL) {
        int effect = pill_effect_of(subtype & PILL_COLOUR);
        *kind = (subtype & PILL_HORSE) ? EID_HORSEPILL : EID_PILL;
        if (effect == -2) { *id = 0; return 1; }     /* not learnt: the mod says so */
        if (effect < 0) return 0;
        /* the database counts its pills from one, the engine from zero */
        *id = (unsigned) effect + 1;
        return 1;
    }
    /* both halves of the broken shovel describe the item they make (550) */
    if (variant == VAR_SHOVEL) {
        *kind = EID_COLLECTIBLE;
        *id = 550;
        return 1;
    }

    switch (variant) {
        case VAR_COLLECTIBLE: *kind = EID_COLLECTIBLE; break;
        case VAR_TRINKET:     *kind = EID_TRINKET; break;
        case VAR_CARD:        *kind = EID_CARD; break;
        default: return 0;
    }
    /* the golden-trinket flag lives high in the subtype and is not an id */
    if (*kind == EID_TRINKET && (subtype & TRINKET_GOLDEN)) *golden = 1;
    *id = (unsigned) (subtype & 0x7fff);
    return 1;
}

/*
 * The list has to be asked whether it is EMPTY first, and this is the second
 * time that has cost a crash.
 *
 * The game's own accessor (0x4da574) reads the vector's begin and end at
 * +0x10 and +0x18, works out the count, clamps the index against it - and then
 * loads from begin anyway:
 *
 *     ldp x8, x9, [x0, #0x10] ; sub x9, x9, x8 ; lsr x9, x9, #3
 *     cmp w9, w19 ; csel w9, w19, wzr, hi ; ldr x0, [x8, w9, uxtw #3]
 *
 * With no players yet - in the menu, and between "new run" and the first room
 * being built - begin is null and that last load is a fault at 0x4da5bc. The
 * engine even prints a warning first and reads regardless. So the emptiness is
 * checked here, before the call.
 */
#define LIST_BEGIN 0x10                     /* the vector inside the player manager */
#define LIST_END   0x18

static unsigned long player_now(unsigned long b) {
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return 0;
    unsigned long list = game + GAME_PLAYERS;
    if (!readable_ptr((const void *) (list + LIST_BEGIN), 16)) return 0;
    unsigned long first = *(const unsigned long *) (list + LIST_BEGIN);
    unsigned long past = *(const unsigned long *) (list + LIST_END);
    if (!first || past <= first) return 0;          /* nobody is playing yet */
    unsigned long (*player)(unsigned long, int) =
        (unsigned long (*)(unsigned long, int)) (b + FN_PLAYER);
    return player(list, 0);
}

/* for eid_glitch.c and eid_craft.c, which read the same database and game */
const char *shim_eid_row(unsigned kind, unsigned id) { return text_of(kind, id); }
int shim_eid_readable(const void *p, unsigned long n) { return readable_ptr(p, n); }
unsigned long shim_eid_list_now(void) { return g_list; }

/* every player in the run, as the game lists them */
int shim_eid_players(unsigned long *out, int max) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return 0;
    unsigned long list = game + GAME_PLAYERS;
    if (!readable_ptr((const void *) (list + LIST_BEGIN), 16)) return 0;
    unsigned long first = *(const unsigned long *) (list + LIST_BEGIN);
    unsigned long past = *(const unsigned long *) (list + LIST_END);
    if (!first || past <= first) return 0;
    int n = (int) ((past - first) / 8);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = ((const unsigned long *) first)[i];
    return n;
}

/*
 * CO-OP (players' reports on 1.73: "EID works for nobody but the host"). The
 * mod describes for one player per controller (main.lua setPlayer, 1068-1091,
 * coopMainPlayers): every player without a parent, the first one on each
 * ControllerIndex, in the game's order - with CoopDescriptions on, its
 * default (eid_config.lua 103). The game's own rule for "a player of his own"
 * (coop.c own_player: no parent, and of twins the one first in the order) is
 * the mod's "Parent == nil", and the device at +0x19ec is its ControllerIndex.
 *
 * One player of his own - a single player, Jacob and Esau, T.Forgotten - is
 * player 0 exactly as before: nothing changes outside co-op.
 *
 * Who the panel belongs to then decides what pages it and how the hint reads
 * (below, PAGES): the bag's player for the recipes (eid_craft.c), the player
 * who found the thing for a walk-up description, the list's player while the
 * inventory is up (items.c).
 */
#define SEAT_AT   0x19ec                    /* the player's device: PLAYER_DEVICE below */
#define MAIN_MAX  8
int shim_coop_own_player(unsigned long p);

static int seat_of(unsigned long p) {
    if (!p || !readable_ptr((const void *) (p + SEAT_AT), 4)) return -1;
    return *(const int *) (p + SEAT_AT);
}

static int main_players(unsigned long *out, int max) {
    unsigned long all[MAIN_MAX];
    int n = shim_eid_players(all, MAIN_MAX), own = 0;
    if (n <= 0 || max <= 0) return 0;
    for (int i = 0; i < n; i++) own += all[i] && shim_coop_own_player(all[i]);
    if (own <= 1) { out[0] = all[0]; return 1; }
    int seat[MAIN_MAX], k = 0;
    for (int i = 0; i < n && k < max; i++) {
        if (!all[i] || !shim_coop_own_player(all[i])) continue;
        int d = seat_of(all[i]), seen = 0;
        for (int j = 0; j < k; j++) if (seat[j] == d) seen = 1;
        if (seen) continue;
        seat[k] = d;
        out[k++] = all[i];
    }
    return k;
}

int shim_eid_main_players(unsigned long *out, int max) { return main_players(out, max); }

/* is p still one of the run's players: a pointer kept from an earlier frame is asked first */
static int known_player(unsigned long p) {
    unsigned long all[MAIN_MAX];
    int n = shim_eid_players(all, MAIN_MAX);
    for (int i = 0; i < n; i++) if (p && all[i] == p) return 1;
    return 0;
}

static unsigned long g_found_by;            /* whose walk-up nearest() found */
static unsigned long g_panel_by;            /* whose panel is up: the walk-up's, the bag's */
static unsigned long g_list_by;             /* whose inventory list is up (items.c) */

/* the player the panel on screen belongs to; player 0 when there is no other */
static unsigned long panel_player(unsigned long b) {
    if (g_panel_by && known_player(g_panel_by)) return g_panel_by;
    return player_now(b);
}

/* the player what is drawn now belongs to: the list's while it is up, else the panel's */
static unsigned long subject_player(unsigned long b) {
    if (g_list_by && known_player(g_list_by)) return g_list_by;
    return panel_player(b);
}

unsigned long shim_eid_panel_player(void) {
    unsigned long b = shim_image_base();
    return b ? panel_player(b) : 0;
}

/* items.c: the list is up for p (0: put away) */
void shim_eid_list_player(unsigned long p) { g_list_by = p; }

/* p's device (+0x19ec), -1 when unreadable: Esau and Jacob share one, as do T.Forgotten's two */
int shim_eid_player_seat(unsigned long p) { return seat_of(p); }

/* the device class of p's own device (devices.c); DEVICE_NONE when unreadable */
int shim_eid_player_device_kind(unsigned long p) {
    int d = seat_of(p);
    return d == -1 ? DEVICE_NONE : shim_device_kind(d);
}

/*
 * The nearest describable pickup, or nothing at all. Called between frames,
 * where the list is settled and nobody is drawing from it.
 */
/*
 * The sacrifice room's spikes, which are not entities at all.
 *
 * They live in the room's grid, and how many times the player has already paid
 * is kept on the spikes themselves - the engine increments it in the sacrifice
 * routine (0x6b78c0: read +0x1c, add one, store). All of it measured:
 *
 *   room        *(Game + 0x21550)
 *   room type   room + 0x10        13 is the sacrifice room, 21 the dice room
 *   grid        room + 0x30        448 GridEntity*, live count is W*H
 *   W, H        room + 0x14/0x18   the room's size plus the wall ring
 *   type        grid entity + 0x08   8 plain spikes, 9 the ones that retract
 *   VarData     grid entity + 0x1c   how many sacrifices have been made
 *
 * A cell's world position is not asked for: the engine's own GetGridIndex
 * (0x5a18ac) inverts exactly (col*40+40, row*40+120), so the same arithmetic
 * gives the position back without a call and without an ABI to get wrong.
 */
#define ROOM_IN_GAME    0x21550
#define ROOM_TYPE       0x10
#define ROOM_W          0x14
#define ROOM_H          0x18
#define ROOM_GRID       0x30
#define ROOM_SACRIFICE  13
#define GRID_CELLS      448
#define GRID_TYPE       0x08
#define GRID_VARIANT    0x0c
#define SANGUINE_VARIANT 100                /* the spikes Sanguine Bond grows */
#define SANGUINE_ITEM   692                 /* and the item that grew them */
#define GRID_VARDATA    0x1c
#define GRID_SPIKES     8
#define GRID_SPIKES_ONOFF 9
#define SACRIFICE_STEPS 12                  /* how many the database carries */

static int spikes_near(const float *me, float *distance, unsigned *id, int *sanguine) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_OBJECT);
    if (!game || !readable_ptr((const void *) (game + ROOM_IN_GAME), 8)) return 0;
    unsigned long room = *(const unsigned long *) (game + ROOM_IN_GAME);
    if (!room || !readable_ptr((const void *) room, ROOM_GRID + 8)) return 0;
    int w = *(const int *) (room + ROOM_W), h = *(const int *) (room + ROOM_H);
    /*
     * The sacrifice count belongs to the sacrifice room; Sanguine Bond's own
     * spikes are described anywhere, so the room type only decides whether the
     * plain spikes are worth reading (0x6b7850 makes the same distinction).
     */
    int sacrifice_room = *(const int *) (room + ROOM_TYPE) == ROOM_SACRIFICE;
    if (w <= 0 || h <= 0 || w * h > GRID_CELLS) return 0;
    if (!readable_ptr((const void *) (room + ROOM_GRID), (unsigned) (w * h) * 8)) return 0;
    const unsigned long *grid = (const unsigned long *) (room + ROOM_GRID);

    int have = 0;
    for (int i = 0; i < w * h; i++) {
        unsigned long g = grid[i];
        if (!g || !readable_ptr((const void *) g, GRID_VARDATA + 4)) continue;
        int kind = *(const int *) (g + GRID_TYPE);
        if (kind != GRID_SPIKES && kind != GRID_SPIKES_ONOFF) continue;
        /*
         * Sanguine Bond grows its own spikes, and the mod describes those as
         * the item that made them rather than as a sacrifice. The engine marks
         * them by variant: a hundred and up (0x6b785c compares against 100 in
         * the very same sacrifice routine).
         */
        int variant = *(const int *) (g + GRID_VARIANT);
        if (!sacrifice_room && variant < SANGUINE_VARIANT) continue;
        if (variant >= SANGUINE_VARIANT) {
            float sx = (float) (i % w) * 40.0f + 40.0f;
            float sy = (float) (i / w) * 40.0f + 120.0f;
            float sdx = sx - me[0], sdy = sy - me[1];
            float sd = sdx * sdx + sdy * sdy;
            if (sd <= REACH * REACH && (!have || sd < *distance)) {
                *distance = sd;
                *id = SANGUINE_ITEM;
                *sanguine = 1;
                have = 1;
            }
            continue;
        }
        float gx = (float) (i % w) * 40.0f + 40.0f;
        float gy = (float) (i / w) * 40.0f + 120.0f;
        float dx = gx - me[0], dy = gy - me[1];
        float d = dx * dx + dy * dy;
        if (d > REACH * REACH || (have && d >= *distance)) continue;
        int paid = *(const int *) (g + GRID_VARDATA) + 1;
        if (paid < 1) paid = 1;
        if (paid > SACRIFICE_STEPS) paid = SACRIFICE_STEPS;
        *distance = d;
        *id = (unsigned) paid;
        *sanguine = 0;
        have = 1;
    }
    return have;
}

/*
 * Tainted Cain's bag of crafting: eight pickups in, one item out.
 *
 * Both halves are the game's own. The bag is eight bytes on the player at
 * +0x2690 - component ids, and the count is the index of the first zero, so a
 * full bag is eight non-zero bytes. The recipe is 0x5551a8, and it was read
 * before it was called: its first argument is dead, it seeds two RNGs of its
 * own on the stack from the run's start seed, it reads the pools without
 * touching them, and it neither empties the bag nor advances anything. So it
 * can be asked what WOULD come out, which is the whole point of a preview.
 *
 * The one hard rule is that a component byte must be 0..30: the routine
 * indexes two tables with it and checks nothing.
 */
#define MAX_ITEM_ID_BAG 732                 /* the highest collectible in this build */
#define PLAYER_BAG     0x2690
#define BAG_SLOTS      8
#define BAG_MAX_ID     30
#define FN_BAG_RESULT  0x5551a8

/*
 * The picture the mod draws before the name, which is the thing itself rather
 * than a symbol: the item's own artwork, the trinket's, the card's face, the
 * pill's COLOUR - not its effect, because the colour is what is lying there.
 */
static void head_icon(found_t *out, unsigned long e) {
    out->icon[0] = 0;
    const int *ident = (const int *) (e + ENT_TYPE);
    switch (out->kind) {
        case EID_COLLECTIBLE:
            snprintf(out->icon, sizeof(out->icon), "{{Collectible%u}}", out->id);
            break;
        case EID_TRINKET:
            snprintf(out->icon, sizeof(out->icon), "{{Trinket%u}}", out->id);
            break;
        case EID_CARD:
            snprintf(out->icon, sizeof(out->icon), "{{Card%u}}", out->id);
            break;
        case EID_PILL:
        case EID_HORSEPILL:
            snprintf(out->icon, sizeof(out->icon), "{{Pill%d}}",
                     ident[2] & PILL_COLOUR);
            break;
        default:
            break;
    }
}

/*
 * A CARD THAT IS FOR SALE keeps its secret until it is bought.
 *
 * The mod's rule, transcribed (main.lua, "Handle Cards & Runes"):
 *
 *     local hideinShop = closest:ToPickup():IsShopItem() and
 *         ((not isSoulstone and not EID.Config["DisplayCardInfoShop"]) or
 *          (isSoulstone and not EID.Config["DisplaySoulstoneInfoShop"]))
 *     if (isOptionsSpawn or hideinShop or obstructed) ... addQuestionMarkDescription
 *
 * with DisplayCardInfoShop false and DisplaySoulstoneInfoShop true by
 * default. So a card on sale shows a question mark and a soul stone does not.
 * Pills are not touched: DisplayPillInfoShop is true, and neither are
 * collectibles or trinkets - their branches carry no such test at all.
 *
 * IsShopItem is a Lua binding and this image has no Lua, so the field behind
 * it was found by measuring rather than by name: the same card read on a shop
 * plinth and lying on the floor differed in exactly three places, and the
 * image settles which of them is the price. 0x718224(pickup, price) WRITES
 * that field, and the road that makes a shop free - Credit Card - is
 *
 *     005470f0  ldr  w8, [x0, #0x564]      the price
 *     005470f4  cbz  w8, #0x547104         zero: not for sale, skip it
 *     005470fc  bl   #0x718224             otherwise set it to nothing
 *
 * so "for sale" is "the price is not zero", which is the game's own test.
 * Negative prices - the devil's hearts - count as for sale there and count
 * here, which is right: those are on sale too.
 */
#define PICKUP_PRICE    0x564               /* int: what it costs; 0 when it is not for sale */
#define SOULSTONE_FIRST 81                  /* the Soul stones, shown even in a shop */
#define SOULSTONE_LAST  97

/*
 * OPTIONS? (the owner, 24.09.2026: as on the PC). The mod hides a card that
 * came from an Options? choice - DisplayCardInfoOptions? false - and shows a
 * pill - DisplayPillInfoOptions? true: `OptionsPickupIndex > 0`. The field is
 * +0x55c: 0x5ac128, which spawns the Options? pickup, stores there what
 * 0x722914 answers, and that one walks the room's pickups for the highest
 * +0x55c and hands out the next.
 */
#define PICKUP_OPTIONS  0x55c

static int for_sale(unsigned long e) {
    return readable_ptr((const void *) (e + PICKUP_PRICE), 4) && *(const int *) (e + PICKUP_PRICE) != 0;
}

static int card_kept_secret(unsigned long e, unsigned id) {
    if (readable_ptr((const void *) (e + PICKUP_OPTIONS), 4) && *(const int *) (e + PICKUP_OPTIONS) > 0)
        return 1;
    if (!for_sale(e)) return 0;
    /* DisplayCardInfoShop and DisplaySoulstoneInfoShop, rows of EID's page */
    if (id >= SOULSTONE_FIRST && id <= SOULSTONE_LAST) return !shim_modset_int(MS_EID_SHOP_SOULS);
    return !shim_modset_int(MS_EID_SHOP_CARDS);
}

/* DisplayPillInfoShop: a pill on sale, when the page says not to tell */
static int pill_kept_secret(unsigned long e) {
    return for_sale(e) && !shim_modset_int(MS_EID_SHOP_PILLS);
}

/*
 * NOTHING IS TOLD ABOUT A CARD OR A PILL THERE IS NO WAY TO REACH.
 *
 * The mod's rule, transcribed (main.lua, and eid_api.lua for the search):
 *
 *     local obstructed = ((not isSoulstone and not EID.Config["DisplayObstructedCardInfo"]) or
 *         (not EID.Config["DisplayObstructedSoulstoneInfo"] and isSoulstone)) and
 *         (not pathsChecked[closest.InitSeed] and not attemptPathfind(closest))
 *
 *     function EID:EvaluateLocation(gridPosition)      -- may a cell be entered
 *         if gridPosition.x < 1 or gridPosition.y < 1
 *            or gridPosition.x >= height-1 or gridPosition.y >= width - 1 then return false end
 *         local gridIndex = gridPosition.x * room:GetGridWidth() + gridPosition.y
 *         return room:GetGridPath(gridIndex) <= 900
 *     end
 *
 * with DisplayObstructedCardInfo false and DisplayObstructedPillInfo false by
 * default, and DisplayObstructedSoulstoneInfo true - so a soul stone is told
 * about through a wall and a card and a pill are not. Collectibles and
 * trinkets carry no such test in the mod at all.
 *
 * The row and column look swapped in that snippet and are not: the pathfinder
 * it hands them to turns them over itself (luafinding.lua, Initialize:
 * `FindVector(start.Y, start.X)`), so x is the ROW and y the COLUMN by the
 * time EvaluateLocation sees them, and `x * width + y` is the game's own
 * index. Checked before transcribing, because a transposed grid would have
 * been a bug worth not copying.
 *
 * WHERE THE NUMBERS COME FROM, in the image rather than by name - the game
 * carries no Lua, so Room::GetGridPath has none:
 *
 *     0052b8e0  ldr  x21, [x8, #0x21550]   the room, which is where we read it
 *     0052b8ec  bl   #0x5a18ac             the grid index of a position
 *     0052b8f0  cmp  w0, #0x1bf            447: the same 448 cells we walk
 *     0052b8f8  add  x8, x21, w0, uxtw #2
 *     0052b8fc  ldr  w9, [x8, #0xe98]      the cost of that cell
 *     0059ac38  cmp  w8, #0x384            900, the mod's own threshold
 *
 * so the walkability the mod asks for is the int array at room+0xe98, one per
 * cell, and 0x5a18ac is Room::GetGridIndex - (pos.x - 60)/40 and
 * (pos.y - 140)/40, rounded, refused outside the grid, index = width*row+col.
 *
 * The search is a breadth-first walk rather than the mod's A*: on a grid of at
 * most 448 cells the two answer the same question - IS there a way - and a
 * breadth-first walk cannot be wrong about it. Four neighbours, no diagonals,
 * as in the mod. The start cell is never tested, exactly as the mod's
 * pathfinder does not test it (a player standing on spikes still gets to
 * look); the goal is, which is what makes a card lying on spikes silent.
 *
 * FLIGHT switches the whole test off, as it does in the mod
 * (DisableObstructionOnFlight, true by default). The flag has no getter in
 * this image - nothing reads it but the code that keeps it - so it was found
 * where it is WRITTEN, in the item pass:
 *
 *     004f6034  ldrb w21, [x9, #0x1d3]     what it was
 *     004f6244  mov  w1, #0x279            ... Dogma?
 *     004f6250  bl   #0x4eeb74             HasCollectible
 *     004f625c  strb wzr, [x20, #0x1d3]    no: cannot fly
 *     004f6264  mov  w8, #5 ; str [x19, #0x1e0]    and walks the ground
 *     004f65e0  strb w8,  [x20, #0x1d3]    yes: can fly
 *     004f65e8  mov  w8, #3 ; str [x19, #0x1e0]    and passes over
 *     004f65ec  ldrb w8, [x20, #0x1d3] ; cmp w8, w21 ; bl 0x4fe154   changed?
 *
 * The items on those branches are the flight ones and no others - Revelation
 * (643) and Dogma (633), taken from the mod's own English descriptions rather
 * than from memory. The base is the player: x9 = x0 + 0x1781 at 0x4f0f34, so
 * the byte is player + 0x1954, next to luck at 0x1950.
 *
 * The entity's grid collision class next to it (player + 0x1e0, 3 while
 * flying and 5 while not) was the other candidate and was rejected: it is a
 * field every entity has, written from dozens of places with four different
 * values, so on the player it would answer more than the one question.
 */
#define PLAYER_CAN_FLY  0x1954              /* byte: the item pass keeps it, see above */
#define ROOM_PATH       0xe98               /* int per cell: the game's own path cost */
#define PATH_OPEN       900                 /* above this: rock, pit, spikes, grimace */
#define FN_GRID_INDEX   0x5a18ac            /* Room::GetGridIndex(room, const Vector *) */

static int cell_open(const int *path, int i, int w, int h) {
    int row = i / w, col = i % w;
    if (row < 1 || col < 1 || row >= h - 1 || col >= w - 1) return 0;
    return path[i] <= PATH_OPEN;
}

/* is there a way from one cell to another, over cells the game calls walkable */
static int reachable(const int *path, int w, int h, int from, int to) {
    unsigned char seen[GRID_CELLS];
    short queue[GRID_CELLS];
    int head = 0, tail = 0, n = w * h;
    if (from == to) return 1;
    if (!cell_open(path, to, w, h)) return 0;
    memset(seen, 0, (size_t) n);
    seen[from] = 1;
    queue[tail++] = (short) from;
    while (head < tail) {
        int i = queue[head++];
        int row = i / w, col = i % w;
        for (int k = 0; k < 4; k++) {
            static const int DR[4] = { -1, 1, 0, 0 }, DC[4] = { 0, 0, -1, 1 };
            int r = row + DR[k], c = col + DC[k];
            if (r < 0 || c < 0 || r >= h || c >= w) continue;
            int j = r * w + c;
            if (seen[j] || !cell_open(path, j, w, h)) continue;
            if (j == to) return 1;
            seen[j] = 1;
            queue[tail++] = (short) j;
        }
    }
    return 0;
}

/* the mod's question: can the player walk to this thing? */
static int way_is_open(unsigned long b, unsigned long me, unsigned long e) {
    /* a player who can fly is told about everything - the mod's own first line,
       DisableObstructionOnFlight, a row of EID's page */
    if (shim_modset_int(MS_EID_FLIGHT) && readable_ptr((const void *) (me + PLAYER_CAN_FLY), 1)
        && *(const unsigned char *) (me + PLAYER_CAN_FLY))
        return 1;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game || !readable_ptr((const void *) (game + ROOM_IN_GAME), 8)) return 1;
    unsigned long room = *(const unsigned long *) (game + ROOM_IN_GAME);
    if (!room || !readable_ptr((const void *) room, ROOM_PATH + GRID_CELLS * 4)) return 1;
    int w = *(const int *) (room + ROOM_W), h = *(const int *) (room + ROOM_H);
    if (w <= 2 || h <= 2 || w * h > GRID_CELLS) return 1;
    int (*index_of)(unsigned long, const void *) =
        (int (*)(unsigned long, const void *)) (b + FN_GRID_INDEX);
    int from = index_of(room, (const void *) (me + ENT_POSITION));
    int to = index_of(room, (const void *) (e + ENT_POSITION));
    /* off the grid: say nothing rather than hide something wrongly */
    if (from < 0 || to < 0 || from >= w * h || to >= w * h) return 1;
    const int *path = (const int *) (room + ROOM_PATH);
    /*
     * Asked once per (room, player cell, thing) rather than once a frame: the
     * walk is cheap but the panel asks about the same thing sixty times a
     * second and the answer only changes when one of the three does.
     */
    static unsigned long was_room, was_e;
    static int was_from, was_to, was_answer = 1;
    if (room == was_room && from == was_from && to == was_to && e == was_e)
        return was_answer;
    was_room = room; was_e = e; was_from = from; was_to = to;
    was_answer = reachable(path, w, h, from, to);
    return was_answer;
}

/* the nearest describable thing to one player, `me` */
static int nearest_to(found_t *out, unsigned long me) {
    unsigned long b = shim_image_base();
    if (!b || !g_list) return 0;
    if (!readable_ptr((const void *) (g_list + LIST_COUNT), 4)) return 0;
    unsigned count = *(const unsigned *) (g_list + LIST_COUNT);
    unsigned long items = *(const unsigned long *) (g_list + LIST_ITEMS);
    if (!items || count == 0 || count > 4096) return 0;

    if (!me || !readable_ptr((const void *) (me + ENT_POSITION), 8)) return 0;
    const float *mine = (const float *) (me + ENT_POSITION);

    /*
     * The list is one array: asked about once, not once per entity. With a
     * room full of Brimstone lasers that alone was hundreds of questions a
     * frame, and every one of them a miss.
     */
    if (!readable_ptr((const void *) items, (unsigned long) count * 8)) return 0;

    float best = 1e30f;
    int have = 0;
    unsigned long chosen = 0;               /* the thing the panel settled on */
    for (unsigned i = 0; i < count; i++) {
        unsigned long e = *(const unsigned long *) (items + i * 8);
        if (!e) continue;
        unsigned kind = 0, id = 0;
        int golden = 0;
        if (!identity_of(e, &kind, &id, &golden)) continue;
        const float *at = (const float *) (e + ENT_POSITION);
        float dx = at[0] - mine[0], dy = at[1] - mine[1];
        float d = dx * dx + dy * dy;
        if (d > REACH * REACH) continue;
        if (d >= best) continue;
        best = d;
        out->kind = kind;
        out->id = id;
        out->distance = d;
        out->hidden = (kind == EID_COLLECTIBLE || kind == EID_GLITCHED) && pickup_hidden(e);
        out->bag = 0;
        out->golden = golden;
        head_icon(out, e);
        if (kind == EID_CARD && card_kept_secret(e, id)) out->hidden = 1;
        if ((kind == EID_PILL || kind == EID_HORSEPILL) && pill_kept_secret(e)) out->hidden = 1;
        chosen = e;
        have = 1;
    }

    /*
     * A card or a pill with no way to it says nothing - the mod's rule above.
     * Soul stones are exempt, as they are in a shop. The way is the first
     * player's whoever found the thing, as in the mod (main.lua 1180-1187,
     * attemptPathfind: EID.player.CanFly, EID.player.Position).
     */
    if (have && chosen && !out->hidden && !shim_modset_int(MS_EID_UNREACHABLE)
        && (out->kind == EID_PILL || out->kind == EID_HORSEPILL
            || (out->kind == EID_CARD
                && !(out->id >= SOULSTONE_FIRST && out->id <= SOULSTONE_LAST)))
        && !way_is_open(b, player_now(b), chosen))
        out->hidden = 1;

    /*
     * And the spikes, weighed against whatever was found among the entities:
     * a sacrifice room can hold a pickup as well, and the nearer thing wins.
     */
    float spikes = 0.0f;
    unsigned paid = 0;
    int sanguine = 0;
    if (spikes_near(mine, &spikes, &paid, &sanguine) && (!have || spikes < out->distance)) {
        out->kind = sanguine ? EID_COLLECTIBLE : EID_SACRIFICE;
        out->id = paid;
        out->distance = spikes;
        out->hidden = 0;
        snprintf(out->icon, sizeof(out->icon), "%s",
                 sanguine ? "{{Collectible692}}" : "");
        have = 1;
    }
    return have;
}

/*
 * The thing on the panel: the first player's nearest, and only when he is
 * near nothing the next player's, and so on (main.lua 1443-1497: one search
 * per player of coopMainPlayers, in order, and the first description added
 * is the one drawn in the corner - printDescription, 575-576; the rest go
 * "Local", beside their things, which this port does not draw).
 */
static int nearest(found_t *out) {
    unsigned long p[MAIN_MAX];
    int n = main_players(p, MAIN_MAX);
    for (int i = 0; i < n; i++)
        if (nearest_to(out, p[i])) { g_found_by = p[i]; return 1; }
    return 0;
}

/* ------------------------------------------------------------- the drawing */

/*
 * Drawn with the game's own font and the game's own text call, not with a
 * layer of our own.
 *
 * The iOS mod builds its panel out of UIKit - labels, buttons, sliders - which
 * this port has no use for: its UIKit exists only as far as the game needs it,
 * and the owner asked for the HUD's pixel font rather than a system one. Both
 * point the same way, so the text goes through the road the HUD itself uses:
 *
 *     font    Manager + 0x1769f8   (0x578490 picks between this and +0x196a48)
 *     colour  0x8a34f4(dst, r, g, b, a) - twenty bytes, four floats and a flag
 *     draw    0x8be324(font, string, colour, 0, 0, x, y)
 *
 * The position is in canvas units, the same ones the HUD is laid out in, so
 * the panel follows the widened canvas without knowing anything about it.
 */
/*
 * EID's own font by default (-1). The game's four are kept reachable through
 * `eidfont` in port-touch.ini: they are what ruled out the headline face
 * without a rebuild apiece, and -2 loads a GAME file into OUR object, which is
 * the measurement that found the path bug above.
 */
void shim_eid_font(int which) {
    if (which >= -2 && which < FONT_COUNT) g_font_choice = which;
}

/* for the settings writer: one writer puts down every key, so it must be able
   to ask each owner what it is holding (sticks.c) */
int shim_eid_font_choice(void) { return g_font_choice; }
#define FN_KCOLOR       0x8a34f4
#define FN_DRAWSTR      0x8be324

/*
 * A std::string in Apple's layout, which is what the image was built against
 * (cxxstr.c explains the difference at length). A short string lives inside
 * the object; a long one borrows a buffer for the length of the call.
 */
/*
 * The text argument is a PLAIN POINTER, not a std::string, and believing
 * otherwise put rubbish on the screen for two rounds.
 *
 * 0x8be324 hands its second argument straight to 0x8be0c4, and that road
 * begins `ldrb w9, [x1]` - it reads the first BYTE as a letter. Building an
 * Apple std::string and passing the object worked by accident for anything
 * under twenty-three letters, because a short string keeps its letters at the
 * head of the object; a longer one keeps a POINTER there, and the pointer's
 * own bytes were drawn as four or five nonsense letters. That is exactly what
 * the owner photographed, and why Russian - whose lines are nearly always
 * longer - was broken everywhere.
 */
/*
 * Ours by default; `eidfont` in port-touch.ini can still ask for one of the
 * game's, which is how the wrong ones were ruled out. A font that failed to
 * load falls back to the game's rather than to nothing.
 */
unsigned long font_now(unsigned long mgr);

static unsigned long panel_font(unsigned long mgr) {
    if (g_font_choice >= 0) return mgr + FONT_CANDIDATES[g_font_choice];
    /* Korean, Chinese and Vietnamese have faces of their own; the rest use ours */
    unsigned long face = face_for(g_lang_code);
    if (face) return face;
    font_open();
    return g_font_failed ? mgr + FONT_CANDIDATES[0] : (unsigned long) g_font;
}

unsigned long font_now(unsigned long mgr) { return panel_font(mgr); }

/*
 * How far apart the lines go, taken from the font instead of chosen by eye.
 * Font::Load parses the descriptor's common block straight into the object at
 * +0x14, and a dump of the loaded object confirmed it: line height, base,
 * sheet, pages - 11, 9, 1024x1024, 2 - exactly the numbers in eid_default.fnt.
 */
#define LINE_AIR 2.0f                       /* the owner asked for a little more */

/*
 * How tall a line of letters is, which is what every picture is centred on.
 * The font carries it at +0x14, and it is read afresh rather than assumed so
 * that a different font would still line up.
 */
static float glyph_line(void) {
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER) : 0;
    if (!mgr) return 11.0f;
    unsigned long font_now(unsigned long);
    unsigned short h = *(const unsigned short *) (font_now(mgr) + 0x14);
    return (h >= 4 && h <= 64) ? (float) h : 11.0f;
}

unsigned long font_now(unsigned long mgr);

static float line_height(unsigned long font) {
    unsigned short h = *(const unsigned short *) (font + 0x14);
    return ((h >= 4 && h <= 64) ? (float) h : 11.0f) + LINE_AIR;
}

static void draw_text(const char *text, float x, float y,
                      float r, float g, float bl, float a) {
    unsigned long b = shim_image_base();
    if (!b || !text || !*text) return;
    unsigned long mgr = *(const unsigned long *) (b + MANAGER);
    if (!mgr) return;
    /*
     * Ours by default; `eidfont` in port-touch.ini can still ask for one of
     * the game's, which is how the wrong ones were ruled out.
     */
    unsigned long font = panel_font(mgr);

    unsigned char colour[32];
    memset(colour, 0, sizeof(colour));
    ((void (*)(void *, float, float, float, float)) (b + FN_KCOLOR))(colour, r, g, bl, a);

    ((void (*)(unsigned long, const char *, void *, int, int, float, float))
        (b + FN_DRAWSTR))(font, text, colour, 0, 0, x, y);
    /* a joining script: the letters again without their rims, over the rims (FACES) */
    unsigned long twin = g_font_choice >= 0 ? 0 : twin_for(g_lang_code);
    if (twin && font == face_for(g_lang_code))
        ((void (*)(unsigned long, const char *, void *, int, int, float, float))
            (b + FN_DRAWSTR))(twin, text, colour, 0, 0, x, y);
}

/*
 * The game's own BOLD faces, lent to our menus.
 *
 * A settings row must read as part of the game. The game's menu words are
 * hand-drawn sprites, thick-stroked; its font directory carries the same hand
 * in bold at two sizes, teammeatfont16bold and teammeatfont20bold, and those
 * are what our rows and our title are written in. The first attempt used
 * TeamMeatFontExtended, the regular weight, and the owner's screenshot put it
 * next to the real menu: too thin to pass.
 *
 * Each face is loaded once, into an object of ours, with the same two checks
 * font_into() makes for the description fonts. A face that fails falls back
 * to the game's regular one rather than to nothing.
 */
/* and the small regular cut, for a user mod's version and description (tweaks.c) */
static unsigned char g_menu_font[3][FONT_BYTES] __attribute__((aligned(16)));
static signed char g_menu_font_state[3];

static unsigned long menu_font(int which, unsigned long mgr) {
    if (which < 0 || which > 2) which = 0;
    if (!g_menu_font_state[which])
        g_menu_font_state[which] = font_into(g_menu_font[which], MENU_FONT[which], 'A') ? 1 : -1;
    return g_menu_font_state[which] > 0 ? (unsigned long) g_menu_font[which]
                                        : mgr + FONT_CANDIDATES[0];
}

void shim_menu_write(int which, const char *text, float x, float y,
                     float r, float g, float b, float a) {
    unsigned long base = shim_image_base();
    if (!base || !text || !*text) return;
    unsigned long mgr = *(const unsigned long *) (base + MANAGER);
    if (!mgr) return;
    unsigned char colour[32];
    memset(colour, 0, sizeof(colour));
    ((void (*)(void *, float, float, float, float)) (base + FN_KCOLOR))(colour, r, g, b, a);
    ((void (*)(unsigned long, const char *, void *, int, int, float, float))
        (base + FN_DRAWSTR))(menu_font(which, mgr), text, colour, 0, 0, x, y);
}

float shim_menu_text_width(int which, const char *text) {
    unsigned long base = shim_image_base();
    if (!base || !text || !*text) return 0.0f;
    unsigned long mgr = *(const unsigned long *) (base + MANAGER);
    if (!mgr) return 0.0f;
    return (float) ((int (*)(void *, const char *)) (base + 0x8bdec8))(
        (void *) menu_font(which, mgr), text);
}

/*
 * WHICH font object the game itself uses, measured rather than deduced.
 *
 * Two attempts at tracing the call chain landed on the wrong object, and the
 * image loads fonts in over a hundred places, so the question is settled by
 * watching: every distinct font handed to DrawString is reported once, as an
 * offset from the Manager. The counters in the corner are drawn with one of
 * them, and its offset is then the answer.
 */
/*
 * What each candidate font object IS, read out of the object itself.
 *
 * Hooking the game's DrawString to watch which font it uses cost a crash at
 * startup, and the question does not need a hook: Font::Load keeps the file
 * handle at +0x38 (0x8bd548), and a loaded font's glyph table and line height
 * are enough to tell a small pixel face from a headline one. So each candidate
 * is simply measured - is it loaded at all, and how tall are its lines - and
 * the answer goes into the report once.
 */
void shim_eid_font_survey(void) {
    static int done;
    if (done || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long mgr = *(const unsigned long *) (b + 0xac2428);
    if (!mgr) return;
    done = 1;
    int shim_trace_readable(const void *, unsigned long);
    for (int i = 0; i < FONT_COUNT; i++) {
        unsigned long f = mgr + FONT_CANDIDATES[i];
        char line[160];
        if (!shim_trace_readable((const void *) f, 0x60)) {
            snprintf(line, sizeof(line), "%d: Manager + 0x%lx unreadable", i, FONT_CANDIDATES[i]);
        } else {
            const unsigned char *o = (const unsigned char *) f;
            snprintf(line, sizeof(line),
                     "%d: Manager + 0x%lx  file %p  words %08x %08x %08x %08x",
                     i, FONT_CANDIDATES[i], *(void *const *) (o + 0x38),
                     *(const unsigned *) (o + 0x40), *(const unsigned *) (o + 0x44),
                     *(const unsigned *) (o + 0x48), *(const unsigned *) (o + 0x4c));
        }
        report_line("font", line);
    }
}

void shim_eid_saw_font(unsigned long font) {
    if (!shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long mgr = *(const unsigned long *) (b + 0xac2428);
    if (!mgr || font < mgr || font - mgr > 0x400000) return;
    static unsigned long seen[16];
    static int n;
    unsigned long off = font - mgr;
    for (int i = 0; i < n; i++) if (seen[i] == off) return;
    if (n >= 16) return;
    seen[n++] = off;
    char line[80];
    snprintf(line, sizeof(line), "Manager + 0x%lx", off);
    report_line("font", line);
}

/* ------------------------------------------------------------- the overlay */

/*
 * Tied to the game's own Extra HUD setting, as the owner asked.
 *
 * The row is read by the options menu at 0x6e3e90, which loads the value from
 * the settings block at +0x55f08: 0 is off, 1 is mini, 2 is full. Off means
 * silence here as well - nothing is read and nothing is drawn.
 */
#define EXTRA_HUD 0x55f08
#define HUD_OFFSET_F 0x55f0c                /* float: 1 flush, 0 hard into the corner */
#define PLAYER_TYPE  0x1738

#define MOD_EID 7                           /* shim_tweaks_mod: the MODS page's last row */

/*
 * Is EID on: its switch on the MODS page, read live - the page changes it
 * without a restart. Everything of the mod's asks here: the panel, the Bag of
 * Crafting's screen and its finger (eid_craft.c), the inventory on a held map
 * (items.c).
 */
int shim_tweaks_mod(int i);
int shim_eid_on(void) { return shim_tweaks_mod(MOD_EID) != 0; }

static int extra_hud(unsigned long b) {
    const unsigned char *settings = *(const unsigned char *const *) (b + MANAGER);
    if (!settings) return 0;
    return *(const int *) (settings + EXTRA_HUD);
}

/* ------------------------------------------------------------- the markup */

/*
 * A description is not plain text. It carries markers in double braces, and
 * they are most of what makes the panel readable:
 *
 *   {{Damage}}       an icon out of one of the mod's four atlases
 *   {{ColorGreen}}   the colour of everything after it, until
 *   {{CR}}           which puts the colour back
 *   {{Collectible5}} the item's own sprite, out of the game's atlas
 *
 * Of the twenty thousand markers in the text we ship, all but fourteen hundred
 * are the first two kinds. The tables are generated from the mod's own Lua by
 * tools/eid_icons.py - names, animations, frames, sizes and colours - so there
 * is nothing to parse on the phone and nothing to keep in step by hand.
 */
#include "eid_icons.h"

#define COUNT(a) ((int) (sizeof(a) / sizeof((a)[0])))

/* the tables are written out sorted, so a name is found by halving */
static int name_cmp(const char *row, const char *name, size_t n) {
    int c = strncmp(row, name, n);
    if (c) return c;
    return row[n] ? 1 : 0;
}

static const eid_icon_t *icon_named(const char *name, size_t n) {
    int lo = 0, hi = COUNT(EID_ICONS) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = name_cmp(EID_ICONS[mid].name, name, n);
        if (!c) return &EID_ICONS[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/*
 * The colours the mod ANIMATES.
 *
 * Golden trinket lines, the Car Battery's and BFFS!'s conditional lines, Tarot
 * Cloth's - all of them are written in a colour that walks between two shades
 * instead of standing still (eid_data.lua, SwagColors): eighty frames for a
 * full round, forty for the purple one, and a straight interpolation between
 * the shades in between. The static table cannot hold those, so they are their
 * own small table and the walk is worked out when the line is drawn.
 *
 * The frame is our own count of panel draws, not the game's: the game's frame
 * counter is not read anywhere else here, and a panel is drawn once a frame.
 */
static unsigned g_panel_frame;

typedef struct {
    const char *name;
    unsigned char shades, ticks;
    float shade[6][4];
} eid_shimmer_t;

static const eid_shimmer_t EID_SHIMMER[] = {
    { "ColorGold", 2, 80, { { 1.00f, 1.00f, 0.00f, 1.0f }, { 1.00f, 0.54f, 0.00f, 1.0f } } },
    { "BlinkYellowGreen", 2, 80, { { 1.00f, 1.00f, 0.00f, 1.0f }, { 0.00f, 1.00f, 0.00f, 1.0f } } },
    { "BlinkYellowRed", 2, 80, { { 1.00f, 1.00f, 0.00f, 1.0f }, { 1.00f, 0.00f, 0.00f, 1.0f } } },
    { "BlinkGray", 2, 80, { { 0.50f, 0.50f, 0.50f, 1.0f }, { 0.75f, 0.75f, 0.75f, 1.0f } } },
    { "BlinkPink", 2, 80, { { 1.00f, 0.698f, 0.886f, 1.0f }, { 1.00f, 0.50f, 1.00f, 1.0f } } },
    { "BlinkBlue", 2, 80, { { 0.341f, 0.529f, 0.906f, 1.0f }, { 0.592f, 0.717f, 0.937f, 1.0f } } },
    { "BlinkGreen", 2, 80, { { 0.404f, 0.663f, 0.306f, 1.0f }, { 0.443f, 0.765f, 0.247f, 1.0f } } },
    { "BlinkBirthright", 2, 80, { { 0.831f, 0.725f, 0.604f, 1.0f }, { 0.671f, 0.557f, 0.443f, 1.0f } } },
    { "ColorShinyPurple", 2, 40, { { 0.812f, 0.627f, 1.00f, 1.0f }, { 0.62f, 0.251f, 1.00f, 1.0f } } },
    { "ColorRainbow", 6, 80, { { 1.00f, 0.00f, 0.00f, 1.0f }, { 1.00f, 1.00f, 0.00f, 1.0f },
                               { 0.00f, 1.00f, 0.00f, 1.0f }, { 0.00f, 1.00f, 1.00f, 1.0f },
                               { 0.00f, 0.00f, 1.00f, 1.0f }, { 1.00f, 0.00f, 1.00f, 1.0f } } },
};

/* SwagColors, one for one: which two shades, and how far between them */
static const eid_colour_t *shimmer_named(const char *name, size_t n) {
    static eid_colour_t made;
    for (unsigned i = 0; i < COUNT(EID_SHIMMER); i++) {
        const eid_shimmer_t *s = &EID_SHIMMER[i];
        if (name_cmp(s->name, name, n)) continue;
        float span = (float) (s->ticks - 1) / (float) s->shades;
        unsigned at = g_panel_frame % s->ticks;
        unsigned step = (unsigned) ((float) at / (span + 1.0f)) + 1;
        unsigned first = step % (s->shades + 1u);
        unsigned second = (step + 1u) % (s->shades + 1u);
        if (!first) first = 1;
        if (!second) second = 1;
        float part = (float) ((float) at - (float) ((int) ((float) at / (span + 1.0f)) * (span + 1.0f))) / span;
        if (part < 0.0f) part = 0.0f;
        if (part > 1.0f) part = 1.0f;
        const float *a = s->shade[first - 1], *b = s->shade[second - 1];
        made.name = s->name;
        made.r = (b[0] - a[0]) * part + a[0];
        made.g = (b[1] - a[1]) * part + a[1];
        made.b = (b[2] - a[2]) * part + a[2];
        made.a = (b[3] - a[3]) * part + a[3];
        return &made;
    }
    return NULL;
}

static const eid_colour_t *colour_named(const char *name, size_t n) {
    const eid_colour_t *moving = shimmer_named(name, n);
    if (moving) return moving;
    int lo = 0, hi = COUNT(EID_COLOURS) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = name_cmp(EID_COLOURS[mid].name, name, n);
        if (!c) return &EID_COLOURS[mid];
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/*
 * Some icons are written as characters rather than as markers, and the mod
 * turns them into icons before anything else (EID.TextReplacementPairs, in its
 * eid_data.lua): an up arrow, a down arrow, three exclamation marks. The older
 * descriptions use control bytes for the same pictures, and those are still in
 * the text we ship, so they are here as well.
 */
static const struct { const char *from; const char *icon; } EID_TEXT_ICONS[] = {
    { "!!!",              "Warning" },
    { "\xe2\x86\x91",     "ArrowUp" },      /* U+2191 */
    { "\xe2\x86\x93",     "ArrowDown" },    /* U+2193 */
    { "\x01",             "ArrowUp" },      /* the legacy bytes */
    { "\x02",             "ArrowDown" },
    { "\x03",             "Warning" },
    { "\x05",             "Key" },
    { "\x06",             "Heart" },
    { "\x0f",             "Coin" },
    { "\x08\xbd",         "Bomb" },
};

/* the icon written at this place as a character, if there is one */
static const eid_icon_t *text_icon(const char *p, size_t *len) {
    for (int i = 0; i < COUNT(EID_TEXT_ICONS); i++) {
        size_t n = strlen(EID_TEXT_ICONS[i].from);
        if (!strncmp(p, EID_TEXT_ICONS[i].from, n)) {
            *len = n;
            return icon_named(EID_TEXT_ICONS[i].icon, strlen(EID_TEXT_ICONS[i].icon));
        }
    }
    return NULL;
}

/* the mod's own shortcuts, applied before anything is looked up */
static void unalias(const char **name, size_t *n) {
    for (int i = 0; i < COUNT(EID_ALIASES); i++)
        if (!name_cmp(EID_ALIASES[i].from, *name, *n)) {
            *name = EID_ALIASES[i].to;
            *n = strlen(EID_ALIASES[i].to);
            return;
        }
}

/* how much the panel can hold */
#define MAX_PIECES 240
#define PIECE_TEXT 96

/*
 * One icon, through the engine's own sprite - the same road the mod takes on
 * the desktop, where the .anm2 decides which part of which atlas a frame is.
 *
 * Everything here was measured in the image rather than guessed:
 *
 *   0x89c16c  Sprite::Sprite(this)                    the public thunk
 *   0x89fe24  Sprite::Load(this, std::string const *, bool force)
 *   0x89eab8  Sprite::SetFrame(this, const char *animation, int frame)
 *   0x89e4c0  Sprite::Render(this, pos, clampTop, clampBottom)
 *   0x95e7a0  a zero Vector, which is what the clamps are given
 *   +0x158    how large a Sprite is (an array of them steps by this)
 *   +0xe8     its colour: four floats, and Render multiplies it into
 *             every layer
 *   +0x149    the flag Load sets and Render checks - our own proof that
 *             the file was found
 *
 * The position is in canvas pixels, top left, with no transform: two HUD call
 * sites hand Render the canvas size straight from 0xac24d8. That is the same
 * measure the text is drawn in, so the icons sit on the line by themselves.
 *
 * The four sprites are built once and kept. Loading four files sixty times a
 * second would be absurd, and a sprite costs 344 bytes.
 */
#define FN_SPRITE_NEW    0x89c16c
#define FN_SPRITE_LOAD   0x89fe24
#define FN_SPRITE_FRAME  0x89eab8
#define FN_SPRITE_RENDER 0x89e4c0
#define ZERO_VECTOR      0x95e7a0
#define SPRITE_BYTES     0x158
#define SPRITE_COLOUR    0xe8
#define SPRITE_SCALE_AT  0xdc               /* the sprite's own scale, two floats */
#define SPRITE_LOADED    0x149

void *shim_str_assign(void *self, const char *s);
void shim_str_dtor(void *self);

/*
 * THE PAD'S BUTTONS, the fifth sheet (the owner's eighth round, 24.09.2026:
 * the Bag of Crafting's line showed keyboard keys to a player on a pad).
 *
 * The mod draws a button marker - {{ButtonA}}, {{ButtonSelect}}... - from one
 * of two sheets, by the player's device (eid_data.lua, getControllerSprite):
 * ControllerIndex above nought is a pad and gets the game's own
 * gfx/ui/buttons.anm2, animation "XboxOne", the frame numbers the same as the
 * keyboard's in its own inline sheet. Nought is the keyboard. Here the game's
 * device list is 0 Keyboard (the touch screen too), 1 DeviceiOS, 2 Gamepad, and
 * the player's device is at player+0x19ec - the same rule gives the same
 * answer.
 */
#define SHEET_PAD        4
#define SHEET_PAD_PATH   "gfx/ui/buttons.anm2"
#define PAD_ANIM         "XboxOne"
#define PLAYER_DEVICE    0x19ec

static unsigned char g_sheet[5][SPRITE_BYTES] __attribute__((aligned(16)));
static signed char g_sheet_state[5];        /* 0 untried, 1 ready, -1 no good */

static const char *sheet_path(int which) {
    return which == SHEET_PAD ? SHEET_PAD_PATH : EID_SHEETS[which];
}

/* the sheet a marker draws from: a button's picture follows the player's device */
static int sheet_for(const eid_icon_t *icon) {
    if (icon->sheet != 0 || !icon->anim || strcmp(icon->anim, PAD_ANIM)) return icon->sheet;
    unsigned long b = shim_image_base();
    /* co-op: the device of the player the panel or the list belongs to - the hints name his keys */
    unsigned long me = b ? subject_player(b) : 0;
    if (!me || !readable_ptr((const void *) (me + PLAYER_DEVICE), 4)) return icon->sheet;
    int device = *(const int *) (me + PLAYER_DEVICE);
    /* by the device's class (devices.c, batch 2 U4): "> 0" took the touch screen 0x10dead for a pad */
    int pad = shim_device_kind(device) == DEVICE_PAD;
    static int said = -2;
    if (device != said && shim_report_active()) {
        said = device;
        char line[80];
        snprintf(line, sizeof(line), "button pictures for device %#x: %s", (unsigned) device, pad ? "the pad's" : "the keys");
        report_line("eid", line);
    }
    return pad ? SHEET_PAD : icon->sheet;
}

/* the ticks of "XboxOne" in the buttons.anm2 the game loads: the pad pictures there are */
static int pad_length(void) {
    static int n = -2;
    if (n == -2) {
        int shim_port_length(const char *rel, const char *anim);
        n = shim_port_length(SHEET_PAD_PATH, PAD_ANIM);
    }
    return n;
}

static int sheet_ready(int which) {
    if (which < 0 || which >= (int) (sizeof(g_sheet) / sizeof(g_sheet[0]))) return 0;
    if (g_sheet_state[which]) return g_sheet_state[which] > 0;
    unsigned long b = shim_image_base();
    if (!b) return 0;
    g_sheet_state[which] = -1;              /* one attempt, whatever happens */

    unsigned char *s = g_sheet[which];
    memset(s, 0, SPRITE_BYTES);
    ((void (*)(void *)) (b + FN_SPRITE_NEW))(s);

    long long raw[3];
    unsigned char *path = (unsigned char *) raw;
    path[23] = 0;
    shim_str_assign(path, sheet_path(which));
    ((void (*)(void *, const void *, int)) (b + FN_SPRITE_LOAD))(s, path, 1);
    shim_str_dtor(path);

    if (s[SPRITE_LOADED]) g_sheet_state[which] = 1;
    LOG("eid: %s %s", sheet_path(which),
        g_sheet_state[which] > 0 ? "is loaded" : "did not load");
    if (shim_report_active()) {
        char line[120];
        snprintf(line, sizeof(line), "%s %s", sheet_path(which),
                 g_sheet_state[which] > 0 ? "loaded" : "NOT loaded");
        report_line("eid", line);
    }
    return g_sheet_state[which] > 0;
}

/*
 * An item's picture is not in any atlas: the mod builds a sprite of its own,
 * puts the item's file into the second layer of the inline-icon animation and
 * draws the frame called "ItemIcon" (eid_api.lua, createItemIconObject). The
 * first layer is the background, and blanking it is what stops a stray corner
 * of the atlas being drawn behind the item.
 *
 *   0x8a0228  Sprite::ReplaceSpritesheet(this, int layer, std::string const *)
 *   0x8a0284  Sprite::LoadGraphics(this, 0)   - the flag Sprite::Load passes
 *
 * Which item a file belongs to comes from the game's own items.xml, generated
 * into eid_icons.h; the engine keeps the same answer in its item configuration
 * but there is no reason to go looking for it.
 *
 * The sprites are a small ring: an item that is still on screen keeps its
 * slot, and one that has scrolled out of the panel loses it. They are handed
 * out while the panel is being laid out, not while it is being drawn, so a
 * frame never loads a file.
 */
#define FN_SPRITE_REPLACE  0x8a0228
#define FN_SPRITE_GRAPHICS 0x8a0284
#define ITEM_SLOTS 12

/*
 * The item's own picture: the mod's row word for word (eid_api.lua,
 * createItemIconObject builds { "ItemIcon", subtype, 11, 8, -2, -2 }). Where
 * and how large it is drawn is decided by its drawing (fit_of below).
 */
static const eid_icon_t ITEM_ICON = { "ItemIcon", "ItemIcon", 0, 0, 11, 8, -2, -2 };

static unsigned char g_item[ITEM_SLOTS][SPRITE_BYTES] __attribute__((aligned(16)));
static int g_item_key[ITEM_SLOTS];          /* +id an item, -id a trinket, 0 empty */
static unsigned g_item_seen[ITEM_SLOTS];    /* the layout that last wanted it */
static signed char g_item_built[ITEM_SLOTS];
static unsigned g_layout;                   /* which layout we are building */

static const eid_item_t *item_row(int id, int trinket) {
    const eid_item_t *table = trinket ? EID_TRINKETS : EID_COLLECTIBLES;
    int lo = 0, hi = (trinket ? COUNT(EID_TRINKETS) : COUNT(EID_COLLECTIBLES)) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (table[mid].id == id) return &table[mid];
        if (table[mid].id < id) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static const char *item_gfx(int id, int trinket) {
    const eid_item_t *row = item_row(id, trinket);
    return row ? row->gfx : NULL;
}

static int item_slot(int id, int trinket) {
    unsigned long b = shim_image_base();
    const char *gfx = item_gfx(id, trinket);
    if (!b || !gfx) return -1;
    int key = trinket ? -id : id;

    for (int i = 0; i < ITEM_SLOTS; i++)
        if (g_item_built[i] > 0 && g_item_key[i] == key) { g_item_seen[i] = g_layout; return i; }

    int take = -1;
    for (int i = 0; i < ITEM_SLOTS && take < 0; i++)
        if (g_item_seen[i] != g_layout) take = i;
    if (take < 0) return -1;                /* this panel already shows twelve */

    unsigned char *s = g_item[take];
    if (!g_item_built[take]) {
        memset(s, 0, SPRITE_BYTES);
        ((void (*)(void *)) (b + FN_SPRITE_NEW))(s);
        long long raw[3];
        unsigned char *path = (unsigned char *) raw;
        path[23] = 0;
        shim_str_assign(path, EID_SHEETS[0]);
        ((void (*)(void *, const void *, int)) (b + FN_SPRITE_LOAD))(s, path, 1);
        shim_str_dtor(path);
        if (!s[SPRITE_LOADED]) { g_item_built[take] = -1; return -1; }
        g_item_built[take] = 1;
    }

    char want[160];
    long long raw[3];
    unsigned char *path = (unsigned char *) raw;
    void (*replace)(void *, int, const void *) =
        (void (*)(void *, int, const void *)) (b + FN_SPRITE_REPLACE);

    path[23] = 0;
    shim_str_assign(path, "");
    replace(s, 0, path);                    /* no background behind the item */
    shim_str_dtor(path);

    snprintf(want, sizeof(want), "gfx/items/%s/%s",
             trinket ? "trinkets" : "collectibles", gfx);
    path[23] = 0;
    shim_str_assign(path, want);
    replace(s, 1, path);
    shim_str_dtor(path);

    ((void (*)(void *, int)) (b + FN_SPRITE_GRAPHICS))(s, 0);
    g_item_key[take] = key;
    g_item_seen[take] = g_layout;
    return take;
}

/*
 * The description, laid out into the pieces it will be drawn as.
 *
 * Two things happen here that the first version did without, and both are what
 * makes a long description readable: the markers become colours and icons
 * instead of being thrown away, and the text is broken to the width of the
 * panel by MEASURING it in the font it will be drawn in - the engine's own
 * measure, so a wide font, a narrow one and Cyrillic all wrap correctly.
 */
#define FN_TEXT_WIDTH 0x8bdec8              /* the engine's own, named in text.c */

static float measure(unsigned long font, const char *s) {
    unsigned long b = shim_image_base();
    if (!b || !font || !s || !*s) return 0.0f;
    return (float) ((int (*)(void *, const char *)) (b + FN_TEXT_WIDTH))((void *) font, s);
}

/*
 * COLUMNS, not lines. This is what the owner kept asking for and what two
 * earlier attempts got wrong.
 *
 * Lining each line up on its own was not enough: a line that opens with two
 * pictures - an arrow and a heart, say - pushed its words further right than a
 * line that opens with one, and the left edge of the text went ragged down the
 * panel. So the panel is built as columns instead:
 *
 *     0    the first picture of the line, whatever it is - a bullet, an
 *          arrow, a heart, the item's own artwork
 *     12   the second picture, if the line has one
 *     24   the words, on every line, and on every line that wraps
 *
 * Nothing is measured to decide those places; they are the same for every
 * line, which is the whole point.
 */
/*
 * A marker's name into a picture. A name with a number in it is a thing rather
 * than a symbol: {{Collectible105}} is that item's own file, {{Card5}} and
 * {{Pill3}} are frames in the cards-and-pills atlas, and both of those count
 * from one where the atlas counts from zero.
 */
static const eid_icon_t *marker_icon(const char *name, size_t n,
                                     eid_icon_t *made, int *slot) {
    *slot = -1;
    int id = 0;
    size_t head = 0;
    while (head < n && (name[head] < '0' || name[head] > '9')) head++;
    if (head < n) {
        for (size_t i = head; i < n; i++) id = id * 10 + (name[i] - '0');
        if (!name_cmp("Collectible", name, head) || !name_cmp("Trinket", name, head)) {
            *slot = item_slot(id, name[0] == 'T');
            if (*slot >= 0) { *made = ITEM_ICON; return made; }
        } else if (!name_cmp("VirtuesCollectible", name, head)) {
            /*
             * The mod draws the item with a wisp ring around it; the port has
             * no such sheet, so it draws the item. The line still says which
             * item the wisp comes from, which is what the icon is for.
             */
            *slot = item_slot(id, 0);
            if (*slot >= 0) { *made = ITEM_ICON; return made; }
        } else if (!name_cmp("Card", name, head) && id > 0) {
            *made = (eid_icon_t) { "Card", "Cards", 2, (unsigned char) (id - 1), 8, 8, 0, 1 };
            return made;
        } else if (!name_cmp("Pill", name, head) && id > 0) {
            *made = (eid_icon_t) { "Pill", "Pills", 2, (unsigned char) ((id % 2048) - 1),
                                   9, 8, 0, 1 };
            return made;
        }
    }
    return icon_named(name, n);
}

/* ------------------------------------------------------------ the layout */

/*
 * THIS IS THE MOD'S OWN LAYOUT, TRANSCRIBED. Nothing here is a scheme of ours.
 *
 * Every number below is read out of the mod's Lua and named after where it
 * lives there. The earlier attempts built columns of their own and kept
 * disagreeing with the desktop screenshots the owner sent; the way to agree
 * with them is to do what the desktop does, step by step:
 *
 *   printDescription  (main.lua)
 *     the item's picture at (X-3, Y-4) plus the icon's own offsets
 *     the type icon at (X+14, Y+2), the charge right after it at X+15
 *     the name at (X+offsetX, Y-3), offsetX = 14, or 22 with a type icon,
 *     or 23 with a charge as well; then Y += 11
 *     a transformation: its icon at (X, Y) plus offsets, its text at
 *     (X + iconWidth + 4, Y); then Y += max(11, iconHeight)
 *   printBulletPoints (main.lua)
 *     every '#' block is wrapped to the text box (fitTextToWidth, 130)
 *     the first thing of a block - an icon if it starts with one, else the
 *     bullet character 7 - stands at X-3 (icons) or X (the bullet)
 *     the words of every line, wrapped ones too, stand at X+12
 *     {{Indent}} moves the whole block on by 12; Y += 11 per line
 *   replaceMarkupSize (eid_api.lua), with StatChangeIcons off, the default
 *     an icon right after an arrow is dropped: "↑ {{Damage}} +1" is "↑ +1"
 *   renderString / filterIconMarkup (eid_api.lua)
 *     an icon inside a line stands where the words before it end, plus its
 *     own left offset; the words after it move on by the icon's width
 *   fitTextToWidth (eid_api.lua)
 *     words break at spaces; the first 17 units never wrap; a word that
 *     follows a lone icon stays with it
 *
 * The departures are the owner's (24-25.09.2026): every picture placed by
 * its drawing, in one column, one gap from the words (put_picture below).
 */
#define M_NAME_Y        (-3.0f)
#define M_TYPE_Y        2.0f
#define M_FORM_GAP      4.0f                /* iconWidth + 4 (a small transformation icon) */
/*
 * Two heights of line (the owner, 24.09.2026, after a picture of both): the
 * words' lines twelve units - the mod's eleven and a breath - and a line with
 * a big picture twenty, its box of nineteen and one to spare, the extra split
 * above and below it.
 */
#define M_LINE          12.0f
#define M_PIC_LINE      20.0f
#define M_PIC_AIR       ((M_PIC_LINE - M_LINE) * 0.5f)
/*
 * ONE STEP BETWEEN THE PARTS (the owner, as he asked when the mod first went
 * in): from the name to the transformation line, from that line to the
 * description, and from the name straight to the description - the same, and
 * more than the description's own line. Sixteen: the name stands three above
 * its line, so the item's picture and the transformation's, each in its box
 * of nineteen, meet and do not overlap.
 */
#define M_SECTION       16.0f
#define M_FORM_AIR      3.0f                /* a small heading picture's breath, above and below */
/*
 * ACROSS, ONE RULE EVERYWHERE (the owner, 25.09.2026: "some gaps big, some
 * small - make it even"). The mod's widths carry each icon's own margins, so
 * the same "icon, then words" came out at different gaps. Here:
 *   - every leading picture - a bullet's dot, an arrow, an item, the
 *     transformation, the header's picture - stands in ONE column from -2,
 *     centred in it: ten wide for the small ones, nineteen (the big box) for
 *     the big ones;
 *   - the words start three past that column: at 11 after a small one, at 20
 *     after a big one - the header's name the same, and a type mark before
 *     the name three past it too;
 *   - a picture among the words stands one SPACE of the font from its
 *     neighbours on either side, and the spaces the text itself has beside it
 *     are dropped (the owner, 25.09.2026: the stat icons stood far from
 *     everything). The mod's text writes "{{ArrowUp}} {{Speed}} +0.3" and its
 *     icon widths carry negative margins that eat those spaces; here the
 *     drawing's own width is used, so the spaces would add to the gap. Now a
 *     word, a picture and a word stand exactly as three words do, whatever
 *     spaces the text has - and a picture at the very start of the words
 *     stands at their start.
 */
#define M_COL_LEFT      (-2.0f)
#define M_SMALL_COL     10.0f
#define M_GAP           4.0f                /* 3, and one more: "even, but too close" (25.09.2026) */
/* the gap around a picture among the words: one space of the panel's font */
static float inline_gap(void);
static float gap_after(const char *next, float gap);
#define M_ATTACH        1.0f                /* a word written against a picture: its own */
#define M_TEXT_OFFSET   (M_COL_LEFT + M_SMALL_COL + M_GAP)     /* 11: the words after a small column */
#define M_TEXT_BIG      (M_COL_LEFT + 19.0f + M_GAP)           /* 20: after a big one */
#define M_INDENT        12.0f               /* textOffset * numIndentations */
#define M_BOX           130.0f              /* TextboxWidth */
#define M_MIN_LINE      17.0f               /* fitTextToWidth: curLength < 17 */

typedef struct {
    unsigned char is_icon;
    eid_icon_t icon;
    signed char slot;
    float px, py;                           /* from the panel's corner */
    float scale;                            /* the sprite's own scale for this picture */
    float r, g, b, a;
    short row;                              /* the description's row it belongs to; -1 the header (pages) */
    char text[PIECE_TEXT];
} piece_t;

typedef struct { float r, g, b, a; } tint_t;

static piece_t g_piece[MAX_PIECES];
static int g_pieces;
static float g_height;                      /* how far down the panel reaches */
static unsigned long g_lay_font;            /* the font the layout measures with */

/*
 * THE ROWS OF A DESCRIPTION, kept while the walk-up panel is laid out, for
 * its pages (below, "PAGES"): every line the description wraps into, from
 * the top of its room to the top of the next one's, and whether a '#' block
 * begins there. A piece knows its row, so a page is a run of whole rows and
 * nothing is laid out twice.
 */
#define MAX_ROWS 160
typedef struct { float top, bottom; unsigned char block; } row_t;
static row_t g_row[MAX_ROWS];
static int g_rows;                          /* -1: more rows than the table holds - one page */
static int g_rows_on;                       /* the walk-up panel is being laid out */
static int g_cur_row = -1;                  /* the row the pieces made now belong to */

static char g_name[128];
static int g_forget;                        /* lay the panel out again next frame */
static int g_name_error;                    /* the name is a complaint, not a title */

/*
 * What the panel is asked to show - the mod's description object, as much of
 * it as the port draws.
 */
typedef struct {
    const char *picture;                    /* the marker of the thing itself, or "" */
    int type_icon;                          /* 0 none, 1 active, 2 familiar */
    int charge_kind;                        /* 0 a number, 1 timed, 2 special */
    int charges;                            /* the number, when charge_kind is 0 */
    const char *name;                       /* may carry markup, e.g. the quality */
    int name_error;                         /* said in the error colour */
    const char *form_icon;                  /* a transformation's icon marker, or NULL */
    const char *form_text;                  /* "Spun! (0/3)" */
    const char *contents;                   /* a line of markup under the name, or NULL */
    const char *text;                       /* the description, '#' between lines */
} panel_t;

/*
 * EVERY PICTURE BY ITS DRAWING (the owner's tenth round, 24.09.2026).
 *
 * The big pictures - an item's or a trinket's own, a card, a pill, a
 * transformation, a character's head - come in frames that say nothing of
 * their drawing: the Sad Onion is a small drawing in its 32 texels, Brimstone
 * fills them. Each is drawn by its drawing, measured on the desk
 * (tools/eid_boxes.py, eid_boxes.h): at the one scale that gives it the area a
 * typical drawing has in the box (0.549 of the square of its longer side - the
 * median of all 910 items), never past the box, centred in it. The box was 13,
 * and x1.5 at the owner's word: 19.
 *
 * The small pictures - arrows, hearts, pickups, the stat icons - at 1.125,
 * six screen pixels a texel on the owner's phone (a unit is 1440 / 270 =
 * 5.333 there); the stat icons at an arrow's height. Every picture has its
 * drawing's middle on the words' middle - the up arrow's, where the mod puts
 * it.
 *
 * Drawn through the sprite's own scale (+0xdc), which multiplies with the
 * .anm2's.
 */
#include "eid_boxes.h"

#define M_SMALL_SCALE 1.125f
#define M_BIG_BOX     19.0f
#define M_FILL        0.549f
#define M_STATS_SIZE  (9.0f * M_SMALL_SCALE)
#define M_TEXT_MID    EID_ARROW_MID_Y

typedef struct { float x, y, w, h, area; } drawing_t;

static const eid_frame_box_t *frame_box(int sheet, const char *anim, int frame) {
    if (!anim) return NULL;
    for (unsigned i = 0; i < sizeof(EID_FRAME_BOXES) / sizeof(EID_FRAME_BOXES[0]); i++) {
        const eid_frame_box_t *f = &EID_FRAME_BOXES[i];
        if (f->sheet == sheet && f->frame == frame && !strcmp(f->anim, anim)) return f;
    }
    return NULL;
}

static const eid_item_box_t *item_box(int id, int trinket) {
    int lo = 0, hi = (int) (sizeof(EID_ITEM_BOXES) / sizeof(EID_ITEM_BOXES[0])) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const eid_item_box_t *b = &EID_ITEM_BOXES[mid];
        int c = b->trinket != trinket ? (b->trinket < trinket ? -1 : 1) : (b->id < id ? -1 : (b->id > id ? 1 : 0));
        if (!c) return b;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static int is_big(const eid_icon_t *icon, int slot) {
    if (slot >= 0) return 1;                                  /* an item's or a trinket's picture */
    if (!icon || !icon->anim) return 0;
    if (icon->sheet == 1) return !strncmp(icon->anim, "Transformation", 14);
    return icon->sheet == 2 || icon->sheet == 3;              /* cards and pills, heads */
}

/* the drawing at a sprite scale of one: its box from the drawing point, and its area */
static int drawing_of(const eid_icon_t *icon, int slot, drawing_t *d) {
    if (slot >= 0) {
        int key = g_item_key[slot];
        const eid_item_box_t *b = key ? item_box(key > 0 ? key : -key, key < 0) : NULL;
        if (b) { *d = (drawing_t) { b->x, b->y, b->w, b->h, b->area }; return 1; }
        /* one the tables do not have (a glitched item): the whole frame, a typical fill */
        *d = (drawing_t) { 0.0f, 0.0f, 24.0f, 24.0f, M_FILL * 24.0f * 24.0f };
        return 1;
    }
    const eid_frame_box_t *f = icon ? frame_box(icon->sheet, icon->anim, icon->frame) : NULL;
    if (!f) return 0;
    *d = (drawing_t) { f->x, f->y, f->w, f->h, f->area };
    return 1;
}

/*
 * How a picture will be drawn: its scale, and its drawing's box from the
 * drawing point at that scale. A picture with no drawing on record (a blank,
 * a frame the tables lack) keeps the mod's frame and offsets, at the small
 * scale.
 */
typedef struct { float s, x, y, w, h; int known; } fit_t;

static fit_t fit_of(const eid_icon_t *icon, int slot) {
    fit_t f;
    drawing_t d;
    int known = drawing_of(icon, slot, &d);
    if (slot < 0 && icon && sheet_for(icon) != icon->sheet) {
        /* drawn from another sheet (the game's pad buttons): measured there */
        const eid_frame_box_t *fb = frame_box(sheet_for(icon), icon->anim, icon->frame);
        known = fb != NULL;
        if (fb) d = (drawing_t) { fb->x, fb->y, fb->w, fb->h, fb->area };
    }
    if (!known || !(d.w > 0.0f) || !(d.h > 0.0f)) {
        f.s = M_SMALL_SCALE;
        f.x = (float) icon->left * f.s; f.y = (float) icon->top * f.s;
        f.w = (float) icon->width * f.s; f.h = (float) icon->height * f.s;
        f.known = 0;
        return f;
    }
    float longer = d.w > d.h ? d.w : d.h, s;
    if (is_big(icon, slot)) {
        s = sqrtf(M_FILL * M_BIG_BOX * M_BIG_BOX / (d.area > 1.0f ? d.area : 1.0f));
        if (s * longer > M_BIG_BOX) s = M_BIG_BOX / longer;
    } else {
        /* the small ones: 1.125, and none taller or wider than an arrow at that size -
           the stat icons, the pad's buttons (the owner, 25.09.2026: "huge") */
        s = M_SMALL_SCALE;
        if (longer * s > M_STATS_SIZE) s = M_STATS_SIZE / longer;
    }
    f.s = s; f.x = d.x * s; f.y = d.y * s; f.w = d.w * s; f.h = d.h * s; f.known = 1;
    return f;
}

static int g_dry;                           /* measuring only (shim_eid_text_height): no pieces */

static piece_t *piece_new(void) {
    if (g_dry || g_pieces >= MAX_PIECES) return NULL;
    piece_t *p = &g_piece[g_pieces++];
    memset(p, 0, sizeof(*p));
    p->slot = -1;
    p->scale = 1.0f;
    p->row = (short) (g_rows_on ? g_cur_row : -2);
    return p;
}

/* the sprite drawn at exactly (px, py), at this scale */
static void place_scaled(const eid_icon_t *icon, int slot, float px, float py, float scale) {
    piece_t *p = piece_new();
    if (!p) return;
    p->is_icon = 1;
    p->icon = *icon;
    p->slot = (signed char) slot;
    p->px = px;
    p->py = py;
    p->scale = scale;
    p->r = p->g = p->b = p->a = 1.0f;
}

/* the picture with its drawing's left edge at x, its middle on the words' middle of a line at y */
static void put_at(const eid_icon_t *icon, int slot, const fit_t *f, float x, float y) {
    if (!f->known) {
        place_scaled(icon, slot, x + f->x, y + f->y, f->s);
        return;
    }
    place_scaled(icon, slot, x - f->x, y + M_TEXT_MID - f->h * 0.5f - f->y, f->s);
}

/* the picture centred in its column, the column starting at x: big or small by the picture */
static void put_in_column(const eid_icon_t *icon, int slot, float x, float y) {
    fit_t f = fit_of(icon, slot);
    float col = is_big(icon, slot) ? M_BIG_BOX : M_SMALL_COL;
    put_at(icon, slot, &f, x + (col - f.w) * 0.5f, y);
}

/* how far the words go on past a picture among them */
static float icon_advance(const eid_icon_t *icon, int slot) {
    fit_t f = fit_of(icon, slot);
    return f.w + 2.0f * inline_gap();
}

static void place_text(const char *s, size_t n, float px, float py, tint_t c) {
    if (!n) return;
    piece_t *p = piece_new();
    if (!p) return;
    if (n >= PIECE_TEXT) n = PIECE_TEXT - 1;
    while (n && ((unsigned char) s[n] & 0xc0) == 0x80) n--;   /* never half a letter */
    memcpy(p->text, s, n);
    p->text[n] = 0;
    p->px = px;
    p->py = py;
    p->r = c.r; p->g = c.g; p->b = c.b; p->a = c.a;
}

static float text_width(const char *s, size_t n) {
    char buf[PIECE_TEXT];
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    return measure(g_lay_font, buf);
}

/*
 * A marker at p: its name and length, or 0. Colour markers, icons, {{Indent}}
 * and the rest all look the same here; who they are is decided by the caller.
 */
static size_t marker_at(const char *p, const char **name, size_t *n) {
    if (p[0] != '{' || p[1] != '{') return 0;
    const char *end = strstr(p + 2, "}}");
    if (!end) return 0;
    *name = p + 2;
    *n = (size_t) (end - *name);
    unalias(name, n);
    return (size_t) (end + 2 - p);
}

static int is_colour_marker(const char *name, size_t n) {
    return colour_named(name, n) != NULL || !name_cmp("ColorReset", name, n) ||
           !name_cmp("ColorText", name, n);
}

/*
 * renderString: the words of one line at (px, py), the icons among them where
 * the words before them end. The colour in force is carried in and out, the
 * way the mod hands it from line to line.
 */
static float render_string(const char *s, float px, float py, tint_t *colour,
                           tint_t base, int bullet_centring) {
    float x = px;
    const char *p = s;
    const char *run = s;                    /* the words gathered so far */

    #define FLUSH_RUN() do { \
        if (after_picture) { while (run < p && *run == ' ') run++; } \
        if (p > run) { place_text(run, (size_t) (p - run), x, py, *colour); \
                       x += text_width(run, (size_t) (p - run)); placed = 1; after_picture = 0; } \
    } while (0)
    /* the words before a picture among them, without the spaces they end with */
    #define FLUSH_TRIMMED() do { \
        const char *e_ = p; \
        if (after_picture) { while (run < e_ && *run == ' ') run++; } \
        while (e_ > run && e_[-1] == ' ') e_--; \
        if (e_ > run) { place_text(run, (size_t) (e_ - run), x, py, *colour); \
                        x += text_width(run, (size_t) (e_ - run)); placed = 1; after_picture = 0; } \
    } while (0)
    int placed = 0;                         /* anything on the line yet: a picture at its start has no gap before */
    int after_picture = 0;                  /* the last thing laid is a picture among the words: its gap is laid */

    while (*p) {
        const char *name; size_t n;
        size_t took = marker_at(p, &name, &n);
        if (took) {
            int picture = !is_colour_marker(name, n) && name_cmp("Indent", name, n) &&
                          name_cmp("NoLB", name, n) && name_cmp("NoLineBreak", name, n);
            if (picture && !bullet_centring) FLUSH_TRIMMED(); else FLUSH_RUN();
            if (is_colour_marker(name, n)) {
                /*
                 * The stat colours are dropped: green, red and lime say what
                 * the arrow beside the number already says, and a few
                 * languages wrapped the numbers in them, so "+0.3" came out
                 * green beside a green arrow while the rest stood white (the
                 * owner, 19.09.2026: the numbers are white everywhere).
                 */
                const eid_colour_t *c = colour_named(name, n);
                if (c && (!name_cmp("ColorGreen", name, n) || !name_cmp("ColorRed", name, n)
                          || !name_cmp("ColorLime", name, n)))
                    c = NULL;
                if (c) { colour->r = c->r; colour->g = c->g; colour->b = c->b; colour->a = c->a; }
                else *colour = base;
            } else if (name_cmp("Indent", name, n) && name_cmp("NoLB", name, n) &&
                       name_cmp("NoLineBreak", name, n)) {
                eid_icon_t made; int slot = -1;
                const eid_icon_t *icon = marker_icon(name, n, &made, &slot);
                if (icon && icon->width > 0) {
                    if (bullet_centring) {
                        /* the leading picture: centred in the one column (put_in_column) */
                        put_in_column(icon, slot, x, py);
                    } else {
                        /* among the words: a space of the font, its drawing, a space */
                        fit_t f = fit_of(icon, slot);
                        float gap = inline_gap();
                        /* the text's space before it becomes a gap; a word written
                           against it ("43{{Coin}}") stays against it - its count */
                        int spaced = p > run && p[-1] == ' ';
                        if (placed) x += spaced ? gap : M_ATTACH;
                        put_at(icon, slot, &f, x, py);
                        x += f.w + gap_after(p + took, gap);
                        placed = 1;
                        after_picture = 1;
                        while (p[took] == ' ') took++;      /* the text's own spaces after it */
                    }
                }
            }
            p += took;
            run = p;
            continue;
        }
        size_t skip = 0;
        const eid_icon_t *arrow = text_icon(p, &skip);
        if (arrow) {                        /* an arrow written as a letter */
            if (bullet_centring) {
                FLUSH_RUN();
                put_in_column(arrow, -1, x, py);
            } else {
                int spaced = p > run && p[-1] == ' ';
                FLUSH_TRIMMED();
                fit_t f = fit_of(arrow, -1);
                float gap = inline_gap();
                if (placed) x += spaced ? gap : M_ATTACH;
                put_at(arrow, -1, &f, x, py);
                x += f.w + gap_after(p + skip, gap);
                placed = 1;
                after_picture = 1;
                while (p[skip] == ' ') skip++;
            }
            p += skip;
            run = p;
            continue;
        }
        p++;
    }
    FLUSH_RUN();
    #undef FLUSH_RUN
    #undef FLUSH_TRIMMED
    return x;
}

/*
 * The gap after a picture among the words, by what follows it (the owner,
 * 25.09.2026: the pad's buttons stood apart from the comma after them). A
 * space, a word or a number - the next thing - stands a gap away; a mark of
 * punctuation or a closing bracket belongs to what came before and stands
 * against the picture, as it would against a word.
 */
static float gap_after(const char *next, float gap) {
    /* past the colour markers: "{{Crafting8}}{{CR}} 3" is a picture, then a space */
    for (;;) {
        const char *name; size_t k;
        size_t took = marker_at(next, &name, &k);
        if (!took || !is_colour_marker(name, k)) break;
        next += took;
    }
    if (*next == ' ') return gap;
    return (*next && strchr(",.;!?)]", *next)) ? M_ATTACH : gap;      /* not the colon: it heads what follows */
}

static float inline_gap(void) {
    static unsigned long font;
    static float gap = 3.0f;
    if (g_lay_font && g_lay_font != font) {
        font = g_lay_font;
        float w = text_width(" ", 1);
        gap = (w > 0.0f && w < 10.0f ? w : 3.0f) + 1.0f;     /* a space and one more, as M_GAP */
    }
    return gap;
}

/* the width a word takes on the line: its letters, and its icons' widths */
static float word_width(const char *s, size_t n) {
    float w = 0.0f;
    const char *p = s, *end = s + n, *run = s;
    while (p < end) {
        const char *name; size_t k;
        size_t took = marker_at(p, &name, &k);
        if (took) {
            w += text_width(run, (size_t) (p - run));
            if (!is_colour_marker(name, k) && name_cmp("Indent", name, k)) {
                eid_icon_t made; int slot = -1;
                const eid_icon_t *icon = marker_icon(name, k, &made, &slot);
                if (icon) w += icon_advance(icon, slot);
            }
            p += took; run = p;
            continue;
        }
        size_t skip = 0;
        const eid_icon_t *arrow = text_icon(p, &skip);
        if (arrow) {
            w += text_width(run, (size_t) (p - run)) + icon_advance(arrow, -1);
            p += skip; run = p;
            continue;
        }
        p++;
    }
    w += text_width(run, (size_t) (p - run));
    return w;
}

/* is this word nothing but one icon (spaces aside)? */
static int lone_icon(const char *s, size_t n) {
    int icons = 0;
    const char *p = s, *end = s + n;
    while (p < end) {
        if (*p == ' ') { p++; continue; }
        const char *name; size_t k;
        size_t took = marker_at(p, &name, &k);
        if (took) { if (!is_colour_marker(name, k)) icons++; p += took; continue; }
        size_t skip = 0;
        if (text_icon(p, &skip)) { icons++; p += skip; continue; }
        return 0;                           /* a letter: not a lone icon */
    }
    return icons == 1;
}

/* does the line begin with a picture (colour markers and spaces aside)? */
static const char *leading_icon(const char *s, size_t *len) {
    const char *p = s;
    for (;;) {
        while (*p == ' ') p++;
        size_t skip = 0;
        if (text_icon(p, &skip)) { *len = skip; return p; }
        const char *name; size_t n;
        size_t took = marker_at(p, &name, &n);
        if (!took) return NULL;
        if (is_colour_marker(name, n)) { p += took; continue; }
        if (!name_cmp("Indent", name, n) || !name_cmp("NoLB", name, n)) { p += took; continue; }
        eid_icon_t made; int slot = -1;
        if (!marker_icon(name, n, &made, &slot)) return NULL;
        *len = took;
        return p;
    }
}

/*
 * fitTextToWidth. Words end at a space (the space stays with the word); the
 * first word never wraps if it is the bullet icon; the first seventeen units
 * of a line never wrap; a word after a lone icon stays with the icon.
 *
 * AND IN CHINESE THERE ARE NO SPACES, which is the other half of the fault the
 * owner reported on 16.09.2026 - "almost every item has part of its text
 * cut off". A whole Chinese sentence came through here as ONE word: nothing to
 * break it at, so it never wrapped, ran past the box and was cut off at the
 * edge. The missing glyphs were only the half that was visible as holes.
 *
 * The mod has the same problem and answers it with a list of languages where a
 * line may break between characters rather than between words
 * (EID.BreakUtf8CharsLanguage, one entry: zh_cn). The same list is kept here,
 * and the same single entry - Japanese is written without spaces too, but the
 * mod does not break it that way and neither do we, because matching the mod
 * is what keeps its measured layout right.
 */
#define MAX_WRAP 12

static size_t utf8_bytes(const char *p) {
    unsigned char c = (unsigned char) *p;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

/*
 * A character a line may be broken before or after. The span covers the CJK
 * radicals, the punctuation, both kana and the unified ideographs; the
 * fullwidth forms are added because Chinese text uses their brackets and
 * commas. ASCII is deliberately outside it, so `{{Tears}}` and a number keep
 * behaving as words.
 */
static int breakable_char(const char *p) {
    size_t n = utf8_bytes(p);
    unsigned cp;
    unsigned char c = (unsigned char) p[0];
    if (n == 2) cp = ((unsigned) (c & 0x1f) << 6) | ((unsigned char) p[1] & 0x3f);
    else if (n == 3) cp = ((unsigned) (c & 0x0f) << 12)
                        | (((unsigned char) p[1] & 0x3f) << 6)
                        | ((unsigned char) p[2] & 0x3f);
    else return 0;
    return (cp >= 0x2e80u && cp <= 0x9fffu)
        || (cp >= 0xf900u && cp <= 0xfaffu)
        || (cp >= 0xff00u && cp <= 0xff60u)
        || (cp >= 0xffe0u && cp <= 0xffe6u);
}

static int fit_to_width(const char *s, float box, char lines[MAX_WRAP][320]) {
    int count = 0;
    if (strstr(s, "{{NoLB}}") || strstr(s, "{{NoLineBreak}}")) {
        char one[320]; size_t at = 0;
        for (const char *p = s; *p && at + 1 < sizeof(one); ) {
            if (!strncmp(p, "{{NoLB}}", 8)) { p += 8; continue; }
            if (!strncmp(p, "{{NoLineBreak}}", 15)) { p += 15; continue; }
            one[at++] = *p++;
        }
        one[at] = 0;
        snprintf(lines[0], 320, "%s", one);
        return 1;
    }
    char cur[320]; size_t cur_at = 0; float cur_len = 0.0f;
    int first_word = 1, prev_lone_icon = 0, prev_label = 0;
    const char *p = s;
    while (*p) {
        const char *w = p;
        if (g_break_chars && breakable_char(p)) {
            p += utf8_bytes(p);             /* one character IS the word here */
        } else {
            while (*p && *p != ' ') {
                /* a run of Latin ends where the Chinese starts */
                if (g_break_chars && p > w && breakable_char(p)) break;
                p += utf8_bytes(p);
            }
        }
        if (*p == ' ') p++;                 /* the space belongs to the word */
        size_t n = (size_t) (p - w);
        float len;
        size_t skip;
        if (first_word && leading_icon(w, &skip) && lone_icon(w, n)) len = 0.0f;
        else len = word_width(w, n);
        first_word = 0;
        if (cur_len + len <= box || cur_len < M_MIN_LINE || prev_lone_icon || prev_label) {
            if (cur_at + n < sizeof(cur)) { memcpy(cur + cur_at, w, n); cur_at += n; }
            cur_len += len;
        } else {
            cur[cur_at] = 0;
            if (count < MAX_WRAP) snprintf(lines[count++], 320, "%s", cur);
            cur_at = 0;
            if (n < sizeof(cur)) { memcpy(cur, w, n); cur_at = n; }
            cur_len = len;
        }
        prev_lone_icon = lone_icon(w, n);
        /* a label - a word that ends in a colon, "Room:" - keeps what it labels on its line */
        {
            size_t k = n;
            while (k && w[k - 1] == ' ') k--;
            prev_label = k && w[k - 1] == ':';
        }
    }
    cur[cur_at] = 0;
    if (count < MAX_WRAP) snprintf(lines[count++], 320, "%s", cur);
    return count;
}

/*
 * RIGHT TO LEFT (Arabic, the owner, 26.10.2026).
 *
 * The Arabic text is already shaped and laid in VISUAL order for a renderer
 * that writes left to right (the "Arabic EID" mod's own form): the first word
 * of a sentence is the rightmost one in the string. Wrapped as it stands, the
 * first line would hold the END of the sentence. So it is wrapped the way the
 * mod wraps it - the leading pictures set aside, the words turned round into
 * reading order, fitted to the box, and each line turned back. The mod leaves
 * a block with colour markup as it is (turning the words round would carry
 * the colour to the wrong ones), and so does this.
 *
 * And the panel reads from the right: the column of pictures - the item's,
 * the bullet's, the arrow's - stands on the right edge and every line of words
 * ends at the same place on that side, the mirror of the left-to-right panel.
 * Each line is laid out as usual and then moved, whole: the order inside it
 * is the text's own visual order and is not touched.
 */
static int lang_rtl(void) { return !strcmp(g_lang_code, "ar"); }

#define M_RIGHT (M_TEXT_OFFSET + M_BOX)     /* where the words wrap to, from the panel's corner */

/* the pieces laid since `from`, which stand at [x, x + w], to the mirror place of that */
static void rtl_mirror(int from, float x, float w) {
    if (!lang_rtl()) return;
    float dx = M_COL_LEFT + M_RIGHT - 2.0f * x - w;
    for (int i = from; i < g_pieces; i++) g_piece[i].px += dx;
}

static int has_arabic(const char *s) {
    for (const unsigned char *p = (const unsigned char *) s; *p; p++)
        if ((*p >= 0xd8 && *p <= 0xdb) || (*p == 0xef && (p[1] >= 0xad && p[1] <= 0xbb))) return 1;
    return 0;
}

/* the words of s in the other order, single spaces between them */
static void words_turned(const char *s, char *out, size_t room) {
    const char *starts[160]; size_t lens[160]; int n = 0;
    for (const char *p = s; *p && n < 160; ) {
        while (*p == ' ') p++;
        if (!*p) break;
        starts[n] = p;
        while (*p && *p != ' ') p++;
        lens[n] = (size_t) (p - starts[n]);
        n++;
    }
    size_t at = 0;
    out[0] = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (at + lens[i] + 2 >= room) break;
        if (at) out[at++] = ' ';
        memcpy(out + at, starts[i], lens[i]);
        at += lens[i];
    }
    out[at] = 0;
}

static int fit_rtl(const char *s, float box, char lines[MAX_WRAP][320]) {
    if (!has_arabic(s) || strstr(s, "{{Color") || strstr(s, "{{CR}}") ||
        strstr(s, "{{NoLB}}") || strstr(s, "{{NoLineBreak}}"))
        return fit_to_width(s, box, lines);
    /* the leading pictures stay at the head of the first line */
    const char *body = s;
    size_t len;
    const char *head;
    while ((head = leading_icon(body, &len)) != NULL) {
        body = head + len;
        while (*body == ' ') body++;
    }
    char turned[1200];
    words_turned(body, turned, sizeof(turned));
    int count = fit_to_width(turned, box, lines);
    for (int i = 0; i < count; i++) {
        char back[320];
        words_turned(lines[i], back, sizeof(back));
        if (i == 0 && body > s) {
            size_t k = (size_t) (body - s);
            if (k > 120) k = 120;
            snprintf(lines[0], 320, "%.*s%s", (int) k, s, back);
        } else {
            snprintf(lines[i], 320, "%s", back);
        }
    }
    return count;
}

/*
 * replaceMarkupSize with StatChangeIcons off: the icon right after an arrow
 * is dropped, and the double space it leaves is closed.
 */
static void drop_stat_icons(const char *s, char *out, size_t room) {
    size_t at = 0;
    const char *p = s;
    while (*p && at + 1 < room) {
        size_t skip = 0;
        int arrow = text_icon(p, &skip) != NULL;
        if (!arrow) {
            const char *name; size_t n;
            size_t took = marker_at(p, &name, &n);
            if (took && (!name_cmp("ArrowUp", name, n) || !name_cmp("ArrowDown", name, n)))
                { arrow = 1; skip = took; }
        }
        if (arrow) {
            if (at + skip + 1 >= room) break;
            memcpy(out + at, p, skip); at += skip; p += skip;
            if (*p == ' ') {
                const char *name; size_t n;
                size_t took = marker_at(p + 1, &name, &n);
                if (took && !is_colour_marker(name, n)) {
                    p += 1 + took;          /* the space and the icon go */
                    if (*p != ' ') { out[at++] = ' '; }
                    continue;
                }
            }
            continue;
        }
        out[at++] = *p++;
    }
    out[at] = 0;
}

/* handleTextIndentation: how many {{Indent}} the block carries, and the block without them */
static int take_indents(const char *s, char *out, size_t room) {
    int n = 0; size_t at = 0;
    for (const char *p = s; *p && at + 1 < room; ) {
        if (!strncmp(p, "{{Indent}}", 10)) { n++; p += 10; continue; }
        if (!strncmp(p, "{{IND}}", 7)) { n++; p += 7; continue; }
        out[at++] = *p++;
    }
    out[at] = 0;
    return n;
}

/* does this piece of markup hold a big picture (a line of it gets M_PIC_LINE) */
static int holds_big(const char *s, size_t n) {
    const char *p = s, *end = s + n;
    while (p < end && *p) {
        const char *name; size_t k;
        size_t took = marker_at(p, &name, &k);
        if (!took) { p++; continue; }
        if (!is_colour_marker(name, k)) {
            eid_icon_t made; int slot = -1;
            const eid_icon_t *icon = marker_icon(name, k, &made, &slot);
            if (icon && is_big(icon, slot)) return 1;
        }
        p += took;
    }
    return 0;
}

/* printBulletPoints, block by block; answers the y after the last line */
static float print_bullet_points(const char *description, float x, float y, tint_t base) {
    char trimmed[1600];
    /* StatChangeIcons, a row of EID's page: kept, or dropped as by default */
    if (shim_modset_int(MS_EID_STAT_ICONS)) snprintf(trimmed, sizeof(trimmed), "%s", description);
    else drop_stat_icons(description, trimmed, sizeof(trimmed));
    const char *p = trimmed;
    while (*p) {
        const char *end = strchr(p, '#');
        size_t n = end ? (size_t) (end - p) : strlen(p);
        char block[1200];
        if (n >= sizeof(block)) n = sizeof(block) - 1;
        memcpy(block, p, n); block[n] = 0;
        p += n; if (*p == '#') p++;
        if (!block[0]) continue;

        char plain[1200];
        float indent = M_INDENT * (float) take_indents(block, plain, sizeof(plain));
        /* a block headed by a big picture has its words past the big column */
        float text_at = M_TEXT_OFFSET;
        {
            size_t len = 0;
            const char *head = leading_icon(plain, &len);
            if (head && holds_big(head, len)) text_at = M_TEXT_BIG;
        }
        char lines[MAX_WRAP][320];
        float box = M_BOX - (text_at - M_TEXT_OFFSET);
        int count = lang_rtl() ? fit_rtl(plain, box, lines) : fit_to_width(plain, box, lines);
        tint_t colour = base;
        /*
         * A block headed by a character's portrait - "{{Player7}} Azazel" -
         * is a heading, the way the transformation line is: the mod writes
         * one before the lines it says about that character (Consolation
         * Prize, Birthright, the health lines). It gets the same breath above
         * and below as the transformation line, M_FORM_AIR; the mod packs it
         * like any other line and the owner found that too tight.
         */
        int portrait = 0;
        int row = -1;
        for (int i = 0; i < count; i++) {
            const char *line = lines[i];
            /* a row of the description: from here to where the next line starts (pages) */
            if (g_rows_on && g_rows >= 0) {
                if (g_rows < MAX_ROWS) {
                    row = g_rows++;
                    g_row[row].top = y;
                    g_row[row].bottom = y;
                    g_row[row].block = i == 0;
                    g_cur_row = row;
                } else {
                    g_rows = -1;                /* too many to page: shown whole, as before */
                    row = -1;
                }
            }
            int big = holds_big(line, strlen(line));
            if (big) y += M_PIC_AIR;
            if (i == 0) {
                size_t len = 0;
                const char *icon = leading_icon(line, &len);
                if (icon) {
                    char one[64];
                    if (len >= sizeof(one)) len = sizeof(one) - 1;
                    memcpy(one, icon, len); one[len] = 0;
                    /* a small head gets the transformation line's breath; a big one its line */
                    portrait = len > 8 && !strncmp(one, "{{Player", 8) && !big;
                    if (portrait) y += M_FORM_AIR;
                    int from = g_pieces;
                    render_string(one, x + M_COL_LEFT + indent, y, &colour, base, 1);
                    rtl_mirror(from, x + M_COL_LEFT + indent, holds_big(one, len) ? M_BIG_BOX : M_SMALL_COL);
                    line = icon + len;      /* the icon is taken out of the line */
                } else {
                    /* the dot in the middle of the small column */
                    float dot = text_width("\a", 1);
                    float at = x + M_COL_LEFT + (M_SMALL_COL - dot) * 0.5f + indent;
                    int from = g_pieces;
                    render_string("\a", at, y, &colour, base, 0);
                    rtl_mirror(from, at, dot);
                }
            }
            while (*line == ' ') line++;
            {
                int from = g_pieces;
                float at = x + text_at + indent;
                float end = render_string(line, at, y, &colour, base, 0);
                rtl_mirror(from, at, end - at);
            }
            y += M_LINE + (big ? M_PIC_AIR : 0.0f);
            if (row >= 0) g_row[row].bottom = y;
        }
        if (portrait) y += M_FORM_AIR;
        if (row >= 0) g_row[row].bottom = y;    /* the heading's breath below is its last row's */
    }
    return y;
}

/* printDescription, the part of it the port draws */
static void panel_layout(const panel_t *d) {
    g_pieces = 0;
    g_layout++;                             /* the item sprites of the last panel age out */
    if (g_rows_on) { g_rows = 0; g_cur_row = -1; }      /* the header is no row (pages) */
    float y = 0.0f;
    tint_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
    const eid_colour_t *cn = colour_named("ColorEIDObjName", 15);
    const eid_colour_t *ce = colour_named("ColorEIDError", 13);
    const eid_colour_t *ct = colour_named("ColorEIDTransform", 17);
    tint_t name_colour = cn ? (tint_t) { cn->r, cn->g, cn->b, cn->a } : white;
    if (d->name_error && ce) name_colour = (tint_t) { ce->r, ce->g, ce->b, ce->a };
    tint_t form_colour = ct ? (tint_t) { ct->r, ct->g, ct->b, ct->a } : white;

    float offset_x = 0.0f;
    float head_air = d->picture && *d->picture && holds_big(d->picture, strlen(d->picture)) ? M_PIC_AIR : 0.0f;
    y += head_air;
    if (d->picture && *d->picture) {
        /* the picture in the one column, on the name's own line; the name past the column */
        tint_t c = white;
        int from = g_pieces;
        render_string(d->picture, M_COL_LEFT, y + M_NAME_Y, &c, white, 1);
        rtl_mirror(from, M_COL_LEFT, head_air > 0.0f ? M_BIG_BOX : M_SMALL_COL);
        offset_x = head_air > 0.0f ? M_TEXT_BIG : M_TEXT_OFFSET;
    }
    /* the type mark, the charge and the name: one line, moved whole (RIGHT TO LEFT) */
    int name_from = g_pieces;
    float name_at = offset_x;
    if (d->type_icon) {
        eid_icon_t made; int slot = -1;
        const char *which = d->type_icon == 1 ? "active" : "familiar";
        const eid_icon_t *icon = marker_icon(which, strlen(which), &made, &slot);
        /* the type mark first, where the name would have been; the name three past it */
        fit_t tf = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0 };
        float tx = offset_x, ty = y + M_NAME_Y;
        if (icon) {
            eid_icon_t bare = *icon; bare.left = 0; bare.top = 0;   /* renderIcon: no offsets */
            tf = fit_of(&bare, -1);
            put_at(&bare, -1, &tf, tx, ty);
        }
        offset_x = tx + tf.w + M_GAP;
        if (d->type_icon == 1) {
            char mark[24];
            if (d->charge_kind == 1) snprintf(mark, sizeof(mark), "Timer");
            else if (d->charge_kind == 2) snprintf(mark, sizeof(mark), "ChargeSpecial");
            else snprintf(mark, sizeof(mark), "%d", d->charges < 0 ? 0 : (d->charges > 12 ? 12 : d->charges));
            const eid_icon_t *num = marker_icon(mark, strlen(mark), &made, &slot);
            if (num && tf.known) {
                /* the mod's: one past the battery's drawing point, the same height - its corner */
                eid_icon_t bare = *num; bare.left = 0; bare.top = 0;
                float bx = tx - tf.x, by = ty + M_TEXT_MID - tf.h * 0.5f - tf.y;
                place_scaled(&bare, -1, bx + 1.0f * tf.s, by, tf.s);
            }
        }
    }
    {
        tint_t c = name_colour;
        float end = render_string(d->name ? d->name : "", offset_x, y + M_NAME_Y, &c, name_colour, 0);
        rtl_mirror(name_from, name_at, end - name_at);
    }
    y += M_SECTION;

    if (d->form_icon && d->form_text) {
        eid_icon_t made; int slot = -1;
        const eid_icon_t *icon = marker_icon(d->form_icon, strlen(d->form_icon), &made, &slot);
        float text_x = M_TEXT_OFFSET;
        if (icon) {
            /* in the one column, the words past it, as every leading picture */
            int from = g_pieces;
            put_in_column(icon, slot, M_COL_LEFT, y);
            rtl_mirror(from, M_COL_LEFT, is_big(icon, slot) ? M_BIG_BOX : M_SMALL_COL);
            if (is_big(icon, slot)) text_x = M_TEXT_BIG;
        }
        tint_t c = form_colour;
        int from = g_pieces;
        float end = render_string(d->form_text, text_x, y, &c, form_colour, 0);
        rtl_mirror(from, text_x, end - text_x);
        y += M_SECTION;
    }

    if (d->contents && *d->contents) {
        tint_t c = white;
        int from = g_pieces;
        float end = render_string(d->contents, M_TEXT_OFFSET, y, &c, white, 0);
        rtl_mirror(from, M_TEXT_OFFSET, end - M_TEXT_OFFSET);
        y += M_LINE;
    }

    if (d->text && *d->text) y = print_bullet_points(d->text, 0.0f, y, white);
    g_height = y;
}

/* ------------------------------------------------------------ the drawing */

/* the frame's corner at exactly (x, y): the layout has already placed it */
static float g_icon_alpha = 1.0f;          /* a faded icon: set around draw_icon */

/* scale: the sprite's own scale for this draw, put back after; 0 leaves the sprite's as it is */
static void draw_icon(const eid_icon_t *icon, int slot, float x, float y, float scale) {
    unsigned long b = shim_image_base();
    if (!b || !icon) return;
    unsigned char *s;
    if (slot >= 0) {
        if (slot >= ITEM_SLOTS || g_item_built[slot] <= 0) return;
        s = g_item[slot];
    } else {
        int sheet = sheet_for(icon);
        if (!sheet_ready(sheet)) sheet = icon->sheet;
        /* a pad picture past the end of the buttons.anm2 the game loads (a user mod's): the key's (M8) */
        if (sheet == SHEET_PAD && icon->frame >= pad_length()) sheet = icon->sheet;
        if (!sheet_ready(sheet)) return;
        s = g_sheet[sheet];
    }
    ((void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME))(s, icon->anim, icon->frame);
    float *colour = (float *) (s + SPRITE_COLOUR);
    colour[0] = colour[1] = colour[2] = 1.0f;
    colour[3] = g_icon_alpha;
    float pos[2] = { x, y };
    float *sc = (float *) (s + SPRITE_SCALE_AT), was0 = sc[0], was1 = sc[1];
    if (scale > 0.0f) { sc[0] = scale; sc[1] = scale; }
    ((void (*)(void *, const void *, const void *, const void *)) (b + FN_SPRITE_RENDER))
        (s, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));
    if (scale > 0.0f) { sc[0] = was0; sc[1] = was1; }
}

static void draw_pieces(float x, float y) {
    /* the mod's Transparency caps the text and the icons alike; ours is 1.0 */
    float opacity = shim_modset_float(MS_EID_OPACITY);
    if (!(opacity > 0.0f) || opacity > 1.0f) opacity = 1.0f;
    for (int i = 0; i < g_pieces; i++) {
        const piece_t *p = &g_piece[i];
        if (p->is_icon) {
            float was = g_icon_alpha;
            g_icon_alpha = was * opacity;
            draw_icon(&p->icon, p->slot, x + p->px, y + p->py, p->scale);
            g_icon_alpha = was;
        } else {
            draw_text(p->text, x + p->px, y + p->py, p->r, p->g, p->b, p->a * opacity);
        }
    }
}

/*
 * A fade is in flight: the screen is going black or coming out of it, and the
 * HUD is not there. The field is the one reset.c already watches before it
 * starts a fade of its own.
 */
#define GAME_FADE 0x1cf44c

/*
 * A timeline of the game's own state, for the report.
 *
 * Two questions need it and neither can be answered by reading the image: WHEN
 * the loading screen is up (the panel is drawn over it, so the gate it uses is
 * not enough), and WHERE our canvas rebuild lands relative to the room being
 * built (the first room comes up without its lighting, which means the rebuild
 * throws away what the room had just put into the effect surfaces).
 *
 * One line whenever any of these changes, with the milliseconds since the
 * previous line: the manager's state, whether a game object exists, the fade,
 * the room, and whether there is a player.
 */
#define MANAGER_STATE 0x204c0
#define GAME_ROOM     0x21550

static int run_stage(void);
static int run_difficulty(void);

static void tell_the_state(void) {
    int shim_report_active(void);
    void report_line(const char *kind, const char *text);
    static int said;
    if (said >= 120 || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    const void *holder = *(const void *const *) (b + MANAGER);
    unsigned long game = *(const unsigned long *) (b + 0xac3b90);
    int state = holder ? *(const int *) ((const char *) holder + MANAGER_STATE) : -1;
    int fade = game ? *(const int *) (game + GAME_FADE) : -1;
    unsigned long room = game ? *(const unsigned long *) (game + GAME_ROOM) : 0;
    /*
     * The player is NOT asked for here, and that cost a crash: the accessor at
     * 0x4da574 walks the player manager, and in the menus there is nothing to
     * walk - it read through a null and took the game down at 0x4da5bc on the
     * sixty-first file of the launch. The four fields below answer the same
     * question without touching anything that may not exist yet.
     */
    static int was_state = -2, was_fade = -2;
    static int was_game, was_room;
    int now_game = game != 0, now_room = room != 0;
    if (state == was_state && fade == was_fade && now_game == was_game
        && now_room == was_room) return;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    long long now_ms = (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
    static long long then_ms;
    said++;
    char line[160];
    snprintf(line, sizeof(line),
             "+%4lld ms: state %d, game %d, fade %d, room %d, floor %d, difficulty %d",
             then_ms ? now_ms - then_ms : 0, state, now_game, fade, now_room,
             run_stage(), run_difficulty());
    then_ms = now_ms;
    was_state = state; was_fade = fade; was_game = now_game; was_room = now_room;
    report_line("state", line);
}

static int screen_fading(unsigned long b) {
    unsigned long game = *(const unsigned long *) (b + GAME_OBJECT);
    if (!game || !readable_ptr((const void *) (game + GAME_FADE), 4)) return 1;
    return *(const int *) (game + GAME_FADE) != 0;
}

/*
 * Called from the overlay's own draw (0x861df8) - the last thing the frame
 * does before the sticks go on top, so the panel sits over the room and under
 * the controls, where a HUD belongs.
 */
void shim_eid_panel_place(float *px, float *py);

/*
 * Is a special seed - an easter egg from the secrets list - on in this run.
 * The run keeps them as a std::set<int> at Game+0x25da0, and the game asks it
 * inline wherever it cares (No HUD at 0x578544): a lower_bound walk from the
 * root, left at +0, right at +8, the value at +0x1c.
 */
#define GAME_SEEDS      0x25da0
#define SEED_NO_HUD     10

static int seed_on(unsigned long b, int id) {
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return 0;
    const unsigned long *end = (const unsigned long *) (game + GAME_SEEDS);
    unsigned long node = end[0], best = (unsigned long) end;
    while (node) {
        if (*(const int *) (node + 0x1c) < id) node = *(const unsigned long *) (node + 8);
        else { best = node; node = *(const unsigned long *) node; }
    }
    return best != (unsigned long) end && *(const int *) (best + 0x1c) == id;
}

/* the same question for the other HUD pieces of the layer (mods.c) */
int shim_eid_seed_on(int id) {
    unsigned long b = shim_image_base();
    return b && seed_on(b, id);
}

/* when the walk-up panel was last drawn (the pages, below: their zone and keys live while it is) */
static long long g_drawn_ns;
static long long pages_drawn_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000000000LL + t.tv_nsec;
}

void shim_eid_draw(void) {
    g_panel_frame++;                        /* the animated colours walk by this */
    unsigned long b = shim_image_base();
    shim_eid_font_survey();
    if (!b || !g_name[0]) return;
    int shim_sticks_in_game(void);
    if (!shim_sticks_in_game() || screen_fading(b)) return;
    int shim_items_open(void);
    if (shim_items_open()) return;          /* the list has that corner now */
    /*
     * EID's own switch on the MODS page (the owner, 23.09.2026) - it used to be
     * the game's EXTRA HUD, the one mod that hung on a switch of the game's.
     */
    if (!shim_eid_on()) return;
    /* HideInBattle, a row of EID's page: the crafting screen's own test */
    int shim_eid_craft_in_battle(void);
    if (shim_modset_int(MS_EID_HIDE_BATTLE) && shim_eid_craft_in_battle()) return;
    /* the "No HUD" egg skips the HUD draw after our hook, so the panel goes too */
    if (seed_on(b, SEED_NO_HUD)) return;

    float x, y;
    shim_eid_panel_place(&x, &y);

    if (shim_report_active()) {
        static int said;
        if (!said) {
            said = 1;
            char line[200];
            snprintf(line, sizeof(line), "panel drawn: %s, %d pieces, hud %d, font %d",
                     g_name, g_pieces, extra_hud(b), g_font_choice);
            report_line("eid", line);
        }
    }
    g_drawn_ns = pages_drawn_now();         /* the pages' zone and keys live while it is drawn */
    draw_pieces(x, y);
}

/*
 * WHERE the panel stands, taken from the mod rather than from a guess - and
 * the list (items.c) asks for the same answer, so the two never disagree.
 *
 * The mod starts at (60, 45) - its own XPosition and YPosition - and then
 * moves the text with the HUD, which is the part we were missing and the
 * reason the panel sat on the coin and bomb counters: the game's HUD Offset
 * option pushes those counters inwards, and the mod follows them (main.lua,
 * CheckPosModifiers):
 *
 *     (20, 12) * (HudOffset - 1) + (0, 3)
 *
 * The setting is the one hud.c already reads for the map's touch zone:
 * Manager+0x55f0c, one flush against the edge, nought hard into the corner. So
 * at nought the panel goes up and left with the HUD, and at one it comes down
 * and right with it.
 *
 * The character adjustments are the mod's too: Tainted Isaac and Tainted ???
 * carry an extra HUD row, Jacob and Esau half of one.
 *
 * TWO numbers here are ours and not the mod's: 32 to the right of its sixty and
 * 4 below its 45 - the owner's own place (24.09.2026, found with the MOVE rows). A
 * phone draws the same HUD on a canvas wider than the mod ever assumed, and at
 * sixty the bullets ran into the coin and bomb counters; at seventy the item
 * pictures and bullets still lay on Stats+'s column of multipliers, which ends
 * at about 79 (the owner's screenshots, 24.09.2026). On top of it, the owner's
 * own move from EID's settings page (MOVE X, MOVE Y), for him to find the
 * place that is then made the default.
 *
 * Not copied: the Greed Mode nudge of eight to the right, and the fourth row
 * of hearts - neither is readable here yet.
 */
/* how wide the panel writes, for whoever lays out beside it (items.c) */
float shim_eid_panel_width(void) { return M_BOX + M_TEXT_OFFSET; }

/* how far down the panel last laid out reaches (items.c: a finger on the bag's screen) */
float shim_eid_panel_height(void) { return g_height; }

void shim_eid_panel_place(float *px, float *py) {
    /* the owner's own place, found with MOVE X / MOVE Y (+8, +4) and made the default */
    float x = 60.0f + 32.0f + (float) shim_modset_int(MS_EID_X);
    float y = 45.0f + 4.0f + (float) shim_modset_int(MS_EID_Y);
    unsigned long b = shim_image_base();
    if (b) {
        const unsigned char *settings = *(const unsigned char *const *) (b + MANAGER);
        float off = 1.0f;
        if (settings && readable_ptr(settings + HUD_OFFSET_F, 4))
            off = *(const float *) (settings + HUD_OFFSET_F);
        if (!(off >= 0.0f && off <= 1.0f)) off = 1.0f;
        x += 20.0f * (off - 1.0f);
        y += 12.0f * (off - 1.0f) + 3.0f;
        unsigned long me = player_now(b);
        if (me && readable_ptr((const void *) (me + PLAYER_TYPE), 4)) {
            int type = *(const int *) (me + PLAYER_TYPE);
            if (type == 21 || type == 25) y += 26.0f;      /* Tainted Isaac, Tainted ??? */
            else if (type == 19) y += 16.0f;               /* Jacob and Esau */
        }
    }
    if (px) *px = x;
    if (py) *py = y;
}

/*
 * Two texts head a room rather than an item: the dice room's effects and the
 * sacrifice room's next payout. They live in the mod's per-language files, not
 * in its description tables, and are generated into eid_icons.h.
 */
static const eid_header_t *headers_now(void) {
    for (int i = 0; i < COUNT(EID_HEADERS); i++)
        if (!strcmp(EID_HEADERS[i].lang, g_lang_code)) return &EID_HEADERS[i];
    for (int i = 0; i < COUNT(EID_HEADERS); i++)
        if (!strcmp(EID_HEADERS[i].lang, "en_us")) return &EID_HEADERS[i];
    return NULL;
}

/* --------------------------------------------------- the transformations */

/*
 * Standing over an item, the mod also says which transformation it counts
 * towards and how close the player is: "Guppy! (2/3)".
 *
 * Both halves are the game's own. Which item counts comes from its tags -
 * playerforms.xml names one tag per form, items_metadata.xml carries the tags -
 * and the names come from the game's string table, so they read exactly as the
 * rest of the game does rather than in the mod's own words. Both are generated
 * into eid_icons.h.
 *
 * The counters are fifteen ints at Entity_Player + 0x1c54, one per form, and
 * three is the threshold: 0x504308 is the engine's own HasPlayerForm and it
 * answers "counter > 2".
 */
#define PLAYER_FORMS 0x1c54
#define FORM_NEEDED  3

static int form_of_item(unsigned id) {
    int lo = 0, hi = COUNT(EID_FORM_ITEMS) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (EID_FORM_ITEMS[mid].id == id) return EID_FORM_ITEMS[mid].form;
        if (EID_FORM_ITEMS[mid].id < id) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/*
 * The game's language against the column it occupies in the string table. The
 * table lists Key first, but the key IS the column, so the strings themselves
 * start at English.
 */
static int form_language_column(void) {
    /* the game has no Arabic: chosen for the descriptions, its names come from the mod */
    if (lang_rtl()) return 9;
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER) : 0;
    int language = mgr ? *(const int *) (mgr + LANGUAGE) : 0;
    switch (language) {
        case 2:  return 1;                  /* Japanese */
        case 11: return 2;                  /* Korean */
        case 13: return 3;                  /* Chinese */
        case 10: return 4;                  /* Russian */
        case 5:  return 5;                  /* German */
        case 4:  return 6;                  /* Spanish */
        case 3:  return 7;                  /* French */
        default: return 0;                  /* English */
    }
}

/*
 * How many items of a transformation the player is carrying RIGHT NOW.
 *
 * The stored counter lags: it is written when the transformation is evaluated,
 * not when the item is picked up, so the panel would show yesterday's number
 * for a moment. The player also keeps a table of how many of each collectible
 * they own - a pointer at +0x1ab8 to one int per id up to 732 - and counting
 * the form's items in it gives the live answer. The larger of the two is what
 * is shown: the table misses progress that was earned and then lost with the
 * item, the counter misses what was just picked up.
 */
#define PLAYER_OWNED   0x1ab8
#define MAX_ITEM_ID    732

/* whose transformations are counted: the first player's, the list's player's while the list asks */
static unsigned long g_form_of;

static int form_owned(int form) {
    unsigned long b = shim_image_base();
    if (!b) return -1;
    unsigned long me = g_form_of ? g_form_of : player_now(b);
    if (!me || !readable_ptr((const void *) (me + PLAYER_OWNED), 8)) return -1;
    const int *counts = *(const int *const *) (me + PLAYER_OWNED);
    if (!counts || !readable_ptr(counts, (MAX_ITEM_ID + 1) * 4)) return -1;
    if (shim_report_active()) {
        static int said;
        if (!said) {
            said = 1;
            long total = 0;
            int bad = 0;
            for (int i = 0; i <= MAX_ITEM_ID; i++) {
                if (counts[i] < 0 || counts[i] > 999) { bad++; continue; }
                total += counts[i];
            }
            unsigned long game = *(const unsigned long *) (b + GAME_OBJECT);
            char line[140];
            snprintf(line, sizeof(line),
                     "curses 0x%x; owned table %d items, %d impossible",
                     game ? *(const unsigned *) (game + GAME_CURSES) : 0,
                     (int) total, bad);
            report_line("eid", line);
        }
    }
    int n = 0;
    for (int i = 0; i < COUNT(EID_FORM_ITEMS); i++) {
        if (EID_FORM_ITEMS[i].form != form) continue;
        int id = EID_FORM_ITEMS[i].id;
        if (id < 0 || id > MAX_ITEM_ID) continue;
        int have = counts[id];
        if (have > 0 && have < 1000) n += have;
    }
    return n;
}

static int form_counter(int form) {
    unsigned long b = shim_image_base();
    if (!b || form < 0 || form >= EID_FORMS) return -1;
    unsigned long me = g_form_of ? g_form_of : player_now(b);
    if (!me || !readable_ptr((const void *) (me + PLAYER_FORMS), EID_FORMS * 4)) return -1;
    int stored = ((const int *) (me + PLAYER_FORMS))[form];
    int live = form_owned(form);
    return (live > stored) ? live : stored;
}

/*
 * Spindown Dice: what the next items would be.
 *
 * Transcribed from the mod rather than invented (eid_modifiers.lua,
 * SpindownDiceCallback, and eid_api.lua, getSpindownResult):
 *
 *   - it is shown on a COLLECTIBLE's description, and only while the player
 *     owns the dice (723); Dad's Note (668) is left alone;
 *   - the rule is one step DOWN the identifier at a time, past anything that
 *     is not a real item, up to ten steps, and nothing below one;
 *   - with Car Battery (356) the dice fires twice, so each result is two
 *     steps rather than one - the mod does the same;
 *   - the line reads "{{Collectible723}} :" and then the results, three of
 *     them by the mod's own default, separated by " ->"; a chain that reaches
 *     Dad's Note stops there.
 *
 * What is deliberately NOT copied: the mod can mark a result the player has
 * not unlocked yet with a question mark, and can print the identifier or the
 * name beside each icon. Both of those are off in the mod's own defaults, and
 * the unlock state is not something this port can read honestly.
 */
#define ITEM_SPINDOWN    723
#define ITEM_BIRTHRIGHT  619
#define ITEM_CAR_BATTERY 356
#define ITEM_DADS_NOTE   668
#define SPINDOWN_RESULTS shim_modset_int(MS_EID_SPINDOWN)   /* 3 by default; a row of the page */

/* how many of an item the player has, not merely whether he has one */
static int owned_count(int id) {
    unsigned long b = shim_image_base();
    if (!b || id < 0 || id > MAX_ITEM_ID) return 0;
    unsigned long me = player_now(b);
    if (!me || !readable_ptr((const void *) (me + PLAYER_OWNED), 8)) return 0;
    const int *counts = *(const int *const *) (me + PLAYER_OWNED);
    if (!counts || !readable_ptr(counts, (MAX_ITEM_ID + 1) * 4)) return 0;
    int n = counts[id];
    return (n > 0 && n < 1000) ? n : 0;
}

static int owns_item(int id) {
    unsigned long b = shim_image_base();
    if (!b || id < 0 || id > MAX_ITEM_ID) return 0;
    unsigned long me = player_now(b);
    if (!me || !readable_ptr((const void *) (me + PLAYER_OWNED), 8)) return 0;
    const int *counts = *(const int *const *) (me + PLAYER_OWNED);
    if (!counts || !readable_ptr(counts, (MAX_ITEM_ID + 1) * 4)) return 0;
    int n = counts[id];
    return n > 0 && n < 1000;
}

/* ------------------------------------------------------- what the run is */
/*
 * Two numbers the mod reads through the modding API and this port reads out of
 * the game's own memory.
 *
 * The DIFFICULTY sits at Game + 0x24f9d8 and is 0 normal, 1 hard, 2 Greed,
 * 3 Greedier. It was found through the game's own IsGreedMode (0x88c994),
 * which is three instructions long -
 *
 *     ldr w8, [x0, #0x24f9d8] ; and w8, w8, #~1 ; cmp w8, #2 ; cset w0, eq
 *
 * - and confirmed against the other fifty-six places that read the same field
 * and compare it with 1, 2 and 3.
 *
 * The FLOOR is the first word of the Game object, because the Level sits at
 * its head: the routine that prints "Level::Init m_Stage %d, m_StageType %d"
 * (the string is at 0xa5f859) reads it as `ldp w8, w9, [x19]`, and the caller
 * hands it the Game. The iOS port's own note that the level's curse mask is at
 * Game + 0x0c says the same thing from the other side.
 */
#define GAME_DIFFICULTY 0x24f9d8
#define GAME_STAGE      0x0
#define GAME_STAGE_TYPE 0x4

static unsigned long game_now(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    return *(const unsigned long *) (b + GAME_PTR);
}

static int run_difficulty(void) {
    unsigned long game = game_now();
    if (!game || !readable_ptr((const void *) (game + GAME_DIFFICULTY), 4)) return -1;
    int said = *(const int *) (game + GAME_DIFFICULTY);
    return (said >= 0 && said <= 3) ? said : -1;
}

static int run_greed(void) {
    int said = run_difficulty();
    return said >= 0 && (said & ~1) == 2;       /* the game's own test, one for one */
}

static int run_hard(void) {
    int said = run_difficulty();
    return said >= 0 && (said & 1) == 1;
}

static int run_stage(void) {
    unsigned long game = game_now();
    if (!game || !readable_ptr((const void *) (game + GAME_STAGE), 4)) return -1;
    int said = *(const int *) (game + GAME_STAGE);
    return (said >= 0 && said <= 13) ? said : -1;
}

/* which character is playing, or -1 when there is nobody to ask about */
static int player_kind(void) {
    unsigned long b = shim_image_base();
    if (!b) return -1;
    unsigned long me = player_now(b);
    if (!me || !readable_ptr((const void *) (me + PLAYER_TYPE), 4)) return -1;
    int type = *(const int *) (me + PLAYER_TYPE);
    return (type >= 0 && type < 64) ? type : -1;
}

/* items.c: which character p is, for the head over his list in co-op; -1 when unreadable */
int shim_eid_player_type(unsigned long p) {
    if (!p || !readable_ptr((const void *) (p + PLAYER_TYPE), 4)) return -1;
    int type = *(const int *) (p + PLAYER_TYPE);
    return (type >= 0 && type < 64) ? type : -1;
}

/* one more piece of a description, with no empty line left at the front */
static void add_piece(char *out, size_t room, const char *piece) {
    if (!piece || !*piece) return;
    size_t at = strlen(out);
    if (!at && *piece == '#') piece++;
    if (at + 1 < room) snprintf(out + at, room - at, "%s", piece);
}

/* one step of the dice: the identifier down, past everything that is not an item */
static int spindown_of(int id) {
    int tries = 0;
    do { id--; tries++; } while (id > 0 && tries <= 10 && !item_row(id, 0));
    return (id > 0 && item_row(id, 0)) ? id : 0;
}

static void spindown_line(unsigned kind, unsigned id, char *out, size_t room) {
    out[0] = 0;
    if (kind != EID_COLLECTIBLE || id == ITEM_DADS_NOTE) return;
    if (!owns_item(ITEM_SPINDOWN) || SPINDOWN_RESULTS <= 0) return;
    int battery = owns_item(ITEM_CAR_BATTERY);
    int n = snprintf(out, room, "#{{Collectible%d}} :", ITEM_SPINDOWN);
    int ref = (int) id, shown = 0;
    for (int i = 1; i <= SPINDOWN_RESULTS && n > 0 && (size_t) n < room - 24; i++) {
        int next = spindown_of(ref);
        if (battery && next > 0 && next != ITEM_DADS_NOTE) next = spindown_of(next);
        ref = next;
        if (ref <= 0) break;
        n += snprintf(out + n, room - (size_t) n, "{{Collectible%d}}", ref);
        shown++;
        if (ref == ITEM_DADS_NOTE) break;
        if (i != SPINDOWN_RESULTS) n += snprintf(out + n, room - (size_t) n, " ->");
    }
    if (!shown) out[0] = 0;               /* nothing below it: no line at all */
}

/* ---------------------------------------------------- what a mimic pays */
/*
 * Blank Card, Clear Rune and Placebo: what the card or the pill costs the item
 * that copies it.
 *
 * One line, transcribed from the mod (eid_modifiers.lua, VariableCharge):
 *
 *     #{{Collectible286}} <phrase> {{4}}{{Battery}}
 *
 * The phrase is the language's own "{1} charge:" with {1} standing for the
 * copying item's name. The mod asks the game's config for the kind of card and
 * for its mimic charge; this port has no modding API to ask with, so both come
 * from the game's own pocketitems.xml through tools/eid_charge.py.
 *
 * The ? card (48) is left out, as the mod leaves it out - it copies whatever
 * it turns into, not itself.
 */
#define ITEM_BLANK_CARD 286
#define ITEM_CLEAR_RUNE 263
#define ITEM_PLACEBO    348
#define ITEM_MOMS_BOX   439
#define CARD_QUESTION   48

enum { CARD_TAROT = 0, CARD_SUIT = 1, CARD_RUNE = 2,
       CARD_SPECIAL = 3, CARD_OBJECT = 4, CARD_TAROT_REVERSE = 5 };

/*
 * What the game's own IsCard means: everything that is not a rune and not one
 * of the three objects (Dice Shard, Emergency Contact, Cracked Key). The
 * numbers are the game's, read out of its parser at 0x73e028.
 */
static int card_is_card(int kind) {
    return kind == CARD_TAROT || kind == CARD_SUIT
        || kind == CARD_SPECIAL || kind == CARD_TAROT_REVERSE;
}

/* Card:IsRune(), for the crafting display: a rune counts as its own ingredient */
int shim_eid_card_is_rune(int id) {
    return id > 0 && id <= EID_CARD_MAX && EID_CARD_KIND[id] == CARD_RUNE;
}

/* the mod's ReplaceVariableStr for the one variable these phrases carry */
static void fill_in(const char *phrase, const char *value, char *out, size_t room) {
    const char *at = strstr(phrase, "{1}");
    if (!at) { snprintf(out, room, "%s", phrase); return; }
    snprintf(out, room, "%.*s%s%s", (int) (at - phrase), phrase, value, at + 3);
}

/* the same for a phrase with two variables */
static void fill_two(const char *phrase, const char *first, const char *second,
                     char *out, size_t room) {
    char once[300];
    fill_in(phrase, first, once, sizeof(once));
    const char *at = strstr(once, "{2}");
    if (!at) { snprintf(out, room, "%s", once); return; }
    snprintf(out, room, "%.*s%s%s", (int) (at - once), once, second, at + 3);
}

static void charge_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    int mimic = -1, item = 0;
    if (f->kind == EID_CARD) {
        if (f->id == CARD_QUESTION || f->id > EID_CARD_MAX) return;
        int kind = EID_CARD_KIND[f->id];
        if (card_is_card(kind) && owns_item(ITEM_BLANK_CARD)) item = ITEM_BLANK_CARD;
        else if (kind == CARD_RUNE && owns_item(ITEM_CLEAR_RUNE)) item = ITEM_CLEAR_RUNE;
        mimic = EID_CARD_CHARGE[f->id];
    } else if (f->kind == EID_PILL || f->kind == EID_HORSEPILL) {
        int effect = (int) f->id - 1;       /* the database counts pills from one */
        if (effect < 0 || effect > EID_PILL_MAX || !owns_item(ITEM_PLACEBO)) return;
        item = ITEM_PLACEBO;
        mimic = EID_PILL_CHARGE[effect];
    }
    if (!item || mimic < 0) return;
    const char *phrase = text_of(EID_PHRASE, PHRASE_VARIABLE_CHARGE);
    if (!phrase || !*phrase) return;
    const char *name = shim_eid_name(EID_COLLECTIBLE, (unsigned) item);
    char said[200];
    fill_in(phrase, name ? name : "", said, sizeof(said));
    snprintf(out, room, "#{{Collectible%d}} %s {{%d}}{{Battery}}", item, said, mimic);
}

/* --------------------------------------------------- golden trinkets */
/*
 * A golden trinket, or any trinket with Mom's Box, works twice; with both, three
 * times. The mod says so by rewriting the numbers in the description
 * (eid_modifiers.lua, GoldenTrinketCallback), and the table telling it WHICH
 * numbers is the mod's own goldenTrinketData, carried into our database by
 * tools/eid_more.py as section 11 - one written-out line per trinket:
 *
 *     t=10,0.4      the numbers to multiply, spelled as the mod spells them
 *     mult=2        the ceiling for the multiplier
 *     mults=1.5,2   the multiplier itself: one doubler, then both
 *     add=1,2,3     what to add after multiplying
 *     flags=3       goldenOnly 1, findReplace 2, append 4, fullReplace 8
 *
 * The words those last three flags need are section 10, the mod's
 * goldenTrinketEffects, its lines separated by one 0x1f.
 *
 * One deliberate difference from the mod: a number inside a {{marker}} is left
 * alone. The mod's search runs over the finished text and can rewrite the 2 in
 * {{Collectible2}}, which turns the marker into rubbish; the port skips those.
 */
#define GOLD_ONLY   1
#define GOLD_FIND   2
#define GOLD_APPEND 4
#define GOLD_FULL   8
#define GOLD_PIECES 9

/* the value of one `key=value` field, or nothing */
static int golden_field(const char *entry, const char *key, char *out, size_t room) {
    size_t klen = strlen(key);
    for (const char *at = entry; at && *at; ) {
        if (!strncmp(at, key, klen) && at[klen] == '=') {
            const char *from = at + klen + 1;
            const char *to = strchr(from, ';');
            size_t n = to ? (size_t) (to - from) : strlen(from);
            if (n >= room) n = room - 1;
            memcpy(out, from, n);
            out[n] = 0;
            return 1;
        }
        at = strchr(at, ';');
        if (at) at++;
    }
    return 0;
}

/* the comma-separated numbers of one field, kept as written and as read */
static int golden_numbers(const char *value, double *v, char spelt[][12], int max) {
    int n = 0;
    while (*value && n < max) {
        const char *end = strchr(value, ',');
        size_t len = end ? (size_t) (end - value) : strlen(value);
        if (len >= sizeof(spelt[0])) len = sizeof(spelt[0]) - 1;
        memcpy(spelt[n], value, len);
        spelt[n][len] = 0;
        v[n] = strtod(spelt[n], NULL);
        n++;
        if (!end) break;
        value = end + 1;
    }
    return n;
}

/* the mod's own words for this trinket, split on the separator */
static int golden_words(int trinket, int own_language, char *store, size_t room,
                        const char *piece[GOLD_PIECES]) {
    const char *whole = own_language ? shim_eid_text(EID_GOLDEN, (unsigned) trinket)
                                     : text_of(EID_GOLDEN, (unsigned) trinket);
    if (!whole || !*whole) return 0;
    snprintf(store, room, "%s", whole);
    int n = 0;
    piece[n++] = store;
    for (char *at = store; *at && n < GOLD_PIECES; at++)
        if (*at == 0x1f) { *at = 0; piece[n++] = at + 1; }
    return n;
}

/* the first occurrence of `find`, replaced; the mod's gsub with a count of one */
static int replace_once(char *text, size_t room, const char *find, const char *with) {
    if (!*find) return 0;
    char *at = strstr(text, find);
    if (!at) return 0;
    size_t head = (size_t) (at - text), skip = strlen(find);
    char tail[3400];
    snprintf(tail, sizeof(tail), "%s", at + skip);
    snprintf(text + head, room - head, "%s%s", with, tail);
    return 1;
}

static void golden_apply(const found_t *f, const char *text, char *out, size_t room) {
    out[0] = 0;
    if (f->kind != EID_TRINKET || !text) return;
    int box = owns_item(ITEM_MOMS_BOX);
    if (!f->golden && !box) return;
    const char *entry = text_of(EID_GOLDEN_DATA, f->id);
    if (!entry || !*entry) return;

    char field[160];
    int flags = golden_field(entry, "flags", field, sizeof(field)) ? atoi(field) : 0;
    if ((flags & GOLD_ONLY) && !f->golden) return;

    /* how much of an effect, and which of the mod's three phrasings */
    double multiplier = 2.0;
    int choice = 1;
    if (f->golden && box) { multiplier = 3.0; choice = 3; }
    else if (box) { choice = 2; }
    double numbers[8];
    char spelt[8][12];
    if (golden_field(entry, "mults", field, sizeof(field))) {
        double m[4]; char s[4][12];
        int n = golden_numbers(field, m, s, 4);
        if (n >= 2) multiplier = (f->golden && box) ? m[1] : m[0];
    } else if (golden_field(entry, "mult", field, sizeof(field))) {
        double m = strtod(field, NULL);
        if ((f->golden && box) || m < 2.0) multiplier = m;
    }
    double addition = 0.0;
    if (golden_field(entry, "add", field, sizeof(field))) {
        double a[4]; char s[4][12];
        int n = golden_numbers(field, a, s, 4);
        int want = box ? choice - 2 : choice - 1;
        if (want >= 0 && want < n) addition = a[want];
    }

    /* the numbers in the description, rewritten where the mod says to */
    int changed = 0;
    size_t at = 0;
    int count = golden_field(entry, "t", field, sizeof(field))
              ? golden_numbers(field, numbers, spelt, 8) : 0;
    for (const char *p = text; *p && at + 32 < room; ) {
        if (p[0] == '{' && p[1] == '{') {           /* a marker is not a number */
            const char *end = strstr(p, "}}");
            size_t n = end ? (size_t) (end + 2 - p) : strlen(p);
            if (at + n >= room) break;
            memcpy(out + at, p, n); at += n; p += n;
            continue;
        }
        int digit = (*p >= '0' && *p <= '9')
                 || (*p == '.' && p[1] >= '0' && p[1] <= '9');
        if (!digit) { out[at++] = *p++; continue; }
        const char *q = p;
        while (*q >= '0' && *q <= '9') q++;
        if (*q == '.' && q[1] >= '0' && q[1] <= '9') { q++; while (*q >= '0' && *q <= '9') q++; }
        char token[12];
        size_t len = (size_t) (q - p);
        if (len >= sizeof(token)) len = sizeof(token) - 1;
        memcpy(token, p, len); token[len] = 0;
        int hit = -1;
        for (int i = 0; i < count && hit < 0; i++)
            if (!strcmp(token, spelt[i])) hit = i;
        if (hit < 0) {
            memcpy(out + at, p, (size_t) (q - p)); at += (size_t) (q - p); p = q;
            continue;
        }
        /* the mod's two hand-made roundings, so a sixth reads as 17% and 33% */
        double v = numbers[hit];
        if (v == 17.0) {
            if (multiplier == 2.0) v = 16.5;
            else if (multiplier == 3.0) v = 100.0 / 6.0;
        } else if (v == 33.0 && (multiplier == 1.5 || multiplier == 3.0)) {
            v = 100.0 / 3.0;
        }
        at += (size_t) snprintf(out + at, room - at, "{{ColorGold}}%.4g{{CR}}",
                                v * multiplier + addition);
        changed++;
        p = q;
    }
    out[at] = 0;

    char store[2400];
    const char *piece[GOLD_PIECES];
    if (flags & GOLD_FIND) {
        int n = golden_words(f->id, 0, store, sizeof(store), piece);
        for (int i = 0; i < n; i += 3) {
            int want = i + (int) multiplier - 1;
            if (want > n - 1) want = n - 1;
            char with[600];
            snprintf(with, sizeof(with), "{{ColorGold}}%s{{ColorText}}", piece[want]);
            changed += replace_once(out, room, piece[i], with);
        }
    }
    if (flags & GOLD_APPEND) {
        int n = golden_words(f->id, 0, store, sizeof(store), piece);
        int want = (n == 2 && choice == 2) ? 1 : (choice < n ? choice : n);
        if (n && piece[want - 1][0]) {
            size_t end = strlen(out);
            snprintf(out + end, room - end, "#{{ColorGold}}%s", piece[want - 1]);
            changed = 1;
        }
    }
    if (flags & GOLD_FULL) {
        /* the whole description, and only if this language wrote one */
        int n = golden_words(f->id, 1, store, sizeof(store), piece);
        int want = (n == 2 && choice == 2) ? 1 : (choice < n ? choice : n);
        if (n) {
            snprintf(out, room, "%s", piece[want - 1]);
            changed = 1;
        }
    }
    if (!changed && multiplier > 1.0) {
        /* nothing to rewrite: the mod just says the effect is doubled */
        const char *said = text_of(EID_PHRASE,
                                   multiplier == 3.0 ? PHRASE_TRIPLED :
                                   multiplier == 4.0 ? PHRASE_QUADRUPLED : PHRASE_GOLDEN);
        if (said && *said) {
            size_t end = strlen(out);
            snprintf(out + end, room - end, "#{{ColorGold}}%s", said);
        }
    }
}

/* ------------------------------------------- what the item does in THIS run */
/*
 * EID does not only say what an item is; it says what it does for the run the
 * player is in. Brimstone in the bag and a pedestal Brimstone overrides, Keeper
 * looking at a heart item, Tarot Cloth and a card on the floor - each adds or
 * rewrites a line. The mod registers those in features/eid_conditionals.lua
 * and applies them in applyConditionals(); this is that loop, with the
 * registrations carried over by tools/eid_rules.py (eid_rules.h) and the words
 * they point at carried into eid.bin as section 12.
 *
 * What is NOT here: conditions the mod answers with a function of its own -
 * what familiars are in the room, whether an active is charged, Greed Mode,
 * the achievement checks. Those need state this port does not read, and the
 * tool lists every one it leaves out rather than dropping them quietly.
 *
 * A condition's words are either one line, which is appended, or a list: one
 * entry replaces the description outright, and two or more are find-and-replace
 * pairs, with an odd last entry appended. That is the mod's own shape and the
 * language files depend on it.
 */
#define PLAYER_TRINKETS 0x1ab0              /* two slots, the high bit for golden */
/*
 * Four pocket slots, each a pair { what, kind }, with the game's own kinds
 * (0x8615d4): 1 a card and `what` its number, 0 a pill and `what` its colour,
 * 2 a POCKET ACTIVE and `what` the number of an active slot. See items.c for
 * where that was measured.
 */
#define PLAYER_POCKET   0x1c10
#define POCKET_PILL     0
#define POCKET_CARD     1
#define TRINKET_SLOTS   2
#define POCKET_SLOTS    4
#define UNIQUE_MARKS    64

/* the game's own variant number for one of our kinds, as the mod's keys spell it */
static int variant_of(unsigned kind) {
    switch (kind) {
        case EID_COLLECTIBLE: return 100;
        case EID_TRINKET:     return 350;
        case EID_CARD:        return 300;
        case EID_PILL:
        case EID_HORSEPILL:   return 70;
        default:              return 0;
    }
}

/* is the player carrying this - a collectible, a trinket, a card, a pill */
static int holding(int kind, int id) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    if (kind == EID_COLLECTIBLE) return owns_item(id);
    unsigned long me = player_now(b);
    if (!me) return 0;
    if (kind == EID_TRINKET) {
        if (!readable_ptr((const void *) (me + PLAYER_TRINKETS), TRINKET_SLOTS * 4)) return 0;
        const int *slot = (const int *) (me + PLAYER_TRINKETS);
        for (int i = 0; i < TRINKET_SLOTS; i++)
            if ((slot[i] & 0x7fff) == id) return 1;
        return 0;
    }
    if (kind == EID_CARD || kind == EID_PILL) {
        if (!readable_ptr((const void *) (me + PLAYER_POCKET), POCKET_SLOTS * 8)) return 0;
        const int *slot = (const int *) (me + PLAYER_POCKET);
        for (int i = 0; i < POCKET_SLOTS; i++) {
            int what = slot[i * 2 + 1], mine = slot[i * 2];
            if (kind == EID_CARD && what == POCKET_CARD && mine == id) return 1;
            /* a pill is carried by its colour; the description counts from one */
            if (kind == EID_PILL && what == POCKET_PILL && mine != 0
                && pill_effect_of(mine & PILL_COLOUR) + 1 == id)
                return 1;
        }
    }
    return 0;
}

/* is the player this character - and, if the mod says so, its tainted twin */
static int being(int type, int tainted) {
    int me = player_kind();
    if (me < 0) return 0;
    if (me == type) return 1;
    return tainted && me < EID_PLAYER_KINDS && EID_REGULAR_OF[me] == type;
}

/* the key of one of the mod's conditional strings, hashed as eid_more.py does */
static unsigned key_hash(const char *text) {
    unsigned h = 2166136261u;
    for (; *text; text++) h = (h ^ (unsigned char) *text) * 16777619u;
    return h;
}

/*
 * The mod does not fall back to English for these lines, on purpose: an
 * English sentence in the middle of a Russian description reads worse than a
 * missing one, and the language files are updated at their own pace. But the
 * Russian file has about a fifth of what English has, so most of what the mod
 * knows about THIS run would never be shown. `eidall=1` in port-touch.ini asks
 * for the English line where the language has none.
 */
static int g_cond_english;

void shim_eid_conditions_english(int on) { g_cond_english = on != 0; }
int shim_eid_conditions_english_get(void) { return g_cond_english; }

/* the same, but only in this language - some lines must not fall back */
static const char *cond_words_own(const char *table, const char *key) {
    char whole[192];
    snprintf(whole, sizeof(whole), "%s|%s", table, key);
    const entry_t *e = lookup(EID_CONDITION, key_hash(whole));
    return e ? g_strings + e->text : NULL;
}

/* one of the conditional strings by name, for the places that need a fixed one */
static const char *cond_words_named(const char *table, const char *key) {
    char whole[192];
    snprintf(whole, sizeof(whole), "%s|%s", table, key);
    unsigned h = key_hash(whole);
    const entry_t *e = lookup(EID_CONDITION, h);
    if (!e) e = lookup_in(g_en_at, EID_CONDITION, h);
    return e ? g_strings + e->text : NULL;
}

static const char *cond_words(const eid_rule_t *r, const char *key) {
    char whole[192];
    snprintf(whole, sizeof(whole), "%s|%s", EID_LOC[r->loc], key);
    unsigned h = key_hash(whole);
    const entry_t *e = lookup(EID_CONDITION, h);
    /*
     * The mod does NOT fall back to English here unless the condition says it
     * may: an English line inside a Russian description is worse than no line,
     * and the language files are updated at their own pace.
     */
    if (!e && (r->fallback || g_cond_english)) e = lookup_in(g_en_at, EID_CONDITION, h);
    return e ? g_strings + e->text : NULL;
}

/* what {1} stands for in this condition's words */
static const char *rule_variable(const eid_rule_t *r) {
    switch (r->var) {
        case RULE_VAR_TEXT:   return r->var_text;
        case RULE_VAR_ITEM:   return shim_eid_name(r->var_kind, (unsigned) r->var_id);
        case RULE_VAR_PLAYER: return shim_eid_name(EID_BIRTHRIGHT, (unsigned) r->var_id);
        default:              return NULL;
    }
}

/* one appended line, with the mod's bullet in front of every line of it */
static void append_lines(char *out, size_t room, const char *lead, const char *text) {
    size_t at = strlen(out);
    if (at + strlen(lead) + 2 >= room) return;
    at += (size_t) snprintf(out + at, room - at, "%s", lead);
    for (const char *p = text; *p && at + strlen(lead) + 2 < room; p++) {
        if (*p == '#') at += (size_t) snprintf(out + at, room - at, "%s", lead);
        else out[at++] = *p;
    }
    out[at] = 0;
}

/* the first number in the text equal to this one, replaced */
static int replace_number_once(char *text, size_t room, const char *spelt, const char *with) {
    for (char *p = text; *p; p++) {
        if (p[0] == '{' && p[1] == '{') {       /* a marker is not a number */
            char *end = strstr(p, "}}");
            if (!end) break;
            p = end + 1;
            continue;
        }
        if (!((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9'))) continue;
        char *q = p;
        while (*q >= '0' && *q <= '9') q++;
        if (*q == '.' && q[1] >= '0' && q[1] <= '9') { q++; while (*q >= '0' && *q <= '9') q++; }
        size_t len = (size_t) (q - p);
        if (len == strlen(spelt) && !strncmp(p, spelt, len)) {
            char tail[3400];
            snprintf(tail, sizeof(tail), "%s", q);
            size_t head = (size_t) (p - text);
            snprintf(text + head, room - head, "%s%s", with, tail);
            return 1;
        }
        p = q - 1;
    }
    return 0;
}

/*
 * A short number that changes when the player's own state does.
 *
 * The mod re-checks every condition on every frame, so picking Dr. Fetus up
 * while standing on a pedestal changes the panel there and then. Laying the
 * panel out again every frame would be wasteful, so instead this walks the
 * items the conditions actually ASK about - the list comes out of the same
 * tool that writes the conditions - plus the trinkets, the pockets and the
 * character, and the panel is rebuilt when the number changes.
 *
 * The cost is about a hundred and fifty reads out of one contiguous array, and
 * only in the frames where the panel is UP: with nothing under the player the
 * layout has already returned by the time this is reached. The item list on
 * the map is a different thing entirely and reads only when it is opened.
 */
static unsigned inventory_stamp(void) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long me = player_now(b);
    if (!me) return 0;
    unsigned h = 2166136261u;
    if (readable_ptr((const void *) (me + PLAYER_OWNED), 8)) {
        const int *counts = *(const int *const *) (me + PLAYER_OWNED);
        if (counts && readable_ptr(counts, (MAX_ITEM_ID + 1) * 4)) {
            for (int i = 0; i < EID_WATCHED_ITEMS; i++) {
                int id = EID_WATCHED[i];
                if (id >= 0 && id <= MAX_ITEM_ID && counts[id] > 0)
                    h = (h ^ (unsigned) id) * 16777619u;
            }
            /* and the few the modifiers ask about, which are not conditions */
            static const short MINE[] = {
                ITEM_SPINDOWN, ITEM_CAR_BATTERY, ITEM_MOMS_BOX,
                ITEM_BLANK_CARD, ITEM_CLEAR_RUNE, ITEM_PLACEBO,
            };
            for (unsigned i = 0; i < sizeof(MINE) / sizeof(MINE[0]); i++)
                if (counts[MINE[i]] > 0) h = (h ^ (unsigned) MINE[i]) * 16777619u;
        }
    }
    if (readable_ptr((const void *) (me + PLAYER_TRINKETS), TRINKET_SLOTS * 4)) {
        const int *slot = (const int *) (me + PLAYER_TRINKETS);
        for (int i = 0; i < TRINKET_SLOTS; i++)
            h = (h ^ (unsigned) slot[i]) * 16777619u;
    }
    if (readable_ptr((const void *) (me + PLAYER_POCKET), POCKET_SLOTS * 8)) {
        const int *slot = (const int *) (me + PLAYER_POCKET);
        for (int i = 0; i < POCKET_SLOTS * 2; i++)
            h = (h ^ (unsigned) slot[i]) * 16777619u;
    }
    if (readable_ptr((const void *) (me + PLAYER_TYPE), 4))
        h = (h ^ (unsigned) *(const int *) (me + PLAYER_TYPE)) * 16777619u;
    return h ? h : 1;
}

/*
 * The mod's CheckActiveChargeType, which asks about the item on the FLOOR: is
 * it an active, does it hold this much, and does it charge this way. All three
 * come from the game's own items.xml (tools/eid_charge.py), so no run state is
 * needed - only the item that does the charging has to be in the bag.
 */
static int charge_matches(const eid_rule_t *r, const found_t *f) {
    if (f->kind != EID_COLLECTIBLE || (int) f->id > EID_ACTIVE_MAX) return 0;
    if (r->value && !owns_item(r->value)) return 0;
    int kind = EID_ACTIVE_KIND[f->id];
    if (kind < 0) return 0;                             /* not an active at all */
    if (r->arg1 >= 0 && EID_ACTIVE_CHARGES[f->id] != r->arg1) return 0;
    if (r->arg2 >= 0 && kind != r->arg2) return 0;
    return 1;
}

/*
 * The active items the player is carrying.
 *
 * Four slots of twenty-eight bytes from Player + 0x1964, the item's number
 * first. Reading the image did not settle this - nothing indexes the array
 * where the disassembler could see it - so it was measured on the phone: a
 * probe walked the player object and reported every word holding the number of
 * an ACTIVE item the player owned. It answered +0x1964 with Isaac's D6 (105),
 * and the same word followed when another active was picked up. The stride
 * then fell out of the image at 0x4f0674:
 *
 *     mov w8, #0x1c ; smaddl x8, w1, w8, x0 ; ldr w1, [x8, #0x1964]
 *
 * Slot 0 is the item in hand, 1 the Schoolbag's, 2 and 3 the pocket actives
 * some characters carry.
 */
#define PLAYER_ACTIVES 0x1964
#define ACTIVE_STRIDE  0x1c
#define ACTIVE_SLOTS   4
#define ITEM_SCHOOLBAG 534

static int active_held(int slot) {
    unsigned long b = shim_image_base();
    if (!b || slot < 0 || slot >= ACTIVE_SLOTS) return 0;
    unsigned long me = player_now(b);
    if (!me) return 0;
    unsigned long at = me + PLAYER_ACTIVES + (unsigned) slot * ACTIVE_STRIDE;
    if (!readable_ptr((const void *) at, 4)) return 0;
    int id = *(const int *) at;
    return (id > 0 && id <= MAX_ITEM_ID) ? id : 0;
}

/* the mod's CheckPlayersForActiveChargeType: an active IN HAND of this kind */
static int held_charge(const eid_rule_t *r) {
    int last = r->value ? ACTIVE_SLOTS - 1 : 1;     /* the pockets, or only the two */
    for (int slot = 0; slot <= last; slot++) {
        int id = active_held(slot);
        if (id <= 0 || id > EID_ACTIVE_MAX) continue;
        int kind = EID_ACTIVE_KIND[id];
        if (kind < 0) continue;
        if (r->arg1 >= 0 && EID_ACTIVE_CHARGES[id] != r->arg1) continue;
        if (r->arg2 >= 0 && kind != r->arg2) continue;
        return id;
    }
    return 0;
}

/* the first item of one of the mod's lists that the player has */
static int first_owned(int which) {
    if (which < 0 || which >= EID_LISTS) return 0;
    for (const short *p = EID_LIST[which]; *p >= 0; p++)
        if (owns_item(*p)) return *p;
    return 0;
}

/*
 * The mod's CheckForMultipleActives: the Schoolbag, or the pocket active a
 * character carries anyway. `only_chargeable` is 4.5 Volt asking, and it wants
 * the ordinary kind only.
 */
static int multi_active(int only_chargeable) {
    if (owns_item(ITEM_SCHOOLBAG)) return ITEM_SCHOOLBAG;
    int me = player_kind();
    if (me < 0 || me >= EID_POCKET_ACTIVE_KINDS) return 0;
    int kind = EID_POCKET_ACTIVE[me];
    if (kind < 0 || (kind != 0 && only_chargeable)) return 0;
    return active_held(2);
}

/*
 * Does this condition hold right now - and, when the mod's own answer is an
 * ITEM rather than a plain yes, which item. The mod puts that item's name and
 * icon into the line it prints, and for some conditions the item also decides
 * WHICH line is printed (useResult).
 */
static int rule_true(const eid_rule_t *r, const found_t *f, int *found_item) {
    *found_item = 0;
    switch (r->test) {
        case RULE_HELD_CHARGE:  return (*found_item = held_charge(r)) != 0;
        case RULE_LIST:         return (*found_item = first_owned(r->arg1)) != 0;
        case RULE_MULTI_ACTIVE: return (*found_item = multi_active(r->arg1)) != 0;
        case RULE_OWNS:     return holding(r->own_kind, r->value);
        case RULE_CHAR:     return being(r->value, r->tainted);
        case RULE_GREED:    return run_greed();
        case RULE_HARD:     return run_hard();
        case RULE_STAGE_EQ: return run_stage() == r->arg1;
        case RULE_STAGE_GT: return run_stage() > r->arg1;
        case RULE_SHOP:     return run_greed() && run_stage() == 0;
        case RULE_RESTOCK:  return owns_item(376) || run_greed();
        case RULE_TAINTED: {
            int me = player_kind();
            return me > 0 && me < EID_PLAYER_KINDS && EID_REGULAR_OF[me] >= 0;
        }
        case RULE_NO_RED: {
            int me = player_kind();
            for (int i = 0; i < EID_NO_RED_HEALTH_COUNT; i++)
                if (EID_NO_RED_HEALTH[i] == me) return 1;
            return 0;
        }
        /*
         * The mod's achievement checks answer "yes" whenever REPENTOGON is not
         * there, which is every game but a modded PC one. So does this.
         */
        case RULE_ALWAYS:   return 1;
        case RULE_CHARGE:   return charge_matches(r, f);
        default:            return 0;
    }
}

static void conditions_apply(const found_t *f, const char *text, char *out, size_t room) {
    snprintf(out, room, "%s", text ? text : "");
    int variant = variant_of(f->kind);
    if (!variant || !g_db) return;
    unsigned want = (f->kind == EID_HORSEPILL) ? EID_PILL : f->kind;
    char tvs[32];
    snprintf(tvs, sizeof(tvs), "5.%d.%u", variant, f->id);
    long highest = -2147483647L;
    unsigned char said[UNIQUE_MARKS];
    memset(said, 0, sizeof(said));

    for (int i = 0; i < EID_RULES; i++) {
        const eid_rule_t *r = &EID_RULE[i];
        if (r->kind != (int) want) continue;
        if (r->id >= 0 && (unsigned) r->id != f->id) continue;
        if ((r->flags & RULE_CHECK_LAYERS) && highest > r->layer) continue;
        int answer = 0;
        if (!rule_true(r, f, &answer)) continue;

        /*
         * A condition that answered with an item can also decide which line is
         * printed: the mod puts the item's number where the modifier text
         * would be (useResult), so carBattery[35] is what Car Battery does to
         * the Necronomicon this player is carrying.
         */
        char named[16];
        const char *modifier = r->modifier;
        if (answer && (r->flags & RULE_USE_RESULT)) {
            snprintf(named, sizeof(named), "%d", answer);
            modifier = named;
        }
        char key[192];
        const char *words;
        if (modifier) {
            snprintf(key, sizeof(key), "%s (%s)", tvs, modifier);
            words = cond_words(r, key);
            if (!words) words = cond_words(r, modifier);
        } else {
            words = cond_words(r, tvs);
            if (!words) {
                snprintf(key, sizeof(key), "%u", f->id);
                words = cond_words(r, key);
            }
        }
        if (!words || !*words) continue;
        if (r->unique && r->unique < UNIQUE_MARKS && said[r->unique]) continue;
        if (r->layer > highest) highest = r->layer;
        if (r->unique && r->unique < UNIQUE_MARKS) said[r->unique] = 1;

        /* and it names itself: the mod's own bullet and {1} for such a line */
        char made[40];
        const char *value = rule_variable(r);
        const char *bullet = r->bullet;
        if (answer) {
            snprintf(made, sizeof(made), "{{Collectible%d}}", answer);
            bullet = made;
            value = shim_eid_name(EID_COLLECTIBLE, (unsigned) answer);
        }
        if (!value) value = "";
        char icon[96];
        icon[0] = 0;
        if (bullet) snprintf(icon, sizeof(icon), "%s ", bullet);
        if (r->colour) {
            size_t at = strlen(icon);
            snprintf(icon + at, sizeof(icon) - at, "{{%s}}", r->colour);
        }
        char filled[1200];
        if (words[0] == 'S') {                  /* one line, appended */
            char lead[100];
            snprintf(lead, sizeof(lead), "#%s", icon);
            fill_in(words + 1, value, filled, sizeof(filled));
            append_lines(out, room, lead, filled);
            continue;
        }
        /* a list: pieces separated by the packer's own mark */
        char store[1600];
        snprintf(store, sizeof(store), "%s", words + 1);
        const char *piece[16];
        int n = 0;
        piece[n++] = store;
        for (char *at = store; *at && n < 16; at++)
            if (*at == 0x1f) { *at = 0; piece[n++] = at + 1; }
        if (n == 1) {                           /* the whole description, replaced */
            fill_in(piece[0] + 1, value, filled, sizeof(filled));
            char whole[2600];
            snprintf(whole, sizeof(whole), "%s%s", icon, filled);
            snprintf(out, room, "%s", whole);
            continue;
        }
        for (int p = 0; p + 1 < n; p += 2) {    /* find and replace, in pairs */
            fill_in(piece[p + 1] + 1, value, filled, sizeof(filled));
            char with[1300];
            if (r->replace) snprintf(with, sizeof(with), "{{%s}}%s{{CR}}", r->replace, filled);
            else            snprintf(with, sizeof(with), "%s", filled);
            if (piece[p][0] == 'n') replace_number_once(out, room, piece[p] + 1, with);
            else                    replace_once(out, room, piece[p] + 1, with);
        }
        if (n & 1) {                            /* an odd last one is appended */
            char lead[100];
            snprintf(lead, sizeof(lead), "#%s", icon);
            fill_in(piece[n - 1] + 1, value, filled, sizeof(filled));
            append_lines(out, room, lead, filled);
        }
    }
}

/* ---------------------------------------------- the last few modifiers */
/*
 * Four of the mod's own callbacks that need nothing this port has not already
 * got: the numbers come from the game's XML and from what the player is
 * carrying, both of which are in hand.
 */
#define ITEM_MOMS_BOX_ID    439
#define TRINKET_BLACK_FEATHER 80
#define ITEM_FALSE_PHD      654
#define ITEM_PHD            75
#define ITEM_LUCKY_FOOT     46
#define ITEM_VIRGO          303
#define PILL_EXPERIMENTAL   49
#define PILL_SHOT_SPEED_DOWN 47
#define PILL_IM_EXCITED     42
#define ITEM_PANDORAS_BOX   297

/*
 * Black Feather: half a point of damage for every "evil" item held, doubled by
 * Mom's Box and doubled again by a golden feather (eid_modifiers.lua,
 * BlackFeatherCallback). A golden trinket counts twice, which is what the
 * mod's GetTrinketMultiplier answers.
 */
static void black_feather_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    if (f->kind != EID_TRINKET || f->id != TRINKET_BLACK_FEATHER) return;
    int count = 0;
    for (int i = 0; i < EID_FEATHER_ITEMS_COUNT; i++)
        count += owned_count(EID_FEATHER_ITEMS[i]);
    unsigned long b = shim_image_base();
    unsigned long me = b ? player_now(b) : 0;
    if (me && readable_ptr((const void *) (me + PLAYER_TRINKETS), TRINKET_SLOTS * 4)) {
        const int *slot = (const int *) (me + PLAYER_TRINKETS);
        for (int i = 0; i < TRINKET_SLOTS; i++)
            for (int k = 0; k < EID_FEATHER_TRINKETS_COUNT; k++)
                if ((slot[i] & 0x7fff) == EID_FEATHER_TRINKETS[k])
                    count += (slot[i] & TRINKET_GOLDEN) ? 2 : 1;
    }
    int box = owns_item(ITEM_MOMS_BOX_ID);
    float damage = 0.5f * (float) count * (box ? 2.0f : 1.0f) * (f->golden ? 2.0f : 1.0f);
    const char *phrase = text_of(EID_PHRASE, PHRASE_BLACK_FEATHER);
    if (!phrase || !*phrase) return;
    char one[16], two[64], said[240];
    snprintf(one, sizeof(one), "%d", count);
    snprintf(two, sizeof(two), "{{%s}}%.1f{{CR}}",
             count == 0 ? "CR" : (box || f->golden) ? "ColorGold" : "BlinkGray", (double) damage);
    fill_two(phrase, one, two, said, sizeof(said));
    snprintf(out, room, "#%s", said);
}

/*
 * False PHD turns a bad pill into a damage up, or into a black heart, going by
 * the class the game's own XML gives the pill (eid_modifiers.lua,
 * FalsePHDCallback). Two pills are named there by hand, because their class
 * does not say what they really do.
 */
static void false_phd_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    if ((f->kind != EID_PILL && f->kind != EID_HORSEPILL) || !owns_item(ITEM_FALSE_PHD)) return;
    int effect = (int) f->id - 1;
    if (effect < 0 || effect > EID_PILL_MAX) return;
    int kind = EID_PILL_CLASS[effect];
    int horse = f->kind == EID_HORSEPILL;
    int damage = ((kind & PILL_BIG) && (kind & PILL_BAD)) || effect == PILL_SHOT_SPEED_DOWN;
    int heart = ((kind & (PILL_SMALL | PILL_MIDDLING))
                 && ((kind & PILL_BAD) || effect == PILL_IM_EXCITED));
    const char *said = NULL;
    if (damage) said = text_of(EID_PHRASE, horse ? PHRASE_FALSE_PHD_HORSE : PHRASE_FALSE_PHD_DAMAGE);
    else if (heart) said = text_of(EID_PHRASE, PHRASE_FALSE_PHD_HEART);
    if (said && *said) snprintf(out, room, "#{{Collectible%d}} %s", ITEM_FALSE_PHD, said);
}

/* and what an experimental pill is worth with PHD, Lucky Foot, Virgo or False PHD */
static void experimental_pill_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    if ((f->kind != EID_PILL && f->kind != EID_HORSEPILL) || (int) f->id - 1 != PILL_EXPERIMENTAL)
        return;
    int which = owns_item(ITEM_PHD) ? ITEM_PHD
              : owns_item(ITEM_LUCKY_FOOT) ? ITEM_LUCKY_FOOT
              : owns_item(ITEM_VIRGO) ? ITEM_VIRGO : 0;
    int both = 0;
    if (owns_item(ITEM_FALSE_PHD)) { both = which != 0; which = ITEM_FALSE_PHD; }
    if (!which) return;
    char said[400];
    if (which == ITEM_FALSE_PHD) {
        const char *damage = text_of(EID_PHRASE, f->kind == EID_HORSEPILL
                                     ? PHRASE_FALSE_PHD_HORSE : PHRASE_FALSE_PHD_DAMAGE);
        if (!damage) damage = "";
        if (both) snprintf(said, sizeof(said), "%s", damage);
        else {
            const char *lead = text_of(EID_PHRASE, PHRASE_EXPERIMENTAL_FALSE);
            snprintf(said, sizeof(said), "%s#{{Collectible%d}} %s",
                     lead ? lead : "", ITEM_FALSE_PHD, damage);
        }
    } else {
        const char *lead = text_of(EID_PHRASE, PHRASE_EXPERIMENTAL_PHD);
        if (!lead || !*lead) return;
        snprintf(said, sizeof(said), "%s", lead);
    }
    snprintf(out, room, "#{{Collectible%d}} %s", which, said);
}

/*
 * Pandora's Box lists what it gives on every floor, and the mod highlights the
 * line for the floor you are standing on (eid_modifiers.lua,
 * PandorasBoxCallback). The two tables below are the mod's own: which block of
 * the description belongs to which floor, ordinary and alternate, and the same
 * for Greed Mode where a third column is used when the language has its own
 * Greed text.
 */
static void pandora_apply(const found_t *f, const char *text, char *out, size_t room) {
    static const unsigned char FLOORS[14][3] = {
        {0,0,0}, {1,1,1}, {2,2,2}, {3,3,3}, {4,4,4}, {5,5,5}, {6,6,6}, {7,7,7},
        {8,8,8}, {9,9,9}, {10,11,11}, {12,13,13}, {9,9,9}, {14,14,14},
    };
    static const unsigned char GREED[8][3] = {
        {0,0,0}, {2,2,1}, {4,4,2}, {6,6,3}, {8,8,4}, {10,10,5}, {13,13,6}, {13,13,6},
    };
    out[0] = 0;
    if (f->kind != EID_COLLECTIBLE || f->id != ITEM_PANDORAS_BOX || !text) return;
    int floor = run_stage();
    if (floor < 1) return;
    int greed = run_greed();
    /* the game's own stage type: 1 and 2 are the alternate floors */
    unsigned long game = game_now();
    int alt = 0;
    if (game && readable_ptr((const void *) (game + GAME_STAGE_TYPE), 4)) {
        int said = *(const int *) (game + GAME_STAGE_TYPE);
        alt = (said == 1 || said == 2);
    }
    const char *modifier = greed ? cond_words_own("ConditionalDescs", "5.100.297 (Greed)") : NULL;
    int block;
    if (greed) {
        if (floor > 7) return;
        block = GREED[floor][modifier ? 2 : (alt ? 1 : 0)];
    } else {
        if (floor > 13) return;
        block = FLOORS[floor][modifier ? 2 : (alt ? 1 : 0)];
    }
    if (!block) return;

    /* the floors are the lines of the description, after its first two */
    snprintf(out, room, "%s", text);
    const char *piece[32];
    int length[32], n = 0;
    const char *at = out;
    int found_void = 0;
    while (*at && n < 32) {
        const char *end = at;
        while (*end && *end != '#' && *end != ';') end++;
        if (end > at) {
            piece[n] = at;
            length[n] = (int) (end - at);
            if (strstr(at, "???") && strstr(at, "???") < end) found_void = 1;
            n++;
        }
        at = *end ? end + 1 : end;
    }
    /* some languages are missing the ???/Void line, and the mod counts around it */
    if (!found_void && !modifier) {
        if (block == 9 || (n < 14 && block > 9)) return;
        if (block > 9) block--;
    }
    /*
     * The mod counts TWO lines of item information before the floors, because
     * by the time it runs, its own "SINGLE USE" warning is already sitting in
     * front of the description - Pandora's Box is a single-use item. This port
     * adds that warning at the end instead, so there is one line to skip here,
     * not two.
     */
    int want = block;
    if (want < 0 || want >= n) return;
    char head[2600], tail[2600];
    snprintf(head, sizeof(head), "%.*s", (int) (piece[want] - out), out);
    snprintf(tail, sizeof(tail), "%s", piece[want] + length[want]);
    char line[400];
    snprintf(line, sizeof(line), "%.*s", length[want], piece[want]);
    snprintf(out, room, "%s{{ColorBagComplete}}%s{{CR}}%s", head, line, tail);
}

/* ------------------------------------------ wisps and locusts */
/*
 * Book of Virtues turns the item you are holding into a wisp, and Abyss turns
 * it into a locust; both describe what they would make. The mod builds those
 * blocks at runtime out of the game's XML and its own language files, and
 * tools/eid_wisps.py builds them ahead of time instead - once per language,
 * into sections 15 and 16 - because only ONE number in them depends on the
 * run: a wisp's damage grows with the floor. That number is left as {1} and
 * filled in here.
 */
#define ITEM_BOOK_OF_VIRTUES 584
#define ITEM_ABYSS           706

/* the mod's own sum, and its own way of printing it */
static void wisp_damage(int id, char *out, size_t room) {
    out[0] = 0;
    for (int i = 0; i < EID_WISPS; i++) {
        if (EID_WISP[i].id != id) continue;
        int floor = run_stage();
        if (floor < 1) floor = 1;
        float damage = (EID_WISP[i].damage + EID_WISP[i].stage * (float) (floor - 1))
                     * (30.0f / EID_WISP[i].delay);
        snprintf(out, room, "%.2f", (double) damage);
        char *end = out + strlen(out) - 1;      /* without trailing zeros */
        while (end > out && *end == '0') *end-- = 0;
        if (end > out && *end == '.') *end = 0;
        return;
    }
    snprintf(out, room, "-");
}

static void virtues_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    if (f->kind != EID_COLLECTIBLE) return;
    int mine = (int) f->id;
    if (mine != ITEM_BOOK_OF_VIRTUES && !owns_item(ITEM_BOOK_OF_VIRTUES)) return;
    /* on the book itself the mod describes the active item in hand */
    if (mine == ITEM_BOOK_OF_VIRTUES) {
        mine = active_held(0);
        if (mine <= 0) return;
    }
    const char *block = text_of(EID_WISP_TEXT, (unsigned) mine);
    if (!block || !*block) return;
    char damage[24];
    wisp_damage(mine, damage, sizeof(damage));
    fill_in(block, damage, out, room);
}

static void abyss_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    if (f->kind != EID_COLLECTIBLE || !owns_item(ITEM_ABYSS)) return;
    const char *block = text_of(EID_LOCUST_TEXT, f->id);
    if (!block || !*block) block = text_of(EID_LOCUST_TEXT, 0);   /* the ordinary locust */
    if (block && *block) snprintf(out, room, "%s", block);
}

/* ------------------------------------------- hearts that are not red */
/*
 * The Lost has no hearts, ??? has soul hearts, Keeper has coins - and every
 * "+1 Health" in a description is wrong for them. The mod rewrites those lines
 * (eid_modifiers.lua, HealthUpCallback) using a table of find-and-replace pairs
 * per heart type, with the item's own number of hearts put into both halves
 * (EID.HealthUpData), and then strips the healing lines for the characters
 * that cannot be healed at all. This is that, one for one.
 *
 * Which character has which heart is the mod's own table, and which items give
 * how much health likewise; both are carried into eid_rules.h. The words are
 * section 13 of the database, and the language's plural endings section 14 -
 * every language writes its plural rule as the same list of endings, so it can
 * be read out without running any Lua.
 */
static int hearts_of(const found_t *f) {
    for (int i = 0; i < EID_HEALTH_UPS; i++)
        if (EID_HEALTH_UP[i].kind == f->kind && EID_HEALTH_UP[i].id == (int) f->id)
            return EID_HEALTH_UP[i].hearts;
    return 1;                                   /* the mod's own default */
}

/* every occurrence, not just the first */
static void replace_all(char *text, size_t room, const char *find, const char *with) {
    if (!*find) return;
    for (int guard = 0; guard < 16; guard++)
        if (!replace_once(text, room, find, with)) break;
}

/* the language's own plural rule: a list of endings, three pieces each */
static void pluralize(char *text, size_t room, int amount) {
    const char *whole = text_of(EID_PLURAL, 0);
    if (!whole || !*whole) return;
    char store[400];
    snprintf(store, sizeof(store), "%s", whole);
    const char *piece[24];
    int n = 0;
    piece[n++] = store;
    for (char *at = store; *at && n < 24; at++)
        if (*at == 0x1f) { *at = 0; piece[n++] = at + 1; }
    for (int i = 0; i + 2 < n; i += 3) {
        const char *name = piece[i], *ending = amount > 1 ? piece[i + 2] : piece[i + 1];
        if (name[0] == '=') {                   /* a whole word, not a marker */
            if (amount > 1) replace_all(text, room, name + 1, piece[i + 2]);
            continue;
        }
        char marker[48];
        snprintf(marker, sizeof(marker), "{%s}", name);
        replace_all(text, room, marker, ending);
    }
}

/* the language's plural rule, for eid_glitch.c */
void shim_eid_pluralize(char *text, unsigned long room, int amount) {
    pluralize(text, (size_t) room, amount);
}

/* a line beginning with this marker is dropped, up to the next break */
static void drop_lines(char *text, const char *marker) {
    for (int guard = 0; guard < 16; guard++) {
        char *at = strstr(text, marker);
        if (!at) return;
        char *end = strchr(at, '#');
        if (!end) { *at = 0; return; }
        memmove(at, end + 1, strlen(end + 1) + 1);
    }
}

static void health_up_apply(const found_t *f, const char *text, char *out, size_t room) {
    out[0] = 0;
    if (!text) return;
    int me = player_kind();
    if (me < 0 || me >= EID_HEART_KINDS) return;
    int heart = EID_HEART_KIND[me];
    if (heart <= 0) return;                     /* red hearts: nothing to do */
    const char *whole = text_of(EID_RED_TO, (unsigned) heart);
    if (!whole || !*whole) return;

    int hearts = hearts_of(f);
    char store[1200];
    snprintf(store, sizeof(store), "%s", whole);
    const char *piece[16];
    int n = 0;
    piece[n++] = store;
    for (char *at = store; *at && n < 16; at++)
        if (*at == 0x1f) { *at = 0; piece[n++] = at + 1; }

    snprintf(out, room, "%s", text);
    char count[16];
    snprintf(count, sizeof(count), "%d", hearts);
    for (int i = 0; i + 1 < n; i += 2) {
        char find[300], with[300];
        fill_in(piece[i], count, find, sizeof(find));
        pluralize(find, sizeof(find), hearts);
        fill_in(piece[i + 1], count, with, sizeof(with));
        replace_once(out, room, find, with);
    }
    pluralize(out, room, hearts);

    /* Soul, Black and None cannot be healed at all, so those lines go */
    if (heart == 1 || heart == 2 || heart == 5) {
        size_t at = strlen(out);
        if (at + 2 < room) { out[at] = '#'; out[at + 1] = 0; }
        drop_lines(out, "{{HealingRed}}");
        drop_lines(out, "{{HealingHalfRed}}");
    }
    /*
     * A line replaced away leaves its break behind, and two breaks in a row
     * are an empty line in the panel. The mod leaves them; the panel should
     * not show them.
     */
    for (char *at = out; *at; ) {
        if (at[0] == '#' && at[1] == '#') memmove(at + 1, at + 2, strlen(at + 2) + 1);
        else at++;
    }
    { size_t end = strlen(out); if (end && out[end - 1] == '#') out[end - 1] = 0; }

    /* and if that emptied the description, say so the way the mod says it */
    int anything = 0;
    for (const char *p = out; *p; p++) if (*p != '#') { anything = 1; break; }
    if (!anything) {
        const char *said = cond_words_named("ConditionalDescs", "No Effect");
        const char *who = shim_eid_name(EID_BIRTHRIGHT, (unsigned) me);
        char name[120];
        snprintf(name, sizeof(name), "{{ColorIsaac}}%s{{CR}}", who ? who : "");
        char line[300];
        fill_in(said ? said : "", name, line, sizeof(line));
        snprintf(out, room, "{{Player%d}} %s", me, line);
    }
}

/* ------------------------------------------------------------- luck */
/*
 * "33% chance at 4 luck".
 *
 * A good few of the game's chances are luck-scaled, and the mod carries a
 * formula for each of them (eid_data.lua, LuckFormulas); tools/eid_luck.py
 * turns those fifty-three Lua one-liners into C. The luck itself is a float at
 * Player + 0x1950 - found in the game rather than guessed: fifty places load
 * that field, take its floor and build a one-in-N chance out of it, which is
 * the same shape the mod's formulas have. At exactly nought luck the mod says
 * nothing at all, and neither does this.
 */
#define PLAYER_LUCK 0x1950

static int player_luck(float *out) {
    unsigned long b = shim_image_base();
    if (!b) return 0;
    unsigned long me = player_now(b);
    if (!me || !readable_ptr((const void *) (me + PLAYER_LUCK), 4)) return 0;
    float luck = *(const float *) (me + PLAYER_LUCK);
    if (!(luck > -1000.0f && luck < 1000.0f)) return 0;
    *out = luck;
    return 1;
}

static void luck_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    float luck = 0.0f, chance = 0.0f;
    if (!player_luck(&luck) || luck == 0.0f) return;
    if (!eid_luck_chance(f->kind, f->id, luck, &chance)) return;
    if (chance < 0.0f) chance = 0.0f;
    if (chance > 100.0f) chance = 100.0f;
    const char *phrase = text_of(EID_PHRASE, PHRASE_LUCK);
    if (!phrase || !*phrase) return;
    char said[240], one[24], two[48];
    snprintf(one, sizeof(one), "%.3g", (double) chance);
    snprintf(two, sizeof(two), "{{BlinkGreen}}%.3g{{CR}}", (double) luck);
    fill_two(phrase, one, two, said, sizeof(said));
    snprintf(out, room, "#{{Luck}} {{NoLB}}%s", said);
}

/* ------------------------------------------------------ spent when used */
/*
 * The mod's SingleUseCollectibles (eid_data.lua), with its warning put in
 * front of the description rather than after it.
 */
/*
 * Consolation Prize, which pays out whatever the player is worst at
 * (eid_modifiers.lua, ConsolationPrizeCallback).
 *
 * The mod puts the four stats on the scale the item works on, takes the
 * lowest, and does the same for the pickups - coins, three to a bomb, three to
 * a key, and for the two Bethanys their charge. A tie prints every winner with
 * a question mark after it, because the item then picks one of them.
 *
 * The fields were measured in this image. The counters are the Repentance
 * player's, named by their writers: AddKeys 0x4f0d20 (+0x16d4), AddBombs
 * 0x4f0ca0 (+0x16dc), AddCoins 0x4f0a14 (+0x16e0) - the +0x1c68..0x1c74 read
 * before 05.10.2026 belonged to the older engine copy (0x1b98c0); the two
 * charges by the getters that only answer for player type 18 (0x4fc2c8) and
 * 36 (0x4fc458).
 */
#define ITEM_CONSOLATION_PRIZE 644
#define PLAYER_FIRE_DELAY   0x1834
#define PLAYER_MOVE_SPEED   0x1838
#define PLAYER_DAMAGE       0x1844
#define PLAYER_TEAR_RANGE   0x1854
#define PLAYER_KEYS         0x16d4
#define PLAYER_BOMBS        0x16dc
#define PLAYER_COINS        0x16e0
#define PLAYER_SOUL_CHARGE  0x2478          /* Bethany's, and hers alone */
#define PLAYER_BLOOD_CHARGE 0x247c          /* Tainted Bethany's */
#define PRIZE_BETHANY       18
#define PRIZE_T_BETHANY     36
#define PRIZE_NEVER         9999            /* what the mod scores a charge nobody has */
#define PRIZE_FIRST_PICKUP  10              /* where the pickup words start in section 17 */
#define PRIZE_UP "\xe2\x86\x91"             /* the arrow the mod puts before a stat */

/* EID:SimpleRound, which keeps two places */
static double round_two(double v) {
    return floor(v * 100.0 + 0.5) / 100.0;
}

/* the quantity goes after the icon, once after every icon, as the mod's gsub does */
static int after_icons(char *text, size_t room, const char *quantity) {
    int done = 0;
    size_t n = strlen(quantity);
    char *at = text;
    while ((at = strstr(at, "}} ")) != NULL) {
        at += 3;
        if (strlen(text) + n + 2 >= room) break;
        memmove(at + n + 1, at, strlen(at) + 1);
        memcpy(at, quantity, n);
        at[n] = ' ';
        at += n + 1;
        done++;
    }
    return done;
}

static void consolation_line(const found_t *f, char *out, size_t room) {
    out[0] = 0;
    if (f->kind != EID_COLLECTIBLE || f->id != ITEM_CONSOLATION_PRIZE) return;
    int type = player_kind();
    unsigned long b = shim_image_base();
    unsigned long me = b ? player_now(b) : 0;
    if (type < 0 || !me) return;
    if (!readable_ptr((const void *) (me + PLAYER_FIRE_DELAY), 0x1000)) return;

    double fire = *(const float *) (me + PLAYER_FIRE_DELAY);
    double speed = *(const float *) (me + PLAYER_MOVE_SPEED);
    double damage = *(const float *) (me + PLAYER_DAMAGE);
    double range = *(const float *) (me + PLAYER_TEAR_RANGE);
    double stat[4];
    stat[0] = round_two(speed * 4.5 - 2.0);
    stat[1] = round_two(pow(30.0 / (fire + 1.0), 0.75) * 2.120391 - 2.0);
    stat[2] = round_two(pow(damage, 0.56) * 2.231179 - 2.0);
    stat[3] = round_two((range - 230.0) / 60.0 + 2.0);

    int pick[5];
    pick[0] = *(const int *) (me + PLAYER_COINS);
    pick[1] = *(const int *) (me + PLAYER_BOMBS) * 3;
    pick[2] = *(const int *) (me + PLAYER_KEYS) * 3;
    pick[3] = type == PRIZE_BETHANY
              ? *(const int *) (me + PLAYER_SOUL_CHARGE) * 2 - 1 : PRIZE_NEVER;
    pick[4] = type == PRIZE_T_BETHANY
              ? *(const int *) (me + PLAYER_BLOOD_CHARGE) * 2 - 1 : PRIZE_NEVER;

    int won_stat[4], stats = 1;
    won_stat[0] = 0;
    for (int i = 1; i < 4; i++) {
        if (stat[i] == stat[won_stat[0]]) won_stat[stats++] = i;
        else if (stat[i] < stat[won_stat[0]]) { won_stat[0] = i; stats = 1; }
    }
    int won_pick[5], picks = 1;
    won_pick[0] = 0;
    for (int i = 1; i < 5; i++) {
        if (pick[i] == pick[won_pick[0]]) won_pick[picks++] = i;
        else if (pick[i] < pick[won_pick[0]]) { won_pick[0] = i; picks = 1; }
    }

    static const char *const STAT_ICON[4] = {
        "{{Speed}}", "{{Tears}}", "{{Damage}}", "{{Range}}"
    };
    static const double STAT_UP[4] = { 0.2, 0.5, 1.0, 2.5 };
    static const char *const HOW_MANY[5] = { "3", "1", "1", "1", "2" };

    const char *who = shim_eid_name(EID_BIRTHRIGHT, (unsigned) type);
    snprintf(out, room, "#{{Player%d}} {{ColorGray}}%s{{CR}}", type, who ? who : "");
    for (int i = 0; i < stats; i++) {
        int v = won_stat[i];
        const char *said = text_of(EID_PRIZE_WORD, (unsigned) v);
        char much[16], line[320], piece[340];
        snprintf(much, sizeof(much), "+%.4g", STAT_UP[v]);
        snprintf(line, sizeof(line), "%s %s", STAT_ICON[v], said ? said : "");
        if (strstr(line, "{1}")) replace_all(line, sizeof(line), "{1}", much);
        else snprintf(line, sizeof(line), "%s %s %s", STAT_ICON[v], much, said ? said : "");
        snprintf(piece, sizeof(piece), "#" PRIZE_UP " %s%s", line, stats > 1 ? "?" : "");
        add_piece(out, room, piece);
    }
    for (int i = 0; i < picks; i++) {
        int v = won_pick[i];
        const char *said = text_of(EID_PRIZE_WORD, PRIZE_FIRST_PICKUP + (unsigned) v);
        char line[320], piece[340];
        if (!said) said = "";
        snprintf(line, sizeof(line), "%s", said);
        if (!after_icons(line, sizeof(line), HOW_MANY[v]))
            snprintf(line, sizeof(line), "%s %s", HOW_MANY[v], said);
        snprintf(piece, sizeof(piece), "#%s%s", line, picks > 1 ? "?" : "");
        add_piece(out, room, piece);
    }
}

static int single_use(const found_t *f) {
    static const unsigned ONCE[] = {
        127, 297, 347, 475, 483, 490, 515, 536,     /* Forget Me Now ... Sacrificial Altar */
        577, 585, 622, 628, 636,                    /* Damocles ... R Key */
    };
    if (f->kind != EID_COLLECTIBLE) return 0;
    for (unsigned i = 0; i < sizeof(ONCE) / sizeof(ONCE[0]); i++)
        if (ONCE[i] == f->id) return 1;
    return 0;
}

/*
 * The transformation an item counts towards, for the panel: its icon's marker
 * and the words "Name (n/3)".
 */
static int form_of_panel(unsigned kind, unsigned id, const char **icon, char *text, size_t room) {
    if (kind != EID_COLLECTIBLE) return 0;
    int form = form_of_item(id);
    if (form < 0) return 0;
    int have = form_counter(form);
    if (have < 0) return 0;
    if (have > FORM_NEEDED) have = FORM_NEEDED;
    const char *name = EID_FORM_NAME[form][form_language_column()];
    if (!name || !*name) name = EID_FORM_NAME[form][0];
    *icon = EID_FORM_ICON[form];
    snprintf(text, room, "%s (%d/%d)", name, have, FORM_NEEDED);
    return 1;
}

/* ------------------------------------------- what the inventory borrows */

/*
 * The inventory (items.c) draws with the same hands as the panel: one picture
 * by its marker name, one line of plain text, and the panel pinned to a chosen
 * thing until the player walks up to something else.
 */
/* is the HUD on screen at all: in a run, and not in the middle of a fade */
int shim_eid_hud_showing(void) {
    unsigned long b = shim_image_base();
    int shim_sticks_in_game(void);
    return b && shim_sticks_in_game() && !screen_fading(b);
}

void shim_eid_marker(const char *name, float x, float y) {
    if (!name || !*name) return;
    eid_icon_t made;
    int slot = -1;
    const eid_icon_t *icon = marker_icon(name, strlen(name), &made, &slot);
    if (icon) draw_icon(icon, slot, x, y, 0.0f);
}

/*
 * The same picture, larger. A sprite carries its own scale as two floats at
 * +0xdc and the renderer multiplies by it, so the inventory can show the same
 * artwork at a size a thumb can aim at.
 */
#define SPRITE_SCALE 0xdc

void shim_eid_marker_faded(const char *name, float x, float y, float scale, float alpha) {
    if (!name || !*name) return;
    eid_icon_t made;
    int slot = -1;
    const eid_icon_t *icon = marker_icon(name, strlen(name), &made, &slot);
    if (!icon) return;
    unsigned char *s = NULL;
    if (slot >= 0) { if (slot < ITEM_SLOTS && g_item_built[slot] > 0) s = g_item[slot]; }
    else if (sheet_ready(icon->sheet)) s = g_sheet[icon->sheet];
    if (!s) return;
    float *sc = (float *) (s + SPRITE_SCALE);
    float was0 = sc[0], was1 = sc[1];
    sc[0] = scale; sc[1] = scale;
    g_icon_alpha = alpha;
    draw_icon(icon, slot, x, y, 0.0f);
    g_icon_alpha = 1.0f;
    sc[0] = was0; sc[1] = was1;
}

void shim_eid_marker_at(const char *name, float x, float y, float scale) {
    shim_eid_marker_faded(name, x, y, scale, 1.0f);
}

void shim_eid_write(const char *text, float x, float y,
                    float r, float g, float b, float a) {
    draw_text(text, x, y, r, g, b, a);
}

/*
 * TEXT ON PAPER. The description face carries a dark rim so it reads over the
 * floor of any room; on the spawner's paper the rim is a smudge (the owner,
 * 18.09.2026). The mod ships the same letters without it, eid_borderless - the
 * same PixelMplus10 with the same 7363 characters, Cyrillic and CJK included -
 * and that is what is written on paper. Languages with faces of their own keep
 * them. Drawn through the engine's scaled string (0x8be42c, as mods.c does).
 */
#define FN_DRAW_SCALED 0x8be42c             /* (font, text, colour, box, centre, x, y, sx, sy) */

static unsigned char g_plain[FONT_BYTES] __attribute__((aligned(16)));
static signed char g_plain_state;

static unsigned long plain_font(unsigned long mgr) {
    unsigned long twin = twin_for(g_lang_code);     /* rimless already, and the letters join */
    if (twin) return twin;
    unsigned long face = face_for(g_lang_code);
    if (face) return face;
    if (!g_plain_state) {
        g_plain_state = font_into(g_plain, "eid_borderless", 'A') ? 1 : -1;
        LOG("eid: the borderless font %s", g_plain_state > 0 ? "is loaded" : "did not load");
    }
    return g_plain_state > 0 ? (unsigned long) g_plain : panel_font(mgr);
}

/* the description face itself - rim and all - at a whole-number scale, in any colour */
void shim_eid_write_scaled(const char *text, float x, float y, float scale,
                           float r, float g, float bl, float a) {
    unsigned long b = shim_image_base();
    if (!b || !text || !*text) return;
    unsigned long mgr = *(const unsigned long *) (b + MANAGER);
    if (!mgr) return;
    unsigned char colour[32];
    memset(colour, 0, sizeof(colour));
    ((void (*)(void *, float, float, float, float)) (b + FN_KCOLOR))(colour, r, g, bl, a);
    ((void (*)(unsigned long, const char *, void *, int, int, float, float, float, float))
        (b + FN_DRAW_SCALED))(panel_font(mgr), text, colour, 0, 0, x, y, scale, scale);
}

float shim_eid_width_scaled(const char *text, float scale) {
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER) : 0;
    return mgr ? measure(panel_font(mgr), text) * scale : 0.0f;
}

void shim_eid_write_plain(const char *text, float x, float y, float scale,
                          float r, float g, float bl, float a) {
    unsigned long b = shim_image_base();
    if (!b || !text || !*text) return;
    unsigned long mgr = *(const unsigned long *) (b + MANAGER);
    if (!mgr) return;
    unsigned char colour[32];
    memset(colour, 0, sizeof(colour));
    ((void (*)(void *, float, float, float, float)) (b + FN_KCOLOR))(colour, r, g, bl, a);
    ((void (*)(unsigned long, const char *, void *, int, int, float, float, float, float))
        (b + FN_DRAW_SCALED))(plain_font(mgr), text, colour, 0, 0, x, y, scale, scale);
}

/* the height of a line of that face, unscaled */
float shim_eid_plain_line(void) {
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER) : 0;
    return mgr ? line_height(plain_font(mgr)) : 11.0f;
}

/*
 * An item's picture in a sprite the caller keeps - the spawner lists more
 * items than the panel's ring of twelve can hold while it is on screen, and
 * the ring is the panel's: its slots are handed out per layout, and a list
 * that scrolls inside one layout ran out of them and drew no pictures.
 */
int shim_eid_item_picture(unsigned char *s, signed char *built, int id, int trinket) {
    unsigned long b = shim_image_base();
    const char *gfx = item_gfx(id, trinket);
    if (!b || !gfx || *built < 0) return 0;
    if (!*built) {
        memset(s, 0, SPRITE_BYTES);
        ((void (*)(void *)) (b + FN_SPRITE_NEW))(s);
        long long raw[3];
        unsigned char *path = (unsigned char *) raw;
        path[23] = 0;
        shim_str_assign(path, EID_SHEETS[0]);
        ((void (*)(void *, const void *, int)) (b + FN_SPRITE_LOAD))(s, path, 1);
        shim_str_dtor(path);
        *built = s[SPRITE_LOADED] ? 1 : -1;
        if (*built < 0) return 0;
    }
    char want[160];
    long long raw[3];
    unsigned char *path = (unsigned char *) raw;
    void (*replace)(void *, int, const void *) =
        (void (*)(void *, int, const void *)) (b + FN_SPRITE_REPLACE);
    path[23] = 0;
    shim_str_assign(path, "");
    replace(s, 0, path);
    shim_str_dtor(path);
    snprintf(want, sizeof(want), "gfx/items/%s/%s", trinket ? "trinkets" : "collectibles", gfx);
    path[23] = 0;
    shim_str_assign(path, want);
    replace(s, 1, path);
    shim_str_dtor(path);
    ((void (*)(void *, int)) (b + FN_SPRITE_GRAPHICS))(s, 0);
    return 1;
}

void shim_eid_item_picture_draw(unsigned char *s, float x, float y, float alpha) {
    unsigned long b = shim_image_base();
    if (!b || !s[SPRITE_LOADED]) return;
    ((void (*)(void *, const char *, int)) (b + FN_SPRITE_FRAME))(s, ITEM_ICON.anim, 0);
    float *colour = (float *) (s + SPRITE_COLOUR);
    colour[0] = colour[1] = colour[2] = 1.0f;
    colour[3] = alpha;
    float pos[2] = { x, y };
    ((void (*)(void *, const void *, const void *, const void *)) (b + FN_SPRITE_RENDER))
        (s, pos, (const void *) (b + ZERO_VECTOR), (const void *) (b + ZERO_VECTOR));
}

float shim_eid_line_height(void) {
    unsigned long base = shim_image_base();
    unsigned long mgr = base ? *(const unsigned long *) (base + MANAGER) : 0;
    return mgr ? line_height(panel_font(mgr)) : 11.0f;
}

/*
 * A pill in a pocket is known by its colour; whether it can be described at
 * all is the pool's business, and that is asked here so the inventory does not
 * have to know about pools.
 */
int shim_eid_pill(int colour, unsigned *kind, unsigned *id) {
    int effect = pill_effect_of(colour);
    if (effect < 0) return 0;
    *kind = EID_PILL;
    *id = (unsigned) effect + 1;
    return 1;
}

/* how far along a transformation is, for the inventory's own last row */
int shim_eid_form(int form, const char **icon, const char **name, int *have, int *needed) {
    if (form < 0 || form >= EID_FORMS) return 0;
    /* the list's own player (co-op: the one who holds the map), as the mod's reminder reads its player */
    g_form_of = g_list_by && known_player(g_list_by) ? g_list_by : 0;
    int n = form_counter(form);
    g_form_of = 0;
    if (n <= 0) return 0;
    if (n > FORM_NEEDED) n = FORM_NEEDED;
    *icon = EID_FORM_ICON[form];
    const char *text = EID_FORM_NAME[form][form_language_column()];
    *name = (text && *text) ? text : EID_FORM_NAME[form][0];
    *have = n;
    *needed = FORM_NEEDED;
    return 1;
}

/*
 * One thing drawn the way the panel draws it, wherever the inventory asks:
 * picture, name and description, through the same layout. It borrows the
 * panel's working space, which is free while the list is up; the panel lays
 * itself out again when the list is put away.
 */
float shim_eid_show_item(const char *picture, const char *name, const char *text,
                         float x, float y) {
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER) : 0;
    if (!mgr) return 0.0f;
    panel_t d;
    memset(&d, 0, sizeof(d));
    d.picture = picture;
    d.name = name;
    d.text = text;
    g_lay_font = panel_font(mgr);
    panel_layout(&d);
    draw_pieces(x, y);
    return g_height;
}

/*
 * HOW TALL A DESCRIPTION'S TEXT IS, laid out and not drawn - nothing of the
 * panel on screen is touched (piece_new hands out nothing while g_dry). For a
 * list that must fit the screen it is on (eid_craft.c, the Bag of Crafting's
 * page): every interface of the port sizes itself to the screen it finds
 * (the owner, 25.09.2026).
 */
float shim_eid_text_height(const char *markup) {
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER) : 0;
    if (!mgr || !markup) return 0.0f;
    tint_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
    unsigned long font = g_lay_font;
    g_lay_font = panel_font(mgr);
    g_dry = 1;
    float h = print_bullet_points(markup, 0.0f, 0.0f, white);
    g_dry = 0;
    g_lay_font = font;
    return h;
}

/* the header's share of a panel: the name's line with a big picture, and the step after it */
float shim_eid_head_height(void) { return M_PIC_AIR + M_SECTION; }

/* a line with a big picture - a recipe's row - without laying one out (no item slot is taken) */
float shim_eid_picture_line(void) { return M_PIC_LINE; }

/*
 * The same layout, WITHOUT drawing: how tall the thing would be.
 *
 * The inventory has to know the height of every entry before it lays the list
 * out, and it cannot get it by drawing - an entry scrolled off the top is not
 * drawn at all. panel_layout and draw_pieces were already two steps, so the
 * measure is the first of them on its own.
 */
float shim_eid_measure_item(const char *picture, const char *name, const char *text) {
    unsigned long b = shim_image_base();
    unsigned long mgr = b ? *(const unsigned long *) (b + MANAGER) : 0;
    if (!mgr) return 0.0f;
    panel_t d;
    memset(&d, 0, sizeof(d));
    d.picture = picture;
    d.name = name;
    d.text = text;
    g_lay_font = panel_font(mgr);
    panel_layout(&d);
    return g_height;
}

static unsigned g_pin_kind, g_pin_id;

void shim_eid_pin(unsigned kind, unsigned id) {
    g_pin_kind = kind;
    g_pin_id = id;
    g_forget = 1;
}

/*
 * Between frames: work out what the player is standing near and remember it.
 * Nothing is drawn yet - that is the next step - but the answer is reported
 * once per change while a tester's report is open, which is how the reading is
 * checked without a single guess.
 */
/*
 * Whether the Korean face draws, answered without looking at the screen.
 *
 * The engine's own measure (0x8bdec8) walks the same glyph table the blitter
 * does, so a Korean word that measures wider than nothing has pixels behind
 * it - and the same word measured with the default face gives nought, which is
 * the fault this fixes. Runs once, in a diagnostic build, from the frame loop,
 * so the main menu is enough to answer it: no run and no eyes needed.
 */
static void korean_check(void) {
    static int said;
    if (said || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long mgr = *(const unsigned long *) (b + MANAGER);
    if (!mgr) return;
    said = 1;
    unsigned long kr = face_for("ko_kr"), cn = face_for("zh_cn");
    font_open();
    int (*measure_with)(void *, const char *) =
        (int (*)(void *, const char *)) (b + 0x8bdec8);
    char line[200];

    const char *word = "\uc544\uc774\uc791";      /* Isaac, in Hangul */
    snprintf(line, sizeof(line),
             "Korean font: %s, the word is %d wide with it and %d wide with the default",
             kr ? "yes" : "no",
             kr ? measure_with((void *) kr, word) : -1,
             g_font_failed ? -1 : measure_with(g_font, word));
    report_line("eid", line);

    /* \u60b2\u4f24\u6d0b\u8471 - Sad Onion. The second character is one eid_default lacks, so
       a width that matches the default's is a sign the face did NOT take. */
    const char *hanzi = "\u60b2\u4f24\u6d0b\u8471";
    snprintf(line, sizeof(line),
             "Chinese font: %s, the word is %d wide with it and %d wide with the default",
             cn ? "yes" : "no",
             cn ? measure_with((void *) cn, hanzi) : -1,
             g_font_failed ? -1 : measure_with(g_font, hanzi));
    report_line("eid", line);
}

/*
 * What the run is, into the report, whenever it changes.
 *
 * The conditions about Greed Mode and about the floor are only as right as the
 * two fields they read, and those cannot be checked by looking at the panel:
 * the item that would carry the line has to turn up first, and items turn up
 * at random. So the fields themselves are reported instead - start a Greed run
 * and the line says difficulty 2, walk down a floor and it says the floor.
 * That settles the reading; everything the rules do with it is settled off the
 * device.
 */
static void run_probe(void) {
    static int said;
    static int was_hard = -2, was_floor = -2;
    if (said >= 24 || !shim_report_active()) return;
    int hard = run_difficulty(), floor = run_stage();
    if (hard == was_hard && floor == was_floor) return;
    was_hard = hard;
    was_floor = floor;
    if (hard < 0 && floor < 0) return;              /* no run yet, nothing to say */
    said++;
    char line[120];
    snprintf(line, sizeof(line), "difficulty %d (%s), floor %d", hard,
             hard < 0 ? "no run" : hard == 0 ? "normal" : hard == 1 ? "hard" :
             hard == 2 ? "greed" : "greedier", floor);
    report_line("run", line);
}

/*
 * Where the player keeps the active item he is holding.
 *
 * Reading the image did not settle it: the field is not indexed by a slot
 * anywhere the disassembler could see, and the charge bar is drawn from values
 * its caller already had. So the question is put to the running game instead,
 * and in a way that answers itself: walk the player object word by word and
 * report every place holding the number of an ACTIVE item that this player
 * owns. The list of what is an active comes from the game's own items.xml
 * (eid_charge.h), and what he owns from the table at +0x1ab8 we already read.
 *
 * A field that satisfies both on two different runs, with two different
 * actives, is the one. Diagnostic builds only.
 */
static void actives_probe(void) {
    static int said;
    static unsigned was;
    if (said >= 12 || !shim_report_active()) return;
    unsigned long b = shim_image_base();
    if (!b) return;
    unsigned long me = player_now(b);
    if (!me || !readable_ptr((const void *) (me + 0x1700), 0x600)) return;

    char line[200];
    int at = 0;
    unsigned stamp = 0;
    for (unsigned off = 0x1700; off <= 0x1d00; off += 4) {
        int held = *(const int *) (me + off);
        if (held <= 0 || held > EID_ACTIVE_MAX) continue;
        if (EID_ACTIVE_KIND[held] < 0) continue;        /* not an active item */
        if (!owns_item(held)) continue;                 /* and not one he has */
        stamp = stamp * 31u + (unsigned) off * 1000u + (unsigned) held;
        if (at < (int) sizeof(line) - 24)
            at += snprintf(line + at, sizeof(line) - (size_t) at, "%s+0x%x=%d",
                           at ? " " : "", off, held);
    }
    if (!at || stamp == was) return;
    was = stamp;
    said++;
    report_line("actives", line);
}

/* ------------------------------------------------------------ the pages */
/*
 * PAGES (the owner, hotfix 1.72, 07.10.2026: "as on the recipes - already
 * proven there"). A description taller than the room under the panel's place
 * ran off the bottom of the screen: the mod draws it whole and lets it run, a
 * phone's canvas is 270 units tall, and the desk counted a long one in every
 * ordinary run (tools/eidlendesk; the research notes of 07.10.2026, EID
 * length).
 *
 * So a description that would pass the canvas's bottom is shown a page at a
 * time, the way the Bag of Crafting shows its recipes (eid_craft.c):
 *
 *   - the header - picture, name, quality, transformation - on every page;
 *   - the rows that fit under it, cut where a '#' block begins, or between
 *     two lines of one block when the block alone is taller than a page;
 *   - "...+N more" - the recipes' own words, in EID's language - above the
 *     rows when lines come before them, below when lines come after; the
 *     last of those lines carries how pages are turned, for the player's
 *     device, in the recipes' style;
 *   - turned by a sideways swipe on the panel (items.c, bagswipe.h - right the
 *     next, left the previous, round the ends), R3 and L3 on a pad (the
 *     next and the previous, both round, as the recipes' R3 - the owner,
 *     07.10.2026: a pad needs a way back), and Map held with a shot on a
 *     keyboard (down or right the next, round to the first; up or left the
 *     previous) - the mod's own keys for the recipes.
 *
 * Nothing is laid out twice: the whole panel is laid out once, as ever, every
 * piece knowing its row (g_row), and a page is the header's pieces, a run of
 * whole rows moved up, and the "more" lines. Every page is cut for the canvas
 * and the panel's place it will be shown at - the canvas's height, HUD Offset,
 * MOVE Y, the extra HUD row of Tainted Isaac - and cut again when one of
 * those changes. A description that fits is shown exactly as before.
 *
 * The page goes back to the first when the thing described changes; a new
 * layout of the same thing (an item picked up that changes a line, the
 * inventory put away) keeps the page it was on.
 */
#define PAGES_MAX       32
#define PHRASE_MORE     18                  /* "...+{1} more", the recipes' own (eid_craft.c) */
#define PAGE_SPELT_MOST "99"                /* the widest count, for measuring the line ahead */
#define FN_ACTION_HELD  0x671c5c            /* (0, action, controller): is it held (items.c) */
#define ACTION_SHOOT_LEFT  4
#define ACTION_SHOOT_RIGHT 5
#define ACTION_SHOOT_UP    6
#define ACTION_SHOOT_DOWN  7
#define ACTION_MAP         13
#define PAD_L3          (1u << 8)           /* pad.c: BTN_L3, BTN_R3 */
#define PAD_R3          (1u << 9)
unsigned shim_pad_buttons(void);
int shim_items_open(void);
#define PAGE_SHOWN_NS   250000000LL         /* "drawn lately": the zone lives while the panel does */

/*
 * How pages are turned, for the player's device: the recipes' own lines'
 * style (eid_craft.c, TOUCH_LINE and PAD_LINE), one row for every language
 * the description database carries.
 */
static const struct { const char *lang, *touch, *pad, *keys; } PAGE_HINT[] = {
    { "en_us", "(Swipe: turn pages)",
               "(Pages: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Page: {{ButtonSelect}} + shoot)" },
    { "ru",    "(\u0421\u0432\u0430\u0439\u043f: \u043b\u0438\u0441\u0442\u0430\u0442\u044c)",
               "(\u0421\u0442\u0440\u0430\u043d\u0438\u0446\u044b: {{ButtonLStick}} / {{ButtonRStick}})",
               "(\u0421\u0442\u0440\u0430\u043d\u0438\u0446\u0430: {{ButtonSelect}} + \u0441\u0442\u0440\u0435\u043b\u044c\u0431\u0430)" },
    { "de",    "(Wischen: bl\u00e4ttern)",
               "(Seiten: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Seite: {{ButtonSelect}} + schie\u00dfen)" },
    { "spa",   "(Desliza: pasar p\u00e1gina)",
               "(P\u00e1ginas: {{ButtonLStick}} / {{ButtonRStick}})",
               "(P\u00e1gina: {{ButtonSelect}} + disparar)" },
    { "fr",    "(Glisser : tourner les pages)",
               "(Pages : {{ButtonLStick}} / {{ButtonRStick}})",
               "(Page : {{ButtonSelect}} + tirer)" },
    { "ja_jp", "(\u30b9\u30ef\u30a4\u30d7: \u30da\u30fc\u30b8\u3092\u3081\u304f\u308b)",
               "(\u30da\u30fc\u30b8: {{ButtonLStick}} / {{ButtonRStick}})",
               "(\u30da\u30fc\u30b8: {{ButtonSelect}} + \u5c04\u6483)" },
    { "ko_kr", "(\uc2a4\uc640\uc774\ud504: \ud398\uc774\uc9c0 \ub118\uae30\uae30)",
               "(\ud398\uc774\uc9c0: {{ButtonLStick}} / {{ButtonRStick}})",
               "(\ud398\uc774\uc9c0: {{ButtonSelect}} + \uacf5\uaca9)" },
    { "zh_cn", "(\u6ed1\u52a8: \u7ffb\u9875)",
               "(\u7ffb\u9875: {{ButtonLStick}} / {{ButtonRStick}})",
               "(\u7ffb\u9875: {{ButtonSelect}} + \u5c04\u51fb)" },
    { "bul",   "(\u041f\u043b\u044a\u0437\u0433\u0430\u043d\u0435: \u043f\u0440\u0435\u043b\u0438\u0441\u0442\u0432\u0430\u043d\u0435)",
               "(\u0421\u0442\u0440\u0430\u043d\u0438\u0446\u0438: {{ButtonLStick}} / {{ButtonRStick}})",
               "(\u0421\u0442\u0440\u0430\u043d\u0438\u0446\u0430: {{ButtonSelect}} + \u0441\u0442\u0440\u0435\u043b\u0431\u0430)" },
    { "cs_cz", "(P\u0159ejet\u00ed: listov\u00e1n\u00ed)",
               "(Str\u00e1nky: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Str\u00e1nka: {{ButtonSelect}} + st\u0159elba)" },
    { "el_gr", "(\u03a3\u03ac\u03c1\u03c9\u03c3\u03b7: \u03b1\u03bb\u03bb\u03b1\u03b3\u03ae \u03c3\u03b5\u03bb\u03af\u03b4\u03b1\u03c2)",
               "(\u03a3\u03b5\u03bb\u03af\u03b4\u03b5\u03c2: {{ButtonLStick}} / {{ButtonRStick}})",
               "(\u03a3\u03b5\u03bb\u03af\u03b4\u03b1: {{ButtonSelect}} + \u03b2\u03bf\u03bb\u03ae)" },
    { "it",    "(Scorri: sfoglia le pagine)",
               "(Pagine: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Pagina: {{ButtonSelect}} + sparo)" },
    { "nl_nl", "(Vegen: bladeren)",
               "(Pagina's: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Pagina: {{ButtonSelect}} + schieten)" },
    { "pl",    "(Przesu\u0144: zmiana strony)",
               "(Strony: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Strona: {{ButtonSelect}} + strza\u0142)" },
    { "pt",    "(Deslizar: mudar de p\u00e1gina)",
               "(P\u00e1ginas: {{ButtonLStick}} / {{ButtonRStick}})",
               "(P\u00e1gina: {{ButtonSelect}} + disparar)" },
    { "pt_br", "(Deslize: mudar de p\u00e1gina)",
               "(P\u00e1ginas: {{ButtonLStick}} / {{ButtonRStick}})",
               "(P\u00e1gina: {{ButtonSelect}} + atirar)" },
    { "ro_ro", "(Gliseaz\u0103: r\u0103sfoie\u0219te)",
               "(Pagini: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Pagin\u0103: {{ButtonSelect}} + tragere)" },
    { "tr_tr", "(Kayd\u0131r: sayfa \u00e7evir)",
               "(Sayfalar: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Sayfa: {{ButtonSelect}} + ate\u015f)" },
    { "uk_ua", "(\u0421\u0432\u0430\u0439\u043f: \u0433\u043e\u0440\u0442\u0430\u0442\u0438)",
               "(\u0421\u0442\u043e\u0440\u0456\u043d\u043a\u0438: {{ButtonLStick}} / {{ButtonRStick}})",
               "(\u0421\u0442\u043e\u0440\u0456\u043d\u043a\u0430: {{ButtonSelect}} + \u0441\u0442\u0440\u0456\u043b\u044c\u0431\u0430)" },
    { "vi",    "(Vu\u1ed1t: l\u1eadt trang)",
               "(Trang: {{ButtonLStick}} / {{ButtonRStick}})",
               "(Trang: {{ButtonSelect}} + b\u1eafn)" },
};

static piece_t g_full[MAX_PIECES];          /* the whole panel, as laid out */
static int g_full_n;
static float g_full_height;
static int g_page_start[PAGES_MAX + 1];     /* the first row of each page; [count] = all rows */
static int g_page_count;                    /* 0 or 1: shown whole */
static int g_page;                          /* the page shown, from 0 */
static float g_paged_top = -1.0f, g_paged_canvas = -1.0f;  /* what the pages were cut for */
static int g_paged_device = -1;
static unsigned g_paged_kind, g_paged_id;
static volatile int g_page_touch;           /* a finger's ask (items.c): +1 the next, -1 the previous */
static int g_keys_resync;                   /* the keys' edges are taken afresh: a press made before counts not */
static volatile int g_touch_ok = 1;         /* the panel is player 0's: a finger may turn it (pages_tick) */

/*
 * the panel's player's device, by its class (devices.c): 0 the keyboard, 1 a pad, 2 the touch
 * screen. Co-op: the device of the player whose panel it is (main.lua 1443-1497 describes per
 * player; eid_bagofcrafting.lua 875-909 pages with bagPlayer.ControllerIndex) - another player's
 * keys turn nothing, and the hint names that player's keys
 */
static int page_device(void) {
    unsigned long b = shim_image_base();
    unsigned long me = b ? panel_player(b) : 0;
    if (!me || !readable_ptr((const void *) (me + PLAYER_DEVICE), 4)) return 2;
    int kind = shim_device_kind(*(const int *) (me + PLAYER_DEVICE));
    return kind == DEVICE_PAD ? 1 : kind == DEVICE_SCREEN ? 2 : 0;
}

static const char *page_hint(int device) {
    int row = 0;
    for (int i = 0; i < COUNT(PAGE_HINT); i++)
        if (!strcmp(PAGE_HINT[i].lang, g_lang_code)) { row = i; break; }
    return device == 1 ? PAGE_HINT[row].pad : device == 2 ? PAGE_HINT[row].touch : PAGE_HINT[row].keys;
}

/* one "more" line: the recipes' "{{Blank}} ...+N more", and how to turn when asked */
static void more_markup(char *out, size_t room, const char *count, int hint) {
    const char *said = text_of(EID_PHRASE, PHRASE_MORE);
    char line[160];
    fill_in(said && *said ? said : "...+{1} more", count, line, sizeof(line));
    if (hint) snprintf(out, room, "{{Blank}} %s %s", line, page_hint(g_paged_device));
    else snprintf(out, room, "{{Blank}} %s", line);
}

/* how tall a "more" line is, laid out and not drawn, with the hint (the taller of the two) */
static float more_height(void) {
    char markup[320];
    more_markup(markup, sizeof(markup), PAGE_SPELT_MOST, 1);
    tint_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
    int was_dry = g_dry, was_rows = g_rows_on;
    g_dry = 1; g_rows_on = 0;
    float h = print_bullet_points(markup, 0.0f, 0.0f, white);
    g_dry = was_dry; g_rows_on = was_rows;
    return h;
}

/*
 * Where the pages begin, for a panel that may be `room` tall: as many whole
 * blocks as fit under the header and the "more" lines, a page at a time; a
 * block taller than a page is cut between its lines; a page holds at least
 * one row whatever it costs, so the cutting always ends.
 */
static int pages_cut(float room) {
    int n = g_rows;
    if (n <= 0 || g_full_height <= room) return 1;
    float head = g_row[0].top, more = more_height();
    int count = 0, s = 0;
    while (s < n && count < PAGES_MAX) {
        g_page_start[count++] = s;
        float y0 = head + (count > 1 ? more : 0.0f);
        if (y0 + (g_row[n - 1].bottom - g_row[s].top) <= room) { s = n; break; }
        float avail = room - y0 - more;
        int by_block = -1, by_line = -1;
        for (int e = s + 1; e <= n; e++) {
            if (g_row[e - 1].bottom - g_row[s].top > avail) break;
            by_line = e;
            if (e == n || g_row[e].block) by_block = e;
        }
        s = by_block > s ? by_block : by_line > s ? by_line : s + 1;
    }
    g_page_start[count] = n;                /* past PAGES_MAX the last page takes the rest */
    return count;
}

/* page p into the panel's pieces: the header, the "more" lines, the page's rows moved up */
static void page_compose(int p) {
    tint_t white = { 1.0f, 1.0f, 1.0f, 1.0f };
    int n = g_rows, s = g_page_start[p], e = g_page_start[p + 1];
    char markup[320], count[16];
    g_pieces = 0;
    for (int i = 0; i < g_full_n; i++)
        if (g_full[i].row < 0 && g_pieces < MAX_PIECES) g_piece[g_pieces++] = g_full[i];
    int was_rows = g_rows_on;
    g_rows_on = 0;
    float y = g_row[0].top;
    if (s > 0) {
        snprintf(count, sizeof(count), "%d", s);
        more_markup(markup, sizeof(markup), count, e >= n);
        y = print_bullet_points(markup, 0.0f, y, white);
    }
    float dy = y - g_row[s].top;
    for (int i = 0; i < g_full_n; i++) {
        if (g_full[i].row < s || g_full[i].row >= e || g_pieces >= MAX_PIECES) continue;
        g_piece[g_pieces] = g_full[i];
        g_piece[g_pieces].py += dy;
        g_pieces++;
    }
    y += g_row[e - 1].bottom - g_row[s].top;
    if (e < n) {
        snprintf(count, sizeof(count), "%d", n - e);
        more_markup(markup, sizeof(markup), count, 1);
        y = print_bullet_points(markup, 0.0f, y, white);
    }
    g_rows_on = was_rows;
    g_height = y;
}

/* the pages cut again for where the panel stands now, the page kept where it can be */
static void pages_refresh(float top, float canvas, int device) {
    g_paged_top = top;
    g_paged_canvas = canvas;
    g_paged_device = device;
    g_page_count = pages_cut(canvas - top);
    if (g_page >= g_page_count) g_page = g_page_count - 1;
    if (g_page < 0) g_page = 0;
    if (g_page_count > 1) page_compose(g_page);
    else {
        memcpy(g_piece, g_full, sizeof(piece_t) * (size_t) g_full_n);
        g_pieces = g_full_n;
        g_height = g_full_height;
    }
}

/*
 * The walk-up panel has just been laid out whole (shim_eid_between_frames):
 * kept, and shown as its first page - or, the same thing laid out anew, the
 * page it was on. The Bag of Crafting's screen pages itself and is left be.
 */
static void pages_after_layout(int pageable, unsigned kind, unsigned id) {
    if (kind != g_paged_kind || id != g_paged_id) { g_page = 0; g_keys_resync = 1; }
    g_paged_kind = kind;
    g_paged_id = id;
    if (!pageable || g_rows < 0) { g_page_count = 0; return; }
    g_full_n = g_pieces;
    memcpy(g_full, g_piece, sizeof(piece_t) * (size_t) g_pieces);
    g_full_height = g_height;
    float x, top;
    shim_eid_panel_place(&x, &top);
    pages_refresh(top, shim_screen_height(), page_device());
}

/* nothing described any more: no pages, and the next thing starts on its first */
static void pages_forget(void) {
    g_page_count = 0;
    g_paged_kind = 0;
    g_paged_id = 0;
    g_page = 0;
    g_keys_resync = 1;
}

static int action_held(int action, int controller) {
    unsigned long b = shim_image_base();
    return b && (((int (*)(unsigned long, int, int)) (b + FN_ACTION_HELD))(0, action, controller) & 1);
}

/* items.c: is `action` held on p's own device (the mod's Input.IsActionPressed(action, ControllerIndex)) */
int shim_eid_player_held(unsigned long p, int action) {
    int d = seat_of(p);
    return d != -1 && action_held(action, d);
}

/*
 * The buttons of p's own pad (pad.c slot behind his device). Co-op with two
 * pads: the other pad's R3 is not his. A pad whose slot is not known yet
 * answers with every pad's, as before.
 */
unsigned shim_pad_slot_buttons(int slot);

static unsigned pad_buttons_of(unsigned long p) {
    int slot = shim_device_pad_slot(seat_of(p));
    return slot >= 0 ? shim_pad_slot_buttons(slot) : shim_pad_buttons();
}

static void page_turn(int to) {
    if (to == g_page || to < 0 || to >= g_page_count) return;
    g_page = to;
    page_compose(g_page);
#if ISAAC_DIAG
    if (shim_report_active()) {
        char line[80];
        snprintf(line, sizeof(line), "description page %d/%d", g_page + 1, g_page_count);
        report_line("eid", line);
    }
#endif
}

/*
 * Every frame the panel is up: the place and the canvas watched, and the page
 * turned by whatever asks - a finger (items.c), R3 and L3, Map with a shot. On the
 * first frame of a new thing the keys' state is only taken, not acted on, so a
 * button already down when the description came does not turn its page.
 */
static void pages_tick(void) {
    static unsigned pad_was;
    static int keys_was[4];
    static unsigned long me_was;
    unsigned long b = shim_image_base();
    unsigned long me = b ? panel_player(b) : 0;     /* co-op: whose panel it is (page_device) */
    if (me != me_was) { me_was = me; g_keys_resync = 1; }  /* his buttons' edges, taken afresh */
    /* a finger pages only player 0's panel: the screen is his (the owner, 11.10.2026; Esau's is Jacob's) */
    g_touch_ok = !b || seat_of(me) == seat_of(player_now(b));
    int device = page_device();
    unsigned pad_now = device == 1 ? pad_buttons_of(me) : 0;
    int keys[4] = { 0, 0, 0, 0 };
    if (device == 0 && me && readable_ptr((const void *) (me + PLAYER_DEVICE), 4)) {
        int controller = *(const int *) (me + PLAYER_DEVICE);
        if (controller != -1 && action_held(ACTION_MAP, controller)) {     /* -1: no device, no keys */
            keys[0] = action_held(ACTION_SHOOT_DOWN, controller);
            keys[1] = action_held(ACTION_SHOOT_RIGHT, controller);
            keys[2] = action_held(ACTION_SHOOT_UP, controller);
            keys[3] = action_held(ACTION_SHOOT_LEFT, controller);
        }
    }
    int r3 = (pad_now & PAD_R3) && !(pad_was & PAD_R3);
    int l3 = (pad_now & PAD_L3) && !(pad_was & PAD_L3);   /* the recipes' lock; theirs while they are up (no pages then) */
    int next_key = (keys[0] && !keys_was[0]) || (keys[1] && !keys_was[1]);
    int back_key = (keys[2] && !keys_was[2]) || (keys[3] && !keys_was[3]);
    pad_was = pad_now;
    memcpy(keys_was, keys, sizeof(keys));
    int touch = g_touch_ok ? g_page_touch : 0;
    g_page_touch = 0;
    if (g_keys_resync) { g_keys_resync = 0; r3 = l3 = next_key = back_key = 0; touch = 0; }

    if (g_page_count <= 0 || !g_name[0]) return;
    float x, top;
    shim_eid_panel_place(&x, &top);
    float canvas = shim_screen_height();
    if (top != g_paged_top || canvas != g_paged_canvas || device != g_paged_device) {
        pages_refresh(top, canvas, device);
        return;
    }
    if (g_page_count < 2 || shim_items_open()) return;
    if (pages_drawn_now() - g_drawn_ns > PAGE_SHOWN_NS) return;     /* not on screen: nothing turns */
    /* a swipe goes round as the sticks' clicks do (the owner, 07.10.2026) */
    if (touch > 0) page_turn(g_page + 1 < g_page_count ? g_page + 1 : 0);
    else if (touch < 0) page_turn(g_page > 0 ? g_page - 1 : g_page_count - 1);
    if (r3 || next_key) page_turn(g_page + 1 < g_page_count ? g_page + 1 : 0);
    if (l3) page_turn(g_page > 0 ? g_page - 1 : g_page_count - 1);
    if (back_key) page_turn(g_page - 1);
}

/* items.c, the touch thread: is the panel on screen player 0's - only then is it a finger's zone */
int shim_eid_panel_touchable(void) { return g_touch_ok; }

/* items.c: is a description up that has more than one page - its zone and its keys live while it is */
int shim_eid_pages_showing(void) {
    return shim_eid_on() && g_page_count > 1 && g_name[0]
        && pages_drawn_now() - g_drawn_ns <= PAGE_SHOWN_NS;
}

/* items.c, the touch thread: a swipe on the panel; carried out between frames */
void shim_eid_pages_touch_turn(int direction) {
    g_page_touch = direction > 0 ? 1 : direction < 0 ? -1 : 0;
}

void shim_eid_between_frames(void) {
    korean_check();
    run_probe();
    actives_probe();
    tell_the_state();
    if (!g_db) return;
    font_open();                            /* once, and it says so in the report */

    /*
     * The game's own language, read every time it changes rather than fixed at
     * startup: the menu can change it mid-session and the descriptions follow.
     */
    unsigned long base = shim_image_base();
    unsigned long manager = base ? *(const unsigned long *) (base + MANAGER) : 0;
    if (manager) {
        /*
         * The number is read every frame but acted on only once it has held
         * still for two seconds. While the game loads it walks through the
         * languages whose string tables it is reading - 11, 13, 0 in one
         * measured run, 0, 2, 4, 5, 10 in another - and a panel laid out on
         * one of those would be in the wrong language until the player picked
         * up something else.
         */
        static int was = -1, seen = -1, steady;
        int now = *(const int *) (manager + LANGUAGE);
        if (now != seen) { seen = now; steady = 0; }
        else if (steady < 120) steady++;
        if (steady >= 120 && now != was) {
            was = now;
            g_game_language = now;
            shim_eid_language(now);
            g_forget = 1;               /* the panel on screen is in the old one */
            if (shim_report_active()) {
                char line[96];
                snprintf(line, sizeof(line), "language %d -> %s (manager %p)",
                         now, g_lang_code, (void *) manager);
                report_line("eid", line);
            }
        }
    }
    /* a one-off measurement of the layout, piece by piece, for the report */
    if (shim_report_active()) {
        static int probed;
        if (!probed && g_db && manager) {
            probed = 1;
            panel_t d0;
            memset(&d0, 0, sizeof(d0));
            char titled0[200];
            snprintf(titled0, sizeof(titled0), "%s - {{Quality2}}", shim_eid_name(EID_COLLECTIBLE, 193));
            d0.picture = "{{Collectible193}}";
            d0.name = titled0;
            d0.form_icon = "Spun"; d0.form_text = "Spun! (0/3)";
            d0.text = shim_eid_text(EID_COLLECTIBLE, 193);
            g_lay_font = panel_font(manager);
            panel_layout(&d0);
            for (int i = 0; i < g_pieces; i++) {
                const piece_t *pc = &g_piece[i];
                char out[160];
                snprintf(out, sizeof(out), "piece %2d at %6.1f,%5.1f  %s%s", i,
                         (double) pc->px, (double) pc->py, pc->is_icon ? "icon " : "",
                         pc->is_icon ? pc->icon.name : pc->text);
                report_line("eid", out);
            }
            g_pieces = 0;
        }
    }

    /*
     * EID switched off: nothing is looked for and nothing is laid out - not the
     * panel, and not the Bag of Crafting's screen, whose recipes would go on
     * being worked out (and its zone on taking fingers) for a screen nobody
     * sees. The language above is still followed, so a panel switched back on
     * speaks the game's language; and it is laid out afresh then.
     */
    if (!shim_eid_on()) {
        g_forget = 1;
        g_name[0] = 0;
        g_pieces = 0;
        pages_forget();
        return;
    }

    found_t f;
    static unsigned was_kind, was_id;
    memset(&f, 0, sizeof(f));
    if (g_lang_changed) { g_lang_changed = 0; g_forget = 1; }
    if (g_forget) { g_forget = 0; was_kind = 0; was_id = 0; }
    /*
     * A player with the Bag of Crafting sees the crafting display before
     * anything else - the mod checks the bag first and, when it has something
     * to say, describes nothing else for that player (main.lua). eid_craft.c.
     */
    unsigned craft_stamp = 0;
    if (shim_eid_craft_text(g_craft, sizeof(g_craft), &craft_stamp)) {
        f.kind = EID_COLLECTIBLE;
        f.id = ITEM_BAG_OF_CRAFTING;
        f.bag = 1;
        snprintf(f.icon, sizeof(f.icon), "{{Collectible%d}}", ITEM_BAG_OF_CRAFTING);
        g_panel_by = shim_eid_craft_player();   /* the bag's player: the first who has it */
    /*
     * A thing chosen from the inventory holds the panel until the player walks
     * up to something real; then the room wins, which is what a player would
     * expect after putting the list away.
     */
    } else if (g_pin_kind && !nearest(&f)) {
        memset(&f, 0, sizeof(f));
        f.kind = g_pin_kind;
        f.id = g_pin_id;
        if (f.kind == EID_COLLECTIBLE)
            snprintf(f.icon, sizeof(f.icon), "{{Collectible%u}}", f.id);
        else if (f.kind == EID_TRINKET)
            snprintf(f.icon, sizeof(f.icon), "{{Trinket%u}}", f.id);
        else if (f.kind == EID_CARD)
            snprintf(f.icon, sizeof(f.icon), "{{Card%u}}", f.id);
        g_panel_by = g_list_by;             /* chosen from a list: that list's player */
    } else if (g_pin_kind) {
        g_pin_kind = 0;                     /* something real is nearer */
        g_panel_by = g_found_by;
    } else if (!nearest(&f)) {
        was_kind = 0; was_id = 0;
        g_name[0] = 0; g_pieces = 0;
        g_panel_by = 0;
        pages_forget();
        return;
    } else {
        g_panel_by = g_found_by;
    }
    /*
     * The panel is laid out when the thing under the player changes - and also
     * when one of the items that CHANGE a description changes hands: picking up
     * Spindown Dice, or Mom's Box, while already standing on a pedestal has to
     * add its line without stepping away and back first.
     */
    unsigned mods_now = inventory_stamp() ^ (f.golden ? 0x9e3779b9u : 0u) ^ (f.bag ? craft_stamp : 0u);
    static unsigned was_mods;
    if (f.kind == was_kind && f.id == was_id && mods_now == was_mods) { pages_tick(); return; }
    was_kind = f.kind;
    was_id = f.id;
    was_mods = mods_now;
    const char *name = shim_eid_name(f.kind, f.id);
    const char *text = shim_eid_text(f.kind, f.id);
    if (shim_report_active() && (!name || !text)) {
        static int said;
        if (said < 12) {
            said++;
            char line[160];
            snprintf(line, sizeof(line), "kind %u id %u in \"%s\": name %s, text %s", f.kind, f.id, g_lang_code,
                     name ? "yes" : "NO", text ? "yes" : "NO");
            report_line("eid", line);
        }
    }

    /*
     * Two things have no entry of their own. The dice room floor is headed by
     * its own text and the face; a pill whose colour the player has not learnt
     * yet says only that, in the mod's own words and its own colour.
     */
    char heading[160];
    int glitch_quality = -1;
    const eid_header_t *h = headers_now();
    g_name_error = 0;
    if (f.kind == EID_DICE) {
        snprintf(heading, sizeof(heading), "%s (%u)", h ? h->dice : "Dice Room", f.id);
        name = heading;
    } else if (f.kind == EID_SACRIFICE) {
        snprintf(heading, sizeof(heading), "%s (%u/%d)",
                 h ? h->sacrifice : "Next Sacrifice", f.id, SACRIFICE_STEPS);
        name = heading;
        /*
         * The first two payments read differently on the first floor: their
         * text ENDS with the bomb that only drops in the basement ("coin#bomb").
         * The mod keeps what stands before the first '#' anywhere deeper
         * (eid_grid_descriptions.lua, sub(1, splitPoint - 1)), and so does this -
         * it used to keep what stood after it, the bomb line (24.09.2026).
         */
        if (f.id <= 2 && absolute_stage() > 1 && text) {
            const char *cut = strchr(text, '#');
            if (cut) {
                static char shortened[512];
                snprintf(shortened, sizeof(shortened), "%.*s", (int) (cut - text), text);
                text = shortened;
            }
        }
    } else if (f.kind == EID_GLITCHED) {
        static char glitch_name[96], glitch_text[2400];
        if (shim_eid_glitch_name((int) f.id, glitch_name, sizeof(glitch_name), &glitch_quality))
            name = glitch_name;
        if (g_glitch_info)
            text = shim_eid_glitch_text((int) f.id, glitch_text, sizeof(glitch_text)) ? glitch_text : NULL;
        else
            text = shim_eid_glitch_own_text((int) f.id, glitch_text, sizeof(glitch_text)) ? glitch_text : NULL;
    } else if ((f.kind == EID_PILL || f.kind == EID_HORSEPILL) && f.id == 0) {
        name = (h && h->unidentified[0]) ? h->unidentified : "Unidentified Pill";
        text = NULL;
        g_name_error = 1;
    }
    /*
     * Under the curse the mod shows a question mark and nothing else - no
     * name, no text - and that is the whole point of the curse.
     */
    if (f.hidden) {
        snprintf(f.icon, sizeof(f.icon), "{{QuestionMark}}");
        name = "";
        text = NULL;
    }
    snprintf(g_name, sizeof(g_name), "%s", name ? name : "");
    /*
     * Laid out once, when the thing under the player changes, rather than
     * every frame: measuring a paragraph sixty times a second for a picture
     * that does not move would be work for nothing.
     */
    /*
     * A hundred and thirty units wide, which is the mod's own text box
     * (eid_config.lua) - not the whole screen. Only a narrow phone held
     * upright would ever have less room than that.
     */
    panel_t d;
    memset(&d, 0, sizeof(d));
    char titled[200], contents[200], form_text[96];
    const eid_item_t *it = NULL;
    if (f.kind == EID_COLLECTIBLE) it = item_row((int) f.id, 0);
    else if (f.kind == EID_TRINKET) it = item_row((int) f.id, 1);
    /* "Name - {{Quality2}}", as printDescription writes it - for collectibles
       only: the mod asks ItemConfig:GetCollectible (getObjectQuality), and a
       trinket's quality 0 stood beside every trinket name (24.09.2026) */
    int quality = shim_modset_int(MS_EID_QUALITY);            /* ShowQuality, a row of the page */
    if (quality && it && f.kind == EID_COLLECTIBLE)
        snprintf(titled, sizeof(titled), "%s - {{Quality%u}}", name ? name : "", it->quality);
    else if (quality && f.kind == EID_GLITCHED && glitch_quality >= 0 && glitch_quality <= 4)
        snprintf(titled, sizeof(titled), "%s - {{Quality%d}}", name ? name : "", glitch_quality);
    else    snprintf(titled, sizeof(titled), "%s", name ? name : "");
    d.picture = f.icon;
    d.type_icon = it ? (it->kind == 1 ? 1 : (it->kind == 2 ? 2 : 0)) : 0;
    d.charge_kind = it ? it->chargetype : 0;
    d.charges = it ? it->charges : 0;
    d.name = titled;
    d.name_error = g_name_error;
    const char *form_icon = NULL;
    /* TransformationText/Icons/Progress, one row of the page for the three */
    if (shim_modset_int(MS_EID_TRANSFORM)
        && form_of_panel(f.kind, f.id, &form_icon, form_text, sizeof(form_text))) {
        d.form_icon = form_icon;
        d.form_text = form_text;
    }
    contents[0] = 0;
    (void) contents;
    if (f.bag) {
        text = g_craft;
        goto laid_out;
    }
    /*
     * Birthright, which is a different item for every character.
     *
     * The mod throws the shared description away and writes the one belonging
     * to the character holding it (eid_modifiers.lua, BirthrightCallback): the
     * character's own head, their name in the mod's Isaac colour, and the
     * effect below. The table is the mod's, carried into our database as
     * section 8 - one row per player type, in every language the mod has.
     *
     * Co-op is not copied: the mod writes one block per player in the room and
     * this port has one player.
     */
    static char birth[1200];
    if (f.kind == EID_COLLECTIBLE && f.id == ITEM_BIRTHRIGHT) {
        int type = player_kind();
        const char *who = type >= 0 ? shim_eid_name(EID_BIRTHRIGHT, (unsigned) type) : NULL;
        const char *what = type >= 0 ? shim_eid_text(EID_BIRTHRIGHT, (unsigned) type) : NULL;
        if (what && *what) {
            snprintf(birth, sizeof(birth), "{{Player%d}} {{ColorIsaac}}%s{{CR}}#%s",
                     type, who ? who : "", what);
            text = birth;
        }
    }
    /*
     * And everything the mod adds to a description it did not write itself:
     * the golden trinket's doubled numbers (which rewrite the description
     * rather than add to it), the warning on an item that is spent when used,
     * the dice's guesses, and what a mimic would pay for a card or a pill.
     * Each one leaves the panel alone when the player has nothing to do with
     * it, so at most a couple ever appear at once.
     */
    static char conditioned[3000];
    conditions_apply(&f, text, conditioned, sizeof(conditioned));
    if (conditioned[0]) text = conditioned;

    /* and the health lines, for a character whose hearts are not red */
    static char healthed[3000];
    healthed[0] = 0;
    if (shim_modset_int(MS_EID_HEALTHUP)) health_up_apply(&f, text, healthed, sizeof(healthed));
    if (healthed[0]) text = healthed;

    /* and Pandora's Box, which highlights the floor you are on */
    static char pandora[3000];
    pandora_apply(&f, text, pandora, sizeof(pandora));
    if (pandora[0]) text = pandora;

    static char gold[3200];
    golden_apply(&f, text, gold, sizeof(gold));
    if (gold[0]) text = gold;

    static char whole[3600];
    static char virtues[700], abyss[700];
    char feather[260], phd[300], experimental[420];
    virtues_line(&f, virtues, sizeof(virtues));
    abyss_line(&f, abyss, sizeof(abyss));
    black_feather_line(&f, feather, sizeof(feather));
    false_phd_line(&f, phd, sizeof(phd));
    experimental_pill_line(&f, experimental, sizeof(experimental));
    char spin[220], charge[240], luck[260];
    static char prize[900];
    spindown_line(f.kind, f.id, spin, sizeof(spin));
    charge_line(&f, charge, sizeof(charge));
    luck_line(&f, luck, sizeof(luck));
    consolation_line(&f, prize, sizeof(prize));
    const char *warning = single_use(&f) ? text_of(EID_PHRASE, PHRASE_SINGLE_USE) : NULL;
    if (spin[0] || charge[0] || luck[0] || virtues[0] || abyss[0] || feather[0]
        || phd[0] || experimental[0] || prize[0] || (warning && *warning)) {
        whole[0] = 0;
        add_piece(whole, sizeof(whole), warning);
        if (text && *text) {
            if (whole[0]) add_piece(whole, sizeof(whole), "#");
            add_piece(whole, sizeof(whole), text);
        }
        add_piece(whole, sizeof(whole), prize);
        add_piece(whole, sizeof(whole), feather);
        add_piece(whole, sizeof(whole), phd);
        add_piece(whole, sizeof(whole), experimental);
        add_piece(whole, sizeof(whole), virtues);
        add_piece(whole, sizeof(whole), abyss);
        add_piece(whole, sizeof(whole), luck);
        add_piece(whole, sizeof(whole), spin);
        add_piece(whole, sizeof(whole), charge);
        text = whole;
    }
laid_out:
    d.text = text;
    g_lay_font = manager ? panel_font(manager) : 0;
    g_rows_on = 1;                          /* every piece knows its row: the pages are cut from them */
    panel_layout(&d);
    g_rows_on = 0;
    pages_after_layout(!f.bag, f.kind, f.id | (f.golden ? 0x8000u : 0u));
    pages_tick();
    LOG("eid: kind %u id %u -> %s: %s", f.kind, f.id,
        name ? name : "(no name)", text ? text : "(no text)");
    /*
     * A few of these and no more. Every item the player walks past used to
     * write four hundred bytes into the report, which in an hour of somebody
     * else's play is a fat file of descriptions and not the measurement the
     * file is for. The first forty prove the database and the language work.
     */
    if (shim_report_active()) {
        static int said;
        if (said < 40) {
            said++;
            char line[400];
            snprintf(line, sizeof(line), "kind %u id %u [%s] %s: %s", f.kind, f.id, g_lang_code,
                     name ? name : "(no name)", text ? text : "(no text)");
            report_line("eid", line);
        }
    }
}
