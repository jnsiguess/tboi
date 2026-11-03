/*
 * The C++ streams the game needs, in the layout the game was built for.
 *
 * Why this file exists. The image carries no C++ runtime of its own: it imports
 * thirty-nine names and its export table defines none of them. They are spelled
 * the Apple way, in the inline namespace `std::__1`. The libc++ that travels
 * inside our apk spells everything `std::__ndk1`, so its names can never match
 * and it never answers. Every one of those names therefore fell through to the
 * only other library on the phone that uses `std::__1` — the platform's own
 * /system/lib64/libc++.so, which dladdr confirmed: twenty names from there,
 * including the stringstream method table.
 *
 * That library is a different version on every release of Android, and the
 * game's inline code computes its field offsets from the copy it was compiled
 * against. On Android 16 the two agreed. On Android 12 they did not:
 * ios_base::init wrote the stream state at the offset ITS libc++ uses, the
 * game read it at +0x20 where its own libc++ keeps it, and what it found was
 * whatever the stack held. getline builds a sentry, the sentry asks good(),
 * the answer was no, and getline returned having extracted nothing. The split
 * that follows produced an empty vector and its pop_back read one byte below
 * null — the crash two testers reported at offset 0x7f2308, on the first .anm2
 * the game opens.
 *
 * The layout below is not guessed. Every offset is read out of the game's own
 * constructor at 0x97f6c and its getline at 0x9808c:
 *
 *   stringstream  +0x00 vptr   +0x10 vptr   +0x18 stringbuf   +0x80 basic_ios
 *   basic_ios     +0x20 rdstate  +0x24 exceptions  +0x28 rdbuf  +0x30 locale
 *                 +0x88 tie      +0x90 fill
 *   streambuf     +0x10 get begin  +0x18 get next  +0x20 get end
 *                 +0x28 put begin  +0x30 put next  +0x38 put end
 *
 * The constructor writes tie at +0x88 and fill at +0x90 itself, and getline
 * reads rdbuf at +0x28 and walks the get area at +0x18 and +0x20. Those four
 * agreements are what pin the rest.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#include <android/log.h>
#else
#define LOG(...) do { } while (0)      /* the build we play on has no log */
#endif

/* ---------- what the standard calls these bits ---------- */

/*
 * libc++ numbers these its own way, and the game was compiled against those
 * numbers: badbit is one, eofbit two, failbit four. Taking the more familiar
 * order instead — the one libstdc++ uses — cost an afternoon: end of input was
 * then written as a failure the game does not test for, its loop never saw a
 * reason to stop, and the split span forever on the first path it was given.
 * The mask the game checks is badbit|failbit, which is five.
 */
#define GOODBIT 0
#define BADBIT  1
#define EOFBIT  2
#define FAILBIT 4

#define SKIPWS  0x1000
#define DEC     0x0002

#define MODE_IN  0x08
#define MODE_OUT 0x10

/* ---------- the objects, laid out as the game expects them ---------- */

typedef struct {
    void *vtable;
    unsigned fmtflags;
    unsigned pad;
    long precision;
    long width;
    unsigned rdstate;          /* +0x20 — the byte getline's sentry asks about */
    unsigned exceptions;
    void *rdbuf;               /* +0x28 */
    void *loc;                 /* +0x30 */
    void *fn;
    void *index;
    unsigned long event_size, event_cap;
    void *iarray;
    unsigned long iarray_size, iarray_cap;
    void *parray;
    unsigned long parray_size, parray_cap;   /* ends at +0x88 */
    void *tie;                 /* +0x88 */
    int fill;                  /* +0x90 */
} ios_t;

typedef struct {
    void *vtable;
    void *loc;
    char *ginp, *gnext, *gend;
    char *pbeg, *pnext, *pend;
    char str[24];
    char *hm;
    int mode;
} sbuf_t;

