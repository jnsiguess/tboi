/*
 * DATA TABLES BUILT FROM THE GAME'S FILE AND THE MODS (plan 30.09, 4.9 M3,
 * 4.10 part 3; the owner 01.10: "a whole file is a REPLACEMENT; replacement +
 * append -> the append lies on top of the replaced one").
 *
 * THE SPEC IS tools/content_sim.py. Its header holds the order of the build,
 * the patch/ syntax and the serialization rules S1-S9; this file is that
 * model in C, and tools/contentdesk/ runs both over the model's --verify
 * corpus and --test cases and compares the bytes and the report lines. The key
 * table is not kept here by hand: content_table.h is generated from the
 * model's T (tools/contentdesk/gen_table.py).
 *
 * For one file F (repentance-resources/data/<name>.xml, or stringtable.sta):
 *   1. BASE: the strongest enabled mod's whole F (resources/F, "legacy"), else
 *      the game's. A legacy F that does not read as XML, or has a record past
 *      its loader's table (the bounds of the key table, then the port's
 *      loader-exact rules of usermods.c), is refused: the next weaker one,
 *      then the game's. Every other legacy F gets a "not used" line.
 *   2. COMPLETION: a legacy base gets the game's records it lacks (S7).
 *   3. CONTENT: content/entities2.xml of every mod, strongest first (S8).
 *   4. PATCH: patch/F of every mod, weakest first, each on top - wherever the
 *      replacing mod stands in the list (4.10.3).
 *   5. PORT: the port's own stringtable keys, filled where empty (M8).
 * Nothing touches F: the output is the game's file, byte for byte.
 *
 * Where the reader of this file differs from the model on purpose: the model
 * is Python, its strings are Unicode - "\d" and str.lower() see every digit and
 * case of Unicode, this code ASCII's only (a record key written in Arabic
 * digits is not one here). Everything else - UTF-8 decoding with U+FFFD,
 * Latin-1 tag names, Unicode whitespace in strip()/split() - is reproduced.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "content.h"

/* ------------------------------------------------------------ buffers, arena */

void cbuf_free(cbuf_t *b) { free(b->s); b->s = NULL; b->n = b->cap = 0; }

static void cb_put(cbuf_t *b, const void *p, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *s = (char *) realloc(b->s, cap);
        if (!s) return;
        b->s = s;
        b->cap = cap;
    }
    if (n) memcpy(b->s + b->n, p, n);
    b->n += n;
    b->s[b->n] = 0;
}
static void cb_puts(cbuf_t *b, const char *s) { cb_put(b, s, strlen(s)); }
static void cb_vprintf(cbuf_t *b, const char *fmt, va_list ap) {
    char tmp[512];
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, aq);
    va_end(aq);
    if (n < 0) return;
    if ((size_t) n < sizeof(tmp)) { cb_put(b, tmp, (size_t) n); return; }
    char *big = (char *) malloc((size_t) n + 1);
    if (!big) return;
    vsnprintf(big, (size_t) n + 1, fmt, ap);
    cb_put(b, big, (size_t) n);
    free(big);
}
static void cb_printf(cbuf_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    cb_vprintf(b, fmt, ap);
    va_end(ap);
}

typedef struct ablock { struct ablock *next; size_t used, cap; } ablock_t;
typedef struct { ablock_t *head; } arena_t;

static void *a_alloc(arena_t *a, size_t n) {
    n = (n + 15) & ~(size_t) 15;
    ablock_t *h = a->head;
    if (!h || h->used + n > h->cap) {
        size_t cap = n > 65536 ? n : 65536;
        h = (ablock_t *) malloc(sizeof(ablock_t) + 16 + cap);
        if (!h) { fprintf(stderr, "content: out of memory\n"); abort(); }
        h->used = 0;
        h->cap = cap;
        h->next = a->head;
        a->head = h;
    }
    char *p = (char *) (h + 1) + 16 + h->used;
    h->used += n;
    return p;
}
static void a_free(arena_t *a) {
    for (ablock_t *h = a->head, *n; h; h = n) { n = h->next; free(h); }
    a->head = NULL;
}

/* a Python str, as UTF-8 bytes (always NUL-terminated); s NULL is None */
typedef struct { const char *s; int n; } str_t;
static const str_t NONE = { NULL, 0 };

static str_t a_str(arena_t *a, const char *p, int n) {
    char *s = (char *) a_alloc(a, (size_t) n + 1);
    if (n) memcpy(s, p, (size_t) n);
    s[n] = 0;
    return (str_t) { s, n };
}
static str_t a_cstr(arena_t *a, const char *s) { return a_str(a, s, (int) strlen(s)); }
static int s_eq(str_t a, str_t b) { return a.s && b.s && a.n == b.n && !memcmp(a.s, b.s, (size_t) a.n); }
static int s_is(str_t a, const char *t) { return a.s && (size_t) a.n == strlen(t) && !memcmp(a.s, t, (size_t) a.n); }
static const char *PY(str_t s) { return s.s ? s.s : "None"; }

/* bytes.decode("utf-8", "replace"): every maximal invalid subpart becomes U+FFFD (CPython's rule) */
static str_t u8(arena_t *a, const char *p, int n) {
    int ascii = 1;
    for (int i = 0; i < n && ascii; i++) ascii = (unsigned char) p[i] < 0x80;
    if (ascii) return a_str(a, p, n);
    char *o = (char *) a_alloc(a, (size_t) n * 3 + 1);
    int k = 0;
    const unsigned char *b = (const unsigned char *) p;
    for (int i = 0; i < n; ) {
        unsigned c = b[i];
        if (c < 0x80) { o[k++] = (char) c; i++; continue; }
        int need = 0;
        unsigned lo = 0x80, hi = 0xBF;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c == 0xE0) { need = 2; lo = 0xA0; }
        else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) need = 2;
        else if (c == 0xED) { need = 2; hi = 0x9F; }
        else if (c == 0xF0) { need = 3; lo = 0x90; }
        else if (c >= 0xF1 && c <= 0xF3) need = 3;
        else if (c == 0xF4) { need = 3; hi = 0x8F; }
        int got = 0;
        while (need && got < need && i + 1 + got < n) {
            unsigned d = b[i + 1 + got];
            unsigned l = got ? 0x80 : lo, h = got ? 0xBF : hi;
            if (d < l || d > h) break;
            got++;
        }
        if (need && got == need) { memcpy(o + k, b + i, (size_t) need + 1); k += need + 1; i += need + 1; }
        else { o[k++] = (char) 0xEF; o[k++] = (char) 0xBF; o[k++] = (char) 0xBD; i += 1 + got; }
    }
    o[k] = 0;
    return (str_t) { o, k };
}

/* bytes.decode("latin-1") */
static str_t l1(arena_t *a, const char *p, int n) {
    char *o = (char *) a_alloc(a, (size_t) n * 2 + 1);
    int k = 0;
    for (int i = 0; i < n; i++) {
        unsigned c = (unsigned char) p[i];
        if (c < 0x80) o[k++] = (char) c;
        else { o[k++] = (char) (0xC0 | c >> 6); o[k++] = (char) (0x80 | (c & 0x3F)); }
    }
    o[k] = 0;
    return (str_t) { o, k };
}

/* str.encode("latin-1") of a str made by l1() */
static void put_l1(cbuf_t *b, str_t s) {
    for (int i = 0; i < s.n; i++) {
        unsigned c = (unsigned char) s.s[i];
        if (c >= 0xC0 && i + 1 < s.n) { char x = (char) ((c & 0x03) << 6 | ((unsigned char) s.s[i + 1] & 0x3F)); cb_put(b, &x, 1); i++; }
        else cb_put(b, s.s + i, 1);
    }
}

/* str.isspace() of the character at p: its length in bytes, 0 when not a space */
static int uspace(const char *p, const char *e) {
    unsigned c = (unsigned char) *p;
    if ((c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20)) return 1;
    if (c < 0xC2 || e - p < 2) return 0;
    unsigned c1 = (unsigned char) p[1];
    if (c == 0xC2) return c1 == 0x85 || c1 == 0xA0 ? 2 : 0;
    if (e - p < 3) return 0;
    unsigned c2 = (unsigned char) p[2];
    if (c == 0xE1 && c1 == 0x9A && c2 == 0x80) return 3;
    if (c == 0xE2 && c1 == 0x80 && (c2 <= 0x8A || c2 == 0xA8 || c2 == 0xA9 || c2 == 0xAF)) return 3;
    if (c == 0xE2 && c1 == 0x81 && c2 == 0x9F) return 3;
    if (c == 0xE3 && c1 == 0x80 && c2 == 0x80) return 3;
    return 0;
}
/* the length of the UTF-8 character at p */
static int uchar_len(const char *p, const char *e) {
    unsigned c = (unsigned char) *p;
    int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    return n > e - p ? (int) (e - p) : n;
}
/* the last character starting before e (UTF-8) */
static const char *uprev(const char *s, const char *e) {
    const char *p = e - 1;
    while (p > s && ((unsigned char) *p & 0xC0) == 0x80) p--;
    return p;
}

/* str.strip() */
static str_t ustrip(str_t v) {
    if (!v.s) return v;
    const char *s = v.s, *e = v.s + v.n;
    int k;
    while (s < e && (k = uspace(s, e))) s += k;
    while (e > s) { const char *p = uprev(s, e); if (uspace(p, e) != e - p) break; e = p; }
    return (str_t) { s, (int) (e - s) };
}

/* bytes.strip() */
static int bstrip_empty(const char *p, int n) {
    for (int i = 0; i < n; i++) if (!strchr(" \t\n\r\x0b\x0c", p[i]) || !p[i]) return 0;
    return 1;
}

/* re.fullmatch(r"[+-]?\d+", s) (ASCII digits) */
static int is_int(str_t s) {
    int i = 0;
    if (s.n && (s.s[0] == '+' || s.s[0] == '-')) i = 1;
    if (i >= s.n) return 0;
    for (; i < s.n; i++) if (s.s[i] < '0' || s.s[i] > '9') return 0;
    return 1;
}

/* str(int(s)) of an is_int string; abs: str(abs(int(s))) */
static str_t int_text(arena_t *a, str_t s, int absolute) {
    int i = 0, neg = 0;
    if (s.s[0] == '+' || s.s[0] == '-') { neg = s.s[0] == '-'; i = 1; }
    while (i < s.n - 1 && s.s[i] == '0') i++;
    int zero = s.n - i == 1 && s.s[i] == '0';
    if (!neg || absolute || zero) return a_str(a, s.s + i, s.n - i);
    char *o = (char *) a_alloc(a, (size_t) (s.n - i) + 2);
    o[0] = '-';
    memcpy(o + 1, s.s + i, (size_t) (s.n - i));
    o[s.n - i + 1] = 0;
    return (str_t) { o, s.n - i + 1 };
}

/* _norm_key */
static str_t norm_key(arena_t *a, str_t v) {
    if (!v.s) return v;
    str_t s = ustrip(v);
    if (is_int(s)) return int_text(a, s, 0);
    return a_str(a, s.s, s.n);
}

/* int(re.match(r"[+-]?\d+", s)) or 0, saturated (only compared with small bounds) */
static long long lead_int(str_t s) {
    int i = 0, neg = 0;
    if (s.n && (s.s[0] == '+' || s.s[0] == '-')) { neg = s.s[0] == '-'; i = 1; }
    if (i >= s.n || s.s[i] < '0' || s.s[i] > '9') return 0;
    long long v = 0;
    for (; i < s.n && s.s[i] >= '0' && s.s[i] <= '9'; i++) v = v > 100000000000000000LL ? v : v * 10 + (s.s[i] - '0');
    return neg ? -v : v;
}

/* ------------------------------------------------------------ a string map (open addressing) */

typedef struct { str_t k; void *v; } sent_t;
typedef struct { sent_t *t; int cap, n; } smap_t;

static unsigned s_hash(const char *s, int n) {
    unsigned h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ (unsigned char) s[i]) * 16777619u;
    return h;
}

static void **smap_at(arena_t *a, smap_t *m, str_t k, int create) {
    if (!k.s) k = (str_t) { "\x01None", 5 };
    if (create && (m->n + 1) * 2 > m->cap) {
        int cap = m->cap ? m->cap * 2 : 64;
        sent_t *t = (sent_t *) a_alloc(a, sizeof(sent_t) * (size_t) cap);
        memset(t, 0, sizeof(sent_t) * (size_t) cap);
        for (int i = 0; i < m->cap; i++) {
            if (!m->t[i].k.s) continue;
            unsigned h = s_hash(m->t[i].k.s, m->t[i].k.n) & (unsigned) (cap - 1);
            while (t[h].k.s) h = (h + 1) & (unsigned) (cap - 1);
            t[h] = m->t[i];
        }
        m->t = t;
        m->cap = cap;
    }
    if (!m->cap) return NULL;
    unsigned h = s_hash(k.s, k.n) & (unsigned) (m->cap - 1);
    while (m->t[h].k.s) {
        if (s_eq(m->t[h].k, k)) return &m->t[h].v;
        h = (h + 1) & (unsigned) (m->cap - 1);
    }
    if (!create) return NULL;
    m->t[h].k = a_str(a, k.s, k.n);
    m->t[h].v = NULL;
    m->n++;
    return &m->t[h].v;
}
static int smap_has(smap_t *m, str_t k) { return smap_at(NULL, m, k, 0) != NULL; }
static void smap_add(arena_t *a, smap_t *m, str_t k) { void **v = smap_at(a, m, k, 1); if (v && !*v) *v = (void *) 1; }

/* a growing list of strings */
typedef struct { str_t *v; int n, cap; } slist_t;
static void sl_add(arena_t *a, slist_t *l, str_t s) {
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        str_t *v = (str_t *) a_alloc(a, sizeof(str_t) * (size_t) cap);
        if (l->n) memcpy(v, l->v, sizeof(str_t) * (size_t) l->n);
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = s;
}

/* ------------------------------------------------------------ the reader (xml_parse of usermods.c) */

typedef struct {
    int start, tag_end, ns, ne, as, ae, inner_end, stop;
    int depth, parent, first, last, next;
} el_t;

typedef struct {
    const char *b;
    int n;
    el_t *v;
    int nv, cap;
    int rfirst, rlast;
    char err[96];
} doc_t;

static int cspace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static int b_find(const char *b, int n, int from, const char *pat) {
    int k = (int) strlen(pat);
    for (int i = from; i + k <= n; i++) if (b[i] == pat[0] && !memcmp(b + i, pat, (size_t) k)) return i;
    return -1;
}

static int tag_end(const char *b, int p, int lim) {
    char q = 0;
    for (p++; p < lim; p++) {
        char c = b[p];
        if (q) { if (c == q) q = 0; }
        else if (c == '"' || c == '\'') q = c;
        else if (c == '>') return p;
    }
    return -1;
}

static void doc_free(doc_t *d) { free(d->v); d->v = NULL; d->nv = d->cap = 0; }

/* every element in document order; 0, or -1 with d->err (the model's ParseError text) */
static int doc_parse(doc_t *d, const char *b, long len) {
    int lim = (int) len, stack[64], sp = 0, p = 0;
    memset(d, 0, sizeof(*d));
    d->b = b;
    d->n = lim;
    d->rfirst = d->rlast = -1;
    while (p < lim) {
        const char *ltp = (const char *) memchr(b + p, '<', (size_t) (lim - p));
        if (!ltp) break;
        int lt = (int) (ltp - b), left = lim - lt, e;
        if (left >= 4 && !memcmp(b + lt, "<!--", 4)) {
            if ((e = b_find(b, lim, lt + 4, "-->")) < 0) { snprintf(d->err, sizeof(d->err), "comment not closed at %d", lt); return -1; }
            p = e + 3; continue;
        }
        if (left >= 9 && !memcmp(b + lt, "<![CDATA[", 9)) {
            if ((e = b_find(b, lim, lt + 9, "]]>")) < 0) { snprintf(d->err, sizeof(d->err), "CDATA not closed at %d", lt); return -1; }
            p = e + 3; continue;
        }
        if (left >= 2 && b[lt + 1] == '?') {
            if ((e = b_find(b, lim, lt + 2, "?>")) < 0) { snprintf(d->err, sizeof(d->err), "<? not closed at %d", lt); return -1; }
            p = e + 2; continue;
        }
        if (left >= 2 && b[lt + 1] == '!') {
            if ((e = tag_end(b, lt, lim)) < 0) { snprintf(d->err, sizeof(d->err), "<! not closed at %d", lt); return -1; }
            p = e + 1; continue;
        }
        if (left >= 2 && b[lt + 1] == '/') {
            const char *gt = (const char *) memchr(b + lt, '>', (size_t) left);
            if (!gt) { snprintf(d->err, sizeof(d->err), "end tag not closed at %d", lt); return -1; }
            e = (int) (gt - b);
            if (sp) { el_t *x = &d->v[stack[--sp]]; x->inner_end = lt; x->stop = e + 1; }
            p = e + 1; continue;
        }
        int q = lt + 1;
        while (q < lim && !cspace(b[q]) && b[q] != '/' && b[q] != '>') q++;
        if (q == lt + 1) { p = lt + 1; continue; }
        int gt = tag_end(b, lt, lim);
        if (gt < 0) { snprintf(d->err, sizeof(d->err), "start tag not closed at %d", lt); return -1; }
        if (d->nv == d->cap) {
            int cap = d->cap ? d->cap * 2 : 1024;
            el_t *v = (el_t *) realloc(d->v, sizeof(el_t) * (size_t) cap);
            if (!v) { snprintf(d->err, sizeof(d->err), "out of memory"); return -1; }
            d->v = v;
            d->cap = cap;
        }
        int idx = d->nv++;
        el_t *x = &d->v[idx];
        int self = b[gt - 1] == '/' && gt - 1 >= q;
        x->start = lt; x->tag_end = gt; x->ns = lt + 1; x->ne = q; x->as = q;
        x->ae = self ? gt - 1 : gt;
        x->depth = sp;
        x->parent = sp ? stack[sp - 1] : -1;
        x->first = x->last = x->next = -1;
        if (x->parent >= 0) {
            el_t *pa = &d->v[x->parent];
            if (pa->last >= 0) d->v[pa->last].next = idx; else pa->first = idx;
            pa->last = idx;
        } else {
            if (d->rlast >= 0) d->v[d->rlast].next = idx; else d->rfirst = idx;
            d->rlast = idx;
        }
        if (self) x->inner_end = x->stop = gt + 1;
        else {
            if (sp == 64) { snprintf(d->err, sizeof(d->err), "nesting deeper than 64"); return -1; }
            stack[sp++] = idx;
            x->inner_end = x->stop = -1;
        }
        p = gt + 1;
    }
    if (sp) { snprintf(d->err, sizeof(d->err), "%d elements not closed", sp); return -1; }
    return 0;
}

