/*
 * The port's additions laid over the game's own files at start (plan 30.09,
 * 4.9 M8).
 *
 * The game's files in the pack are the IPA's, untouched. What the port adds -
 * the straps, its rows in the settings, the MODS item, the D-pad and the twin
 * buttons, its retouches of a few sheets - comes from the assets folder
 * port_patch/, laid into the same table the pack lives in, under the game's
 * own paths, before the game opens a single file. pack.c's put() puts a new
 * entry at the head of its chain, so what is laid here is what is found.
 *
 * port_patch/index.txt (written by tools/port_patch.py) says what goes where:
 *
 *   file     <target> <asset>          a file of the port's own (its sheets...)
 *   over     <target> <asset>          the port's version of a game file; a mod's
 *                                      own still wins - mods are mapped before
 *                                      the pack is asked (uikit.c shim_map_path)
 *   patch    <target> <asset> [<base>] an .anm2 patch, over the file the pack
 *                                      holds at <target> (or at <base>)
 *   variants <target> <pct,...>        the stick size variants, from the patched
 *                                      <target> (sticks.c asks for them)
 *
 * The patch format and its rules are tools/anm2patch.py's, and this file does
 * exactly what that one does, down to the byte: tools/portdesk compiles this
 * very file on the desk and compares (pp_apply, pp_variant).
 *
 * The NUMBERS. A patch names everything by name; the numbers the port's code
 * still needs - the layer a word of the image sets a frame of, the strap
 * layers straps.c draws - are read back from the file the game will actually
 * load: shim_port_layer / shim_port_null. A file where the name is missing, or
 * language copies that disagree, answer -1 and say so in the report; the
 * caller then leaves its change out instead of drawing someone else's layer.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "diag.h"

#ifndef PORTPATCH_DESK
#include <pthread.h>
#include <android/log.h>
#include <android/asset_manager.h>
#endif

#if ISAAC_DIAG && !defined(PORTPATCH_DESK)
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

void report_line(const char *kind, const char *text);
void shim_pack_add(const char *path, const void *data, long length);
const void *shim_pack_peek(const char *inner, long *length);
const char *shim_pack_root(void);

/*
 * A mod's file that would win over the pack for this absolute path, asked
 * without side effects (usermods.c may define it; until then the pack's file
 * is the one loaded - usermods.c BY_NUMBER keeps every patched .anm2 the game's).
 */
const char *shim_usermods_peek(const char *path) __attribute__((weak));

/* ================================================================== arena */

typedef struct chunk { struct chunk *next; size_t used, cap; char data[]; } chunk_t;
typedef struct { chunk_t *head; } arena_t;

static void *ar_alloc(arena_t *a, size_t n) {
    n = (n + 15) & ~(size_t) 15;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = n > 65536 ? n : 65536;
        chunk_t *c = (chunk_t *) malloc(sizeof(chunk_t) + cap);
        if (!c) return NULL;
        c->next = a->head; c->used = 0; c->cap = cap;
        a->head = c;
    }
    void *p = a->head->data + a->head->used;
    a->head->used += n;
    return p;
}

static void ar_free(arena_t *a) {
    while (a->head) { chunk_t *n = a->head->next; free(a->head); a->head = n; }
}

static char *ar_str(arena_t *a, const char *s, size_t n) {
    char *d = (char *) ar_alloc(a, n + 1);
    if (!d) return NULL;
    memcpy(d, s, n); d[n] = 0;
    return d;
}

/* ================================================================== the tree */

typedef struct { const char *name; size_t nl; const char *val; size_t vl; char q; } attr_t;

typedef struct node {
    const char *tag; size_t tl;
    attr_t *at; int na, ca;
    struct node **kid; int nk, ck;
} node_t;

typedef struct {
    arena_t ar;
    const char *prolog; size_t prolog_len;
    node_t *root;
    int crlf;
    int failed;                 /* out of memory somewhere */
} doc_t;

static node_t *nd_new(doc_t *d, const char *tag, size_t tl) {
    node_t *n = (node_t *) ar_alloc(&d->ar, sizeof(node_t));
    if (!n) { d->failed = 1; return NULL; }
    memset(n, 0, sizeof(*n));
    n->tag = tag; n->tl = tl;
    return n;
}

static int nd_tag(const node_t *n, const char *tag) {
    size_t l = strlen(tag);
    return n && n->tl == l && !memcmp(n->tag, tag, l);
}

static void at_push(doc_t *d, node_t *n, const char *name, size_t nl, const char *val, size_t vl, char q) {
    if (n->na == n->ca) {
        int c = n->ca ? n->ca * 2 : 8;
        attr_t *x = (attr_t *) ar_alloc(&d->ar, sizeof(attr_t) * (size_t) c);
        if (!x) { d->failed = 1; return; }
        if (n->na) memcpy(x, n->at, sizeof(attr_t) * (size_t) n->na);
        n->at = x; n->ca = c;
    }
    attr_t *a = &n->at[n->na++];
    a->name = name; a->nl = nl; a->val = val; a->vl = vl; a->q = q;
}

static void kid_insert(doc_t *d, node_t *n, int at, node_t *k) {
    if (!k) return;
    if (n->nk == n->ck) {
        int c = n->ck ? n->ck * 2 : 8;
        node_t **x = (node_t **) ar_alloc(&d->ar, sizeof(node_t *) * (size_t) c);
        if (!x) { d->failed = 1; return; }
        if (n->nk) memcpy(x, n->kid, sizeof(node_t *) * (size_t) n->nk);
        n->kid = x; n->ck = c;
    }
    if (at < 0 || at > n->nk) at = n->nk;
    memmove(n->kid + at + 1, n->kid + at, sizeof(node_t *) * (size_t) (n->nk - at));
    n->kid[at] = k;
    n->nk++;
}

static const attr_t *nd_attr(const node_t *n, const char *name) {
    size_t l = strlen(name);
    for (int i = 0; n && i < n->na; i++)
        if (n->at[i].nl == l && !memcmp(n->at[i].name, name, l)) return &n->at[i];
    return NULL;
}

static int same_val(const attr_t *a, const attr_t *b) {
    if (!a || !b) return a == b;
    return a->vl == b->vl && !memcmp(a->val, b->val, a->vl);
}

static void nd_set(doc_t *d, node_t *n, const char *name, size_t nl, const char *val, size_t vl) {
    for (int i = 0; i < n->na; i++)
        if (n->at[i].nl == nl && !memcmp(n->at[i].name, name, nl)) {
            n->at[i].val = val; n->at[i].vl = vl;
            return;
        }
    at_push(d, n, name, nl, val, vl, '"');
}

static void nd_set_int(doc_t *d, node_t *n, const char *name, long v) {
    char b[24];
    int l = snprintf(b, sizeof(b), "%ld", v);
    char *s = ar_str(&d->ar, b, (size_t) l);
    if (!s) { d->failed = 1; return; }
    nd_set(d, n, name, strlen(name), s, (size_t) l);
}

static node_t *nd_find(const node_t *n, const char *tag) {
    for (int i = 0; n && i < n->nk; i++) if (nd_tag(n->kid[i], tag)) return n->kid[i];
    return NULL;
}

static node_t *nd_copy(doc_t *d, const node_t *s) {
    node_t *n = nd_new(d, s->tag, s->tl);
    if (!n) return NULL;
    for (int i = 0; i < s->na; i++) at_push(d, n, s->at[i].name, s->at[i].nl, s->at[i].val, s->at[i].vl, s->at[i].q);
    for (int i = 0; i < s->nk; i++) kid_insert(d, n, -1, nd_copy(d, s->kid[i]));
    return n;
}

static long num_of(const attr_t *a) {
    if (!a || !a->vl) return -1;
    size_t i = 0;
    int neg = 0;
    if (a->val[0] == '-' || a->val[0] == '+') { neg = a->val[0] == '-'; i = 1; }
    if (i >= a->vl) return -1;
    long v = 0;
    for (; i < a->vl; i++) {
        if (a->val[i] < '0' || a->val[i] > '9') return -1;
        v = v * 10 + (a->val[i] - '0');
        if (v > 100000000L) return -1;
    }
    return neg ? -v : v;
}

/* ================================================================== reader (anm2patch.parse) */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static long tag_end(const char *b, long p, long lim) {
    char q = 0;
    for (p++; p < lim; p++) {
        char c = b[p];
        if (q) { if (c == q) q = 0; }
        else if (c == '"' || c == '\'') q = c;
        else if (c == '>') return p;
    }
    return -1;
}

static long find_str(const char *b, long from, long lim, const char *s) {
    size_t l = strlen(s);
    for (long p = from; p + (long) l <= lim; p++)
        if (!memcmp(b + p, s, l)) return p;
    return -1;
}

static void parse_attrs(doc_t *d, node_t *n, const char *b, long p, long end) {
    for (;;) {
        while (p < end && is_space(b[p])) p++;
        if (p >= end) break;
        long ns = p;
        while (p < end && b[p] != '=' && !is_space(b[p])) p++;
        long ne = p;
        while (p < end && is_space(b[p])) p++;
        if (p >= end || b[p] != '=') break;
        p++;
        while (p < end && is_space(b[p])) p++;
        if (p >= end || (b[p] != '"' && b[p] != '\'')) break;
        char q = b[p++];
        long vs = p;
        while (p < end && b[p] != q) p++;
        if (p >= end) break;
        at_push(d, n, b + ns, (size_t) (ne - ns), b + vs, (size_t) (p - vs), q);
        p++;
    }
}