/* the pieces of the string that cxxstr.c already owns, in Apple's layout */
const char *shim_str_bytes(const void *s);
size_t shim_str_length(const void *s);
void *shim_str_append_n(void *self, const char *s, size_t n);
void shim_str_push_back(void *self, char c);
void shim_str_resize(void *self, size_t n, char c);
void shim_str_dtor(void *self);
void *shim_str_assign_str(void *self, const void *other);
void shim_strbuf_str(void *self, const void *src);
void shim_strbuf_str_get(const void *self, void *ret);
int shim_strbuf_overflow(void *self, int c);

/* ---------- ios_base ---------- */

/*
 * The one function the whole crash turned on.
 *
 * A stream is born good when it is given a buffer and bad when it is not, and
 * everything after that — the sentry, getline, the split — reads the answer
 * from the field at +0x20. Writing it anywhere else leaves the game reading
 * stack litter.
 */
void shim_ios_init(void *self, void *sb) {
    ios_t *s = (ios_t *) self;
    s->rdbuf = sb;
    s->rdstate = sb ? GOODBIT : BADBIT;
    s->exceptions = GOODBIT;
    s->fmtflags = SKIPWS | DEC;
    s->width = 0;
    s->precision = 6;
    s->fn = NULL;
    s->index = NULL;
    s->event_size = s->event_cap = 0;
    s->iarray = NULL;
    s->iarray_size = s->iarray_cap = 0;
    s->parray = NULL;
    s->parray_size = s->parray_cap = 0;
    /*
     * The locale stays empty on purpose. A real one would be a reference to the
     * global locale, and the only thing the game ever does with it here is
     * destroy it — which is our locale::~locale below, and it accepts this.
     */
    s->loc = NULL;
}

/*
 * Setting the state. The standard says the stored state becomes exactly what is
 * asked for, plus badbit when there is no buffer, and that an exception is
 * thrown if that state is one the stream was told to throw on. The game never
 * asks for exceptions, so raising one would be inventing behaviour it has no
 * handler for; instead the state stands and the caller reads it, which is what
 * every path in the game actually does.
 */
void shim_ios_clear(void *self, unsigned state) {
    ios_t *s = (ios_t *) self;
    s->rdstate = s->rdbuf ? state : (state | BADBIT);
}

void shim_ios_badbit(void *self) {
    ios_t *s = (ios_t *) self;
    s->rdstate |= BADBIT;
}

/* the virtual base sits at a fixed distance, fixed by the game's own code */
#define VBASE 0x80

static ios_t *ios_of(void *stream) {
    void **vp = (void **) stream;
    long vbase = *(long *) ((char *) *vp - 0x18);
    return (ios_t *) ((char *) stream + vbase);
}

/* ---------- basic_istream::sentry ---------- */

/*
 * Built before every extraction and asked once: may we read?
 *
 * A stream that is not good answers no and takes failbit for saying so. With
 * noskipws — which is how getline always builds it — nothing else happens. Ours
 * skips whitespace by the plain C rules when asked to, because the game only
 * ever runs the classic locale.
 */
void shim_istream_sentry(void *self, void *is, int noskipws) {
    char *ok = (char *) self;
    ios_t *s = ios_of(is);
    *ok = 0;
    if (s->rdstate != GOODBIT) {
        shim_ios_clear(s, s->rdstate | FAILBIT);
        return;
    }
    if (!noskipws && (s->fmtflags & SKIPWS)) {
        sbuf_t *b = (sbuf_t *) s->rdbuf;
        while (b && b->gnext < b->gend) {
            unsigned char c = (unsigned char) *b->gnext;
            if (c != ' ' && c != '\t' && c != '\n' && c != '\v' && c != '\f' && c != '\r') break;
            b->gnext++;
        }
        if (b && b->gnext >= b->gend) {
            shim_ios_clear(s, s->rdstate | FAILBIT | EOFBIT);
            return;
        }
    }
    *ok = 1;
}

/* ---------- basic_ostream ---------- */

void shim_ostream_sentry_ctor(void *self, void *os) {
    char *ok = (char *) self;
    ios_t *s = ios_of(os);
    *ok = (s->rdstate == GOODBIT) ? 1 : 0;
}

