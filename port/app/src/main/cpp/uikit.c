/*
 * A stand-in for UIKit and Foundation: only what the game asks for.
 *
 * Every method here appeared not in advance but after a trace showed it in the
 * list of misses. That is why the list is short and grows exactly as the need
 * arises.
 *
 * The arrangement is simple: objects are ordinary blocks of memory whose first
 * field is the class and whose remaining fields are our own. The iOS classes in
 * the image have no fields at all, so the layout is ours to define.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)      /* the private build has no log */
#endif

void *shim_objc_make_class(const char *bind_name);
void *shim_objc_alloc(void *cls);
void *shim_objc_opt_class(void *o);
void *shim_objc_msgSend(void *self, const char *sel, ...);
void shim_objc_set_builtins(const void *table);
int shim_gl_start(void);
int shim_gl_make_current(void);
int shim_gl_bind_drawable(void);
int shim_gl_present(void);
int shim_gl_width(void);
int shim_gl_height(void);
static double screen_scale(void);

typedef struct { void *isa; } object_t;

/* find or create one of our classes by its short name */
static void *cls_named(const char *name) {
    char buf[80];
    snprintf(buf, sizeof(buf), "_OBJC_CLASS_$_%s", name);
    return shim_objc_make_class(buf);
}

static void *make(const char *cls_name) {
    void *cls = cls_named(cls_name);
    return cls ? shim_objc_alloc(cls) : NULL;
}

/* gamepad: the answers live in pad.c, here only the entries in the shared table */
void *shim_pad_controllers(void *, const char *);
void *shim_pad_gamepad(void *, const char *);
long shim_pad_player_index(void *, const char *);
void shim_pad_set_player_index(void *, const char *, long);
void *shim_pad_name(void *, const char *);
void *shim_pad_buttonA(void *, const char *);
void *shim_pad_buttonB(void *, const char *);
void *shim_pad_buttonX(void *, const char *);
void *shim_pad_buttonY(void *, const char *);
void *shim_pad_L1(void *, const char *);
void *shim_pad_R1(void *, const char *);
void *shim_pad_L2(void *, const char *);
void *shim_pad_R2(void *, const char *);
void *shim_pad_menu(void *, const char *);
void *shim_pad_options(void *, const char *);
void *shim_pad_dpad(void *, const char *);
void *shim_pad_left_stick(void *, const char *);
void *shim_pad_right_stick(void *, const char *);
long shim_pad_is_pressed(void *, const char *);
float shim_pad_force(void *, const char *);
void *shim_pad_x(void *, const char *);
void *shim_pad_y(void *, const char *);
void *shim_pad_up(void *, const char *);
void *shim_pad_down(void *, const char *);
void *shim_pad_left(void *, const char *);
void *shim_pad_right(void *, const char *);
float shim_pad_axis_value(void *, const char *);

/* ---------- NSObject ---------- */

/*
 * Without these three nothing gets created: IsaacShell writes no init of its
 * own and inherits one, and alloc and new are called from everywhere.
 */
static void *ns_init(void *self, const char *sel) { (void) sel; return self; }
static void *ns_alloc(void *cls, const char *sel) { (void) sel; return shim_objc_alloc(cls); }
static void *ns_new(void *cls, const char *sel) {
    (void) sel;
    void *o = shim_objc_alloc(cls);
    return o ? shim_objc_msgSend(o, "init") : NULL;
}
static void *ns_self(void *self, const char *sel) { (void) sel; return self; }
static void ns_void(void *self, const char *sel) { (void) self; (void) sel; }
static long ns_yes(void *self, const char *sel) { (void) self; (void) sel; return 1; }
static long ns_no(void *self, const char *sel) { (void) self; (void) sel; return 0; }

/* ---------- NSDictionary ---------- */

/*
 * The game puts the properties of the drawing layer into a dictionary: the
 * colour format and the "keep the contents" flag. It is not the game that reads
 * them afterwards but we ourselves, so the dictionary can be anything — as long
 * as the pairs are in it.
 */
typedef struct {
    void *isa;
    long count;
    void *keys[8];
    void *objs[8];
} dict_t;

static void *dict_with(void *cls, const char *sel, void **objs, void **keys, long count) {
    (void) sel;
    dict_t *d = (dict_t *) calloc(1, sizeof(dict_t));
    if (!d) return NULL;
    d->isa = cls;
    d->count = count > 8 ? 8 : count;
    for (long i = 0; i < d->count; i++) { d->keys[i] = keys[i]; d->objs[i] = objs[i]; }
    LOG("NSDictionary: %ld pairs", d->count);
    return d;
}

/* ---------- UIViewController and the view ---------- */

void *shim_objc_class_named(const char *);   /* the real class from the image */

/*
 * The view is created on demand and lives alone: we have only one screen
 * anyway, and it cannot be kept in the controller's fields — the field layout
 * is set by the image and there is no free room there.
 */
static void *g_view, *g_layer, *g_screen;

static void *vc_view(void *self, const char *sel) {
    (void) self; (void) sel;
    if (!g_view) {
        /*
         * On iOS the controller's view is the game's own class AAPLOpenGLView.
         * Text input lives on it: insertText:, deleteBackward, hasText,
         * canBecomeFirstResponder. By substituting a plain UIView stub we were
         * depriving the game of its own methods, and there was nothing to type
         * seeds with — the keyboard came up but the letters had nowhere to go.
         *
         * Everything else we serve on behalf of UIView stays reachable: the
         * method lookup walks up the chain of ancestors.
         */
        /*
         * The controller's view is the game's own class, just as on iOS. Text
         * input lives on it (insertText:, deleteBackward, hasText), and without
         * it there is nothing to type seeds with.
         *
         * The class is taken REAL, from the image: make() would have created a
         * same-named blank without a single method of the game. The first
         * attempt broke touches on this — but the cause was not the class, it
         * was the method lookup: it did not walk up the chain of ancestors, and
         * the object never reached the UIView stubs. The lookup is fixed, its
         * ancestry is AAPLOpenGLView -> UIView.
         */
        void *cls = shim_objc_class_named("AAPLOpenGLView");
        g_view = cls ? shim_objc_alloc(cls) : make("UIView");
        LOG("view created: %p", g_view);
    }
    return g_view;
}

static void *view_layer(void *self, const char *sel) {
    (void) self; (void) sel;
    if (!g_layer) { g_layer = make("CAEAGLLayer"); LOG("layer created: %p", g_layer); }
    return g_layer;
}

static void *screen_main(void *cls, const char *sel) {
    (void) cls; (void) sel;
    if (!g_screen) g_screen = make("UIScreen");
    return g_screen;
}

/* ---------- NSString ---------- */

/*
 * A string is an ordinary block of memory: the class, a pointer to the text in
 * UTF-8 and a length. We do not need to tell encodings apart: the game uses
 * only UTF-8 and ASCII, and here they coincide.
 */
typedef struct {
    void *isa;
    char *buf;
    long len;
} nsstr_t;

static void *g_str_cls;

/*
 * The image's own constant strings are laid out DIFFERENTLY from ours.
 *
 *   our nsstr_t           { class, char *text, length }
 *   constant __cfstring   { class, flags,      const char *text, length }
 *
 * Their text is at +16, and at +8 sit the flags — 0x7c8 in this build. We read
 * +8 as a pointer to the text and called strlen on it: the game died with
 * SIGSEGV at address 0x7c8. It showed up on death, where the engine does
 * [NSString stringWithFormat:@"%s", path].
 *
 * They are told apart by address: constants live in the section
 * __DATA_CONST,__cfstring, ours live on the heap. The loader supplies the
 * section bounds.
 */
typedef struct {
    void *isa;
    long flags;
    const char *bytes;
    long len;
} cfstr_t;

static uintptr_t g_cf_lo, g_cf_hi;

void shim_str_set_cfstrings(void *lo, unsigned long size) {
    g_cf_lo = (uintptr_t) lo;
    g_cf_hi = g_cf_lo + size;
    LOG("image strings: constants %p..%p", lo, (char *) lo + size);
}

static const cfstr_t *as_cfstring(const void *o) {
    uintptr_t a = (uintptr_t) o;
    if (!g_cf_lo || a < g_cf_lo || a >= g_cf_hi) return NULL;
    return (const cfstr_t *) o;
}

void *shim_str_make(const char *text) {
    if (!g_str_cls) g_str_cls = cls_named("NSString");
    nsstr_t *s = (nsstr_t *) shim_objc_alloc(g_str_cls);
    if (!s) return NULL;
    const char *src = text ? text : "";
    s->len = (long) strlen(src);
    s->buf = (char *) malloc((size_t) s->len + 1);
    if (s->buf) memcpy(s->buf, src, (size_t) s->len + 1);
    return s;
}

/* the text of a string: both for the game and for ourselves */
const char *shim_str_text(void *o) {
    const cfstr_t *c = as_cfstring(o);
    if (c) return c->bytes ? c->bytes : "";
    nsstr_t *s = (nsstr_t *) o;
    return (s && s->buf) ? s->buf : "";
}

static void *str_with_cstring(void *cls, const char *sel, const char *text, long enc) {
    (void) cls; (void) sel; (void) enc;
    return shim_str_make(text);
}

static void *str_with_utf8(void *cls, const char *sel, const char *text) {
    (void) cls; (void) sel;
    return shim_str_make(text);
}

static const char *str_utf8(void *self, const char *sel) {
    (void) sel;
    nsstr_t *s = (nsstr_t *) self;
    if (!as_cfstring(self) && (!s || !s->buf))
        LOG("UTF8String on an empty string (%p) — returning empty", self);
    return shim_str_text(self);
}

static const char *str_cstring_enc(void *self, const char *sel, long enc) {
    (void) sel; (void) enc;
    return shim_str_text(self);
}

static long str_length(void *self, const char *sel) {
    (void) sel;
    const cfstr_t *c = as_cfstring(self);
    if (c) return c->len;
    nsstr_t *s = (nsstr_t *) self;
    return s ? s->len : 0;
}

static long str_equal(void *self, const char *sel, void *other) {
    (void) sel;
    return !strcmp(shim_str_text(self), shim_str_text(other));
}

static void *str_append(void *self, const char *sel, void *other) {
    (void) sel;
    const char *a = shim_str_text(self), *b = shim_str_text(other);
    char *tmp = (char *) malloc(strlen(a) + strlen(b) + 1);
    if (!tmp) return self;
    strcpy(tmp, a); strcat(tmp, b);
    void *r = shim_str_make(tmp);
    free(tmp);
    return r;
}

static void *str_append_path(void *self, const char *sel, void *other) {
    (void) sel;
    const char *a = shim_str_text(self), *b = shim_str_text(other);
    char *tmp = (char *) malloc(strlen(a) + strlen(b) + 2);
    if (!tmp) return self;
    snprintf(tmp, strlen(a) + strlen(b) + 2, "%s/%s", a, b);
    void *r = shim_str_make(tmp);
    free(tmp);
    return r;
}

/*
 * Building a string from a format.
 *
 * This used to hand back the format itself. While the engine called it only
 * from the removed StoreKit and GameKit that stayed invisible; on death it
 * builds a file path this way — [NSString stringWithFormat:@"%s", path] — and
 * instead of the path out came the string "%s".
 *
 * The game's variadic arguments lie the Apple way, on the stack: the thunk in
 * objc_asm.S catches them and passes them here as the fourth argument. After
 * that the trick is the same as in shim_sprintf_impl — an AAPCS64 argument list
 * with an empty register part.
 *
 * An ordinary vsnprintf does not know %@. Its argument is the same size as the
 * one for %s — a pointer — so the format is rewritten into %s and the argument
 * itself is swapped for the object's text. The arguments are copied to us:
 * somebody else's frame is not ours to write into.
 */
void shim_str_with_format_abi(void);   /* the thunk from objc_asm.S: it catches the argument stack */

typedef struct { void *stack, *gr_top, *vr_top; int gr_offs, vr_offs; } va_apple_t;

static const char *object_text(void *o) {
    if (!o) return "(null)";
    if (as_cfstring(o)) return shim_str_text(o);
    nsstr_t *s = (nsstr_t *) o;
    if (g_str_cls && s->isa == g_str_cls && s->buf) return s->buf;
    return "<object>";      /* not a string — guessing and following the pointer is not allowed */
}