/* 0 on success; the tree points into b, which must outlive it */
static int doc_parse(doc_t *d, const char *b, long lim) {
    memset(d, 0, sizeof(*d));
    node_t *stack[64];
    int sp = 0;
    long root_at = -1;
    long p = 0;
    while (p < lim) {
        const char *x = memchr(b + p, '<', (size_t) (lim - p));
        if (!x) break;
        long lt = x - b;
        if (lim - lt >= 4 && !memcmp(b + lt, "<!--", 4)) {
            long e = find_str(b, lt + 4, lim, "-->");
            if (e < 0) return -1;
            p = e + 3;
            continue;
        }
        if (lt + 1 < lim && b[lt + 1] == '?') {
            long e = find_str(b, lt + 2, lim, "?>");
            if (e < 0) return -1;
            p = e + 2;
            continue;
        }
        if (lt + 1 < lim && b[lt + 1] == '!') {
            long e = tag_end(b, lt, lim);
            if (e < 0) return -1;
            p = e + 1;
            continue;
        }
        if (lt + 1 < lim && b[lt + 1] == '/') {
            const char *e = memchr(b + lt, '>', (size_t) (lim - lt));
            if (!e) return -1;
            if (!sp) return -1;
            sp--;
            p = (e - b) + 1;
            if (!sp) break;
            continue;
        }
        long q = lt + 1;
        while (q < lim && !is_space(b[q]) && b[q] != '/' && b[q] != '>') q++;
        if (q == lt + 1) { p = lt + 1; continue; }
        long gt = tag_end(b, lt, lim);
        if (gt < 0) return -1;
        int selfc = b[gt - 1] == '/' && gt - 1 >= q;
        node_t *n = nd_new(d, b + lt + 1, (size_t) (q - lt - 1));
        if (!n) return -1;
        parse_attrs(d, n, b, q, selfc ? gt - 1 : gt);
        if (sp) kid_insert(d, stack[sp - 1], -1, n);
        else if (!d->root) { d->root = n; root_at = lt; }
        else return -1;
        if (!selfc) {
            if (sp == 64) return -1;
            stack[sp++] = n;
        } else if (!sp && d->root == n) break;
        p = gt + 1;
    }
    if (!d->root || sp || d->failed) return -1;
    d->prolog = b;
    d->prolog_len = (size_t) root_at;
    const char *nl = memchr(b, '\n', (size_t) lim);
    d->crlf = nl && nl > b && nl[-1] == '\r';
    return 0;
}

/* ================================================================== writer (anm2patch.serialize) */

typedef struct { char *b; size_t n, cap; int failed; } buf_t;

static void put_bytes(buf_t *o, const void *s, size_t n) {
    if (o->failed) return;
    if (o->n + n > o->cap) {
        size_t c = o->cap ? o->cap : 65536;
        while (c < o->n + n) c *= 2;
        char *x = (char *) realloc(o->b, c);
        if (!x) { o->failed = 1; return; }
        o->b = x; o->cap = c;
    }
    memcpy(o->b + o->n, s, n);
    o->n += n;
}

static void put_s(buf_t *o, const char *s) { put_bytes(o, s, strlen(s)); }

static void write_node(buf_t *o, const doc_t *d, const node_t *n, int depth) {
    const char *nl = d->crlf ? "\r\n" : "\n";
    for (int i = 0; i < depth; i++) put_bytes(o, "\t", 1);
    put_bytes(o, "<", 1);
    put_bytes(o, n->tag, n->tl);
    for (int i = 0; i < n->na; i++) {
        put_bytes(o, " ", 1);
        put_bytes(o, n->at[i].name, n->at[i].nl);
        put_bytes(o, "=", 1);
        put_bytes(o, &n->at[i].q, 1);
        put_bytes(o, n->at[i].val, n->at[i].vl);
        put_bytes(o, &n->at[i].q, 1);
    }
    if (!n->nk) { put_s(o, " />"); put_s(o, nl); return; }
    put_s(o, ">"); put_s(o, nl);
    for (int i = 0; i < n->nk; i++) write_node(o, d, n->kid[i], depth + 1);
    for (int i = 0; i < depth; i++) put_bytes(o, "\t", 1);
    put_s(o, "</");
    put_bytes(o, n->tag, n->tl);
    put_s(o, ">"); put_s(o, nl);
}

static int doc_write(const doc_t *d, buf_t *o) {
    memset(o, 0, sizeof(*o));
    put_bytes(o, d->prolog, d->prolog_len);
    write_node(o, d, d->root, 0);
    if (o->failed) { free(o->b); memset(o, 0, sizeof(*o)); return -1; }
    return 0;
}

/* ================================================================== the model */

typedef struct { node_t *content, *anims, *sheets, *layers, *nulls; } model_t;

static int ord_of(const node_t *k, const char *const *order) {
    for (int i = 0; order[i]; i++) if (nd_tag(k, order[i])) return i;
    return -1;
}

/* a part of the parent in the game's order, made when missing (Model._part / _anim_part) */
static node_t *part_of(doc_t *d, node_t *parent, const char *tag, const char *const *order) {
    node_t *x = nd_find(parent, tag);
    if (x) return x;
    x = nd_new(d, tag, strlen(tag));
    if (!x) return NULL;
    int mine = -1;
    for (int i = 0; order[i]; i++) if (!strcmp(order[i], tag)) mine = i;
    int at = parent->nk;
    for (int i = 0; i < parent->nk; i++) {
        int o = ord_of(parent->kid[i], order);
        if (o >= 0 && o > mine) { at = i; break; }
    }
    kid_insert(d, parent, at, x);
    return x;
}

static const char *const CONTENT_ORDER[] = {"Spritesheets", "Layers", "Nulls", "Events", NULL};
static const char *const ANIM_ORDER[] = {"RootAnimation", "LayerAnimations", "NullAnimations", "Triggers", NULL};

static int model_of(doc_t *d, model_t *m) {
    m->content = nd_find(d->root, "Content");
    m->anims = nd_find(d->root, "Animations");
    if (!m->content || !m->anims) return -1;
    m->sheets = part_of(d, m->content, "Spritesheets", CONTENT_ORDER);
    m->layers = part_of(d, m->content, "Layers", CONTENT_ORDER);
    m->nulls = part_of(d, m->content, "Nulls", CONTENT_ORDER);
    return (m->sheets && m->layers && m->nulls) ? 0 : -1;
}

static int ascii_eq_fold(const char *a, size_t al, const char *b, size_t bl) {
    if (al != bl) return 0;
    for (size_t i = 0; i < al; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char) (x + 32);
        if (y >= 'A' && y <= 'Z') y = (char) (y + 32);
        if (x != y) return 0;
    }
    return 1;
}

static node_t *by_attr(node_t *part, const char *tag, const char *attr, const attr_t *want, int fold) {
    if (!want) return NULL;
    for (int i = 0; i < part->nk; i++) {
        node_t *x = part->kid[i];
        if (!nd_tag(x, tag)) continue;
        const attr_t *v = nd_attr(x, attr);
        if (!v) continue;
        if (fold ? ascii_eq_fold(v->val, v->vl, want->val, want->vl) : same_val(v, want)) return x;
    }
    return NULL;
}

static long max_id(node_t *part, const char *tag) {
    long m = -1;
    for (int i = 0; i < part->nk; i++)
        if (nd_tag(part->kid[i], tag)) { long v = num_of(nd_attr(part->kid[i], "Id")); if (v > m) m = v; }
    return m;
}

/* ================================================================== reports */

typedef struct { void (*say)(void *, const char *); void *ctx; } sayer_t;

