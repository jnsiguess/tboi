/*
 * std::string, Apple style.
 *
 * The same class of trap as variadic arguments and directory entries, and the
 * most expensive of the three: Apple's libc++ and Android's lay a string out
 * DIFFERENTLY.
 *
 *   Apple:    data +0, size +8, capacity +16, the "long" flag is the top bit
 *             of byte 0x17; a short string lives inside the object itself and
 *             keeps its length in that same byte 0x17.
 *   Android:  capacity +0, size +8, data +16, the flag is the low bit of byte 0.
 *
 * The game inlines string handling straight into its own code — the
 * disassembly shows it reading byte 0x17 and taking the pointer from zero —
 * while calling the outside operations (append, insert, operator +) by name.
 * Those names cannot be handed to the system libc++: each side would read the
 * other's fields, and the failure would surface not where the mistake is but
 * on some random pointer much later.
 *
 * So a dozen and a half names live here and work in Apple's layout. Memory
 * comes from the same operator new the game uses, so strings pass freely from
 * its code into ours and back.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

/*
 * Memory comes from exactly the operator new the game uses: its own calls
 * resolve to the system libc++, and a string built by the game's inlined code
 * ends up being destroyed here — and the other way round. So we take those
 * names from the same place instead of substituting malloc.
 */
static void *(*p_new)(unsigned long);
static void (*p_delete)(void *);

void *shim_android_symbol(const char *name);

static void take_allocator(void) {
    p_new = (void *(*)(unsigned long)) shim_android_symbol("_Znwm");
    p_delete = (void (*)(void *)) shim_android_symbol("_ZdlPv");
}

static void *_Znwm(unsigned long n) {
    if (!p_new) take_allocator();
    return p_new(n);
}

static void _ZdlPv(void *p) {
    if (!p_delete) take_allocator();
    p_delete(p);
}

#define LONG_BIT (((size_t) 1) << 63)
#define SHORT_CAP 22                 /* 23 bytes of data, the last one for the NUL */

typedef struct { char *data; size_t size; size_t cap; } str_t;

static int is_long(const void *s) { return (((const unsigned char *) s)[23] & 0x80) != 0; }

static char *sdata(void *s) { return is_long(s) ? ((str_t *) s)->data : (char *) s; }

static const char *sdata_c(const void *s) {
    return is_long(s) ? ((const str_t *) s)->data : (const char *) s;
}

static size_t ssize(const void *s) {
    return is_long(s) ? ((const str_t *) s)->size
                      : (size_t) (((const unsigned char *) s)[23] & 0x7f);
}

static size_t scap(const void *s) {
    return is_long(s) ? (((const str_t *) s)->cap & ~LONG_BIT) - 1 : SHORT_CAP;
}

static void set_size(void *s, size_t n) {
    if (is_long(s)) ((str_t *) s)->size = n;
    else ((unsigned char *) s)[23] = (unsigned char) n;
    sdata(s)[n] = 0;
}

/*
 * Grow while keeping what is written. Capacity is rounded up to a multiple of
 * sixteen, the same way libc++ itself does it and for the same reason:
 * allocating a byte per append is far too wasteful.
 */
static void reserve_at_least(void *s, size_t need) {
    if (need <= scap(s)) return;
    size_t want = need + 1;
    size_t twice = scap(s) * 2 + 1;
    if (want < twice) want = twice;
    want = (want + 15) & ~(size_t) 15;
    char *p = (char *) _Znwm(want);
    size_t n = ssize(s);
    memcpy(p, sdata(s), n + 1);
    if (is_long(s)) _ZdlPv(((str_t *) s)->data);
    str_t *L = (str_t *) s;
    L->data = p;
    L->size = n;
    L->cap = want | LONG_BIT;
}

static void set_from(void *s, const char *src, size_t n) {
    reserve_at_least(s, n);
    memmove(sdata(s), src, n);
    set_size(s, n);
}