void *shim_str_with_format(void *cls, const char *sel, void *fmt, void *args) {
    (void) cls; (void) sel;
    const char *f = shim_str_text(fmt);
    const unsigned long *src = (const unsigned long *) args;
    unsigned long ours[16];
    char pat[512];
    size_t o = 0;
    int slot = 0, whole = 1;

    for (size_t i = 0; f[i]; i++) {
        if (f[i] != '%') {
            if (o + 1 >= sizeof(pat)) { whole = 0; break; }
            pat[o++] = f[i];
            continue;
        }
        if (f[i + 1] == '%') {
            if (o + 2 >= sizeof(pat)) { whole = 0; break; }
            pat[o++] = '%'; pat[o++] = '%'; i++;
            continue;
        }
        size_t from = i++;                          /* start of the conversion */
        while (f[i] && !strchr("diouxXeEfgGaAcsp@", f[i])) i++;
        if (!f[i] || slot >= (int) (sizeof(ours) / sizeof(ours[0]))
                  || o + (i - from) + 2 >= sizeof(pat)) { whole = 0; break; }
        ours[slot] = src ? src[slot] : 0;
        memcpy(pat + o, f + from, i - from);
        o += i - from;
        if (f[i] == '@') {
            ours[slot] = (unsigned long) (uintptr_t) object_text((void *) (uintptr_t) ours[slot]);
            pat[o++] = 's';
        } else {
            pat[o++] = f[i];
        }
        slot++;
    }
    if (!whole) {
        LOG("stringWithFormat: format '%s' is beyond us — handed back as is", f);
        return shim_str_make(f);
    }
    pat[o] = 0;

    va_apple_t v;
    v.stack = ours; v.gr_top = ours; v.vr_top = ours;
    v.gr_offs = 0; v.vr_offs = 0;
    char out[1024];
    vsnprintf(out, sizeof(out), pat, *(va_list *) &v);

    static int said;
    if (said < 20) { said++; LOG("stringWithFormat '%s' -> '%s'", f, out); }
    return shim_str_make(out);
}

/* a mutable string: created empty and appended to */
/*
 * NSString encodings. The game uses more than UTF-8: a measurement showed a
 * one-character string four bytes long, that is, UTF-32. While the encoding
 * argument was ignored, such text landed in the buffer together with its
 * zeroes, and the very next read saw an empty string — what had been typed got
 * wiped every other time.
 *
 * The values come from Foundation: 1 ASCII, 4 UTF-8, 10 UTF-16, then the
 * endian-specific variants of UTF-16 and UTF-32.
 */
static int encoding_width(long enc) {
    switch ((unsigned long) enc) {
        case 10: case 0x90000100UL: case 0x94000100UL: return 2;
        case 0x8c000100UL: case 0x98000100UL: case 0x9c000100UL: return 4;
        default: return 1;
    }
}

static int big_endian(long enc) {
    unsigned long e = (unsigned long) enc;
    return e == 0x90000100UL || e == 0x98000100UL;
}

/* Write a character in UTF-8; returns how many bytes it took. */
static int to_utf8(unsigned long ch, char *dst) {
    if (ch < 0x80) { dst[0] = (char) ch; return 1; }
    if (ch < 0x800) {
        dst[0] = (char) (0xC0 | (ch >> 6));
        dst[1] = (char) (0x80 | (ch & 0x3F));
        return 2;
    }
    dst[0] = (char) (0xE0 | (ch >> 12));
    dst[1] = (char) (0x80 | ((ch >> 6) & 0x3F));
    dst[2] = (char) (0x80 | (ch & 0x3F));
    return 3;
}

/*
 * Comparing strings.
 *
 * Without it a miss returned zero, and zero in NSComparisonResult means
 * "equal". So on the seed screen every letter typed looked equal to the one the
 * game compares it with, and the keypress worked as a confirmation — the screen
 * closed instead of taking input.
 */
static long str_compare(void *self, const char *sel, void *other) {
    (void) sel;
    const char *a = shim_str_text(self);
    const char *b = shim_str_text(other);
    if (!a) a = "";
    if (!b) b = "";
    int r = strcmp(a, b);
    LOG("RECON compare: '%s' with '%s' -> %d", a, b, r < 0 ? -1 : (r > 0 ? 1 : 0));
    return r < 0 ? -1 : (r > 0 ? 1 : 0);
}

/*
 * Hand the text into the caller's buffer. The game takes back what was typed
 * with it; when there is not enough room Apple returns NO and leaves the buffer
 * alone — we do the same.
 */
static long str_get_cstring(void *self, const char *sel, char *buf, long max, long enc) {
    (void) sel; (void) enc;
    if (!buf || max <= 0) return 0;
    const char *t = shim_str_text(self);
    if (!t) t = "";
    size_t n = strlen(t);
    int width = encoding_width(enc);
    if (width > 1) {
        /* hand it back in the width that was asked for: seed characters are all ASCII */
        long need = (long) (n + 1) * width;
        if (need > max) { buf[0] = 0; return 0; }
        memset(buf, 0, (size_t) need);
        int big = big_endian(enc);
        for (size_t i = 0; i < n; i++)
            buf[i * (size_t) width + (big ? (size_t) width - 1 : 0)] = t[i];
        return 1;
    }
    if ((long) n + 1 > max) { buf[0] = 0; return 0; }
    memcpy(buf, t, n + 1);
    LOG("RECON getCString: handed '%s' (%zu) into a buffer of %ld, encoding %ld", t, n, max, enc);
    return 1;
}

static void *mstr_init_capacity(void *self, const char *sel, long cap) {
    (void) sel; (void) cap;
    nsstr_t *s = (nsstr_t *) self;
    if (s && !s->buf) { s->buf = (char *) calloc(1, 1); s->len = 0; }
    return self;
}

/*
 * A string from ready bytes. The game assembles the seed string with it, and
 * without it no buffer appeared for it at all: insertText: ran, and nothing
 * changed on the screen — the letter had nowhere to land.
 *
 * The encoding is not parsed. The game asks for either UTF-8 or ASCII, and for
 * both it is enough to copy the bytes as they are: we keep our own strings in
 * UTF-8 anyway.
 */
static void *mstr_init_bytes(void *self, const char *sel,
                             const void *bytes, long len, long enc) {
    (void) sel; (void) enc;
    nsstr_t *s = (nsstr_t *) self;
    if (!s) return NULL;
    if (len < 0) len = 0;
    int width = encoding_width(enc);
    char *fresh = (char *) calloc(1, (size_t) len * 3 + 4);   /* with room to spare for UTF-8 */
    if (!fresh) return NULL;
    if (bytes && len) {
        if (width == 1) {
            memcpy(fresh, bytes, (size_t) len);
            len = (long) strlen(fresh);
        } else {
            const unsigned char *b = (const unsigned char *) bytes;
            int big = big_endian(enc);
            long made = 0;
            for (long i = 0; i + width <= len; i += width) {
                unsigned long ch = 0;
                for (int j = 0; j < width; j++)
                    ch |= (unsigned long) b[i + (big ? width - 1 - j : j)] << (8 * j);
                if (!ch) break;                 /* end of string */
                made += to_utf8(ch, fresh + made);
            }
            fresh[made] = 0;
            len = made;
        }
    }
    free(s->buf);
    s->buf = fresh;
    s->len = len;
    LOG("RECON initWithBytes: object %p got '%s' (%ld), encoding %ld, width %d", self, fresh, len, enc, width);
    return self;
}

static void mstr_append(void *self, const char *sel, void *other) {
    (void) sel;
    nsstr_t *s = (nsstr_t *) self;
    if (!s) return;
    const char *b = shim_str_text(other);
    size_t n = (s->buf ? strlen(s->buf) : 0) + strlen(b) + 1;
    char *tmp = (char *) malloc(n);
    if (!tmp) return;
    snprintf(tmp, n, "%s%s", s->buf ? s->buf : "", b);
    free(s->buf);
    s->buf = tmp;
    s->len = (long) strlen(tmp);
}

/*
 * Delete a piece of a string. The game erases a character on the seed entry
 * screen with this: `virtualKeyboardDeleteBackward` takes the length of the
 * storage and asks for the last character to be removed. Without this method a
 * press of "erase" reached the game and sank — a measurement on the device
 * showed exactly that:
 *
 *     CALL [AAPLOpenGLView deleteBackward]
 *     CALL [IsaacShell virtualKeyboardDeleteBackward]
 *     NO METHOD [NSMutableString deleteCharactersInRange:]
 *
 * The range here is an NSRange, and that is a STRUCTURE OF TWO WORDS: it
 * arrives in two registers, not as a single pointer. Registers have to be
 * counted, not arguments.
 *
 * We keep the length in bytes (that is what `length` hands back as well), so we
 * cut by bytes too — consistent with ourselves. One caveat: if the cut fell in
 * the middle of a multi-byte character, we step back to its start, otherwise
 * the string is left with a tail and no head. Seeds are typed in Latin letters,
 * but the owner's keyboard is Russian, and half a letter would show up there
 * easily.
 */
static void mstr_delete_range(void *self, const char *sel, long start, long count) {
    (void) sel;
    nsstr_t *s = (nsstr_t *) self;
    if (!s || !s->buf || count <= 0 || start < 0 || start >= s->len) return;
    if (start + count > s->len) count = s->len - start;
    while (start > 0 && ((unsigned char) s->buf[start] & 0xC0) == 0x80) {
        start--;
        count++;
    }
    memmove(s->buf + start, s->buf + start + count,
            (size_t) (s->len - start - count) + 1);
    s->len -= count;
}

/* ---------- NSArray ---------- */

/*
 * The list is needed for one thing: to hand back the contents of a folder. So
 * it is simple — an array of pointers of a given length, never changed after
 * creation.
 */
typedef struct {
    void *isa;
    long count;
    void **items;
} nsarr_t;

static void *g_arr_cls;

static void *arr_make(long n) {
    if (!g_arr_cls) g_arr_cls = cls_named("NSArray");
    nsarr_t *a = (nsarr_t *) shim_objc_alloc(g_arr_cls);
    if (!a) return NULL;
    a->count = n;
    a->items = n ? (void **) calloc((size_t) n, sizeof(void *)) : NULL;
    return a;
}

/* an array from ready objects: the gamepad needs it for +[GCController controllers] */
void *shim_arr_make_from(void **src, long count) {
    nsarr_t *a = (nsarr_t *) arr_make(count);
    if (a && src)
        for (long i = 0; i < count; i++) a->items[i] = src[i];
    return a;
}

static long arr_count(void *self, const char *sel) {
    (void) sel;
    nsarr_t *a = (nsarr_t *) self;
    return a ? a->count : 0;
}

static void *arr_at(void *self, const char *sel, long i) {
    (void) sel;
    nsarr_t *a = (nsarr_t *) self;
    return (a && i >= 0 && i < a->count) ? a->items[i] : NULL;
}

/*
 * Where in the list this object sits.
 *
 * Apple answers NSNotFound - NSIntegerMax - when it is not there at all, and
 * the answer matters: the controller menu asks the list of pads where the one
 * it remembers is, and a missing method used to answer zero. Zero means "the
 * first one", so the menu pointed at a device that was not in the list, and
 * the key-binding screen it opened had nothing to read from.
 *
 * The comparison is by identity: our objects are single copies, one per pad,
 * and the game hands back the very object it was given.
 */
#define NS_NOT_FOUND 0x7fffffffffffffffL

static long arr_index_of(void *self, const char *sel, void *obj) {
    (void) sel;
    nsarr_t *a = (nsarr_t *) self;
    if (!a || !a->items) return NS_NOT_FOUND;
    for (long i = 0; i < a->count; i++)
        if (a->items[i] == obj) return i;
    return NS_NOT_FOUND;
}

static char arr_contains(void *self, const char *sel, void *obj) {
    return arr_index_of(self, sel, obj) != NS_NOT_FOUND;
}

static void *arr_first(void *self, const char *sel) { return arr_at(self, sel, 0); }
static void *arr_last(void *self, const char *sel) {
    nsarr_t *a = (nsarr_t *) self;
    return a && a->count ? arr_at(self, sel, a->count - 1) : NULL;
}

/* ---------- keyboard ---------- */