/* the element's tag is exactly t (a Latin-1 str of the bytes: bytes compare) */
static int tag_is(const doc_t *d, const el_t *x, const char *t) {
    size_t n = strlen(t);
    return (size_t) (x->ne - x->ns) == n && !memcmp(d->b + x->ns, t, n);
}
static int tag_eq(const doc_t *a, const el_t *x, const doc_t *b, const el_t *y) {
    return x->ne - x->ns == y->ne - y->ns && !memcmp(a->b + x->ns, b->b + y->ns, (size_t) (x->ne - x->ns));
}
static int tag_in(const doc_t *d, const el_t *x, const char *const *tags) {
    for (; *tags; tags++) if (tag_is(d, x, *tags)) return 1;
    return 0;
}
static str_t name_of(arena_t *a, const doc_t *d, const el_t *x) { return l1(a, d->b + x->ns, x->ne - x->ns); }

typedef struct { str_t name; int ns, vs, ve; char q; } at_t;
typedef struct { at_t *v; int n; } ats_t;

/* the attributes of a start tag, stopping where xml_attr stops */
static ats_t attrs_of(arena_t *a, const doc_t *d, const el_t *x) {
    ats_t r = { NULL, 0 };
    int cap = 0, p = x->as, end = x->ae;
    const char *b = d->b;
    for (;;) {
        while (p < end && cspace(b[p])) p++;
        if (p >= end) break;
        int ns = p;
        while (p < end && b[p] != '=' && !cspace(b[p])) p++;
        int ne = p;
        while (p < end && cspace(b[p])) p++;
        if (p >= end || b[p] != '=') break;
        p++;
        while (p < end && cspace(b[p])) p++;
        if (p >= end || (b[p] != '"' && b[p] != '\'')) break;
        char q = b[p++];
        int vs = p;
        while (p < end && b[p] != q) p++;
        if (p >= end) break;
        if (r.n == cap) {
            cap = cap ? cap * 2 : 16;
            at_t *v = (at_t *) a_alloc(a, sizeof(at_t) * (size_t) cap);
            if (r.n) memcpy(v, r.v, sizeof(at_t) * (size_t) r.n);
            r.v = v;
        }
        at_t *t = &r.v[r.n++];
        t->name = l1(a, b + ns, ne - ns);
        t->ns = ns; t->vs = vs; t->ve = p; t->q = q;
        p++;
    }
    return r;
}

/* get (the first, as xml_get) / get_last (the game's loaders: the last one sticks) */
static str_t get_attr(arena_t *a, const doc_t *d, const el_t *x, const char *attr, int last) {
    ats_t at = attrs_of(a, d, x);
    const at_t *hit = NULL;
    for (int i = 0; i < at.n; i++) if (s_is(at.v[i].name, attr)) { hit = &at.v[i]; if (!last) break; }
    return hit ? u8(a, d->b + hit->vs, hit->ve - hit->vs) : NONE;
}

/* ------------------------------------------------------------ edits (ed_apply) */

typedef struct { int at, end, seq; const char *text; int tn; } edit_t;
typedef struct { edit_t *v; int n, cap; int bad; } edits_t;

static void ed_add(arena_t *a, edits_t *ed, int at, int end, const char *text, int tn) {
    if (ed->n == ed->cap) {
        int cap = ed->cap ? ed->cap * 2 : 64;
        edit_t *v = (edit_t *) a_alloc(a, sizeof(edit_t) * (size_t) cap);
        if (ed->n) memcpy(v, ed->v, sizeof(edit_t) * (size_t) ed->n);
        ed->v = v;
        ed->cap = cap;
    }
    char *t = (char *) a_alloc(a, (size_t) tn + 1);
    if (tn) memcpy(t, text, (size_t) tn);
    ed->v[ed->n] = (edit_t) { at, end, ed->n, t, tn };
    ed->n++;
}
static void ed_addb(arena_t *a, edits_t *ed, int at, int end, const cbuf_t *b) { ed_add(a, ed, at, end, b->s ? b->s : "", (int) b->n); }

static int ed_cmp(const void *x, const void *y) {
    const edit_t *a = (const edit_t *) x, *b = (const edit_t *) y;
    return a->at != b->at ? (a->at < b->at ? -1 : 1) : a->seq - b->seq;
}

/* the edits applied to b; an edit that overlaps the one before is dropped (the model stops there) */
static void ed_apply(edits_t *ed, const char *b, int n, cbuf_t *out) {
    qsort(ed->v, (size_t) ed->n, sizeof(edit_t), ed_cmp);
    int at = 0;
    out->n = 0;
    cb_put(out, "", 0);
    for (int i = 0; i < ed->n; i++) {
        const edit_t *e = &ed->v[i];
        if (e->at < at) { ed->bad++; continue; }
        cb_put(out, b + at, (size_t) (e->at - at));
        cb_put(out, e->text, (size_t) e->tn);
        at = e->end;
    }
    cb_put(out, b + at, (size_t) (n - at));
}

static const char *newline_of(const char *b, int n) {
    const char *lf = (const char *) memchr(b, '\n', (size_t) n);
    return lf && lf > b && lf[-1] == '\r' ? "\r\n" : "\n";
}

/* spaces/tabs from the line start to pos; -1 when pos is not first on its line (else the indent's start) */
static int indent_of(const char *b, int pos) {
    int i = pos;
    while (i > 0 && (b[i - 1] == ' ' || b[i - 1] == '\t')) i--;
    return i == 0 || b[i - 1] == '\n' || b[i - 1] == '\r' ? i : -1;
}

/* the CR/LF (or the end) that ends pos's line when only spaces, tabs and comments come between; -1 */
static int eol(const char *b, int n, int pos) {
    int i = pos;
    while (i < n) {
        char c = b[i];
        if (c == ' ' || c == '\t') i++;
        else if (n - i >= 4 && !memcmp(b + i, "<!--", 4)) {
            int e = b_find(b, n, i + 4, "-->");
            if (e < 0) return -1;
            i = e + 3;
        } else if (c == '\n' || c == '\r') return i;
        else return -1;
    }
    return i;
}
static int line_end_after(const char *b, int n, int pos) { int e = eol(b, n, pos); return e < 0 ? pos : e; }

/* S1: the value and the quote to write it in */
static char quote_value(arena_t *a, const char *raw, int rn, char q, str_t *val) {
    char other = q == '"' ? '\'' : '"';
    if (!memchr(raw, q, (size_t) rn)) { *val = (str_t) { raw, rn }; return q; }
    if (!memchr(raw, other, (size_t) rn)) { *val = (str_t) { raw, rn }; return other; }
    const char *ent = q == '"' ? "&quot;" : "&apos;";
    cbuf_t t = { 0 };
    for (int i = 0; i < rn; i++) if (raw[i] == q) cb_puts(&t, ent); else cb_put(&t, raw + i, 1);
    *val = a_str(a, t.s, (int) t.n);
    cbuf_free(&t);
    return q;
}

/* ------------------------------------------------------------ the key table (generated) */

typedef struct {
    const char *attr;
    long long lo, hi;
    int has_lo, has_hi;
    char kind;                              /* 'd' danger, 'i' ignored (the loader skips), 'a' ascii */
    char when;                              /* 0 always, 'a' when when_s's value is one of when_vals, 'p' prefix when_s */
    const char *when_s;
    const char *const *when_vals;
} cbound_t;

typedef struct crec {
    const char *const *tags;                /* tags[0] is the class */
    const char *const *key;
    const char *keydef[3];                  /* a key attribute's default, NULL: required */
    int nkey;
    const char *const *attrs;               /* what the loader reads */
    const char *const *lists;
    const char *lkind;                      /* per list: 'm' multiset, 's' set */
    const char *const *refs;
    const char *const *rkind;               /* per ref: collectible trinket card pill costume */
    const struct crec *kids;
    const cbound_t *bounds;
    int add, remove;
} crec_t;

typedef struct { const char *name; const char *const *rattrs; const crec_t *recs; } croot_t;
typedef struct { const char *name; unsigned loader; const croot_t *roots; int fixed, read, content; } ctable_t;

#include "content_table.h"

#define STRINGTABLE "stringtable.sta"

static const ctable_t *table_of(const char *fname) {
    for (const ctable_t *t = CT_TABLES; t->name; t++) if (!strcmp(t->name, fname)) return t;
    return NULL;
}

int content_known(const char *fname) { return !strcmp(fname, STRINGTABLE) || table_of(fname) != NULL; }
const char *content_port_stringtable(long *len) { *len = CT_PORT_STRINGTABLE_LEN; return CT_PORT_STRINGTABLE; }

static int sl_in(const char *const *list, str_t s) {
    if (!list || !s.s) return 0;
    for (; *list; list++) if (s_is(s, *list)) return 1;
    return 0;
}
static int sl_index(const char *const *list, str_t s) {
    if (!list || !s.s) return -1;
    for (int i = 0; list[i]; i++) if (s_is(s, list[i])) return i;
    return -1;
}

static const croot_t *root_of(const ctable_t *t, const doc_t *d, const el_t *x) {
    for (const croot_t *r = t ? t->roots : NULL; r && r->name; r++) if (tag_is(d, x, r->name)) return r;
    return NULL;
}
static const crec_t *rec_in(const crec_t *recs, const doc_t *d, const el_t *x) {
    for (const crec_t *r = recs; r && r->tags; r++) if (tag_in(d, x, r->tags)) return r;
    return NULL;
}
/* rec_of(table, root name, tag): the root named by an element's tag */
static const crec_t *rec_of(const ctable_t *t, const doc_t *rd, const el_t *rx, const doc_t *d, const el_t *x) {
    const croot_t *r = rx ? root_of(t, rd, rx) : NULL;
    return r ? rec_in(r->recs, d, x) : NULL;
}

/* ------------------------------------------------------------ the build's context */

typedef struct {
    arena_t A;
    const char *fname;
    const ctable_t *table;
    const cgame_t *game;
    const char *g; int gn;                  /* the game's F */
    cbuf_t *lines;
    int ids_done;
    smap_t ids[5];                          /* collectible trinket card pill costume */
    smap_t seen;                            /* "<rec class>\x01<attr>": what the game's file uses */
    smap_t seen_done;
    doc_t gdoc; int gdoc_done, gdoc_bad;
} bctx_t;

static void say(bctx_t *c, const char *fmt, ...) {
    cb_printf(c->lines, "%s: ", c->fname);
    va_list ap;
    va_start(ap, fmt);
    cb_vprintf(c->lines, fmt, ap);
    va_end(ap);
    cb_puts(c->lines, "\n");
}

static const char *const KINDS[5] = { "collectible", "trinket", "card", "pill", "costume" };
static int kind_index(const char *k) { for (int i = 0; i < 5; i++) if (!strcmp(KINDS[i], k)) return i; return -1; }

/* Game.ids(): the game's collectibles, trinkets, cards, pills, costumes by id */
static void game_ids(bctx_t *c) {
    if (c->ids_done) return;
    c->ids_done = 1;
    static const char *const FILES[3] = { "items.xml", "pocketitems.xml", "costumes2.xml" };
    for (int f = 0; f < 3; f++) {
        long n = 0;
        const char *b = c->game->file(c->game->ctx, FILES[f], &n);
        if (!b) continue;
        doc_t d;
        if (doc_parse(&d, b, n)) { doc_free(&d); continue; }
        for (int i = 0; i < d.nv; i++) {
            const el_t *x = &d.v[i];
            if (x->depth != 1) continue;
            str_t id = norm_key(&c->A, get_attr(&c->A, &d, x, "id", 1));
            if (!id.s) continue;
            int k = tag_is(&d, x, "passive") || tag_is(&d, x, "active") || tag_is(&d, x, "familiar") ? 0
                  : tag_is(&d, x, "trinket") ? 1 : tag_is(&d, x, "card") || tag_is(&d, x, "rune") ? 2
                  : tag_is(&d, x, "pilleffect") ? 3 : tag_is(&d, x, "costume") ? 4 : -1;
            if (k >= 0) smap_add(&c->A, &c->ids[k], id);
        }
        doc_free(&d);
    }
}

static int ref_ok(bctx_t *c, const char *kind, str_t value) {
    str_t v = ustrip(value);
    if (!is_int(v)) return 0;
    str_t n = int_text(&c->A, v, 1);
    game_ids(c);
    int k = kind_index(kind);
    if (k == 3 && n.n <= 2 && atoi(n.s) < 14) return 1;
    return k >= 0 && smap_has(&c->ids[k], n);
}

/* attrs_seen: the attribute names the game's own file uses on this record class (any depth) */
static int attr_seen(bctx_t *c, const crec_t *r, str_t a) {
    str_t cls = { r->tags[0], (int) strlen(r->tags[0]) };
    if (!smap_has(&c->seen_done, cls)) {
        smap_add(&c->A, &c->seen_done, cls);
        doc_t d = { 0 };
        if (c->g && !doc_parse(&d, c->g, c->gn)) {
            for (int i = 0; i < d.nv; i++) {
                if (!tag_in(&d, &d.v[i], r->tags)) continue;
                ats_t at = attrs_of(&c->A, &d, &d.v[i]);
                for (int j = 0; j < at.n; j++) {
                    cbuf_t k = { 0 };
                    cb_puts(&k, r->tags[0]);
                    cb_put(&k, "\x01", 1);
                    cb_put(&k, at.v[j].name.s, (size_t) at.v[j].name.n);
                    smap_add(&c->A, &c->seen, (str_t) { k.s, (int) k.n });
                    cbuf_free(&k);
                }
            }
        }
        doc_free(&d);
    }
    cbuf_t k = { 0 };
    cb_puts(&k, r->tags[0]);
    cb_put(&k, "\x01", 1);
    cb_put(&k, a.s, (size_t) a.n);
    int has = smap_has(&c->seen, (str_t) { k.s, (int) k.n });
    cbuf_free(&k);
    return has;
}

static const doc_t *game_doc(bctx_t *c) {
    if (!c->gdoc_done) { c->gdoc_done = 1; c->gdoc_bad = doc_parse(&c->gdoc, c->g, c->gn); }
    return c->gdoc_bad ? NULL : &c->gdoc;
}

/* key_of: "cls[k1.k2]", NONE when a required key attribute is missing */
static str_t key_of(bctx_t *c, const doc_t *d, const el_t *x, const crec_t *r) {
    cbuf_t k = { 0 };
    cb_puts(&k, r->tags[0]);
    cb_puts(&k, "[");
    for (int i = 0; i < r->nkey; i++) {
        str_t v = get_attr(&c->A, d, x, r->key[i], 1);
        if (!v.s && r->keydef[i]) v = a_cstr(&c->A, r->keydef[i]);
        if (!v.s) { cbuf_free(&k); return NONE; }
        v = norm_key(&c->A, v);
        if (!strcmp(r->tags[0], "costume") && !strcmp(r->key[i], "type")
                && (s_is(v, "passive") || s_is(v, "active") || s_is(v, "familiar"))) v = a_cstr(&c->A, "item");
        if (i) cb_puts(&k, ".");
        cb_put(&k, v.s, (size_t) v.n);
    }
    cb_puts(&k, "]");
    str_t s = a_str(&c->A, k.s, (int) k.n);
    cbuf_free(&k);
    return s;
}

/* ------------------------------------------------------------ 1. the base: bounds */

typedef struct { char kind; str_t text; } bhit_t;

/* bound_check: the record's values outside the loader's table, as [(kind, text)] */
static int bound_check(bctx_t *c, const doc_t *d, const el_t *x, const crec_t *r, bhit_t *out, int max) {
    int n = 0;
    for (const cbound_t *b = r->bounds; b && b->attr; b++) {
        str_t raw = get_attr(&c->A, d, x, b->attr, 1);
        if (!raw.s) continue;
        if (b->when == 'a') {
            str_t w = get_attr(&c->A, d, x, b->when_s, 1);
            if (!w.s || !sl_in(b->when_vals, ustrip(w))) continue;
        }
        str_t s = ustrip(raw);
        str_t name = name_of(&c->A, d, x);
        if (b->kind == 'a') {
            int high = 0;
            for (int i = 0; i < raw.n && !high; i++) high = (unsigned char) raw.s[i] >= 0x80;
            if (high && n < max) {
                cbuf_t t = { 0 };
                cb_printf(&t, "%s %s=%s (a byte >= 0x80)", name.s, b->attr, raw.s);
                out[n++] = (bhit_t) { 'd', a_str(&c->A, t.s, (int) t.n) };
                cbuf_free(&t);
            }
            continue;
        }
        if (b->when == 'p') {
            size_t pl = strlen(b->when_s);
            if ((size_t) s.n < pl || memcmp(s.s, b->when_s, pl)) continue;
            s.s += pl; s.n -= (int) pl;
        } else {
            int sign = s.n && (s.s[0] == '+' || s.s[0] == '-');
            int digit = s.n > sign && s.s[sign] >= '0' && s.s[sign] <= '9';
            if (!digit && s.n >= 2 && isalpha((unsigned char) s.s[0]) && (unsigned char) s.s[0] < 0x80) {
                int all = 1;
                for (int i = 1; i < s.n && all; i++) all = s.s[i] >= '0' && s.s[i] <= '9';
                if (all) continue;                  /* another prefix form (wisps 's3' against the numeric bound) */
            }
        }
        long long v = lead_int(s);
        if ((b->has_lo && v < b->lo) || (b->has_hi && v > b->hi)) {
            if (n < max) {
                cbuf_t t = { 0 };
                char lo[32] = "", hi[32] = "";
                if (b->has_lo) snprintf(lo, sizeof(lo), "%lld", b->lo);
                if (b->has_hi) snprintf(hi, sizeof(hi), "%lld", b->hi);
                cb_printf(&t, "%s %s=%s outside %s..%s", name.s, b->attr, raw.s, lo, hi);
                out[n++] = (bhit_t) { b->kind == 'd' ? 'd' : 'i', a_str(&c->A, t.s, (int) t.n) };
                cbuf_free(&t);
            }
        }
    }
    return n;
}

static void join5(cbuf_t *t, const slist_t *l) {
    for (int i = 0; i < l->n && i < 5; i++) { if (i) cb_puts(t, ", "); cb_put(t, l->v[i].s, (size_t) l->v[i].n); }
    if (l->n > 5) cb_puts(t, " ...");
}