static void say(const sayer_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void say(const sayer_t *s, const char *fmt, ...) {
    if (!s || !s->say) return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    s->say(s->ctx, line);
}

#define AV(a) (int) (a ? a->vl : 0), (a ? a->val : "")

/* ================================================================== apply (anm2patch.apply) */

static int skip_attr(const attr_t *a, const char *const *names) {
    for (int i = 0; names[i]; i++) if (a->nl == strlen(names[i]) && !memcmp(a->name, names[i], a->nl)) return 1;
    return 0;
}

static void op_sheet(doc_t *d, model_t *m, const node_t *op) {
    const attr_t *path = nd_attr(op, "Path");
    if (!path || by_attr(m->sheets, "Spritesheet", "Path", path, 1)) return;
    node_t *s = nd_new(d, "Spritesheet", 11);
    if (!s) return;
    at_push(d, s, "Id", 2, "", 0, '"');
    nd_set_int(d, s, "Id", max_id(m->sheets, "Spritesheet") + 1);
    at_push(d, s, "Path", 4, path->val, path->vl, '"');
    kid_insert(d, m->sheets, -1, s);
}

static void op_layer(doc_t *d, model_t *m, const node_t *op, const sayer_t *sy) {
    const attr_t *name = nd_attr(op, "Name");
    const attr_t *sp = nd_attr(op, "Spritesheet");
    const attr_t *of = nd_attr(op, "SheetOf");
    node_t *s = (sp && sp->vl) ? by_attr(m->sheets, "Spritesheet", "Path", sp, 1) : NULL;
    if (!s && of && of->vl) {
        node_t *o = by_attr(m->layers, "Layer", "Name", of, 0);
        if (o) {
            const attr_t *sid = nd_attr(o, "SpritesheetId");
            for (int i = 0; i < m->sheets->nk; i++) {
                node_t *x = m->sheets->kid[i];
                if (nd_tag(x, "Spritesheet")) {
                    const attr_t *xi = nd_attr(x, "Id");
                    if (sid && xi && same_val(xi, sid)) s = x;
                }
            }
        }
    }
    if (!s) { say(sy, "layer %.*s: no sheet %.*s - refused", AV(name), AV(sp)); return; }
    if (!name) return;
    const attr_t *sid = nd_attr(s, "Id");
    node_t *l = by_attr(m->layers, "Layer", "Name", name, 0);
    if (!l) {
        l = nd_new(d, "Layer", 5);
        if (!l) return;
        at_push(d, l, "Id", 2, "", 0, '"');
        nd_set_int(d, l, "Id", max_id(m->layers, "Layer") + 1);
        at_push(d, l, "Name", 4, name->val, name->vl, '"');
        at_push(d, l, "SpritesheetId", 13, sid ? sid->val : "", sid ? sid->vl : 0, '"');
        static const char *const SKIP[] = {"Name", "Spritesheet", "SheetOf", "Id", "SpritesheetId", NULL};
        for (int i = 0; i < op->na; i++)
            if (!skip_attr(&op->at[i], SKIP))
                at_push(d, l, op->at[i].name, op->at[i].nl, op->at[i].val, op->at[i].vl, op->at[i].q);
        kid_insert(d, m->layers, -1, l);
    } else if (!same_val(nd_attr(l, "SpritesheetId"), sid)) {
        nd_set(d, l, "SpritesheetId", 13, sid ? sid->val : "", sid ? sid->vl : 0);
    }
}

static void op_null(doc_t *d, model_t *m, const node_t *op, const sayer_t *sy) {
    const attr_t *name = nd_attr(op, "Name");
    if (!name || by_attr(m->nulls, "Null", "Name", name, 0)) return;
    const attr_t *bf = nd_attr(op, "Before");
    node_t *before = (bf && bf->vl) ? by_attr(m->nulls, "Null", "Name", bf, 0) : NULL;
    if (bf && bf->vl && !before)
        say(sy, "null %.*s: no null %.*s to go before - appended", AV(name), AV(bf));
    long nid;
    int at;
    if (before) {
        nid = num_of(nd_attr(before, "Id"));
        for (int i = 0; i < m->nulls->nk; i++) {
            node_t *x = m->nulls->kid[i];
            long v = num_of(nd_attr(x, "Id"));
            if (nd_tag(x, "Null") && v >= nid) nd_set_int(d, x, "Id", v + 1);
        }
        for (int i = 0; i < m->anims->nk; i++) {
            node_t *na = nd_find(m->anims->kid[i], "NullAnimations");
            for (int j = 0; na && j < na->nk; j++) {
                node_t *x = na->kid[j];
                long v = num_of(nd_attr(x, "NullId"));
                if (nd_tag(x, "NullAnimation") && v >= nid) nd_set_int(d, x, "NullId", v + 1);
            }
        }
        at = 0;
        while (at < m->nulls->nk && m->nulls->kid[at] != before) at++;
    } else {
        nid = max_id(m->nulls, "Null") + 1;
        at = m->nulls->nk;
    }
    node_t *n = nd_new(d, "Null", 4);
    if (!n) return;
    at_push(d, n, "Id", 2, "", 0, '"');
    nd_set_int(d, n, "Id", nid);
    at_push(d, n, "Name", 4, name->val, name->vl, '"');
    static const char *const SKIP[] = {"Name", "Before", "Id", NULL};
    for (int i = 0; i < op->na; i++)
        if (!skip_attr(&op->at[i], SKIP))
            at_push(d, n, op->at[i].name, op->at[i].nl, op->at[i].val, op->at[i].vl, op->at[i].q);
    kid_insert(d, m->nulls, at, n);
}

static void copy_kids(doc_t *d, node_t *to, const node_t *from) {
    to->nk = 0;
    for (int i = 0; i < from->nk; i++) kid_insert(d, to, -1, nd_copy(d, from->kid[i]));
}

static void op_anim(doc_t *d, model_t *m, const node_t *op, const sayer_t *sy) {
    const attr_t *name = nd_attr(op, "Name");
    if (!name) return;
    node_t *a = by_attr(m->anims, "Animation", "Name", name, 0);
    if (!a) {
        a = nd_new(d, "Animation", 9);
        if (!a) return;
        at_push(d, a, "Name", 4, name->val, name->vl, '"');
        for (int i = 0; ANIM_ORDER[i]; i++) kid_insert(d, a, -1, nd_new(d, ANIM_ORDER[i], strlen(ANIM_ORDER[i])));
        kid_insert(d, m->anims, -1, a);
    }
    for (int i = 0; i < op->na; i++)
        if (!(op->at[i].nl == 4 && !memcmp(op->at[i].name, "Name", 4)))
            nd_set(d, a, op->at[i].name, op->at[i].nl, op->at[i].val, op->at[i].vl);
    for (int k = 0; k < op->nk; k++) {
        const node_t *part = op->kid[k];
        if (nd_tag(part, "RootAnimation") || nd_tag(part, "Triggers")) {
            char tag[16];
            snprintf(tag, sizeof(tag), "%.*s", (int) part->tl, part->tag);
            node_t *x = part_of(d, a, tag, ANIM_ORDER);
            if (x) copy_kids(d, x, part);
            continue;
        }
        int is_layer = nd_tag(part, "LayerAnimation");
        if (!is_layer && !nd_tag(part, "NullAnimation")) {
            say(sy, "%.*s: unknown part <%.*s> - skipped", AV(name), (int) part->tl, part->tag);
            continue;
        }
        const char *key = is_layer ? "Layer" : "Null";
        const char *idattr = is_layer ? "LayerId" : "NullId";
        node_t *box = part_of(d, a, is_layer ? "LayerAnimations" : "NullAnimations", ANIM_ORDER);
        if (!box) return;
        const attr_t *target = nd_attr(part, key);
        node_t *ref = target ? by_attr(is_layer ? m->layers : m->nulls, key, "Name", target, 0) : NULL;
        if (!ref) {
            say(sy, "%.*s: %s %.*s is not in the file - refused", AV(name), is_layer ? "layer" : "null", AV(target));
            continue;
        }
        const attr_t *tid = nd_attr(ref, "Id");
        node_t *cur = NULL;
        for (int i = 0; i < box->nk; i++)
            if (nd_tag(box->kid[i], is_layer ? "LayerAnimation" : "NullAnimation") &&
                same_val(nd_attr(box->kid[i], idattr), tid)) { cur = box->kid[i]; break; }
        if (cur) {
            for (int i = 0; i < part->na; i++) {
                const attr_t *x = &part->at[i];
                if ((x->nl == strlen(key) && !memcmp(x->name, key, x->nl)) || (x->nl == 5 && !memcmp(x->name, "After", 5)))
                    continue;
                nd_set(d, cur, x->name, x->nl, x->val, x->vl);
            }
            copy_kids(d, cur, part);
            continue;
        }
        node_t *nw = nd_new(d, part->tag, part->tl);
        if (!nw) return;
        at_push(d, nw, idattr, strlen(idattr), tid ? tid->val : "", tid ? tid->vl : 0, '"');
        for (int i = 0; i < part->na; i++) {
            const attr_t *x = &part->at[i];
            if ((x->nl == strlen(key) && !memcmp(x->name, key, x->nl)) || (x->nl == 5 && !memcmp(x->name, "After", 5)))
                continue;
            at_push(d, nw, x->name, x->nl, x->val, x->vl, x->q);
        }
        copy_kids(d, nw, part);
        const attr_t *after = nd_attr(part, "After");
        int pos = -1;
        if (!after || !after->vl) pos = 0;
        else {
            node_t *an = by_attr(is_layer ? m->layers : m->nulls, key, "Name", after, 0);
            if (an) {
                const attr_t *aid = nd_attr(an, "Id");
                for (int i = 0; i < box->nk; i++)
                    if (nd_tag(box->kid[i], is_layer ? "LayerAnimation" : "NullAnimation") &&
                        same_val(nd_attr(box->kid[i], idattr), aid)) { pos = i + 1; break; }
            }
            if (pos < 0) {
                say(sy, "%.*s: %s %.*s - %.*s is not drawn here, appended", AV(name), is_layer ? "layer" : "null",
                    AV(target), AV(after));
                pos = box->nk;
            }
        }
        kid_insert(d, box, pos, nw);
    }
}

/*
 * base + patch -> *out (malloc'd; the base's own bytes copied when nothing
 * changes). 0 on success, -1 when either does not read as XML or the base is
 * not an .anm2.
 */
int pp_apply(const char *base, long bn, const char *patch, long pn, char **out, long *on,
             void (*sayf)(void *, const char *), void *ctx) {
    sayer_t sy = {sayf, ctx};
    doc_t d, p;
    *out = NULL; *on = 0;
    if (doc_parse(&d, base, bn)) { ar_free(&d.ar); return -1; }
    model_t m;
    if (model_of(&d, &m)) { ar_free(&d.ar); return -1; }
    if (doc_parse(&p, patch, pn)) { ar_free(&d.ar); ar_free(&p.ar); return -1; }
    buf_t before;
    if (doc_write(&d, &before)) { ar_free(&d.ar); ar_free(&p.ar); return -1; }
    for (int i = 0; i < p.root->nk; i++) {
        const node_t *op = p.root->kid[i];
        if (nd_tag(op, "Spritesheet")) op_sheet(&d, &m, op);
        else if (nd_tag(op, "Layer")) op_layer(&d, &m, op, &sy);
        else if (nd_tag(op, "Null")) op_null(&d, &m, op, &sy);
        else if (nd_tag(op, "Animation")) op_anim(&d, &m, op, &sy);
        else say(&sy, "unknown operation <%.*s> - skipped", (int) op->tl, op->tag);
    }
    buf_t after;
    int bad = d.failed || doc_write(&d, &after);
    ar_free(&p.ar);
    ar_free(&d.ar);
    if (bad) { free(before.b); return -1; }
    if (after.n == before.n && !memcmp(after.b, before.b, after.n)) {
        free(after.b);
        free(before.b);
        char *c = (char *) malloc((size_t) bn ? (size_t) bn : 1);
        if (!c) return -1;
        memcpy(c, base, (size_t) bn);
        *out = c; *on = bn;
        return 0;
    }
    free(before.b);
    *out = after.b; *on = (long) after.n;
    return 0;
}

/* ================================================================== the stick size variants (port_patch.variant) */

static int scaled_anim(const attr_t *name) {
    if (!name || name->vl < 7 || memcmp(name->val, "Analog ", 7)) return 0;
    for (size_t i = 0; i + 4 <= name->vl; i++) if (!memcmp(name->val + i, "Twin", 4)) return 0;
    return 1;
}

static int scale_text(const attr_t *a, int pct, char *dst, size_t cap) {
    char tmp[64];
    if (!a || a->vl >= sizeof(tmp)) return -1;
    memcpy(tmp, a->val, a->vl); tmp[a->vl] = 0;
    char *end;
    double v = strtod(tmp, &end);
    if (end == tmp) return -1;
    double f = v * pct / 100.0;
    double r = round(f);
    if (fabs(f - r) < 1e-9) return snprintf(dst, cap, "%ld", (long) r);
    int n = snprintf(dst, cap, "%.4f", f);
    while (n > 0 && dst[n - 1] == '0') dst[--n] = 0;
    if (n > 0 && dst[n - 1] == '.') dst[--n] = 0;
    return n;
}

int pp_variant(const char *base, long bn, int pct, char **out, long *on) {
    doc_t d;
    *out = NULL; *on = 0;
    if (doc_parse(&d, base, bn)) { ar_free(&d.ar); return -1; }
    node_t *anims = nd_find(d.root, "Animations");
    for (int i = 0; anims && i < anims->nk; i++) {
        node_t *a = anims->kid[i];
        if (!nd_tag(a, "Animation") || !scaled_anim(nd_attr(a, "Name"))) continue;
        node_t *las = nd_find(a, "LayerAnimations");
        for (int j = 0; las && j < las->nk; j++) {
            node_t *la = las->kid[j];
            for (int k = 0; k < la->nk; k++) {
                node_t *f = la->kid[k];
                if (!nd_tag(f, "Frame")) continue;
                static const char *const KEYS[] = {"XScale", "YScale"};
                for (int q = 0; q < 2; q++) {
                    const attr_t *x = nd_attr(f, KEYS[q]);
                    char v[48];
                    int n = x ? scale_text(x, pct, v, sizeof(v)) : -1;
                    if (n <= 0) continue;
                    char *s = ar_str(&d.ar, v, (size_t) n);
                    if (s) nd_set(&d, f, KEYS[q], strlen(KEYS[q]), s, (size_t) n);
                }
            }
        }
    }
    buf_t o;
    int bad = d.failed || doc_write(&d, &o);
    ar_free(&d.ar);
    if (bad) return -1;
    *out = o.b; *on = (long) o.n;
    return 0;
}

/* ================================================================== numbers */

/*
 * One question about an .anm2: Q_LAYER / Q_NULL - the Id of the layer (null) by
 * that name; Q_LAYERS - how many layers; Q_FRAMES - the FrameNum of the
 * animation by that name; Q_LENGTH - the longest of its layers in ticks (the
 * sum of their frames' Delay: what SetFrame(name, n) can show - the game's
 * own icon sheets carry more key frames than FrameNum says). -1: no such
 * name, -2: the file does not read.
 */
enum { Q_LAYER, Q_NULL, Q_LAYERS, Q_FRAMES, Q_LENGTH };

int pp_query(const char *b, long n, int q, const char *name) {
    doc_t d;
    if (doc_parse(&d, b, n)) { ar_free(&d.ar); return -2; }
    model_t m;
    long v = -2;
    if (!model_of(&d, &m)) {
        attr_t want = {"", 0, name ? name : "", name ? strlen(name) : 0, '"'};
        node_t *x;
        switch (q) {
        case Q_LAYER:
        case Q_NULL:
            x = by_attr(q == Q_NULL ? m.nulls : m.layers, q == Q_NULL ? "Null" : "Layer", "Name", &want, 0);
            v = x ? num_of(nd_attr(x, "Id")) : -1;
            break;
        case Q_LAYERS:
            v = 0;
            for (int i = 0; i < m.layers->nk; i++) if (nd_tag(m.layers->kid[i], "Layer")) v++;
            break;
        case Q_FRAMES:
            x = by_attr(m.anims, "Animation", "Name", &want, 0);
            v = x ? num_of(nd_attr(x, "FrameNum")) : -1;
            break;
        case Q_LENGTH: {
            x = by_attr(m.anims, "Animation", "Name", &want, 0);
            node_t *las = x ? nd_find(x, "LayerAnimations") : NULL;
            v = x ? 0 : -1;
            for (int i = 0; las && i < las->nk; i++) {
                long t = 0;
                for (int j = 0; j < las->kid[i]->nk; j++) {
                    node_t *f = las->kid[i]->kid[j];
                    if (!nd_tag(f, "Frame")) continue;
                    long dl = num_of(nd_attr(f, "Delay"));
                    t += dl > 0 ? dl : 1;
                }
                if (t > v) v = t;
            }
            break;
        }
        }
    }
    ar_free(&d.ar);
    return (int) v;
}

int pp_number(const char *b, long n, int is_null, const char *name) {
    return pp_query(b, n, is_null ? Q_NULL : Q_LAYER, name);
}

/* ================================================================== all or nothing (port_patch.complete) */
/*
 * A MOD'S MENU FILE NEVER COSTS THE PLAYER THE PORT'S MENU (02.10.2026, the
 * owner's concern for the mods people already made). A mod's .anm2 of a path
 * the port patches is handed out with the patch over it only when the patch
 * lands WHOLE: pp_apply said nothing (every sheet, layer, null and animation it
 * names was there to hang its additions on), and the result answers every
 * question the port's code and the game's own code ask of the file the way the
 * game's patched file does:
 *   - every layer the PATCH adds is there, by name (the port reads its
 *     numbers back by name - straps.c, loader.c);
 *   - every layer of the game's own keeps its number (the game's code and the
 *     image patches draw them by number: OnOff 4, DailyPaper 7..);
 *   - every null keeps its number (the finger zones and the MODS row go by
 *     null number: ChallengeButton 0, Area1..6, AreaDailyRun 7);
 *   - every animation is there.
 * Anything short of that and the caller serves the game's file with the patch
 * (as when the mod brings none), and says why. mine: the mod's file patched;
 * game: the pack's patched file at the same path; patch: the patch itself.
 * 0 - whole; 1 - not, *why says the first thing missing; -1 - does not read.
 */
static int patch_adds_layer(const node_t *proot, const attr_t *name) {
    for (int i = 0; i < proot->nk; i++) {
        const node_t *op = proot->kid[i];
        if (nd_tag(op, "Layer") && same_val(nd_attr(op, "Name"), name)) return 1;
    }
    return 0;
}

int pp_complete(const char *mine, long mn, const char *game, long gn, const char *patch, long pn,
                char *why, size_t room) {
    doc_t a, g, p;
    model_t ma, mg;
    int r = -1;
    if (why && room) why[0] = 0;
    int pa = doc_parse(&a, mine, mn), pg = doc_parse(&g, game, gn), pp = doc_parse(&p, patch, pn);
    if (pa || pg || pp || model_of(&a, &ma) || model_of(&g, &mg)) goto done;
    r = 0;
    for (int i = 0; i < mg.layers->nk && !r; i++) {
        node_t *x = mg.layers->kid[i];
        if (!nd_tag(x, "Layer")) continue;
        const attr_t *name = nd_attr(x, "Name");
        node_t *y = by_attr(ma.layers, "Layer", "Name", name, 0);
        if (!y) { snprintf(why, room, "the port's menu needs layer %.*s", AV(name)); r = 1; break; }
        if (!patch_adds_layer(p.root, name) && num_of(nd_attr(y, "Id")) != num_of(nd_attr(x, "Id"))) {
            snprintf(why, room, "layer %.*s is number %ld here, the game's code draws it as %ld", AV(name),
                     num_of(nd_attr(y, "Id")), num_of(nd_attr(x, "Id")));
            r = 1;
        }
    }
    for (int i = 0; i < mg.nulls->nk && !r; i++) {
        node_t *x = mg.nulls->kid[i];
        if (!nd_tag(x, "Null")) continue;
        const attr_t *name = nd_attr(x, "Name");
        node_t *y = by_attr(ma.nulls, "Null", "Name", name, 0);
        if (!y) { snprintf(why, room, "the port's menu needs null %.*s", AV(name)); r = 1; break; }
        if (num_of(nd_attr(y, "Id")) != num_of(nd_attr(x, "Id"))) {
            snprintf(why, room, "null %.*s is number %ld here, the menu's touch zones go by %ld", AV(name),
                     num_of(nd_attr(y, "Id")), num_of(nd_attr(x, "Id")));
            r = 1;
        }
    }
    for (int i = 0; i < mg.anims->nk && !r; i++) {
        node_t *x = mg.anims->kid[i];
        if (!nd_tag(x, "Animation")) continue;
        const attr_t *name = nd_attr(x, "Name");
        if (!by_attr(ma.anims, "Animation", "Name", name, 0)) {
            snprintf(why, room, "the port's menu needs animation %.*s", AV(name));
            r = 1;
        }
    }
done:
    /* doc_parse leaves its arena to be freed whether it read or not (as pp_apply frees it) */
    ar_free(&a.ar);
    ar_free(&g.ar);
    ar_free(&p.ar);
    return r;
}

/* the first report line of pp_apply, and how many there were: one line means the patch did not land whole */
typedef struct { int n; char first[300]; } tally_t;
static void say_tally(void *ctx, const char *line) {
    tally_t *t = (tally_t *) ctx;
    if (!t->n++) snprintf(t->first, sizeof(t->first), "%s", line);
}

/*
 * The mod's file with the patch over it, all or nothing: 0 and *out (malloc'd)
 * - whole; 1 - the patch did not land whole or the result is not the game's
 * shape (*why); -1 - the mod's file does not read as an .anm2. game: the
 * pack's patched file at this path (NULL: only the patch's own report counts).
 */
int pp_apply_whole(const char *mine, long mn, const char *game, long gn, const char *patch, long pn,
                   char **out, long *on, char *why, size_t room) {
    tally_t t = {0, ""};
    *out = NULL; *on = 0;
    if (why && room) why[0] = 0;
    char *o; long ol;
    if (pp_apply(mine, mn, patch, pn, &o, &ol, say_tally, &t)) return -1;
    if (t.n) {
        snprintf(why, room, "the port's menu does not fit it: %s", t.first);
        free(o);
        return 1;
    }
    if (game) {
        int c = pp_complete(o, ol, game, gn, patch, pn, why, room);
        if (c) { free(o); return c < 0 ? -1 : 1; }
    }
    *out = o; *on = ol;
    return 0;
}

/* ================================================================== pixels (port_patch.pixels_*) */
/*
 * THE PORT'S RETOUCH OF A GAME SHEET, OVER A MOD'S SHEET TOO (02.10.2026, the
 * M8 regression: a mod's gamemenu.png of the game's size brought back the
 * blank paper the port clears under its PORT CREDITS strap). The port's
 * retouch of such a sheet is kept as a list of pixels - every pixel where the
 * port's version differs from the game's, with the port's value - and laid
 * over whichever sheet of that path wins: the game's (the "over" file is
 * exactly that, proved on the desk) and a mod's of the game's size. A sheet of
 * another size is not the game's layout: left as it is.
 *
 * ONLY WHERE THE MOD KEPT THE GAME'S ART (04.10.2026, the 25-mod desk campaign):
 * a mod that draws its own opaque background over the game's blank paper
 * (Reflashed) got a hole punched in it. So each pixel carries the vanilla
 * value too, and a flag for the strap's footprint (the PortStrap* layers,
 * drawn UNDER Main, show their own pixels there). The whole retouch goes on
 * only when the mod kept the game's art under it: at least 9 in 10 of the
 * pixels in the game's opacity class (alpha > 8 or not) and, opaque, every
 * channel within 48 of vanilla (vanilla, the language sheets and recolours:
 * all of them; Reflashed: 59 in 100). Otherwise only the footprint's pixels:
 * the strap shows, the mod's background stays around it. tools/port_patch.py
 * (pixels_apply) is the twin, tools/portdesk/pixels.py the proof.
 *
 * The patch file: "ISAACPX2", u16 width, u16 height, u32 count, then count
 * records of u16 x, u16 y, u8 port r, g, b, a, u8 vanilla r, g, b, a, u8 flags
 * (1: the strap's footprint) - little endian, 13 bytes each.
 *
 * The PNG reader here takes what libpng 1.0.2 takes - grey, RGB, palette,
 * grey+alpha, RGBA; 1, 2, 4, 8 and 16 bits; tRNS; Adam7 - with an inflate of
 * its own (no zlib: the desk builds this file as it is); the writer writes
 * 8-bit RGBA in stored deflate blocks, which every reader reads.
 */
typedef struct {
    const unsigned char *in; size_t n, pos;
    unsigned bit; int nbits;
    unsigned char *out; size_t on, cap;
    int err;
} zst_t;

static unsigned z_bits(zst_t *z, int k) {
    while (z->nbits < k) {
        if (z->pos >= z->n) { z->err = 1; return 0; }
        z->bit |= (unsigned) z->in[z->pos++] << z->nbits;
        z->nbits += 8;
    }
    unsigned v = z->bit & ((1u << k) - 1u);
    z->bit >>= k;
    z->nbits -= k;
    return v;
}

typedef struct { unsigned short count[16], sym[320]; } huff_t;

static int huff_build(huff_t *h, const unsigned char *len, int n) {
    unsigned short offs[16];
    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int i = 1; i < 16; i++) { left <<= 1; left -= h->count[i]; if (left < 0) return -1; }
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (unsigned short) (offs[i] + h->count[i]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (unsigned short) i;
    return 0;
}

static int huff_dec(zst_t *z, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int) z_bits(z, 1);
        int c = h->count[len];
        if (code - c < first) return h->sym[index + (code - first)];
        index += c;
        first += c;
        first <<= 1;
        code <<= 1;
        if (z->err) return -1;
    }
    z->err = 1;
    return -1;
}

