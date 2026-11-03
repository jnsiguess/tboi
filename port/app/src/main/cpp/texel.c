/*
 * WHERE THE QUADS LAND ON THE PIXEL GRID. Report builds only (14.10.2026).
 *
 * The owner: in big rooms, as the camera moves, the textures of rocks, floor,
 * doors and walls "walk" - the quads get half-pixel coordinates and the texel
 * alignment is lost. This measures exactly that, on the device, for the draws
 * the game really makes.
 *
 * For a short stretch of every window, each sprite draw is read back from the
 * game's own client memory (the vertices never live in GL buffers, shim.c):
 * every corner is taken through the program's Transform and the viewport into
 * pixels of the target, and its distance from a whole pixel is sorted into
 * "on a whole pixel", "on a half" (a pixel centre: the rasteriser and the
 * point sampler break the tie by rounding noise, and a texel column comes out
 * N - 1 or N + 1 wide) and "between". For a square quad the first texel edge
 * inside it is placed the same way. Draws are grouped by the target they go
 * into: the enlarged scene, a canvas-sized surface, or anything else.
 *
 * What the game itself does (ImageBase::Render 0x8b44a0..0x8b4528): a square
 * quad of a point-sampled image is rounded to the pixel grid of the CURRENT
 * target before it is written - round(v * s) / s, s = target pixels to the
 * unit (0x8b54a8). A rotated quad (its left edge not vertical) and an image
 * filtered LINEAR both ways (image+0x2c, +0x30) are left as they are. So the
 * "half" and "between" counts name exactly the draws that escape that rounding,
 * and the pixels-to-the-unit of each target says on which grid the rest sit.
 *
 * One line per target kind per window: kind, size, pixels to the unit, the
 * shaders, the counts, the worst corner, and the first quad off the grid with
 * its pixels and texels. The cost is report-only: a few GL queries per draw
 * for one second in every ten.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <GLES3/gl3.h>
#include "diag.h"

#if ISAAC_DIAG

int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_screen_scene_scale(int *cw, int *ch);
unsigned shim_gpu_program(void);
const char *shim_gpu_program_label(void);
int shim_gl_attr_get(unsigned idx, const void **ptr, int *size, int *stride, unsigned *type);
void *shim_android_symbol(const char *name);

#define WINDOW_S   10.0          /* a line set every ten seconds */
#define LOOK_S      1.0          /* measured during the first second of it */
#define QUADS_MAX  16            /* quads read per draw */
#define LINES_MAX  90            /* lines in the whole report */
#define PROGS      4096
#define KINDS      3             /* 0 the enlarged scene, 1 a canvas-sized surface, 2 other */
#define NAMES      6

typedef struct {
    int draws, quads, square;
    int c_whole, c_half, c_between;     /* corners */
    int e_whole, e_half, e_between;     /* the first texel edge inside a square quad */
    float worst;
    int w, h;
    float per_unit;
    const char *name[NAMES];
    int name_n[NAMES];
    int have_example;
    char example[200];
} kind_t;

static kind_t g_k[KINDS];
static double g_window = -1.0;
static int g_lines;

static signed char g_ok[PROGS];         /* 0 not asked, 1 usable, -1 not a sprite program */
static GLint g_transform[PROGS];
static signed char g_pos[PROGS], g_tc[PROGS];

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec + (double) t.tv_nsec * 1e-9;
}

static int program_ready(unsigned p) {
    if (p == 0 || p >= PROGS) return 0;
    if (g_ok[p]) return g_ok[p] > 0;
    GLint tr = glGetUniformLocation(p, "Transform");
    GLint pos = glGetAttribLocation(p, "Position");
    GLint tc = glGetAttribLocation(p, "TexCoord");
    g_ok[p] = (tr >= 0 && pos >= 0 && pos < 8) ? 1 : -1;
    g_transform[p] = tr;
    g_pos[p] = (signed char) pos;
    g_tc[p] = (signed char) ((tc >= 0 && tc < 8) ? tc : -1);
    return g_ok[p] > 0;
}

/* 0 on a whole pixel, 1 on a half (a pixel centre), 2 between; d is the distance from whole */
static int grid_class(float px, float *d) {
    float f = px - floorf(px);
    float w = f < 0.5f ? f : 1.0f - f;
    if (d) *d = w;
    if (w < 0.01f) return 0;
    if (fabsf(w - 0.5f) < 0.01f) return 1;
    return 2;
}