/*
 * Seed entry. The game asks for a keyboard exactly as it would on an iPhone: it
 * configures its look (setKeyboardType: and friends) and then sends the view
 * becomeFirstResponder. After that the system shows the keyboard itself and
 * hands back what was typed through insertText: and deleteBackward.
 *
 * Here we are the system: we show the Android keyboard and make the same calls.
 * The keyboard appearance properties are accepted in silence — Android does not
 * ask for them, and without an answer the game logged a miss for every one.
 */
static void *g_first_responder;                  /* the view that was given input focus */
void shim_ime_show(int show);       /* the bridge into Java, in loader.c */

static long view_become_first(void *self, const char *sel) {
    (void) sel;
    g_first_responder = self;
    shim_ime_show(1);
    return 1;
}

static long view_resign_first(void *self, const char *sel) {
    (void) sel;
    if (g_first_responder == self) g_first_responder = NULL;
    shim_ime_show(0);
    return 1;
}

static long view_is_first(void *self, const char *sel) {
    (void) sel;
    return g_first_responder == self;
}

static void view_trait(void *self, const char *sel, long v) {
    (void) self; (void) sel; (void) v;
}

/*
 * The receiver of what is typed is whoever asked the game for focus. That is
 * now its real view, and a separate object for input is no longer needed: it
 * was a workaround while the controller's view was a stub.
 */
void *shim_ime_target(void) { return g_first_responder; }


/* ---------- path translation ---------- */

/*
 * The game carries the iOS sandbox paths inside it: /Documents, /Library, /tmp.
 * On Android there are no such folders and there cannot be — they belong to the
 * system. So their base is swapped here for a folder of our own.
 *
 * This is not a fix for one case but the layer any port has: mapping somebody
 * else's file arrangement onto our own. All file access goes through it.
 */
const char *shim_home_dir(void);
const char *shim_saves_dir(void);
const char *shim_sticks_anm2(const char *);

const char *shim_map_path(const char *p) {
    static char buf[1024];
    if (!p || p[0] != '/') return p;
    /*
     * The /private prefix is put on by us: the game is shown its sandbox just
     * as it looks on an iPhone. Here, at the border of the system calls, it is
     * taken back off — beyond this point the real Android path begins.
     */
    if (strncmp(p, "/private/", 9) == 0) p += 8;
    /* the game's own path, before any mod swaps it: the dance's marker (mods.c) */
    { void shim_mods_path_asked(const char *); shim_mods_path_asked(p); }
    /* the size of the sticks is set by an animation file: substitute the right variant */
    { const char *alt = shim_sticks_anm2(p); if (alt) p = alt; }
    /* a resource an enabled user mod holds is read from the mod (usermods.c) */
    { const char *shim_usermods_map(const char *);
      const char *mod = shim_usermods_map(p); if (mod) return mod; }
    /*
     * The game carries the sandbox paths in ABSOLUTE form: not "/Documents/..."
     * but "/private<home>/Documents/..." — that is how Foundation hands them
     * out. So the home is taken off the front, and the common root parsing
     * works from there.
     *
     * It is taken off only if a sandbox root follows: the resources live under
     * the same home, and "/repentance-resources/..." would not be found
     * without it.
     */
    { const char *home = shim_home_dir();
      size_t d = home ? strlen(home) : 0;
      if (d && strncmp(p, home, d) == 0 && p[d] == '/') {
          static const char *roots[] = {"/Documents", "/Library", "/tmp", "/var", NULL};
          const char *tail = p + d;
          for (int i = 0; roots[i]; i++) {
              size_t m = strlen(roots[i]);
              if (strncmp(tail, roots[i], m) == 0 && (tail[m] == 0 || tail[m] == '/')) {
                  p = tail;
                  break;
              }
          }
      } }
    /*
     * Saves are led away into the folder that was opened: the game asks for
     * /Documents/Repentance, and we hand back the one the owner can reach with
     * a file manager. The other sandbox roots (/Library, /tmp, /var) are
     * internal, their place is inside.
     */
    { const char *saves = shim_saves_dir();
      if (saves && strncmp(p, "/Documents", 10) == 0 && (p[10] == 0 || p[10] == '/')) {
          snprintf(buf, sizeof(buf), "%s%s", saves, p + 10);
          return buf;
      } }
    static const char *roots[] = {"/Documents", "/Library", "/tmp", "/var", NULL};
    for (int i = 0; roots[i]; i++) {
        size_t n = strlen(roots[i]);
        if (strncmp(p, roots[i], n) == 0 && (p[n] == 0 || p[n] == '/')) {
            snprintf(buf, sizeof(buf), "%s%s", shim_home_dir(), p);
            return buf;
        }
    }
    return p;
}

/* ---------- NSFileManager ---------- */

/*
 * The engine looks for its resources through this. The paths that arrive are
 * real and the files are in place, so the work here comes down to translating
 * from the language of Foundation into ordinary calls: stat, opendir, mkdir.
 */
static void *g_fm;

static void *fm_default(void *cls, const char *sel) {
    (void) sel;
    if (!g_fm) g_fm = shim_objc_alloc(cls);
    return g_fm;
}

void shim_safe_wait_path(const char *path);
void shim_safe_wait_dir(const char *dir);

static long fm_exists(void *self, const char *sel, void *path) {
    (void) self; (void) sel;
    struct stat st;
    const char *p = shim_map_path(shim_str_text(path));
    shim_safe_wait_path(p);                 /* a first write still queued (safefile.c): it is there */
    int ok = stat(p, &st) == 0;
    LOG("does %s exist -> %s", p, ok ? "yes" : "no");
    return ok;
}

/*
 * The "is it a directory" flag is ONE byte.
 *
 * It used to be written as a long. The game gives it a single byte of stack -
 * 0x8e1a74 does strb wzr, [sp, #0xf] and hands over sp+0xf, with its own
 * things at sp+0x10 - so eight bytes there quietly wiped what came after.
 */
static long fm_exists_isdir(void *self, const char *sel, void *path,
                            unsigned char *isdir) {
    (void) self; (void) sel;
    struct stat st;
    const char *p = shim_map_path(shim_str_text(path));
    shim_safe_wait_path(p);
    if (stat(p, &st) != 0) { if (isdir) *isdir = 0; return 0; }
    if (isdir) *isdir = S_ISDIR(st.st_mode) ? 1 : 0;
    return 1;
}

static void *fm_contents(void *self, const char *sel, void *path, void *err) {
    (void) self; (void) sel; (void) err;
    const char *p = shim_map_path(shim_str_text(path));
    shim_safe_wait_dir(p);
    DIR *d = opendir(p);
    if (!d) { LOG("no such folder: %s", p); return arr_make(0); }
    long n = 0;
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    rewinddir(d);
    nsarr_t *a = (nsarr_t *) arr_make(n);
    long i = 0;
    while ((e = readdir(d)) && a && i < n)
        if (e->d_name[0] != '.') a->items[i++] = shim_str_make(e->d_name);
    closedir(d);
    LOG("in folder %s: %ld entries", p, n);
    return a;
}

static long fm_mkdir(void *self, const char *sel, void *path, long inter, void *attrs, void *err) {
    (void) self; (void) sel; (void) inter; (void) attrs; (void) err;
    const char *p = shim_map_path(shim_str_text(path));
    errno = 0;
    int ok = mkdir(p, 0770) == 0 || errno == EEXIST;
    LOG("create folder %s -> %s (%s)", p, ok ? "yes" : "no", strerror(errno));
    return ok;
}

static long fm_remove(void *self, const char *sel, void *path, void *err) {
    (void) self; (void) sel; (void) err;
    char p[1024];
    snprintf(p, sizeof(p), "%s", shim_map_path(shim_str_text(path)));
    /* a queued write of it (or inside it) first, the removal after: never brought back by the queue */
    shim_safe_wait_path(p);
    shim_safe_wait_dir(p);
    return remove(p) == 0;
}

/* ---------- NSData ---------- */

/*
 * A block of memory with a length. Needed on exactly one path, and that path
 * matters: on death the engine zeroes the run file —
 *
 *     [[[NSData alloc] initWithBytes:empty length:0] writeToFile:path ...]
 *
 * Without these methods the message silently gave zero, the file stayed as it
 * was, and after death the run went on counting as alive.
 */
typedef struct {
    void *isa;
    unsigned char *bytes;
    long len;
} nsdata_t;

static void *data_init_bytes(void *self, const char *sel, const void *src, long n) {
    (void) sel;
    nsdata_t *d = (nsdata_t *) self;
    if (!d) return NULL;
    free(d->bytes);
    d->len = n > 0 ? n : 0;
    d->bytes = (unsigned char *) malloc((size_t) d->len + 1);
    if (!d->bytes) { d->len = 0; return d; }
    if (d->len && src) memcpy(d->bytes, src, (size_t) d->len);
    d->bytes[d->len] = 0;
    return d;
}

static const void *data_bytes(void *self, const char *sel) {
    (void) sel;
    nsdata_t *d = (nsdata_t *) self;
    return (d && d->bytes) ? (const void *) d->bytes : (const void *) "";
}

static long data_length(void *self, const char *sel) {
    (void) sel;
    nsdata_t *d = (nsdata_t *) self;
    return d ? d->len : 0;
}

/*
 * Writing to disk. The game's path is the Apple one (/private/...), so it must
 * go through shim_map_path — otherwise the file would land outside the sandbox.
 * The error is handed back empty: the game only stores and prints it, and there
 * is nothing for us to fill in.
 */
static long data_write_file(void *self, const char *sel, void *path, long opts, void **err) {
    (void) sel; (void) opts;
    if (err) *err = NULL;
    nsdata_t *d = (nsdata_t *) self;
    char p[1024];
    snprintf(p, sizeof(p), "%s", shim_map_path(shim_str_text(path)));
    size_t n = (d && d->len > 0 && d->bytes) ? (size_t) d->len : 0;
    /*
     * Through the port's one writer (shim.c, P1): whole or not at all. While an
     * import is on its way in the game's saves are let go (saves.c).
     */
    int shim_safe_game_write(const char *, const void *, size_t, const char *);
    int shim_saves_frozen(const char *);
    if (shim_saves_frozen(p)) return 1;
    /* a copy onto the writer's queue (safefile.c): after the game's earlier writes of it, off this thread */
    int ok = shim_safe_game_write(p, n ? d->bytes : "", n, "the game (NSData)") == 0;
    LOG("NSData: wrote '%s', %zu bytes: %s (%s)", p, n, ok ? "whole" : "NOT written", ok ? "-" : strerror(errno));
    return ok;
}

/* ---------- UIApplication ---------- */

static void *g_app;

static void *app_shared(void *cls, const char *sel) {
    (void) sel;
    if (!g_app) g_app = shim_objc_alloc(cls);
    return g_app;
}

/* ---------- frame clock, the turn, the services ---------- */

/*
 * Our frames are driven by a thread of our own, so CADisplayLink is needed only
 * as an object that can be created and put somewhere: the game keeps it in a
 * field and later calls invalidate. Handing back nothing is not allowed — it
 * writes through it.
 *
 * The same goes for the rest: Apple's services do not work in the port and
 * never will, but the object must exist, otherwise a write into its field kills
 * the game.
 */
static void *singleton(const char *name) {
    /*
     * Sixteen places for the nine the game asks for: CKContainer, the five
     * GameKit ones, and the rest. With eight the ninth quietly got nothing.
     */
    static void *kept[16];
    static const char *names[16];
    static int n;
    for (int i = 0; i < n; i++) if (!strcmp(names[i], name)) return kept[i];
    if (n >= (int) (sizeof(kept) / sizeof(kept[0]))) return NULL;
    names[n] = name;
    kept[n] = make(name);
    return kept[n++];
}

/* ---------- the hardware keyboard ---------- */

/*
 * The game reads a keyboard the UIKit way: its view implements
 * pressesBegan:withEvent: and for every press asks the shell to handle it -
 * [shell onKeyDown: press] - and the shell reads three things off the press:
 * its type (a cancelled one, 4, is ignored), press.key.keyCode, which it looks
 * up in a table of 256 inside the image, and press.key.modifierFlags, of which
 * it keeps four bits from the 17th.
 *
 * So a key from Android has to arrive as such a press. There is no need for
 * more than one object: the call is synchronous, so the press and its key are
 * made once and answer for whichever key is being delivered right now.
 *
 * Codes come in already as HID usages (the numbering UIKey uses); the
 * translation from Android's own numbering is in MainActivity, where the
 * Android names are.
 */
