/*
 * GLITCHED ITEMS: what a TMTRAINER collectible does, in words.
 *
 * The EID mod has two ways of describing one (features/eid_tmtrainer.lua,
 * CheckGlitchedItemConfig). Without REPENTOGON it can only read the item's
 * config: the hearts, bombs, coins and keys it hands out. With REPENTOGON it
 * reads the procedural item itself - the item it grants, what it does to the
 * stats and every "when X happens, do Y" it carries. REPENTOGON gets that by
 * reading the game's own structures, and so does the port, so this is the
 * full description, line for line in the mod's order and markup.
 *
 * WHAT WAS READ IN THE IMAGE (23.09.2026):
 *
 *   ItemConfig::GetCollectible 0x73f7f4 sends a negative id to 0x5d07f8 with
 *   the procedural item manager at Game+0x2a58c8: a vector of ProceduralItem*,
 *   index ~id (-1 is 0).
 *   ProceduralItem  +0x00 its own ItemConfig item
 *                   +0x18 the ItemConfig item it grants (null: none)
 *                   +0x20 six floats: speed, fire delay, damage, range, shot
 *                         speed, luck - summed per player at 0x5d14d4
 *                   +0x38 vector<Effect*>
 *   Effect          +0x00 when: 0 used, 1 tear fired, 2 enemy hit, 3 enemy
 *                         killed, 4 damage taken, 5 room cleared, 6 entity
 *                         spawned, 7 pickup collected, 8 chained to the last
 *                         (0x5d0a58, 0x5d0b38)
 *                   +0x04 what: 0 use an active, 1 its effect for the room,
 *                         2 convert, 3 area damage, 4 spawn, 5 fart (the
 *                         switch at 0x5d0cac)
 *                   +0x0c the spawned entity it waits for: type << 16 |
 *                         variant, 0xffff for any variant
 *                   +0x10 the item id, or the entity to spawn / convert from
 *                   +0x14 the entity to convert to
 *                   +0x18 area damage's damage
 *                   +0x1c area damage's and a fart's radius
 *   ItemConfig item +0x00 type (3 active), +0x08 name, +0xc8 quality,
 *                   +0x58 max hearts, +0x5c hearts, +0x60 soul hearts,
 *                   +0x64 black hearts, +0x68 bombs, +0x6c keys, +0x70 coins
 *                   (the items.xml reader, 0x73857c)
 *
 * The words are the mod's GlitchedItemText and VoidNames, in eid.bin sections
 * 18 and 17 (tools/eid_more.py), and the entity names its EID.XMLEntityNames
 * (tools/eid_entities.py).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "eid_entities.h"

unsigned long shim_image_base(void);
const char *shim_eid_row(unsigned kind, unsigned id);           /* text, English when missing */
const char *shim_eid_name_of(unsigned kind, unsigned id);       /* name, English when missing */
void shim_eid_pluralize(char *text, unsigned long room, int amount);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

#define GAME_PTR            0xac3b90
#define GAME_PROCEDURAL     0x2a58c8        /* the procedural item manager */

#define PROC_CONFIG         0x00
#define PROC_TARGET         0x18
#define PROC_STATS          0x20
#define PROC_EFFECTS        0x38

#define EFFECT_WHEN         0x00
#define EFFECT_WHAT         0x04
#define EFFECT_WHEN_ENTITY  0x0c
#define EFFECT_THING        0x10
#define EFFECT_THING_TO     0x14
#define EFFECT_DAMAGE       0x18
#define EFFECT_RADIUS       0x1c

#define CONFIG_TYPE         0x00
#define CONFIG_ID           0x04
#define CONFIG_NAME         0x08            /* std::string */
#define CONFIG_DESCRIPTION  0x20            /* std::string, the garbled text for these */
#define CONFIG_QUALITY      0xc8
#define CONFIG_ADDS         0x58            /* seven ints, the mod's order below */
#define TYPE_TRINKET        2
#define TYPE_ACTIVE         3

