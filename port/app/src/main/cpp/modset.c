/*
 * THE SETTINGS OF THE PORTED MODS, in sections of port-touch.ini.
 *
 * On the PC each of these mods has a page in Mod Config Menu. The port has
 * the Tweaks page for what is switched often - whether a mod is on at all -
 * and puts the rest where a page of forty rows would be a heap: in the port's
 * settings file, one section a mod (the owner, 16.09.2026):
 *
 *   [specialist]    Specialist for Good Items
 *   [bossbars]      Enhanced Boss Bars
 *   [wavecounter]   Boss Rush Wave Counter
 *   [statsplus]     Stats+
 *   [rangefix]      Range Fix (Bones & Sword, Bag of Crafting)
 *
 * Every setting is one row of the table below - its section, key, kind,
 * default and range, and the line of help written above it - so the reader,
 * the writer and the defaults cannot drift apart. The defaults are the mods'
 * own. Planetarium Chance and Completion Mark Reminder have no settings.
 *
 * sticks.c owns the file: it hands every line inside a section here, and asks
 * for the sections when it writes the file out.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "modset.h"

/* the bar styles are bossbars.c's table */
int shim_bossbars_style_count(void);
const char *shim_bossbars_style_name(int i);

typedef enum { K_BOOL, K_INT, K_FLOAT, K_CHOICE, K_LIST, K_RGB } kind_t;

typedef struct {
    const char *section, *key;
    kind_t kind;
    float def, lo, hi;
    const char *const *words;               /* a choice's words; NULL for the bar styles */
    int word_count;
    const char *list_def;                   /* a list's or a colour's default */
    const char *help;
} setting_t;

static const char *const POSITIONS[] = { "bottom", "top", "left", "right" };
static const char *const SORTINGS[] = { "segments", "bosses", "vanilla" };
static const char *const TEXTS[] = { "none", "percent", "hp" };
static const char *const PRESETS[] = { "default", "foundhud", "bossbar" };
static const char *const BRACKETS[] = { "none", "square", "round", "curly", "angle" };
static const char *const WHENS[] = { "always", "map", "drop" };
static const char *const SHOWS[] = { "none", "d8", "damagemult", "tearcap" };
static const char *const SORTS[] = { "quality", "name" };
static const char *const BAG_SHOWS[] = { "always", "hold", "never" };
static const char *const DANCE_TRACKS[] = { "classic", "tainted", "sad", "glitched" };
static const char *const COLOURS[] = { "none", "grey", "red", "green", "blue", "orange", "magenta",
                                       "cyan" };

#define WORDS(w) (w), (int) (sizeof(w) / sizeof((w)[0]))