/* _legacy_refusal: why the legacy file is refused, NONE when it is not */
static str_t legacy_refusal(bctx_t *c, const char *mb, int mn, const char *mid) {
    doc_t d;
    if (doc_parse(&d, mb, mn)) {
        cbuf_t t = { 0 };
        cb_printf(&t, "it does not read as XML (%s)", d.err);
        str_t s = a_str(&c->A, t.s, (int) t.n);
        cbuf_free(&t);
        doc_free(&d);
        return s;
    }
    if (!c->table) { doc_free(&d); return NONE; }
    slist_t bad = { 0 }, skipped = { 0 };
    for (int i = 0; i < d.nv; i++) {
        const el_t *x = &d.v[i];
        if (x->depth != 1) continue;
        const crec_t *r = rec_of(c->table, &d, &d.v[x->parent], &d, x);
        if (!r || !r->bounds) continue;
        bhit_t h[8];
        int n = bound_check(c, &d, x, r, h, 8);
        for (int j = 0; j < n; j++) sl_add(&c->A, h[j].kind == 'd' ? &bad : &skipped, h[j].text);
    }
    doc_free(&d);
    if (skipped.n) {
        cbuf_t t = { 0 };
        join5(&t, &skipped);
        say(c, "legacy %s: %d records the loader skips (outside its table): %s", mid, skipped.n, t.s);
        cbuf_free(&t);
    }
    if (bad.n) {
        cbuf_t t = { 0 };
        cb_puts(&t, "records outside the loader's table, the game would write past it: ");
        join5(&t, &bad);
        str_t s = a_str(&c->A, t.s, (int) t.n);
        cbuf_free(&t);
        return s;
    }
    return NONE;
}

/* ------------------------------------------------------------ 1b. the base's item lists, mended (M1) */
/*
 * _mend_lists (the owner, 02.10.2026): a mod's WHOLE table that hands the game a list of item ids - players.xml
 * items=, challenges.xml startingitems / startingitems2 / startingtrinkets - is mended, not refused and not passed
 * as it is: the ids the game's own rule (0x73f904: 0 < id < the item count, not 43/61/235, an entry exists) does
 * not take are left out of the list, everything else of the file stays. The loaders copy such lists unchecked and
 * AddCollectible takes any number (the research notes of 02.10.2026, batch 5 M1-M5). A negative id is a pocket
 * active only in challenges.xml.
 */
static void tokens(arena_t *a, str_t s, slist_t *out);
static str_t list_value(bctx_t *c, str_t old, char kind, const slist_t *adds, const slist_t *dels);
static const char *rkind_of(const crec_t *r, str_t a);
static char lkind_of(const crec_t *r, str_t a);

static int item_ok(bctx_t *c, const char *kind, str_t t) {
    str_t s = ustrip(t);
    if (!is_int(s)) return 0;
    if (strcmp(kind, "collectible")) return ref_ok(c, kind, s);
    if (s.s[0] == '-' && !strcmp(c->fname, "players.xml")) return 0;
    str_t n = int_text(&c->A, s, 1);
    if (s_is(n, "0") || s_is(n, "43") || s_is(n, "61") || s_is(n, "235")) return 0;
    game_ids(c);
    return smap_has(&c->ids[0], n);
}

static int mend_lists(bctx_t *c, const char *mb, int mn, const char *mid, cbuf_t *out) {
    doc_t d;
    if (doc_parse(&d, mb, mn)) { doc_free(&d); return 0; }
    edits_t ed = { 0 };
    for (int i = 0; i < d.nv; i++) {
        const el_t *x = &d.v[i];
        if (x->depth != 1) continue;
        const crec_t *r = rec_of(c->table, &d, &d.v[x->parent], &d, x);
        if (!r) continue;
        ats_t have = attrs_of(&c->A, &d, x);
        for (int a = 0; a < have.n; a++) {
            str_t an = have.v[a].name;
            int first = 1;
            for (int j = 0; j < a && first; j++) first = !s_eq(have.v[j].name, an);
            if (!first) continue;
            const char *kind = rkind_of(r, an);
            char lk = lkind_of(r, an);
            if (!lk || !kind || (strcmp(kind, "collectible") && strcmp(kind, "trinket"))) continue;
            int last = a;
            for (int j = a + 1; j < have.n; j++) if (s_eq(have.v[j].name, an)) last = j;
            str_t old = u8(&c->A, mb + have.v[last].vs, have.v[last].ve - have.v[last].vs);
            slist_t toks = { 0 }, bad = { 0 };
            tokens(&c->A, ustrip(old), &toks);
            for (int k = 0; k < toks.n; k++) if (!item_ok(c, kind, toks.v[k])) sl_add(&c->A, &bad, toks.v[k]);
            if (!bad.n) continue;
            str_t who = get_attr(&c->A, &d, x, "name", 1);
            if (!who.s || !ustrip(who).n) {
                who = key_of(c, &d, x, r);
                if (!who.s) who = name_of(&c->A, &d, x);
            }
            for (int k = 0; k < bad.n; k++) say(c, "legacy %s: %s: %s %s does not exist - left out", mid, who.s, an.s, bad.v[k].s);
            slist_t none = { 0 };
            str_t nv = list_value(c, old, lk, &none, &bad);
            for (int j = a; j < have.n; j++) {
                const at_t *h = &have.v[j];
                if (!s_eq(h->name, an)) continue;
                if (h->ve - h->vs == nv.n && !memcmp(mb + h->vs, nv.s, (size_t) nv.n)) continue;
                str_t val;
                char q2 = quote_value(&c->A, nv.s, nv.n, h->q, &val);
                if (q2 == h->q) ed_add(&c->A, &ed, h->vs, h->ve, val.s, val.n);
                else {
                    cbuf_t t = { 0 };
                    cb_put(&t, &q2, 1); cb_put(&t, val.s, (size_t) val.n); cb_put(&t, &q2, 1);
                    ed_addb(&c->A, &ed, h->vs - 1, h->ve + 1, &t);
                    cbuf_free(&t);
                }
            }
        }
    }
    int done = ed.n > 0;
    if (done) ed_apply(&ed, mb, mn, out);
    doc_free(&d);
    return done;
}

/* ------------------------------------------------------------ 1a. version (04.10.2026) */
/*
 * A WHOLE TABLE MADE FOR ANOTHER VERSION OF THE GAME (the owner approved,
 * 04.10.2026): a three-way merge against the other version's own table
 * (Repentance+'s, carried by the port: cgame_t.other). The spec and its
 * rules are content_sim.py's _version, to the byte: per record and attribute
 * the mod's value that is Repentance+'s and not the game's is put back to the
 * game's; a record only Repentance+ and the mod have is left out; a record's
 * children as a whole likewise; everything else of the mod's stays.
 */
typedef struct { const char *b; int n; } raw_t;
static int kids_same(bctx_t *c, const doc_t *da, const el_t *xa, const doc_t *db, const el_t *xb);

/* the last occurrence's raw bytes of attr (n -1: none) */
static raw_t v_val(const doc_t *d, const ats_t *at, str_t name) {
    raw_t r = { NULL, -1 };
    for (int i = 0; i < at->n; i++)
        if (s_eq(at->v[i].name, name)) { r.b = d->b + at->v[i].vs; r.n = at->v[i].ve - at->v[i].vs; }
    return r;
}
static int raw_eq(raw_t a, raw_t b) { return a.n == b.n && (a.n <= 0 || !memcmp(a.b, b.b, (size_t) a.n)); }

static const char *v_shown(bctx_t *c, raw_t r) {
    if (r.n < 0) return "none";
    str_t u = u8(&c->A, r.b, r.n);
    cbuf_t t = { 0 };
    cb_printf(&t, "\"%s\"", u.s);
    str_t s = a_str(&c->A, t.s, (int) t.n);
    cbuf_free(&t);
    return s.s;
}

/* an attribute's (name, value) for the canonical compare: sorted by name, then value (Python's tuple order) */
typedef struct { const char *nb; int nn; const char *vb; int vn; } nv_t;
static int bcmp_(const char *a, int an, const char *b, int bn) {
    int m = an < bn ? an : bn;
    int r = memcmp(a, b, (size_t) m);
    return r ? r : an - bn;
}
static int nv_cmp(const void *x, const void *y) {
    const nv_t *a = (const nv_t *) x, *b = (const nv_t *) y;
    int r = bcmp_(a->nb, a->nn, b->nb, b->nn);
    return r ? r : bcmp_(a->vb, a->vn, b->vb, b->vn);
}

/* _canon(a) == _canon(b): the tag, the attributes as a sorted list, the children in order */
static int el_same(bctx_t *c, const doc_t *da, const el_t *xa, const doc_t *db, const el_t *xb) {
    if (!tag_eq(da, xa, db, xb)) return 0;
    ats_t aa = attrs_of(&c->A, da, xa), ab = attrs_of(&c->A, db, xb);
    if (aa.n != ab.n) return 0;
    if (aa.n) {
        nv_t *va = (nv_t *) a_alloc(&c->A, sizeof(nv_t) * (size_t) aa.n), *vb = (nv_t *) a_alloc(&c->A, sizeof(nv_t) * (size_t) ab.n);
        for (int i = 0; i < aa.n; i++) {
            va[i] = (nv_t) { aa.v[i].name.s, aa.v[i].name.n, da->b + aa.v[i].vs, aa.v[i].ve - aa.v[i].vs };
            vb[i] = (nv_t) { ab.v[i].name.s, ab.v[i].name.n, db->b + ab.v[i].vs, ab.v[i].ve - ab.v[i].vs };
        }
        qsort(va, (size_t) aa.n, sizeof(nv_t), nv_cmp);
        qsort(vb, (size_t) ab.n, sizeof(nv_t), nv_cmp);
        for (int i = 0; i < aa.n; i++) if (nv_cmp(&va[i], &vb[i])) return 0;
    }
    return kids_same(c, da, xa, db, xb);
}
static int kids_same(bctx_t *c, const doc_t *da, const el_t *xa, const doc_t *db, const el_t *xb) {
    int i = xa->first, j = xb->first;
    for (; i >= 0 && j >= 0; i = da->v[i].next, j = db->v[j].next)
        if (!el_same(c, da, &da->v[i], db, &db->v[j])) return 0;
    return i < 0 && j < 0;
}

/* the attribute rule on one element (_version_attrs): *back and *own counted */
static void v_attrs(bctx_t *c, const doc_t *mv, const el_t *x, const doc_t *gv, const el_t *gx, const doc_t *ov,
                    const el_t *ox, edits_t *ed, const char *mid, const char *who, int *back, int *own) {
    ats_t ma = attrs_of(&c->A, mv, x), ga = attrs_of(&c->A, gv, gx), oa = { NULL, 0 };
    if (ox) oa = attrs_of(&c->A, ov, ox);
    int total = ma.n + ga.n, nn = 0;
    str_t *names = (str_t *) a_alloc(&c->A, sizeof(str_t) * (size_t) (total + 1));
    for (int i = 0; i < total; i++) {
        str_t a = i < ma.n ? ma.v[i].name : ga.v[i - ma.n].name;
        int seen = 0;
        for (int k = 0; k < nn && !seen; k++) seen = s_eq(names[k], a);
        if (!seen) names[nn++] = a;
    }
    for (int i = 0; i < nn; i++) {
        str_t a = names[i];
        raw_t m = v_val(mv, &ma, a), g = v_val(gv, &ga, a);
        if (raw_eq(m, g)) continue;
        raw_t o = ox ? v_val(ov, &oa, a) : (raw_t) { NULL, -1 };
        if (!ox || !raw_eq(m, o) || raw_eq(o, g)) { (*own)++; continue; }
        (*back)++;
        if (g.n < 0) {
            for (int k = 0; k < ma.n; k++) {
                if (!s_eq(ma.v[k].name, a)) continue;
                int at = ma.v[k].ns;
                while (at > x->as && cspace(mv->b[at - 1])) at--;
                ed_add(&c->A, ed, at, ma.v[k].ve + 1, "", 0);
            }
        } else if (m.n < 0) {
            str_t val;
            char q2 = quote_value(&c->A, g.b, g.n, '"', &val);
            cbuf_t t = { 0 };
            cb_puts(&t, " ");
            put_l1(&t, a);
            cb_put(&t, "=", 1);
            cb_put(&t, &q2, 1);
            cb_put(&t, val.s, (size_t) val.n);
            cb_put(&t, &q2, 1);
            int at = ma.n ? ma.v[ma.n - 1].ve + 1 : x->ne;
            ed_addb(&c->A, ed, at, at, &t);
            cbuf_free(&t);
        } else {
            for (int k = 0; k < ma.n; k++) {
                if (!s_eq(ma.v[k].name, a)) continue;
                raw_t here = { mv->b + ma.v[k].vs, ma.v[k].ve - ma.v[k].vs };
                if (raw_eq(here, g)) continue;
                str_t val;
                char q = ma.v[k].q, q2 = quote_value(&c->A, g.b, g.n, q, &val);
                if (q2 == q) ed_add(&c->A, ed, ma.v[k].vs, ma.v[k].ve, val.s, val.n);
                else {
                    cbuf_t t = { 0 };
                    cb_put(&t, &q2, 1);
                    cb_put(&t, val.s, (size_t) val.n);
                    cb_put(&t, &q2, 1);
                    ed_addb(&c->A, ed, ma.v[k].vs - 1, ma.v[k].ve + 1, &t);
                    cbuf_free(&t);
                }
            }
        }
        say(c, "legacy %s: %s %s=%s is Repentance+'s - the game's %s kept", mid, who, a.s, v_shown(c, m), v_shown(c, g));
    }
}

/* {root name \x01 key} -> the element's index + 1, or -1 when the key comes twice (_keyed) */
static void v_keyed(bctx_t *c, const doc_t *d, smap_t *out) {
    for (int i = 0; i < d->nv; i++) {
        const el_t *x = &d->v[i];
        if (x->depth != 1) continue;
        const el_t *rx = &d->v[x->parent];
        const crec_t *r = rec_of(c->table, d, rx, d, x);
        if (!r || !r->nkey) continue;
        str_t k = key_of(c, d, x, r);
        if (!k.s) continue;
        str_t rn = name_of(&c->A, d, rx);
        cbuf_t t = { 0 };
        cb_put(&t, rn.s, (size_t) rn.n);
        cb_put(&t, "\x01", 1);
        cb_put(&t, k.s, (size_t) k.n);
        void **slot = smap_at(&c->A, out, (str_t) { t.s, (int) t.n }, 1);
        cbuf_free(&t);
        if (!slot) continue;
        *slot = *slot ? (void *) (intptr_t) -1 : (void *) (intptr_t) (i + 1);
    }
}
static int v_find(bctx_t *c, smap_t *m, str_t rn, str_t k) {
    cbuf_t t = { 0 };
    cb_put(&t, rn.s, (size_t) rn.n);
    cb_put(&t, "\x01", 1);
    cb_put(&t, k.s, (size_t) k.n);
    void **slot = smap_at(NULL, m, (str_t) { t.s, (int) t.n }, 0);
    cbuf_free(&t);
    (void) c;
    return slot ? (int) (intptr_t) *slot : 0;       /* 0 none, -1 twice, else index + 1 */
}
static int first_root(const doc_t *d, const doc_t *nd, const el_t *named) {
    for (int i = d->rfirst; i >= 0; i = d->v[i].next) if (tag_eq(d, &d->v[i], nd, named)) return i;
    return -1;
}
static int str_cmp(const void *x, const void *y) {
    const str_t *a = (const str_t *) x, *b = (const str_t *) y;
    return bcmp_(a->s, a->n, b->s, b->n);
}

/* _version: the mod's whole table with the other version's differences put back; 0 when nothing changed */
static int version_of(bctx_t *c, const char *mb, int mn, const char *ob, int on, const char *mid, cbuf_t *out) {
    doc_t mv, gv, ov;
    int bm = doc_parse(&mv, mb, mn), bg = doc_parse(&gv, c->g, c->gn), bo = doc_parse(&ov, ob, on);
    int back = 0, own = 0;
    edits_t ed = { 0 };
    if (bm || bg || bo) goto done;
    smap_t gk = { 0 }, ok = { 0 }, mk = { 0 };
    v_keyed(c, &gv, &gk);
    v_keyed(c, &ov, &ok);
    v_keyed(c, &mv, &mk);
    for (int i = 0; i < mv.nv; i++) {
        const el_t *x = &mv.v[i];
        if (x->depth == 0) {
            int gr = first_root(&gv, &mv, x);
            if (gr < 0) continue;
            int orr = first_root(&ov, &mv, x);
            str_t rn = name_of(&c->A, &mv, x);
            cbuf_t w = { 0 };
            cb_printf(&w, "<%s>", rn.s);
            str_t who = a_str(&c->A, w.s, (int) w.n);
            cbuf_free(&w);
            v_attrs(c, &mv, x, &gv, &gv.v[gr], &ov, orr >= 0 ? &ov.v[orr] : NULL, &ed, mid, who.s, &back, &own);
            continue;
        }
        if (x->depth != 1) continue;
        const el_t *rx = &mv.v[x->parent];
        if (first_root(&gv, &mv, rx) < 0) continue;
        const crec_t *r = rec_of(c->table, &mv, rx, &mv, x);
        if (!r || !r->nkey) continue;
        str_t k = key_of(c, &mv, x, r);
        if (!k.s) continue;
        str_t rn = name_of(&c->A, &mv, rx);
        if (v_find(c, &mk, rn, k) <= 0) continue;
        int gi = v_find(c, &gk, rn, k), oi = v_find(c, &ok, rn, k);
        if (!gi) {
            if (oi > 0) {
                int ind = indent_of(mb, x->start), e = eol(mb, mn, x->stop);
                if (ind >= 0 && e >= 0 && e < mn) ed_add(&c->A, &ed, ind, e + (mn - e >= 2 && mb[e] == '\r' && mb[e + 1] == '\n' ? 2 : 1), "", 0);
                else ed_add(&c->A, &ed, x->start, x->stop, "", 0);
                back++;
                say(c, "legacy %s: %s is Repentance+'s (the game has none) - left out", mid, k.s);
            } else own++;
            continue;
        }
        if (gi < 0) continue;
        const el_t *gx = &gv.v[gi - 1], *ox = oi > 0 ? &ov.v[oi - 1] : NULL;
        v_attrs(c, &mv, x, &gv, gx, &ov, ox, &ed, mid, k.s, &back, &own);
        if (kids_same(c, &mv, x, &gv, gx)) continue;
        if (!ox || !kids_same(c, &mv, x, &ov, ox) || x->inner_end == x->stop) { own++; continue; }
        if (gx->inner_end != gx->stop) ed_add(&c->A, &ed, x->tag_end + 1, x->inner_end, c->g + gx->tag_end + 1, gx->inner_end - gx->tag_end - 1);
        else ed_add(&c->A, &ed, x->tag_end + 1, x->inner_end, "", 0);
        back++;
        /* the children's tags, of the mod's and the game's, sorted, once each */
        int cap = 0;
        for (int j = x->first; j >= 0; j = mv.v[j].next) cap++;
        for (int j = gx->first; j >= 0; j = gv.v[j].next) cap++;
        str_t *tags = (str_t *) a_alloc(&c->A, sizeof(str_t) * (size_t) (cap + 1));
        int nt = 0;
        for (int pass = 0; pass < 2; pass++) {
            const doc_t *d = pass ? &gv : &mv;
            for (int j = pass ? gx->first : x->first; j >= 0; j = d->v[j].next) {
                str_t t = name_of(&c->A, d, &d->v[j]);
                int have = 0;
                for (int q = 0; q < nt && !have; q++) have = s_eq(tags[q], t);
                if (!have) tags[nt++] = t;
            }
        }
        qsort(tags, (size_t) nt, sizeof(str_t), str_cmp);
        cbuf_t tl = { 0 };
        for (int q = 0; q < nt; q++) { if (q) cb_puts(&tl, "> <"); cb_put(&tl, tags[q].s, (size_t) tags[q].n); }
        cb_put(&tl, "", 1);
        say(c, "legacy %s: %s: its <%s> are Repentance+'s - the game's kept", mid, k.s, tl.s);
        cbuf_free(&tl);
    }
    if (back) {
        say(c, "legacy %s: a table of Repentance+ - %d of its differences are Repentance+'s, the game's kept; "
               "%d changes of the mod's own taken", mid, back, own);
        ed_apply(&ed, mb, mn, out);
    }
done:
    doc_free(&mv);
    doc_free(&gv);
    doc_free(&ov);
    return back;
}

