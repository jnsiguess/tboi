/*
 * json.h - a small strict JSON reader (RFC 8259) and the mod.json reader on
 * top of it (plan 30.09, 4.10 part 1; settings schema of 02.10.2026).
 *
 * The parser takes the whole text, checks it fully, then fills one block
 * (nodes followed by the decoded strings) - a single malloc per document.
 * Nodes lie in document order: a container's children follow it directly,
 * and `size` (the node count of a subtree, itself included) skips one.
 */
#ifndef ISAAC_JSON_H
#define ISAAC_JSON_H

#include <stddef.h>
#include "diag.h"

/* ------------------------------------------------------------ JSON */

#define JSON_MAX_TEXT     (1024 * 1024)  /* bytes of text */
#define JSON_MAX_DEPTH    32             /* arrays and objects inside each other */
#define JSON_MAX_MEMBERS  1024           /* members of one object (duplicates are looked for) */
#define JSON_MAX_NUMBER   64             /* characters of one number */

typedef enum {
    JSON_NULL, JSON_FALSE, JSON_TRUE, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} json_type_t;

typedef struct json_node {
    unsigned char type;        /* json_type_t */
    unsigned char whole;       /* a number whose value is exactly the integer in ival */
    unsigned at;               /* byte offset of the value in the text */
    unsigned size;             /* nodes in this subtree, itself included */
    unsigned count;            /* an array's elements or an object's members */
    unsigned key_len, len;     /* bytes of key and of str, without the closing NUL */
    const char *key;           /* a member of an object: its name (UTF-8, NUL-ended); NULL otherwise */
    const char *str;           /* a string: its text (UTF-8, NUL-ended; may hold \0 inside - len counts) */
    double num;                /* a number */
    long long ival;            /* a number with whole set */
} json_node_t;

typedef struct {
    json_node_t *nodes;        /* nodes[0] is the root; NULL when nothing was parsed */
    unsigned count;
} json_doc_t;

typedef struct {
    size_t at;                 /* byte offset of the fault in the text */
    int line, col;             /* 1-based; the column counts characters, not bytes */
    char what[128];            /* English: "expected ',' or '}', found ']'" */
} json_error_t;

/* 1 parsed (free with json_free), 0 refused (err says where and why; nothing to free) */
int json_parse(json_doc_t *doc, const char *text, size_t len, json_error_t *err);
/* the same, for a text written by hand (mod.json, hotfix 1.71): what org.json and a hand would take - comments,
   trailing and doubled commas, single quotes, bare words, '=' and ';', a key twice (json_get answers the last),
   text after the value, bytes that are not UTF-8 (U+FFFD) - read as the strict JSON it means */
int json_parse_lenient(json_doc_t *doc, const char *text, size_t len, json_error_t *err);
/* that reading's text as strict JSON (malloc'd, NUL-ended; the installer hands it to org.json), NULL when even it fails */
char *json_lenient_text(const char *text, size_t len, size_t *outlen, size_t *bad_at);
void json_free(json_doc_t *doc);

const json_node_t *json_root(const json_doc_t *doc);
/* children of an array or object: for (v = json_first(c); v; v = json_next(c, v)) */
const json_node_t *json_first(const json_node_t *container);
const json_node_t *json_next(const json_node_t *container, const json_node_t *child);
/* an object's member by name (NULL: absent or not an object); the i-th child of a container */
const json_node_t *json_get(const json_node_t *object, const char *key);
const json_node_t *json_at(const json_node_t *container, unsigned i);

/* ------------------------------------------------------------ mod.json */

#define MODJ_FILE_MAX      (64 * 1024)   /* bytes of mod.json */
#define MODJ_ID_MAX        63            /* bytes of "id" (the mods' ID_MAX is 64 with the NUL) */
#define MODJ_SETTINGS_MAX  16
#define MODJ_KEY_MAX       32            /* a setting's key and a choice's value: [a-z0-9_-] */
#define MODJ_LABEL_MAX     24            /* characters of a label (a setting's and a choice value's), in each language */
#define MODJ_VALUES_MIN    2
#define MODJ_VALUES_MAX    16
#define MODJ_STEPS_MAX     100           /* (max - min) / step */
#define MODJ_NUM_LIMIT     1000000       /* |min|, |max| */
#define MODJ_LANGS_MAX     16            /* entries of one {language: text} map */
#define MODJ_ISSUES_MAX    24
#define MODJ_ISSUE_TEXT    176