/* ---------- construction and destruction ---------- */

void *shim_str_copy_ctor(void *self, const void *other) {
    ((unsigned char *) self)[23] = 0;
    set_from(self, sdata_c(other), ssize(other));
    return self;
}

void *shim_str_sub_ctor(void *self, const void *other, size_t pos, size_t n, const void *al) {
    (void) al;
    size_t have = ssize(other);
    if (pos > have) pos = have;
    if (n > have - pos) n = have - pos;
    const char *src = sdata_c(other) + pos;
    ((unsigned char *) self)[23] = 0;
    set_from(self, src, n);
    return self;
}

void shim_str_dtor(void *self) {
    if (is_long(self)) _ZdlPv(((str_t *) self)->data);
    ((unsigned char *) self)[23] = 0;
}

void *shim_str_assign_str(void *self, const void *other) {
    if (self != other) set_from(self, sdata_c(other), ssize(other));
    return self;
}

/* ---------- appending ---------- */

/*
 * Appending, including a string to itself.
 *
 * `s += s` used to read freed memory: making room can move the buffer, and the
 * source pointer still looked at the old one. So a source that lives inside us
 * is remembered by its offset and found again after the move.
 */
void *shim_str_append_n(void *self, const char *s, size_t n) {
    size_t have = ssize(self);
    const char *base = sdata_c(self);
    int inside = (s >= base && s <= base + have);
    size_t at = inside ? (size_t) (s - base) : 0;
    reserve_at_least(self, have + n);
    const char *from = inside ? sdata(self) + at : s;
    memmove(sdata(self) + have, from, n);
    set_size(self, have + n);
    return self;
}

void *shim_str_append(void *self, const char *s) { return shim_str_append_n(self, s, strlen(s)); }

void *shim_str_assign(void *self, const char *s) { set_from(self, s, strlen(s)); return self; }

void shim_str_push_back(void *self, char c) { shim_str_append_n(self, &c, 1); }

/* ---------- insert, erase, replace ---------- */

void *shim_str_insert_n(void *self, size_t pos, const char *s, size_t n) {
    size_t have = ssize(self);
    if (pos > have) pos = have;
    reserve_at_least(self, have + n);
    char *d = sdata(self);
    memmove(d + pos + n, d + pos, have - pos);
    memmove(d + pos, s, n);
    set_size(self, have + n);
    return self;
}

void *shim_str_insert(void *self, size_t pos, const char *s) {
    return shim_str_insert_n(self, pos, s, strlen(s));
}

void *shim_str_insert_fill(void *self, size_t pos, size_t n, char c) {
    size_t have = ssize(self);
    if (pos > have) pos = have;
    reserve_at_least(self, have + n);
    char *d = sdata(self);
    memmove(d + pos + n, d + pos, have - pos);
    memset(d + pos, c, n);
    set_size(self, have + n);
    return self;
}

void *shim_str_erase(void *self, size_t pos, size_t n) {
    size_t have = ssize(self);
    if (pos > have) pos = have;
    if (n > have - pos) n = have - pos;
    char *d = sdata(self);
    memmove(d + pos, d + pos + n, have - pos - n);
    set_size(self, have - n);
    return self;
}

void shim_str_resize(void *self, size_t n, char c) {
    size_t have = ssize(self);
    if (n > have) {
        reserve_at_least(self, n);
        memset(sdata(self) + have, c, n - have);
    }
    set_size(self, n);
}

void *shim_str_replace_n(void *self, size_t pos, size_t n1, const char *s, size_t n2) {
    size_t have = ssize(self);
    if (pos > have) pos = have;
    if (n1 > have - pos) n1 = have - pos;
    /*
     * The copy is there for the case of inserting a piece of this very string:
     * after a grow the old pointer leads nowhere.
     */
    char small[64];
    char *tmp = (n2 <= sizeof(small)) ? small : (char *) _Znwm(n2);
    memcpy(tmp, s, n2);
    reserve_at_least(self, have - n1 + n2);
    char *d = sdata(self);
    memmove(d + pos + n2, d + pos + n1, have - pos - n1);
    memmove(d + pos, tmp, n2);
    set_size(self, have - n1 + n2);
    if (tmp != small) _ZdlPv(tmp);
    return self;
}