#define KIND_COLLECTIBLE    1
#define KIND_TRINKET        2
#define KIND_VOID_NAMES     17              /* rows 0..5 */
#define KIND_GLITCH_TEXT    18

/* eid.bin section 18, the rows tools/eid_more.py gives each key */
#define ROW_ADDS            21              /* AddMaxHearts .. AddKeys */
#define ROW_WHEN            30              /* + the game's trigger number */
#define ROW_WHAT            40              /* + the game's action number */
#define ROW_GRANTS          50
#define ROW_ENTITY_FIRST    100
#define ROW_ENTITY_LAST     160

#define WHEN_ACTIVE         0
#define WHEN_SPAWNED        6
#define WHEN_CHAIN          8
#define WHEN_COUNT          9
#define WHAT_USE            0
#define WHAT_EFFECT         1
#define WHAT_CONVERT        2
#define WHAT_AREA           3
#define WHAT_SPAWN          4
#define WHAT_FART           5
#define WHAT_COUNT          6

/* the order the mod reads the config in: AddMaxHearts, AddHearts, AddSoulHearts,
   AddBlackHearts, AddBombs, AddCoins, AddKeys - and where each one lives */
static const unsigned char ADD_AT[7] = { 0x58, 0x5c, 0x60, 0x64, 0x68, 0x70, 0x6c };

/* triggerColors, by the game's trigger number */
static const char *const WHEN_COLOUR[WHEN_COUNT] = {
    "{{ColorLime}}", "{{ColorCyan}}", "{{ColorEIDError}}", "{{ColorRed}}", "{{ColorRed}}",
    "{{ColorOrange}}", "", "{{ColorYellow}}", "{{ColorSilver}}",
};

/* REPENTOGON's stat getters are multiplied by these before they are printed */
static const float STAT_MULT[6] = { 0.2f, 1.0f, 1.0f, 1.5f, 0.2f, 1.0f };

static int readable(unsigned long p, unsigned long n) {
    int shim_eid_readable(const void *p, unsigned long n);
    return p && shim_eid_readable((const void *) p, n);
}

static const char *cxx_string(unsigned long s) {
    if (!readable(s, 24)) return "";
    const unsigned char *raw = (const unsigned char *) s;
    return (raw[23] & 0x80) ? *(const char *const *) raw : (const char *) raw;
}

/* the ProceduralItem behind a glitched id, or 0 */
static unsigned long procedural(int id) {
    unsigned long b = shim_image_base();
    if (!b || id >= 0) return 0;
    unsigned long game = *(const unsigned long *) (b + GAME_PTR);
    if (!game) return 0;
    unsigned long vec = game + GAME_PROCEDURAL;
    if (!readable(vec, 16)) return 0;
    unsigned long begin = *(const unsigned long *) vec, end = *(const unsigned long *) (vec + 8);
    unsigned index = ~(unsigned) id;
    if (!begin || end < begin || index >= (end - begin) / 8) return 0;
    unsigned long item = ((const unsigned long *) begin)[index];
    return readable(item, PROC_EFFECTS + 16) ? item : 0;
}

static void add(char *out, size_t room, const char *s) {
    size_t at = strlen(out);
    if (s && at + 1 < room) snprintf(out + at, room - at, "%s", s);
}

/* EID:ReplaceVariableStr(text, key, value): every {key} */
static void put_value(char *text, size_t room, const char *key, const char *value) {
    char marker[16];
    snprintf(marker, sizeof(marker), "{%s}", key);
    for (int guard = 0; guard < 8; guard++) {
        char *at = strstr(text, marker);
        if (!at) return;
        size_t head = (size_t) (at - text), mlen = strlen(marker), vlen = strlen(value);
        size_t tail = strlen(at + mlen);
        if (head + vlen + tail + 1 > room) return;
        memmove(text + head + vlen, at + mlen, tail + 1);
        memcpy(text + head, value, vlen);
    }
}