/*
 * Besides the code and the modifiers a key carries the character the layout
 * made of it and whether it is the system's auto-repeat (batch 2, K2/K4): the
 * game reads neither - it reads places, one press per press, as on iOS - but
 * the spawner's search is a text field and needs both (objc.c).
 */
typedef struct { int code, down, mods, ch, repeat; } kevent_t;

#define KEY_QUEUE 64
static kevent_t g_keyq[KEY_QUEUE];
static int g_keyn;
static pthread_mutex_t g_keylock = PTHREAD_MUTEX_INITIALIZER;

static long g_key_code, g_key_mods;

/* called from the layout thread */
/*
 * Which came last, a key or a finger. The touch screen and a hardware keyboard
 * are one device to the game (DeviceKeyboard, device 0), and the Bag of
 * Crafting's line names the keys only to someone typing (eid_craft.c).
 */
static volatile int g_keys_last;
int shim_input_keys_last(void) { return g_keys_last; }

/*
 * A REPEAT NEVER COSTS A PRESS OR A RELEASE (1.74, the research notes of
 * 11.10.2026, keyboard). The queue is emptied once a frame. While a frame stalls - a
 * floor loading, a run restarting - a held key's auto-repeat (some twenty a
 * second a key) filled the 64 places within a second or two, and every event
 * after that was thrown away: a release lost is a key held in the game until it
 * is pressed again, and the restart key held for good restarts run after run
 * (reset.c). Measured on the desk (tools/resetdesk, queue_stall): three keys
 * held through a 3 s stall, all three releases lost. Only the spawner's search
 * reads a repeat (objc.c drops them otherwise), so a key keeps one repeat in
 * the queue at a time, and a full queue gives its oldest repeat up to a press
 * or a release.
 */
void shim_key_push_ex(int code, int down, int mods, int ch, int repeat) {
    if (down && !repeat) g_keys_last = 1;
    pthread_mutex_lock(&g_keylock);
    if (repeat) {
        for (int i = 0; i < g_keyn; i++)
            if (g_keyq[i].repeat && g_keyq[i].code == code) { pthread_mutex_unlock(&g_keylock); return; }
    } else if (g_keyn == KEY_QUEUE) {
        for (int i = 0; i < g_keyn; i++)
            if (g_keyq[i].repeat) {
                memmove(&g_keyq[i], &g_keyq[i + 1], sizeof(g_keyq[0]) * (size_t) (g_keyn - i - 1));
                g_keyn--;
                break;
            }
    }
    if (g_keyn < KEY_QUEUE) {
        g_keyq[g_keyn].code = code;
        g_keyq[g_keyn].down = down;
        g_keyq[g_keyn].mods = mods;
        g_keyq[g_keyn].ch = ch;
        g_keyq[g_keyn].repeat = repeat;
        g_keyn++;
    }
    pthread_mutex_unlock(&g_keylock);
}

void shim_key_push(int code, int down, int mods) { shim_key_push_ex(code, down, mods, 0, 0); }

/* called from the frame thread: takes one event, or answers 0 */
int shim_key_next_ex(int *code, int *down, int *mods, int *ch, int *repeat) {
    int got = 0;
    pthread_mutex_lock(&g_keylock);
    if (g_keyn > 0) {
        *code = g_keyq[0].code;
        *down = g_keyq[0].down;
        *mods = g_keyq[0].mods;
        *ch = g_keyq[0].ch;
        *repeat = g_keyq[0].repeat;
        memmove(&g_keyq[0], &g_keyq[1], sizeof(g_keyq[0]) * (size_t) (g_keyn - 1));
        g_keyn--;
        got = 1;
    }
    pthread_mutex_unlock(&g_keylock);
    return got;
}

int shim_key_next(int *code, int *down, int *mods) {
    int ch, repeat;
    return shim_key_next_ex(code, down, mods, &ch, &repeat);
}

static void *singleton(const char *name);

/* the press being delivered right now, filled in just before the call */
void *shim_key_press(long code, long mods) {
    g_key_code = code;
    g_key_mods = mods;
    return singleton("UIPress");
}

static long press_type(void *self, const char *sel) {
    (void) self; (void) sel;
    return 0;                      /* anything but 4, which means cancelled */
}

static void *press_key(void *self, const char *sel) {
    (void) self; (void) sel;
    return singleton("UIKey");
}

static long key_code(void *self, const char *sel) { (void) self; (void) sel; return g_key_code; }
static long key_mods(void *self, const char *sel) { (void) self; (void) sel; return g_key_mods; }

/*
 * How many frames per second the game itself asks for. On iOS CADisplayLink
 * tells it, and more than smoothness depends on it: the menu lists compute
 * scrolling from the difference between frames, and at the wrong rate the
 * differences come out the wrong size.
 */
static int g_want_fps = 60;

int shim_want_fps(void) { return g_want_fps; }

static void link_fps(void *self, const char *sel, long fps) {
    (void) self; (void) sel;
    if (fps > 0 && fps <= 240) g_want_fps = (int) fps;
    LOG("the game asks for %ld frames per second", fps);
}

static void *link_make(void *cls, const char *sel, void *target, void *action) {
    (void) cls; (void) sel; (void) target; (void) action;
    LOG("CADisplayLink requested — our frames are our own");
    return make("CADisplayLink");
}

static void *runloop_current(void *cls, const char *sel) { (void) cls; (void) sel; return singleton("NSRunLoop"); }
static void *player_local(void *cls, const char *sel) { (void) cls; (void) sel; return singleton("GKLocalPlayer"); }
static void *ck_default(void *cls, const char *sel) { (void) cls; (void) sel; return singleton("CKContainer"); }

/*
 * Game Center: the sign-in handler has to be called, not swallowed.
 *
 * The game sets it up like this: first it raises the "do not draw frames yet"
 * flag (0xabec9a), then hands over the Game Center handler and waits. The
 * handler is the very place where the flag is lowered again. On iOS the system
 * calls it when the sign-in has finished — with success or refusal; we have no
 * Game Center at all, so the answer is known in advance and there is no point
 * in dragging it out.
 *
 * That is where the black screen came from: the shell honestly spun its frames
 * but skipped every one of them, because the answer never arrived.
 *
 * A block sits in memory as {isa, flags, reserved, body, descriptor, ...}, the
 * body at offset 0x10; it is passed itself, the sign-in window and an error.
 * There is no window and no error — the sign-in simply did not happen.
 */
static void player_set_auth(void *self, const char *sel, void *block) {
    (void) self; (void) sel;
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, void *, void *) = (void (*)(void *, void *, void *)) b[2];
    if (!invoke) return;
    LOG("Game Center: answering the handler at once — there is no sign-in");
    invoke(block, NULL, NULL);
}

/*
 * Apple's cloud is exactly the same story, and it is the one that held the
 * black screen longest. The shell starts a frame with the question "has the
 * sync finished?" and, while there is no answer, RETURNS from draw: on the very
 * first line. The answer is supposed to arrive in a handler from CloudKit; we
 * have no CloudKit, so the answer is known: the account does not exist. The
 * numbers are Apple's: 0 — cannot tell, 1 — available, 2 — no account, 3 —
 * restricted. Ours is the second.
 */
/*
 * Loading Game Center leaderboards.
 *
 * On entry the daily run menu asks for the current leaderboard and WAITS for
 * the completion handler. We have no Game Center, the method did not answer at
 * all — and the menu hung while the run never started. A measurement showed
 * exactly that: after [GameKitManager loadCurrentLeaderboard:callback:] the log
 * held NO METHOD, and everything stopped there.
 *
 * We answer at once and empty: no leaderboards, no error. From there the game
 * decides for itself — show "no connection" or go on without a leaderboard;
 * both paths are provided for.
 */
static void gk_load_leaderboards(void *self, const char *sel, void *ids, void *block) {
    (void) self; (void) sel; (void) ids;
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, void *, void *) = (void (*)(void *, void *, void *)) b[2];
    if (!invoke) return;
    /*
     * Hand back a list of ONE leaderboard, not an empty one and not nothing.
     * Taking the handler apart showed why both earlier answers were no good: on
     * nil the game goes down the failure branch, and out of a non-empty list it
     * takes the FIRST element and sends it loadEntriesForPlayerScope:...; an
     * empty list has no first, the message goes nowhere, and the game gets no
     * answer at all.
     *
     * So the leaderboard has to be real — empty, but existing, the way Game
     * Center would hand back one that simply has no entries.
     */
    LOG("RECON GK: asking for the list of leaderboards");
    void *board = singleton("GKLeaderboard");
    invoke(block, shim_arr_make_from(&board, board ? 1 : 0), NULL);
}

/*
 * Leaderboard entries. We have none and cannot have any, but we are obliged to
 * answer: the game waits for a handler, and without it the daily run menu will
 * stall again.
 *
 * The handler's arguments come in this order: itself, the player's entry, the
 * list of entries, the total number of players, an error. We hand back "there
 * are entries, and there are zero of them".
 */
/*
 * The previous occurrence of a leaderboard. For daily leaderboards Game Center
 * keeps a chain of occurrences, and the game asks for the previous one to show
 * yesterday's places. We have none and never had — so that is the answer: no
 * occurrence, no error.
 *
 * Staying silent is not allowed: without a handler the daily run menu is left
 * with grey buttons, which is what we saw.
 */
/* ---------- dates ---------- */

/*
 * A real NSDate, not a blank.
 *
 * The game asks a leaderboard for the start date of its occurrence and decides
 * from it whether the occurrence is today's. An empty object gave no answer,
 * and the menu polled us round and round — five times in a second, which is
 * visible in the log.
 *
 * Inside we keep seconds since 1970: that is enough both for a difference and
 * for a comparison, and it is exactly what the game needs.
 */
typedef struct {
    void *isa;
    double secs;
} nsdate_t;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

static void *date_from(double seconds) {
    nsdate_t *d = (nsdate_t *) shim_objc_alloc(cls_named("NSDate"));
    if (d) d->secs = seconds;
    return d;
}

static double time_of(void *o) { return o ? ((nsdate_t *) o)->secs : 0.0; }

static double date_since_1970(void *self, const char *sel) {
    (void) sel;
    return time_of(self);
}

static double date_since_date(void *self, const char *sel, void *other) {
    (void) sel;
    return time_of(self) - time_of(other);
}

static double date_since_now(void *self, const char *sel) {
    (void) sel;
    return time_of(self) - now_seconds();
}

static long date_compare(void *self, const char *sel, void *other) {
    (void) sel;
    double a = time_of(self), b = time_of(other);
    return a < b ? -1 : (a > b ? 1 : 0);
}

/*
 * The properties of a leaderboard entry and of a player.
 *
 * Taking things apart led to the place where the second stage of the deferred
 * work asks the entry that arrived for its fields. A blank has none, and on a
 * miss everything stops. We hand back a sensible minimum: there is a player,
 * the rank is first, the score is zero.
 */
static void *gk_entry_player(void *self, const char *sel) {
    (void) self; (void) sel;
    return singleton("GKPlayer");
}

static long gk_entry_rank(void *self, const char *sel) { (void) self; (void) sel; return 1; }
static long gk_entry_score(void *self, const char *sel) { (void) self; (void) sel; return 0; }
static long gk_entry_context(void *self, const char *sel) { (void) self; (void) sel; return 0; }

static void *gk_entry_date(void *self, const char *sel) {
    (void) self; (void) sel;
    return date_from(now_seconds());
}

static void *gk_entry_formatted(void *self, const char *sel) {
    (void) self; (void) sel;
    return shim_str_make("0");
}

/* The player's name: our own, local — we are not in Game Center anyway. */
static void *gk_player_name(void *self, const char *sel) {
    (void) self; (void) sel;
    return shim_str_make("Player");
}

/*
 * The leftovers of Game Center: friends, achievements, the access point.
 *
 * All of them wait for a handler, and any one of them can hold the menu. We
 * answer at once and empty in meaning, but with objects rather than nothing —
 * this game treats nothing as a refusal, we have seen it three times.
 */