/* ------------------------------------------------------------ 2. completion (S7) */

static int complete(bctx_t *c, const char *mb, int mn, cbuf_t *out) {
    doc_t mv, gv;
    int bad = doc_parse(&mv, mb, mn);
    bad |= doc_parse(&gv, c->g, c->gn);
    int added = 0;
    edits_t ed = { 0 };
    for (int gr = bad ? -1 : gv.rfirst; gr >= 0; gr = gv.v[gr].next) {
        const el_t *g = &gv.v[gr];
        int mr = -1;
        for (int i = mv.rfirst; i >= 0 && mr < 0; i = mv.v[i].next) if (tag_eq(&mv, &mv.v[i], &gv, g)) mr = i;
        if (mr < 0 || mv.v[mr].inner_end == mv.v[mr].stop) continue;
        smap_t have = { 0 };
        for (int i = mv.v[mr].first; i >= 0; i = mv.v[i].next) {
            const crec_t *r = rec_of(c->table, &gv, g, &mv, &mv.v[i]);
            if (r && r->nkey) {
                str_t k = key_of(c, &mv, &mv.v[i], r);
                if (k.s) smap_add(&c->A, &have, k);
            }
        }
        cbuf_t buf = { 0 };
        for (int i = g->first; i >= 0; i = gv.v[i].next) {
            const crec_t *r = rec_of(c->table, &gv, g, &gv, &gv.v[i]);
            if (!r || !r->nkey) continue;
            str_t k = key_of(c, &gv, &gv.v[i], r);
            if (!k.s || smap_has(&have, k)) continue;
            cb_puts(&buf, "\n");
            cb_put(&buf, c->g + gv.v[i].start, (size_t) (gv.v[i].stop - gv.v[i].start));
            added++;
        }
        if (buf.n) {
            cb_puts(&buf, "\n");
            ed_addb(&c->A, &ed, mv.v[mr].inner_end, mv.v[mr].inner_end, &buf);
        }
        cbuf_free(&buf);
    }
    if (added) ed_apply(&ed, mb, mn, out);
    doc_free(&mv);
    doc_free(&gv);
    return added;
}

/* ------------------------------------------------------------ 3. content/entities2.xml (S8) */

static int is_digits(str_t s) {
    if (!s.n) return 0;
    for (int i = 0; i < s.n; i++) if (s.s[i] < '0' || s.s[i] > '9') return 0;
    return 1;
}

/* str.lower() of ASCII letters */
static str_t lower_of(arena_t *a, str_t s) {
    str_t r = a_str(a, s.s, s.n);
    for (int i = 0; i < r.n; i++) ((char *) r.s)[i] = (char) tolower((unsigned char) r.s[i]);
    return r;
}

static str_t ekey3(bctx_t *c, const doc_t *d, const el_t *x, str_t *type) {
    static const char *const A[3] = { "id", "variant", "subtype" }, *const D[3] = { "-1", "0", "0" };
    cbuf_t k = { 0 };
    for (int i = 0; i < 3; i++) {
        str_t v = get_attr(&c->A, d, x, A[i], 0);
        if (!v.s || !v.n) v = a_cstr(&c->A, D[i]);
        v = norm_key(&c->A, v);
        if (!i && type) *type = v;
        if (i) cb_put(&k, "\x01", 1);
        cb_put(&k, v.s, (size_t) v.n);
    }
    str_t s = a_str(&c->A, k.s, (int) k.n);
    cbuf_free(&k);
    return s;
}

static int first_whole_root(const doc_t *d, const char *name) {
    for (int i = d->rfirst; i >= 0; i = d->v[i].next)
        if (tag_is(d, &d->v[i], name) && d->v[i].inner_end != d->v[i].stop) return i;
    return -1;
}

static void content_entities(bctx_t *c, cbuf_t *cur, const cmodf_t *mods, const int *with, int nw) {
    doc_t v;
    int root = doc_parse(&v, cur->s, (long) cur->n) ? -1 : first_whole_root(&v, "entities");
    if (root < 0) {
        say(c, "content: the base has no whole <entities> root - nothing merged");
        doc_free(&v);
        return;
    }
    str_t base_root = get_attr(&c->A, &v, &v.v[root], "anm2root", 0);
    if (!base_root.s || !base_root.n) base_root = a_cstr(&c->A, "gfx/");
    smap_t keys = { 0 }, types = { 0 };
    for (int i = v.v[root].first; i >= 0; i = v.v[i].next) {
        if (!tag_is(&v, &v.v[i], "entity")) continue;
        str_t t;
        smap_add(&c->A, &keys, ekey3(c, &v, &v.v[i], &t));
        smap_add(&c->A, &types, t);
    }
    const crec_t *erec = NULL;
    for (const croot_t *r = c->table->roots; r->name && !erec; r++)
        if (!strcmp(r->name, "entities")) for (const crec_t *q = r->recs; q->tags && !erec; q++) if (!strcmp(q->tags[0], "entity")) erec = q;
    cbuf_t out = { 0 };
    for (int w = 0; w < nw; w++) {
        const cmodf_t *m = &mods[with[w]];
        doc_t mv;
        if (doc_parse(&mv, m->content, m->content_n)) {
            say(c, "content %s: does not read as XML (%s) - skipped", m->id, mv.err);
            doc_free(&mv);
            continue;
        }
        int mr = first_whole_root(&mv, "entities");
        if (mr < 0) {
            say(c, "content %s: no whole <entities> root - skipped", m->id);
            doc_free(&mv);
            continue;
        }
        str_t mod_root = get_attr(&c->A, &mv, &mv.v[mr], "anm2root", 0);
        if (!mod_root.s || !mod_root.n) mod_root = a_cstr(&c->A, "gfx/");
        str_t prefix = { "", 0 };
        str_t ml = lower_of(&c->A, mod_root), bl = lower_of(&c->A, base_root);
        if (!s_eq(ml, bl)) {
            if (ml.n >= bl.n && !memcmp(ml.s, bl.s, (size_t) bl.n)) {
                /* mod_root[len(base_root):] - by characters */
                int chars = 0;
                for (int i = 0; i < base_root.n; i += uchar_len(base_root.s + i, base_root.s + base_root.n)) chars++;
                const char *p = mod_root.s, *e = mod_root.s + mod_root.n;
                while (chars-- > 0 && p < e) p += uchar_len(p, e);
                prefix = (str_t) { p, (int) (e - p) };
            } else {
                say(c, "content %s: anm2root \"%s\" is not under the game's \"%s\" - its paths are kept as they are",
                    m->id, mod_root.s, base_root.s);
            }
        }
        int added = 0, dup = 0, newtype = 0, wide = 0;
        cbuf_t chunk = { 0 };
        for (int i = mv.v[mr].first; i >= 0; i = mv.v[i].next) {
            const el_t *x = &mv.v[i];
            if (!tag_is(&mv, x, "entity")) continue;
            str_t t;
            str_t k = ekey3(c, &mv, x, &t);
            if (!smap_has(&types, t) || !is_digits(t)) { newtype++; continue; }
            bhit_t h[4];
            if (erec && bound_check(c, &mv, x, erec, h, 4)) { wide++; continue; }
            if (smap_has(&keys, k)) { dup++; continue; }
            smap_add(&c->A, &keys, k);
            ats_t at = attrs_of(&c->A, &mv, x);
            const at_t *ap = NULL;
            for (int j = 0; j < at.n && !ap; j++) if (s_is(at.v[j].name, "anm2path")) ap = &at.v[j];
            if (prefix.n && ap) {
                cb_put(&chunk, mv.b + x->start, (size_t) (ap->vs - x->start));
                cb_put(&chunk, prefix.s, (size_t) prefix.n);
                cb_put(&chunk, mv.b + ap->vs, (size_t) (x->stop - ap->vs));
            } else cb_put(&chunk, mv.b + x->start, (size_t) (x->stop - x->start));
            cb_puts(&chunk, "\n");
            added++;
        }
        char more[160] = "";
        if (wide) snprintf(more, sizeof(more), "; %d with a variant/subtype past the loader's key (variant 0..4095, subtype 0..255) - left out", wide);
        say(c, "content %s: %d new entities merged; %d the game or a stronger mod has already, %d of a type the game "
            "does not have (a script's creature) - left out%s", m->id, added, dup, newtype, more);
        if (added) {
            cb_printf(&out, "\n<!-- %s (content/entities2.xml, merged by the port) -->\n", m->id);
            cb_put(&out, chunk.s, chunk.n);
        }
        cbuf_free(&chunk);
        doc_free(&mv);
    }
    if (out.n) {
        cb_puts(&out, "\n");
        edits_t ed = { 0 };
        ed_addb(&c->A, &ed, v.v[root].inner_end, v.v[root].inner_end, &out);
        cbuf_t res = { 0 };
        ed_apply(&ed, cur->s, (int) cur->n, &res);
        cbuf_free(cur);
        *cur = res;
    }
    cbuf_free(&out);
    doc_free(&v);
}

/* ------------------------------------------------------------ 4. patch/ (S1-S6) */

typedef struct { char op; str_t a; const char *raw; int rawn; } op_t;   /* r remove, u unset, s set, a -add, d -remove */
typedef struct { op_t *v; int n, cap; } ops_t;

static void ops_add(arena_t *a, ops_t *o, op_t op) {
    if (o->n == o->cap) {
        int cap = o->cap ? o->cap * 2 : 16;
        op_t *v = (op_t *) a_alloc(a, sizeof(op_t) * (size_t) cap);
        if (o->n) memcpy(v, o->v, sizeof(op_t) * (size_t) o->n);
        o->v = v;
        o->cap = cap;
    }
    o->v[o->n++] = op;
}

/* str.split(): the words between runs of whitespace */
static void usplit(arena_t *a, str_t s, slist_t *out) {
    const char *p = s.s, *e = s.s + s.n;
    while (p < e) {
        int k;
        while (p < e && (k = uspace(p, e))) p += k;
        if (p >= e) break;
        const char *w = p;
        while (p < e && !uspace(p, e)) p += uchar_len(p, e);
        sl_add(a, out, a_str(a, w, (int) (p - w)));
    }
}

/* _tokens / the list split: the words between runs of commas and whitespace */
static void tokens(arena_t *a, str_t s, slist_t *out) {
    const char *p = s.s, *e = s.s + s.n;
    while (p < e) {
        int k = 0;
        while (p < e && (*p == ',' || (k = uspace(p, e)))) p += *p == ',' ? 1 : k;
        if (p >= e) break;
        const char *w = p;
        while (p < e && *p != ',' && !uspace(p, e)) p += uchar_len(p, e);
        sl_add(a, out, a_str(a, w, (int) (p - w)));
    }
}

static int ends_with(str_t s, const char *t) {
    int k = (int) strlen(t);
    return s.n >= k && !memcmp(s.s + s.n - k, t, (size_t) k);
}

/* _parse_ops: a patch element's operations, in document order */
static void parse_ops(bctx_t *c, const doc_t *pd, const el_t *px, ops_t *ops) {
    ats_t at = attrs_of(&c->A, pd, px);
    for (int i = 0; i < at.n; i++) {
        str_t n = at.v[i].name;
        const char *raw = pd->b + at.v[i].vs;
        int rn = at.v[i].ve - at.v[i].vs;
        if (s_is(n, "remove")) ops_add(&c->A, ops, (op_t) { 'r', NONE, raw, rn });
        else if (s_is(n, "unset")) {
            slist_t w = { 0 };
            usplit(&c->A, u8(&c->A, raw, rn), &w);
            for (int j = 0; j < w.n; j++) ops_add(&c->A, ops, (op_t) { 'u', w.v[j], NULL, 0 });
        } else if (ends_with(n, "-add")) ops_add(&c->A, ops, (op_t) { 'a', a_str(&c->A, n.s, n.n - 4), raw, rn });
        else if (ends_with(n, "-remove")) ops_add(&c->A, ops, (op_t) { 'd', a_str(&c->A, n.s, n.n - 7), raw, rn });
        else ops_add(&c->A, ops, (op_t) { 's', n, raw, rn });
    }
}

static int has_op(const ops_t *o, char op) { for (int i = 0; i < o->n; i++) if (o->v[i].op == op) return 1; return 0; }

/* _list_value (S4) */
static str_t list_value(bctx_t *c, str_t old, char kind, const slist_t *adds, const slist_t *dels) {
    arena_t *A = &c->A;
    slist_t toks = { 0 };
    if (old.s && old.n) tokens(A, ustrip(old), &toks);
    const char *sep;
    if (kind == 'm') {
        sep = old.n && strstr(old.s, ", ") ? ", " : ",";
        for (int i = 0; i < dels->n; i++) {
            str_t d = norm_key(A, dels->v[i]);
            for (int j = 0; j < toks.n; j++)
                if (s_eq(norm_key(A, toks.v[j]), d)) {
                    memmove(toks.v + j, toks.v + j + 1, sizeof(str_t) * (size_t) (toks.n - j - 1));
                    toks.n--;
                    break;
                }
        }
        for (int i = 0; i < adds->n; i++) sl_add(A, &toks, adds->v[i]);
    } else {
        sep = !old.n || !memchr(old.s, ',', (size_t) old.n) ? " " : ",";
        smap_t dn = { 0 };
        for (int i = 0; i < dels->n; i++) smap_add(A, &dn, norm_key(A, dels->v[i]));
        int k = 0;
        for (int j = 0; j < toks.n; j++) if (!smap_has(&dn, norm_key(A, toks.v[j]))) toks.v[k++] = toks.v[j];
        toks.n = k;
        for (int i = 0; i < adds->n; i++) {
            str_t an = norm_key(A, adds->v[i]);
            int have = 0;
            for (int j = 0; j < toks.n && !have; j++) have = s_eq(norm_key(A, toks.v[j]), an);
            if (!have) sl_add(A, &toks, adds->v[i]);
        }
    }
    cbuf_t t = { 0 };
    cb_put(&t, "", 0);
    for (int j = 0; j < toks.n; j++) { if (j) cb_puts(&t, sep); cb_put(&t, toks.v[j].s, (size_t) toks.v[j].n); }
    str_t r = a_str(A, t.s, (int) t.n);
    cbuf_free(&t);
    return r;
}

typedef struct { const char *mid; char list; } owner_t;

typedef struct {
    bctx_t *c;
    const doc_t *v;                         /* the base being patched */
    const doc_t *pv;                        /* the patch */
    const char *mid, *base_id;
    smap_t *owners;                         /* "<k>\x01<attr>" -> owner_t */
    edits_t ed;
    const char *nl;
    int changed, refused;
} pctx_t;

static const char *rkind_of(const crec_t *r, str_t a) { int i = sl_index(r->refs, a); return i >= 0 ? r->rkind[i] : NULL; }
static char lkind_of(const crec_t *r, str_t a) { int i = r ? sl_index(r->lists, a) : -1; return i >= 0 ? r->lkind[i] : 0; }

/* _bad_refs: the first reference that names nothing of the game's, NONE when all do */
static str_t bad_refs(bctx_t *c, const crec_t *r, const op_t *ops, int n) {
    for (int i = 0; i < n; i++) {
        const op_t *o = &ops[i];
        const char *kind = o->a.s ? rkind_of(r, o->a) : NULL;
        if (!kind || !o->raw) continue;
        slist_t t = { 0 };
        if (lkind_of(r, o->a) || o->op != 's') tokens(&c->A, ustrip(u8(&c->A, o->raw, o->rawn)), &t);
        else sl_add(&c->A, &t, u8(&c->A, o->raw, o->rawn));
        for (int j = 0; j < t.n; j++) {
            /* M1 (the batch 5 M1-M5 notes): players.xml hands its numbers to AddCollectible as they are - a negative one is
               a pocket active only in challenges.xml, here no item (the game's 0x73f904 rules) */
            str_t ts = ustrip(t.v[j]);
            int neg = !strcmp(c->fname, "players.xml") && !strcmp(kind, "collectible") && ts.n && ts.s[0] == '-';
            if (neg || !ref_ok(c, kind, t.v[j])) {
                cbuf_t m = { 0 };
                cb_printf(&m, "%s %s is not a %s of the game", o->a.s, t.v[j].s, kind);
                str_t s = a_str(&c->A, m.s, (int) m.n);
                cbuf_free(&m);
                return s;
            }
        }
    }
    return NONE;
}

typedef struct { str_t a; char kind; const char *raw; int rawn; str_t old; slist_t adds, dels; } fin_t;