/* Lua's string.format("%.4g") / ("%.2g"): no trailing zeros, as C's %g */
static void number(char *out, size_t n, float v, int digits) {
    snprintf(out, n, digits == 4 ? "%.4g" : "%.2g", (double) v);
}

static float simple_round(float v) {           /* EID:SimpleRound(num), 2 places */
    return floorf(v * 100.0f + 0.5f) / 100.0f;
}

/* "{1}" filled with the value; a language that wrote no {1} gets it in front */
static void stat_line(char *out, size_t room, const char *prefix, const char *words, const char *value) {
    char line[160];
    snprintf(line, sizeof(line), "%s", words ? words : "");
    if (strstr(line, "{1}")) put_value(line, sizeof(line), "1", value);
    else {
        char with[160];
        snprintf(with, sizeof(with), "%s %s", value, line);
        snprintf(line, sizeof(line), "%s", with);
    }
    add(out, room, prefix);
    add(out, room, line);
    add(out, room, "#");
}

/* entityToName(): the language's own word first, then the mod's XML names */
static void entity_name(int type, int variant, int plural, char *out, size_t n) {
    char key[24], zero[24];
    snprintf(key, sizeof(key), "%d.%d", type, variant);
    snprintf(zero, sizeof(zero), "%d.%d", type, variant < 0 ? 0 : variant);
    const char *name = NULL;
    for (unsigned row = ROW_ENTITY_FIRST; row <= ROW_ENTITY_LAST && !name; row++) {
        const char *k = shim_eid_name_of(KIND_GLITCH_TEXT, row);
        if (!k) continue;
        if (!strcmp(k, key)) name = shim_eid_row(KIND_GLITCH_TEXT, row);
    }
    for (unsigned row = ROW_ENTITY_FIRST; row <= ROW_ENTITY_LAST && !name; row++) {
        const char *k = shim_eid_name_of(KIND_GLITCH_TEXT, row);
        if (k && !strcmp(k, zero)) name = shim_eid_row(KIND_GLITCH_TEXT, row);
    }
    if (!name) {                                /* GetEntityXMLName(Type, Variant, 0) */
        const char *by_variant = NULL, *by_zero = NULL;
        for (unsigned i = 0; i < sizeof(EID_ENTITY) / sizeof(EID_ENTITY[0]); i++) {
            if (EID_ENTITY[i].type != type || EID_ENTITY[i].variant != variant) continue;
            if (EID_ENTITY[i].subtype == -1) by_variant = EID_ENTITY[i].name;
            if (EID_ENTITY[i].subtype == 0) by_zero = EID_ENTITY[i].name;
        }
        name = by_variant ? by_variant : by_zero;
    }
    snprintf(out, n, "%s", name ? name : key);
    if (plural) {
        size_t at = strlen(out);
        if (at + 12 < n) snprintf(out + at, n - at, "{pluralize}");
        shim_eid_pluralize(out, n, 2);
        put_value(out, n, "pluralize", "");
    }
}

/* the entity an int of the effect names: type << 16 | variant, 0xffff for any */
static void packed_entity(unsigned packed, int plural, char *out, size_t n) {
    int type = (int) (packed >> 16);
    int variant = (int) (packed & 0xffff);
    if (variant == 0xffff) variant = -1;
    entity_name(type, variant, plural, out, n);
}

static void item_name(unsigned long config, char *out, size_t n) {
    int type = *(const int *) (config + CONFIG_TYPE);
    int id = *(const int *) (config + CONFIG_ID);
    const char *name = shim_eid_name_of(type == TYPE_TRINKET ? KIND_TRINKET : KIND_COLLECTIBLE, (unsigned) id);
    snprintf(out, n, "%s", name ? name : cxx_string(config + CONFIG_NAME));
}

/* EID:getXMLDescription: the item's own description, the garbled one */
int shim_eid_glitch_own_text(int id, char *out, unsigned long n) {
    unsigned long item = procedural(id);
    unsigned long config = item ? *(const unsigned long *) (item + PROC_CONFIG) : 0;
    out[0] = 0;
    if (!readable(config, CONFIG_DESCRIPTION + 24)) return 0;
    snprintf(out, n, "%s", cxx_string(config + CONFIG_DESCRIPTION));
    return out[0] != 0;
}