static const setting_t SETTINGS[MS_COUNT] = {
    /*
     * [eid] - the settings of EID's Mod Config Menu that a player of this port
     * changes (the owner, 24.09.2026). The defaults are the PC mod's, but for
     * the opacity: the port has always drawn at 1.0 and keeps it. The panel's
     * place and size stay the port's own; Options? follows the PC (a card from
     * an Options? spawn is a question mark, a pill is not) and is no row.
     */
    [MS_EID_OPACITY] = { "eid", "opacity", K_FLOAT, 1.0f, 0.1f, 1.0f, NULL, 0, NULL,
        "how opaque the panel is, 0.1 .. 1" },
    [MS_EID_HIDE_BATTLE] = { "eid", "hideinbattle", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = no descriptions while enemies are in the room" },
    [MS_EID_SHOW_BLIND] = { "eid", "showoncurse", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = describe items under Curse of the Blind as well" },
    [MS_EID_QUALITY] = { "eid", "quality", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = an item's quality beside its name" },
    [MS_EID_TRANSFORM] = { "eid", "transformations", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = the transformation an item counts towards, with its progress" },
    [MS_EID_STAT_ICONS] = { "eid", "staticons", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = the stat icons in front of stat lines" },
    [MS_EID_HEALTHUP] = { "eid", "dynamichealthups", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = health lines rewritten for characters whose hearts are not red" },
    [MS_EID_DISTANCE] = { "eid", "distance", K_INT, 5, 1, 10, NULL, 0, NULL,
        "how far a thing is described from, in tiles" },
    [MS_EID_SHOP_CARDS] = { "eid", "shopcards", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = describe cards for sale" },
    [MS_EID_SHOP_PILLS] = { "eid", "shoppills", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = describe pills for sale" },
    [MS_EID_SHOP_SOULS] = { "eid", "shopsoulstones", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = describe soul stones for sale" },
    [MS_EID_UNREACHABLE] = { "eid", "unreachable", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = describe cards and pills there is no way to walk to" },
    [MS_EID_FLIGHT] = { "eid", "flight", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = a flying player is told about everything, reachable or not" },
    [MS_EID_UNIDENTIFIED] = { "eid", "unidentified", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = the effect of a pill the player has not identified yet" },
    [MS_EID_SPINDOWN] = { "eid", "spindown", K_INT, 3, 0, 10, NULL, 0, NULL,
        "how many Spindown Dice results are shown ahead (0 = none)" },
    /*
     * HOLD, not the mod's "always": the screen of recipes only while the bag is
     * held up, the room's descriptions the rest of the time - what was agreed
     * with the owner (the ninth round's sheet: recipes "while the bag is up").
     * A key of its own once more (03.10.2026): every file written so far holds
     * "bagdisplay=always", which is the old default written out with the rest,
     * not anybody's choice - read, it kept the screen up all the time. The key
     * before that (bagofcrafting) was on/off and would read as a choice's number.
     */
    [MS_EID_BAG] = { "eid", "bagshow", K_CHOICE, MS_BAG_HOLD, 0, 0, WORDS(BAG_SHOWS), NULL,
        "when the Bag of Crafting screen shows: always, hold (only while the bag is held up), never" },
    [MS_EID_BAG_RESULTS] = { "eid", "recipes", K_INT, 7, 1, 11, NULL, 0, NULL,
        "recipes shown on one page of the Bag of Crafting screen" },
    [MS_EID_BAG_SORT] = { "eid", "recipesort", K_CHOICE, 0, 0, 0, WORDS(SORTS), NULL,
        "the recipes in order of quality, or by name" },
    /* ours, not the mod's: where the panel stands (the owner, 24.09.2026) */
    [MS_EID_X] = { "eid", "x", K_INT, 0, -100, 200, NULL, 0, NULL,
        "moves the descriptions right (+) or left (-)" },
    [MS_EID_Y] = { "eid", "y", K_INT, 0, -40, 200, NULL, 0, NULL,
        "moves the descriptions down (+) or up (-)" },

    [MS_DANCE_QUALITY] = { "specialist", "quality", K_INT, 4, 0, 4, NULL, 0, NULL,
        "the lowest item quality that starts the dance (0-4)" },
    [MS_DANCE_IGNORE_QUALITY] = { "specialist", "ignorequality", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = quality does not count, only the items in 'always' start it" },
    [MS_DANCE_MUSIC] = { "specialist", "music", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = the Specialist theme plays while dancing" },
    [MS_DANCE_BLIND] = { "specialist", "blind", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = dance under Curse of the Blind as well" },
    [MS_DANCE_ALWAYS] = { "specialist", "always", K_LIST, 0, 0, 0, NULL, 0, "4,12,118,182",
        "item ids that always start the dance, whatever their quality" },
    [MS_DANCE_NEVER] = { "specialist", "never", K_LIST, 0, 0, 0, NULL, 0, "",
        "item ids that never start it" },
    /*
     * The options of the mod's 1.5.1 (04.10.2026; their ids stand at the end of
     * modset.h, the file writes them here). DEFAULTS AS THE AUTHOR MEANT THEM
     * (the owner, 04.10.2026): EpicSettings holds OverrideSpecialMusic = 0 and
     * ReduceFlashing = 0, meaning off. In Lua 0 is true, so the PC mod, untouched,
     * actually plays the default track for everyone and Tainted Eden dances
     * Eden's dance (DoCostume tests both with a bare "if"; MCM shows True) - the
     * owner chose the full mod: the tainted remixes and the flickering Eden.
     */
    [MS_DANCE_DEFAULT_MUSIC] = { "specialist", "defaultmusic", K_CHOICE, MS_TRACK_CLASSIC, 0, 0,
        WORDS(DANCE_TRACKS), NULL,
        "the track of the regular characters (and of everyone with alwaysdefault=1): "
        "classic, tainted, sad (Tainted Cain's), glitched (Tainted Eden's)" },
    [MS_DANCE_ALWAYS_DEFAULT] = { "specialist", "alwaysdefault", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = every character dances to the default track; 0 = the tainted have their own remixes" },
    [MS_DANCE_REDUCE_FLASHING] = { "specialist", "reduceflashing", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = Tainted Eden dances regular Eden's dance instead of the flickering one" },

    /*
     * [rangefix] - "Range fix for Bones & Sword" and "Range fix for Bag of
     * Crafting", one mod here (rangefix.c). The settings and defaults are the
     * Bones & Sword mod's (its Mod Config Menu; a scroll setting there is 0..10).
     */
    [MS_RANGE_ENABLED] = { "rangefix", "enabled", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = the melee weapons' swing stays close at high range (bone club, scythe, jawbone, "
        "notched axe, spirit sword, tech sword)" },
    [MS_RANGE_FACTOR] = { "rangefix", "rangefactor", K_INT, 5, 0, 10, NULL, 0, NULL,
        "how much range makes the swing bigger instead, 0-10 (0 = not at all)" },
    [MS_RANGE_BOC_ENABLED] = { "rangefix", "enabledboc", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = the same for the Bag of Crafting's swing" },
    [MS_RANGE_BOC_FACTOR] = { "rangefix", "rangefactorboc", K_INT, 0, 0, 10, NULL, 0, NULL,
        "how much range makes the bag's swing bigger, 0-10" },

    [MS_BARS_STYLE] = { "bossbars", "style", K_CHOICE, 0, 0, 0, NULL, 0, NULL,
        "the design of the bars: Default, Abyss, Copper, Dots, Enter the Gungeon, Flash, Flip, "
        "Hearts, Lil' Portal, Magic Mushroom, Minimal, Minecraft, Playdough Cookie, Planetarium, "
        "Revelation, Spin To Win, Terraria, TM Trainer, Void, ..." },
    [MS_BARS_POSITION] = { "bossbars", "position", K_CHOICE, 0, 0, 0, WORDS(POSITIONS), NULL,
        "where the bars are: bottom, top, left, right" },
    [MS_BARS_SORTING] = { "bossbars", "sorting", K_CHOICE, 0, 0, 0, WORDS(SORTINGS), NULL,
        "segments = a bar for every part; bosses = a segmented boss in one bar; "
        "vanilla = one bar for all" },
    [MS_BARS_ICONS] = { "bossbars", "icons", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = an icon at the start of every bar" },
    [MS_BARS_BOSS_ICONS] = { "bossbars", "bossicons", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = the boss's own icon; 0 = the same skull for all" },
    [MS_BARS_STATUS] = { "bossbars", "status", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = burn, poison, charm... icons over the bar" },
    [MS_BARS_BOSS_DESIGNS] = { "bossbars", "bossdesigns", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = the Beast, Dogma, Mother and others keep designs of their own" },
    [MS_BARS_NOTCHES] = { "bossbars", "notches", K_BOOL, 0, 0, 1, NULL, 0, NULL,
        "1 = a notch every ten per cent" },
    [MS_BARS_PER_ROW] = { "bossbars", "perrow", K_INT, 7, 1, 20, NULL, 0, NULL,
        "bars in one row before a new row starts" },
    [MS_BARS_PADDING] = { "bossbars", "padding", K_FLOAT, 15, 0, 100, NULL, 0, NULL,
        "the gap between two bars" },
    [MS_BARS_SCREEN_PADDING] = { "bossbars", "screenpadding", K_FLOAT, 17, 0, 135, NULL, 0, NULL,
        "the gap between the bars and the edge of the screen" },
    [MS_BARS_FLASHING] = { "bossbars", "flashing", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = a bar flashes when its boss is hurt or heals" },
    [MS_BARS_INVINCIBLE] = { "bossbars", "invincible", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = a bar turns grey while its boss cannot be hurt" },
    [MS_BARS_CHAMPION_COLOURS] = { "bossbars", "championcolors", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = a champion boss's icon in the champion's colours" },
    [MS_BARS_SPIDER_MOD] = { "bossbars", "spidermod", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "0 = no bars while a player carries Spider Mod (it shows health bars itself)" },
    [MS_BARS_BEAST] = { "bossbars", "beast", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = bars in the fight with the Beast and its horsemen" },
    [MS_BARS_MOTHER] = { "bossbars", "mother", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = bars in the fight with Mother" },
    [MS_BARS_MEGA_SATAN] = { "bossbars", "megasatan", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = bars in the fight with Mega Satan" },
    [MS_BARS_TEXT] = { "bossbars", "text", K_CHOICE, 0, 0, 0, WORDS(TEXTS), NULL,
        "text on a bar: none, percent, hp (health left)" },
    [MS_BARS_TEXT_ALPHA] = { "bossbars", "textalpha", K_FLOAT, 1, 0, 1, NULL, 0, NULL,
        "the text's opacity, 0-1" },
    [MS_BARS_TEXT_SIZE] = { "bossbars", "textsize", K_FLOAT, 0.5f, 0.1f, 2, NULL, 0, NULL,
        "the text's size, 0.5 is the mod's" },

    [MS_WAVES_PRESET] = { "wavecounter", "preset", K_CHOICE, 0, 0, 0, WORDS(PRESETS), NULL,
        "where the counter stands: default (by the minimap), foundhud (under the stats), "
        "bossbar (over the boss bar)" },
    [MS_WAVES_X] = { "wavecounter", "x", K_INT, 0, -500, 500, NULL, 0, NULL,
        "moves the counter right (+) or left (-)" },
    [MS_WAVES_Y] = { "wavecounter", "y", K_INT, 0, -300, 300, NULL, 0, NULL,
        "moves the counter up (+) or down (-)" },
    [MS_WAVES_BAR_SCALE] = { "wavecounter", "barscale", K_INT, 100, 50, 200, NULL, 0, NULL,
        "the bar picture's size in per cent" },
    [MS_WAVES_TEXT_SCALE] = { "wavecounter", "textscale", K_INT, 100, 50, 200, NULL, 0, NULL,
        "the text's size in per cent" },
    [MS_WAVES_TEXT_NUDGE] = { "wavecounter", "textnudge", K_INT, 0, -20, 20, NULL, 0, NULL,
        "moves only the text down (+) or up (-)" },
    [MS_WAVES_COLOUR] = { "wavecounter", "color", K_RGB, 0, 0, 0, NULL, 0, "255,255,255",
        "the text's colour, red,green,blue 0-255" },

    [MS_STATS_OPACITY] = { "statsplus", "opacity", K_FLOAT, 0.4f, 0.1f, 0.6f, NULL, 0, NULL,
        "how strongly the numbers beside the stats show, 0.1-0.6" },
    [MS_STATS_BRACKETS] = { "statsplus", "brackets", K_CHOICE, 0, 0, 0, WORDS(BRACKETS), NULL,
        "around the numbers: none, square, round, curly, angle" },
    [MS_STATS_SPACING] = { "statsplus", "spacing", K_INT, 5, 0, 10, NULL, 0, NULL,
        "the gap after the stat's own number, 0-10" },
    [MS_STATS_CHANGES] = { "statsplus", "changes", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = a change of a number slides out beside it for a few seconds" },
    [MS_STATS_TRACK_D8] = { "statsplus", "trackd8", K_BOOL, 1, 0, 1, NULL, 0, NULL,
        "1 = the damage multiplier counts D8's roll" },
    [MS_STATS_D8_COLOUR] = { "statsplus", "d8color", K_CHOICE, MS_COLOUR_BLUE, 0, 0, WORDS(COLOURS), NULL,
        "the D8 multiplier's colour: none, grey, red, green, blue, orange, magenta, cyan" },
    [MS_STATS_DAMAGE_COLOUR] = { "statsplus", "damagecolor", K_CHOICE, MS_COLOUR_RED, 0, 0, WORDS(COLOURS), NULL,
        "the damage multiplier's colour" },
    [MS_STATS_TEARCAP_COLOUR] = { "statsplus", "tearcapcolor", K_CHOICE, MS_COLOUR_GREEN, 0, 0, WORDS(COLOURS),
        NULL, "the tear cap's colour" },
    [MS_STATS_SPEED_WHEN] = { "statsplus", "speedwhen", K_CHOICE, 1, 0, 0, WORDS(WHENS), NULL,
        "when speed shows its first choice: always, map (while the map button is held), drop (switched by a press of the drop button)" },
    [MS_STATS_SPEED_THEN] = { "statsplus", "speedthen", K_CHOICE, 1, 0, 0, WORDS(SHOWS), NULL,
        "what speed shows then: none, d8, damagemult (damage only), tearcap (tears only)" },
    [MS_STATS_SPEED_ELSE] = { "statsplus", "speedelse", K_CHOICE, 0, 0, 0, WORDS(SHOWS), NULL,
        "what speed shows otherwise" },
    [MS_STATS_TEARS_WHEN] = { "statsplus", "tearswhen", K_CHOICE, 1, 0, 0, WORDS(WHENS), NULL,
        "when tears shows its first choice: always, map (while the map button is held), drop (switched by a press of the drop button)" },
    [MS_STATS_TEARS_THEN] = { "statsplus", "tearsthen", K_CHOICE, 1, 0, 0, WORDS(SHOWS), NULL,
        "what tears shows then: none, d8, damagemult (damage only), tearcap (tears only)" },
    [MS_STATS_TEARS_ELSE] = { "statsplus", "tearselse", K_CHOICE, 3, 0, 0, WORDS(SHOWS), NULL,
        "what tears shows otherwise" },
    [MS_STATS_DAMAGE_WHEN] = { "statsplus", "damagewhen", K_CHOICE, 1, 0, 0, WORDS(WHENS), NULL,
        "when damage shows its first choice: always, map (while the map button is held), drop (switched by a press of the drop button)" },
    [MS_STATS_DAMAGE_THEN] = { "statsplus", "damagethen", K_CHOICE, 1, 0, 0, WORDS(SHOWS), NULL,
        "what damage shows then: none, d8, damagemult (damage only), tearcap (tears only)" },
    [MS_STATS_DAMAGE_ELSE] = { "statsplus", "damageelse", K_CHOICE, 2, 0, 0, WORDS(SHOWS), NULL,
        "what damage shows otherwise" },
    [MS_STATS_RANGE_WHEN] = { "statsplus", "rangewhen", K_CHOICE, 1, 0, 0, WORDS(WHENS), NULL,
        "when range shows its first choice: always, map (while the map button is held), drop (switched by a press of the drop button)" },
    [MS_STATS_RANGE_THEN] = { "statsplus", "rangethen", K_CHOICE, 1, 0, 0, WORDS(SHOWS), NULL,
        "what range shows then: none, d8, damagemult (damage only), tearcap (tears only)" },
    [MS_STATS_RANGE_ELSE] = { "statsplus", "rangeelse", K_CHOICE, 0, 0, 0, WORDS(SHOWS), NULL,
        "what range shows otherwise" },
    [MS_STATS_SHOTSPEED_WHEN] = { "statsplus", "shotspeedwhen", K_CHOICE, 0, 0, 0, WORDS(WHENS), NULL,
        "when shotspeed shows its first choice: always, map (while the map button is held), drop (switched by a press of the drop button)" },
    [MS_STATS_SHOTSPEED_THEN] = { "statsplus", "shotspeedthen", K_CHOICE, 0, 0, 0, WORDS(SHOWS), NULL,
        "what shotspeed shows then: none, d8, damagemult (damage only), tearcap (tears only)" },
    [MS_STATS_SHOTSPEED_ELSE] = { "statsplus", "shotspeedelse", K_CHOICE, 0, 0, 0, WORDS(SHOWS), NULL,
        "what shotspeed shows otherwise" },
    [MS_STATS_LUCK_WHEN] = { "statsplus", "luckwhen", K_CHOICE, 0, 0, 0, WORDS(WHENS), NULL,
        "when luck shows its first choice: always, map (while the map button is held), drop (switched by a press of the drop button)" },
    [MS_STATS_LUCK_THEN] = { "statsplus", "luckthen", K_CHOICE, 0, 0, 0, WORDS(SHOWS), NULL,
        "what luck shows then: none, d8, damagemult (damage only), tearcap (tears only)" },
    [MS_STATS_LUCK_ELSE] = { "statsplus", "luckelse", K_CHOICE, 0, 0, 0, WORDS(SHOWS), NULL,
        "what luck shows otherwise" },
};

#define LIST_MAX 64

static float g_value[MS_COUNT];
static int g_list[MS_COUNT][LIST_MAX];
static int g_list_count[MS_COUNT];
static int g_ready;

static void parse_list(int id, const char *text) {
    g_list_count[id] = 0;
    while (text && *text && g_list_count[id] < LIST_MAX) {
        char *end;
        long v = strtol(text, &end, 10);
        if (end == text) { text++; continue; }
        g_list[id][g_list_count[id]++] = (int) v;
        text = end;
    }
}

static void defaults(void) {
    if (g_ready) return;
    g_ready = 1;
    for (int i = 0; i < MS_COUNT; i++) {
        g_value[i] = SETTINGS[i].def;
        if (SETTINGS[i].kind == K_LIST || SETTINGS[i].kind == K_RGB) parse_list(i, SETTINGS[i].list_def);
    }
}

static int choice_of(int id, const char *word) {
    if (id == MS_BARS_STYLE) {
        for (int k = 0; k < shim_bossbars_style_count(); k++)
            if (!strcasecmp(word, shim_bossbars_style_name(k))) return k;
        return -1;
    }
    for (int k = 0; k < SETTINGS[id].word_count; k++)
        if (!strcasecmp(word, SETTINGS[id].words[k])) return k;
    return -1;
}

/* a line of a section; answers 1 when the section and key are ours */
int shim_modset_parse(const char *section, const char *key, const char *value) {
    defaults();
    for (int i = 0; i < MS_COUNT; i++) {
        const setting_t *s = &SETTINGS[i];
        if (strcasecmp(section, s->section) || strcasecmp(key, s->key)) continue;
        char word[128];
        size_t n = 0;
        while (*value == ' ' || *value == '\t') value++;
        while (value[n] && value[n] != '\r' && value[n] != '\n' && n < sizeof(word) - 1) {
            word[n] = value[n];
            n++;
        }
        while (n && (word[n - 1] == ' ' || word[n - 1] == '\t')) n--;
        word[n] = 0;
        switch (s->kind) {
        case K_LIST: case K_RGB:
            parse_list(i, word);
            break;
        case K_CHOICE: {
            int k = choice_of(i, word);
            if (k >= 0) g_value[i] = (float) k;
            break;
        }
        default: {
            float v = (float) atof(word);
            if (s->kind != K_FLOAT) v = (float) (int) v;
            if (v < s->lo) v = s->lo;
            if (v > s->hi) v = s->hi;
            g_value[i] = v;
        }
        }
        return 1;
    }
    return 0;
}

/* is this a section the table owns */
int shim_modset_section(const char *section) {
    for (int i = 0; i < MS_COUNT; i++)
        if (!strcasecmp(section, SETTINGS[i].section)) return 1;
    return 0;
}

/* one row of a section, with its help */
static void write_row(FILE *f, int i) {
    const setting_t *s = &SETTINGS[i];
    fprintf(f, "# %s\n", s->help);
    switch (s->kind) {
    case K_LIST: case K_RGB:
        fprintf(f, "%s=", s->key);
        for (int k = 0; k < g_list_count[i]; k++)
            fprintf(f, "%s%d", k ? "," : "", g_list[i][k]);
        fputc('\n', f);
        break;
    case K_CHOICE:
        fprintf(f, "%s=%s\n", s->key, i == MS_BARS_STYLE ? shim_bossbars_style_name((int) g_value[i])
                                                       : s->words[(int) g_value[i]]);
        break;
    case K_FLOAT:
        fprintf(f, "%s=%g\n", s->key, (double) g_value[i]);
        break;
    default:
        fprintf(f, "%s=%d\n", s->key, (int) g_value[i]);
    }
}

/*
 * Every section once, with its help, as it stands now. A section is written
 * where its first row stands, with all its rows - rows added at the end of
 * modset.h (their ids must not move) still land in their own section.
 */
void shim_modset_write(FILE *f) {
    defaults();
    for (int i = 0; i < MS_COUNT; i++) {
        int seen = 0;
        for (int k = 0; k < i && !seen; k++) seen = !strcmp(SETTINGS[k].section, SETTINGS[i].section);
        if (seen) continue;
        fprintf(f, "\n[%s]\n", SETTINGS[i].section);
        for (int j = i; j < MS_COUNT; j++)
            if (!strcmp(SETTINGS[j].section, SETTINGS[i].section)) write_row(f, j);
    }
}

int shim_modset_int(int id) {
    defaults();
    return (id >= 0 && id < MS_COUNT) ? (int) g_value[id] : 0;
}

float shim_modset_float(int id) {
    defaults();
    return (id >= 0 && id < MS_COUNT) ? g_value[id] : 0.0f;
}

int shim_modset_listed(int id, int value) {
    defaults();
    if (id < 0 || id >= MS_COUNT) return 0;
    for (int k = 0; k < g_list_count[id]; k++)
        if (g_list[id][k] == value) return 1;
    return 0;
}

void shim_modset_rgb(int id, float *r, float *g, float *b) {
    defaults();
    int c[3] = { 255, 255, 255 };
    for (int k = 0; k < 3 && id >= 0 && id < MS_COUNT && k < g_list_count[id]; k++) {
        int v = g_list[id][k];
        c[k] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
    *r = (float) c[0] / 255.0f;
    *g = (float) c[1] / 255.0f;
    *b = (float) c[2] / 255.0f;
}

/* ------------------------------------------------ the settings pages (tweaks.c) */
/*
 * A row of a mod's settings page steps its setting and shows it. How far one
 * step goes and how many decimals a number shows are the page's business, not
 * the file's: the file takes any value in range. One tap moves by what the
 * mods' own Mod Config Menu moves by where it says (text transparency by a
 * tenth), and elsewhere by what makes the range walkable - offsets, scales and
 * colours by five.
 */
static float step_of(int id, int *decimals) {
    *decimals = 0;
    switch (id) {
    case MS_WAVES_X: case MS_WAVES_Y: case MS_WAVES_BAR_SCALE: case MS_WAVES_TEXT_SCALE:
    case MS_WAVES_COLOUR:
        return 5.0f;
    case MS_BARS_TEXT_ALPHA: case MS_BARS_TEXT_SIZE:
        *decimals = 1;
        return 0.1f;
    case MS_STATS_OPACITY:
        *decimals = 2;
        return 0.05f;
    case MS_EID_X: case MS_EID_Y:
        return 2.0f;
    case MS_EID_OPACITY:                    /* the mod's own step */
        *decimals = 1;
        return 0.1f;
    }
    return 1.0f;
}

static int choice_count(int id) {
    return id == MS_BARS_STYLE ? shim_bossbars_style_count() : SETTINGS[id].word_count;
}

/* one step forwards or back; the file is written by the caller */
void shim_modset_step(int id, int comp, int dir) {
    defaults();
    if (id < 0 || id >= MS_COUNT || !dir) return;
    const setting_t *s = &SETTINGS[id];
    int dec;
    float step = step_of(id, &dec);
    switch (s->kind) {
    case K_BOOL:
        g_value[id] = g_value[id] != 0.0f ? 0.0f : 1.0f;
        break;
    case K_CHOICE: {
        int n = choice_count(id);
        if (n > 0) g_value[id] = (float) ((((int) g_value[id] + dir) % n + n) % n);
        break;
    }
    case K_RGB: {
        while (g_list_count[id] < 3) g_list[id][g_list_count[id]++] = 255;
        int v = g_list[id][comp] + dir * (int) step;
        g_list[id][comp] = v < 0 ? 0 : (v > 255 ? 255 : v);
        break;
    }
    case K_INT: case K_FLOAT: {
        float v = g_value[id] + (float) dir * step;
        v = s->lo + roundf((v - s->lo) / step) * step;      /* on the step's grid */
        if (v < s->lo) v = s->lo;
        if (v > s->hi) v = s->hi;
        g_value[id] = v;
        break;
    }
    default:
        break;
    }
}

/* a number as the page shows it: which 0 is the value, 1 the lowest, 2 the highest */
void shim_modset_format(int id, int comp, int which, char *out, unsigned long n) {
    defaults();
    out[0] = 0;
    if (id < 0 || id >= MS_COUNT) return;
    const setting_t *s = &SETTINGS[id];
    int dec;
    step_of(id, &dec);
    float v;
    if (s->kind == K_RGB) {
        int c = comp < g_list_count[id] ? g_list[id][comp] : 255;
        v = which == 1 ? 0.0f : (which == 2 ? 255.0f : (float) c);
    } else {
        v = which == 1 ? s->lo : (which == 2 ? s->hi : g_value[id]);
    }
    snprintf(out, n, "%.*f", dec, (double) v);
}