/* the port's language sets, in the order the menus know them */
#define MODJ_PORT_LANGS    { "en", "ru", "de", "es", "jp", "kr", "zh" }

typedef enum { MODJ_SWITCH, MODJ_CHOICE, MODJ_NUMBER } modj_kind_t;

/*
 * What went wrong. modj_reason_code() names each in a short stable word the
 * UI maps to its own note text; the English line goes to check.txt and the report.
 */
typedef enum {
    MODJ_R_NONE,
    /* the whole mod.json (level BROKEN) */
    MODJ_R_FILE_OPEN,          /* "file_open"          could not be opened or read */
    MODJ_R_FILE_BIG,           /* "file_big"           larger than MODJ_FILE_MAX */
    MODJ_R_SYNTAX,             /* "syntax"             not JSON: line, column, byte and why */
    MODJ_R_ROOT,               /* "root_not_object"    the top is not {...} */
    /* the mod's own fields (level NOTE: the field is ignored) */
    MODJ_R_FIELD_TYPE,         /* "field_type"         "id" not a string, or a NUL inside it, or too long */
    /* all settings (level REFUSED, setting -1) */
    MODJ_R_SETTINGS_TYPE,      /* "settings_not_array" */
    MODJ_R_SETTINGS_MANY,      /* "settings_many"      entries past the 16th */
    /* one setting (level REFUSED) */
    MODJ_R_SETTING_TYPE,       /* "setting_not_object" */
    MODJ_R_KEY_MISSING,        /* "key_missing" */
    MODJ_R_KEY_BAD,            /* "key_bad"            not a string, empty, longer than 32, or outside [a-z0-9_-] */
    MODJ_R_KEY_DUP,            /* "key_dup" */
    MODJ_R_KIND_MISSING,       /* "kind_missing" */
    MODJ_R_KIND_BAD,           /* "kind_bad"           not switch / choice / number */
    MODJ_R_LABEL_MISSING,      /* "label_missing"      a setting's or a choice value's */
    MODJ_R_TEXT_TYPE,          /* "text_type"          neither text nor {language: text}, or a language's text not a string */
    MODJ_R_TEXT_EMPTY,         /* "text_empty"         "" or a map with no text in it */
    MODJ_R_TEXT_NUL,           /* "text_nul"           \u0000 inside */
    MODJ_R_TEXT_LANGS,         /* "text_langs"         more than 16 languages */
    MODJ_R_TEXT_LANG_CODE,     /* "text_lang_code"     a language code that is empty, long or not letters */
    MODJ_R_LABEL_LONG,         /* "label_long"         more than 24 characters */
    MODJ_R_DEFAULT_MISSING,    /* "default_missing" */
    MODJ_R_DEFAULT_TYPE,       /* "default_type"       switch: not true/false; choice: not a string; number: not a number */
    MODJ_R_DEFAULT_NOT_VALUE,  /* "default_not_value"  choice: not one of the values */
    MODJ_R_DEFAULT_RANGE,      /* "default_range"      number: outside min..max */
    MODJ_R_DEFAULT_GRID,       /* "default_grid"       number: not min + k*step */
    MODJ_R_VALUES_MISSING,     /* "values_missing" */
    MODJ_R_VALUES_TYPE,        /* "values_type"        not an array */
    MODJ_R_VALUES_COUNT,       /* "values_count"       not 2..16 */
    MODJ_R_VALUE_TYPE,         /* "value_type"         an entry not an object */
    MODJ_R_VALUE_BAD,          /* "value_bad"          "value" missing, not a string, or not a folder name [a-z0-9_-]{1,32} */
    MODJ_R_VALUE_DUP,          /* "value_dup" */
    MODJ_R_NUM_MISSING,        /* "num_missing"        min, max or step */
    MODJ_R_NUM_WHOLE,          /* "num_whole"          min/max/step/default not a whole number within +-1000000 */
    MODJ_R_NUM_ORDER,          /* "num_order"          min >= max */
    MODJ_R_NUM_STEP,           /* "num_step"           step <= 0 */
    MODJ_R_NUM_STEPS,          /* "num_steps"          more than 100 steps */
    MODJ_R_NUM_GRID,           /* "num_grid"           max not reached from min in whole steps */
    MODJ_R_RESTART_TYPE,       /* "restart_type"       not true/false */
    /* notes (level NOTE: something ignored, the setting stays) */
    MODJ_R_UNKNOWN_FIELD,      /* "unknown_field"      a field the schema does not have, or not for this kind */
    MODJ_R_LANG_UNKNOWN,       /* "lang_unknown"       a language that is none of the port's sets */
    MODJ_R_COUNT
} modj_reason_t;