/* _apply_ops: a record's (or a root's) attribute operations */
static void apply_ops(pctx_t *p, const el_t *x, const op_t *ops, int nops, const crec_t *r, str_t k,
                      const char *const *rattrs, int root) {
    bctx_t *c = p->c;
    const char *b = p->v->b;
    ats_t have = attrs_of(&c->A, p->v, x);
    fin_t *fin = (fin_t *) a_alloc(&c->A, sizeof(fin_t) * (size_t) (nops + 1));
    int nf = 0;
    for (int i = 0; i < nops; i++) {
        const op_t *o = &ops[i];
        if (o->op == 'r') continue;
        str_t a = o->a;
        if (!root && sl_in(r->key, a)) {
            if (o->op == 's') {
                str_t last = get_attr(&c->A, p->v, x, a.s, 1);
                if (!last.s) last = a_cstr(&c->A, "");
                if (s_eq(norm_key(&c->A, u8(&c->A, o->raw, o->rawn)), norm_key(&c->A, last))) continue;
            }
            say(c, "patch %s: %s %s refused - a key attribute is never changed", p->mid, k.s, a.s);
            p->refused++;
            continue;
        }
        if (root && o->op != 's' && o->op != 'u') {
            say(c, "patch %s: %s %s refused - lists on a root", p->mid, k.s, a.s);
            p->refused++;
            continue;
        }
        if ((o->op == 'a' || o->op == 'd') && !lkind_of(r, a)) {
            say(c, "patch %s: %s %s-%s refused - %s is not a list", p->mid, k.s, a.s, o->op == 'a' ? "add" : "remove", a.s);
            p->refused++;
            continue;
        }
        if (!root) {
            str_t bad = bad_refs(c, r, o, 1);
            if (bad.s) {
                say(c, "patch %s: %s refused - %s", p->mid, k.s, bad.s);
                p->refused++;
                continue;
            }
        }
        int known = root ? sl_in(rattrs, a) : sl_in(r->attrs, a) || attr_seen(c, r, a);
        if (!known) say(c, "patch %s: %s %s - the loader does not read this attribute (applied anyway)", p->mid, k.s, a.s);
        fin_t *f = NULL;
        for (int j = 0; j < nf && !f; j++) if (s_eq(fin[j].a, a)) f = &fin[j];
        if (!f) { f = &fin[nf++]; memset(f, 0, sizeof(*f)); f->a = a; }
        if (o->op == 's') { f->kind = 's'; f->raw = o->raw; f->rawn = o->rawn; }
        else if (o->op == 'u') f->kind = 'u';
        else {
            slist_t tk = { 0 };
            tokens(&c->A, ustrip(u8(&c->A, o->raw, o->rawn)), &tk);
            if (f->kind == 's') {
                f->old = u8(&c->A, f->raw, f->rawn);
                f->kind = 'L';
                memset(&f->adds, 0, sizeof(f->adds));
                memset(&f->dels, 0, sizeof(f->dels));
            }
            if (f->kind != 'L' && f->kind != 'l') {
                memset(&f->adds, 0, sizeof(f->adds));
                memset(&f->dels, 0, sizeof(f->dels));
                f->kind = 'l';
            }
            for (int j = 0; j < tk.n; j++) sl_add(&c->A, o->op == 'a' ? &f->adds : &f->dels, tk.v[j]);
        }
    }
    for (int j = 0; j < nf; j++) {
        fin_t *f = &fin[j];
        str_t a = f->a;
        cbuf_t ok = { 0 };
        cb_put(&ok, k.s, (size_t) k.n);
        cb_put(&ok, "\x01", 1);
        cb_put(&ok, a.s, (size_t) a.n);
        void **slot = smap_at(&c->A, p->owners, (str_t) { ok.s, (int) ok.n }, 1);
        cbuf_free(&ok);
        owner_t *prev = (owner_t *) *slot;
        if (f->kind != 'l' && prev && strcmp(prev->mid, p->mid))
            say(c, "conflict %s %s: %s and %s - %s wins", k.s, a.s, prev->mid, p->mid, p->mid);
        owner_t *now = (owner_t *) a_alloc(&c->A, sizeof(owner_t));
        now->mid = p->mid;
        now->list = f->kind == 'l';
        *slot = now;
        int nocc = 0, lastocc = -1;
        for (int i = 0; i < have.n; i++) if (s_eq(have.v[i].name, a)) { nocc++; lastocc = i; }
        if (f->kind == 'u') {
            for (int i = 0; i < have.n; i++) {
                if (!s_eq(have.v[i].name, a)) continue;
                int s = have.v[i].ns;
                while (s > x->as && cspace(b[s - 1])) s--;
                ed_add(&c->A, &p->ed, s, have.v[i].ve + 1, "", 0);
            }
            p->changed += nocc > 0;
            continue;
        }
        str_t nv;
        if (f->kind == 's') nv = (str_t) { f->raw, f->rawn };
        else if (f->kind == 'L') nv = list_value(c, f->old, lkind_of(r, a), &f->adds, &f->dels);
        else {
            str_t old = lastocc >= 0 ? u8(&c->A, b + have.v[lastocc].vs, have.v[lastocc].ve - have.v[lastocc].vs) : a_cstr(&c->A, "");
            nv = list_value(c, old, lkind_of(r, a), &f->adds, &f->dels);
        }
        if (nocc) {
            for (int i = 0; i < have.n; i++) {
                const at_t *h = &have.v[i];
                if (!s_eq(h->name, a)) continue;
                if (h->ve - h->vs == nv.n && !memcmp(b + h->vs, nv.s, (size_t) nv.n)) continue;
                str_t val;
                char q2 = quote_value(&c->A, nv.s, nv.n, h->q, &val);
                if (q2 == h->q) ed_add(&c->A, &p->ed, h->vs, h->ve, val.s, val.n);
                else {
                    cbuf_t t = { 0 };
                    cb_put(&t, &q2, 1); cb_put(&t, val.s, (size_t) val.n); cb_put(&t, &q2, 1);
                    ed_addb(&c->A, &p->ed, h->vs - 1, h->ve + 1, &t);
                    cbuf_free(&t);
                }
            }
            p->changed++;
        } else {
            str_t val;
            char q2 = quote_value(&c->A, nv.s, nv.n, '"', &val);
            int at = have.n ? have.v[have.n - 1].ve + 1 : x->ne;
            cbuf_t t = { 0 };
            cb_puts(&t, " ");
            put_l1(&t, a);
            cb_puts(&t, "=");
            cb_put(&t, &q2, 1); cb_put(&t, val.s, (size_t) val.n); cb_put(&t, &q2, 1);
            ed_addb(&c->A, &p->ed, at, at, &t);
            cbuf_free(&t);
            p->changed++;
        }
    }
}

/* S6 */
static void remove_element(pctx_t *p, const el_t *x) {
    const char *b = p->v->b;
    int n = p->v->n, ind = indent_of(b, x->start), e = eol(b, n, x->stop);
    if (ind >= 0 && e >= 0 && e < n) ed_add(&p->c->A, &p->ed, ind, e + (n - e >= 2 && b[e] == '\r' && b[e + 1] == '\n' ? 2 : 1), "", 0);
    else ed_add(&p->c->A, &p->ed, x->start, x->stop, "", 0);
}

/* S5 */
static void add_children(pctx_t *p, const el_t *x, const cbuf_t *texts, const int *ends, int nt) {
    const char *b = p->v->b;
    int n = p->v->n, at;
    cbuf_t lead = { 0 };
    if (x->last >= 0) {
        const el_t *last = &p->v->v[x->last];
        int ind = indent_of(b, last->start);
        at = line_end_after(b, n, last->stop);
        if (ind >= 0) { cb_puts(&lead, p->nl); cb_put(&lead, b + ind, (size_t) (last->start - ind)); }
        else cb_puts(&lead, " ");
    } else {
        int ind = indent_of(b, x->start);
        at = x->tag_end + 1;
        if (ind >= 0) { cb_puts(&lead, p->nl); cb_put(&lead, b + ind, (size_t) (x->start - ind)); cb_puts(&lead, "\t"); }
        else cb_puts(&lead, " ");
        if (x->inner_end == x->stop) {
            say(p->c, "patch %s: a self-closing parent cannot take entries", p->mid);
            cbuf_free(&lead);
            return;
        }
    }
    cbuf_t all = { 0 };
    for (int i = 0, from = 0; i < nt; from = ends[i], i++) {
        cb_put(&all, lead.s, lead.n);
        cb_put(&all, texts->s + from, (size_t) (ends[i] - from));
    }
    ed_addb(&p->c->A, &p->ed, at, at, &all);
    cbuf_free(&all);
    cbuf_free(&lead);
}

typedef struct { int *v; int n, cap; } ilist_t;
typedef struct { int x; const crec_t *r; str_t k; ilist_t pxs; } grp_t;
typedef struct { grp_t *v; int n, cap; } grps_t;
typedef struct { const crec_t *r; str_t k; int px; } miss_t;
typedef struct { miss_t *v; int n, cap; } misses_t;

static void il_add(arena_t *a, ilist_t *l, int x) {
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        int *v = (int *) a_alloc(a, sizeof(int) * (size_t) cap);
        if (l->n) memcpy(v, l->v, sizeof(int) * (size_t) l->n);
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = x;
}

static void missing_top(pctx_t *p, const el_t *proot, const crec_t *r, str_t k, int px) {
    bctx_t *c = p->c;
    ops_t o = { 0 };
    parse_ops(c, p->pv, &p->pv->v[px], &o);
    if (has_op(&o, 'r')) say(c, "patch %s: %s remove refused - a record of a fixed table is never removed", p->mid, k.s);
    const doc_t *gv = game_doc(c);
    int in_game = 0;
    if (gv) {
        int gr = -1;
        for (int i = gv->rfirst; i >= 0 && gr < 0; i = gv->v[i].next) if (tag_eq(gv, &gv->v[i], p->pv, proot)) gr = i;
        for (int i = gr >= 0 ? gv->v[gr].first : -1; i >= 0 && !in_game; i = gv->v[i].next)
            if (tag_in(gv, &gv->v[i], r->tags)) in_game = s_eq(key_of(c, gv, &gv->v[i], r), k);
    }
    if (in_game && strcmp(p->base_id, "game"))
        say(c, "patch %s: %s refused - overridden by legacy %s (its file has no such record)", p->mid, k.s, p->base_id);
    else
        say(c, "patch %s: %s refused - no such record (a patch changes records; new ones go through content/ where "
            "the table allows it)", p->mid, k.s);
    p->refused++;
}

/* _group: patch elements -> (base element, rec, key, [patch elements]) in first-seen order */
static void group(pctx_t *p, int parent, const ilist_t *pels, const crec_t *recs, str_t kprefix, const el_t *proot,
                  grps_t *out, misses_t *missing) {
    bctx_t *c = p->c;
    const doc_t *v = p->v, *pv = p->pv;
    int nidx = 0;
    const crec_t *icls[16];
    smap_t imap[16];
    int *gat = (int *) a_alloc(&c->A, sizeof(int) * (size_t) (v->nv + 1));   /* base element -> its group + 1 */
    memset(gat, 0, sizeof(int) * (size_t) (v->nv + 1));
    for (int pi = 0; pi < pels->n; pi++) {
        int px = pels->v[pi];
        const el_t *pe = &pv->v[px];
        str_t ptag = name_of(&c->A, pv, pe);
        const crec_t *r = rec_in(recs, pv, pe);
        if (!r) {
            if (!kprefix.n) {
                str_t rn = name_of(&c->A, pv, proot);
                say(c, "patch %s: %s<%s> refused - not a record of <%s>", p->mid, kprefix.s, ptag.s, rn.s);
            } else say(c, "patch %s: %s<%s> refused - not a patchable child (positional or unknown)", p->mid, kprefix.s, ptag.s);
            p->refused++;
            continue;
        }
        ilist_t hits = { 0 };
        str_t k;
        cbuf_t kb = { 0 };
        if (!r->nkey) {
            for (int i = v->v[parent].first; i >= 0; i = v->v[i].next) if (tag_in(v, &v->v[i], r->tags)) { il_add(&c->A, &hits, i); break; }
            cb_put(&kb, kprefix.s, (size_t) kprefix.n);
            cb_puts(&kb, r->tags[0]);
            cb_puts(&kb, "[]");
            k = a_str(&c->A, kb.s, (int) kb.n);
        } else {
            str_t kk = key_of(c, pv, pe, r);
            if (!kk.s) {
                cbuf_t ks = { 0 };
                for (int i = 0; i < r->nkey; i++) { if (i) cb_puts(&ks, " "); cb_puts(&ks, r->key[i]); }
                say(c, "patch %s: %s<%s> refused - its key (%s) is missing", p->mid, kprefix.s, ptag.s, ks.s);
                cbuf_free(&ks);
                cbuf_free(&kb);
                p->refused++;
                continue;
            }
            cb_put(&kb, kprefix.s, (size_t) kprefix.n);
            cb_put(&kb, kk.s, (size_t) kk.n);
            k = a_str(&c->A, kb.s, (int) kb.n);
            int ix = -1;
            for (int i = 0; i < nidx && ix < 0; i++) if (!strcmp(icls[i]->tags[0], r->tags[0])) ix = i;
            if (ix < 0 && nidx < 16) {
                ix = nidx++;
                icls[ix] = r;
                memset(&imap[ix], 0, sizeof(smap_t));
                for (int i = v->v[parent].first; i >= 0; i = v->v[i].next) {
                    if (!tag_in(v, &v->v[i], r->tags)) continue;
                    void **s = smap_at(&c->A, &imap[ix], key_of(c, v, &v->v[i], r), 1);
                    if (!*s) { *s = a_alloc(&c->A, sizeof(ilist_t)); memset(*s, 0, sizeof(ilist_t)); }
                    il_add(&c->A, (ilist_t *) *s, i);
                }
            }
            void **s = ix >= 0 ? smap_at(&c->A, &imap[ix], kk, 0) : NULL;
            if (s && *s) hits = *(ilist_t *) *s;
        }
        cbuf_free(&kb);
        if (!hits.n) {
            if (missing) {
                if (missing->n == missing->cap) {
                    int cap = missing->cap ? missing->cap * 2 : 8;
                    miss_t *m = (miss_t *) a_alloc(&c->A, sizeof(miss_t) * (size_t) cap);
                    if (missing->n) memcpy(m, missing->v, sizeof(miss_t) * (size_t) missing->n);
                    missing->v = m;
                    missing->cap = cap;
                }
                missing->v[missing->n++] = (miss_t) { r, k, px };
            } else missing_top(p, proot, r, k, px);
            continue;
        }
        for (int h = 0; h < hits.n; h++) {
            int x = hits.v[h];
            if (!gat[x]) {
                if (out->n == out->cap) {
                    int cap = out->cap ? out->cap * 2 : 16;
                    grp_t *g = (grp_t *) a_alloc(&c->A, sizeof(grp_t) * (size_t) cap);
                    if (out->n) memcpy(g, out->v, sizeof(grp_t) * (size_t) out->n);
                    out->v = g;
                    out->cap = cap;
                }
                memset(&out->v[out->n], 0, sizeof(grp_t));
                out->v[out->n].x = x;
                out->v[out->n].r = r;
                out->v[out->n].k = k;
                gat[x] = ++out->n;
            }
            il_add(&c->A, &out->v[gat[x] - 1].pxs, px);
        }
    }
}

static void apply_record(pctx_t *p, int xi, const ilist_t *pxs, const crec_t *r, str_t k) {
    bctx_t *c = p->c;
    const doc_t *pv = p->pv;
    const el_t *x = &p->v->v[xi];
    ops_t ops = { 0 };
    for (int i = 0; i < pxs->n; i++) parse_ops(c, pv, &pv->v[pxs->v[i]], &ops);
    if (has_op(&ops, 'r')) {
        if (!r->remove) {
            say(c, "patch %s: %s remove refused - only pool entries may be removed", p->mid, k.s);
            p->refused++;
            int j = 0;
            for (int i = 0; i < ops.n; i++) if (ops.v[i].op != 'r') ops.v[j++] = ops.v[i];
            ops.n = j;
        } else {
            remove_element(p, x);
            p->changed++;
            return;
        }
    }
    apply_ops(p, x, ops.v, ops.n, r, k, NULL, 0);
    ilist_t pk_all = { 0 };
    for (int i = 0; i < pxs->n; i++) for (int j = pv->v[pxs->v[i]].first; j >= 0; j = pv->v[j].next) il_add(&c->A, &pk_all, j);
    misses_t miss = { 0 };
    grps_t groups = { 0 };
    cbuf_t kp = { 0 };
    cb_put(&kp, k.s, (size_t) k.n);
    cb_puts(&kp, "/");
    str_t kprefix = a_str(&c->A, kp.s, (int) kp.n);
    cbuf_free(&kp);
    group(p, xi, &pk_all, r->kids, kprefix, NULL, &groups, &miss);
    cbuf_t added = { 0 };
    int *ends = (int *) a_alloc(&c->A, sizeof(int) * (size_t) (miss.n + 1)), na = 0;
    for (int i = 0; i < miss.n; i++) {
        const miss_t *m = &miss.v[i];
        ops_t kops = { 0 };
        parse_ops(c, pv, &pv->v[m->px], &kops);
        if (!m->r->add || !m->r->nkey) {
            say(c, "patch %s: %s refused - no such entry (this table adds none)", p->mid, m->k.s);
            p->refused++;
            continue;
        }
        if (has_op(&kops, 'r')) {
            say(c, "patch %s: %s remove - no such entry, nothing to remove", p->mid, m->k.s);
            continue;
        }
        int plain = 1;
        for (int j = 0; j < kops.n && plain; j++) plain = kops.v[j].op == 's';
        if (!plain) {
            say(c, "patch %s: %s refused - a new entry carries plain attributes only", p->mid, m->k.s);
            p->refused++;
            continue;
        }
        str_t bad = bad_refs(c, m->r, kops.v, kops.n);
        if (bad.s) {
            say(c, "patch %s: %s refused - %s", p->mid, m->k.s, bad.s);
            p->refused++;
            continue;
        }
        const el_t *pk = &pv->v[m->px];
        cb_put(&added, pv->b + pk->start, (size_t) (pk->stop - pk->start));
        ends[na++] = (int) added.n;
        p->changed++;
    }
    for (int i = 0; i < groups.n; i++) apply_record(p, groups.v[i].x, &groups.v[i].pxs, groups.v[i].r, groups.v[i].k);
    if (na) add_children(p, x, &added, ends, na);
    cbuf_free(&added);
}

static void apply_patch(bctx_t *c, cbuf_t *cur, const cmodf_t *m, const char *base_id, smap_t *owners) {
    doc_t pv, v;
    if (doc_parse(&pv, m->patch, m->patch_n)) {
        say(c, "patch %s: refused - the file does not read as XML (%s)", m->id, pv.err);
        doc_free(&pv);
        return;
    }
    if (doc_parse(&v, cur->s, (long) cur->n)) { doc_free(&pv); doc_free(&v); return; }
    pctx_t p;
    memset(&p, 0, sizeof(p));
    p.c = c; p.v = &v; p.pv = &pv; p.mid = m->id; p.base_id = base_id; p.owners = owners;
    p.nl = newline_of(cur->s, (int) cur->n);
    for (int pr = pv.rfirst; pr >= 0; pr = pv.v[pr].next) {
        const el_t *pre = &pv.v[pr];
        const croot_t *tr = root_of(c->table, &pv, pre);
        str_t rname = name_of(&c->A, &pv, pre);
        if (!tr) {
            say(c, "patch %s: <%s> refused - the file has no such root", m->id, rname.s);
            p.refused++;
            continue;
        }
        int br = -1;
        for (int i = v.rfirst; i >= 0 && br < 0; i = v.v[i].next) if (tag_eq(&v, &v.v[i], &pv, pre)) br = i;
        if (br < 0) {
            say(c, "patch %s: <%s> refused - the base has no such root", m->id, rname.s);
            p.refused++;
            continue;
        }
        ops_t ops = { 0 };
        parse_ops(c, &pv, pre, &ops);
        cbuf_t kb = { 0 };
        cb_printf(&kb, "<%s>", rname.s);
        apply_ops(&p, &v.v[br], ops.v, ops.n, NULL, a_str(&c->A, kb.s, (int) kb.n), tr->rattrs, 1);
        cbuf_free(&kb);
        ilist_t pels = { 0 };
        for (int j = pre->first; j >= 0; j = pv.v[j].next) il_add(&c->A, &pels, j);
        grps_t groups = { 0 };
        group(&p, br, &pels, tr->recs, (str_t) { "", 0 }, pre, &groups, NULL);
        for (int i = 0; i < groups.n; i++) apply_record(&p, groups.v[i].x, &groups.v[i].pxs, groups.v[i].r, groups.v[i].k);
    }
    cbuf_t res = { 0 };
    ed_apply(&p.ed, cur->s, (int) cur->n, &res);
    cbuf_free(cur);
    *cur = res;
    say(c, "patch %s: %d changes, %d refused", m->id, p.changed, p.refused);
    doc_free(&pv);
    doc_free(&v);
}

