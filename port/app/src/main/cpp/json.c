/*
 * json.c - a small strict JSON reader (RFC 8259) and mod.json on top of it
 * (plan 30.09, 4.10 part 1; the settings schema, the research notes of 02.10.2026, mod settings schema).
 *
 * Until now mod.json was read by looking for "name" anywhere in the first
 * 8 KB: a nested "name" (a setting's) would have been taken for the mod's.
 * Here the text is parsed whole, strictly, and the reader walks the tree.
 *
 * The parser runs twice over the text: the first pass checks everything and
 * counts nodes and string bytes, the second fills one block of that size.
 * Strictness, all with the byte offset of the fault:
 *   - UTF-8 only, checked (no overlongs, no encoded surrogates, nothing past
 *     U+10FFFF); a leading BOM is skipped (Notepad writes one);
 *   - \uXXXX escapes are decoded to UTF-8, surrogate pairs joined, a lone
 *     surrogate refused;
 *   - no comments, no trailing commas, no single quotes, no leading zeros,
 *     no raw control characters inside strings, no duplicate keys;
 *   - limits: JSON_MAX_TEXT bytes, JSON_MAX_DEPTH nesting, JSON_MAX_MEMBERS
 *     per object, JSON_MAX_NUMBER characters per number, finite numbers.
 *
 * Nothing here logs except the self-test (ISAAC_DIAG): the reader hands its
 * findings to the caller as issues - an English line and a reason code.
 *
 * A MOD.JSON IS READ LENIENTLY (hotfix 1.71, the owner 05.10.2026: "leniently -
 * settings and the rest are an optional part, a mod does not need them").
 * Players wrote their mod.json by hand for 1.666, whose installer read it with
 * org.json and whose game looked for "name" in it; a slip there never cost a
 * mod. So a mod.json that is not strict JSON is read again by json_parse_lenient
 * - comments (slash-star, slash-slash, #), trailing and doubled commas, single
 * quotes, bare words for keys and texts, '=' / '=>' / ';' as org.json takes
 * them, a key twice (the last one stands), text after the closing brace, bytes
 * that are not UTF-8 - and the slip is only a note. When even that reading
 * fails, the mod keeps its name from a "name" found in the text (as 1.666 did).
 * Strictness stays where it protects something: a broken setting is switched
 * off, with its note; the mod itself always works.
 */
#include "json.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if ISAAC_DIAG
#include <android/log.h>
#endif

/* ------------------------------------------------------------ the parser */

typedef struct {
    const unsigned char *s;
    size_t n, i;
    int depth;
    int fill;                  /* the second pass: write nodes and strings */
    json_node_t *nodes;        /* the block being filled */
    char *heap;                /* the next string byte */
    size_t count, bytes;       /* nodes and string bytes so far */
    json_error_t *err;
    int dups_ok;               /* json_parse_lenient: a key twice is taken (json_get answers the last) */
} jp_t;

static int fail(jp_t *p, size_t at, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(p->err->what, sizeof(p->err->what), fmt, ap);
    va_end(ap);
    p->err->at = at;
    return 0;
}

/* "expected X" with what was found there, and the usual non-JSON habits named */
static int unexpected(jp_t *p, const char *what) {
    if (p->i >= p->n) return fail(p, p->i, "the text ends where %s was expected", what);
    unsigned c = p->s[p->i];
    if (c == '/') return fail(p, p->i, "comments are not JSON (expected %s)", what);
    if (c == '\'') return fail(p, p->i, "strings take double quotes, not single (expected %s)", what);
    if (c > 0x20 && c < 0x7f) return fail(p, p->i, "expected %s, found '%c'", what, (int) c);
    return fail(p, p->i, "expected %s, found byte 0x%02X", what, c);
}

static void skip_ws(jp_t *p) {
    while (p->i < p->n) {
        unsigned char c = p->s[p->i];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        p->i++;
    }
}