typedef enum {
    MODJ_BROKEN,               /* mod.json was not read at all: the mod still runs from its files, named by its folder */
    MODJ_REFUSED,              /* one setting (or all of them, setting -1) is off */
    MODJ_NOTE                  /* something ignored; nothing is off */
} modj_level_t;

typedef struct {
    modj_level_t level;
    modj_reason_t reason;
    int setting;               /* its place in "settings", 0-based; -1 for the file and the mod's own fields */
    char key[MODJ_KEY_MAX + 1];/* the setting's key when it had a well-formed one, else "" */
    char text[MODJ_ISSUE_TEXT];/* English, one line: `setting 2 "style": default "x" is not one of the values` */
} modj_issue_t;

/* a text that is either one string or a {language: text} map; read it with modj_text() */
typedef struct { const json_node_t *node; } modj_text_t;

typedef struct {
    const char *value;         /* the folder name under options/<key>/ */
    modj_text_t label;
} modj_value_t;

typedef struct {
    int index;                 /* its place in the document's "settings" array, 0-based */
    const char *key;           /* [a-z0-9_-]{1,32}, unique in the mod */
    modj_kind_t kind;
    modj_text_t label;
    int restart;               /* as the author declared it; 1 when absent (the port decides whether false holds) */
    int def;                   /* switch: 0/1; choice: an index into values; number: the value */
    int min, max, step;        /* number only */
    int nvalues;               /* choice only */
    modj_value_t values[MODJ_VALUES_MAX];
} modj_setting_t;

typedef struct {
    int ok;                    /* the document was read (the settings below may still have refusals) */
    int lenient;               /* it was not strict JSON and was read leniently (a note says where) */
    int open_errno;            /* modj_read_file: errno when the file could not be opened (ENOENT: no mod.json) */
    json_error_t error;        /* MODJ_R_SYNTAX: where and why */
    const char *id;            /* NULL when absent */
    modj_text_t name, version, author, description;
    int nsettings;
    modj_setting_t settings[MODJ_SETTINGS_MAX];
    int nissues, issues_lost;  /* issues past MODJ_ISSUES_MAX are only counted */
    modj_issue_t issues[MODJ_ISSUES_MAX];
    json_doc_t doc;            /* owns every string above */
} modj_t;

/* 1: read (m->ok); 0: BROKEN, the reason is m->issues[0]. Either way modj_free(m) afterwards. */
int modj_read_file(modj_t *m, const char *path);
int modj_read_text(modj_t *m, const char *text, size_t len);
void modj_free(modj_t *m);

/*
 * A text in the wanted language: that language, else "en", else the first one
 * present (empty strings count as absent). NULL when there is none. A plain
 * string answers every language. The port's font has no CJK: for jp/kr/zh the
 * caller asks for "en" (the schema, "Languages").
 */
const char *modj_text(modj_text_t t, const char *want);
/* copy into out, cut at a character boundary; returns the bytes copied */
size_t modj_copy(char *out, size_t room, const char *text);

const modj_setting_t *modj_find(const modj_t *m, const char *key);
/* the folder name of a value under options/<key>/: "on"/"off", the choice's value, the number. 0 when out of range */
int modj_value_name(const modj_setting_t *s, int value, char *out, size_t room);
/* the reverse, for mod-settings.ini: 1 and *value when the text is a valid value of this setting now */
int modj_parse_value(const modj_setting_t *s, const char *text, int *value);

const char *modj_reason_code(modj_reason_t r);
const char *modj_level_name(modj_level_t l);   /* "broken", "refused", "note" */

#if ISAAC_DIAG
/* `json: selftest` lines in the log; returns the number of failed checks */
int shim_json_selftest(void);
#endif

#endif