void shim_ostream_sentry_dtor(void *self) { (void) self; }

static void put_char(void *os, char c) {
    ios_t *s = ios_of(os);
    if (s->rdstate != GOODBIT) return;
    if (shim_strbuf_overflow(s->rdbuf, (unsigned char) c) < 0)
        shim_ios_clear(s, s->rdstate | BADBIT);
}

void *shim_ostream_put(void *os, char c) {
    put_char(os, c);
    return os;
}

/* nothing is buffered anywhere but in the string itself, so there is nothing to flush */
void *shim_ostream_flush(void *os) { return os; }

void *shim_ostream_put_long(void *os, long v) {
    char tmp[24];
    int n = snprintf(tmp, sizeof(tmp), "%ld", v);
    for (int i = 0; i < n; i++) put_char(os, tmp[i]);
    return os;
}

/* ---------- destruction ---------- */

/*
 * Everything these have to release lives in the string buffer, and that is
 * released by the buffer's own destructor. The stream parts hold a locale we
 * never made and a tie we never set.
 */
void shim_locale_dtor(void *self) { (void) self; }
void shim_basic_ios_dtor(void *self) { (void) self; }
void shim_iostream_dtor(void *self) { (void) self; }

void shim_streambuf_ctor(void *self) {
    sbuf_t *b = (sbuf_t *) self;
    b->loc = NULL;
    b->ginp = b->gnext = b->gend = NULL;
    b->pbeg = b->pnext = b->pend = NULL;
}

void shim_streambuf_dtor(void *self) { (void) self; }

/*
 * ctype<char>::id — a name, not a function.
 *
 * Every locale facet carries one of these so the locale can tell them apart;
 * the game only ever takes its address. A single zeroed word is a facet that
 * has never been registered, which is exactly true of ours.
 */
static unsigned long g_ctype_id;
void *shim_ctype_id(void) { return &g_ctype_id; }

/* ---------- the locale, and the one facet the game asks it for ---------- */

/*
 * Why a stream needs a locale at all here.
 *
 * The constructor leaves the fill character as -1, meaning "not chosen yet".
 * The first time anything is written, basic_ios::fill() sees that and works the
 * character out the long way: it takes the stream's locale, asks it for the
 * ctype facet, and calls widen(' ') on it. That is three calls into the C++
 * runtime for a space, and every one of them used to land in the phone's libc++
 * — which then walked a locale we had left empty.
 *
 * So the locale is a real object here. It never has to be more than a token,
 * because the only thing anyone does with it is hand it back to use_facet, but
 * it does have to be answerable.
 */
static char g_locale_token;

void shim_ios_getloc(void *ret, const void *ios) {
    (void) ios;
    *(void **) ret = &g_locale_token;
}

/*
 * A ctype facet for the classic locale.
 *
 * The slot the game reaches for is the eighth function in the table, which is
 * where libc++ puts do_widen(char): the facet inherits a destructor pair and
 * __on_zero_shared from locale::facet before its own methods begin. In the C
 * locale widening is the identity, so the answers are short — but every slot is
 * filled, because a table with holes is a crash waiting for the first caller
 * who takes a different path.
 */