static void z_put(zst_t *z, unsigned char c) {
    if (z->on >= z->cap) { z->err = 1; return; }
    z->out[z->on++] = c;
}

static const unsigned short LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                         67, 83, 99, 115, 131, 163, 195, 227, 258};
static const unsigned char LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const unsigned short DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const unsigned char DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                       12, 12, 13, 13};

static void z_codes(zst_t *z, const huff_t *lit, const huff_t *dist) {
    for (;;) {
        int s = huff_dec(z, lit);
        if (z->err || s < 0) { z->err = 1; return; }
        if (s < 256) { z_put(z, (unsigned char) s); if (z->err) return; continue; }
        if (s == 256) return;
        s -= 257;
        if (s >= 29) { z->err = 1; return; }
        size_t len = LBASE[s] + z_bits(z, LEXT[s]);
        int d = huff_dec(z, dist);
        if (z->err || d < 0 || d >= 30) { z->err = 1; return; }
        size_t back = DBASE[d] + z_bits(z, DEXT[d]);
        if (z->err || back > z->on) { z->err = 1; return; }
        for (size_t i = 0; i < len && !z->err; i++) z_put(z, z->out[z->on - back]);
        if (z->err) return;
    }
}

/* a zlib stream into out (cap bytes at most); the bytes written, or -1 */
static long z_inflate(const unsigned char *in, size_t n, unsigned char *out, size_t cap) {
    zst_t z = {in, n, 2, 0, 0, out, 0, cap, 0};
    if (n < 2 || (in[0] & 15) != 8 || ((in[0] << 8) | in[1]) % 31 || (in[1] & 32)) return -1;
    static const unsigned char ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    int final;
    do {
        final = (int) z_bits(&z, 1);
        int type = (int) z_bits(&z, 2);
        if (z.err) return -1;
        if (type == 0) {
            z.bit = 0; z.nbits = 0;
            if (z.pos + 4 > z.n) return -1;
            unsigned len = (unsigned) z.in[z.pos] | (unsigned) z.in[z.pos + 1] << 8;
            unsigned nlen = (unsigned) z.in[z.pos + 2] | (unsigned) z.in[z.pos + 3] << 8;
            z.pos += 4;
            if ((len ^ 0xffffu) != nlen || z.pos + len > z.n || z.on + len > z.cap) return -1;
            memcpy(z.out + z.on, z.in + z.pos, len);
            z.on += len; z.pos += len;
        } else if (type == 1) {
            static huff_t fl, fd;
            static int ready;
            if (!ready) {
                unsigned char l[288];
                for (int i = 0; i < 144; i++) l[i] = 8;
                for (int i = 144; i < 256; i++) l[i] = 9;
                for (int i = 256; i < 280; i++) l[i] = 7;
                for (int i = 280; i < 288; i++) l[i] = 8;
                huff_build(&fl, l, 288);
                for (int i = 0; i < 30; i++) l[i] = 5;
                huff_build(&fd, l, 30);
                ready = 1;
            }
            z_codes(&z, &fl, &fd);
        } else if (type == 2) {
            int nlen = (int) z_bits(&z, 5) + 257, ndist = (int) z_bits(&z, 5) + 1, ncode = (int) z_bits(&z, 4) + 4;
            if (z.err || nlen > 286 || ndist > 30) return -1;
            unsigned char l[320];
            memset(l, 0, sizeof(l));
            for (int i = 0; i < ncode; i++) l[ORDER[i]] = (unsigned char) z_bits(&z, 3);
            huff_t lc, lit, dist;
            if (huff_build(&lc, l, 19)) return -1;
            int i = 0;
            while (i < nlen + ndist) {
                int s = huff_dec(&z, &lc);
                if (z.err || s < 0) return -1;
                if (s < 16) { l[i++] = (unsigned char) s; continue; }
                int rep, v = 0;
                if (s == 16) { if (!i) return -1; v = l[i - 1]; rep = 3 + (int) z_bits(&z, 2); }
                else if (s == 17) rep = 3 + (int) z_bits(&z, 3);
                else rep = 11 + (int) z_bits(&z, 7);
                if (i + rep > nlen + ndist) return -1;
                while (rep--) l[i++] = (unsigned char) v;
            }
            if (huff_build(&lit, l, nlen) || huff_build(&dist, l + nlen, ndist)) {
                /* an incomplete code is allowed (one distance code, puff.c); over-subscribed is not */
                return -1;
            }
            z_codes(&z, &lit, &dist);
        } else return -1;
        if (z.err) return -1;
    } while (!final);
    return (long) z.on;
}