static void gk_load_friends(void *self, const char *sel, void *block) {
    (void) self; (void) sel;
    LOG("RECON GK: asking for the list of friends");
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, void *, void *) = (void (*)(void *, void *, void *)) b[2];
    if (invoke) invoke(block, shim_arr_make_from(NULL, 0), NULL);
}

static void *gk_achievement_init(void *self, const char *sel, void *ident, void *player) {
    (void) sel; (void) ident; (void) player;
    LOG("RECON GK: creating an achievement");
    return self;
}

static void gk_report_achievements(void *self, const char *sel, void *list, void *block) {
    (void) self; (void) sel; (void) list;
    LOG("RECON GK: reporting achievements");
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, void *) = (void (*)(void *, void *)) b[2];
    if (invoke) invoke(block, NULL);
}

static void *gk_access_point(void *cls, const char *sel) {
    (void) cls; (void) sel;
    return singleton("GKAccessPoint");
}

/*
 * The start date of an occurrence. We hand back an object, not nothing: this
 * game reads an empty answer as a failure, we have tripped on that twice
 * already.
 */
static void *gk_start_date(void *self, const char *sel) {
    (void) self; (void) sel;
    /*
     * A daily run occurrence starts at midnight universal time — hence the
     * "time to the next one" counter on the screen. We hand back the start of
     * the current day.
     */
    double midnight = (double) ((long long) (now_seconds() / 86400.0) * 86400);
    LOG("RECON GK: asking for the occurrence start date — handing back %.0f", midnight);
    return date_from(midnight);
}

/*
 * Submitting a result. We have nowhere to submit to, but we are obliged to call
 * the handler: without it the game waits forever — that was the spinning head
 * after pressing "Daily Run".
 *
 * The arguments: score, context, player, the list of leaderboards, the handler
 * — one word each, with no structures among them. The handler is passed only an
 * error; there is none.
 */
static void gk_submit_score(void *self, const char *sel, long score, long context,
                            void *player, void *boards, void *block) {
    (void) self; (void) sel; (void) player; (void) boards;
    LOG("RECON GK: submitting a result, score %ld, context %ld", score, context);
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, void *) = (void (*)(void *, void *)) b[2];
    if (invoke) invoke(block, NULL);
}

static void gk_prev_occurrence(void *self, const char *sel, void *block) {
    (void) self; (void) sel;
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, void *, void *) = (void (*)(void *, void *, void *)) b[2];
    if (!invoke) return;
    /*
     * Hand back a LEADERBOARD, not nothing. An empty answer is read by the game
     * as a failure and it calls its own logError: — this is the second time in
     * this chain: loadLeaderboardsWithIDs: behaved the same way. It needs an
     * object, even one without entries.
     */
    LOG("RECON GK: asking for the previous occurrence");
    /*
     * Hand back a SEPARATE object, not the same leaderboard. Given the same
     * object for both the current and the previous one, the game puts it into
     * both fields and then asks the previous one for its previous — and the
     * polling goes round in a circle, which is what the log shows.
     */
    static void *previous;
    if (!previous) {
        void *cls = shim_objc_class_named("GKLeaderboard");
        if (!cls) cls = cls_named("GKLeaderboard");
        previous = cls ? shim_objc_alloc(cls) : NULL;
    }
    invoke(block, previous, NULL);
}

static void gk_load_entries(void *self, const char *sel, long scope, long time,
                            long start, long count, void *block) {
    (void) self; (void) sel; (void) scope; (void) time;
    LOG("RECON GK: asking for entries, scope %ld, time %ld, range %ld+%ld",
        scope, time, start, count);
    /*
     * The range here is an NSRange, and that is a STRUCTURE OF TWO WORDS,
     * passed in two registers. While it was described as a single pointer,
     * everything after it slid over by a register: the second half of the range
     * landed in block, the layer read a number as a pointer and died at address
     * 0x74.
     */
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, void *, void *, long, void *) =
        (void (*)(void *, void *, void *, long, void *)) b[2];
    /*
     * Hand back ONE entry, not nothing.
     *
     * Taking things apart led to the last gate: the second stage of the
     * deferred work compares the number of entries that arrived with zero and
     * on zero leaves at once, putting the screen into "Idle" — that was the
     * blinking head. An empty answer here means "no data", and the run does not
     * start.
     */
    void *entry = singleton("GKLeaderboardEntry");
    if (invoke) invoke(block, entry, shim_arr_make_from(&entry, entry ? 1 : 0), 1, NULL);
}

static void ck_account_status(void *self, const char *sel, void *block) {
    (void) self; (void) sel;
    if (!block) return;
    void **b = (void **) block;
    void (*invoke)(void *, long, void *) = (void (*)(void *, long, void *)) b[2];
    if (!invoke) return;
    LOG("cloud: there is no account — answering at once");
    invoke(block, 2, NULL);
}

/* ---------- touches ---------- */

/*
 * The engine needs little from a touch: walk the set, ask every finger for its
 * place in the view and its phase. All of that is three methods, and they are
 * here.
 *
 * The unit of the coordinates is not arbitrary: the shell keeps the view size
 * in points, and a measurement showed exactly the window size in pixels there
 * (3120 x 1440). So the place of a touch is given in pixels too, with the
 * origin at the top left — the way Android hands them out.
 *
 * The events arrive from the layout thread, and they can only be parsed where
 * the frames run: the game is not built for two threads. So there is a queue
 * here, and the start of a frame drains it.
 */
#define TOUCH_MAX 10
#define TOUCH_QUEUE 256   /* a lot of intermediate samples pile up within a frame */

typedef struct { void *isa; double x, y; long phase; } touch_t;

typedef struct { int id, phase; float x, y; } tevent_t;

static touch_t *g_touch[TOUCH_MAX];        /* one object per finger */
static int g_touch_id[TOUCH_MAX] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static void *g_touch_one[1];               /* a set of one finger */
static int g_touch_count;

static tevent_t g_queue[TOUCH_QUEUE];
static int g_qn;
static pthread_mutex_t g_qlock = PTHREAD_MUTEX_INITIALIZER;

/* phases as on Apple: 0 began, 1 moved, 2 stationary, 3 ended, 4 cancelled */
static long touch_phase(void *self, const char *sel) { (void) sel; return ((touch_t *) self)->phase; }

typedef struct { double x, y; } cgpoint_t;

static cgpoint_t touch_location(void *self, const char *sel, void *view) {
    (void) sel;
    cgpoint_t p = { ((touch_t *) self)->x, ((touch_t *) self)->y };
    static int told;
    if (told < 12) { told++; LOG("touch: place %.0f %.0f (view %p)", p.x, p.y, view); }
    return p;
}

/*
 * The fast Objective-C enumeration. The layout of the state is set by the
 * language: a counter, a pointer to items, a pointer to a mutations counter and
 * some spare room. The engine reads all three, so all three are filled in.
 */
typedef struct {
    unsigned long state;
    void **itemsPtr;
    unsigned long *mutationsPtr;
    unsigned long extra[5];
} fast_state_t;

static unsigned long set_enumerate(void *self, const char *sel, fast_state_t *st,
                                   void **buf, unsigned long len) {
    (void) self; (void) sel; (void) buf; (void) len;
    static unsigned long mutations;
    static int told;
    if (told < 12) { told++; LOG("touch: the set is being enumerated, %d fingers", g_touch_count); }
    if (st->state) return 0;
    st->state = 1;
    st->itemsPtr = g_touch_one;
    st->mutationsPtr = &mutations;
    return (unsigned long) g_touch_count;
}

/* called from the layout thread */
/*
 * A move replaces this finger's pending move; a down or an up is never dropped.
 *
 * The queue used to take everything and silently throw away whatever did not
 * fit. That is harmless for a move - the next sample says the same thing more
 * accurately - and fatal for an up: the game goes on holding a finger nobody
 * is holding, the stick stays a little off centre, and the character stands
 * and twitches until the game is started again. A stalled frame - a room being
 * loaded, a save being written - is all it takes to fill 256 places at two
 * fingers and a couple of hundred samples a second.
 *
 * Coalescing is not a saving either, it is what UIKit does: it hands the game
 * ONE touchesMoved per finger per frame and keeps the finer samples aside, and
 * the game is built for that stream. So a move for a finger that already has
 * one waiting simply overwrites it, and the queue can no longer grow with the
 * frame time.
 */
/*
 * The long press on the pause button gets first look: a press that lands there
 * is held back until it is known to be a tap or a reset (reset.c). Everything
 * it does not claim goes on unchanged.
 */
void shim_touch_push_raw(int id, int phase, float x, float y);

/*
 * PROBE, diagnostic builds only: which of the three claims a finger going
 * down, and where it went down. A long press that stops working on a button
 * nobody should be holding back is answered here in one line.
 */
static void tell_claim(const char *who, int phase, float x, float y) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    /* a palm turned down (phase 4 from Java's touchPalm, U7): who took it, first ten (devices.c) */
    if (phase == 4) { void shim_devices_palm_told(const char *, float, float); shim_devices_palm_told(who, x, y); }
    static int said;
    if (phase != 0 || said >= 24 || !shim_report_active()) return;
    said++;
    char line[120];
    snprintf(line, sizeof(line), "%s took a finger going down at %.0f,%.0f", who,
             (double) x, (double) y);
    report_line("touch", line);
}

void shim_touch_push(int id, int phase, float x, float y) {
    if (phase == 0) {
        g_keys_last = 0;
        /* the touch screen wants player 0 back (coop.c); it only takes note */
        void shim_coop_touch_began(void);
        shim_coop_touch_began();
    }
    { void shim_screen_seeds_probe(int, int, float, float); shim_screen_seeds_probe(id, phase, x, y); }
    /* the port's credits: the plaque on the main menu, and the page it opens */
    int shim_credits_touch(int, int, float, float);
    if (shim_credits_touch(id, phase, x, y)) { tell_claim("credits", phase, x, y); return; }
    int shim_tweaks_touch(int, int, float, float);
    if (shim_tweaks_touch(id, phase, x, y)) { tell_claim("tweaks", phase, x, y); return; }
    /* the item spawner's card: while it is up every finger is its own (spawner.c) */
    int shim_spawner_touch(int, int, float, float);
    if (shim_spawner_touch(id, phase, x, y)) { tell_claim("spawner", phase, x, y); return; }
    /* the controls editor first: while it is up every finger is its own (touchedit.c) */
    int shim_touchedit_touch(int, int, float, float);
    if (shim_touchedit_touch(id, phase, x, y)) { tell_claim("editor", phase, x, y); return; }
    /* the inventory first: while it is up nothing behind it may be touched */
    int shim_items_touch(int, int, float, float);
    if (shim_items_touch(id, phase, x, y)) { tell_claim("items", phase, x, y); return; }
    int shim_reset_touch(int, int, float, float);
    if (shim_reset_touch(id, phase, x, y)) { tell_claim("reset", phase, x, y); return; }
    /* the Marked button, a whole finger at a time (marked.c) */
    int shim_marked_touch(int, int, float, float);
    if (shim_marked_touch(id, phase, x, y)) { tell_claim("marked", phase, x, y); return; }
    /* the same holding back, on the card and pill button (pocket.c) */
    int shim_pocket_touch(int, int, float, float);
    if (shim_pocket_touch(id, phase, x, y)) { tell_claim("pocket", phase, x, y); return; }
    /* the D-pads: a finger of one is put on one of its eight points (sticks.c) */
    { int shim_sticks_dpad(int, int, float *, float *);
      int took = shim_sticks_dpad(id, phase, &x, &y);
      if (took == 2) { tell_claim("d-pad, refused", phase, x, y); return; }
      if (took) { tell_claim("d-pad", phase, x, y); shim_touch_push_raw(id, phase, x, y); return; } }
    /*
     * The movement stick's dead zone is no link of this chain any more (batch
     * 3): it works on the stick's output (sticks.c, shim_stick_value), and the
     * game owns every finger that reaches it, whole.
     */
    tell_claim("the game", phase, x, y);
    shim_touch_push_raw(id, phase, x, y);
}