static void ctype_dtor(void *self) { (void) self; }
static void ctype_on_zero(void *self) { (void) self; }
static char ctype_toupper(void *self, char c) {
    (void) self;
    return (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
}
static const char *ctype_toupper_range(void *self, char *lo, const char *hi) {
    (void) self;
    for (; lo < hi; lo++) *lo = ctype_toupper(NULL, *lo);
    return hi;
}
static char ctype_tolower(void *self, char c) {
    (void) self;
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}
static const char *ctype_tolower_range(void *self, char *lo, const char *hi) {
    (void) self;
    for (; lo < hi; lo++) *lo = ctype_tolower(NULL, *lo);
    return hi;
}
static char ctype_widen(void *self, char c) { (void) self; return c; }
static const char *ctype_widen_range(void *self, const char *lo, const char *hi, char *to) {
    (void) self;
    while (lo < hi) *to++ = *lo++;
    return hi;
}
static char ctype_narrow(void *self, char c, char dflt) { (void) self; (void) dflt; return c; }
static const char *ctype_narrow_range(void *self, const char *lo, const char *hi,
                                      char dflt, char *to) {
    (void) self; (void) dflt;
    while (lo < hi) *to++ = *lo++;
    return hi;
}

static void *g_ctype_table[16];
static struct { void *vptr; long owners; const void *tab; unsigned char del; } g_ctype;

void *shim_use_facet(const void *loc, const void *id) {
    (void) loc; (void) id;
    if (!g_ctype.vptr) {
        g_ctype_table[0] = (void *) 0;                     /* offset to top */
        g_ctype_table[1] = (void *) 0;                     /* type info */
        g_ctype_table[2] = (void *) ctype_dtor;
        g_ctype_table[3] = (void *) ctype_dtor;
        g_ctype_table[4] = (void *) ctype_on_zero;
        g_ctype_table[5] = (void *) ctype_toupper;
        g_ctype_table[6] = (void *) ctype_toupper_range;
        g_ctype_table[7] = (void *) ctype_tolower;
        g_ctype_table[8] = (void *) ctype_tolower_range;
        g_ctype_table[9] = (void *) ctype_widen;           /* the one fill() wants */
        g_ctype_table[10] = (void *) ctype_widen_range;
        g_ctype_table[11] = (void *) ctype_narrow;
        g_ctype_table[12] = (void *) ctype_narrow_range;
        g_ctype.owners = 1;                                /* never let it be freed */
        g_ctype.tab = NULL;
        g_ctype.del = 0;
        g_ctype.vptr = &g_ctype_table[2];
    }
    return &g_ctype;
}

/* ---------- the string buffer's own method table ---------- */

/*
 * Written out rather than copied from the phone.
 *
 * The copy is where this whole failure came from: taking the platform's table
 * and patching a few slots leaves the rest pointing into a libc++ that lays its
 * objects out differently. The order below is the one the standard fixes for a
 * stream buffer, and the game confirms it — getline calls the slot at +0x50,
 * which is uflow, exactly where it falls here.
 */
static int sb_underflow(void *self) {
    sbuf_t *b = (sbuf_t *) self;
    if (!(b->mode & MODE_IN)) return -1;
    if (b->hm && b->gend < b->hm) b->gend = b->hm;      /* writing grew the readable part */
    if (b->gnext < b->gend) return (unsigned char) *b->gnext;
    return -1;
}

static int sb_uflow(void *self) {
    int c = sb_underflow(self);
    if (c >= 0) ((sbuf_t *) self)->gnext++;
    return c;
}

static int sb_pbackfail(void *self, int c) {
    sbuf_t *b = (sbuf_t *) self;
    if (b->gnext <= b->ginp) return -1;
    if (c != -1 && (char) c != b->gnext[-1]) {
        if (!(b->mode & MODE_OUT)) return -1;
        b->gnext[-1] = (char) c;
    }
    b->gnext--;
    return c == -1 ? 0 : c;
}

static long sb_xsgetn(void *self, char *dst, long n) {
    sbuf_t *b = (sbuf_t *) self;
    long got = 0;
    while (got < n) {
        if (b->gnext >= b->gend && sb_underflow(self) < 0) break;
        long have = b->gend - b->gnext;
        long take = (n - got < have) ? n - got : have;
        memcpy(dst + got, b->gnext, (size_t) take);
        b->gnext += take;
        got += take;
    }
    return got;
}

static long sb_xsputn(void *self, const char *src, long n) {
    long put = 0;
    while (put < n) {
        if (shim_strbuf_overflow(self, (unsigned char) src[put]) < 0) break;
        put++;
    }
    return put;
}

static long sb_showmanyc(void *self) {
    sbuf_t *b = (sbuf_t *) self;
    return (b->gend > b->gnext) ? (b->gend - b->gnext) : 0;
}

static int sb_sync(void *self) { (void) self; return 0; }
static void *sb_setbuf(void *self, char *p, long n) { (void) p; (void) n; return self; }
static void sb_imbue(void *self, const void *loc) { (void) self; (void) loc; }

/*
 * Seeking. The game does not seek a split apart, but a stream buffer that
 * answers "impossible" to every seek is still a correct one, and saying so is
 * better than leaving a slot pointing at somebody else's code.
 */
static long sb_seekoff(void *self, long off, int way, int which) {
    (void) self; (void) off; (void) way; (void) which;
    return -1;
}

static long sb_seekpos(void *self, long pos, int which) {
    (void) self; (void) pos; (void) which;
    return -1;
}

static void sb_dtor(void *self) { shim_str_dtor(((sbuf_t *) self)->str); }

void *shim_android_symbol(const char *name);

/* the same allocator the rest of the layer uses; cxxstr.c takes it the same way */
static void cxx_free(void *p) {
    static void (*p_delete)(void *);
    if (!p_delete) p_delete = (void (*)(void *)) shim_android_symbol("_ZdlPv");
    if (p_delete) p_delete(p);
}

static void sb_dtor_delete(void *self) {
    sb_dtor(self);
    cxx_free(self);
}

/*
 * Two words of housekeeping, then the methods. The game installs the table
 * PLUS sixteen as the object's pointer, so the first entry it ever calls is
 * the one at index two.
 */
static void *g_sb_table[16];

void *shim_stringbuf_table(void) {
    if (g_sb_table[2]) return g_sb_table;
    g_sb_table[0] = (void *) 0;                    /* offset to top */
    g_sb_table[1] = (void *) 0;                    /* type info: none of ours is thrown */
    g_sb_table[2] = (void *) sb_dtor;
    g_sb_table[3] = (void *) sb_dtor_delete;
    g_sb_table[4] = (void *) sb_imbue;
    g_sb_table[5] = (void *) sb_setbuf;
    g_sb_table[6] = (void *) sb_seekoff;
    g_sb_table[7] = (void *) sb_seekpos;
    g_sb_table[8] = (void *) sb_sync;
    g_sb_table[9] = (void *) sb_showmanyc;
    g_sb_table[10] = (void *) sb_xsgetn;
    g_sb_table[11] = (void *) sb_underflow;
    g_sb_table[12] = (void *) sb_uflow;
    g_sb_table[13] = (void *) sb_pbackfail;
    g_sb_table[14] = (void *) sb_xsputn;
    g_sb_table[15] = (void *) shim_strbuf_overflow;
    return g_sb_table;
}

/* ---------- the stringstream's tables ---------- */

/*
 * A class with a virtual base needs two things: a method table built as three
 * pieces, and a VTT telling the constructor which piece to install at each step.
 * Both are compiler-generated, and both have to be built by hand here because
 * the game imports them.
 *
 * The shape is read straight out of the constructor at 0x97f6c. It installs
 * table+0x18 as the object's own pointer, table+0x40 at +0x10, table+0x68 at
 * the virtual base, and it finds the virtual base by reading the word eighteen
 * hex below the pointer it just installed. So:
 *
 *   +0x00  offset to the virtual base, read as [vptr-0x18]  -> 0x80
 *   +0x08  offset to top
 *   +0x10  type info
 *   +0x18  the stringstream's own two destructors            <- vptr at +0x00
 *   +0x28  offset to the virtual base for the second piece
 *   +0x30  offset to top (-0x10)
 *   +0x38  type info
 *   +0x40  the second piece                                  <- vptr at +0x10
 *   +0x58  offset to top (-0x80)
 *   +0x60  type info
 *   +0x68  the virtual base's own destructors                <- vptr at +0x80
 */
static void ss_dtor(void *self) { (void) self; }
static void ss_dtor_delete(void *self) { cxx_free(self); }

static void *g_ss_table[16];
static void *g_ss_vtt[16];

void *shim_stringstream_table(void) {
    if (g_ss_table[3]) return g_ss_table;
    g_ss_table[0] = (void *) (uintptr_t) VBASE;    /* [vptr-0x18] */
    g_ss_table[1] = (void *) 0;                    /* offset to top */
    g_ss_table[2] = (void *) 0;                    /* type info */
    g_ss_table[3] = (void *) ss_dtor;              /* vptr at +0x00 lands here */
    g_ss_table[4] = (void *) ss_dtor_delete;
    g_ss_table[5] = (void *) (uintptr_t) (VBASE - 0x10);
    g_ss_table[6] = (void *) (uintptr_t) -0x10L;   /* offset to top */
    g_ss_table[7] = (void *) 0;                    /* type info */
    g_ss_table[8] = (void *) ss_dtor;              /* vptr at +0x10 lands here */
    g_ss_table[9] = (void *) ss_dtor_delete;
    g_ss_table[10] = (void *) 0;
    g_ss_table[11] = (void *) (uintptr_t) -(long) VBASE;  /* offset to top */
    g_ss_table[12] = (void *) 0;                   /* type info */
    g_ss_table[13] = (void *) shim_basic_ios_dtor; /* vptr at +0x80 lands here */
    g_ss_table[14] = (void *) ss_dtor_delete;
    g_ss_table[15] = (void *) 0;
    return g_ss_table;
}

/*
 * The VTT: which method table to install at each step of building and taking
 * apart an object that has a virtual base.
 *
 * The entries are not invented. The constructor at 0x97f6c reads one through
 * six, and the destructor at 0x7f20ec reads nought, eight and nine — the last
 * two at +0x40 and +0x48, which is why an array of eight words was not enough
 * and reading past its end put rubbish where a method table belongs.
 *
 * Which piece each entry names is fixed by where the game stores it. An entry
 * that lands at the object's own start must be a piece whose word eighteen hex
 * below it is 0x80, the distance to the virtual base; one that lands at +0x10
 * must say 0x70 from there; one that lands at the virtual base itself is the
 * base's own piece. That is the whole rule, and it decides every slot.
 */
void *shim_stringstream_vtt(void) {
    if (g_ss_vtt[1]) return g_ss_vtt;
    char *t = (char *) shim_stringstream_table();
    void *primary = t + 0x18;      /* installed at +0x00, says 0x80 to the base */
    void *second  = t + 0x40;      /* installed at +0x10, says 0x70 to the base */
    void *base    = t + 0x68;      /* the virtual base's own piece              */
    g_ss_vtt[0] = primary;         /* taking apart: the object's own            */
    g_ss_vtt[1] = primary;
    g_ss_vtt[2] = primary;
    g_ss_vtt[3] = base;
    g_ss_vtt[4] = second;
    g_ss_vtt[5] = base;
    g_ss_vtt[6] = base;
    g_ss_vtt[7] = base;
    g_ss_vtt[8] = base;            /* taking apart: the virtual base            */
    g_ss_vtt[9] = second;          /* taking apart: the second piece            */
    for (int i = 10; i < 16; i++) g_ss_vtt[i] = base;
    return g_ss_vtt;
}

/* ---------- the default random engine ---------- */

/*
 * The game asks the standard library for a shared random engine. Ours is the
 * same small generator libc++ uses when it has nothing better, seeded once. It
 * matters that it exists and answers, not that it agrees with any other
 * implementation: nothing compares its numbers against a second source.
 */
static unsigned long g_rs = 1u;

void *shim_rs_get(void) { return &g_rs; }
void shim_rs_dtor(void *self) { (void) self; }

unsigned shim_rs_call(void *self) {
    unsigned long *s = self ? (unsigned long *) self : &g_rs;
    *s = *s * 6364136223846793005UL + 1442695040888963407UL;
    return (unsigned) (*s >> 33);
}