void *shim_str_replace(void *self, size_t pos, size_t n1, const char *s) {
    return shim_str_replace_n(self, pos, n1, s, strlen(s));
}

/* ---------- the calls that return a whole string ---------- */

/*
 * The result is wider than sixteen bytes, so by the arm64 convention the room
 * for it arrives in x8 rather than x0. The assembly wrapper does the shuffle;
 * by the time it gets here the address is an ordinary argument.
 */
void shim_str_to_string(long long v, void *ret) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", v);
    ((unsigned char *) ret)[23] = 0;
    set_from(ret, buf, (size_t) n);
}

void shim_str_concat_cs(const char *lhs, const void *rhs, void *ret) {
    size_t a = strlen(lhs), b = ssize(rhs);
    const char *rp = sdata_c(rhs);
    ((unsigned char *) ret)[23] = 0;
    reserve_at_least(ret, a + b);
    char *d = sdata(ret);
    memcpy(d, lhs, a);
    memcpy(d + a, rp, b);
    set_size(ret, a + b);
}

/* ---------- string stream buffer ---------- */

/*
 * The game inlines the whole of stringstream and only calls out for
 * str(string). The object's layout is Apple's, so this work has to happen here
 * too: the system libc++ would read the string through its own fields and send
 * memmove into nowhere — which is exactly how it used to crash.
 *
 * Layout (shared by both libc++ builds; only the string differs):
 *   +0x00 vtable            +0x28 put area begin
 *   +0x08 locale            +0x30 put pointer
 *   +0x10 get area begin    +0x38 put area end
 *   +0x18 get pointer       +0x40 the string itself
 *   +0x20 get area end      +0x58 high mark, +0x60 mode
 */
typedef struct {
    void *vtable;
    void *loc;
    char *ginp, *gnext, *gend;
    char *pbeg, *pnext, *pend;
    char str[24];
    char *hm;
    int mode;
} strbuf_t;

#define MODE_APP 0x01
#define MODE_ATE 0x02
#define MODE_IN  0x08
#define MODE_OUT 0x10

void shim_strbuf_str(void *self, const void *src) {
    strbuf_t *b = (strbuf_t *) self;
    shim_str_assign_str(b->str, src);
    b->hm = NULL;
    if (b->mode & MODE_IN) {
        char *d = sdata(b->str);
        b->hm = d + ssize(b->str);
        b->ginp = d;
        b->gnext = d;
        b->gend = b->hm;
    }
    if (b->mode & MODE_OUT) {
        size_t had = ssize(b->str);
        b->hm = sdata(b->str) + had;
        shim_str_resize(b->str, scap(b->str), 0);
        char *d = sdata(b->str);
        b->pbeg = d;
        b->pnext = d;
        b->pend = d + ssize(b->str);
        if (b->mode & (MODE_APP | MODE_ATE)) b->pnext += had;
    }
}

/* ---------- comparison ---------- */

static int cmp_bytes(const char *a, size_t na, const char *b, size_t nb) {
    size_t n = na < nb ? na : nb;
    int r = n ? memcmp(a, b, n) : 0;
    if (r) return r;
    return na < nb ? -1 : (na > nb ? 1 : 0);
}

int shim_str_compare_c(const void *self, const char *s) {
    return cmp_bytes(sdata_c(self), ssize(self), s, strlen(s));
}