void shim_touch_push_raw(int id, int phase, float x, float y) {
    pthread_mutex_lock(&g_qlock);
    if (phase == 1) {
        for (int i = g_qn - 1; i >= 0; i--) {
            if (g_queue[i].id != id) continue;
            if (g_queue[i].phase == 1) {          /* replace it where it stands */
                g_queue[i].x = x;
                g_queue[i].y = y;
                pthread_mutex_unlock(&g_qlock);
                return;
            }
            break;             /* a down or an up of this finger: keep the order */
        }
    }
    if (g_qn >= TOUCH_QUEUE && phase != 1) {
        /* full, and this is a fact rather than a sample: let the oldest move go */
        for (int i = 0; i < g_qn; i++)
            if (g_queue[i].phase == 1) {
                memmove(&g_queue[i], &g_queue[i + 1],
                        sizeof(g_queue[0]) * (size_t) (g_qn - i - 1));
                g_qn--;
                break;
            }
    }
    if (g_qn < TOUCH_QUEUE) {
        g_queue[g_qn].id = id;
        g_queue[g_qn].phase = phase;
        g_queue[g_qn].x = x;
        g_queue[g_qn].y = y;
        g_qn++;
    }
    pthread_mutex_unlock(&g_qlock);
}

/*
 * Every finger let go at once.
 *
 * Android does not always deliver the cancel when the window loses the touch -
 * a call, a notification shade, the recents screen - and a finger the game
 * still believes in is the same stuck stick. Leaving for the background is a
 * moment when nothing can legitimately be held, so everything is ended there.
 */
/*
 * How many fingers have been put down since the app started, and whether one is
 * down now.
 *
 * The options menu needs the PRESS, not the pressing: the byte a row flips is
 * flipped again on every frame a finger lies on it, so the only honest way to
 * tell one tap from a held finger is to ask the touch layer, which sees the
 * event itself. A counter rather than a flag, so a press cannot be missed
 * between two looks.
 */
static unsigned g_downs;
static int g_down_now;
#if ISAAC_DIAG
static void diag_summary(void);   /* the pool's report, below touch_for */
#endif

unsigned shim_touch_downs(void) { return g_downs; }
int shim_touch_any_down(void) { return g_down_now; }

void shim_touch_cancel_all(void) {
    void shim_reset_forget(void);
    void shim_pocket_forget(void);
    void shim_items_forget(void);
    shim_reset_forget();            /* a held-back press is let go of as well */
    shim_pocket_forget();
    /*
     * The map's long press was left out of this, and it is the one that could
     * not recover on its own: reset.c has a stuck-press sweeper and pocket.c
     * is re-judged from the slots every frame, but items.c only ever forgets a
     * finger when it sees that finger end. The cancels pushed below go in as
     * RAW events - straight into the game's queue, past the claim chain - so
     * items.c never saw them. One lost release (a notification shade, the app
     * going to the background, the overlay rebuilt under a held finger) left
     * its finger id set for good, and every later press on the map was refused
     * until the process restarted. Told here, along with the other two.
     */
    shim_items_forget();
#if ISAAC_DIAG
    diag_summary();
#endif
    for (int i = 0; i < TOUCH_MAX; i++) {
        if (g_touch_id[i] < 0 || !g_touch[i]) continue;
        if (g_touch[i]->phase >= 3) continue;               /* already up */
        shim_touch_push_raw(g_touch_id[i], 4, g_touch[i]->x, g_touch[i]->y);
    }
}

/*
 * One object per TOUCH, as UIKit gives it - not one per slot.
 *
 * The engine tells fingers apart by the object alone. DeviceiOS (0x8b382c)
 * looks its table up by the object's address (0x8b3a28..0x8b3a4c) and finds
 * an ENDED record just as well as a live one; a "began" that finds a record
 * only moves it (0x8b3afc..0x8b3b74) - the "has appeared" bit is not set and
 * the pending end is not cleared - and a new record is made only for a began
 * that finds none (0x8b3a5c). The record of a lifted finger lives until the
 * SECOND device update after the lift (0x8b3718: the first turns 0x2000 into
 * "ended", the second deletes it).
 *
 * The objects used to belong to the slots, and a new finger took the first
 * free slot - which is the slot of the finger just lifted. So "let go of one
 * stick and touch the other at once" handed the new finger the old finger's
 * object while its ended record was still in the table: the press went into
 * that record, was deleted with it, every later move found nothing and made
 * nothing, and the finger was dead to the game for its whole touch ("missed
 * inputs", 30.09). On iOS a new touch is always a new UITouch, and the one
 * ending in the same event is still alive, so this could not happen there.
 *
 * Now the slots only say which finger is on the glass; the objects come from
 * a pool, and a new touch gets one that no finger holds and that was let go
 * of at least TOUCH_GAME_HOLDS deliveries ago - the one let go of longest ago.
 * None like that: a new object, up to TOUCH_POOL. The pool is far larger than
 * ten fingers, so even a game that updated its devices less often than we
 * deliver would not meet a recent object.
 */
#define TOUCH_POOL 32            /* > the 16 records of DeviceiOS + 10 fingers */
#define TOUCH_GAME_HOLDS 2       /* deliveries a lifted finger's record lives on */

static touch_t *g_pool[TOUCH_POOL];
static unsigned g_pool_freed[TOUCH_POOL];  /* the delivery that let it go */
static int g_pool_n;
static unsigned g_delivery;                /* deliveries so far (shim_touch_deliver) */

static int pool_bound(const touch_t *t) {
    for (int i = 0; i < TOUCH_MAX; i++)
        if (g_touch_id[i] >= 0 && g_touch[i] == t) return 1;
    return 0;
}

static touch_t *pool_take(void) {
    int best = -1, oldest = -1;
    for (int k = 0; k < g_pool_n; k++) {
        if (pool_bound(g_pool[k])) continue;
        unsigned age = g_delivery - g_pool_freed[k];
        if (oldest < 0 || age > g_delivery - g_pool_freed[oldest]) oldest = k;
        if (age >= TOUCH_GAME_HOLDS && (best < 0 || age > g_delivery - g_pool_freed[best])) best = k;
    }
    if (best >= 0) return g_pool[best];
    if (g_pool_n < TOUCH_POOL) {
        touch_t *t = (touch_t *) make("UITouch");
        if (t) {
            g_pool_freed[g_pool_n] = g_delivery - TOUCH_GAME_HOLDS;   /* never held by anyone */
            g_pool[g_pool_n++] = t;
            return t;
        }
    }
    return oldest >= 0 ? g_pool[oldest] : NULL;   /* a full pool: the best there is */
}

/* the finger is up: its slot is free, and its object starts to age */
static void touch_release(int id, const touch_t *t) {
    for (int k = 0; k < TOUCH_MAX; k++)
        if (g_touch_id[k] == id && g_touch[k] == t) { g_touch_id[k] = -1; break; }
    for (int k = 0; k < g_pool_n; k++)
        if (g_pool[k] == t) { g_pool_freed[k] = g_delivery; break; }
}

/*
 * A slot keeps pointing at its last object after it is freed (cancel_all reads
 * it from another thread and must never meet a NULL), so a free slot's object
 * may be bound to another slot by now: everything goes by g_touch_id first.
 *
 * No free slot (ten fingers down and a tap's release still carried over):
 * that finger is not handed to the game at all. It used to get slot 0's
 * object - another finger's, live in the game's table - and the two fingers
 * became one record.
 */
static touch_t *touch_for(int id) {
    for (int i = 0; i < TOUCH_MAX; i++)
        if (g_touch[i] && g_touch_id[i] == id) return g_touch[i];
    for (int i = 0; i < TOUCH_MAX; i++)
        if (g_touch_id[i] < 0 || !g_touch[i]) {
            touch_t *t = pool_take();
            if (!t) return NULL;
            g_touch[i] = t;
            g_touch_id[i] = id;
            return t;
        }
    return NULL;
}

#if ISAAC_DIAG
/*
 * REPORT: a finger going down on an object the game still keeps a record of.
 * That finger is lost to the game for its whole touch (see the pool above).
 * Looked up in the touch screen's own table: DeviceiOS (vtable 0xa92d88) keeps
 * 0x50-byte records at +0xe8, their count at +0x5e8, the object as the first
 * word and the flags at +0x24 (devices.c reads the same table).
 */
static unsigned g_diag_downs, g_diag_held;

static int game_holds(const touch_t *t, unsigned *flags) {
    int shim_eid_readable(const void *p, unsigned long n);
    unsigned long shim_image_base(void);
    unsigned long b = shim_image_base();
    if (!b || !t) return 0;
    unsigned long input = b + 0xac5528;
    int n = ((int (*)(unsigned long)) (b + 0x8b5c24))(input);
    for (int i = 0; i < n && i < 16; i++) {
        int id = ((int (*)(unsigned long, int)) (b + 0x8b5c68))(input, i);
        if (id == -1) continue;
        unsigned long dev = ((unsigned long (*)(unsigned long, int)) (b + 0x8b5d18))(input, id);
        if (!dev || !shim_eid_readable((const void *) dev, 0x5e8 + 4)) continue;
        if (*(const unsigned long *) dev != b + 0xa92d88) continue;
        unsigned count = *(const unsigned *) (dev + 0x5e8);
        for (unsigned r = 0; r < count && r < 16; r++) {
            const unsigned char *rec = (const unsigned char *) (dev + 0xe8 + r * 0x50);
            if (*(const touch_t *const *) rec != t) continue;
            *flags = *(const unsigned *) (rec + 0x24);
            return 1;
        }
    }
    return 0;
}

static void diag_down(int id, const touch_t *t) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    if (!shim_report_active()) return;
    g_diag_downs++;
    unsigned flags = 0;
    if (!game_holds(t, &flags)) return;
    g_diag_held++;
    static int said;
    if (said >= 20) return;
    said++;
    unsigned ago = 0;
    for (int k = 0; k < g_pool_n; k++)
        if (g_pool[k] == t) { ago = g_delivery - g_pool_freed[k]; break; }
    char line[200];
    snprintf(line, sizeof(line), "finger %d went down on an object the game still holds (record flags 0x%x, "
             "released %u deliveries ago) - the finger is lost", id, flags, ago);
    report_line("touch", line);
}

/* the run's sum: at the end of every run and on leaving for the background */
static void diag_summary(void) {
    int shim_report_active(void);
    void report_line(const char *, const char *);
    static int said;
    if (!shim_report_active() || !g_diag_downs || said >= 40) return;
    said++;
    char line[120];
    snprintf(line, sizeof(line), "%u downs, %u on an object still held", g_diag_downs, g_diag_held);
    report_line("touch", line);
    g_diag_downs = g_diag_held = 0;
}

static void diag_run_watch(void) {
    int shim_sticks_in_game(void);
    static int was_run;
    int run = shim_sticks_in_game();
    if (was_run && !run) diag_summary();
    was_run = run;
}
#endif

/*
 * The object the game knows a finger by, or NULL - never made here (touch_for
 * makes one). The stick keeps the one it took at +0x170, so this is how the
 * double tap asks the game whose finger it is (sticks.c).
 */
void *shim_touch_object(int id) {
    for (int i = 0; i < TOUCH_MAX; i++)
        if (g_touch[i] && g_touch_id[i] == id) return g_touch[i];
    return NULL;
}