/* the length of a well-formed UTF-8 sequence at s, 0 when it is not one */
static int utf8_len(const unsigned char *s, size_t left) {
    unsigned c = s[0];
    if (c < 0x80) return 1;
    if (c < 0xC2) return 0;                                   /* a continuation byte, or an overlong of two */
    if (c < 0xE0) return left >= 2 && (s[1] & 0xC0) == 0x80 ? 2 : 0;
    if (c < 0xF0) {
        if (left < 3 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        if (c == 0xE0 && s[1] < 0xA0) return 0;               /* overlong */
        if (c == 0xED && s[1] >= 0xA0) return 0;              /* a surrogate, encoded */
        return 3;
    }
    if (c < 0xF5) {
        if (left < 4 || (s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        if (c == 0xF0 && s[1] < 0x90) return 0;               /* overlong */
        if (c == 0xF4 && s[1] >= 0x90) return 0;              /* past U+10FFFF */
        return 4;
    }
    return 0;
}

static int hex4(const unsigned char *s, size_t left, unsigned *out) {
    if (left < 4) return 0;
    unsigned v = 0;
    for (int k = 0; k < 4; k++) {
        unsigned c = s[k], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return 0;
        v = v << 4 | d;
    }
    *out = v;
    return 1;
}

static void emit(jp_t *p, unsigned char c) {
    if (p->fill) *p->heap++ = (char) c;
    p->bytes++;
}

static void emit_cp(jp_t *p, unsigned cp) {
    if (cp < 0x80) emit(p, (unsigned char) cp);
    else if (cp < 0x800) { emit(p, (unsigned char) (0xC0 | cp >> 6)); emit(p, (unsigned char) (0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        emit(p, (unsigned char) (0xE0 | cp >> 12));
        emit(p, (unsigned char) (0x80 | (cp >> 6 & 0x3F)));
        emit(p, (unsigned char) (0x80 | (cp & 0x3F)));
    } else {
        emit(p, (unsigned char) (0xF0 | cp >> 18));
        emit(p, (unsigned char) (0x80 | (cp >> 12 & 0x3F)));
        emit(p, (unsigned char) (0x80 | (cp >> 6 & 0x3F)));
        emit(p, (unsigned char) (0x80 | (cp & 0x3F)));
    }
}

/* a string at p->i (its opening quote): decoded into the heap on the second pass */
static int string(jp_t *p, const char **out, unsigned *len) {
    size_t start = p->i, from = p->bytes;
    char *at = p->heap;
    p->i++;
    for (;;) {
        if (p->i >= p->n) return fail(p, start, "a string that never closes");
        unsigned c = p->s[p->i];
        if (c == '"') { p->i++; break; }
        if (c < 0x20)
            return fail(p, p->i, "a raw control character U+%04X inside a string (write it as an escape)", c);
        if (c == '\\') {
            if (p->i + 1 >= p->n) return fail(p, start, "a string that never closes");
            unsigned e = p->s[p->i + 1];
            static const char from[] = "\"\\/bfnrt", to[] = "\"\\/\b\f\n\r\t";
            const char *plain = e ? strchr(from, (int) e) : NULL;
            if (plain) {
                emit(p, (unsigned char) to[plain - from]);
                p->i += 2;
                continue;
            }
            if (e != 'u') {
                if (e > 0x20 && e < 0x7f) return fail(p, p->i, "an unknown escape \\%c", (int) e);
                return fail(p, p->i, "an unknown escape (a backslash before byte 0x%02X)", e);
            }
            unsigned cp, lo;
            if (!hex4(p->s + p->i + 2, p->n - p->i - 2, &cp))
                return fail(p, p->i, "a \\u escape needs four hex digits");
            if (cp >= 0xDC00 && cp <= 0xDFFF)
                return fail(p, p->i, "a lone low surrogate \\u%04X", cp);
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                size_t j = p->i + 6;
                if (j + 1 >= p->n || p->s[j] != '\\' || p->s[j + 1] != 'u' || !hex4(p->s + j + 2, p->n - j - 2, &lo)
                    || lo < 0xDC00 || lo > 0xDFFF)
                    return fail(p, p->i, "a lone high surrogate \\u%04X (a low one must follow it)", cp);
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                p->i += 6;
            }
            emit_cp(p, cp);
            p->i += 6;
            continue;
        }
        int k = utf8_len(p->s + p->i, p->n - p->i);
        if (!k) return fail(p, p->i, "not UTF-8 (byte 0x%02X)", c);
        for (int b = 0; b < k; b++) emit(p, p->s[p->i + b]);
        p->i += (size_t) k;
    }
    emit(p, 0);
    if (out) *out = at;
    if (len) *len = (unsigned) (p->bytes - from - 1);
    return 1;
}

static int is_digit(jp_t *p) { return p->i < p->n && p->s[p->i] >= '0' && p->s[p->i] <= '9'; }

static int number(jp_t *p, json_node_t *node) {
    size_t at = p->i;
    int neg = 0, exact = 1;
    if (p->s[p->i] == '-') { neg = 1; p->i++; }
    if (!is_digit(p)) return unexpected(p, "a digit");
    if (p->s[p->i] == '0') {
        p->i++;
        if (is_digit(p)) return fail(p, at, "a number with a leading zero");
    } else while (is_digit(p)) p->i++;
    if (p->i < p->n && p->s[p->i] == '.') {
        p->i++;
        if (!is_digit(p)) return unexpected(p, "a digit after '.'");
        while (is_digit(p)) p->i++;
        exact = 0;
    }
    if (p->i < p->n && (p->s[p->i] == 'e' || p->s[p->i] == 'E')) {
        p->i++;
        if (p->i < p->n && (p->s[p->i] == '+' || p->s[p->i] == '-')) p->i++;
        if (!is_digit(p)) return unexpected(p, "a digit in the exponent");
        while (is_digit(p)) p->i++;
        exact = 0;
    }
    size_t len = p->i - at;
    if (len > JSON_MAX_NUMBER) return fail(p, at, "a number longer than %d characters", JSON_MAX_NUMBER);
    long long iv = 0;
    double d;
    if (exact) {
        /* digits only: exact while it fits in 64 bits */
        unsigned long long u = 0, cap = neg ? (unsigned long long) LLONG_MAX + 1 : (unsigned long long) LLONG_MAX;
        for (size_t k = at + (size_t) neg; k < p->i && exact; k++) {
            unsigned dg = p->s[k] - '0';
            if (u > (cap - dg) / 10) exact = 0;
            else u = u * 10 + dg;
        }
        if (exact) iv = neg ? (u == (unsigned long long) LLONG_MAX + 1 ? LLONG_MIN : -(long long) u) : (long long) u;
    }
    if (exact) d = (double) iv;
    else {
        char buf[JSON_MAX_NUMBER + 1];
        memcpy(buf, p->s + at, len);
        buf[len] = 0;
        d = strtod(buf, NULL);
        if (!isfinite(d)) return fail(p, at, "a number too large for a double");
        if (d == floor(d) && fabs(d) <= 9007199254740992.0) { exact = 1; iv = (long long) d; }
    }
    if (node) { node->num = d; node->whole = (unsigned char) exact; node->ival = exact ? iv : 0; }
    return 1;
}

static int value(jp_t *p, const char *key, unsigned klen);

static int container(jp_t *p, json_node_t *node, int object) {
    size_t open = p->i;
    char close = object ? '}' : ']';
    size_t self = p->count - 1;            /* this container's index */
    unsigned count = 0;
    p->i++;
    if (++p->depth > JSON_MAX_DEPTH) return fail(p, open, "nesting deeper than %d", JSON_MAX_DEPTH);
    skip_ws(p);
    if (p->i < p->n && p->s[p->i] == close) { p->i++; goto done; }
    for (;;) {
        const char *k = NULL;
        unsigned kl = 0;
        skip_ws(p);
        if (object) {
            if (p->i >= p->n || p->s[p->i] != '"') return unexpected(p, "a member name in double quotes");
            size_t kat = p->i;
            if (!string(p, &k, &kl)) return 0;
            if (count >= JSON_MAX_MEMBERS) return fail(p, kat, "an object with more than %d members", JSON_MAX_MEMBERS);
            if (p->fill && !p->dups_ok) {
                const json_node_t *c = json_first(&p->nodes[self]);
                for (unsigned m = 0; m < count; m++, c += c->size)
                    if (c->key_len == kl && !memcmp(c->key, k, kl))
                        return fail(p, kat, "the key \"%.40s\" twice in one object", k);
            }
            skip_ws(p);
            if (p->i >= p->n || p->s[p->i] != ':') return unexpected(p, "':' after the member name");
            p->i++;
        }
        if (!value(p, k, kl)) return 0;
        count++;
        if (p->fill) p->nodes[self].count = count;   /* the duplicate scan walks the members so far */
        skip_ws(p);
        if (p->i < p->n && p->s[p->i] == ',') {
            size_t comma = p->i++;
            skip_ws(p);
            if (p->i < p->n && p->s[p->i] == close)
                return fail(p, p->i, "a comma before '%c' (trailing commas are not JSON; the comma is at byte %u)",
                            close, (unsigned) comma);
            continue;
        }
        if (p->i < p->n && p->s[p->i] == close) { p->i++; break; }
        return unexpected(p, object ? "',' or '}'" : "',' or ']'");
    }
done:
    if (node) node->count = count;
    p->depth--;
    return 1;
}

static int word(jp_t *p, const char *w) {
    size_t k = strlen(w);
    if (p->n - p->i < k || memcmp(p->s + p->i, w, k)) return 0;
    p->i += k;
    return 1;
}

static int value(jp_t *p, const char *key, unsigned klen) {
    skip_ws(p);
    if (p->i >= p->n) return unexpected(p, "a value");
    size_t idx = p->count++;
    json_node_t *node = NULL;
    if (p->fill) {
        node = &p->nodes[idx];
        memset(node, 0, sizeof(*node));
        node->key = key;
        node->key_len = klen;
        node->at = (unsigned) p->i;
    }
    unsigned c = p->s[p->i];
    int ok, type;
    switch (c) {
    case '{': type = JSON_OBJECT; if (node) node->type = (unsigned char) type; ok = container(p, node, 1); break;
    case '[': type = JSON_ARRAY; if (node) node->type = (unsigned char) type; ok = container(p, node, 0); break;
    case '"': {
        const char *s = NULL;
        unsigned l = 0;
        type = JSON_STRING;
        ok = string(p, &s, &l);
        if (node) { node->str = s; node->len = l; }
        break;
    }
    case 't': type = JSON_TRUE; ok = word(p, "true") || fail(p, p->i, "an unknown word (true, false or null?)"); break;
    case 'f': type = JSON_FALSE; ok = word(p, "false") || fail(p, p->i, "an unknown word (true, false or null?)"); break;
    case 'n': type = JSON_NULL; ok = word(p, "null") || fail(p, p->i, "an unknown word (true, false or null?)"); break;
    default:
        if (c == '-' || (c >= '0' && c <= '9')) { type = JSON_NUMBER; ok = number(p, node); break; }
        return unexpected(p, "a value");
    }
    if (!ok) return 0;
    if (node) { node->type = (unsigned char) type; node->size = (unsigned) (p->count - idx); }
    return 1;
}

static int top(jp_t *p) {
    if (p->n >= 3 && p->s[0] == 0xEF && p->s[1] == 0xBB && p->s[2] == 0xBF) p->i = 3;
    skip_ws(p);
    if (p->i >= p->n) return fail(p, p->i, "the text is empty");
    if (!value(p, NULL, 0)) return 0;
    skip_ws(p);
    if (p->i < p->n) {
        if (p->s[p->i] == '/') return fail(p, p->i, "comments are not JSON");
        return fail(p, p->i, "more text after the value");
    }
    return 1;
}

static void place(json_error_t *err, const unsigned char *s, size_t n) {
    size_t at = err->at < n ? err->at : n;
    int line = 1, col = 1;
    for (size_t k = 0; k < at; k++) {
        if (s[k] == '\n') { line++; col = 1; }
        else if ((s[k] & 0xC0) != 0x80) col++;
    }
    err->line = line;
    err->col = col;
}

static int parse_with(json_doc_t *doc, const char *text, size_t len, json_error_t *err, int dups_ok) {
    json_error_t spare;
    if (!err) err = &spare;
    memset(err, 0, sizeof(*err));
    memset(doc, 0, sizeof(*doc));
    jp_t p;
    memset(&p, 0, sizeof(p));
    p.s = (const unsigned char *) (text ? text : "");
    p.n = text ? len : 0;
    p.err = err;
    p.dups_ok = dups_ok;
    if (p.n > JSON_MAX_TEXT) { fail(&p, 0, "the text is larger than %d bytes", JSON_MAX_TEXT); goto refused; }
    if (!top(&p)) goto refused;
    size_t count = p.count, bytes = p.bytes;
    json_node_t *block = malloc(count * sizeof(json_node_t) + bytes + 1);
    if (!block) { fail(&p, 0, "no memory for %u values", (unsigned) count); goto refused; }
    memset(&p, 0, sizeof(p));
    p.s = (const unsigned char *) text;
    p.n = len;
    p.err = err;
    p.fill = 1;
    p.dups_ok = dups_ok;
    p.nodes = block;
    p.heap = (char *) (block + count);
    if (!top(&p)) { free(block); goto refused; }    /* a duplicate key shows only now */
    doc->nodes = block;
    doc->count = (unsigned) count;
    return 1;
refused:
    place(err, p.s, p.n);
    return 0;
}

int json_parse(json_doc_t *doc, const char *text, size_t len, json_error_t *err) {
    return parse_with(doc, text, len, err, 0);
}

/* ------------------------------------------------------------ the lenient reading */

/*
 * The text org.json (Android's JSONTokener) and a hand would take, written out
 * as strict JSON. One pass, the output at most six bytes per input byte.
 */
typedef struct {
    const unsigned char *s;
    size_t n, i;
    char *o;
    size_t on, room;
    int bad, depth;
} lx_t;

static void lx_put(lx_t *x, const char *b, size_t k) {
    if (x->on + k >= x->room) { x->bad = 1; return; }
    memcpy(x->o + x->on, b, k);
    x->on += k;
}
static void lx_c(lx_t *x, char c) { lx_put(x, &c, 1); }

/* whitespace and comments; the next byte, or -1 at the end */
static int lx_clean(lx_t *x) {
    while (x->i < x->n) {
        unsigned char c = x->s[x->i];
        if (c <= ' ') { x->i++; continue; }
        if (c == '#' || (c == '/' && x->i + 1 < x->n && x->s[x->i + 1] == '/')) {
            while (x->i < x->n && x->s[x->i] != '\n' && x->s[x->i] != '\r') x->i++;
            continue;
        }
        if (c == '/' && x->i + 1 < x->n && x->s[x->i + 1] == '*') {
            x->i += 2;
            while (x->i + 1 < x->n && !(x->s[x->i] == '*' && x->s[x->i + 1] == '/')) x->i++;
            x->i = x->i + 1 < x->n ? x->i + 2 : x->n;
            continue;
        }
        return c;
    }
    return -1;
}

/* a text's bytes into a JSON string's body: control characters escaped, what is not UTF-8 made U+FFFD */
static void lx_text_byte(lx_t *x, const unsigned char *b, size_t left, size_t *used) {
    unsigned c = b[0];
    *used = 1;
    if (c == '"') { lx_put(x, "\\\"", 2); return; }
    if (c == '\\') { lx_put(x, "\\\\", 2); return; }
    if (c < 0x20) { char e[8]; snprintf(e, sizeof(e), "\\u%04x", c); lx_put(x, e, 6); return; }
    if (c < 0x80) { lx_c(x, (char) c); return; }
    int k = utf8_len(b, left);
    if (k > 0) { lx_put(x, (const char *) b, (size_t) k); *used = (size_t) k; return; }
    lx_put(x, "\xEF\xBF\xBD", 3);
}

static int lx_hex(unsigned char c) { return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'f'); }

/* a quoted text, " or ': org.json's escapes; one it does not know stands for its own character */
static void lx_string(lx_t *x, unsigned char q) {
    x->i++;
    lx_c(x, '"');
    while (!x->bad) {
        if (x->i >= x->n) { x->bad = 1; return; }               /* the text ends inside the string */
        unsigned char c = x->s[x->i];
        if (c == q) { x->i++; break; }
        if (c == '\\' && x->i + 1 < x->n) {
            unsigned char e = x->s[x->i + 1];
            x->i += 2;
            if (e == 'u' && x->i + 4 <= x->n && lx_hex(x->s[x->i]) && lx_hex(x->s[x->i + 1]) && lx_hex(x->s[x->i + 2])
                && lx_hex(x->s[x->i + 3])) {
                lx_put(x, "\\u", 2);
                lx_put(x, (const char *) x->s + x->i, 4);
                x->i += 4;
            } else if (e == 'b' || e == 'f' || e == 'n' || e == 'r' || e == 't') {
                char t[2] = { '\\', (char) e };
                lx_put(x, t, 2);
            } else {
                size_t used;
                lx_text_byte(x, x->s + x->i - 1, x->n - (x->i - 1), &used);
                x->i += used - 1;
            }
            continue;
        }
        size_t used;
        lx_text_byte(x, x->s + x->i, x->n - x->i, &used);
        x->i += used;
    }
    lx_c(x, '"');
}

/* a strict JSON number's spelling? */
static int lx_strict_number(const unsigned char *b, size_t k) {
    size_t i = 0;
    if (i < k && b[i] == '-') i++;
    if (i >= k) return 0;
    if (b[i] == '0') i++;
    else if (b[i] >= '1' && b[i] <= '9') while (i < k && b[i] >= '0' && b[i] <= '9') i++;
    else return 0;
    if (i < k && b[i] == '.') { i++; if (i >= k || b[i] < '0' || b[i] > '9') return 0; while (i < k && b[i] >= '0' && b[i] <= '9') i++; }
    if (i < k && (b[i] == 'e' || b[i] == 'E')) {
        i++;
        if (i < k && (b[i] == '+' || b[i] == '-')) i++;
        if (i >= k || b[i] < '0' || b[i] > '9') return 0;
        while (i < k && b[i] >= '0' && b[i] <= '9') i++;
    }
    return i == k;
}

/* a bare word: true / false / null (any case), a number (org.json's: +1, 0x1F, 1.), or a text */
static void lx_literal(lx_t *x, int as_key) {
    size_t a = x->i;
    while (x->i < x->n) {
        unsigned char c = x->s[x->i];
        if (c <= ' ' || strchr("{}[]/\\:,=;#'\"", c)) break;
        x->i++;
    }
    size_t k = x->i - a;
    const unsigned char *b = x->s + a;
    if (!k) { x->bad = 1; return; }
    if (!as_key) {
        static const char *const W[] = { "true", "false", "null" };
        for (int w = 0; w < 3; w++)
            if (k == strlen(W[w])) {
                size_t j = 0;
                while (j < k && (b[j] | 0x20) == W[w][j]) j++;
                if (j == k) { lx_put(x, W[w], k); return; }
            }
        if (lx_strict_number(b, k)) { lx_put(x, (const char *) b, k); return; }
        if (k < 40) {
            char t[48], *end = NULL;
            memcpy(t, b, k);
            t[k] = 0;
            double v;
            if (k > 2 && t[0] == '0' && (t[1] | 0x20) == 'x') v = (double) strtoll(t, &end, 16);
            else v = strtod(t, &end);
            if (end && !*end && isfinite(v)) {
                char o[48];
                if (v == (double) (long long) v && fabs(v) < 1e15) snprintf(o, sizeof(o), "%lld", (long long) v);
                else snprintf(o, sizeof(o), "%.17g", v);
                lx_put(x, o, strlen(o));
                return;
            }
        }
    }
    lx_c(x, '"');
    for (size_t j = 0; j < k && !x->bad; ) {
        size_t used;
        lx_text_byte(x, b + j, k - j, &used);
        j += used;
    }
    lx_c(x, '"');
}

static void lx_value(lx_t *x);

static void lx_container(lx_t *x, int object) {
    char close = object ? '}' : ']';
    x->i++;
    lx_c(x, object ? '{' : '[');
    if (++x->depth > JSON_MAX_DEPTH) { x->bad = 1; return; }
    int count = 0;
    while (!x->bad) {
        int c = lx_clean(x);
        if (c < 0) break;                                          /* the text ends: closed for it */
        if (c == close) { x->i++; break; }
        if (c == ',' || c == ';') { x->i++; continue; }            /* a doubled or trailing comma */
        if (c == '}' || c == ']') { x->i++; break; }               /* the other bracket: taken as the close */
        if (count++) lx_c(x, ',');
        if (object) {
            if (c == '"' || c == '\'') lx_string(x, (unsigned char) c);
            else lx_literal(x, 1);
            int sep = lx_clean(x);
            if (sep == ':' || sep == '=') {
                x->i++;
                if (sep == '=' && x->i < x->n && x->s[x->i] == '>') x->i++;
            } else { x->bad = 1; break; }                          /* a name with no value */
            lx_c(x, ':');
        }
        lx_value(x);
    }
    x->depth--;
    lx_c(x, close);
}

static void lx_value(lx_t *x) {
    int c = lx_clean(x);
    if (c < 0) { x->bad = 1; return; }
    if (c == '{') lx_container(x, 1);
    else if (c == '[') lx_container(x, 0);
    else if (c == '"' || c == '\'') lx_string(x, (unsigned char) c);
    else lx_literal(x, 0);
}

char *json_lenient_text(const char *text, size_t len, size_t *outlen, size_t *bad_at) {
    lx_t x;
    memset(&x, 0, sizeof(x));
    x.s = (const unsigned char *) (text ? text : "");
    x.n = text ? len : 0;
    if (x.n > JSON_MAX_TEXT) { if (bad_at) *bad_at = 0; return NULL; }
    if (x.n >= 3 && x.s[0] == 0xEF && x.s[1] == 0xBB && x.s[2] == 0xBF) x.i = 3;
    x.room = x.n * 6 + 64;
    x.o = malloc(x.room);
    if (!x.o) { if (bad_at) *bad_at = 0; return NULL; }
    lx_value(&x);                                                  /* what follows the value is not read */
    if (x.bad) { if (bad_at) *bad_at = x.i; free(x.o); return NULL; }
    x.o[x.on] = 0;
    if (outlen) *outlen = x.on;
    return x.o;
}

int json_parse_lenient(json_doc_t *doc, const char *text, size_t len, json_error_t *err) {
    json_error_t spare;
    if (!err) err = &spare;
    memset(doc, 0, sizeof(*doc));
    size_t on = 0, bad_at = 0;
    char *o = json_lenient_text(text, len, &on, &bad_at);
    if (!o) {
        memset(err, 0, sizeof(*err));
        err->at = bad_at;
        snprintf(err->what, sizeof(err->what), "not readable even leniently");
        place(err, (const unsigned char *) (text ? text : ""), text ? len : 0);
        return 0;
    }
    int ok = parse_with(doc, o, on, err, 1);
    free(o);
    return ok;
}

void json_free(json_doc_t *doc) {
    if (!doc) return;
    free(doc->nodes);
    doc->nodes = NULL;
    doc->count = 0;
}

const json_node_t *json_root(const json_doc_t *doc) { return doc && doc->count ? doc->nodes : NULL; }

const json_node_t *json_first(const json_node_t *c) {
    return c && (c->type == JSON_ARRAY || c->type == JSON_OBJECT) && c->count ? c + 1 : NULL;
}

const json_node_t *json_next(const json_node_t *c, const json_node_t *v) {
    if (!c || !v) return NULL;
    const json_node_t *n = v + v->size;
    return n < c + c->size ? n : NULL;
}

const json_node_t *json_get(const json_node_t *o, const char *key) {
    if (!o || o->type != JSON_OBJECT || !key) return NULL;
    size_t kl = strlen(key);
    const json_node_t *last = NULL;
    for (const json_node_t *v = json_first(o); v; v = json_next(o, v))
        if (v->key_len == kl && !memcmp(v->key, key, kl)) last = v;    /* a key twice (lenient): the last, as org.json */
    return last;
}

const json_node_t *json_at(const json_node_t *c, unsigned i) {
    const json_node_t *v = json_first(c);
    while (v && i--) v = json_next(c, v);
    return v;
}

/* ------------------------------------------------------------ mod.json */

static const char *const REASON[MODJ_R_COUNT] = {
    "none",
    "file_open", "file_big", "syntax", "root_not_object",
    "field_type",
    "settings_not_array", "settings_many",
    "setting_not_object", "key_missing", "key_bad", "key_dup", "kind_missing", "kind_bad",
    "label_missing", "text_type", "text_empty", "text_nul", "text_langs", "text_lang_code", "label_long",
    "default_missing", "default_type", "default_not_value", "default_range", "default_grid",
    "values_missing", "values_type", "values_count", "value_type", "value_bad", "value_dup",
    "num_missing", "num_whole", "num_order", "num_step", "num_steps", "num_grid",
    "restart_type",
    "unknown_field", "lang_unknown",
};

const char *modj_reason_code(modj_reason_t r) { return r >= 0 && r < MODJ_R_COUNT ? REASON[r] : "?"; }

const char *modj_level_name(modj_level_t l) {
    return l == MODJ_BROKEN ? "broken" : l == MODJ_REFUSED ? "refused" : l == MODJ_NOTE ? "note" : "?";
}

static const char *const PORT_LANGS[] = MODJ_PORT_LANGS;

static void issue(modj_t *m, modj_level_t lv, modj_reason_t r, int setting, const char *key, const char *fmt, ...) {
    if (m->nissues >= MODJ_ISSUES_MAX) { m->issues_lost++; return; }
    modj_issue_t *is = &m->issues[m->nissues++];
    memset(is, 0, sizeof(*is));
    is->level = lv;
    is->reason = r;
    is->setting = setting;
    if (key) snprintf(is->key, sizeof(is->key), "%s", key);
    int k = 0;
    if (setting >= 0 && key && *key) k = snprintf(is->text, sizeof(is->text), "setting %d \"%s\": ", setting + 1, key);
    else if (setting >= 0) k = snprintf(is->text, sizeof(is->text), "setting %d: ", setting + 1);
    if (k < 0 || (size_t) k >= sizeof(is->text)) k = 0;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(is->text + k, sizeof(is->text) - (size_t) k, fmt, ap);
    va_end(ap);
}

static size_t chars(const char *s, size_t n) {
    size_t c = 0;
    for (size_t k = 0; k < n; k++) if (((unsigned char) s[k] & 0xC0) != 0x80) c++;
    return c;
}

/* [a-z0-9_-]{1,32}: a setting's key, a choice's value - both folder names */
static int folder_word(const json_node_t *v) {
    if (!v || v->type != JSON_STRING || !v->len || v->len > MODJ_KEY_MAX) return 0;
    for (unsigned k = 0; k < v->len; k++) {
        char c = v->str[k];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int lang_code(const json_node_t *v) {
    if (!v->key_len || v->key_len > 15) return 0;
    for (unsigned k = 0; k < v->key_len; k++) {
        char c = v->key[k];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_')) return 0;
    }
    return 1;
}

static int port_lang(const char *code) {
    for (size_t k = 0; k < sizeof(PORT_LANGS) / sizeof(PORT_LANGS[0]); k++) if (!strcmp(code, PORT_LANGS[k])) return 1;
    return 0;
}

/*
 * A text: one string or a {language: text} map. max: the characters each
 * language may hold, 0 for no limit. The reason, and why in English, or NONE.
 */
static modj_reason_t check_text(const json_node_t *n, int max, const char *what, char *why, size_t room) {
    if (n->type == JSON_STRING) {
        if (!n->len) { snprintf(why, room, "%s is empty", what); return MODJ_R_TEXT_EMPTY; }
        if (strlen(n->str) != n->len) { snprintf(why, room, "%s holds a \\u0000", what); return MODJ_R_TEXT_NUL; }
        size_t c = chars(n->str, n->len);
        if (max && c > (size_t) max) {
            snprintf(why, room, "%s is %u characters long, %d at most", what, (unsigned) c, max);
            return MODJ_R_LABEL_LONG;
        }
        return MODJ_R_NONE;
    }
    if (n->type != JSON_OBJECT) {
        snprintf(why, room, "%s is neither text nor a {language: text} map", what);
        return MODJ_R_TEXT_TYPE;
    }
    if (n->count > MODJ_LANGS_MAX) {
        snprintf(why, room, "%s has %u languages, %d at most", what, n->count, MODJ_LANGS_MAX);
        return MODJ_R_TEXT_LANGS;
    }
    int any = 0;
    for (const json_node_t *v = json_first(n); v; v = json_next(n, v)) {
        if (!lang_code(v)) {
            snprintf(why, room, "%s: \"%.20s\" is not a language code", what, v->key);
            return MODJ_R_TEXT_LANG_CODE;
        }
        if (v->type != JSON_STRING) {
            snprintf(why, room, "%s: the \"%s\" text is not a string", what, v->key);
            return MODJ_R_TEXT_TYPE;
        }
        if (strlen(v->str) != v->len) {
            snprintf(why, room, "%s: the \"%s\" text holds a \\u0000", what, v->key);
            return MODJ_R_TEXT_NUL;
        }
        size_t c = chars(v->str, v->len);
        if (max && c > (size_t) max) {
            snprintf(why, room, "%s: the \"%s\" text is %u characters long, %d at most", what, v->key, (unsigned) c, max);
            return MODJ_R_LABEL_LONG;
        }
        any |= v->len > 0;
    }
    if (!any) { snprintf(why, room, "%s has no text in any language", what); return MODJ_R_TEXT_EMPTY; }
    return MODJ_R_NONE;
}

/* the languages none of the port's sets know: a note each (a typo "RU" would silently show "en") */
static void lang_notes(modj_t *m, const json_node_t *n, int setting, const char *key, const char *what) {
    if (n->type != JSON_OBJECT) return;
    for (const json_node_t *v = json_first(n); v; v = json_next(n, v))
        if (!port_lang(v->key))
            issue(m, MODJ_NOTE, MODJ_R_LANG_UNKNOWN, setting, key,
                  "%s: language \"%s\" is none of the port's (en ru de es jp kr zh) - shown only when nothing else is there",
                  what, v->key);
}

static int whole(const json_node_t *v, int *out) {
    if (!v || v->type != JSON_NUMBER || !v->whole || v->ival < -MODJ_NUM_LIMIT || v->ival > MODJ_NUM_LIMIT) return 0;
    *out = (int) v->ival;
    return 1;
}

static const char *const KIND[] = { "switch", "choice", "number" };

/* which fields a kind reads: key kind label default restart + its own */
static int field_known(const char *f, modj_kind_t kind, int *other_kind) {
    static const char *const common[] = { "key", "kind", "label", "default", "restart" };
    *other_kind = 0;
    for (size_t k = 0; k < sizeof(common) / sizeof(common[0]); k++) if (!strcmp(f, common[k])) return 1;
    int is_values = !strcmp(f, "values");
    int is_num = !strcmp(f, "min") || !strcmp(f, "max") || !strcmp(f, "step");
    if ((is_values && kind == MODJ_CHOICE) || (is_num && kind == MODJ_NUMBER)) return 1;
    *other_kind = is_values || is_num;
    return 0;
}

#define REFUSE(r, ...) do { issue(m, MODJ_REFUSED, r, idx, key, __VA_ARGS__); return 0; } while (0)

/* one entry of "settings"; 1 when it stands. seen: the well-formed keys of the entries before it */
static int read_setting(modj_t *m, const json_node_t *n, int idx, const char **seen, int nseen, modj_setting_t *s) {
    const char *key = NULL;
    char why[MODJ_ISSUE_TEXT];
    memset(s, 0, sizeof(*s));
    s->index = idx;
    if (n->type != JSON_OBJECT) REFUSE(MODJ_R_SETTING_TYPE, "is not an object {...}");
    const json_node_t *k = json_get(n, "key");
    if (!k) REFUSE(MODJ_R_KEY_MISSING, "has no \"key\"");
    if (!folder_word(k)) {
        if (k->type != JSON_STRING) REFUSE(MODJ_R_KEY_BAD, "\"key\" is not a string");
        REFUSE(MODJ_R_KEY_BAD, "key \"%.40s\" must be 1 to %d of a-z 0-9 _ - (it names a folder)", k->str, MODJ_KEY_MAX);
    }
    key = k->str;
    for (int j = 0; j < nseen; j++)
        if (!strcmp(seen[j], key)) REFUSE(MODJ_R_KEY_DUP, "the key is already taken by an earlier setting");
    s->key = key;

    const json_node_t *kind = json_get(n, "kind");
    if (!kind) REFUSE(MODJ_R_KIND_MISSING, "has no \"kind\" (switch, choice or number)");
    int kd = -1;
    if (kind->type == JSON_STRING)
        for (int j = 0; j < 3; j++) if (!strcmp(kind->str, KIND[j])) kd = j;
    if (kd < 0) {
        if (kind->type == JSON_STRING) REFUSE(MODJ_R_KIND_BAD, "kind \"%.24s\" is none of switch, choice, number", kind->str);
        REFUSE(MODJ_R_KIND_BAD, "\"kind\" is not a string (switch, choice or number)");
    }
    s->kind = (modj_kind_t) kd;

    const json_node_t *label = json_get(n, "label");
    if (!label) REFUSE(MODJ_R_LABEL_MISSING, "has no \"label\"");
    modj_reason_t r = check_text(label, MODJ_LABEL_MAX, "the label", why, sizeof(why));
    if (r) REFUSE(r, "%s", why);
    s->label.node = label;

    const json_node_t *def = json_get(n, "default");
    if (s->kind == MODJ_CHOICE) {
        const json_node_t *vals = json_get(n, "values");
        if (!vals) REFUSE(MODJ_R_VALUES_MISSING, "a choice has no \"values\"");
        if (vals->type != JSON_ARRAY) REFUSE(MODJ_R_VALUES_TYPE, "\"values\" is not an array [...]");
        if (vals->count < MODJ_VALUES_MIN || vals->count > MODJ_VALUES_MAX)
            REFUSE(MODJ_R_VALUES_COUNT, "a choice has %u values, %d to %d needed", vals->count, MODJ_VALUES_MIN, MODJ_VALUES_MAX);
        int j = 0;
        for (const json_node_t *v = json_first(vals); v; v = json_next(vals, v), j++) {
            if (v->type != JSON_OBJECT) REFUSE(MODJ_R_VALUE_TYPE, "value %d is not an object {\"value\", \"label\"}", j + 1);
            const json_node_t *name = json_get(v, "value");
            if (!name) REFUSE(MODJ_R_VALUE_BAD, "value %d has no \"value\"", j + 1);
            if (!folder_word(name)) {
                if (name->type != JSON_STRING) REFUSE(MODJ_R_VALUE_BAD, "value %d: \"value\" is not a string", j + 1);
                REFUSE(MODJ_R_VALUE_BAD, "value %d: \"%.40s\" must be 1 to %d of a-z 0-9 _ - (it names a folder)",
                       j + 1, name->str, MODJ_KEY_MAX);
            }
            for (int q = 0; q < j; q++)
                if (!strcmp(s->values[q].value, name->str)) REFUSE(MODJ_R_VALUE_DUP, "value \"%s\" twice", name->str);
            const json_node_t *vl = json_get(v, "label");
            if (!vl) REFUSE(MODJ_R_LABEL_MISSING, "value \"%s\" has no \"label\"", name->str);
            char what[64];
            snprintf(what, sizeof(what), "the label of value \"%s\"", name->str);
            r = check_text(vl, MODJ_LABEL_MAX, what, why, sizeof(why));
            if (r) REFUSE(r, "%s", why);
            s->values[j].value = name->str;
            s->values[j].label.node = vl;
        }
        s->nvalues = j;
        if (!def) REFUSE(MODJ_R_DEFAULT_MISSING, "has no \"default\"");
        if (def->type != JSON_STRING) REFUSE(MODJ_R_DEFAULT_TYPE, "a choice's default must be one of its values, as a string");
        s->def = -1;
        for (int q = 0; q < s->nvalues; q++) if (!strcmp(s->values[q].value, def->str)) s->def = q;
        if (s->def < 0) REFUSE(MODJ_R_DEFAULT_NOT_VALUE, "default \"%.40s\" is not one of the values", def->str);
    } else if (s->kind == MODJ_NUMBER) {
        static const char *const nf[] = { "min", "max", "step" };
        int v3[3];
        for (int j = 0; j < 3; j++) {
            const json_node_t *v = json_get(n, nf[j]);
            if (!v) REFUSE(MODJ_R_NUM_MISSING, "a number has no \"%s\"", nf[j]);
            if (!whole(v, &v3[j]))
                REFUSE(MODJ_R_NUM_WHOLE, "\"%s\" is not a whole number from -%d to %d", nf[j], MODJ_NUM_LIMIT, MODJ_NUM_LIMIT);
        }
        s->min = v3[0]; s->max = v3[1]; s->step = v3[2];
        if (s->min >= s->max) REFUSE(MODJ_R_NUM_ORDER, "min %d is not below max %d", s->min, s->max);
        if (s->step <= 0) REFUSE(MODJ_R_NUM_STEP, "step %d is not above zero", s->step);
        long span = (long) s->max - s->min;
        if (span / s->step > MODJ_STEPS_MAX)
            REFUSE(MODJ_R_NUM_STEPS, "%ld steps from %d to %d, %d at most", span / s->step, s->min, s->max, MODJ_STEPS_MAX);
        if (span % s->step)
            REFUSE(MODJ_R_NUM_GRID, "max %d is not reached from min %d in steps of %d", s->max, s->min, s->step);
        if (!def) REFUSE(MODJ_R_DEFAULT_MISSING, "has no \"default\"");
        if (def->type != JSON_NUMBER) REFUSE(MODJ_R_DEFAULT_TYPE, "a number's default is not a number");
        if (!whole(def, &s->def))
            REFUSE(MODJ_R_NUM_WHOLE, "\"default\" is not a whole number from -%d to %d", MODJ_NUM_LIMIT, MODJ_NUM_LIMIT);
        if (s->def < s->min || s->def > s->max)
            REFUSE(MODJ_R_DEFAULT_RANGE, "default %d is outside %d..%d", s->def, s->min, s->max);
        if ((s->def - s->min) % s->step)
            REFUSE(MODJ_R_DEFAULT_GRID, "default %d is not min %d plus whole steps of %d", s->def, s->min, s->step);
    } else {
        if (!def) REFUSE(MODJ_R_DEFAULT_MISSING, "has no \"default\"");
        if (def->type != JSON_TRUE && def->type != JSON_FALSE) REFUSE(MODJ_R_DEFAULT_TYPE, "a switch's default is not true or false");
        s->def = def->type == JSON_TRUE;
    }

    const json_node_t *rs = json_get(n, "restart");
    s->restart = 1;
    if (rs) {
        if (rs->type != JSON_TRUE && rs->type != JSON_FALSE) REFUSE(MODJ_R_RESTART_TYPE, "\"restart\" is not true or false");
        s->restart = rs->type == JSON_TRUE;
    }

    /* it stands: now the notes */
    for (const json_node_t *f = json_first(n); f; f = json_next(n, f)) {
        int other;
        if (field_known(f->key, s->kind, &other)) continue;
        if (other)
            issue(m, MODJ_NOTE, MODJ_R_UNKNOWN_FIELD, idx, key, "\"%.24s\" means nothing for a %s - ignored", f->key, KIND[kd]);
        else issue(m, MODJ_NOTE, MODJ_R_UNKNOWN_FIELD, idx, key, "unknown field \"%.24s\" - ignored", f->key);
    }
    lang_notes(m, label, idx, key, "the label");
    for (int j = 0; j < s->nvalues; j++) {
        char what[64];
        snprintf(what, sizeof(what), "the label of value \"%s\"", s->values[j].value);
        lang_notes(m, s->values[j].label.node, idx, key, what);
    }
    return 1;
}

#undef REFUSE

static void read_settings(modj_t *m, const json_node_t *arr) {
    if (arr->type != JSON_ARRAY) {
        issue(m, MODJ_REFUSED, MODJ_R_SETTINGS_TYPE, -1, NULL, "\"settings\" is not an array [...] - no settings");
        return;
    }
    const char *seen[MODJ_SETTINGS_MAX];
    int nseen = 0, idx = 0;
    for (const json_node_t *n = json_first(arr); n; n = json_next(arr, n), idx++) {
        if (idx >= MODJ_SETTINGS_MAX) {
            issue(m, MODJ_REFUSED, MODJ_R_SETTINGS_MANY, idx, NULL, "past the limit of %d settings - it and the %u after it are dropped",
                  MODJ_SETTINGS_MAX, arr->count - (unsigned) idx - 1);
            break;
        }
        modj_setting_t *s = &m->settings[m->nsettings];
        if (read_setting(m, n, idx, seen, nseen, s)) m->nsettings++;
        /* a refused entry still holds its key, so a later one cannot take the same folder */
        const json_node_t *k = n->type == JSON_OBJECT ? json_get(n, "key") : NULL;
        if (folder_word(k)) {
            int dup = 0;
            for (int j = 0; j < nseen; j++) dup |= !strcmp(seen[j], k->str);
            if (!dup) seen[nseen++] = k->str;
        }
    }
}

/*
 * mod.json not readable even leniently: its name, as 1.666 took it - the first
 * "name" (quoted either way or bare) followed by ':' or '=' and a quoted text.
 * The mod is never nameless for a slip. Held in a small document of its own.
 */
static void name_from_text(modj_t *m, const char *text, size_t len) {
    const unsigned char *s = (const unsigned char *) text;
    for (size_t i = 0; i + 4 <= len; i++) {
        if (memcmp(s + i, "name", 4)) continue;
        size_t j = i + 4;
        if (j < len && (s[j] == '"' || s[j] == '\'')) j++;
        while (j < len && (s[j] == ' ' || s[j] == '\t')) j++;
        if (j >= len || (s[j] != ':' && s[j] != '=')) continue;
        j++;
        while (j < len && (s[j] <= ' ')) j++;
        if (j >= len || (s[j] != '"' && s[j] != '\'')) continue;
        unsigned char q = s[j++];
        size_t a = j;
        while (j < len && s[j] != q && s[j] != '\n') j++;
        if (j >= len || s[j] != q || j == a || j - a > 200) continue;
        char doc[700];
        size_t k = (size_t) snprintf(doc, sizeof(doc), "{\"name\":\"");
        for (size_t c = a; c < j && k + 8 < sizeof(doc); c++) {
            if (s[c] == '"' || s[c] == '\\') doc[k++] = '\\';
            if (s[c] < 0x20) continue;
            doc[k++] = (char) s[c];
        }
        k += (size_t) snprintf(doc + k, sizeof(doc) - k, "\"}");
        json_error_t e;
        json_doc_t d;
        if (!json_parse_lenient(&d, doc, k, &e)) return;
        json_free(&m->doc);
        m->doc = d;
        m->name.node = json_get(json_root(&m->doc), "name");
        return;
    }
}

static int read_doc(modj_t *m, const char *text, size_t len) {
    if (len > MODJ_FILE_MAX) {
        issue(m, MODJ_BROKEN, MODJ_R_FILE_BIG, -1, NULL, "mod.json is %lu bytes, %d at most", (unsigned long) len, MODJ_FILE_MAX);
        return 0;
    }
    if (!json_parse(&m->doc, text, len, &m->error)) {
        json_error_t strict = m->error;
        if (!json_parse_lenient(&m->doc, text, len, &m->error)) {
            issue(m, MODJ_BROKEN, MODJ_R_SYNTAX, -1, NULL, "mod.json is not JSON: line %d, column %d (byte %lu): %s",
                  strict.line, strict.col, (unsigned long) strict.at, strict.what);
            name_from_text(m, text, len);
            return 0;
        }
        m->lenient = 1;
        issue(m, MODJ_NOTE, MODJ_R_SYNTAX, -1, NULL, "mod.json is not strict JSON (line %d, column %d: %s) - read "
              "leniently, as 1.666 did", strict.line, strict.col, strict.what);
    }
    const json_node_t *root = json_root(&m->doc);
    if (root->type != JSON_OBJECT) {
        issue(m, MODJ_BROKEN, MODJ_R_ROOT, -1, NULL, "mod.json is not an object {...}");
        json_free(&m->doc);
        name_from_text(m, text, len);
        return 0;
    }
    static const char *const texts[] = { "name", "version", "author", "description" };
    modj_text_t *slot[] = { &m->name, &m->version, &m->author, &m->description };
    for (const json_node_t *f = json_first(root); f; f = json_next(root, f)) {
        int known = 0;
        if (json_get(root, f->key) != f) continue;     /* a key twice (lenient): the last one stands */
        if (!strcmp(f->key, "id")) {
            known = 1;
            if (f->type != JSON_STRING || strlen(f->str) != f->len || !f->len || f->len > MODJ_ID_MAX)
                issue(m, MODJ_NOTE, MODJ_R_FIELD_TYPE, -1, NULL, "\"id\" is not text of 1 to %d bytes - ignored", MODJ_ID_MAX);
            else m->id = f->str;
        }
        for (int j = 0; j < 4; j++) {
            if (strcmp(f->key, texts[j])) continue;
            known = 1;
            char what[24], why[MODJ_ISSUE_TEXT];
            snprintf(what, sizeof(what), "\"%s\"", texts[j]);
            modj_reason_t r = check_text(f, 0, what, why, sizeof(why));
            if (r) issue(m, MODJ_NOTE, r, -1, NULL, "%s - ignored", why);
            else { slot[j]->node = f; lang_notes(m, f, -1, NULL, what); }
        }
        if (!strcmp(f->key, "settings")) { known = 1; read_settings(m, f); }
        if (!known) issue(m, MODJ_NOTE, MODJ_R_UNKNOWN_FIELD, -1, NULL, "unknown field \"%.24s\" - ignored", f->key);
    }
    m->ok = 1;
    return 1;
}

int modj_read_text(modj_t *m, const char *text, size_t len) {
    memset(m, 0, sizeof(*m));
    return read_doc(m, text, len);
}

int modj_read_file(modj_t *m, const char *path) {
    memset(m, 0, sizeof(*m));
    FILE *f = fopen(path, "rb");
    if (!f) {
        m->open_errno = errno ? errno : EIO;
        issue(m, MODJ_BROKEN, MODJ_R_FILE_OPEN, -1, NULL, "mod.json could not be opened (%s)", strerror(m->open_errno));
        return 0;
    }
    char *buf = malloc(MODJ_FILE_MAX + 1);
    if (!buf) {
        fclose(f);
        issue(m, MODJ_BROKEN, MODJ_R_FILE_OPEN, -1, NULL, "no memory to read mod.json");
        return 0;
    }
    size_t n = fread(buf, 1, MODJ_FILE_MAX + 1, f);
    int bad = ferror(f), err = errno;
    fclose(f);
    int ok = 0;
    if (bad) {
        m->open_errno = err ? err : EIO;
        issue(m, MODJ_BROKEN, MODJ_R_FILE_OPEN, -1, NULL, "mod.json could not be read (%s)", strerror(m->open_errno));
    } else ok = read_doc(m, buf, n);    /* more than MODJ_FILE_MAX read: refused as too big */
    free(buf);
    return ok;
}

void modj_free(modj_t *m) {
    if (!m) return;
    json_free(&m->doc);
    memset(m, 0, sizeof(*m));
}

const char *modj_text(modj_text_t t, const char *want) {
    const json_node_t *n = t.node, *v;
    if (!n) return NULL;
    if (n->type == JSON_STRING) return n->len ? n->str : NULL;
    if (want && *want && (v = json_get(n, want)) && v->type == JSON_STRING && v->len) return v->str;
    if ((v = json_get(n, "en")) && v->type == JSON_STRING && v->len) return v->str;
    for (v = json_first(n); v; v = json_next(n, v)) if (v->type == JSON_STRING && v->len) return v->str;
    return NULL;
}

size_t modj_copy(char *out, size_t room, const char *text) {
    if (!room) return 0;
    size_t n = text ? strlen(text) : 0;
    if (n >= room) {
        n = room - 1;
        while (n && ((unsigned char) text[n] & 0xC0) == 0x80) n--;   /* text[n] would start the next character */
    }
    if (n) memcpy(out, text, n);
    out[n] = 0;
    return n;
}

const modj_setting_t *modj_find(const modj_t *m, const char *key) {
    for (int j = 0; m && key && j < m->nsettings; j++) if (!strcmp(m->settings[j].key, key)) return &m->settings[j];
    return NULL;
}

int modj_value_name(const modj_setting_t *s, int value, char *out, size_t room) {
    if (room) out[0] = 0;
    switch (s->kind) {
    case MODJ_SWITCH:
        if (value != 0 && value != 1) return 0;
        snprintf(out, room, "%s", value ? "on" : "off");
        return 1;
    case MODJ_CHOICE:
        if (value < 0 || value >= s->nvalues) return 0;
        snprintf(out, room, "%s", s->values[value].value);
        return 1;
    case MODJ_NUMBER:
        if (value < s->min || value > s->max || (value - s->min) % s->step) return 0;
        snprintf(out, room, "%d", value);
        return 1;
    }
    return 0;
}

int modj_parse_value(const modj_setting_t *s, const char *text, int *value) {
    if (!s || !text) return 0;
    switch (s->kind) {
    case MODJ_SWITCH:
        if (!strcmp(text, "on")) { *value = 1; return 1; }
        if (!strcmp(text, "off")) { *value = 0; return 1; }
        return 0;
    case MODJ_CHOICE:
        for (int j = 0; j < s->nvalues; j++) if (!strcmp(text, s->values[j].value)) { *value = j; return 1; }
        return 0;
    case MODJ_NUMBER: {
        /* the way modj_value_name writes it: -?(0|[1-9][0-9]*) */
        const char *p = text;
        if (*p == '-') p++;
        if (!*p || (*p == '0' && p[1]) || strlen(p) > 7) return 0;
        long v = 0;
        for (; *p; p++) {
            if (*p < '0' || *p > '9') return 0;
            v = v * 10 + (*p - '0');
        }
        if (text[0] == '-') v = -v;
        if (v < s->min || v > s->max || (v - s->min) % s->step) return 0;
        *value = (int) v;
        return 1;
    }
    }
    return 0;
}

/* ------------------------------------------------------------ the self-test */

#if ISAAC_DIAG
#define TLOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)

static int t_checks, t_failed;

static void check(int ok, const char *fmt, ...) {
    t_checks++;
    if (ok) return;
    t_failed++;
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    TLOG("json: selftest FAIL %s", line);
}

/* a document that must parse; returns the root's first string for spot checks */
static void t_valid(const char *text, size_t len) {
    json_doc_t d;
    json_error_t e;
    int ok = json_parse(&d, text, len, &e);
    check(ok, "valid %.60s refused: byte %lu: %s", text, (unsigned long) e.at, e.what);
    if (ok) json_free(&d);
}

static void t_invalid(const char *text, size_t len, size_t at, const char *part) {
    json_doc_t d;
    json_error_t e;
    int ok = json_parse(&d, text, len, &e);
    check(!ok, "invalid %.60s accepted", text);
    if (ok) { json_free(&d); return; }
    check(e.at == at && strstr(e.what, part), "invalid %.60s: byte %lu \"%s\", expected byte %lu with \"%s\"",
          text, (unsigned long) e.at, e.what, (unsigned long) at, part);
}

static void t_string(const char *text, const char *want, unsigned wlen) {
    json_doc_t d;
    json_error_t e;
    if (!json_parse(&d, text, strlen(text), &e)) { check(0, "string %s refused: %s", text, e.what); return; }
    const json_node_t *r = json_root(&d);
    check(r->type == JSON_STRING && r->len == wlen && !memcmp(r->str, want, wlen), "string %s decoded wrong", text);
    json_free(&d);
}

static void t_number(const char *text, double num, int is_whole, long long iv) {
    json_doc_t d;
    json_error_t e;
    if (!json_parse(&d, text, strlen(text), &e)) { check(0, "number %s refused: %s", text, e.what); return; }
    const json_node_t *r = json_root(&d);
    check(r->type == JSON_NUMBER && r->num == num && r->whole == is_whole && (!is_whole || r->ival == iv),
          "number %s: %.17g whole %d ival %lld", text, r->num, r->whole, r->ival);
    json_free(&d);
}

#define GOOD_SETTING "{\"key\":\"ok\",\"kind\":\"switch\",\"label\":\"Ok\",\"default\":true}"

/* one bad setting before a good one: the bad one refused with this reason, the good one standing */
static void t_refuse(const char *setting, modj_reason_t want, int at) {
    static char doc[4096];
    snprintf(doc, sizeof(doc), "{\"name\":\"T\",\"settings\":[%s," GOOD_SETTING "]}", setting);
    modj_t *m = malloc(sizeof(modj_t));
    if (!m) return;
    int ok = modj_read_text(m, doc, strlen(doc));
    int found = 0;
    for (int j = 0; j < m->nissues; j++)
        if (m->issues[j].level == MODJ_REFUSED && m->issues[j].reason == want && m->issues[j].setting == at) found = 1;
    int stands = 0;
    for (int j = 0; j < m->nsettings; j++) stands |= !strcmp(m->settings[j].key, "ok") && m->settings[j].index == at + 1;
    check(ok && found && stands && m->nsettings == 1, "refusal %s: ok %d found %d good %d settings %d, first issue %s: %s",
          modj_reason_code(want), ok, found, stands, m->nsettings,
          m->nissues ? modj_reason_code(m->issues[0].reason) : "-", m->nissues ? m->issues[0].text : "-");
    modj_free(m);
    free(m);
}

static const modj_issue_t *t_has(const modj_t *m, modj_level_t lv, modj_reason_t r) {
    for (int j = 0; j < m->nissues; j++) if (m->issues[j].level == lv && m->issues[j].reason == r) return &m->issues[j];
    return NULL;
}

static void t_json(void) {
    /* valid */
    static const char *const good[] = {
        "{}", "[]", "0", "-0", "\"x\"", "true", "false", "null", " \t\r\n[ ] ",
        "[1, 2.5, -3e2, 1E+2, 0.0, \"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\", {\"k\": [null, {}]}]",
        "{\"a\":{\"b\":{\"c\":[[[]]]}}, \"d\":\"\xD0\xB9\xE2\x82\xAC\xF0\x9F\x98\x80\"}",
        "\xEF\xBB\xBF{\"bom\":1}",
        "{\"a\":1,\"A\":2,\"a \":3}",
    };
    for (size_t k = 0; k < sizeof(good) / sizeof(good[0]); k++) t_valid(good[k], strlen(good[k]));
    {
        char deep[80];
        memset(deep, '[', 32);
        memset(deep + 32, ']', 32);
        t_valid(deep, 64);                                /* JSON_MAX_DEPTH exactly */
        memset(deep, '[', 33);
        memset(deep + 33, ']', 33);
        t_invalid(deep, 66, 32, "nesting deeper");
    }
    /* strings */
    t_string("\"\\u00e9\"", "\xC3\xA9", 2);
    t_string("\"\\u20AC\"", "\xE2\x82\xAC", 3);
    t_string("\"\\ud83d\\ude00\"", "\xF0\x9F\x98\x80", 4);
    t_string("\"\\uDBFF\\uDFFF\"", "\xF4\x8F\xBF\xBF", 4);
    t_string("\"a\\u0000b\"", "a\0b", 3);
    t_string("\"\\u0041\\n\\t\\/\"", "A\n\t/", 4);
    t_string("\"\xD0\x9F\xD1\x80\xD0\xB8\"", "\xD0\x9F\xD1\x80\xD0\xB8", 6);
    t_string("\"\"", "", 0);
    /* numbers */
    t_number("0", 0, 1, 0);
    t_number("-0", 0, 1, 0);
    t_number("42", 42, 1, 42);
    t_number("-17", -17, 1, -17);
    t_number("1e2", 100, 1, 100);
    t_number("1.0", 1, 1, 1);
    t_number("1.5", 1.5, 0, 0);
    t_number("-2.5e-1", -0.25, 0, 0);
    t_number("9223372036854775807", 9223372036854775807.0, 1, LLONG_MAX);
    t_number("-9223372036854775808", -9223372036854775808.0, 1, LLONG_MIN);
    t_number("9223372036854775808", 9223372036854775808.0, 0, 0);
    t_number("1e-999", 0, 1, 0);
    /* invalid, with the byte of the fault */
    struct { const char *text; size_t at; const char *part; } bad[] = {
        { "", 0, "empty" },
        { "   ", 3, "empty" },
        { "{", 1, "ends" },
        { "[1,]", 3, "trailing commas" },
        { "{\"a\":1,}", 7, "trailing commas" },
        { "[1 2]", 3, "',' or ']'" },
        { "{\"a\" 1}", 5, "':'" },
        { "{a:1}", 1, "double quotes" },
        { "{'a':1}", 1, "single" },
        { "['a']", 1, "single" },
        { "[01]", 1, "leading zero" },
        { "[-]", 2, "digit" },
        { "[1.]", 3, "after '.'" },
        { "[1e]", 3, "exponent" },
        { "[.5]", 1, "a value" },
        { "[+1]", 1, "a value" },
        { "\"abc", 0, "never closes" },
        { "\"a\\x\"", 2, "unknown escape" },
        { "\"\\u12G4\"", 1, "four hex" },
        { "\"\\ud800\"", 1, "high surrogate" },
        { "\"\\udc00\"", 1, "low surrogate" },
        { "\"\\ud800\\u0041\"", 1, "high surrogate" },
        { "\"a\nb\"", 2, "control character" },
        { "\"\xC0\xAF\"", 1, "UTF-8" },
        { "\"\xED\xA0\x80\"", 1, "UTF-8" },
        { "\"\xF4\x90\x80\x80\"", 1, "UTF-8" },
        { "\"\xE2\x82\"", 1, "UTF-8" },
        { "[1] x", 4, "more text" },
        { "tru", 0, "unknown word" },
        { "nulll", 4, "more text" },
        { "{\"a\":1,\"a\":2}", 7, "twice" },
        { "{\"x\":{\"a\":1},\"y\":{\"b\":1,\"b\":[]}}", 24, "twice" },
        { "// c\n{}", 0, "comments" },
        { "{} // c", 3, "comments" },
        { "[1, /* c */ 2]", 4, "comments" },
        { "1e999", 0, "too large" },
        { "[NaN]", 1, "a value" },
        { "{\"a\":1}}", 7, "more text" },
        { "[\"a\",,1]", 5, "a value" },
        { "{,}", 1, "member name" },
    };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); k++) t_invalid(bad[k].text, strlen(bad[k].text), bad[k].at, bad[k].part);
    t_invalid("[1]\0", 4, 3, "more text");            /* a NUL past the value */
    t_invalid("[1,\0]", 5, 3, "byte 0x00");           /* a NUL where a value goes */
    t_invalid("\"a\0b\"", 5, 2, "U+0000");             /* a raw NUL inside a string */
    {
        /* where: line and column (characters, not bytes) */
        json_doc_t d;
        json_error_t e;
        const char *t = "{\n  \"\xD0\xB9\xD0\xB9\": 1,\n  \"b\" 2\n}";
        check(!json_parse(&d, t, strlen(t), &e) && e.line == 3 && e.col == 7, "line/column: %d:%d \"%s\"", e.line, e.col, e.what);
    }
    {
        /* the walk: siblings past nested children, json_get, json_at */
        json_doc_t d;
        json_error_t e;
        const char *t = "{\"a\":[1,[2,3],{\"x\":4}],\"b\":\"s\",\"c\":{}}";
        if (json_parse(&d, t, strlen(t), &e)) {
            const json_node_t *r = json_root(&d), *a = json_get(r, "a"), *b = json_get(r, "b"), *c = json_get(r, "c");
            check(r->count == 3 && r->size == d.count && a && a->count == 3 && b && b->type == JSON_STRING && !strcmp(b->str, "s")
                  && c && c->type == JSON_OBJECT && !c->count && !json_first(c),
                  "walk: root %u members, a %p, b %p, c %p", r->count, (void *) a, (void *) b, (void *) c);
            const json_node_t *x = json_get(json_at(a, 2), "x");
            check(x && x->whole && x->ival == 4 && json_at(json_at(a, 1), 1)->ival == 3 && !json_at(a, 3) && !json_get(r, "z"),
                  "walk: json_at / json_get");
            json_free(&d);
        } else check(0, "walk document refused: %s", e.what);
    }
    {
        /* the size limit */
        size_t n = JSON_MAX_TEXT + 1;
        char *big = malloc(n);
        if (big) {
            memset(big, ' ', n);
            big[0] = '1';
            t_invalid(big, n, 0, "larger than");
            free(big);
        }
    }
}

static void t_modj(void) {
    modj_t *m = malloc(sizeof(modj_t));
    if (!m) { check(0, "no memory for modj_t"); return; }
    /* the schema's example (the mod settings schema notes, section 3) */
    const char *doc =
        "{\n"
        "  \"id\": \"better-isaac\",\n"
        "  \"name\": \"Better Isaac Sprites\",\n"
        "  \"version\": \"v1.2\",\n"
        "  \"author\": \"Someone\",\n"
        "  \"description\": {\n"
        "    \"en\": \"Replaces Isaac and his tears with a cleaner hand-drawn style. 214 files.\",\n"
        "    \"ru\": \"\xD0\x97\xD0\xB0\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8F\xD0\xB5\xD1\x82 \xD0\x90\xD0\xB9\xD0\xB7\xD0\xB5\xD0\xBA\xD0\xB0.\"\n"
        "  },\n"
        "  \"settings\": [\n"
        "    { \"key\": \"blood-tears\", \"kind\": \"switch\",\n"
        "      \"label\": { \"en\": \"Blood tears\", \"ru\": \"\xD0\x9A\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xB2\xD1\x8B\xD0\xB5 \xD1\x81\xD0\xBB\xD1\x91\xD0\xB7\xD1\x8B\" },\n"
        "      \"default\": false },\n"
        "    { \"key\": \"style\", \"kind\": \"choice\", \"label\": { \"en\": \"Isaac style\", \"ru\": \"\xD0\xA0\xD0\xB8\xD1\x81\xD0\xBE\xD0\xB2\xD0\xBA\xD0\xB0\" },\n"
        "      \"values\": [\n"
        "        { \"value\": \"classic\", \"label\": { \"en\": \"Classic\", \"ru\": \"\xD0\x9A\xD0\xBB\xD0\xB0\xD1\x81\xD1\x81\xD0\xB8\xD0\xBA\xD0\xB0\" } },\n"
        "        { \"value\": \"clean\",   \"label\": { \"en\": \"Clean\" } },\n"
        "        { \"value\": \"retro\",   \"label\": \"Retro\" }\n"
        "      ],\n"
        "      \"default\": \"clean\" },\n"
        "    { \"key\": \"outline\", \"kind\": \"number\", \"label\": { \"en\": \"Outline width\" },\n"
        "      \"min\": 0, \"max\": 3, \"step\": 1, \"default\": 1 },\n"
        "    { \"key\": \"costumes\", \"kind\": \"switch\", \"label\": \"Costumes too\", \"default\": true, \"restart\": false }\n"
        "  ]\n"
        "}\n";
    int ok = modj_read_text(m, doc, strlen(doc));
    check(ok && m->ok && m->nissues == 0 && m->nsettings == 4, "schema example: ok %d, %d issues (%s), %d settings",
          ok, m->nissues, m->nissues ? m->issues[0].text : "-", m->nsettings);
    if (ok && m->nsettings == 4) {
        const modj_setting_t *s = m->settings;
        check(m->id && !strcmp(m->id, "better-isaac") && !strcmp(modj_text(m->name, "ru"), "Better Isaac Sprites")
              && !strcmp(modj_text(m->version, "en"), "v1.2") && !strcmp(modj_text(m->author, NULL), "Someone"),
              "schema example: the mod's fields");
        check(!strncmp(modj_text(m->description, "ru"), "\xD0\x97\xD0\xB0", 4)
              && !strncmp(modj_text(m->description, "de"), "Replaces", 8)
              && !strncmp(modj_text(m->description, NULL), "Replaces", 8), "schema example: description by language");
        check(s[0].kind == MODJ_SWITCH && !strcmp(s[0].key, "blood-tears") && s[0].def == 0 && s[0].restart == 1
              && !strcmp(modj_text(s[0].label, "en"), "Blood tears"), "schema example: blood-tears");
        check(s[1].kind == MODJ_CHOICE && s[1].nvalues == 3 && s[1].def == 1 && !strcmp(s[1].values[2].value, "retro")
              && !strcmp(modj_text(s[1].values[1].label, "ru"), "Clean") && !strcmp(modj_text(s[1].values[2].label, "jp"), "Retro"),
              "schema example: style");
        check(s[2].kind == MODJ_NUMBER && s[2].min == 0 && s[2].max == 3 && s[2].step == 1 && s[2].def == 1, "schema example: outline");
        check(s[3].kind == MODJ_SWITCH && s[3].def == 1 && s[3].restart == 0 && s[3].index == 3, "schema example: costumes");
        check(modj_find(m, "outline") == &s[2] && !modj_find(m, "nope"), "modj_find");
        char v[40];
        int x = -1;
        check(modj_value_name(&s[0], 1, v, sizeof(v)) && !strcmp(v, "on") && modj_value_name(&s[1], 2, v, sizeof(v))
              && !strcmp(v, "retro") && modj_value_name(&s[2], 3, v, sizeof(v)) && !strcmp(v, "3")
              && !modj_value_name(&s[1], 3, v, sizeof(v)) && !modj_value_name(&s[2], 4, v, sizeof(v)), "modj_value_name");
        check(modj_parse_value(&s[0], "off", &x) && x == 0 && modj_parse_value(&s[1], "classic", &x) && x == 0
              && modj_parse_value(&s[2], "2", &x) && x == 2 && !modj_parse_value(&s[2], "02", &x) && !modj_parse_value(&s[2], "4", &x)
              && !modj_parse_value(&s[2], "", &x) && !modj_parse_value(&s[1], "Retro", &x) && !modj_parse_value(&s[0], "true", &x),
              "modj_parse_value");
    }
    modj_free(m);

    /* the language rule: wanted, then en, then the first present; empty counts as absent */
    const char *langs = "{\"name\":{\"ru\":\"R\",\"de\":\"D\"},\"author\":{\"en\":\"\",\"es\":\"S\"},\"version\":{\"de\":\"D\",\"en\":\"E\"}}";
    ok = modj_read_text(m, langs, strlen(langs));
    check(ok && !strcmp(modj_text(m->name, "es"), "R") && !strcmp(modj_text(m->name, "de"), "D")
          && !strcmp(modj_text(m->author, "ru"), "S") && !strcmp(modj_text(m->version, "ru"), "E")
          && !strcmp(modj_text(m->version, "de"), "D") && !modj_text(m->description, "en"), "the language rule");
    modj_free(m);

    /* every refusal of a setting */
    char longkey[64], label25[64], many[1200], vals17[1600];
    memset(longkey, 'k', 33); longkey[33] = 0;
    memset(label25, 'L', 25); label25[25] = 0;
    char s1[2048];
#define R(text, reason) t_refuse(text, reason, 0)
    R("5", MODJ_R_SETTING_TYPE);
    R("{\"kind\":\"switch\",\"label\":\"A\",\"default\":true}", MODJ_R_KEY_MISSING);
    R("{\"key\":\"Bad Key\",\"kind\":\"switch\",\"label\":\"A\",\"default\":true}", MODJ_R_KEY_BAD);
    R("{\"key\":7,\"kind\":\"switch\",\"label\":\"A\",\"default\":true}", MODJ_R_KEY_BAD);
    R("{\"key\":\"\",\"kind\":\"switch\",\"label\":\"A\",\"default\":true}", MODJ_R_KEY_BAD);
    R("{\"key\":\"a.b\",\"kind\":\"switch\",\"label\":\"A\",\"default\":true}", MODJ_R_KEY_BAD);
    snprintf(s1, sizeof(s1), "{\"key\":\"%s\",\"kind\":\"switch\",\"label\":\"A\",\"default\":true}", longkey);
    R(s1, MODJ_R_KEY_BAD);
    {
        /* the same key twice: the first stands, the second is refused */
        const char *t = "{\"settings\":[" GOOD_SETTING "," GOOD_SETTING "]}";
        ok = modj_read_text(m, t, strlen(t));
        const modj_issue_t *is = t_has(m, MODJ_REFUSED, MODJ_R_KEY_DUP);
        check(ok && m->nsettings == 1 && m->settings[0].index == 0 && is && is->setting == 1 && !strcmp(is->key, "ok")
              && m->nissues == 1, "duplicate key: %d settings, %d issues", m->nsettings, m->nissues);
        modj_free(m);
    }
    R("{\"key\":\"a\",\"label\":\"A\",\"default\":true}", MODJ_R_KIND_MISSING);
    R("{\"key\":\"a\",\"kind\":\"slider\",\"label\":\"A\",\"default\":true}", MODJ_R_KIND_BAD);
    R("{\"key\":\"a\",\"kind\":5,\"label\":\"A\",\"default\":true}", MODJ_R_KIND_BAD);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"default\":true}", MODJ_R_LABEL_MISSING);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":5,\"default\":true}", MODJ_R_TEXT_TYPE);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":{\"en\":5},\"default\":true}", MODJ_R_TEXT_TYPE);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":\"\",\"default\":true}", MODJ_R_TEXT_EMPTY);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":{},\"default\":true}", MODJ_R_TEXT_EMPTY);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":{\"en\":\"\",\"ru\":\"\"},\"default\":true}", MODJ_R_TEXT_EMPTY);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":\"a\\u0000b\",\"default\":true}", MODJ_R_TEXT_NUL);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":{\"en\":\"x\\u0000\"},\"default\":true}", MODJ_R_TEXT_NUL);
    {
        int k = snprintf(many, sizeof(many), "{\"key\":\"a\",\"kind\":\"switch\",\"default\":true,\"label\":{");
        for (int j = 0; j < 17; j++) k += snprintf(many + k, sizeof(many) - (size_t) k, "%s\"l%c\":\"x\"", j ? "," : "", 'a' + j);
        snprintf(many + k, sizeof(many) - (size_t) k, "}}");
        R(many, MODJ_R_TEXT_LANGS);
    }
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":{\"\":\"x\"},\"default\":true}", MODJ_R_TEXT_LANG_CODE);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":{\"e n\":\"x\"},\"default\":true}", MODJ_R_TEXT_LANG_CODE);
    snprintf(s1, sizeof(s1), "{\"key\":\"a\",\"kind\":\"switch\",\"label\":\"%s\",\"default\":true}", label25);
    R(s1, MODJ_R_LABEL_LONG);
    snprintf(s1, sizeof(s1), "{\"key\":\"a\",\"kind\":\"switch\",\"label\":{\"en\":\"ok\",\"ru\":\"%s\"},\"default\":true}", label25);
    R(s1, MODJ_R_LABEL_LONG);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":\"A\"}", MODJ_R_DEFAULT_MISSING);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":\"A\",\"default\":\"yes\"}", MODJ_R_DEFAULT_TYPE);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":\"A\",\"default\":1}", MODJ_R_DEFAULT_TYPE);