int shim_str_compare_sub_c(const void *self, size_t pos, size_t n1, const char *s) {
    size_t have = ssize(self);
    if (pos > have) pos = have;
    if (n1 > have - pos) n1 = have - pos;
    return cmp_bytes(sdata_c(self) + pos, n1, s, strlen(s));
}

/* ---------- buffer behaviour that depends on the string ---------- */

/*
 * Out of the buffer's whole vtable only two entries touch the string:
 * overflow, when there is no room left to write, and destruction. The rest
 * walk the begin and end pointers, which both libc++ builds keep in the same
 * place — those can stay the system's. So we take Android's table and patch it
 * in two spots instead of writing our own: less code and fewer ways to drift
 * apart.
 */
#define SB_EOF (-1)

int shim_strbuf_overflow(void *self, int c) {
    strbuf_t *b = (strbuf_t *) self;
    if (c == SB_EOF) return 0;                      /* not_eof(eof) */
    long ninp = b->gnext - b->ginp;
    if (b->pnext == b->pend) {
        if (!(b->mode & MODE_OUT)) return SB_EOF;
        long nout = b->pnext - b->pbeg;
        long hm = b->hm - b->pbeg;
        shim_str_push_back(b->str, 0);
        shim_str_resize(b->str, scap(b->str), 0);
        char *p = sdata(b->str);
        b->pbeg = p;
        b->pnext = p + nout;
        b->pend = p + ssize(b->str);
        b->hm = p + hm;
    }
    if (b->pnext + 1 > b->hm) b->hm = b->pnext + 1;
    if (b->mode & MODE_IN) {
        char *p = sdata(b->str);
        b->ginp = p;
        b->gnext = p + ninp;
        b->gend = b->hm;
    }
    *b->pnext++ = (char) c;
    return (unsigned char) c;
}

/* the result is a whole string; the room for it arrives in x8 (see the wrapper) */
void shim_strbuf_str_get(const void *self, void *ret) {
    const strbuf_t *b = (const strbuf_t *) self;
    ((unsigned char *) ret)[23] = 0;
    const char *from = NULL, *to = NULL;
    if (b->mode & MODE_OUT) {
        from = b->pbeg;
        to = (b->hm > b->pnext) ? b->hm : b->pnext;
    } else if (b->mode & MODE_IN) {
        from = b->ginp;
        to = b->gend;
    }
    set_from(ret, from ? from : "", (from && to > from) ? (size_t) (to - from) : 0);
}

static void strbuf_release(void *self) { shim_str_dtor(((strbuf_t *) self)->str); }

static void strbuf_delete(void *self) { strbuf_release(self); _ZdlPv(self); }

/*
 * The buffer's vtable. The game looks it up by name and installs it in the
 * object itself, adding sixteen: the first two words are housekeeping (offset
 * to top and type info), the methods follow. We copy the system's table whole
 * so the type info stays genuine, and replace three slots.
 */
#define SB_SLOTS 16
static void *g_sb_vtable[SB_SLOTS];

void *shim_strbuf_vtable(void) {
    if (g_sb_vtable[1]) return g_sb_vtable;
    void **src = (void **) shim_android_symbol(
        "_ZTVNSt3__115basic_stringbufIcNS_11char_traitsIcEENS_9allocatorIcEEEE");
    if (!src) { LOG("the system has no string-buffer vtable"); return NULL; }
    LOG("our own string-buffer vtable installed (system one at %p)", (void *) src);
    memcpy(g_sb_vtable, src, sizeof(g_sb_vtable));
    g_sb_vtable[2] = (void *) strbuf_release;     /* ~basic_stringbuf          */
    g_sb_vtable[3] = (void *) strbuf_delete;      /* ~basic_stringbuf and free */
    g_sb_vtable[15] = (void *) shim_strbuf_overflow;
    return g_sb_vtable;
}

/* ---------- for the rest of the layer ---------- */

const char *shim_str_bytes(const void *s) { return sdata_c(s); }
size_t shim_str_length(const void *s) { return ssize(s); }
