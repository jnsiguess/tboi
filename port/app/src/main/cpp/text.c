/*
 * Text: teaching the engine to read UTF-8.
 *
 * The font keeps one glyph table of 65536 entries (font+0x50, a code to a
 * glyph number) and the engine reaches it by two roads:
 *
 *   0x8bdec8 / 0x8be0c4   read the string BYTE by byte
 *   0x8bdf8c / 0x8be4b8   read it sixteen bits at a time
 *
 * The wide pair reaches the whole table; the byte pair only ever sees
 * 0x00..0xff. Everything the game keeps in `stringtable.sta` is UTF-8, so a
 * string drawn along the byte road has each of its bytes looked up as if it
 * were a character: "ä" is C3 A4 and comes out as two blanks, Russian "Vyberite" ("Choose") as
 * sixteen. That is the popup that asks for a controller, and it is the game's
 * own defect - the developers' Russian translation of the "controller
 * disconnected" window cannot be shown on any platform, iOS included.
 *
 * The two roads are the same function twice over. Their prologues match word
 * for word - x0 the font, x1 the string, x2 x3 x4 and s0..s3 carried through
 * untouched - and both end in the same glyph blitter. So the byte road is
 * simply sent down the wide one: decode the UTF-8, hand over sixteen-bit
 * characters, and every narrow string in the game becomes readable, not just
 * the two windows that prompted this.
 *
 * A byte that is not valid UTF-8 is taken as Latin-1 and passed through. Some
 * strings in the game really are single-byte, and they must keep working; this
 * way nothing that used to draw stops drawing.
 *
 * The detour is four words at the head of each function - load an address,
 * jump to it - and the body behind it is never entered again, so nothing has
 * to be copied or relocated. Both heads were checked with tools/into.py: only
 * the function's first instruction is ever a branch target.
 */
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

unsigned long shim_image_base(void);

#define NARROW_WIDTH 0x8bdec8
#define NARROW_DRAW  0x8be0c4
#define WIDE_WIDTH   0x8bdf8c
#define WIDE_DRAW    0x8be4b8

/* the five arguments and four floats both roads carry, in the order they sit in */
typedef int  (*wide_width_fn)(void *font, const uint16_t *text);
typedef void (*wide_draw_fn)(void *font, const uint16_t *text, void *a2,
                             unsigned long a3, void *a4,
                             float f0, float f1, float f2, float f3);

/*
 * The line being drawn, decoded. One buffer is enough: drawing happens on the
 * frame thread, one string at a time, and a line of a menu is short. Anything
 * longer than this is cut rather than allowed to run off the end.
 */
#define TEXT_MAX 1024

static int decode(const char *s, uint16_t *out) {
    int n = 0;
    const unsigned char *p = (const unsigned char *) s;
    while (*p && n < TEXT_MAX - 1) {
        unsigned c = *p;
        int extra = 0;
        unsigned code = c;
        if (c >= 0xf0) { extra = 3; code = c & 0x07; }
        else if (c >= 0xe0) { extra = 2; code = c & 0x0f; }
        else if (c >= 0xc0) { extra = 1; code = c & 0x1f; }
        /*
         * A sequence is taken only when it is whole and every tail byte is a
         * tail byte. Otherwise the lead byte is passed through as Latin-1 -
         * which is what the byte road did with it before us.
         */
        int ok = extra > 0;
        for (int i = 1; ok && i <= extra; i++)
            if ((p[i] & 0xc0) != 0x80) ok = 0;
        if (!ok) { out[n++] = (uint16_t) c; p++; continue; }
        for (int i = 1; i <= extra; i++) code = (code << 6) | (p[i] & 0x3f);
        p += extra + 1;
        /* the table holds the basic plane; anything above it has no glyph anyway */
        out[n++] = (uint16_t) (code > 0xffff ? 0xfffd : code);
    }
    out[n] = 0;
    return n;
}

static int width_detour(void *font, const char *text) {
    static uint16_t wide[TEXT_MAX];
    if (!text) return 0;
    decode(text, wide);
    return ((wide_width_fn) (shim_image_base() + WIDE_WIDTH))(font, wide);
}

static void draw_detour(void *font, const char *text, void *a2,
                        unsigned long a3, void *a4,
                        float f0, float f1, float f2, float f3) {
    static uint16_t wide[TEXT_MAX];
    if (!text) return;
    decode(text, wide);
    ((wide_draw_fn) (shim_image_base() + WIDE_DRAW))(font, wide, a2, a3, a4, f0, f1, f2, f3);
}

/*
 * Four words at the head: `ldr x16, #8` / `br x16` / the address. The words
 * that are there now are checked first, all four of them - a head that is not
 * what we disassembled means the image is not the one this was written for,
 * and then it is better to leave the text broken than to break the game.
 */
static int detour(unsigned long off, const uint32_t *expect, void *fn) {
    unsigned long base = shim_image_base();
    if (!base) return 0;
    uint32_t *p = (uint32_t *) (base + off);
    uint32_t want[4];
    want[0] = 0x58000050u;                 /* ldr x16, #8 */
    want[1] = 0xD61F0200u;                 /* br  x16     */
    memcpy(&want[2], &fn, 8);
    if (!memcmp(p, want, sizeof want)) return 1;      /* already ours */
    for (int i = 0; i < 4; i++)
        if (p[i] != expect[i]) {
            LOG("text: %lx holds %08x where %08x was expected — the detour is cancelled",
                off + (unsigned long) i * 4, p[i], expect[i]);
            return 0;
        }
    long ps = sysconf(_SC_PAGESIZE);
    if (ps <= 0) ps = 4096;
    uint8_t *page = (uint8_t *) ((uintptr_t) p & ~(uintptr_t) (ps - 1));
    size_t len = (size_t) ((uint8_t *) (p + 4) - page);
    if (mprotect(page, len, PROT_READ | PROT_WRITE | PROT_EXEC) != 0 &&
        mprotect(page, len, PROT_READ | PROT_WRITE) != 0) {
        LOG("text: the page under %lx did not open for writing", off);
        return 0;
    }
    memcpy(p, want, sizeof want);
    __builtin___clear_cache((char *) p, (char *) (p + 4));
    mprotect(page, len, PROT_READ | PROT_EXEC);
    return 1;
}

void shim_text_utf8(void) {
    static const uint32_t width_head[4] = {0xA9BB67FAu, 0xA9015FF8u, 0xA90257F6u, 0xA9034FF4u};
    static const uint32_t draw_head[4]  = {0xD10383FFu, 0x6D0633EDu, 0x6D072BEBu, 0x6D0823E9u};
    int a = detour(NARROW_WIDTH, width_head, (void *) width_detour);
    int b = detour(NARROW_DRAW, draw_head, (void *) draw_detour);
    LOG("text: UTF-8 on the byte road — width %s, drawing %s",
        a ? "yes" : "NO", b ? "yes" : "NO");
}
