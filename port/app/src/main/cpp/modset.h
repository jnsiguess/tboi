/*
 * The settings of the ported mods, as they stand in the sections of
 * port-touch.ini (modset.c). Whether a mod is on at all stays a Tweaks switch
 * (moddance, modbars...); what a mod does when it is on is here.
 */
#ifndef MODSET_H
#define MODSET_H

enum {
    /* [specialist] */
    MS_DANCE_QUALITY,
    MS_DANCE_IGNORE_QUALITY,
    MS_DANCE_MUSIC,
    MS_DANCE_BLIND,
    MS_DANCE_ALWAYS,
    MS_DANCE_NEVER,
    /* [bossbars] */
    MS_BARS_STYLE,
    MS_BARS_POSITION,
    MS_BARS_SORTING,
    MS_BARS_ICONS,
    MS_BARS_BOSS_ICONS,
    MS_BARS_STATUS,
    MS_BARS_BOSS_DESIGNS,
    MS_BARS_NOTCHES,
    MS_BARS_PER_ROW,
    MS_BARS_PADDING,
    MS_BARS_SCREEN_PADDING,
    MS_BARS_FLASHING,
    MS_BARS_INVINCIBLE,
    MS_BARS_CHAMPION_COLOURS,
    MS_BARS_SPIDER_MOD,
    MS_BARS_BEAST,
    MS_BARS_MOTHER,
    MS_BARS_MEGA_SATAN,
    MS_BARS_TEXT,
    MS_BARS_TEXT_ALPHA,
    MS_BARS_TEXT_SIZE,
    /* [wavecounter] */
    MS_WAVES_PRESET,
    MS_WAVES_X,
    MS_WAVES_Y,
    MS_WAVES_BAR_SCALE,
    MS_WAVES_TEXT_SCALE,
    MS_WAVES_TEXT_NUDGE,
    MS_WAVES_COLOUR,
    /* [statsplus] */
    MS_STATS_OPACITY,
    MS_STATS_BRACKETS,
    MS_STATS_SPACING,
    MS_STATS_CHANGES,
    MS_STATS_TRACK_D8,
    MS_STATS_D8_COLOUR,
    MS_STATS_DAMAGE_COLOUR,
    MS_STATS_TEARCAP_COLOUR,
    /* a stat's loadout: when, then, else - six stats, in the column's order */
    MS_STATS_SPEED_WHEN,
    MS_STATS_SPEED_THEN,
    MS_STATS_SPEED_ELSE,
    MS_STATS_TEARS_WHEN,
    MS_STATS_TEARS_THEN,
    MS_STATS_TEARS_ELSE,
    MS_STATS_DAMAGE_WHEN,
    MS_STATS_DAMAGE_THEN,
    MS_STATS_DAMAGE_ELSE,
    MS_STATS_RANGE_WHEN,
    MS_STATS_RANGE_THEN,
    MS_STATS_RANGE_ELSE,
    MS_STATS_SHOTSPEED_WHEN,
    MS_STATS_SHOTSPEED_THEN,
    MS_STATS_SHOTSPEED_ELSE,
    MS_STATS_LUCK_WHEN,
    MS_STATS_LUCK_THEN,
    MS_STATS_LUCK_ELSE,
    /* [eid] - EID's own settings page (the owner, 24.09.2026) */
    MS_EID_OPACITY,
    MS_EID_HIDE_BATTLE,
    MS_EID_SHOW_BLIND,
    MS_EID_QUALITY,
    MS_EID_TRANSFORM,
    MS_EID_STAT_ICONS,
    MS_EID_HEALTHUP,
    MS_EID_DISTANCE,
    MS_EID_SHOP_CARDS,
    MS_EID_SHOP_PILLS,
    MS_EID_SHOP_SOULS,
    MS_EID_UNREACHABLE,
    MS_EID_FLIGHT,
    MS_EID_UNIDENTIFIED,
    MS_EID_SPINDOWN,
    MS_EID_BAG,
    MS_EID_BAG_RESULTS,
    MS_EID_BAG_SORT,
    MS_EID_X,
    MS_EID_Y,
    /*
     * [specialist], the options of the mod's 1.5.1 (04.10.2026). Added at the
     * end, not in the [specialist] block above: the settings pages store these
     * ids as numbers (tweaks_layout.h SP_ROW), and a row in the middle would
     * move every id after it. The file still writes them in their section.
     */
    MS_DANCE_DEFAULT_MUSIC,
    MS_DANCE_ALWAYS_DEFAULT,
    MS_DANCE_REDUCE_FLASHING,
    /* [rangefix] - the Range Fix mods, merged (rangefix.c, 04.10.2026) */
    MS_RANGE_ENABLED,
    MS_RANGE_FACTOR,
    MS_RANGE_BOC_ENABLED,
    MS_RANGE_BOC_FACTOR,

    MS_COUNT
};
/* the default track, the mod's DefaultMusic: the order of mods.c T_* and the mod's music.xml */
enum { MS_TRACK_CLASSIC, MS_TRACK_TAINTED, MS_TRACK_SAD, MS_TRACK_GLITCHED };
enum { MS_SORT_QUALITY, MS_SORT_NAME };
enum { MS_BAG_ALWAYS, MS_BAG_HOLD, MS_BAG_NEVER };   /* DisplayBagOfCrafting, the mod's three */

/* choices, in the order of the words the file takes */
enum { MS_POSITION_BOTTOM, MS_POSITION_TOP, MS_POSITION_LEFT, MS_POSITION_RIGHT };
enum { MS_SORTING_SEGMENTS, MS_SORTING_BOSSES, MS_SORTING_VANILLA };
enum { MS_TEXT_NONE, MS_TEXT_PERCENT, MS_TEXT_HP };
enum { MS_WAVES_DEFAULT, MS_WAVES_FOUND_HUD, MS_WAVES_BOSS_BAR };
enum { MS_BRACKETS_NONE, MS_BRACKETS_SQUARE, MS_BRACKETS_ROUND, MS_BRACKETS_CURLY, MS_BRACKETS_ANGLE };
enum { MS_WHEN_ALWAYS, MS_WHEN_MAP, MS_WHEN_DROP };
enum { MS_SHOW_NONE, MS_SHOW_D8, MS_SHOW_DAMAGE, MS_SHOW_TEARCAP };
enum { MS_COLOUR_NONE, MS_COLOUR_GREY, MS_COLOUR_RED, MS_COLOUR_GREEN, MS_COLOUR_BLUE,
       MS_COLOUR_ORANGE, MS_COLOUR_MAGENTA, MS_COLOUR_CYAN };
#define MS_STATS_LOADOUT MS_STATS_SPEED_WHEN      /* + stat * 3 + 0 when, 1 then, 2 else */

int shim_modset_int(int id);                 /* a switch, a number, or a choice's index */
float shim_modset_float(int id);
int shim_modset_listed(int id, int value);   /* is value in a list setting */
void shim_modset_rgb(int id, float *r, float *g, float *b);

/*
 * EID's rows on its settings page (tools/modset_pages.py, EID_IDS). They are
 * not this table's: the language and the glitch switch keep their own
 * top-level keys, eidlang and eidglitch, and tweaks.c hands them to eid.c.
 */
#define MS_EID_LANG   900
#define MS_EID_GLITCH 901

#endif