static unsigned be32u(const unsigned char *p) { return (unsigned) p[0] << 24 | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* a PNG into 8-bit RGBA (malloc'd, w*h*4); 0, or -1 when it does not read */
int pp_png_read(const unsigned char *b, long n, unsigned char **rgba, int *w, int *h) {
    static const unsigned char SIG[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    *rgba = NULL;
    if (n < 8 + 25 || memcmp(b, SIG, 8)) return -1;
    unsigned W = 0, H = 0;
    int depth = 0, ct = -1, il = 0, npal = 0, ntrns = 0;
    unsigned char pal[256][4];
    unsigned char trns[256];
    unsigned short tkey[3] = {0, 0, 0};
    int has_key = 0;
    unsigned char *idat = NULL;
    size_t in = 0, icap = 0;
    long p = 8;
    int seen_ihdr = 0;
    while (p + 12 <= n) {
        unsigned len = be32u(b + p);
        const unsigned char *t = b + p + 4, *d = b + p + 8;
        if (len > (unsigned) (n - p - 12)) { free(idat); return -1; }
        if (!memcmp(t, "IHDR", 4) && len >= 13) {
            W = be32u(d); H = be32u(d + 4); depth = d[8]; ct = d[9]; il = d[12];
            if (d[10] || d[11] || il > 1) { free(idat); return -1; }
            seen_ihdr = 1;
        } else if (!memcmp(t, "PLTE", 4)) {
            npal = (int) (len / 3 > 256 ? 256 : len / 3);
            for (int i = 0; i < npal; i++) { pal[i][0] = d[3 * i]; pal[i][1] = d[3 * i + 1]; pal[i][2] = d[3 * i + 2]; pal[i][3] = 255; }
        } else if (!memcmp(t, "tRNS", 4)) {
            if (ct == 3) { ntrns = (int) (len > 256 ? 256 : len); memcpy(trns, d, (size_t) ntrns); }
            else if (ct == 0 && len >= 2) { tkey[0] = (unsigned short) (d[0] << 8 | d[1]); has_key = 1; }
            else if (ct == 2 && len >= 6) {
                for (int i = 0; i < 3; i++) tkey[i] = (unsigned short) (d[2 * i] << 8 | d[2 * i + 1]);
                has_key = 1;
            }
        } else if (!memcmp(t, "IDAT", 4)) {
            if (in + len > icap) {
                size_t nc = (in + len) * 2 + 4096;
                unsigned char *x = (unsigned char *) realloc(idat, nc);
                if (!x) { free(idat); return -1; }
                idat = x; icap = nc;
            }
            memcpy(idat + in, d, len);
            in += len;
        } else if (!memcmp(t, "IEND", 4)) break;
        p += 12 + (long) len;
    }
    int ch = ct == 0 ? 1 : ct == 2 ? 3 : ct == 3 ? 1 : ct == 4 ? 2 : ct == 6 ? 4 : 0;
    int ok_depth = ct == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8)
                 : ct == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)
                 : (depth == 8 || depth == 16);
    if (!seen_ihdr || !ch || !ok_depth || !W || !H || W > 8192 || H > 8192 || !idat || (ct == 3 && !npal)) {
        free(idat);
        return -1;
    }
    for (int i = 0; i < ntrns && i < npal; i++) pal[i][3] = trns[i];
    int bits = ch * depth;
    int bpp = bits >= 8 ? bits / 8 : 1;
    static const int PX[7] = {0, 4, 0, 2, 0, 1, 0}, PY[7] = {0, 0, 4, 0, 2, 0, 1},
                     SX[7] = {8, 8, 4, 4, 2, 2, 1}, SY[7] = {8, 8, 8, 4, 4, 2, 2};
    int passes = il ? 7 : 1;
    size_t raw_n = 0;
    for (int k = 0; k < passes; k++) {
        size_t pw = il ? (W - PX[k] + SX[k] - 1) / SX[k] : W, ph = il ? (H - PY[k] + SY[k] - 1) / SY[k] : H;
        if (W <= (unsigned) PX[k] || H <= (unsigned) PY[k]) continue;
        raw_n += ph * (1 + (pw * (size_t) bits + 7) / 8);
    }
    unsigned char *raw = (unsigned char *) malloc(raw_n + 1);
    unsigned char *out = (unsigned char *) malloc((size_t) W * H * 4);
    if (!raw || !out) { free(raw); free(out); free(idat); return -1; }
    long got = z_inflate(idat, in, raw, raw_n);
    free(idat);
    if (got != (long) raw_n) { free(raw); free(out); return -1; }
    unsigned char *r = raw;
    for (int k = 0; k < passes; k++) {
        if (il && (W <= (unsigned) PX[k] || H <= (unsigned) PY[k])) continue;
        size_t pw = il ? (W - PX[k] + SX[k] - 1) / SX[k] : W, ph = il ? (H - PY[k] + SY[k] - 1) / SY[k] : H;
        size_t stride = (pw * (size_t) bits + 7) / 8;
        unsigned char *prev = NULL;
        for (size_t y = 0; y < ph; y++) {
            int f = r[0];
            unsigned char *row = r + 1;
            for (size_t i = 0; i < stride; i++) {
                int a = i >= (size_t) bpp ? row[i - bpp] : 0, up = prev ? prev[i] : 0, c = (prev && i >= (size_t) bpp) ? prev[i - bpp] : 0;
                int v = row[i];
                switch (f) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += up; break;
                case 3: v += (a + up) / 2; break;
                case 4: v += paeth(a, up, c); break;
                default: free(raw); free(out); return -1;
                }
                row[i] = (unsigned char) v;
            }
            for (size_t x = 0; x < pw; x++) {
                unsigned s[4] = {0, 0, 0, 0};
                for (int c2 = 0; c2 < ch; c2++) {
                    if (depth == 16) s[c2] = (unsigned) row[(x * ch + c2) * 2] << 8 | row[(x * ch + c2) * 2 + 1];
                    else if (depth == 8) s[c2] = row[x * ch + c2];
                    else {
                        size_t bit = x * (size_t) depth;
                        s[c2] = (unsigned) (row[bit / 8] >> (8 - depth - (int) (bit % 8))) & ((1u << depth) - 1u);
                    }
                }
                unsigned char px[4];
                int top = (1 << depth) - 1;
                if (ct == 3) {
                    if ((int) s[0] >= npal) { free(raw); free(out); return -1; }
                    memcpy(px, pal[s[0]], 4);
                } else if (ct == 0 || ct == 4) {
                    unsigned char g = (unsigned char) (depth == 16 ? s[0] >> 8 : depth == 8 ? s[0] : s[0] * 255 / (unsigned) top);
                    px[0] = px[1] = px[2] = g;
                    px[3] = ct == 4 ? (unsigned char) (depth == 16 ? s[1] >> 8 : s[1]) : (has_key && s[0] == tkey[0]) ? 0 : 255;
                } else {
                    for (int c2 = 0; c2 < 3; c2++) px[c2] = (unsigned char) (depth == 16 ? s[c2] >> 8 : s[c2]);
                    px[3] = ct == 6 ? (unsigned char) (depth == 16 ? s[3] >> 8 : s[3])
                          : (has_key && s[0] == tkey[0] && s[1] == tkey[1] && s[2] == tkey[2]) ? 0 : 255;
                }
                size_t X = il ? PX[k] + x * SX[k] : x, Y = il ? PY[k] + y * SY[k] : y;
                memcpy(out + (Y * W + X) * 4, px, 4);
            }
            prev = row;
            r += 1 + stride;
        }
    }
    free(raw);
    *rgba = out; *w = (int) W; *h = (int) H;
    return 0;
}