/* ------------------------------------------------------------ stringtable.sta (S9) */

typedef struct { long long idx; str_t id, name; } lang_t;

typedef struct {
    int root;
    lang_t *langs;
    int nlangs;
    smap_t cats;                            /* category name -> element + 1 (the first of a name) */
    smap_t keys;                            /* "<cat>\x01<key>" -> element + 1 */
} stx_t;

static str_t none_key(arena_t *a, str_t s) { return s.s ? s : a_cstr(a, "\x02None"); }

static str_t st_pair(arena_t *a, str_t c, str_t k) {
    c = none_key(a, c);
    k = none_key(a, k);
    cbuf_t t = { 0 };
    cb_put(&t, c.s, (size_t) c.n);
    cb_put(&t, "\x01", 1);
    cb_put(&t, k.s, (size_t) k.n);
    str_t r = a_str(a, t.s, (int) t.n);
    cbuf_free(&t);
    return r;
}

static int lang_cmp(const void *x, const void *y) {
    const lang_t *a = (const lang_t *) x, *b = (const lang_t *) y;
    if (a->idx != b->idx) return a->idx < b->idx ? -1 : 1;
    int k = strcmp(a->id.s, b->id.s);
    return k ? k : strcmp(a->name.s, b->name.s);
}

/* _st_index; 0, or -1 when the file has no <stringtable> root */
static int st_index(bctx_t *c, const doc_t *v, stx_t *ix) {
    memset(ix, 0, sizeof(*ix));
    ix->root = -1;
    for (int i = v->rfirst; i >= 0 && ix->root < 0; i = v->v[i].next) if (tag_is(v, &v->v[i], "stringtable")) ix->root = i;
    if (ix->root < 0) return -1;
    for (int x = v->v[ix->root].first; x >= 0; x = v->v[x].next) {
        const el_t *e = &v->v[x];
        if (tag_is(v, e, "languages")) {
            int n = 0;
            for (int y = e->first; y >= 0; y = v->v[y].next) n += tag_is(v, &v->v[y], "language");
            lang_t *ls = (lang_t *) a_alloc(&c->A, sizeof(lang_t) * (size_t) (n + 1));
            n = 0;
            for (int y = e->first; y >= 0; y = v->v[y].next) {
                if (!tag_is(v, &v->v[y], "language")) continue;
                str_t idx = get_attr(&c->A, v, &v->v[y], "index", 0);
                str_t id = get_attr(&c->A, v, &v->v[y], "id", 0), nm = get_attr(&c->A, v, &v->v[y], "name", 0);
                ls[n].idx = idx.s && idx.n ? strtoll(ustrip(idx).s, NULL, 10) : 0;
                ls[n].id = id.s ? id : a_cstr(&c->A, "");
                ls[n].name = nm.s ? nm : a_cstr(&c->A, "");
                n++;
            }
            qsort(ls, (size_t) n, sizeof(lang_t), lang_cmp);
            int k = 0;
            for (int i = 0; i < n; i++) if (!s_is(ls[i].name, "Key")) ls[k++] = ls[i];
            ix->langs = ls;
            ix->nlangs = k;
        } else if (tag_is(v, e, "category")) {
            str_t cn = get_attr(&c->A, v, e, "name", 0);
            void **s = smap_at(&c->A, &ix->cats, none_key(&c->A, cn), 1);
            if (!*s) *s = (void *) (size_t) (x + 1);
            for (int y = e->first; y >= 0; y = v->v[y].next) {
                if (!tag_is(v, &v->v[y], "key")) continue;
                void **t = smap_at(&c->A, &ix->keys, st_pair(&c->A, cn, get_attr(&c->A, v, &v->v[y], "name", 0)), 1);
                if (!*t) *t = (void *) (size_t) (y + 1);
            }
        }
    }
    return 0;
}

static int sm_get(smap_t *m, str_t k) { void **s = smap_at(NULL, m, k, 0); return s ? (int) (size_t) *s - 1 : -1; }

typedef struct { int col; const char *t; int tn; } want_t;
typedef struct { str_t cname; str_t kname; want_t *w; int nw; } newkey_t;

static int want_cmp(const void *x, const void *y) { return ((const want_t *) x)->col - ((const want_t *) y)->col; }

/* want[col] = text (a later one replaces) */
static void want_put(arena_t *a, want_t **w, int *n, int *cap, int col, const char *t, int tn) {
    for (int i = 0; i < *n; i++) if ((*w)[i].col == col) { (*w)[i].t = t; (*w)[i].tn = tn; return; }
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 8;
        want_t *v = (want_t *) a_alloc(a, sizeof(want_t) * (size_t) nc);
        if (*n) memcpy(v, *w, sizeof(want_t) * (size_t) *n);
        *w = v;
        *cap = nc;
    }
    (*w)[(*n)++] = (want_t) { col, t, tn };
}
static const want_t *want_get(const want_t *w, int n, int col) { for (int i = 0; i < n; i++) if (w[i].col == col) return &w[i]; return NULL; }

/* the inner bytes of a <string> ("" for <string/>) */
static void inner_of(const doc_t *d, const el_t *s, const char **t, int *tn) {
    if (s->inner_end != s->stop) { *t = d->b + s->tag_end + 1; *tn = s->inner_end - (s->tag_end + 1); }
    else { *t = ""; *tn = 0; }
}

static void st_patch(bctx_t *c, cbuf_t *cur, const char *pb, long pn, const char *mid, int fill, smap_t *owners) {
    doc_t v, pv;
    stx_t ix;
    if (doc_parse(&v, cur->s, (long) cur->n) || st_index(c, &v, &ix)) {
        say(c, "patch %s: refused - the base has no <stringtable>", mid);
        doc_free(&v);
        return;
    }
    if (doc_parse(&pv, pb, pn)) {
        say(c, "patch %s: refused - the file does not read as XML (%s)", mid, pv.err);
        doc_free(&v); doc_free(&pv);
        return;
    }
    const char *nl = newline_of(cur->s, (int) cur->n);
    const char *b = cur->s;
    edits_t ed = { 0 };
    int changed = 0, refused = 0;
    int pr = -1;
    for (int i = pv.rfirst; i >= 0 && pr < 0; i = pv.v[i].next) if (tag_is(&pv, &pv.v[i], "stringtable")) pr = i;
    if (pr < 0) {
        say(c, "patch %s: refused - no <stringtable> root", mid);
        doc_free(&v); doc_free(&pv);
        return;
    }
    newkey_t *nk = NULL;
    int nnk = 0, nkcap = 0;
    for (int pc = pv.v[pr].first; pc >= 0; pc = pv.v[pc].next) {
        if (!tag_is(&pv, &pv.v[pc], "category")) continue;
        str_t cname = get_attr(&c->A, &pv, &pv.v[pc], "name", 0);
        if (sm_get(&ix.cats, none_key(&c->A, cname)) < 0) {
            say(c, "patch %s: category %s refused - a new category (keys go into the game's categories)", mid, PY(cname));
            refused++;
            continue;
        }
        for (int pk = pv.v[pc].first; pk >= 0; pk = pv.v[pk].next) {
            if (!tag_is(&pv, &pv.v[pk], "key")) continue;
            str_t kname = get_attr(&c->A, &pv, &pv.v[pk], "name", 0);
            want_t *w = NULL;
            int nw = 0, wcap = 0, pos = 0;
            for (int ps = pv.v[pk].first; ps >= 0; ps = pv.v[ps].next) {
                const el_t *se = &pv.v[ps];
                if (!tag_is(&pv, se, "string")) continue;
                str_t lang = get_attr(&c->A, &pv, se, "lang", 0);
                int col = pos;
                if (lang.s) {
                    col = -1;
                    str_t ll = lower_of(&c->A, lang);
                    for (int i = 0; i < ix.nlangs && col < 0; i++)
                        if (s_eq(ll, lower_of(&c->A, ix.langs[i].name)) || s_eq(lang, ix.langs[i].id)) col = i;
                }
                pos++;
                if (col < 0) {
                    say(c, "patch %s: %s/%s refused - language %s is not in the file's <languages>", mid, PY(cname), PY(kname), lang.s);
                    refused++;
                    continue;
                }
                const char *t;
                int tn;
                inner_of(&pv, se, &t, &tn);
                want_put(&c->A, &w, &nw, &wcap, col, t, tn);
            }
            int ke = sm_get(&ix.keys, st_pair(&c->A, cname, kname));
            if (ke < 0) {
                if (nnk == nkcap) {
                    int cap = nkcap ? nkcap * 2 : 8;
                    newkey_t *v2 = (newkey_t *) a_alloc(&c->A, sizeof(newkey_t) * (size_t) cap);
                    if (nnk) memcpy(v2, nk, sizeof(newkey_t) * (size_t) nnk);
                    nk = v2;
                    nkcap = cap;
                }
                nk[nnk++] = (newkey_t) { cname, kname, w, nw };
                changed++;
                continue;
            }
            ilist_t strs = { 0 };
            for (int y = v.v[ke].first; y >= 0; y = v.v[y].next) if (tag_is(&v, &v.v[y], "string")) il_add(&c->A, &strs, y);
            if (nw) qsort(w, (size_t) nw, sizeof(want_t), want_cmp);
            want_t *extra = (want_t *) a_alloc(&c->A, sizeof(want_t) * (size_t) (nw + 1));
            int nx = 0;
            for (int i = 0; i < nw; i++) {
                int col = w[i].col;
                if (col < strs.n) {
                    const el_t *s = &v.v[strs.v[col]];
                    const char *old;
                    int on;
                    inner_of(&v, s, &old, &on);
                    if (fill && !bstrip_empty(old, on)) continue;
                    if (on == w[i].tn && !memcmp(old, w[i].t, (size_t) on)) continue;
                    cbuf_t ok = { 0 };
                    cb_printf(&ok, "%s\x01%s\x01%d", PY(none_key(&c->A, cname)), PY(none_key(&c->A, kname)), col);
                    void **slot = smap_at(&c->A, owners, (str_t) { ok.s, (int) ok.n }, 1);
                    cbuf_free(&ok);
                    if (!fill && *slot && strcmp((const char *) *slot, mid))
                        say(c, "conflict %s/%s %s: %s and %s - %s wins", PY(cname), PY(kname),
                            col < ix.nlangs ? ix.langs[col].name.s : "?", (const char *) *slot, mid, mid);
                    *slot = (void *) mid;
                    if (s->inner_end == s->stop) {
                        cbuf_t t = { 0 };
                        cb_puts(&t, "<string>");
                        cb_put(&t, w[i].t, (size_t) w[i].tn);
                        cb_puts(&t, "</string>");
                        ed_addb(&c->A, &ed, s->start, s->stop, &t);
                        cbuf_free(&t);
                    } else ed_add(&c->A, &ed, s->tag_end + 1, s->inner_end, w[i].t, w[i].tn);
                    changed++;
                } else extra[nx++] = w[i];
            }
            if (nx) {
                const el_t *last = strs.n ? &v.v[strs.v[strs.n - 1]] : NULL;
                int ind = last ? indent_of(b, last->start) : -1;
                if (!last || ind < 0) {
                    say(c, "patch %s: %s/%s refused - the key has no <string> line to follow", mid, PY(cname), PY(kname));
                    refused++;
                    continue;
                }
                int at = line_end_after(b, (int) cur->n, last->stop), top = 0;
                for (int i = 0; i < nx; i++) if (extra[i].col > top) top = extra[i].col;
                cbuf_t parts = { 0 };
                for (int col = strs.n; col <= top; col++) {
                    const want_t *g = want_get(extra, nx, col);
                    cb_puts(&parts, nl);
                    cb_put(&parts, b + ind, (size_t) (last->start - ind));
                    cb_puts(&parts, "<string>");
                    if (g) cb_put(&parts, g->t, (size_t) g->tn);
                    cb_puts(&parts, "</string>");
                }
                ed_addb(&c->A, &ed, at, at, &parts);
                cbuf_free(&parts);
                changed += nx;
            }
        }
    }
    /* the new keys, a category at a time in the order the categories stand */
    int *done = (int *) a_alloc(&c->A, sizeof(int) * (size_t) (nnk + 1));
    memset(done, 0, sizeof(int) * (size_t) (nnk + 1));
    for (;;) {
        int best = -1, bstart = 0;
        for (int i = 0; i < nnk; i++) {
            if (done[i]) continue;
            int ce = sm_get(&ix.cats, none_key(&c->A, nk[i].cname));
            if (best < 0 || v.v[ce].start < bstart) { best = i; bstart = v.v[ce].start; }
        }
        if (best < 0) break;
        str_t cname = nk[best].cname;
        int ce = sm_get(&ix.cats, none_key(&c->A, cname));
        ilist_t ks = { 0 };
        for (int y = v.v[ce].first; y >= 0; y = v.v[y].next) if (tag_is(&v, &v.v[y], "key")) il_add(&c->A, &ks, y);
        if (!ks.n) {
            say(c, "patch %s: category %s refused - it has no key to follow", mid, PY(cname));
            for (int i = 0; i < nnk; i++) if (s_eq(none_key(&c->A, nk[i].cname), none_key(&c->A, cname))) done[i] = 1;
            continue;
        }
        const el_t *last = &v.v[ks.v[ks.n - 1]];
        int ki = indent_of(b, last->start);
        str_t kind = ki >= 0 ? (str_t) { b + ki, last->start - ki } : (str_t) { "", 0 };
        int fs = -1;
        for (int y = v.v[ks.v[0]].first; y >= 0 && fs < 0; y = v.v[y].next) if (tag_is(&v, &v.v[y], "string")) fs = y;
        int si = fs >= 0 ? indent_of(b, v.v[fs].start) : -1;
        cbuf_t sind = { 0 };
        if (si >= 0) cb_put(&sind, b + si, (size_t) (v.v[fs].start - si));
        else { cb_put(&sind, kind.s, (size_t) kind.n); cb_puts(&sind, "    "); }
        int at = line_end_after(b, (int) cur->n, last->stop);
        cbuf_t parts = { 0 };
        for (int i = 0; i < nnk; i++) {
            if (done[i] || !s_eq(none_key(&c->A, nk[i].cname), none_key(&c->A, cname))) continue;
            done[i] = 1;
            cb_puts(&parts, nl);
            cb_put(&parts, kind.s, (size_t) kind.n);
            cb_puts(&parts, "<key name=\"");
            cb_puts(&parts, PY(nk[i].kname));
            cb_puts(&parts, "\">");
            for (int col = 0; col < ix.nlangs; col++) {
                const want_t *g = want_get(nk[i].w, nk[i].nw, col);
                cb_puts(&parts, nl);
                cb_put(&parts, sind.s, sind.n);
                cb_puts(&parts, "<string>");
                if (g) cb_put(&parts, g->t, (size_t) g->tn);
                cb_puts(&parts, "</string>");
            }
            cb_puts(&parts, nl);
            cb_put(&parts, kind.s, (size_t) kind.n);
            cb_puts(&parts, "</key>");
        }
        ed_addb(&c->A, &ed, at, at, &parts);
        cbuf_free(&parts);
        cbuf_free(&sind);
    }
    if (!fill) say(c, "patch %s: %d strings changed, %d refused", mid, changed, refused);
    else say(c, "port: %d strings filled", changed);
    cbuf_t res = { 0 };
    ed_apply(&ed, cur->s, (int) cur->n, &res);
    cbuf_free(cur);
    *cur = res;
    doc_free(&v);
    doc_free(&pv);
}

static void build_stringtable(bctx_t *c, const cmodf_t *mods, int n, const char *port, long portn, cbuf_t *cur,
                              char *base, size_t room) {
    const char *base_id = "game";
    int np = 0;
    for (int i = 0; i < n; i++) {
        const cmodf_t *m = &mods[i];
        if (!m->legacy) continue;
        doc_t v;
        stx_t ix;
        int ok = !doc_parse(&v, m->legacy, m->legacy_n) && !st_index(c, &v, &ix);
        doc_free(&v);
        if (!ok) {
            say(c, "legacy %s: refused - it does not read as a <stringtable>; the next one below is used", m->id);
            continue;
        }
        cur->n = 0;
        cb_put(cur, m->legacy, (size_t) m->legacy_n);
        base_id = m->id;
        for (int j = i + 1; j < n; j++)
            if (mods[j].legacy) say(c, "legacy %s: not used - %s replaces the file (the top of MODS wins)", mods[j].id, m->id);
        break;
    }
    smap_t owners = { 0 };
    for (int i = n - 1; i >= 0; i--) if (mods[i].patch) { st_patch(c, cur, mods[i].patch, mods[i].patch_n, mods[i].id, 0, &owners); np++; }
    if (port) st_patch(c, cur, port, portn, "port", 1, &owners);
    int any_legacy = 0;
    for (int i = 0; i < n; i++) any_legacy |= mods[i].legacy != NULL;
    if (any_legacy || np) say(c, "base %s, patches from %d mods", base_id, np);
    snprintf(base, room, "%s", base_id);
}

/* ------------------------------------------------------------ build */