/* called from the frame thread, before drawing */
void shim_touch_deliver(void *vc) {
    tevent_t batch[TOUCH_QUEUE];
    int n;
    pthread_mutex_lock(&g_qlock);
    n = g_qn;
    memcpy(batch, g_queue, sizeof(tevent_t) * (size_t) n);
    g_qn = 0;
    g_delivery++;            /* the pool's clock: one delivery, one device update of the game */
    /* the deepest the queue has ever been: it is the overflow that loses an up */
    DIAG({ static int deepest; if (n > deepest) { deepest = n;
             LOG("touch: queue high-water %d of %d", n, TOUCH_QUEUE); } });
    pthread_mutex_unlock(&g_qlock);
    if (!vc) return;

    static const char *by_phase[] = {
        "touchesBegan:withEvent:", "touchesMoved:withEvent:", NULL,
        "touchesEnded:withEvent:", "touchesCancelled:withEvent:"
    };
    /*
     * A finger that has only just appeared does not move in the same frame.
     *
     * On iOS a touch arrives one event per turn of the run loop, so a began and
     * a move of the same finger practically never land in one frame. Here they
     * always do: Android samples the screen two to four times faster than the
     * game draws, and the queue hands the whole lot over at once. The game then
     * sees the move before it gets as far as seating the stick, the "has just
     * appeared" bit is gone by then, and the seating is skipped - so a dynamic
     * stick stayed wherever it had last managed to sit, which is exactly the
     * "it goes to some precomputed zone" testers reported.
     *
     * Measured: 117 grabs, and every one of them either landed exactly under
     * the finger (the touch divided by five, to the second decimal) or repeated
     * the previous seat to the digit. Never anything in between.
     *
     * So a move is held back for one frame when its own finger began in the
     * same batch. Android sends the next move a few milliseconds later anyway.
     *
     * An up is held back too - carried over whole to the next frame, not
     * dropped - and that one was found the hard way, as "Spin to Win stops
     * working after a card". The widget pass at 0x8621f4 lets go of a button
     * only when the finger's flags at +0x24 say (flags & 5) == 4: ended, and
     * NOT just appeared. A finger that appears and ends inside one frame reads
     * 5, so the button is never let go of; after the frame the finger leaves
     * the table, the pass never visits that button again, and its "pressed"
     * byte at +0x18c stays set for good. The walk at 0x861d1c then publishes
     * that dead button every frame - the first pressed one it finds - and the
     * item button's hold behind it is never heard. A new card or a new floor
     * rebuilds the list, which is why those "fixed" it. Every card and pill
     * tap goes through pocket.c, which hands the press and the release over
     * together, so it happened every single time.
     *
     * Delivered a frame apart, a tap is still a tap: a press in one frame and
     * a release in the next is exactly what a finger on iOS produces.
     */
    int began[TOUCH_QUEUE];
    int began_n = 0;
    tevent_t later[TOUCH_QUEUE];                /* ups carried over to the next frame */
    int later_n = 0;

    for (int i = 0; i < n; i++) {
        if (batch[i].phase < 0 || batch[i].phase > 4 || !by_phase[batch[i].phase]) continue;
        /*
         * Behind a carried-over up, everything of the same id waits as well:
         * Android hands a lifted finger's id to the next finger down, so what
         * follows is a NEW touch, and it must come after its predecessor's
         * release. Delivered now, its down found the id still bound and took
         * the lifted finger's object - two touches as one record, the second
         * one lost (found by the desk harness, a tap twice in one frame).
         */
        { int behind = 0;
          for (int k = 0; k < later_n; k++)
              if (later[k].id == batch[i].id) { behind = 1; break; }
          if (behind) {
              if (later_n < TOUCH_QUEUE) later[later_n++] = batch[i];
              continue;
          } }
        if (batch[i].phase == 1 || batch[i].phase >= 3) {
            int fresh = 0;
            for (int k = 0; k < began_n; k++)
                if (began[k] == batch[i].id) { fresh = 1; break; }
            if (fresh) {
                if (batch[i].phase >= 3 && later_n < TOUCH_QUEUE) later[later_n++] = batch[i];
                continue;
            }
        }
        if (batch[i].phase == 0 && began_n < TOUCH_QUEUE) began[began_n++] = batch[i].id;
        if (batch[i].phase == 0) g_downs++;
        touch_t *t = touch_for(batch[i].id);
        if (!t) continue;
#if ISAAC_DIAG
        if (batch[i].phase == 0) diag_down(batch[i].id, t);
#endif
        /*
         * The double tap's watch (sticks.c): here, on the game's thread, with
         * the finger's object bound and before the game sees the event - so
         * whose finger it is comes from the stick itself (+0x170), and the
         * detector is written ahead of the flick it is for. A cancel (phase 4)
         * comes this way too, and is no tap.
         */
        { void shim_sticks_tap_watch(int, int, float, float);
          shim_sticks_tap_watch(batch[i].id, (int) batch[i].phase, batch[i].x, batch[i].y); }
        /* a floating D-pad's finger is put on its point here, on the game thread (sticks.c) */
        { void shim_sticks_dpad_float(int, int, float *, float *);
          shim_sticks_dpad_float(batch[i].id, batch[i].phase, &batch[i].x, &batch[i].y); }
        t->x = batch[i].x;                     /* a point and a pixel are the same thing for the game */
        t->y = batch[i].y;
        t->phase = batch[i].phase;
        g_touch_one[0] = t;
        g_touch_count = 1;
        void *set = singleton("NSSet");
        static int told;
        if (told < 12) {
            told++;
            LOG("touch: sending %s, finger %d, %.0f %.0f",
                by_phase[batch[i].phase], batch[i].id, (double) batch[i].x, (double) batch[i].y);
        }
        shim_objc_msgSend(vc, by_phase[batch[i].phase], set, NULL);
        g_touch_count = 0;
        /* the finger is released: its slot is free at once, its object only after the game drops it */
        if (batch[i].phase >= 3) touch_release(batch[i].id, t);
    }

    /*
     * The ups carried over go back to the head of the queue, ahead of whatever
     * has arrived since: a finger that came down again with the same id must
     * still be seen after its own release.
     */
    if (later_n) {
        pthread_mutex_lock(&g_qlock);
        int keep = g_qn;
        if (keep > TOUCH_QUEUE - later_n) keep = TOUCH_QUEUE - later_n;
        memmove(&g_queue[later_n], g_queue, sizeof(tevent_t) * (size_t) keep);
        memcpy(g_queue, later, sizeof(tevent_t) * (size_t) later_n);
        g_qn = keep + later_n;
        pthread_mutex_unlock(&g_qlock);
    }
    /* and whether anything is still lying on the screen */
    g_down_now = 0;
    for (int k = 0; k < TOUCH_MAX; k++)
        if (g_touch_id[k] >= 0 && g_touch[k] && g_touch[k]->phase < 3) { g_down_now = 1; break; }
#if ISAAC_DIAG
    diag_run_watch();
#endif
}

/* ---------- NSDate and settings ---------- */

/*
 * The game needs the time for stamps and the settings for small things like
 * volume. There is no real storage yet: to any question we answer with nothing,
 * and the game takes its own defaults.
 */
static double date_now(void *self, const char *sel) {
    (void) self; (void) sel;
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (double) t.tv_sec + t.tv_nsec / 1e9;
}

/* [NSDate date] — the real moment, with a time inside, not the shared blank */
static void *date_make(void *cls, const char *sel) {
    (void) cls; (void) sel;
    return date_from(now_seconds());
}
static void *defaults_std(void *cls, const char *sel) { (void) cls; (void) sel; return singleton("NSUserDefaults"); }

/* ---------- screen sizes ---------- */

/*
 * The real sizes come from Java: the system knows them there. The game takes
 * them through UIScreen and computes the size of the drawing buffer from them —
 * at zero its own check aborts the launch.
 *
 * A rectangle in iOS is four floating-point numbers. On arm64 such a quadruple
 * is returned whole in registers d0-d3, so it can be returned by value and no
 * calling-convention swap is needed.
 */
typedef struct { double x, y, w, h; } cgrect_t;

static double g_scr_w = 1280, g_scr_h = 720, g_scr_scale = 2.0;

/*
 * The screen scale is kept at one, and that is not a simplification.
 *
 * The game says itself what it expects: it calls
 * [shell resize:X viewSizePoints:Y], putting the same drawableSize into BOTH
 * arguments. That is, a point and a pixel are one and the same to it. Touch
 * handling divides by their ratio, and the moment the real Android density is
 * put in, the touch area moves that many times outside the screen: taps still
 * land (there is a separate branch for those), but dragging lists is counted as
 * outside the field and dies.
 */
static double g_scr_density;            /* Android's dp scale, kept for physical sizes only */

void shim_set_screen(int w, int h, double scale) {
    if (w > 0 && h > 0) { g_scr_w = w; g_scr_h = h; }
    g_scr_scale = 1.0;
    if (scale > 0.3 && scale < 12.0) g_scr_density = scale;
    LOG("screen: %.0f x %.0f, a point equals a pixel (density %.2f)", g_scr_w, g_scr_h, g_scr_density);
}

/*
 * Pixels in one dp (DisplayMetrics.density), 0 if Java has not said. NOT the
 * game's scale, which stays one (above): only the port's own sizes that must
 * be the same under a thumb on any screen ask for it (sticks.c, shoot_middle).
 */
float shim_screen_density(void) { return (float) g_scr_density; }

static double screen_scale(void) { return g_scr_scale; }

static double scr_scale(void *self, const char *sel) { (void) self; (void) sel; return g_scr_scale; }

static cgrect_t scr_bounds(void *self, const char *sel) {
    (void) self; (void) sel;
    cgrect_t r = {0, 0, g_scr_w / g_scr_scale, g_scr_h / g_scr_scale};
    return r;
}

/*
 * The view size is taken from the surface itself, not from the Java metrics:
 * the engine divides the size in pixels by the size in points and gets a
 * multiplier. If the numbers come from different places, the multiplier lies.
 */
static cgrect_t view_bounds(void *self, const char *sel) {
    (void) self; (void) sel;
    double w = shim_gl_width() > 0 ? (double) shim_gl_width() : g_scr_w;
    double h = shim_gl_height() > 0 ? (double) shim_gl_height() : g_scr_h;
    cgrect_t r = {0, 0, w / g_scr_scale, h / g_scr_scale};
    return r;
}

/* ---------- EAGLContext ---------- */

/*
 * For now this is only a shell: a real EGL context will appear once we have a
 * surface from Android. What matters here is not handing back nothing —
 * otherwise the game decides there are no graphics and goes no further.
 */
static void *eagl_init_api(void *self, const char *sel, long api) {
    (void) sel;
    LOG("EAGLContext: OpenGL ES %ld requested", api);
    return self;
}

static long eagl_set_current(void *cls, const char *sel, void *ctx) {
    (void) cls; (void) sel;
    if (!ctx) return 1;                       /* clearing the context is of no use to us */
    if (!shim_gl_start()) { LOG("EGL did not come up"); return 0; }
    return shim_gl_make_current();
}

/* the game binds its colour buffer to the layer — for us, to the window */
static long eagl_storage(void *self, const char *sel, long target, void *drawable) {
    (void) self; (void) sel; (void) target; (void) drawable;
    return shim_gl_bind_drawable();
}

static long eagl_present(void *self, const char *sel, long target) {
    (void) self; (void) sel; (void) target;
    return shim_gl_present();
}

/* ---------- the table ---------- */

typedef struct { const char *cls; const char *sel; void *imp; } builtin_t;