static unsigned crc_table_png[256];
static unsigned png_crc(const unsigned char *p, size_t n, unsigned c) {
    if (!crc_table_png[1]) {
        for (unsigned i = 0; i < 256; i++) {
            unsigned v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
            crc_table_png[i] = v;
        }
    }
    c = ~c;
    for (size_t i = 0; i < n; i++) c = crc_table_png[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}

static void put_be32(unsigned char *p, unsigned v) { p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16); p[2] = (unsigned char) (v >> 8); p[3] = (unsigned char) v; }

/* 8-bit RGBA into a PNG (malloc'd): filter 0, stored deflate blocks */
int pp_png_write(const unsigned char *rgba, int w, int h, unsigned char **png, long *n) {
    size_t row = (size_t) w * 4 + 1, raw = row * (size_t) h;
    size_t blocks = raw / 65535 + 1;
    size_t zn = 2 + raw + blocks * 5 + 4;
    size_t total = 8 + 25 + 12 + zn + 12;
    unsigned char *o = (unsigned char *) malloc(total);
    if (!o) return -1;
    static const unsigned char SIG[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    unsigned char *q = o;
    memcpy(q, SIG, 8); q += 8;
    put_be32(q, 13); memcpy(q + 4, "IHDR", 4);
    put_be32(q + 8, (unsigned) w); put_be32(q + 12, (unsigned) h);
    q[16] = 8; q[17] = 6; q[18] = 0; q[19] = 0; q[20] = 0;
    put_be32(q + 21, png_crc(q + 4, 17, 0));
    q += 25;
    put_be32(q, (unsigned) zn); memcpy(q + 4, "IDAT", 4);
    unsigned char *z = q + 8, *zs = z;
    *z++ = 0x78; *z++ = 0x01;
    unsigned a1 = 1, a2 = 0;
    size_t left = raw, at = 0;
    while (left || !at) {
        size_t k = left > 65535 ? 65535 : left;
        *z++ = (unsigned char) (left <= 65535 ? 1 : 0);
        *z++ = (unsigned char) k; *z++ = (unsigned char) (k >> 8);
        *z++ = (unsigned char) ~k; *z++ = (unsigned char) (~k >> 8);
        for (size_t i = 0; i < k; i++, at++) {
            size_t y = at / row, x = at % row;
            unsigned char v = x ? rgba[y * (size_t) w * 4 + x - 1] : 0;
            *z++ = v;
            a1 = (a1 + v) % 65521u; a2 = (a2 + a1) % 65521u;
        }
        left -= k;
        if (!k) break;
    }
    put_be32(z, a2 << 16 | a1); z += 4;
    zn = (size_t) (z - zs);
    put_be32(q, (unsigned) zn);
    put_be32(z, png_crc(q + 4, zn + 4, 0));
    q = z + 4;
    put_be32(q, 0); memcpy(q + 4, "IEND", 4); put_be32(q + 8, png_crc(q + 4, 4, 0));
    q += 12;
    *png = o; *n = (long) (q - o);
    return 0;
}

/*
 * A sheet with the port's pixels laid over it: 0 and *out (malloc'd PNG), *mode
 * 2 - the whole retouch, 1 - the strap's footprint only (above);
 * 1 - the sheet is not the patch's size (not the game's layout: left alone);
 * -1 - the sheet or the patch does not read.
 */
#define PX_REC 13
#define PX_TOL 48
int pp_pixels_apply(const unsigned char *png, long n, const unsigned char *patch, long pn, unsigned char **out, long *on,
                    int *mode) {
    *out = NULL; *on = 0;
    if (mode) *mode = 0;
    if (pn < 16 || memcmp(patch, "ISAACPX2", 8)) return -1;
    int pw = patch[8] | patch[9] << 8, ph = patch[10] | patch[11] << 8;
    unsigned count = (unsigned) patch[12] | (unsigned) patch[13] << 8 | (unsigned) patch[14] << 16 | (unsigned) patch[15] << 24;
    if ((long) count > (pn - 16) / PX_REC) return -1;
    unsigned char *rgba;
    int w, h;
    if (pp_png_read(png, n, &rgba, &w, &h)) return -1;
    if (w != pw || h != ph) { free(rgba); return 1; }
    /* did the mod keep the game's art under the retouch */
    unsigned kept = 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *r = patch + 16 + PX_REC * (size_t) i;
        int x = r[0] | r[1] << 8, y = r[2] | r[3] << 8;
        if (x >= w || y >= h) continue;
        const unsigned char *m = rgba + ((size_t) y * (size_t) w + (size_t) x) * 4, *v = r + 8;
        int mo = m[3] > 8, vo = v[3] > 8, near = 1;
        for (int c = 0; c < 3; c++) if (abs((int) m[c] - (int) v[c]) > PX_TOL) near = 0;
        if (mo == vo && (!mo || near)) kept++;
    }
    int whole = (unsigned long long) kept * 10 >= (unsigned long long) count * 9;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *r = patch + 16 + PX_REC * (size_t) i;
        int x = r[0] | r[1] << 8, y = r[2] | r[3] << 8;
        if (x < w && y < h && (whole || (r[12] & 1))) memcpy(rgba + ((size_t) y * (size_t) w + (size_t) x) * 4, r + 4, 4);
    }
    int bad = pp_png_write(rgba, w, h, out, on);
    free(rgba);
    if (mode) *mode = whole ? 2 : 1;
    return bad ? -1 : 0;
}