int content_build(const char *fname, const cmodf_t *mods, int n, const cgame_t *game,
                  const char *port, long portn, cbuf_t *out, cbuf_t *lines, char *base, size_t room) {
    bctx_t cx;
    memset(&cx, 0, sizeof(cx));
    bctx_t *c = &cx;
    c->fname = fname;
    c->game = game;
    c->lines = lines;
    long gn = 0;
    c->g = game->file(game->ctx, fname, &gn);
    c->gn = (int) gn;
    out->n = 0;
    cb_put(out, "", 0);
    snprintf(base, room, "none");
    if (!c->g) {
        cb_printf(lines, "%s: the game has no such file\n", fname);
        return -1;
    }
    cb_put(out, c->g, (size_t) c->gn);
    if (!strcmp(fname, STRINGTABLE)) {
        build_stringtable(c, mods, n, port, portn, out, base, room);
        a_free(&c->A);
        doc_free(&c->gdoc);
        return 0;
    }
    c->table = table_of(fname);
    int nl = 0, nc = 0, np = 0;
    int *with = (int *) a_alloc(&c->A, sizeof(int) * (size_t) (n + 1));
    for (int i = 0; i < n; i++) {
        nl += mods[i].legacy != NULL;
        if (c->table && c->table->content && mods[i].content) with[nc++] = i;
        np += mods[i].patch != NULL;
    }
    const char *base_id = "game";
    if (c->table && !c->table->read) {
        int first = -1;
        for (int i = 0; i < n && first < 0; i++) if (mods[i].legacy) first = i;
        if (first >= 0) {
            say(c, "legacy %s: the game does not read this file - handed out as it is", mods[first].id);
            out->n = 0;
            cb_put(out, mods[first].legacy, (size_t) mods[first].legacy_n);
            snprintf(base, room, "%s", mods[first].id);
        } else {
            for (int i = n - 1; i >= 0; i--) if (mods[i].patch) say(c, "patch %s: refused - the game does not read this file", mods[i].id);
            snprintf(base, room, "game");
        }
        a_free(&c->A);
        return 0;
    }
    /* 1. base (1a: a table of another version of the game, merged three ways first) */
    long on = 0;
    const char *ob = c->table && game->other ? game->other(game->ctx, fname, &on) : NULL;
    for (int i = 0; i < n; i++) {
        const cmodf_t *m = &mods[i];
        if (!m->legacy) continue;
        const char *mb = m->legacy;
        long mn = m->legacy_n;
        cbuf_t ver = { 0 };
        if (ob && version_of(c, m->legacy, (int) m->legacy_n, ob, (int) on, m->id, &ver)) { mb = ver.s; mn = (long) ver.n; }
        str_t why = legacy_refusal(c, mb, (int) mn, m->id);
        char extra[700] = "";
        if (!why.s && game->refuse && game->refuse(game->ctx, fname, mb, mn, c->g, c->gn, extra, sizeof(extra)))
            why = a_cstr(&c->A, extra);
        if (why.s) {
            say(c, "legacy %s: refused - %s; the next one below is used", m->id, why.s);
            cbuf_free(&ver);
            continue;
        }
        out->n = 0;
        cb_put(out, mb, (size_t) mn);
        cbuf_free(&ver);
        base_id = m->id;
        for (int j = i + 1; j < n; j++)
            if (mods[j].legacy) say(c, "legacy %s: not used - %s replaces the file (the top of MODS wins)", mods[j].id, m->id);
        break;
    }
    /* 1b. the base's lists of item ids, mended (M1) */
    if (strcmp(base_id, "game") && c->table) {
        cbuf_t mended = { 0 };
        if (mend_lists(c, out->s, (int) out->n, base_id, &mended)) { cbuf_free(out); *out = mended; }
        else cbuf_free(&mended);
    }
    /* 2. completion */
    if (strcmp(base_id, "game") && c->table) {
        cbuf_t done = { 0 };
        int added = complete(c, out->s, (int) out->n, &done);
        if (added) {
            cbuf_free(out);
            *out = done;
            say(c, "legacy %s: an older table - %d records of the game's were missing, added", base_id, added);
        } else cbuf_free(&done);
    }
    /* 3. content */
    if (nc) content_entities(c, out, mods, with, nc);
    /* 4. patch, the weakest first */
    smap_t owners = { 0 };
    for (int i = n - 1; i >= 0; i--) {
        if (!mods[i].patch) continue;
        if (!c->table) { say(c, "patch %s: refused - not a data table the port knows", mods[i].id); continue; }
        apply_patch(c, out, &mods[i], base_id, &owners);
    }
    /* 5. port: the data tables carry no port additions today */
    if (nl || nc || np) say(c, "base %s, content from %d mods, patches from %d mods", base_id, nc, np);
    snprintf(base, room, "%s", base_id);
    a_free(&c->A);
    doc_free(&c->gdoc);
    return 0;
}

/* ================================================================ the device side */
#ifndef CONTENT_CORE_ONLY
/*
 * WHERE THE MODS' FILES COME FROM. usermods.c hands every mod of the run here,
 * in the order of MODS (the strongest first), at its index (content_scan_mod):
 * a mod's top-level files of the tables above in resources-dlc3/ (the PC's
 * Repentance layer) or resources/ - the legacy F, the dlc3 one first - and in
 * content/ and patch/ (UserMods.java takes patch/<table>.xml, patch/stringtable.sta
 * and content/entities2.xml at the top of those folders). Names are matched
 * without case, as the pack matches them.
 *
 * SETTINGS' LAYERS (plan 4.10, part 2): a chosen option's own resources/,
 * content/ and patch/ go in through content_scan_layer, called BEFORE
 * content_scan_mod for that mod (usermods.c, the later-declared setting first).
 * A whole file and content/: the first of a name is the one used, so the
 * option's file stands over the mod's. patch/: they STACK - the option's patch
 * is applied over the mod's own (build_entries), and changes of one mod's
 * layers to one value are no conflict. The layers in use are in the cache's key.
 *
 * WHEN. content_prepare runs once, in the guard's run (the first file the game
 * asks for: the pack is open from then on): every file some mod touches is
 * built, or taken from content-cache/data/ when nothing it was made from has
 * changed - the game's file (its length and hash: a new APK is a new key), each
 * mod file's size and time, the order, the rules' version. content_map then
 * answers the game's requests for repentance-resources/data/<F>.
 *
 * THE RESULT IS CHECKED TOO (M4 on what the mods made, not only on what they
 * brought): the port's loader-exact refusal (usermods.c: the loaders' fixed
 * tables, the bestiary's parent chain) is asked about the finished file. A
 * result it refuses is built again without one mod's content/ and patch/ at a
 * time until one passes; then without any; the game's
 * own file is the last answer. Each step is a line. (The weakest mod is left out
 * first: MODS is the player's word on whose files matter more.)
 */
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>
#include "diag.h"

const void *shim_pack_peek(const char *inner, long *length);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_usermods_table_refusal(const char *fname, const char *buf, long len, const char *game, long glen,
                                char *why, size_t room);

#define CONTENT_RULES 3                     /* the cache's version: a cache of other rules is not used
                                               (2: the item lists mended, patches of the settings' layers stacked) */
#define CM_MAX 256                          /* usermods.c MODS_MAX (hotfix 1.75: 64 -> 256) - every mod may bring tables */
#define CM_PATCHES 18                       /* patches of one table in one mod: its own and a layer's per setting */

typedef struct { char kind; char leaf[48]; char path[900]; long long size, mtime; int scan; } cfile_t;   /* l legacy, c content, p patch */
typedef struct {
    char id[64];
    cfile_t *f;
    int nf, cap;
    cbuf_t check;                           /* its <id>.check.txt section */
    content_sum_t sum;
    char layers[700];                       /* its settings' layers in use (usermods.c), part of the cache's key */
    char last_patched[48];                  /* the table its patch count was last raised for (one table, one count) */
} cmod_t;

typedef struct { char leaf[48]; char path[900]; char base[64]; int same; cbuf_t lines; } cbuilt_t;

static cmod_t g_cm[CM_MAX];
static int g_ncm;
static cbuilt_t *g_built;
static int g_nbuilt;
static int g_prepared;
static char g_mods_dir[700];
static pthread_mutex_t g_content_lock = PTHREAD_MUTEX_INITIALIZER;

static cmod_t *cm_of(const char *id) {
    for (int i = 0; i < g_ncm; i++) if (!strcmp(g_cm[i].id, id)) return &g_cm[i];
    if (g_ncm >= CM_MAX) return NULL;
    cmod_t *m = &g_cm[g_ncm++];
    memset(m, 0, sizeof(*m));
    snprintf(m->id, sizeof(m->id), "%s", id);
    return m;
}

static const cfile_t *cm_file(const cmod_t *m, char kind, const char *leaf) {
    for (int i = 0; i < m->nf; i++) if (m->f[i].kind == kind && !strcmp(m->f[i].leaf, leaf)) return &m->f[i];
    return NULL;
}

/*
 * One folder of a mod: its top-level files of the known tables, as files of this kind; how many were new.
 * Whole files ('l') and content/ ('c'): the first of a name is the one used - a settings layer, scanned before
 * the mod's own folders, stands over them. Patches ('p') STACK (plan 4.10, part 2: the second patch slot): every
 * layer's patch/<F> is kept, the first scanned the strongest, and each is applied over the ones below it.
 */
static int g_scan;
int content_scan_layer(const char *id, const char *dir, char kind) {
    cmod_t *m = cm_of(id);
    DIR *d = m ? opendir(dir) : NULL;
    if (!d) return 0;
    int added = 0, scan = ++g_scan;
    struct dirent *e;
    while ((e = readdir(d))) {
        char leaf[48];
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(leaf)) continue;
        snprintf(leaf, sizeof(leaf), "%s", e->d_name);
        for (char *s = leaf; *s; s++) *s = (char) tolower((unsigned char) *s);
        if (!content_known(leaf)) continue;
        const cfile_t *had = cm_file(m, kind, leaf);
        if (had && kind != 'p') continue;
        int same_scan = 0;
        for (int k = 0; k < m->nf && kind == 'p'; k++)
            same_scan |= m->f[k].kind == 'p' && m->f[k].scan == scan && !strcmp(m->f[k].leaf, leaf);
        if (same_scan) continue;
        char full[1100];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st) || !S_ISREG(st.st_mode)) continue;
        if (m->nf == m->cap) {
            int cap = m->cap ? m->cap * 2 : 16;
            cfile_t *f = (cfile_t *) realloc(m->f, sizeof(cfile_t) * (size_t) cap);
            if (!f) break;
            m->f = f;
            m->cap = cap;
        }
        cfile_t *f = &m->f[m->nf++];
        f->kind = kind;
        snprintf(f->leaf, sizeof(f->leaf), "%s", leaf);
        snprintf(f->path, sizeof(f->path), "%s", full);
        f->size = (long long) st.st_size;
        f->mtime = (long long) st.st_mtime;
        f->scan = scan;
        added++;
    }
    closedir(d);
    return added;
}

void content_scan_mod(const char *mods_dir, const char *id) {
    snprintf(g_mods_dir, sizeof(g_mods_dir), "%s", mods_dir);
    static const struct { const char *dir; char kind; } LAYERS[] = {
        { "resources-dlc3", 'l' }, { "resources", 'l' }, { "content", 'c' }, { "patch", 'p' },
    };
    for (size_t i = 0; i < sizeof(LAYERS) / sizeof(LAYERS[0]); i++) {
        char dir[900];
        snprintf(dir, sizeof(dir), "%s/%s/%s", mods_dir, id, LAYERS[i].dir);
        content_scan_layer(id, dir, LAYERS[i].kind);
    }
}

/* the mod's settings' layers in use ("" none): a changed setting is a new key for every table the mod touches */
void content_mod_layers(const char *id, const char *layers) {
    cmod_t *m = cm_of(id);
    if (m) snprintf(m->layers, sizeof(m->layers), "%s", layers ? layers : "");
}

/* the mod's count of files content.c takes (usermods.c: a mod of patch/ alone is a mod with files) */
int content_mod_files(const char *id) {
    for (int i = 0; i < g_ncm; i++) if (!strcmp(g_cm[i].id, id)) return g_cm[i].nf;
    return 0;
}

static unsigned long long fnv64(const char *b, long n) {
    unsigned long long h = 1469598103934665603ULL;
    for (long i = 0; i < n; i++) h = (h ^ (unsigned char) b[i]) * 1099511628211ULL;
    return h;
}

static char *c_slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n >= 0 ? (char *) malloc((size_t) n + 1) : NULL;
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) { free(b); b = NULL; }
    fclose(f);
    if (b) { b[n] = 0; *len = n; }
    return b;
}

static int c_spit(const char *path, const char *b, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(b, 1, n, f) == n;
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}

static const char *pack_game_file(void *ctx, const char *name, long *len) {
    (void) ctx;
    char inner[300];
    snprintf(inner, sizeof(inner), "repentance-resources/data/%s", name);
    long n = 0;
    const char *d = (const char *) shim_pack_peek(inner, &n);
    *len = d ? n : 0;
    return d;
}

/* the other version's table (Repentance+'s): the port's copy, laid into the pack by portpatch.c (port_patch/index.txt) */
static const char *pack_other_file(void *ctx, const char *name, long *len) {
    (void) ctx;
    char inner[300];
    snprintf(inner, sizeof(inner), "port-version-ref/repplus/%s", name);
    long n = 0;
    const char *d = (const char *) shim_pack_peek(inner, &n);
    *len = d ? n : 0;
    return d;
}

static int port_refuse(void *ctx, const char *fname, const char *buf, long len, const char *game, long glen, char *why, size_t room) {
    (void) ctx;
    return shim_usermods_table_refusal(fname, buf, len, game, glen, why, room);
}

/* the built file whose result the port refuses: again without one mod's content/ and patch/, then without all */
static void result_check(const char *leaf, cmodf_t *mods, int n, const cgame_t *game, const char *port, long portn,
                         cbuf_t *out, cbuf_t *lines, char *base, size_t room, const char *g, long gn) {
    char why[700] = "";
    if ((long) out->n == gn && !memcmp(out->s, g, (size_t) gn)) return;
    if (!shim_usermods_table_refusal(leaf, out->s, (long) out->n, NULL, 0, why, sizeof(why))) return;
    cb_printf(lines, "%s: result refused - %s\n", leaf, why);
    cmodf_t *try = (cmodf_t *) malloc(sizeof(cmodf_t) * (size_t) (n + 1));
    if (!try) return;
    int done = 0;
    for (int k = n - 1; k >= 0 && !done; k--) {           /* the weakest first: the order of MODS says who matters */
        if (!mods[k].content && !mods[k].patch) continue;
        memcpy(try, mods, sizeof(cmodf_t) * (size_t) n);
        try[k].content = try[k].patch = NULL;
        try[k].content_n = try[k].patch_n = 0;
        cbuf_t o2 = { 0 }, l2 = { 0 };
        char b2[64];
        content_build(leaf, try, n, game, port, portn, &o2, &l2, b2, sizeof(b2));
        if (!shim_usermods_table_refusal(leaf, o2.s, (long) o2.n, NULL, 0, why, sizeof(why))) {
            cb_printf(lines, "%s: result built again without content/ and patch/ of %s - it passes\n", leaf, mods[k].id);
            cb_put(lines, l2.s, l2.n);
            cbuf_free(out);
            *out = o2;
            snprintf(base, room, "%s", b2);
            done = 1;
        } else cbuf_free(&o2);
        cbuf_free(&l2);
    }
    if (!done) {
        for (int i = 0; i < n; i++) { try[i] = mods[i]; try[i].content = try[i].patch = NULL; try[i].content_n = try[i].patch_n = 0; }
        cbuf_t o2 = { 0 }, l2 = { 0 };
        char b2[64];
        content_build(leaf, try, n, game, port, portn, &o2, &l2, b2, sizeof(b2));
        if (!shim_usermods_table_refusal(leaf, o2.s, (long) o2.n, NULL, 0, why, sizeof(why))) {
            cb_printf(lines, "%s: result built again without any mod's content/ and patch/ - it passes\n", leaf);
            cbuf_free(out);
            *out = o2;
            snprintf(base, room, "%s", b2);
        } else {
            cb_printf(lines, "%s: result still refused - %s; the game's file is used\n", leaf, why);
            cbuf_free(&o2);
            out->n = 0;
            cb_put(out, g, (size_t) gn);
            snprintf(base, room, "game");
        }
        cbuf_free(&l2);
    }
    free(try);
}

static void mkdir_p(const char *path) {
    char p[1000];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; mkdir(p, 0700); *s = '/'; }
    mkdir(p, 0700);
}

/*
 * M5 (the research notes of 02.10.2026, batch 5 M1-M5): a mod's new variant with a <bestiary> of its own - its page
 * draws the bestiary's anm2path; one the mod and the game do not have is replaced by parent="<type>.0"
 * (the base type's page surely draws) when the game has that record. Done on the mod's content/ bytes
 * before the build; the line says so. And, in the report build, a line per new variant: own page, a
 * parent's, or none.
 */
static int file_reachable(const cmod_t *m, const char *mods_dir, const char *rel) {
    static const char *const DIRS[2] = { "resources-dlc3", "resources" };
    struct stat st;
    for (int i = 0; i < 2; i++) {
        char p[1100];
        snprintf(p, sizeof(p), "%s/%s/%s/%s", mods_dir, m->id, DIRS[i], rel);
        if (!stat(p, &st)) return 1;
    }
    char inner[700];
    long n = 0;
    snprintf(inner, sizeof(inner), "repentance-resources/data/%s", rel);
    return shim_pack_peek(inner, &n) != NULL;
}

static char *bestiary_fix(const cmod_t *m, const char *mods_dir, const char *b, long n, const char *g, long gn,
                          cbuf_t *lines, long *outn) {
    doc_t d, gd;
    if (doc_parse(&d, b, n)) { doc_free(&d); return NULL; }
    int gok = g && !doc_parse(&gd, g, gn);
    arena_t A = { 0 };
    int root = first_whole_root(&d, "entities");
    str_t aroot = root >= 0 ? get_attr(&A, &d, &d.v[root], "anm2root", 0) : NONE;
    if (!aroot.s || !aroot.n) aroot = a_cstr(&A, "gfx/");
    edits_t ed = { 0 };
    for (int i = root >= 0 ? d.v[root].first : -1; i >= 0; i = d.v[i].next) {
        if (!tag_is(&d, &d.v[i], "entity")) continue;
        int bx = -1;
        for (int j = d.v[i].first; j >= 0 && bx < 0; j = d.v[j].next) if (tag_is(&d, &d.v[j], "bestiary")) bx = j;
        if (bx < 0) continue;
        str_t ap = get_attr(&A, &d, &d.v[bx], "anm2path", 0), par = get_attr(&A, &d, &d.v[bx], "parent", 0);
        if (par.s || !ap.s || !ap.n) continue;
        char rel[600];
        snprintf(rel, sizeof(rel), "%s%s", aroot.s, ap.s);
        for (char *s = rel; *s; s++) if (*s == '\\') *s = '/';
        if (file_reachable(m, mods_dir, rel)) continue;
        str_t t = norm_key(&A, get_attr(&A, &d, &d.v[i], "id", 0)), v = norm_key(&A, get_attr(&A, &d, &d.v[i], "variant", 0));
        int have0 = 0;
        for (int k = gok ? gd.rfirst : -1; k >= 0 && !have0; k = gd.v[k].next)
            for (int j = gd.v[k].first; j >= 0 && !have0; j = gd.v[j].next) {
                if (!tag_is(&gd, &gd.v[j], "entity")) continue;
                str_t gt = norm_key(&A, get_attr(&A, &gd, &gd.v[j], "id", 0)), gv = get_attr(&A, &gd, &gd.v[j], "variant", 0);
                have0 = s_eq(gt, t) && (!gv.s || s_is(norm_key(&A, gv), "0"));
            }
        if (!have0) {
            cb_printf(lines, "entities2.xml: content %s: %s.%s: the bestiary's anm2 %s is in neither the mod nor the game, "
                      "and the game has no %s.0 - left as it is\n", m->id, PY(t), PY(v), rel, PY(t));
            continue;
        }
        char repl[120];
        int rn = snprintf(repl, sizeof(repl), "<bestiary parent=\"%s.0\"/>", t.s);
        ed_add(&A, &ed, d.v[bx].start, d.v[bx].stop, repl, rn);
        cb_printf(lines, "entities2.xml: content %s: %s.%s: the bestiary's anm2 %s is in neither the mod nor the game - "
                  "its page goes to %s.0 (parent)\n", m->id, PY(t), PY(v), rel, t.s);
    }
    char *res = NULL;
    if (ed.n) {
        cbuf_t o = { 0 };
        ed_apply(&ed, b, (int) n, &o);
        res = o.s;
        *outn = (long) o.n;
    }
    a_free(&A);
    doc_free(&d);
    if (gok) doc_free(&gd);
    return res;
}