#define CH(vals, def) "{\"key\":\"c\",\"kind\":\"choice\",\"label\":\"C\",\"values\":" vals ",\"default\":" def "}"
#define V2 "[{\"value\":\"a\",\"label\":\"A\"},{\"value\":\"b\",\"label\":\"B\"}]"
    R(CH(V2, "1"), MODJ_R_DEFAULT_TYPE);
    R(CH(V2, "\"z\""), MODJ_R_DEFAULT_NOT_VALUE);
    R("{\"key\":\"c\",\"kind\":\"choice\",\"label\":\"C\",\"values\":" V2 "}", MODJ_R_DEFAULT_MISSING);
    R("{\"key\":\"c\",\"kind\":\"choice\",\"label\":\"C\",\"default\":\"a\"}", MODJ_R_VALUES_MISSING);
    R(CH("{}", "\"a\""), MODJ_R_VALUES_TYPE);
    R(CH("[{\"value\":\"a\",\"label\":\"A\"}]", "\"a\""), MODJ_R_VALUES_COUNT);
    {
        int k = snprintf(vals17, sizeof(vals17), "{\"key\":\"c\",\"kind\":\"choice\",\"label\":\"C\",\"default\":\"v0\",\"values\":[");
        for (int j = 0; j < 17; j++)
            k += snprintf(vals17 + k, sizeof(vals17) - (size_t) k, "%s{\"value\":\"v%d\",\"label\":\"V\"}", j ? "," : "", j);
        snprintf(vals17 + k, sizeof(vals17) - (size_t) k, "]}");
        R(vals17, MODJ_R_VALUES_COUNT);
    }
    R(CH("[5,{\"value\":\"b\",\"label\":\"B\"}]", "\"b\""), MODJ_R_VALUE_TYPE);
    R(CH("[{\"label\":\"A\"},{\"value\":\"b\",\"label\":\"B\"}]", "\"b\""), MODJ_R_VALUE_BAD);
    R(CH("[{\"value\":\"A\",\"label\":\"A\"},{\"value\":\"b\",\"label\":\"B\"}]", "\"b\""), MODJ_R_VALUE_BAD);
    R(CH("[{\"value\":1,\"label\":\"A\"},{\"value\":\"b\",\"label\":\"B\"}]", "\"b\""), MODJ_R_VALUE_BAD);
    R(CH("[{\"value\":\"b\",\"label\":\"A\"},{\"value\":\"b\",\"label\":\"B\"}]", "\"b\""), MODJ_R_VALUE_DUP);
    R(CH("[{\"value\":\"a\"},{\"value\":\"b\",\"label\":\"B\"}]", "\"b\""), MODJ_R_LABEL_MISSING);
    snprintf(s1, sizeof(s1), CH("[{\"value\":\"a\",\"label\":\"%s\"},{\"value\":\"b\",\"label\":\"B\"}]", "\"b\""), label25);
    R(s1, MODJ_R_LABEL_LONG);