#ifndef PORTPATCH_DESK
/* ================================================================== at start */

#define MAX_ENTRIES 160
typedef struct {
    char kind;                  /* p(atch), (pi)x(els); files and overs are laid and forgotten */
    char target[200];
    const char *patch; long patch_len;      /* p: the patch, kept for a mod's file (shim_port_patch_file) */
} entry_t;
static entry_t g_entry[MAX_ENTRIES];
static int g_entries;
static int g_open;
static int g_laid, g_failed;

static void say_report(void *ctx, const char *line) {
    char text[600];
    snprintf(text, sizeof(text), "%s: %s", (const char *) ctx, line);
    report_line("port patch", text);
    LOG("portpatch: %s", text);
}

static const void *asset_bytes(AAssetManager *mgr, const char *name, long *n) {
    AAsset *a = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
    if (!a) return NULL;
    const void *b = AAsset_getBuffer(a);
    *n = (long) AAsset_getLength64(a);
    if (!b || *n <= 0) { AAsset_close(a); return NULL; }
    return b;                   /* left open on purpose: the table points into it for good */
}

static int split_tabs(char *line, char **f, int max) {
    int n = 0;
    while (n < max) {
        f[n++] = line;
        char *t = strchr(line, '\t');
        if (!t) break;
        *t = 0;
        line = t + 1;
    }
    return n;
}

static const entry_t *entry_for(const char *inner);
static char *effective(const entry_t *e, long *n, int *owned, char *who, size_t whocap);

/*
 * The stick sizes (plan 30.09, M9): built from the ios_buttons.anm2 that wins -
 * a mod's, with the port's patch over it, else the pack's patched one - so the
 * size the player picks never brings back a file nobody loads (the twelve
 * ready-made variants the port used to carry are gone).
 */
static void lay_variants(const char *target, const char *pcts) {
    long bn = 0;
    int owned = 0;
    char who[400];
    const entry_t *e = entry_for(target);
    const char *b = e ? effective(e, &bn, &owned, who, sizeof(who)) : (const char *) shim_pack_peek(target, &bn);
    if (!b) { char t[300]; snprintf(t, sizeof(t), "variants: %s is not in the pack", target);
              report_line("port patch", t); g_failed++; return; }
    char folder[200];
    snprintf(folder, sizeof(folder), "%s", target);
    char *slash = strrchr(folder, '/');
    if (slash) *slash = 0;
    char list[200];
    snprintf(list, sizeof(list), "%s", pcts);
    for (char *p = strtok(list, ","); p; p = strtok(NULL, ",")) {
        int pct = atoi(p);
        char *out; long on;
        if (pct <= 0 || pp_variant(b, bn, pct, &out, &on)) { g_failed++; continue; }
        char path[260];
        snprintf(path, sizeof(path), "%s/ios_buttons_%d.anm2", folder, pct);
        shim_pack_add(path, out, on);
        g_laid++;
    }
    if (owned) free((void *) b);
}