#if ISAAC_DIAG
/* the report build: what the bestiary does with each new variant a mod's content/ brought */
static void bestiary_report(const char *id, const char *b, long n) {
    doc_t d = { 0 };
    if (!shim_report_active() || doc_parse(&d, b, n)) { doc_free(&d); return; }
    arena_t A = { 0 };
    int root = first_whole_root(&d, "entities");
    for (int i = root >= 0 ? d.v[root].first : -1; i >= 0; i = d.v[i].next) {
        if (!tag_is(&d, &d.v[i], "entity")) continue;
        const el_t *x = &d.v[i];
        str_t t = norm_key(&A, get_attr(&A, &d, x, "id", 0)), v = get_attr(&A, &d, x, "variant", 0);
        v = v.s ? norm_key(&A, v) : a_cstr(&A, "0");
        int bx = -1;
        for (int j = x->first; j >= 0 && bx < 0; j = d.v[j].next) if (tag_is(&d, &d.v[j], "bestiary")) bx = j;
        str_t par = bx >= 0 ? get_attr(&A, &d, &d.v[bx], "parent", 0) : NONE;
        str_t flag = get_attr(&A, &d, x, "bestiary", 1), port = get_attr(&A, &d, x, "portrait", 1), hp = get_attr(&A, &d, x, "baseHP", 1);
        int own = bx >= 0 ? !par.s : flag.s && (s_is(flag, "1") || s_is(lower_of(&A, flag), "true"));
        int hidden = !port.s || !port.n || (hp.s && atof(hp.s) == 0.0);
        char line[400];
        snprintf(line, sizeof(line), "bestiary of %s: variant %s.%s - %s%s", id, PY(t), PY(v),
                 par.s ? "through its parent" : own ? "a page of its own" : "no page (no <bestiary>)",
                 own && hidden ? ", not shown in the menu (no portrait or baseHP=0)" : "");
        if (par.s) { size_t k = strlen(line); snprintf(line + k, sizeof(line) - k, " %s", par.s); }
        report_line("content", line);
    }
    a_free(&A);
    doc_free(&d);
}
#endif

int content_mod_has(const char *id, const char *leaf);

static void sum_line(const cbuilt_t *bt, const char *line) {
    if (strlen(line) < strlen(bt->leaf) + 2) return;
    const char *rest = line + strlen(bt->leaf) + 2;
    static const char *const KIND[4] = { "legacy ", "patch ", "content ", "base " };
    for (int i = 0; i < g_ncm; i++) {
        cmod_t *m = &g_cm[i];
        char pat[100];
        int kind = -1, mine = 0;
        for (int k = 0; k < 4 && kind < 0; k++) {
            snprintf(pat, sizeof(pat), "%s%s%s", KIND[k], m->id, k == 3 ? "," : ":");
            if (!strncmp(rest, pat, strlen(pat))) kind = k;
        }
        if (kind >= 0) mine = 1;
        else if (!strncmp(rest, "conflict ", 9)) {
            char a[100], b2[100];
            snprintf(a, sizeof(a), ": %s and ", m->id);
            snprintf(b2, sizeof(b2), " and %s - ", m->id);
            if (strstr(rest, a) || strstr(rest, b2)) { mine = 1; m->sum.conflicts++; }
        } else if (!strncmp(rest, "result ", 7) && content_mod_has(m->id, bt->leaf)) {
            /* the finished file refused: every mod of it is told; the one left out counts it as not used */
            char left[120];
            snprintf(left, sizeof(left), " of %s - it passes", m->id);
            mine = 1;
            if (strstr(rest, left)) m->sum.refused++;
        }
        if (!mine) continue;
        if (!m->check.n)
            cb_puts(&m->check, "\nData tables built by the port from the game's file and this mod's resources/, content/ "
                               "and patch/ (the order of MODS decides; each line: the file, then what happened):\n");
        cb_puts(&m->check, line);
        cb_puts(&m->check, "\n");
        if (kind < 0) continue;
        const char *what = rest + strlen(KIND[kind]) + strlen(m->id) + 1;
        if (kind == 3 && strcmp(bt->base, "game")) m->sum.replaced++;
        if (kind == 0 && (!strncmp(what, " refused", 8) || !strncmp(what, " not used", 9))) m->sum.refused++;
        /* M1: " <who>: <attr> <id> does not exist - left out" */
        static const char LEFT_OUT[] = " does not exist - left out";
        size_t wl = strlen(what), ll = sizeof(LEFT_OUT) - 1;
        if (kind == 0 && wl > ll + 2 && !strcmp(what + wl - ll, LEFT_OUT)) {
            if (!m->sum.mended++) {
                char body[600];
                snprintf(body, sizeof(body), "%.*s", (int) (wl - ll - 1), what + 1);
                char *sp = strrchr(body, ' ');
                char *colon = NULL;
                for (char *q = strstr(body, ": "); q; q = strstr(q + 1, ": ")) colon = q;
                if (sp) snprintf(m->sum.mend_item, sizeof(m->sum.mend_item), "%s", sp + 1);
                if (colon) { *colon = 0; snprintf(m->sum.mend_who, sizeof(m->sum.mend_who), "%s", body); }
            }
        }
        /* a table counts once, however many of the mod's layers patch it */
        if (kind == 1 && ((strstr(what, " changes, ") && strncmp(what, " 0 changes", 10))
                          || (strstr(what, " strings changed, ") && strncmp(what, " 0 strings", 10)))
                && strcmp(m->last_patched, bt->leaf)) {
            m->sum.patched++;
            snprintf(m->last_patched, sizeof(m->last_patched), "%s", bt->leaf);
        }
        if (kind == 2 && strstr(what, " new entities merged") && strncmp(what, " 0 new", 6)) m->sum.merged++;
        /* 1a (04.10.2026): " a table of Repentance+ - <n> of its differences are Repentance+'s, ..." */
        static const char VER[] = " a table of Repentance+ - ";
        if (kind == 0 && !strncmp(what, VER, sizeof(VER) - 1)) {
            m->sum.versioned += atoi(what + sizeof(VER) - 1);
            m->sum.version_tables++;
        }
        const char *ov = kind == 1 ? strstr(what, "overridden by legacy ") : NULL;
        if (ov) {
            m->sum.overridden++;
            const char *l = ov + 21, *sp = strstr(l, " (its file");
            size_t ln = sp ? (size_t) (sp - l) : strlen(l);
            if (ln >= sizeof(m->sum.legacy)) ln = sizeof(m->sum.legacy) - 1;
            memcpy(m->sum.legacy, l, ln);
            m->sum.legacy[ln] = 0;
        }
    }
}

/* a built file's lines: into each mod's check text and counts, and (the report build) into the report */
static void sum_lines(const cbuilt_t *bt) {
    const char *s = bt->lines.s ? bt->lines.s : "";
    while (*s) {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t) (e - s) : strlen(s);
        char line[1600];
        snprintf(line, sizeof(line), "%.*s", (int) len, s);
        sum_line(bt, line);
#if ISAAC_DIAG
        __android_log_print(ANDROID_LOG_INFO, "isaac", "content: %s", line);
        if (shim_report_active()) report_line("content", line);
#endif
        s += len + (e ? 1 : 0);
    }
}

/*
 * The mods of one build, strongest first, as content_build takes them. A mod is one entry - its whole file,
 * its content/ and its WEAKEST patch (the mod's own) - and, before it, one entry per further patch of its
 * settings' layers, the strongest first (the second patch slot): content_build applies patches from the
 * weakest up, so a layer's patch lands on the mod's own. The entries of one mod share its id, so their
 * changes to one value are no "conflict" between mods.
 */
typedef struct { int cm; const cfile_t *file[3]; } bentry_t;

static int build_entries(const char *leaf, bentry_t **out) {
    int cap = 0;
    for (int i = 0; i < g_ncm; i++)
        for (int k = 0; k < g_cm[i].nf; k++) cap += !strcmp(g_cm[i].f[k].leaf, leaf);
    bentry_t *e = (bentry_t *) calloc((size_t) cap + 1, sizeof(bentry_t));
    int n = 0;
    for (int i = 0; e && i < g_ncm; i++) {
        const cmod_t *m = &g_cm[i];
        const cfile_t *lf = cm_file(m, 'l', leaf), *cf = cm_file(m, 'c', leaf), *pf[CM_PATCHES];
        int np = 0;
        for (int k = 0; k < m->nf && np < CM_PATCHES; k++)
            if (m->f[k].kind == 'p' && !strcmp(m->f[k].leaf, leaf)) pf[np++] = &m->f[k];
        if (!lf && !cf && !np) continue;
        for (int k = 0; k + 1 < np; k++) {           /* the layers' patches over the mod's own, strongest first */
            e[n].cm = i;
            e[n].file[2] = pf[k];
            n++;
        }
        e[n].cm = i;
        e[n].file[0] = lf;
        e[n].file[1] = cf;
        e[n].file[2] = np ? pf[np - 1] : NULL;
        n++;
    }
    *out = e;
    return n;
}

static void build_one(const char *cache_dir, const char *mods_dir, const char *leaf) {
    long gn = 0;
    const char *g = pack_game_file(NULL, leaf, &gn);
    if (!g) return;                         /* the game has no such file: the usual lookup answers */
    bentry_t *ent = NULL;
    int n = build_entries(leaf, &ent);
    if (!ent) return;
    cbuf_t key = { 0 };
    cb_printf(&key, "content %d\nfile %s\ngame %ld %016llx\n", CONTENT_RULES, leaf, gn, fnv64(g, gn));
    {   /* the other version's table (1a): a new port's copy is a new key */
        long on = 0;
        const char *ob = pack_other_file(NULL, leaf, &on);
        if (ob) cb_printf(&key, "other %ld %016llx\n", on, fnv64(ob, on));
    }
    for (int j = 0; j < n; j++) {
        const cmod_t *m = &g_cm[ent[j].cm];
        for (int k = 0; k < 3; k++)
            if (ent[j].file[k]) cb_printf(&key, "mod %s %c %lld %lld %s\n", m->id, "lcp"[k], ent[j].file[k]->size,
                                          ent[j].file[k]->mtime, ent[j].file[k]->path);
        if (m->layers[0] && (j + 1 == n || ent[j + 1].cm != ent[j].cm)) cb_printf(&key, "layers %s %s\n", m->id, m->layers);
    }
    long portn = 0;
    const char *port = !strcmp(leaf, STRINGTABLE) ? content_port_stringtable(&portn) : NULL;
    if (port) cb_printf(&key, "port %ld %016llx\n", portn, fnv64(port, portn));
    cb_puts(&key, "--\n");
    char path[1000], note[1000];
    snprintf(path, sizeof(path), "%s/data/%s", cache_dir, leaf);
    snprintf(note, sizeof(note), "%s/data/%s.txt", cache_dir, leaf);
    cbuilt_t *bt = &g_built[g_nbuilt];
    memset(bt, 0, sizeof(*bt));
    snprintf(bt->leaf, sizeof(bt->leaf), "%s", leaf);
    snprintf(bt->path, sizeof(bt->path), "%s", path);
    /* the cache: the same key, and the built file of the length it says */
    long cn = 0;
    char *cached = c_slurp(note, &cn);
    if (cached && (size_t) cn > key.n && !memcmp(cached, key.s, key.n)) {
        const char *body = cached + key.n, *sep = strstr(body, "--\n");
        long len = -1;
        int same = 0;
        char base[64] = "", ownp[900] = "";
        const char *op = sep ? strstr(sep, "\nown ") : NULL;
        if (op) sscanf(op + 5, "%899[^\n]", ownp);
        if (sep && sscanf(sep + 3, "base %63s\nsame %d\nlen %ld", base, &same, &len) == 3) {
            struct stat st;
            if (ownp[0]) snprintf(bt->path, sizeof(bt->path), "%s", ownp);
            if (same || (!stat(bt->path, &st) && (long) st.st_size == len)) {
                cb_put(&bt->lines, body, (size_t) (sep - body));
                snprintf(bt->base, sizeof(bt->base), "%s", base);
                bt->same = same;
                g_nbuilt++;
                free(cached);
                cbuf_free(&key);
                free(ent);
                return;
            }
        }
    }
    free(cached);
    cmodf_t *mods = (cmodf_t *) calloc((size_t) n + 1, sizeof(cmodf_t));
    char **own = (char **) calloc((size_t) n * 3 + 1, sizeof(char *));
    long *ownn = (long *) calloc((size_t) n * 3 + 1, sizeof(long));
    if (!mods || !own || !ownn) { free(mods); free(own); free(ownn); free(ent); cbuf_free(&key); return; }
    for (int j = 0; j < n; j++) {
        const cmod_t *m = &g_cm[ent[j].cm];
        for (int k = 0; k < 3; k++) if (ent[j].file[k]) own[j * 3 + k] = c_slurp(ent[j].file[k]->path, &ownn[j * 3 + k]);
        if (own[j * 3 + 1] && !strcmp(leaf, "entities2.xml")) {
            long fn = 0;
            char *fixed = bestiary_fix(m, mods_dir, own[j * 3 + 1], ownn[j * 3 + 1], g, gn, &bt->lines, &fn);
            if (fixed) { free(own[j * 3 + 1]); own[j * 3 + 1] = fixed; ownn[j * 3 + 1] = fn; }
#if ISAAC_DIAG
            bestiary_report(m->id, own[j * 3 + 1], ownn[j * 3 + 1]);
#endif
        }
        mods[j].id = m->id;
        mods[j].legacy = own[j * 3]; mods[j].legacy_n = ownn[j * 3];
        mods[j].content = own[j * 3 + 1]; mods[j].content_n = ownn[j * 3 + 1];
        mods[j].patch = own[j * 3 + 2]; mods[j].patch_n = ownn[j * 3 + 2];
    }
    cgame_t game = { pack_game_file, NULL, port_refuse, pack_other_file };
    cbuf_t out = { 0 };
    content_build(leaf, mods, n, &game, port, portn, &out, &bt->lines, bt->base, sizeof(bt->base));
    result_check(leaf, mods, n, &game, port, portn, &out, &bt->lines, bt->base, sizeof(bt->base), g, gn);
    bt->same = (long) out.n == gn && !memcmp(out.s, g, (size_t) gn);
    /* the base mod's own file, as it is (nothing completed, mended, patched or merged): handed out from its
       place, so a crash right after it names the mod's file itself */
    char own_path[900] = "";
    for (int j = 0; j < n && !bt->same; j++) {
        const cfile_t *lf = ent[j].file[0];
        if (!strcmp(mods[j].id, bt->base) && lf && own[j * 3] && (size_t) ownn[j * 3] == out.n
                && !memcmp(own[j * 3], out.s, out.n)) snprintf(own_path, sizeof(own_path), "%s", lf->path);
    }
    if (own_path[0]) snprintf(bt->path, sizeof(bt->path), "%s", own_path);
    int ok = bt->same || own_path[0] || !c_spit(path, out.s, out.n);
    if (!ok) {
        cb_printf(&bt->lines, "%s: the built file could not be written (%s) - the game's file is used\n", leaf, strerror(errno));
        bt->same = 1;
    }
    cbuf_t txt = { 0 };
    cb_put(&txt, key.s, key.n);
    cb_put(&txt, bt->lines.s, bt->lines.n);
    cb_printf(&txt, "--\nbase %s\nsame %d\nlen %ld\nown %s\n", bt->base, bt->same, bt->same ? -1L : (long) out.n, own_path);
    if (ok) c_spit(note, txt.s, txt.n);
    cbuf_free(&txt);
    cbuf_free(&out);
    for (int j = 0; j < n * 3; j++) free(own[j]);
    free(own);
    free(ownn);
    free(mods);
    free(ent);
    cbuf_free(&key);
    g_nbuilt++;
}

void content_prepare(const char *cache_dir) {
    pthread_mutex_lock(&g_content_lock);
    if (g_prepared) { pthread_mutex_unlock(&g_content_lock); return; }
    g_prepared = 1;
    /* the files some mod touches, in the order first met; and stringtable.sta always - the port's own keys are
       filled into the game's file with no mod on too (plan 30.09, M8: the pack holds the game's file as it is) */
    char leaves[128][48];
    int nl = 0;
    snprintf(leaves[nl++], sizeof(leaves[0]), "%s", STRINGTABLE);
    for (int i = 0; i < g_ncm; i++)
        for (int k = 0; k < g_cm[i].nf; k++) {
            int have = 0;
            for (int j = 0; j < nl && !have; j++) have = !strcmp(leaves[j], g_cm[i].f[k].leaf);
            if (!have && nl < 128) snprintf(leaves[nl++], sizeof(leaves[0]), "%s", g_cm[i].f[k].leaf);
        }
    char dir[900];
    snprintf(dir, sizeof(dir), "%s/data", cache_dir);
    mkdir_p(dir);
    g_built = (cbuilt_t *) calloc((size_t) nl, sizeof(cbuilt_t));
    for (int j = 0; g_built && j < nl; j++) build_one(cache_dir, g_mods_dir, leaves[j]);
    for (int j = 0; j < g_nbuilt; j++) sum_lines(&g_built[j]);
    pthread_mutex_unlock(&g_content_lock);
}

const char *content_map(const char *leaf, int *game) {
    *game = 0;
    for (int j = 0; j < g_nbuilt; j++) {
        if (strcasecmp(g_built[j].leaf, leaf)) continue;
        if (g_built[j].same) { *game = 1; return NULL; }
        return g_built[j].path;
    }
    return NULL;
}

/* the mod brings a file of this table (any kind): it may be blamed for a crash right after the built file */
int content_mod_has(const char *id, const char *leaf) {
    for (int i = 0; i < g_ncm; i++)
        if (!strcmp(g_cm[i].id, id))
            for (int k = 0; k < g_cm[i].nf; k++) if (!strcasecmp(g_cm[i].f[k].leaf, leaf)) return 1;
    return 0;
}

const char *content_check_text(const char *id) {
    for (int i = 0; i < g_ncm; i++) if (!strcmp(g_cm[i].id, id)) return g_cm[i].check.s ? g_cm[i].check.s : "";
    return "";
}

int content_summary(const char *id, content_sum_t *s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < g_ncm; i++)
        if (!strcmp(g_cm[i].id, id)) {
            *s = g_cm[i].sum;
            return s->replaced || s->patched || s->merged || s->refused || s->conflicts || s->overridden || s->mended
                || s->versioned;
        }
    return 0;
}

#endif /* CONTENT_CORE_ONLY */