static void name_count(kind_t *k, const char *n) {
    for (int i = 0; i < NAMES; i++) {
        if (k->name[i] == n) { k->name_n[i]++; return; }
        if (!k->name[i]) { k->name[i] = n; k->name_n[i] = 1; return; }
    }
}

static void flush(void) {
    static const char *const KIND[KINDS] = { "scene", "canvas", "other" };
    int shot = shim_screen_scene_scale(0, 0);
    for (int i = 0; i < KINDS; i++) {
        kind_t *k = &g_k[i];
        if (!k->draws || g_lines >= LINES_MAX) continue;
        g_lines++;
        char names[160];
        int n = 0;
        names[0] = 0;
        for (int j = 0; j < NAMES && k->name[j]; j++)
            n += snprintf(names + n, sizeof(names) - (size_t) n, "%s%s %d", j ? ", " : "",
                          k->name[j], k->name_n[j]);
        char what[32];
        if (i == 0) snprintf(what, sizeof(what), "%s x%d", KIND[i], shot);
        else snprintf(what, sizeof(what), "%s", KIND[i]);
        char line[720];
        snprintf(line, sizeof(line),
                 "into %s (%d x %d, %.3f px/unit): %d draws [%s], %d quads, %d square; "
                 "corners on whole px %d, on half %d, between %d, worst %.3f px; "
                 "texel edges on whole px %d, on half %d, between %d%s%s",
                 what, k->w, k->h, (double) k->per_unit, k->draws, names, k->quads, k->square,
                 k->c_whole, k->c_half, k->c_between, (double) k->worst,
                 k->e_whole, k->e_half, k->e_between,
                 k->have_example ? "; first off the grid: " : "", k->have_example ? k->example : "");
        report_line("texel", line);
    }
    memset(g_k, 0, sizeof(g_k));
}