static const builtin_t g_table[] = {
    /* NSObject: suits any receiver */
    {NULL, "init", (void *) ns_init},
    {NULL, "alloc", (void *) ns_alloc},
    {NULL, "new", (void *) ns_new},
    {NULL, "self", (void *) ns_self},
    {NULL, "retain", (void *) ns_self},
    {NULL, "autorelease", (void *) ns_self},
    {NULL, "release", (void *) ns_void},
    {NULL, "dealloc", (void *) ns_void},
    {NULL, "class", (void *) shim_objc_opt_class},
    {NULL, "respondsToSelector:", (void *) ns_yes},

    {"NSDictionary", "dictionaryWithObjects:forKeys:count:", (void *) dict_with},

    /* by class name rather than empty: NSString already has a length of its own */
    /*
     * The gamepad. The game reads it through GameController, so exactly that
     * surface is substituted — otherwise neither the Controls submenu nor the
     * button assignment would have worked. The answers themselves live in
     * pad.c.
     */
    {"GCController", "controllers", (void *) shim_pad_controllers},
    {"GCController", "extendedGamepad", (void *) shim_pad_gamepad},
    {"GCController", "playerIndex", (void *) shim_pad_player_index},
    {"GCController", "setPlayerIndex:", (void *) shim_pad_set_player_index},
    {"GCController", "vendorName", (void *) shim_pad_name},

    {"GCExtendedGamepad", "buttonA", (void *) shim_pad_buttonA},
    {"GCExtendedGamepad", "buttonB", (void *) shim_pad_buttonB},
    {"GCExtendedGamepad", "buttonX", (void *) shim_pad_buttonX},
    {"GCExtendedGamepad", "buttonY", (void *) shim_pad_buttonY},
    {"GCExtendedGamepad", "leftShoulder", (void *) shim_pad_L1},
    {"GCExtendedGamepad", "rightShoulder", (void *) shim_pad_R1},
    {"GCExtendedGamepad", "leftTrigger", (void *) shim_pad_L2},
    {"GCExtendedGamepad", "rightTrigger", (void *) shim_pad_R2},
    {"GCExtendedGamepad", "buttonMenu", (void *) shim_pad_menu},
    {"GCExtendedGamepad", "buttonOptions", (void *) shim_pad_options},
    {"GCExtendedGamepad", "dpad", (void *) shim_pad_dpad},
    {"GCExtendedGamepad", "leftThumbstick", (void *) shim_pad_left_stick},
    {"GCExtendedGamepad", "rightThumbstick", (void *) shim_pad_right_stick},

    {"GCControllerButtonInput", "isPressed", (void *) shim_pad_is_pressed},
    {"GCControllerButtonInput", "value", (void *) shim_pad_force},
    {"GCControllerDirectionPad", "xAxis", (void *) shim_pad_x},
    {"GCControllerDirectionPad", "yAxis", (void *) shim_pad_y},
    /*
     * The four direction buttons. The game reads the d-pad with exactly these,
     * not with axes — without these lines it stayed silent even though the
     * values arrived intact.
     */
    {"GCControllerDirectionPad", "up", (void *) shim_pad_up},
    {"GCControllerDirectionPad", "down", (void *) shim_pad_down},
    {"GCControllerDirectionPad", "left", (void *) shim_pad_left},
    {"GCControllerDirectionPad", "right", (void *) shim_pad_right},
    {"GCControllerAxisInput", "value", (void *) shim_pad_axis_value},

    {"NSData", "initWithBytes:length:", (void *) data_init_bytes},
    {"NSData", "writeToFile:options:error:", (void *) data_write_file},
    {"NSData", "bytes", (void *) data_bytes},
    {"NSData", "length", (void *) data_length},

    {"NSString", "stringWithCString:encoding:", (void *) str_with_cstring},
    {"NSString", "stringWithUTF8String:", (void *) str_with_utf8},
    {"NSString", "stringWithFormat:", (void *) shim_str_with_format_abi},
    /*
     * A date knows how to compare itself, and its entry has to stand before
     * the classless one: the search goes top down and an entry with no class
     * suits any receiver, so NSDate's own was never reached.
     */
    {"NSDate", "compare:", (void *) date_compare},
    {NULL, "compare:", (void *) str_compare},
    {NULL, "getCString:maxLength:encoding:", (void *) str_get_cstring},
    {NULL, "UTF8String", (void *) str_utf8},
    {NULL, "cStringUsingEncoding:", (void *) str_cstring_enc},
    {NULL, "length", (void *) str_length},
    {NULL, "isEqualToString:", (void *) str_equal},
    {NULL, "stringByAppendingString:", (void *) str_append},
    {NULL, "stringByAppendingPathComponent:", (void *) str_append_path},
    {"NSMutableString", "initWithCapacity:", (void *) mstr_init_capacity},
    {"NSMutableString", "initWithBytes:length:encoding:", (void *) mstr_init_bytes},
    {"NSString", "initWithBytes:length:encoding:", (void *) mstr_init_bytes},
    {NULL, "appendString:", (void *) mstr_append},
    {NULL, "deleteCharactersInRange:", (void *) mstr_delete_range},

    {NULL, "count", (void *) arr_count},
    {NULL, "objectAtIndex:", (void *) arr_at},
    {NULL, "objectAtIndexedSubscript:", (void *) arr_at},
    {NULL, "indexOfObject:", (void *) arr_index_of},
    {NULL, "containsObject:", (void *) arr_contains},
    {NULL, "firstObject", (void *) arr_first},
    {NULL, "lastObject", (void *) arr_last},

    {"NSFileManager", "defaultManager", (void *) fm_default},
    {NULL, "fileExistsAtPath:", (void *) fm_exists},
    {NULL, "fileExistsAtPath:isDirectory:", (void *) fm_exists_isdir},
    {NULL, "contentsOfDirectoryAtPath:error:", (void *) fm_contents},
    {NULL, "createDirectoryAtPath:withIntermediateDirectories:attributes:error:", (void *) fm_mkdir},
    {NULL, "removeItemAtPath:error:", (void *) fm_remove},

    {"UIApplication", "sharedApplication", (void *) app_shared},
    {"UIApplication", "supportsAlternateIcons", (void *) ns_no},

    {"CADisplayLink", "displayLinkWithTarget:selector:", (void *) link_make},
    {NULL, "addToRunLoop:forMode:", (void *) ns_void},
    {NULL, "setPreferredFramesPerSecond:", (void *) link_fps},
    {NULL, "invalidate", (void *) ns_void},
    {"NSRunLoop", "currentRunLoop", (void *) runloop_current},
    {"NSRunLoop", "mainRunLoop", (void *) runloop_current},

    /* Apple services: the object exists, the use does not — that is the intent */
    {"GKLocalPlayer", "localPlayer", (void *) player_local},
    /* the new name of the same method: the game calls exactly this one, the old one is kept for compatibility */
    {"GKLocalPlayer", "local", (void *) player_local},
    /*
     * The one place where the layer tells the game an untruth.
     *
     * On "not signed in" the daily run screen shows a window that does NOT
     * close: it is meant for a missing network and holds the player until a
     * connection appears. We have no Game Center and never will, so an honest
     * answer makes the screen impassable forever.
     *
     * We answer "signed in" only to the local player and only here; the entry
     * stands ABOVE the common one, otherwise it would not fire — the lookup
     * takes the first match, and the common one matches any class. To everyone
     * else "is it signed in" still answers "no".
     */
    {"GKLocalPlayer", "isAuthenticated", (void *) ns_yes},
    {NULL, "isAuthenticated", (void *) ns_no},
    {NULL, "setAuthenticateHandler:", (void *) player_set_auth},
    {NULL, "authenticateHandler", (void *) ns_no},
    {"CKContainer", "defaultContainer", (void *) ck_default},
    {"GKLeaderboardEntry", "player", (void *) gk_entry_player},
    {"GKLeaderboardEntry", "rank", (void *) gk_entry_rank},
    {"GKLeaderboardEntry", "score", (void *) gk_entry_score},
    {"GKLeaderboardEntry", "context", (void *) gk_entry_context},
    {"GKLeaderboardEntry", "date", (void *) gk_entry_date},
    {"GKLeaderboardEntry", "formattedScore", (void *) gk_entry_formatted},
    /*
     * The same properties for the local player too: our stubs are synthetic,
     * there is no kinship between GKLocalPlayer and GKPlayer among them, and
     * entries for one class do not reach the other.
     */
    {"GKLocalPlayer", "displayName", (void *) gk_player_name},
    {"GKLocalPlayer", "alias", (void *) gk_player_name},
    {"GKLocalPlayer", "gamePlayerID", (void *) gk_player_name},
    {"GKLocalPlayer", "teamPlayerID", (void *) gk_player_name},
    {"GKPlayer", "displayName", (void *) gk_player_name},
    {"GKPlayer", "alias", (void *) gk_player_name},
    {"GKPlayer", "gamePlayerID", (void *) gk_player_name},
    {"GKPlayer", "teamPlayerID", (void *) gk_player_name},
    {"GKLocalPlayer", "loadFriends:", (void *) gk_load_friends},
    {"GKAchievement", "initWithIdentifier:player:", (void *) gk_achievement_init},
    {"GKAchievement", "reportAchievements:withCompletionHandler:",
                                        (void *) gk_report_achievements},
    {"GKAccessPoint", "shared", (void *) gk_access_point},
    {"GKLeaderboard", "startDate", (void *) gk_start_date},
    {"NSDate", "timeIntervalSince1970", (void *) date_since_1970},
    {"NSDate", "timeIntervalSinceDate:", (void *) date_since_date},
    {"NSDate", "timeIntervalSinceNow", (void *) date_since_now},
    {"GKLeaderboard", "submitScore:context:player:leaderboardIDs:completionHandler:",
                                        (void *) gk_submit_score},
    {"GKLeaderboard", "loadPreviousOccurrenceWithCompletionHandler:",
                                        (void *) gk_prev_occurrence},
    {"GKLeaderboard", "loadEntriesForPlayerScope:timeScope:range:completionHandler:",
                                        (void *) gk_load_entries},
    {"GKLeaderboard", "loadLeaderboardsWithIDs:completionHandler:",
                                        (void *) gk_load_leaderboards},
    {NULL, "accountStatusWithCompletionHandler:", (void *) ck_account_status},

    /* touches: the set is enumerated, a finger is asked for its place and phase */
    {"NSSet", "countByEnumeratingWithState:objects:count:", (void *) set_enumerate},
    {"UITouch", "locationInView:", (void *) touch_location},
    {"UITouch", "phase", (void *) touch_phase},
    /* a key press, the shape the shell reads it in */
    {"UIPress", "type", (void *) press_type},
    {"UIPress", "key", (void *) press_key},
    {"UIKey", "keyCode", (void *) key_code},
    {"UIKey", "modifierFlags", (void *) key_mods},

    {"NSDate", "date", (void *) date_make},
    {NULL, "timeIntervalSince1970", (void *) date_now},
    {"NSUserDefaults", "standardUserDefaults", (void *) defaults_std},
    {NULL, "objectForKey:", (void *) ns_no},
    {NULL, "stringForKey:", (void *) ns_no},
    {NULL, "boolForKey:", (void *) ns_no},
    {NULL, "integerForKey:", (void *) ns_no},
    {NULL, "setObject:forKey:", (void *) ns_void},
    {NULL, "setBool:forKey:", (void *) ns_void},
    {NULL, "setInteger:forKey:", (void *) ns_void},
    {NULL, "setFloat:forKey:", (void *) ns_void},
    {NULL, "setDouble:forKey:", (void *) ns_void},
    {NULL, "floatForKey:", (void *) ns_no},
    {NULL, "doubleForKey:", (void *) ns_no},
    {NULL, "synchronize", (void *) ns_yes},

    /*
     * The game's view controller is its own — AAPLOpenGLViewController, not
     * UIViewController, and by class name it never got here. That is why `view`
     * handed back nothing, the view size in points came out zero, and the
     * engine divided by it, getting a zero multiplier: touches arrived at point
     * 0,0.
     */
    {NULL, "view", (void *) vc_view},
    {"UIView", "becomeFirstResponder", (void *) view_become_first},
    {"UIView", "resignFirstResponder", (void *) view_resign_first},
    {"UIView", "isFirstResponder", (void *) view_is_first},
    {"UIView", "setKeyboardType:", (void *) view_trait},
    {"UIView", "setKeyboardAppearance:", (void *) view_trait},
    {"UIView", "setAutocorrectionType:", (void *) view_trait},
    {"UIView", "setAutocapitalizationType:", (void *) view_trait},
    {"UIView", "setSpellCheckingType:", (void *) view_trait},
    {"UIView", "setReturnKeyType:", (void *) view_trait},
    {"UIView", "setEnablesReturnKeyAutomatically:", (void *) view_trait},
    {"UIView", "layer", (void *) view_layer},
    {"UIScreen", "mainScreen", (void *) screen_main},
    {"UIScreen", "nativeScale", (void *) scr_scale},
    {"UIScreen", "scale", (void *) scr_scale},
    {"UIScreen", "bounds", (void *) scr_bounds},
    {"UIScreen", "nativeBounds", (void *) scr_bounds},
    {"UIView", "bounds", (void *) view_bounds},
    {"UIView", "frame", (void *) view_bounds},
    {"UIView", "contentScaleFactor", (void *) scr_scale},
    {"CAEAGLLayer", "bounds", (void *) view_bounds},
    {"CAEAGLLayer", "frame", (void *) view_bounds},
    {"CAEAGLLayer", "contentsScale", (void *) scr_scale},

    {"EAGLContext", "initWithAPI:", (void *) eagl_init_api},
    {"EAGLContext", "setCurrentContext:", (void *) eagl_set_current},
    {"EAGLContext", "renderbufferStorage:fromDrawable:", (void *) eagl_storage},
    {"EAGLContext", "presentRenderbuffer:", (void *) eagl_present},

    /* the store and achievements: the port has neither and never will */
    {"SKPaymentQueue", "defaultQueue", (void *) ns_no},

    {NULL, NULL, NULL}
};

void shim_uikit_install(void) { shim_objc_set_builtins(g_table); }
