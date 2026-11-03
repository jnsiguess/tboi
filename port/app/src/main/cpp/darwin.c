/*
 * The pieces of libc that only Apple has.
 *
 * Their absence does not crash the game — which is the worst kind of breakage.
 * A stub quietly answers with garbage and you only see it at the far end:
 * shots drop instead of flying, enemies pile into one corner, music turns into
 * a roar.
 *
 * All of that has one root: __sincos_stret. Apple computes sine and cosine in
 * a single call and returns the pair in d0/d1; Android has no such function at
 * all. Without it EVERY rotation in the game is wrong — the direction of a
 * shot, an enemy's move, where an item lands on screen. The Vorbis decoder
 * built into the game uses the same call to build its transform window, which
 * is where the "roar" instead of music came from.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ---------- sine and cosine at once ---------- */

/*
 * A pair of floating-point numbers comes back whole in d0/d1 (s0/s1 for
 * float), so it can be returned by value: the convention here is exactly
 * Apple's and no assembly wrapper is needed.
 */
typedef struct { double s, c; } sincos_d_t;
typedef struct { float s, c; } sincos_f_t;

sincos_d_t shim_sincos_stret(double x) {
    sincos_d_t r;
    r.s = sin(x);
    r.c = cos(x);
    return r;
}

sincos_f_t shim_sincosf_stret(float x) {
    sincos_f_t r;
    r.s = sinf(x);
    r.c = cosf(x);
    return r;
}

double shim_exp10(double x) { return pow(10.0, x); }

/*
 * Filling memory with a repeating sixteen-byte pattern. Ordinary business on
 * Apple for arrays of floating-point numbers; Android knows nothing of it.
 */
void shim_memset_pattern16(void *dst, const void *pattern, size_t len) {
    if (!dst || !pattern) return;
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *p = (const uint8_t *) pattern;
    size_t i = 0;
    while (i + 16 <= len) { memcpy(d + i, p, 16); i += 16; }
    if (i < len) memcpy(d + i, p, len - i);
}

/* ---------- character classification ---------- */

/*
 * On Apple the isalpha-style checks are inlined in the header and read the
 * _DefaultRuneLocale table directly, calling __maskrune only past its end. We
 * build the table ourselves out of Android's answers, so the values are
 * guaranteed to agree with what the game gets from every other call.
 *
 * The layout is dictated by Apple's header and repeated here field by field:
 * eight bytes of signature, thirty-two of encoding name, two pointers, the
 * invalid rune, and then three tables of 256 entries.
 */
#define RUNE_CACHED 256

#define CT_A 0x00000100u   /* letter       */
#define CT_C 0x00000200u   /* control      */
#define CT_D 0x00000400u   /* digit        */
#define CT_G 0x00000800u   /* graphic      */
#define CT_L 0x00001000u   /* lower case   */
#define CT_P 0x00002000u   /* punctuation  */
#define CT_S 0x00004000u   /* whitespace   */
#define CT_U 0x00008000u   /* upper case   */
#define CT_X 0x00010000u   /* hex digit    */
#define CT_B 0x00020000u   /* blank        */
#define CT_R 0x00040000u   /* printable    */

typedef struct {
    char magic[8];
    char encoding[32];
    void *sgetrune;
    void *sputrune;
    int32_t invalid_rune;
    uint32_t runetype[RUNE_CACHED];
    int32_t maplower[RUNE_CACHED];
    int32_t mapupper[RUNE_CACHED];
} rune_locale_t;

static rune_locale_t shim_DefaultRuneLocale;

/* only this file knows the layout, so the outside world gets an address */
void *shim_rune_table(void) { return &shim_DefaultRuneLocale; }

static uint32_t rune_flags(int c) {
    uint32_t f = 0;
    if (isalpha(c)) f |= CT_A;
    if (iscntrl(c)) f |= CT_C;
    if (isdigit(c)) f |= CT_D | CT_X;
    if (isgraph(c)) f |= CT_G;
    if (islower(c)) f |= CT_L;
    if (ispunct(c)) f |= CT_P;
    if (isspace(c)) f |= CT_S;
    if (isupper(c)) f |= CT_U;
    if (isxdigit(c)) f |= CT_X;
    if (c == ' ' || c == '\t') f |= CT_B;
    if (isprint(c)) f |= CT_R;
    /* Apple keeps the character's display width in the low byte */
    if (isprint(c)) f |= 1u;
    return f;
}

void shim_darwin_init(void) {
    static int done;
    if (done) return;
    done = 1;
    memcpy(shim_DefaultRuneLocale.magic, "RuneMagA", 8);
    strcpy(shim_DefaultRuneLocale.encoding, "NONE");
    shim_DefaultRuneLocale.invalid_rune = -1;
    for (int c = 0; c < RUNE_CACHED; c++) {
        shim_DefaultRuneLocale.runetype[c] = rune_flags(c);
        shim_DefaultRuneLocale.maplower[c] = tolower(c);
        shim_DefaultRuneLocale.mapupper[c] = toupper(c);
    }
}

int shim_maskrune(int c, unsigned long f) {
    if (c < 0 || c >= RUNE_CACHED) return 0;
    return (int) (shim_DefaultRuneLocale.runetype[c] & (uint32_t) f);
}

int shim_tolower_rune(int c) { return (c >= 0 && c < RUNE_CACHED) ? tolower(c) : c; }
int shim_toupper_rune(int c) { return (c >= 0 && c < RUNE_CACHED) ? toupper(c) : c; }

/* ---------- the error stream ---------- */

/*
 * On Apple stderr is reached through the __stderrp pointer. The game writes
 * its complaints there; without a real stream the very first such write is a
 * crash.
 */
FILE *shim_stderrp = NULL;

void shim_darwin_streams(void) { shim_stderrp = stderr; }