void shim_texel_probe(unsigned mode, int count, unsigned type, const void *idx) {
    if (!shim_report_active() || mode != GL_TRIANGLES || count < 6 || !idx) return;
    double t = now_s();
    if (g_window < 0.0 || t - g_window >= WINDOW_S) {
        if (g_window >= 0.0) flush();
        g_window = t;
    }
    if (t - g_window > LOOK_S || g_lines >= LINES_MAX) return;
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT && type != GL_UNSIGNED_BYTE) return;

    unsigned p = shim_gpu_program();
    if (!program_ready(p)) return;
    const void *pp = 0, *tp = 0;
    int psz = 0, pst = 0, tsz = 0, tst = 0;
    unsigned pty = 0, tty = 0;
    if (!shim_gl_attr_get((unsigned) g_pos[p], &pp, &psz, &pst, &pty) || pty != GL_FLOAT || psz < 2) return;
    int have_tc = g_tc[p] >= 0 && shim_gl_attr_get((unsigned) g_tc[p], &tp, &tsz, &tst, &tty)
                  && tty == GL_FLOAT && tsz >= 2;

    GLfloat m[16];
    glGetUniformfv(p, g_transform[p], m);
    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return;

    /* the texture on unit 0, for texels */
    GLint tw = 0, th = 0, unit = 0;
    if (have_tc) {
        static void (*level)(GLenum, GLint, GLenum, GLint *);
        static int looked;
        if (!looked) {
            looked = 1;
            level = (void (*)(GLenum, GLint, GLenum, GLint *)) shim_android_symbol("glGetTexLevelParameteriv");
        }
        glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
        if (level && unit == GL_TEXTURE0) {
            level(GL_TEXTURE_2D, 0, 0x1000 /* WIDTH */, &tw);
            level(GL_TEXTURE_2D, 0, 0x1001 /* HEIGHT */, &th);
        }
    }

    int cw = 0, ch = 0, shot = shim_screen_scene_scale(&cw, &ch);
    int kind = (shot > 1 && vp[2] == cw * shot && vp[3] == ch * shot) ? 0
             : (vp[2] == cw && vp[3] == ch) ? 1 : 2;
    kind_t *k = &g_k[kind];
    k->draws++;
    k->w = vp[2];
    k->h = vp[3];
    k->per_unit = m[0] * 0.5f * (float) vp[2];        /* pixels to one unit of x */
    const char *label = shim_gpu_program_label();
    name_count(k, label);

    int quads = count / 6;
    if (quads > QUADS_MAX) quads = QUADS_MAX;
    for (int q = 0; q < quads; q++) {
        unsigned v[4];
        int nv = 0;
        for (int j = 0; j < 6 && nv < 4; j++) {
            int at = q * 6 + j;
            unsigned ix = type == GL_UNSIGNED_SHORT ? ((const unsigned short *) idx)[at]
                        : type == GL_UNSIGNED_INT ? ((const unsigned *) idx)[at]
                        : ((const unsigned char *) idx)[at];
            int seen = 0;
            for (int s = 0; s < nv; s++) if (v[s] == ix) seen = 1;
            if (!seen) v[nv++] = ix;
        }
        if (nv != 4) continue;
        float x[4], y[4], u[4], w[4];
        for (int s = 0; s < 4; s++) {
            const float *a = (const float *) ((const char *) pp + (size_t) v[s] * (size_t) pst);
            float z = psz >= 3 ? a[2] : 0.0f;
            float cx = m[0] * a[0] + m[4] * a[1] + m[8] * z + m[12];
            float cy = m[1] * a[0] + m[5] * a[1] + m[9] * z + m[13];
            float cc = m[3] * a[0] + m[7] * a[1] + m[11] * z + m[15];
            if (cc == 0.0f) cc = 1.0f;
            x[s] = (cx / cc + 1.0f) * 0.5f * (float) vp[2] + (float) vp[0];
            y[s] = (cy / cc + 1.0f) * 0.5f * (float) vp[3] + (float) vp[1];
            if (have_tc) {
                const float *b = (const float *) ((const char *) tp + (size_t) v[s] * (size_t) tst);
                u[s] = b[0];
                w[s] = b[1];
            } else {
                u[s] = w[s] = 0.0f;
            }
        }
        k->quads++;
        int off = 0;
        float worst_here = 0.0f;
        for (int s = 0; s < 4; s++) {
            float dx, dy;
            int a = grid_class(x[s], &dx), b = grid_class(y[s], &dy);
            int c = a > b ? a : b;
            if (c == 0) k->c_whole++; else if (c == 1) k->c_half++; else k->c_between++;
            if (dx > worst_here) worst_here = dx;
            if (dy > worst_here) worst_here = dy;
            if (c) off = 1;
        }
        if (worst_here > k->worst) k->worst = worst_here;

        /* square: every corner on one of two columns and one of two rows */
        float x0 = x[0], x1 = x[0], y0 = y[0], y1 = y[0];
        for (int s = 1; s < 4; s++) {
            if (x[s] < x0) x0 = x[s];
            if (x[s] > x1) x1 = x[s];
            if (y[s] < y0) y0 = y[s];
            if (y[s] > y1) y1 = y[s];
        }
        int square = x1 > x0 && y1 > y0;
        for (int s = 0; s < 4 && square; s++)
            if ((fabsf(x[s] - x0) > 1e-3f && fabsf(x[s] - x1) > 1e-3f) ||
                (fabsf(y[s] - y0) > 1e-3f && fabsf(y[s] - y1) > 1e-3f)) square = 0;
        float per_texel = 0.0f, texel0 = 0.0f;
        if (square) {
            k->square++;
            if (have_tc && tw > 0) {
                int lo = 0, hi = 0;
                for (int s = 0; s < 4; s++) {
                    if (fabsf(x[s] - x0) <= 1e-3f) lo = s;
                    if (fabsf(x[s] - x1) <= 1e-3f) hi = s;
                }
                float a0 = u[lo] * (float) tw, a1 = u[hi] * (float) tw;
                texel0 = a0;
                if (fabsf(a1 - a0) > 1e-3f) {
                    per_texel = (x1 - x0) / fabsf(a1 - a0);
                    /* the first texel edge to the right of the left side */
                    float edge = a1 > a0 ? ceilf(a0 + 1e-4f) : floorf(a0 - 1e-4f);
                    float at = x0 + fabsf(edge - a0) * per_texel;
                    if (at < x1) {
                        int c = grid_class(at, 0);
                        if (c == 0) k->e_whole++; else if (c == 1) k->e_half++; else k->e_between++;
                        if (c) off = 1;
                    }
                }
            }
        }
        if (off && !k->have_example) {
            k->have_example = 1;
            snprintf(k->example, sizeof(k->example),
                     "%s %s quad at %.3f,%.3f, %.2f x %.2f px, texel %.3f of a %d x %d texture, "
                     "%.3f px/texel", label, square ? "square" : "turned", (double) x0, (double) y0,
                     (double) (x1 - x0), (double) (y1 - y0), (double) texel0, tw, th,
                     (double) per_texel);
        }
    }
}

#else

void shim_texel_probe(unsigned mode, int count, unsigned type, const void *idx) {
    (void) mode; (void) count; (void) type; (void) idx;
}

#endif