#define NUM(fields) "{\"key\":\"n\",\"kind\":\"number\",\"label\":\"N\"," fields "}"
    R(NUM("\"max\":3,\"step\":1,\"default\":1"), MODJ_R_NUM_MISSING);
    R(NUM("\"min\":0,\"max\":3,\"default\":1"), MODJ_R_NUM_MISSING);
    R(NUM("\"min\":\"0\",\"max\":3,\"step\":1,\"default\":1"), MODJ_R_NUM_WHOLE);
    R(NUM("\"min\":0.5,\"max\":3,\"step\":1,\"default\":1"), MODJ_R_NUM_WHOLE);
    R(NUM("\"min\":0,\"max\":2000000,\"step\":100000,\"default\":0"), MODJ_R_NUM_WHOLE);
    R(NUM("\"min\":3,\"max\":3,\"step\":1,\"default\":3"), MODJ_R_NUM_ORDER);
    R(NUM("\"min\":4,\"max\":3,\"step\":1,\"default\":3"), MODJ_R_NUM_ORDER);
    R(NUM("\"min\":0,\"max\":3,\"step\":0,\"default\":1"), MODJ_R_NUM_STEP);
    R(NUM("\"min\":0,\"max\":3,\"step\":-1,\"default\":1"), MODJ_R_NUM_STEP);
    R(NUM("\"min\":0,\"max\":101,\"step\":1,\"default\":1"), MODJ_R_NUM_STEPS);
    R(NUM("\"min\":0,\"max\":10,\"step\":3,\"default\":0"), MODJ_R_NUM_GRID);
    R(NUM("\"min\":0,\"max\":3,\"step\":1"), MODJ_R_DEFAULT_MISSING);
    R(NUM("\"min\":0,\"max\":3,\"step\":1,\"default\":\"1\""), MODJ_R_DEFAULT_TYPE);
    R(NUM("\"min\":0,\"max\":3,\"step\":1,\"default\":1.5"), MODJ_R_NUM_WHOLE);
    R(NUM("\"min\":0,\"max\":3,\"step\":1,\"default\":4"), MODJ_R_DEFAULT_RANGE);
    R(NUM("\"min\":0,\"max\":10,\"step\":2,\"default\":3"), MODJ_R_DEFAULT_GRID);
    R("{\"key\":\"a\",\"kind\":\"switch\",\"label\":\"A\",\"default\":true,\"restart\":\"no\"}", MODJ_R_RESTART_TYPE);