void shim_portpatch_open(void *manager) {
    AAssetManager *mgr = (AAssetManager *) manager;
    if (!mgr || g_open) return;
    g_open = 1;
    long n = 0;
    const char *idx = (const char *) asset_bytes(mgr, "port_patch/index.txt", &n);
    if (!idx) { report_line("port patch", "port_patch/index.txt is not in the assets - the port's menus stay the game's");
                return; }
    char *text = (char *) malloc((size_t) n + 1);
    if (!text) return;
    memcpy(text, idx, (size_t) n); text[n] = 0;
    /* three passes, as the index is ordered: files and overs, patches, variants */
    for (int pass = 0; pass < 3; pass++) {
        char *copy = strdup(text);
        if (!copy) break;
        char *save = NULL;
        for (char *ln = strtok_r(copy, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
            size_t l = strlen(ln);
            if (l && ln[l - 1] == '\r') ln[l - 1] = 0;
            if (!ln[0] || ln[0] == '#') continue;
            char *f[4] = {0};
            int k = split_tabs(ln, f, 4);
            if (k < 3) continue;
            int is_file = !strcmp(f[0], "file") || !strcmp(f[0], "over");
            int is_patch = !strcmp(f[0], "patch");
            int is_var = !strcmp(f[0], "variants");
            int is_pixels = !strcmp(f[0], "pixels");
            if ((pass == 0 && (is_file || is_pixels)) || (pass == 1 && is_patch)) {
                long an = 0;
                const char *ab = (const char *) asset_bytes(mgr, f[2], &an);
                if (!ab) {
                    char t[400]; snprintf(t, sizeof(t), "%s: the asset %s is missing", f[1], f[2]);
                    report_line("port patch", t); g_failed++; continue;
                }
                if (is_file) { shim_pack_add(f[1], ab, an); g_laid++; continue; }
                /* the port's retouch as pixels, for a mod's sheet of this path (the game's is the "over" file) */
                if (is_pixels) {
                    if (g_entries < MAX_ENTRIES) {
                        entry_t *e = &g_entry[g_entries++];
                        e->kind = 'x';
                        snprintf(e->target, sizeof(e->target), "%s", f[1]);
                        e->patch = ab; e->patch_len = an;
                    }
                    continue;
                }
                const char *basepath = (k >= 4 && f[3][0]) ? f[3] : f[1];
                long bn = 0;
                const char *b = (const char *) shim_pack_peek(basepath, &bn);
                if (!b) {
                    char t[400]; snprintf(t, sizeof(t), "%s: no base %s in the pack", f[1], basepath);
                    report_line("port patch", t); g_failed++; continue;
                }
                char *out; long on;
                if (pp_apply(b, bn, ab, an, &out, &on, say_report, f[1])) {
                    char t[400]; snprintf(t, sizeof(t), "%s: does not read as an anm2 - left as the game's", f[1]);
                    report_line("port patch", t); g_failed++; continue;
                }
                shim_pack_add(f[1], out, on);
                g_laid++;
                if (g_entries < MAX_ENTRIES) {
                    entry_t *e = &g_entry[g_entries++];
                    e->kind = 'p';
                    snprintf(e->target, sizeof(e->target), "%s", f[1]);
                    e->patch = ab; e->patch_len = an;
                }
            } else if (pass == 2 && is_var) {
                lay_variants(f[1], f[2]);
            }
        }
        free(copy);
    }
    free(text);
    char t[160];
    snprintf(t, sizeof(t), "%d laid over the pack, %d failed", g_laid, g_failed);
    report_line("port patch", t);
    LOG("portpatch: %s", t);
}

static const entry_t *entry_kind(const char *inner, char kind) {
    for (int i = 0; i < g_entries; i++)
        if (g_entry[i].kind == kind && ascii_eq_fold(g_entry[i].target, strlen(g_entry[i].target), inner, strlen(inner)))
            return &g_entry[i];
    return NULL;
}
static const entry_t *entry_for(const char *inner) { return entry_kind(inner, 'p'); }

/*
 * A mod's own sheet for a path whose game sheet the port retouches (pixels):
 * 1 and *out (malloc'd PNG) - the port's pixels laid over it; 0 - no retouch
 * for this path; -1 - the mod's sheet does not read here; -2 - it is not the
 * game's size (*why) - not the game's layout, left as it is. On anything but 1
 * the mod's own file is served unchanged.
 */
int shim_port_pixels_file(const char *inner, const void *in, long n, void **out, long *on, char *why, size_t room) {
    char spare[2];
    if (!why || !room) { why = spare; room = sizeof(spare); }
    why[0] = 0;
    const entry_t *e = inner ? entry_kind(inner, 'x') : NULL;
    if (!e) return 0;
    unsigned char *o; long ol;
    int mode = 0;
    int r = pp_pixels_apply((const unsigned char *) in, n, (const unsigned char *) e->patch, e->patch_len, &o, &ol, &mode);
    if (r < 0) { snprintf(why, room, "the picture does not read here"); return -1; }
    if (r > 0) {
        snprintf(why, room, "not the game's size - the port's retouch (its strap's place) is left off it");
        return -2;
    }
    if (mode == 1)
        snprintf(why, room, "the mod draws its own picture where the port clears the game's blank paper - only the "
                 "strap's own place is cleared");
    *out = o; *on = ol;
    return 1;
}

/*
 * A mod's own .anm2 for a path the port patches: the port's patch laid over it
 * (for usermods.c, when it hands a mod's file out instead of the pack's), ALL
 * OR NOTHING (pp_apply_whole):
 * 1 and *out (malloc'd) - patched whole; 0 - the port has no patch for this
 * path; -1 - the mod's file does not read as an anm2; -2 - the patch does not
 * land whole on it, *why says what is missing. On -1 and -2 the game's file
 * (the pack's, patched) is the one to serve.
 */
int shim_port_patch_file(const char *inner, const void *in, long n, void **out, long *on, char *why, size_t room) {
    char spare[2];
    if (!why || !room) { why = spare; room = sizeof(spare); }
    why[0] = 0;
    const entry_t *e = inner ? entry_for(inner) : NULL;
    if (!e) return 0;
    long gn = 0;
    const char *game = (const char *) shim_pack_peek(e->target, &gn);
    char *o; long ol;
    int r = pp_apply_whole((const char *) in, n, game, gn, e->patch, e->patch_len, &o, &ol, why, room);
    if (r < 0) { snprintf(why, room, "does not read as an .anm2"); return -1; }
    if (r > 0) {
        char t[700];
        snprintf(t, sizeof(t), "%s: a mod's file - %s; the game's file with the port's additions is used", e->target, why);
        report_line("port patch", t);
        LOG("portpatch: %s", t);
        return -2;
    }
    *out = o; *on = ol;
    return 1;
}

/* the paths the port patches, one by one (usermods.c checks a mod's file for each it would answer); NULL past the end */
const char *shim_port_patch_target(int i) {
    int k = 0;
    for (int j = 0; j < g_entries; j++)
        if (g_entry[j].kind == 'p' && k++ == i) return g_entry[j].target;
    return NULL;
}

/* the bytes the game will load for a patched target: a mod's (patched whole), else the pack's */
static char *effective(const entry_t *e, long *n, int *owned, char *who, size_t whocap) {
    *owned = 0;
    snprintf(who, whocap, "the pack");
    if (shim_usermods_peek) {
        char full[700];
        snprintf(full, sizeof(full), "%s%s", shim_pack_root(), e->target);
        const char *mod = shim_usermods_peek(full);
        if (mod) {
            FILE *f = fopen(mod, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long len = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *b = len > 0 ? (char *) malloc((size_t) len) : NULL;
                int got = b && fread(b, 1, (size_t) len, f) == (size_t) len;
                fclose(f);
                if (got) {
                    void *o; long ol;
                    char why[300];
                    int r = shim_port_patch_file(e->target, b, len, &o, &ol, why, sizeof(why));
                    free(b);
                    if (r == 1) { *owned = 1; *n = ol; snprintf(who, whocap, "%s", mod); return (char *) o; }
                    /* not whole, or not an anm2: usermods.c serves the game's file - so do the numbers */
                } else free(b);
            }
        }
    }
    return (char *) shim_pack_peek(e->target, n);
}

/*
 * Every copy of an .anm2 the game may load for rel ("gfx/ui/main menu/
 * optionsmenu.anm2"): the port's patched targets that end in it (every language
 * copy), else the pack's own files - the first edition that has it in data/,
 * with its language sets.
 */
static const char *const EDITIONS[] = {"repentance-resources", "afterbirthplus-resources", "afterbirth-resources",
                                       "rebirth-resources", NULL};
static const char *const SETS[] = {"data", "dlc3.de", "dlc3.es", "dlc3.jp", "dlc3.kr", "dlc3.ru", "dlc3.zh",
                                   "data.jp", NULL};

/* the bytes the game will load at an inner path the port does not patch: a mod's, else the pack's */
static char *plain_effective(const char *inner, long *n, int *owned, char *who, size_t whocap) {
    *owned = 0;
    snprintf(who, whocap, "the pack");
    if (shim_usermods_peek) {
        char full[700];
        snprintf(full, sizeof(full), "%s%s", shim_pack_root(), inner);
        const char *mod = shim_usermods_peek(full);
        FILE *f = mod ? fopen(mod, "rb") : NULL;
        if (f) {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *b = len > 0 ? (char *) malloc((size_t) len) : NULL;
            int got = b && fread(b, 1, (size_t) len, f) == (size_t) len;
            fclose(f);
            if (got) { *owned = 1; *n = len; snprintf(who, whocap, "%s", mod); return b; }
            free(b);
        }
    }
    return (char *) shim_pack_peek(inner, n);
}

static const char *const Q_WORD[] = {"layer", "null", "layers of", "animation", "animation"};

/*
 * The answer to one question about rel in the file the game will load: every
 * copy must agree (the image's words carry one number); a frame count is the
 * smallest of them. -1 and a report line when the name is missing anywhere,
 * the copies disagree, or there is no such file.
 */
#define CACHE 64
static struct { char rel[96]; char name[40]; char q; int v; } g_cache[CACHE];
static int g_cached;

static int ask_locked(const char *rel, int q, const char *name);
static pthread_mutex_t g_ask_lock = PTHREAD_MUTEX_INITIALIZER;

/* asked from the Java thread at start and from the game's thread later: one at a time */
static int ask(const char *rel, int q, const char *name) {
    pthread_mutex_lock(&g_ask_lock);
    int v = ask_locked(rel, q, name);
    pthread_mutex_unlock(&g_ask_lock);
    return v;
}

static int ask_locked(const char *rel, int q, const char *name) {
    if (!name) name = "";
    for (int i = 0; i < g_cached; i++)
        if (g_cache[i].q == q && !strcmp(g_cache[i].rel, rel) && !strcmp(g_cache[i].name, name))
            return g_cache[i].v;
    int answer = -1, seen = 0, refused = 0;
    char t[600];
    size_t rl = strlen(rel);
    char inner[300];
    const char *copies[64];
    int ncopies = 0;
    static char storage[64][300];
    for (int i = 0; i < g_entries && ncopies < 64; i++) {
        const char *tg = g_entry[i].target;
        size_t tl = strlen(tg);
        if (tl > rl && tg[tl - rl - 1] == '/' && ascii_eq_fold(tg + tl - rl, rl, rel, rl)) copies[ncopies++] = tg;
    }
    int patched = ncopies > 0;
    for (int e = 0; !patched && EDITIONS[e] && !ncopies; e++)
        for (int k = 0; SETS[k] && ncopies < 64; k++) {
            snprintf(inner, sizeof(inner), "%s/%s/%s", EDITIONS[e], SETS[k], rel);
            if (shim_pack_peek(inner, NULL)) {
                snprintf(storage[ncopies], sizeof(storage[0]), "%s", inner);
                copies[ncopies] = storage[ncopies];
                ncopies++;
            }
        }
    /* a file the port brings in itself (eid.c bring_in: "gfx/...", "resources/data/gfx/...") */
    static const char *const OWN[] = {"%s", "resources/data/%s", NULL};
    for (int k = 0; !patched && !ncopies && OWN[k]; k++) {
        snprintf(inner, sizeof(inner), OWN[k], rel);
        if (shim_pack_peek(inner, NULL)) {
            snprintf(storage[0], sizeof(storage[0]), "%s", inner);
            copies[0] = storage[0];
            ncopies = 1;
        }
    }
    for (int i = 0; i < ncopies && !refused; i++) {
        long n = 0;
        int owned;
        char who[400];
        char *b = patched ? effective(entry_for(copies[i]), &n, &owned, who, sizeof(who))
                          : plain_effective(copies[i], &n, &owned, who, sizeof(who));
        int v = b ? pp_query(b, n, q, name) : -2;
        if (owned) free(b);
        if (v < 0) {
            snprintf(t, sizeof(t), "%s %s is not in %s (%s) - the change that needs it is left out",
                     Q_WORD[q], q == Q_LAYERS ? "the file" : name, copies[i], who);
            report_line("port layer", t);
            refused = 1;
            break;
        }
        if (seen && v != answer) {
            if (q == Q_FRAMES || q == Q_LAYERS || q == Q_LENGTH) { if (v < answer) answer = v; continue; }
            snprintf(t, sizeof(t), "%s %s is %d in one copy of %s and %d in %s - the change that needs it is left out",
                     Q_WORD[q], name, answer, rel, v, copies[i]);
            report_line("port layer", t);
            refused = 1;
            break;
        }
        answer = v; seen = 1;
    }
    if (refused) answer = -1;
    else if (!seen) {
        snprintf(t, sizeof(t), "%s: no such file - %s %s unknown", rel, Q_WORD[q], name);
        report_line("port layer", t);
        answer = -1;
    }
    if (g_cached < CACHE) {
        snprintf(g_cache[g_cached].rel, sizeof(g_cache[g_cached].rel), "%s", rel);
        snprintf(g_cache[g_cached].name, sizeof(g_cache[g_cached].name), "%s", name);
        g_cache[g_cached].q = (char) q;
        g_cache[g_cached].v = answer;
        g_cached++;
    }
    LOG("portpatch: %s %s in %s = %d", Q_WORD[q], name, rel, answer);
    return answer;
}

int shim_port_layer(const char *rel, const char *name) { return ask(rel, Q_LAYER, name); }
int shim_port_null(const char *rel, const char *name) { return ask(rel, Q_NULL, name); }
/* how many layers the file has (the smallest of its copies) */
int shim_port_layer_count(const char *rel) { return ask(rel, Q_LAYERS, NULL); }
/* the FrameNum of an animation (the smallest of the copies) */
int shim_port_frames(const char *rel, const char *anim) { return ask(rel, Q_FRAMES, anim); }
/* the ticks an animation's longest layer covers (the smallest of the copies) */
int shim_port_length(const char *rel, const char *anim) { return ask(rel, Q_LENGTH, anim); }
#endif