/* the glitched item's own name and quality, from its config */
int shim_eid_glitch_name(int id, char *out, unsigned long n, int *quality) {
    unsigned long item = procedural(id);
    unsigned long config = item ? *(const unsigned long *) (item + PROC_CONFIG) : 0;
    if (!readable(config, CONFIG_QUALITY + 4)) return 0;
    snprintf(out, n, "%s", cxx_string(config + CONFIG_NAME));
    if (quality) *quality = *(const int *) (config + CONFIG_QUALITY);
    return 1;
}

/* EID:CheckGlitchedItemConfig, the REPENTOGON path */
int shim_eid_glitch_text(int id, char *out, unsigned long room) {
    out[0] = 0;
    unsigned long item = procedural(id);
    unsigned long config = item ? *(const unsigned long *) (item + PROC_CONFIG) : 0;
    if (!readable(config, CONFIG_ADDS + 0x1c)) return 0;

    add(out, room, "#");
    /* the hearts, bombs, coins and keys the config hands out on pickup */
    for (int i = 0; i < 7; i++) {
        int raw = *(const int *) (config + ADD_AT[i]);
        if (!raw) continue;
        float val = (float) raw;
        if (i < 4) val /= 2.0f;                 /* the Add Heart attributes count halves */
        char s[24], shown[28];
        number(s, sizeof(s), val, 4);
        const char *prefix = "↑ ";
        if (val > 0) snprintf(shown, sizeof(shown), "+%s", s);
        else { snprintf(shown, sizeof(shown), "%s", s); prefix = "↓ "; }
        stat_line(out, room, prefix, shim_eid_row(KIND_GLITCH_TEXT, ROW_ADDS + (unsigned) i), shown);
        shim_eid_pluralize(out, room, (int) val);
    }

    /* the item it grants */
    unsigned long target = *(const unsigned long *) (item + PROC_TARGET);
    if (readable(target, CONFIG_NAME + 24)) {
        int trinket = *(const int *) (target + CONFIG_TYPE) == TYPE_TRINKET;
        char name[96], line[200];
        item_name(target, name, sizeof(name));
        const char *grants = shim_eid_row(KIND_GLITCH_TEXT, ROW_GRANTS);
        snprintf(line, sizeof(line), "%s{{%s%d}} %s#", grants ? grants : "",
                 trinket ? "Trinket" : "Collectible", *(const int *) (target + CONFIG_ID), name);
        add(out, room, line);
    }

    int last = WHEN_CHAIN;
    if (*(const int *) (config + CONFIG_TYPE) == TYPE_ACTIVE) {
        last = WHEN_ACTIVE;
        add(out, room, "#{{Blank}} ");
        add(out, room, WHEN_COLOUR[WHEN_ACTIVE]);
        add(out, room, shim_eid_row(KIND_GLITCH_TEXT, ROW_WHEN + WHEN_ACTIVE));
    }

    /* what it does to the stats */
    const float *stat = (const float *) (item + PROC_STATS);
    for (int i = 0; i < 6; i++) {
        float val = stat[i] * STAT_MULT[i];
        if (val == 0.0f) continue;
        char s[24], shown[28];
        number(s, sizeof(s), simple_round(val), 2);
        const char *prefix = "↑ ";
        if (val > 0) snprintf(shown, sizeof(shown), "+%s", s);
        else { snprintf(shown, sizeof(shown), "%s", s); prefix = "↓ "; }
        stat_line(out, room, prefix, shim_eid_row(KIND_VOID_NAMES, (unsigned) i), shown);
    }

    /* and the effects, one trigger heading for each run of the same trigger */
    unsigned long vec = item + PROC_EFFECTS;
    unsigned long begin = *(const unsigned long *) vec, end = *(const unsigned long *) (vec + 8);
    char when_entity[96] = "";
    for (unsigned long at = begin; begin && at < end && readable(at, 8); at += 8) {
        unsigned long effect = *(const unsigned long *) at;
        if (!readable(effect, EFFECT_RADIUS + 4)) break;
        int when = *(const int *) (effect + EFFECT_WHEN);
        int what = *(const int *) (effect + EFFECT_WHAT);
        if (when < 0 || when >= WHEN_COUNT || what < 0 || what >= WHAT_COUNT) continue;
        if (when == WHEN_SPAWNED) {
            char name[80];
            packed_entity(*(const unsigned *) (effect + EFFECT_WHEN_ENTITY), 0, name, sizeof(name));
            snprintf(when_entity, sizeof(when_entity), "{{ColorEIDObjName}}%s{{CR}}", name);
        }
        char r1[120] = "", r2[120] = "";
        if (what == WHAT_EFFECT || what == WHAT_USE) {
            int thing = *(const int *) (effect + EFFECT_THING);
            const char *name = shim_eid_name_of(KIND_COLLECTIBLE, (unsigned) thing);
            snprintf(r1, sizeof(r1), "{{Collectible%d}} %s", thing, name ? name : "");
        } else if (what == WHAT_CONVERT) {
            char from[80], to[80];
            packed_entity(*(const unsigned *) (effect + EFFECT_THING), 1, from, sizeof(from));
            packed_entity(*(const unsigned *) (effect + EFFECT_THING_TO), 0, to, sizeof(to));
            snprintf(r1, sizeof(r1), "{{ColorGray}}%s{{CR}}", from);
            snprintf(r2, sizeof(r2), "{{ColorGray}}%s{{CR}}", to);
        } else if (what == WHAT_SPAWN) {
            char name[80];
            packed_entity(*(const unsigned *) (effect + EFFECT_THING), 0, name, sizeof(name));
            snprintf(r1, sizeof(r1), "{{ColorGray}}%s{{CR}}", name);
        } else if (what == WHAT_FART) {
            number(r1, sizeof(r1), simple_round(*(const float *) (effect + EFFECT_RADIUS)), 4);
        } else if (what == WHAT_AREA) {
            number(r1, sizeof(r1), simple_round(*(const float *) (effect + EFFECT_DAMAGE)), 4);
            number(r2, sizeof(r2), simple_round(*(const float *) (effect + EFFECT_RADIUS)), 4);
        }

        if (when != last) {
            if (when != WHEN_CHAIN) add(out, room, "#{{Blank}} ");
            add(out, room, WHEN_COLOUR[when]);
            add(out, room, shim_eid_row(KIND_GLITCH_TEXT, ROW_WHEN + (unsigned) when));
        } else if (when != WHEN_CHAIN) {
            add(out, room, "#");
        }
        /*
         * `attributes .. localizedNames[effectType] or localizedNamesEnglish[effectType] .. "#"`
         * - Lua binds `..` before `or`, so the action is appended WITHOUT a
         * closing "#" whenever the language has the word, which is always.
         * The layout that gives is the mod's; it is kept.
         */
        add(out, room, shim_eid_row(KIND_GLITCH_TEXT, ROW_WHAT + (unsigned) what));
        if (when_entity[0]) put_value(out, room, "T1", when_entity);
        if (r1[0]) put_value(out, room, "1", r1);
        if (r2[0]) put_value(out, room, "2", r2);
        if (when != WHEN_CHAIN) last = when;
    }
    /* PROBE: what a glitched item came to, the first few */
    static int said;
    if (shim_report_active() && said < 8) {
        said++;
        char line[900];
        snprintf(line, sizeof(line), "id %d, %ld effects, stats %.2f %.2f %.2f %.2f %.2f %.2f: %.700s", id,
                 (long) ((end - begin) / 8), (double) stat[0], (double) stat[1], (double) stat[2],
                 (double) stat[3], (double) stat[4], (double) stat[5], out);
        report_line("glitch", line);
    }
    return out[0] != 0;
}