#undef R

    /* edges that stand */
    {
        const char *edge =
            "{\"settings\":["
            NUM("\"min\":-100,\"max\":0,\"step\":1,\"default\":-100") ","
            "{\"key\":\"l\",\"kind\":\"switch\",\"default\":false,\"label\":\"\xD0\x90\xD0\x91\xD0\x92\xD0\x93\xD0\x94\xD0\x95"
            "\xD0\x81\xD0\x96\xD0\x97\xD0\x98\xD0\x99\xD0\x9A\xD0\x9B\xD0\x9C\xD0\x9D\xD0\x9E\xD0\x9F\xD0\xA0\xD0\xA1\xD0\xA2\xD0\xA3"
            "\xD0\xA4\xD0\xA5\xD0\xA6\"},"                                 /* 24 Cyrillic letters, 48 bytes */
            "{\"key\":\"x_9-z\",\"kind\":\"number\",\"label\":\"S\",\"min\":0,\"max\":1e2,\"step\":1,\"default\":100.0}"
            "]}";
        ok = modj_read_text(m, edge, strlen(edge));
        check(ok && m->nsettings == 3 && m->nissues == 0 && m->settings[0].def == -100 && m->settings[2].max == 100
              && m->settings[2].def == 100, "edges: %d settings, %d issues (%s)", m->nsettings, m->nissues,
              m->nissues ? m->issues[0].text : "-");
        modj_free(m);
    }
    /* a duplicate key: the first entry keeps it even when refused */
    {
        const char *dup = "{\"settings\":[{\"key\":\"d\",\"kind\":\"switch\",\"label\":\"A\"},"
                          "{\"key\":\"d\",\"kind\":\"switch\",\"label\":\"B\",\"default\":true}]}";
        ok = modj_read_text(m, dup, strlen(dup));
        const modj_issue_t *a = t_has(m, MODJ_REFUSED, MODJ_R_DEFAULT_MISSING), *b = t_has(m, MODJ_REFUSED, MODJ_R_KEY_DUP);
        check(ok && m->nsettings == 0 && a && a->setting == 0 && b && b->setting == 1 && !strcmp(b->key, "d"),
              "duplicate key after a refused entry");
        modj_free(m);
    }
    /* the settings as a whole */
    {
        const char *t = "{\"name\":\"X\",\"settings\":{}}";
        ok = modj_read_text(m, t, strlen(t));
        const modj_issue_t *is = t_has(m, MODJ_REFUSED, MODJ_R_SETTINGS_TYPE);
        check(ok && m->nsettings == 0 && is && is->setting == -1 && !strcmp(modj_text(m->name, "en"), "X"), "settings not an array");
        modj_free(m);
        char *t18 = malloc(4096);
        if (t18) {
            int k = snprintf(t18, 4096, "{\"settings\":[");
            for (int j = 0; j < 18; j++)
                k += snprintf(t18 + k, 4096 - (size_t) k, "%s{\"key\":\"s%d\",\"kind\":\"switch\",\"label\":\"S\",\"default\":true}",
                              j ? "," : "", j);
            snprintf(t18 + k, 4096 - (size_t) k, "]}");
            ok = modj_read_text(m, t18, strlen(t18));
            is = t_has(m, MODJ_REFUSED, MODJ_R_SETTINGS_MANY);
            check(ok && m->nsettings == 16 && is && is->setting == 16 && m->nissues == 1 && !strcmp(m->settings[15].key, "s15"),
                  "18 settings: %d kept, %d issues", m->nsettings, m->nissues);
            modj_free(m);
            free(t18);
        }
    }
    /* notes: the setting stands */
    {
        const char *t = "{\"name\":\"N\",\"homepage\":\"x\",\"settings\":["
                        "{\"key\":\"a\",\"kind\":\"switch\",\"label\":{\"en\":\"A\",\"fr\":\"B\"},\"default\":true,\"comment\":\"c\","
                        "\"values\":[]},"
                        "{\"key\":\"b\",\"kind\":\"number\",\"label\":\"B\",\"min\":0,\"max\":2,\"step\":1,\"default\":0,\"values\":5}]}";
        ok = modj_read_text(m, t, strlen(t));
        int unknown = 0, lang = 0;
        for (int j = 0; j < m->nissues; j++) {
            unknown += m->issues[j].level == MODJ_NOTE && m->issues[j].reason == MODJ_R_UNKNOWN_FIELD;
            lang += m->issues[j].level == MODJ_NOTE && m->issues[j].reason == MODJ_R_LANG_UNKNOWN;
        }
        check(ok && m->nsettings == 2 && unknown == 4 && lang == 1 && m->nissues == 5, "notes: %d settings, %d unknown, %d lang",
              m->nsettings, unknown, lang);
        modj_free(m);
    }
    /* the mod's own fields, wrong: ignored with a note, the rest stands */
    {
        const char *t = "{\"id\":5,\"name\":5,\"version\":1.2,\"author\":{\"en\":7},\"description\":\"\",\"settings\":[]}";
        ok = modj_read_text(m, t, strlen(t));
        check(ok && !m->id && !m->name.node && !m->version.node && !m->author.node && !m->description.node && m->nissues == 5
              && t_has(m, MODJ_NOTE, MODJ_R_FIELD_TYPE) && t_has(m, MODJ_NOTE, MODJ_R_TEXT_TYPE) && t_has(m, MODJ_NOTE, MODJ_R_TEXT_EMPTY),
              "wrong mod fields: %d issues", m->nissues);
        modj_free(m);
    }
    /* the whole file */
    {
        const char *t = "[1]";
        ok = modj_read_text(m, t, strlen(t));
        check(!ok && !m->ok && t_has(m, MODJ_BROKEN, MODJ_R_ROOT) && !m->doc.nodes, "root not an object");
        modj_free(m);
        /* not strict JSON: read leniently (hotfix 1.71), the strict fault named in a note */
        t = "{\"name\": \"x\",\n \"settings\": [ }";
        ok = modj_read_text(m, t, strlen(t));
        const modj_issue_t *is = t_has(m, MODJ_NOTE, MODJ_R_SYNTAX);
        check(ok && m->lenient && is && strstr(is->text, "line 2, column 16") && !t_has(m, MODJ_BROKEN, MODJ_R_SYNTAX)
              && modj_text(m->name, "en") && !strcmp(modj_text(m->name, "en"), "x"), "lenient: %s", is ? is->text : "-");
        modj_free(m);
        /* the slips of a hand (and of 1.666's org.json): each read, the mod named */
        {
            static const char *const SLIP[][2] = {
                { "{\"id\":\"a\",\"name\":\"Trailing\",}", "Trailing" },
                { "{\"id\":\"b\", // my mod\n\"name\":\"Comment\"}", "Comment" },
                { "{/* c */\"name\":\"Block\" # hash\n}", "Block" },
                { "{'id':'d','name':'Single'}", "Single" },
                { "{id:\"e\",name:\"Unquoted\"}", "Unquoted" },
                { "{\"name\":\"Dupe\",\"name\":\"Dupe2\"}", "Dupe2" },
                { "{\"name\":\"Tail\"} # end", "Tail" },
                { "{\"name\"=\"Eq\";\"version\"=>\"1\"}", "Eq" },
                { "{\"name\":\"Comma\",,\"version\":\"1\",}", "Comma" },
                { "{\"name\":\"Bare words\",\"version\":1.0,\"author\":me}", "Bare words" },
                { "{\"name\":\"Esc\\q\",}", "Escq" },
                { "{\"name\":\"Open\"", "Open" },
                { "{\"name\":\"Bad \xFF byte\"}", "Bad \xEF\xBF\xBD" " byte" },
            };
            for (size_t k = 0; k < sizeof(SLIP) / sizeof(SLIP[0]); k++) {
                ok = modj_read_text(m, SLIP[k][0], strlen(SLIP[k][0]));
                const char *nm = modj_text(m->name, "en");
                check(ok && m->lenient && nm && !strcmp(nm, SLIP[k][1]),
                      "slip %u read: %s", (unsigned) k, nm ? nm : "-");
                modj_free(m);
            }
            /* not readable at all: the name taken from the text, as 1.666 did; broken, never nameless */
            t = "name: \"Garbage\" }}} {{{";
            ok = modj_read_text(m, t, strlen(t));
            check(modj_text(m->name, "en") && !strcmp(modj_text(m->name, "en"), "Garbage"), "garbage: the name kept (%s)",
                  modj_text(m->name, "en") ? modj_text(m->name, "en") : "-");
            modj_free(m);
            /* a setting broken: switched off alone, the mod read */
            t = "{\"name\":\"S\",\"settings\":[{\"key\":\"BAD KEY\",\"kind\":\"switch\"},]}";
            ok = modj_read_text(m, t, strlen(t));
            check(ok && m->nsettings == 0 && t_has(m, MODJ_REFUSED, MODJ_R_KEY_BAD) && modj_text(m->name, "en"),
                  "a broken setting off, the mod read");
            modj_free(m);
        }
        size_t n = MODJ_FILE_MAX + 1;
        char *big = malloc(n);
        if (big) {
            memset(big, ' ', n);
            big[0] = '{'; big[n - 1] = '}';
            ok = modj_read_text(m, big, n);
            check(!ok && t_has(m, MODJ_BROKEN, MODJ_R_FILE_BIG), "64 KB + 1");
            modj_free(m);
            ok = modj_read_text(m, big + 1, n - 1);            /* "   ...}" - exactly the limit, and not JSON */
            check(!ok && t_has(m, MODJ_BROKEN, MODJ_R_SYNTAX), "64 KB exactly is parsed");
            modj_free(m);
            free(big);
        }
        ok = modj_read_file(m, "/nonexistent-dir-of-the-json-selftest/mod.json");
        check(!ok && m->open_errno == ENOENT && t_has(m, MODJ_BROKEN, MODJ_R_FILE_OPEN), "no file: errno %d", m->open_errno);
        modj_free(m);
    }
    /* the issue list is bounded */
    {
        char *t = malloc(8192);
        if (t) {
            int k = snprintf(t, 8192, "{\"settings\":[");
            for (int j = 0; j < 16; j++)
                k += snprintf(t + k, 8192 - (size_t) k, "%s{\"key\":\"s%d\",\"kind\":\"switch\",\"label\":\"S\",\"default\":true,"
                              "\"u1\":1,\"u2\":2}", j ? "," : "", j);
            snprintf(t + k, 8192 - (size_t) k, "]}");
            ok = modj_read_text(m, t, strlen(t));
            check(ok && m->nsettings == 16 && m->nissues == MODJ_ISSUES_MAX && m->issues_lost == 32 - MODJ_ISSUES_MAX,
                  "issue overflow: %d kept, %d lost", m->nissues, m->issues_lost);
            modj_free(m);
            free(t);
        }
    }
    /* cutting a text at a character boundary */
    {
        char out[8];
        size_t n = modj_copy(out, 4, "\xD0\x9F\xD1\x80\xD0\xB8");        /* Russian "Pri" (U+041F U+0440 U+0438): 2 bytes fit, the second letter does not */
        check(n == 2 && !strcmp(out, "\xD0\x9F"), "modj_copy: %u bytes", (unsigned) n);
        n = modj_copy(out, 8, "abc");
        check(n == 3 && !strcmp(out, "abc"), "modj_copy short");
        n = modj_copy(out, 5, "ab\xF0\x9F\x98\x80");
        check(n == 2 && !strcmp(out, "ab"), "modj_copy before a 4-byte letter");
        n = modj_copy(out, 1, "abc");
        check(n == 0 && !out[0], "modj_copy into one byte");
        memset(out, 'x', sizeof(out));
        n = modj_copy(out, 4, "abcd");                                    /* exactly room bytes: one must go for the NUL */
        check(n == 3 && !strcmp(out, "abc") && out[4] == 'x', "modj_copy of exactly room bytes");
    }
    /* every reason has its word */
    {
        int named = 1;
        for (int r = 0; r < MODJ_R_COUNT; r++) named &= REASON[r] && REASON[r][0] && strcmp(modj_reason_code((modj_reason_t) r), "?");
        check(named && !strcmp(modj_reason_code(MODJ_R_LANG_UNKNOWN), "lang_unknown")
              && !strcmp(modj_reason_code(MODJ_R_NUM_GRID), "num_grid"), "reason words");
    }
    free(m);
#undef CH
#undef V2
#undef NUM
}

int shim_json_selftest(void) {
    t_checks = t_failed = 0;
    t_json();
    t_modj();
    if (t_failed) TLOG("json: selftest FAILED %d of %d checks", t_failed, t_checks);
    else TLOG("json: selftest ok, %d checks", t_checks);
    return t_failed;
}
#endif  /* ISAAC_DIAG */
