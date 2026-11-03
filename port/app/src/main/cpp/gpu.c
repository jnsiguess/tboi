/*
 * The GPU's own clock, and who spent it. Report builds only.
 *
 * Why this exists. Two testers' reports (11.09.2026) show the same thing right
 * after a pill is used on a weak phone: frames of 160 ms on a Samsung A17 and
 * 60 ms on an Infinix, for ten seconds and more, with nothing loaded and the
 * processor idle - all of the time is spent in eglSwapBuffers, waiting for the
 * GPU. What the reports could not say is WHICH pass the GPU was busy with.
 *
 * What is already known from the image. The frame loop makes the Color
 * Correction Surface (0xac2460) the target before Game::Render (0x6718b0 ->
 * 0x88e54c), and that surface is one of the two the port enlarges (screen.c,
 * worth_enlarging): x4 on 2340 x 1080, x6 on 3120 x 1440, where iOS draws the
 * scene at the canvas size. The screen effects are composited into that
 * target - bloom (49 texture reads a pixel), hallucination, pixelation, Old
 * TV - so their cost grows with our enlargement. But the game starts bloom and
 * hallucination for 6 to 30 frames at a time (0x893564, 0x8956b0), which does
 * not by itself explain ten seconds.
 *
 * On a fast phone the same effect leaves no lag and no wait in the swap, so
 * waiting time is the wrong thing to measure. This measures the work instead:
 *
 *   GPU milliseconds per frame, from the driver's own timer queries
 *   (GL_EXT_disjoint_timer_query), if the driver has them;
 *
 *   which shader every draw call used, and the largest target it drew into.
 *   A shader is recognised by a line only its file contains - the table below
 *   was taken from the game's own shaders/*.fs by a script, not written by
 *   hand - so "bloom" in the report is the bloom shader and nothing else.
 *
 * Recognising the shaders is no longer diagnostics alone: bloom.c needs to know
 * when the bloom shader is in use, in every build, so the names are taken at
 * compile time always - a few string searches per shader, once. Only the
 * timing and the counting below stay behind the report.
 *
 * The report gets: a "gpu:" line when an effect shader starts being used and
 * when it stops, with the GPU time it cost while it ran; a "gpu:" line for a
 * frame that took far more GPU time than usual, naming the shaders in it; and
 * the average and worst GPU time on every frames line.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <GLES3/gl3.h>
#include "diag.h"

void *shim_android_symbol(const char *name);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

int g_gpu_on;                               /* a report is open: the draw hooks count */

/*
 * A line that appears in exactly one of the game's fragment shaders
 * (repentance-resources/data/shaders/*.fs). Ordered so that a shader sharing a
 * word with another is tried after the more specific one.
 */
static const struct { const char *name, *key; int effect; } SHADERS[] = {
    { "coloroffset_champion", "vec4 SourceColor = texture(Texture0, TexCoord0);", 0 },
    { "coloroffset_gold",     "float cubic(float x",                              0 },
    { "coloroffset_dogma",    "fragColor = vec4(Color.rgb + ColorOffsetOut * Color.a, Color.a);", 0 },
    /* before coloroffset: colormod.fs carries "ColorizeOut" in a comment, and every
       colormod pass used to be counted as a sprite draw (14.10.2026) */
    { "colormod",             "Brightness0",                                      1 },
    { "coloroffset",          "ColorizeOut",                                      0 },
    { "bg",                   "wrapx(TexCoord0",                                  0 },
    { "colorcorrection",      "GammaOut",                                         0 },
    { "bloom",                "OutBloomAmount",                                   1 },
    { "dizzy",                "mat2 rmat = mat2(cos(rot)",                        1 },
    { "hallucination",        "vec3(dot(vec3(0.2126,0.7152,0.0722)",              1 },
    { "heat_wave",            "* 50.0 * Ratio0.x",                                1 },
    { "water_overlay",        "* 10.0 * Ratio0.x",                                1 },
    /* not "lum_add": the port's hq4x.fs of 13.10.2026 had none of it; the game's own
       (the only one since hotfix 1.75) has both */
    { "hq4x",                 "ScreenSizePow2Out",                                1 },
    { "mirror",               "vec2 diff = 0.7 * (texcoord - center)",            1 },
    { "oldtv",                "scanline",                                         1 },
    { "pixelation",           "OutPixelationAmount",                              1 },
    { "shockwave",            "Shockwave2Out",                                    1 },
    { "water_v2",             "pow(dot(n, h), 160.0)",                            1 },
    { "water",                "clamp(color.rgb, vec3(0.0,0.0,0.0), vec3(1.0,1.0,1.0))", 1 },
};
#define SHADERS_N ((int) (sizeof(SHADERS) / sizeof(SHADERS[0])))
#define LABELS (SHADERS_N + 1)                  /* label 0: a shader that is none of the files */

static const char *label_name(int l) {
    return (l <= 0 || l > SHADERS_N) ? "other" : SHADERS[l - 1].name;
}

#define GL_FRAGMENT_SHADER_   0x8B30
#define GL_SHADER_TYPE_       0x8B4F
#define GL_TIME_ELAPSED_EXT   0x88BF
#define GL_QUERY_RESULT_EXT   0x8866
#define GL_QUERY_AVAILABLE    0x8867
#define GL_GPU_DISJOINT_EXT   0x8FBB

#define IDS 4096
static unsigned char g_frag[IDS];           /* shader id -> label + 1; 0 = not a fragment shader */
static unsigned char g_prog_label[IDS];     /* program id -> label */
static unsigned g_prog;
static int g_vw, g_vh;

typedef struct { int draws; long verts; int w, h; } use_t;
static use_t g_now[LABELS];

static int contains(const char *s, int n, const char *key) {
    int k = (int) strlen(key);
    for (int i = 0; i + k <= n; i++)
        if (s[i] == key[0] && !memcmp(s + i, key, (size_t) k)) return 1;
    return 0;
}

/* ---------------------------------------------------------- the shaders */

void shim_gpu_shader_source(unsigned sh, int count, const char *const *str, const int *len) {
    if (sh >= IDS || !str || count <= 0) return;
    void (*getiv)(unsigned, unsigned, int *) =
        (void (*)(unsigned, unsigned, int *)) shim_android_symbol("glGetShaderiv");
    int type = 0;
    if (getiv) getiv(sh, GL_SHADER_TYPE_, &type);
    if (type != GL_FRAGMENT_SHADER_) return;
    int label = 0;
    for (int k = 0; k < SHADERS_N && !label; k++)
        for (int i = 0; i < count && !label; i++) {
            if (!str[i]) continue;
            int n = (len && len[i] >= 0) ? len[i] : (int) strlen(str[i]);
            if (contains(str[i], n, SHADERS[k].key)) label = k + 1;
        }
    g_frag[sh] = (unsigned char) (label + 1);
    /* each name once, so the report says which of the files the game really compiled */
    static unsigned char named[LABELS];
    static int others;
    if (!shim_report_active()) return;
    if (label && !named[label]) {
        named[label] = 1;
        char line[96];
        snprintf(line, sizeof(line), "shader %s compiled (as %u)", label_name(label), sh);
        report_line("gpu", line);
    } else if (!label && others++ < 4) {
        char line[96];
        snprintf(line, sizeof(line), "a fragment shader that is none of the files (as %u)", sh);
        report_line("gpu", line);
    }
}

void shim_gpu_attach(unsigned prog, unsigned sh) {
    if (prog >= IDS || sh >= IDS || !g_frag[sh]) return;
    g_prog_label[prog] = (unsigned char) (g_frag[sh] - 1);
}

void shim_gpu_use(unsigned prog) { g_prog = prog; }

/* the program in use is the game's bloom shader (bloom.c) - known in every build */
int shim_gpu_bloom_bound(void) {
    static int bloom = -1;
    if (bloom < 0) {
        bloom = 0;
        for (int k = 0; k < SHADERS_N; k++)
            if (!strcmp(SHADERS[k].name, "bloom")) bloom = k + 1;
    }
    return bloom > 0 && g_prog < IDS && g_prog_label[g_prog] == bloom;
}
/*
 * The program in use lays the picture on the window (stretch.c): colorcorrection.
 * hq4x.fs did it too while the Filter option existed; the Filter is off for good
 * since hotfix 1.75 (loader.c filter_gone) and the port no longer carries its own
 * hq4x.fs, so the game's colour correction is the one road onto the window.
 */
int shim_gpu_stretch_bound(void) {
    static int cc = -1;
    if (cc < 0) {
        cc = 0;
        for (int k = 0; k < SHADERS_N; k++)
            if (!strcmp(SHADERS[k].name, "colorcorrection")) cc = k + 1;
    }
    int l = g_prog < IDS ? g_prog_label[g_prog] : 0;
    return l && l == cc;
}
unsigned shim_gpu_program(void) { return g_prog; }
/* the shader file of the program in use, by name ("other" for the engine's own) - texel.c */
const char *shim_gpu_program_label(void) {
    return label_name(g_prog < IDS ? g_prog_label[g_prog] : 0);
}

void shim_gpu_viewport(int w, int h) { g_vw = w; g_vh = h; }

void shim_gpu_draw(int count) {
    int l = g_prog < IDS ? g_prog_label[g_prog] : 0;
    use_t *u = &g_now[l];
    u->draws++;
    u->verts += count;
    if ((long) g_vw * g_vh > (long) u->w * u->h) { u->w = g_vw; u->h = g_vh; }
}

/* ------------------------------------------------------------ the clock */

#define RING 8
typedef struct { unsigned q; int state; long frame; double wall; use_t use[LABELS]; } slot_t;   /* 0 free, 1 running, 2 waiting */
static slot_t g_ring[RING];
static int g_running = -1;
static int g_timer = -1;                    /* -1 not asked yet, 0 none, 1 the driver has them */
static long g_frame;

/*
 * THE REPORT'S OWN SLOW FRAME (26.10.2026). The probe frame below (report
 * builds) calls glFinish at every pass's end, and on a busy Mali that is a
 * 35-45 ms frame spent in the driver's futex - exactly the "stall" the frame
 * thread's lines then reported as the game's, forty times a run. The stall
 * line asks this and says so.
 */
static long g_probe_frame = -1000;
int shim_gpu_probe_recent(void) { return g_frame - g_probe_frame <= 1; }

static void (*q_gen)(int, unsigned *);
static void (*q_begin)(unsigned, unsigned);
static void (*q_end)(unsigned);
static void (*q_geti)(unsigned, unsigned, int *);
static void (*q_get64)(unsigned, unsigned, long long *);

static double g_usual;                      /* a slow average, not dragged up by the spikes */
static double g_win_sum, g_win_worst;
static long g_win_n;

/* how long an effect shader has been in use, and what the GPU spent meanwhile */
static int g_fx_on[LABELS];
static long g_fx_since[LABELS], g_fx_last[LABELS], g_fx_n[LABELS];
static double g_fx_ms[LABELS];

/*
 * AND WHAT THE PLAYER SAW: the frame's own length, start to start (08.10.2026).
 *
 * GPU milliseconds alone cannot answer "it drops the FPS": a frame can be slow
 * on the processor with the card idle, and the effect lines then said "GPU as
 * usual" about frames that were not. So every frame also carries its length on
 * the wall clock - from its own start to the next frame's start, vsync waits
 * included, which is what a frame rate is - and an effect's lines give it
 * beside the GPU time, against the usual length.
 */
static double g_usual_wall;
static double g_fx_wall[LABELS], g_fx_wall_worst[LABELS];
static long g_fx_wall_n[LABELS];

#define FX_GAP 30                           /* frames without a draw before an effect counts as off */

static void setup(void) {
    const char *ext = (const char *) glGetString(GL_EXTENSIONS);
    const char *renderer = (const char *) glGetString(GL_RENDERER);
    int has = ext && strstr(ext, "GL_EXT_disjoint_timer_query");
    q_gen = (void (*)(int, unsigned *)) shim_android_symbol("glGenQueriesEXT");
    q_begin = (void (*)(unsigned, unsigned)) shim_android_symbol("glBeginQueryEXT");
    q_end = (void (*)(unsigned)) shim_android_symbol("glEndQueryEXT");
    q_geti = (void (*)(unsigned, unsigned, int *)) shim_android_symbol("glGetQueryObjectivEXT");
    q_get64 = (void (*)(unsigned, unsigned, long long *)) shim_android_symbol("glGetQueryObjecti64vEXT");
    g_timer = (has && q_gen && q_begin && q_end && q_geti && q_get64) ? 1 : 0;
    if (g_timer) {
        for (int i = 0; i < RING; i++) q_gen(1, &g_ring[i].q);
        for (int i = 0; i < RING; i++) if (!g_ring[i].q) g_timer = 0;
    }
    char line[220];
    snprintf(line, sizeof(line), "%s (%s; extension %s, functions %s)",
             g_timer ? "timer queries on: GPU milliseconds are measured"
                     : "no timer queries: draws are counted, GPU time is not",
             renderer ? renderer : "?", has ? "present" : "absent",
             (q_gen && q_begin && q_end && q_geti && q_get64) ? "found" : "missing");
    report_line("gpu", line);
}

static void top_uses(const use_t *use, char *out, size_t room) {
    int shown[LABELS] = {0};
    size_t at = 0;
    out[0] = 0;
    for (int k = 0; k < 6; k++) {
        int best = -1;
        for (int l = 0; l < LABELS; l++)
            if (!shown[l] && use[l].draws > 0 && (best < 0 || use[l].draws > use[best].draws)) best = l;
        if (best < 0) break;
        shown[best] = 1;
        if (at < room)
            at += (size_t) snprintf(out + at, room - at, "%s%s %d @%dx%d", at ? ", " : "",
                                    label_name(best), use[best].draws, use[best].w, use[best].h);
    }
}

/*
 * one finished frame: its shaders, its GPU time when there is a clock (ms < 0
 * when not) and its length on the wall clock (wall < 0 when not known)
 */
static void finished(const use_t *use, long frame, double ms, double wall) {
    if (ms >= 0.0) {
        g_win_sum += ms; g_win_n++;
        if (ms > g_win_worst) g_win_worst = ms;
        if (g_usual <= 0.0) g_usual = ms;
        else {
            double clipped = ms < g_usual * 2.0 ? ms : g_usual * 2.0;
            g_usual += (clipped - g_usual) * 0.02;
        }
    }
    if (wall > 0.0 && wall < 1000.0) {          /* a second and more is a pause, not a frame */
        if (g_usual_wall <= 0.0) g_usual_wall = wall;
        else {
            double clipped = wall < g_usual_wall * 2.0 ? wall : g_usual_wall * 2.0;
            g_usual_wall += (clipped - g_usual_wall) * 0.02;
        }
    } else {
        wall = -1.0;
    }
    static int told;
    for (int l = 1; l < LABELS; l++) {
        if (!SHADERS[l - 1].effect) continue;
        if (use[l].draws > 0) {
            if (!g_fx_on[l]) {
                g_fx_on[l] = 1; g_fx_since[l] = frame; g_fx_n[l] = 0; g_fx_ms[l] = 0.0;
                g_fx_wall[l] = 0.0; g_fx_wall_worst[l] = 0.0; g_fx_wall_n[l] = 0;
                if (told < 400) {
                    told++;
                    char line[240];
                    int at;
                    if (ms >= 0.0)
                        at = snprintf(line, sizeof(line), "%s on: %d draw(s) into %dx%d | GPU %.1f ms, usual %.1f",
                                      label_name(l), use[l].draws, use[l].w, use[l].h, ms, g_usual);
                    else
                        at = snprintf(line, sizeof(line), "%s on: %d draw(s) into %dx%d",
                                      label_name(l), use[l].draws, use[l].w, use[l].h);
                    if (wall > 0.0 && at > 0 && at < (int) sizeof(line))
                        snprintf(line + at, sizeof(line) - (size_t) at, " | frame %.1f ms, usual %.1f",
                                 wall, g_usual_wall);
                    report_line("gpu", line);
                }
            }
            g_fx_last[l] = frame;
            if (ms >= 0.0) { g_fx_ms[l] += ms; g_fx_n[l]++; }
            if (wall > 0.0) {
                g_fx_wall[l] += wall; g_fx_wall_n[l]++;
                if (wall > g_fx_wall_worst[l]) g_fx_wall_worst[l] = wall;
            }
        } else if (g_fx_on[l] && frame - g_fx_last[l] > FX_GAP) {
            g_fx_on[l] = 0;
            if (told < 400) {
                told++;
                char line[240];
                int at;
                if (g_fx_n[l] > 0)
                    at = snprintf(line, sizeof(line), "%s off after %ld frames | GPU %.1f ms a frame while on, usual %.1f",
                                  label_name(l), g_fx_last[l] - g_fx_since[l] + 1, g_fx_ms[l] / (double) g_fx_n[l], g_usual);
                else
                    at = snprintf(line, sizeof(line), "%s off after %ld frames",
                                  label_name(l), g_fx_last[l] - g_fx_since[l] + 1);
                if (g_fx_wall_n[l] > 0 && at > 0 && at < (int) sizeof(line))
                    snprintf(line + at, sizeof(line) - (size_t) at,
                             " | frames %.1f ms avg, %.1f worst while on, usual %.1f",
                             g_fx_wall[l] / (double) g_fx_wall_n[l], g_fx_wall_worst[l], g_usual_wall);
                report_line("gpu", line);
            }
        }
    }
    /* a frame far heavier than usual: which shaders were in it */
    static long spoke_at = -1000;
    static int heavy;
    if (ms >= 0.0 && g_usual > 0.0 && ms > g_usual * 1.8 && ms > g_usual + 3.0
        && frame - spoke_at >= 60 && heavy < 200) {
        heavy++;
        spoke_at = frame;
        char tops[260], line[380];
        top_uses(use, tops, sizeof(tops));
        if (wall > 0.0)
            snprintf(line, sizeof(line), "heavy frame %.1f ms, usual %.1f | frame %.1f ms | %s",
                     ms, g_usual, wall, tops);
        else
            snprintf(line, sizeof(line), "heavy frame %.1f ms, usual %.1f | %s", ms, g_usual, tops);
        report_line("gpu", line);
    }
}

/*
 * The disjoint flag is asked ONCE a frame and the answer kept: asking clears
 * it, and the pass clock further down (report builds) needs the same answer.
 */
static int g_disjoint;

static void harvest(void) {
    int disjoint = g_disjoint;
    for (;;) {
        int pick = -1;
        for (int i = 0; i < RING; i++)
            if (g_ring[i].state == 2 && (pick < 0 || g_ring[i].frame < g_ring[pick].frame)) pick = i;
        if (pick < 0) return;
        int ready = 0;
        q_geti(g_ring[pick].q, GL_QUERY_AVAILABLE, &ready);
        if (!ready) return;                              /* the older ones come first */
        long long ns = 0;
        q_get64(g_ring[pick].q, GL_QUERY_RESULT_EXT, &ns);
        g_ring[pick].state = 0;
        if (!disjoint) finished(g_ring[pick].use, g_ring[pick].frame, (double) ns / 1e6, g_ring[pick].wall);
    }
}

/*
 * The frame's length, start to start: known only when the next one starts,
 * so it is written into the slot of the frame it belongs to then - before the
 * harvest, which cannot have taken that slot yet (it closed after the last
 * start).
 */
static long long g_open_ns;
static int g_last_slot = -1;
static long g_last_slot_frame;
static double g_last_wall = -1.0;           /* the frame before, for the road with no clock */

static void frame_length(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now = (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
    g_last_wall = g_open_ns ? (double) (now - g_open_ns) / 1e6 : -1.0;
    g_open_ns = now;
    if (g_last_slot >= 0 && g_ring[g_last_slot].state == 2 && g_ring[g_last_slot].frame == g_last_slot_frame)
        g_ring[g_last_slot].wall = g_last_wall;
    g_last_slot = -1;
}

/* the start of a frame, with the context current (gl.c) */
/*
 * THE CLOCK ON THE CARD IS OFF WHILE THIS IS BEING ANSWERED.
 *
 * The frame stands in an ioctl on /dev/kgsl-3d0 - the kernel's own word, read
 * from /proc/self/task/<tid>/syscall - while not one of the GL calls the layer
 * stands in front of takes any time at all. Something is making the frame wait
 * for the card WITHOUT being a draw, and this file is the first suspect: a
 * GL_TIME_ELAPSED query around every frame and a glGetIntegerv of
 * GL_GPU_DISJOINT_EXT inside it, both of which can put a fence in the pipe -
 * and all of it switched on by nothing more than a report being open, which is
 * exactly the build the lag is felt in (14.09.2026).
 *
 * ANSWERED: it was NOT this. With the clock off the hitches kept their depth
 * exactly - 62 ms against 60, three quarters of the samples still inside
 * __ioctl - so gpu.c is cleared and the clock goes back on. The lag was a grid
 * of glReadPixels in gl.c, put there by a diagnostic of ours.
 */
#define GPU_CLOCK 1

void shim_gpu_frame_open(void) {
    g_gpu_on = GPU_CLOCK && shim_report_active();
    if (!g_gpu_on) return;
    if (g_timer < 0) setup();
    g_frame++;
    frame_length();
    if (g_timer > 0) {
        g_disjoint = 0;
        glGetIntegerv(GL_GPU_DISJOINT_EXT, &g_disjoint);   /* asking clears it */
    }
#if ISAAC_DIAG
    { void shim_gpu_passes_open(void); shim_gpu_passes_open(); }
#endif
    if (g_timer > 0) {
        if (g_running >= 0) {                            /* no present last frame: nothing to keep */
            q_end(GL_TIME_ELAPSED_EXT);
            g_ring[g_running].state = 0;
            g_running = -1;
        }
        harvest();
        for (int i = 0; i < RING; i++)
            if (g_ring[i].state == 0) {
                q_begin(GL_TIME_ELAPSED_EXT, g_ring[i].q);
                g_ring[i].state = 1;
                g_ring[i].frame = g_frame;
                g_running = i;
                break;
            }
    }
    memset(g_now, 0, sizeof(g_now));
}

/* the end of the frame's drawing, just before the swap (gl.c) */
void shim_gpu_frame_close(void) {
    if (!g_gpu_on) return;
#if ISAAC_DIAG
    { void shim_gpu_passes_close(void); shim_gpu_passes_close(); }
#endif
    if (g_timer > 0) {
        if (g_running < 0) return;
        q_end(GL_TIME_ELAPSED_EXT);
        memcpy(g_ring[g_running].use, g_now, sizeof(g_now));
        g_ring[g_running].state = 2;
        g_ring[g_running].wall = -1.0;          /* written at the next start (frame_length) */
        g_last_slot = g_running;
        g_last_slot_frame = g_ring[g_running].frame;
        g_running = -1;
    } else {
        finished(g_now, g_frame, -1.0, g_last_wall);   /* no clock: the frame before's length */
    }
}

/* for the frames line: the GPU time over the window just closed, "" when there is no clock */
void shim_gpu_window(char *out, size_t room) {
    if (!room) return;
    if (g_timer <= 0 || g_win_n == 0) { snprintf(out, room, "gpu not timed"); return; }
    snprintf(out, room, "gpu %.1f ms avg, %.1f worst", g_win_sum / (double) g_win_n, g_win_worst);
    g_win_sum = 0.0; g_win_worst = 0.0; g_win_n = 0;
}

#if ISAAC_DIAG
/* ================================================================ THE PASSES
 *
 * WHAT THE FILTER-OFF FRAME DOES THAT THE FILTER-ON FRAME DOES NOT. Report
 * builds only (06.10.2026).
 *
 * Players: heavy lag with Filter off, gone with Filter on - and POTATO, which
 * draws the scene at one canvas just as Filter does, did not help them. Read
 * in the image, the frame loop 0x671788 makes the same calls on both roads
 * except that Filter on ADDS one canvas-sized pass (the colour correction
 * composited into the HQX4 surface, 0x671958) and draws the last full-screen
 * quad with hq4x.fs instead of colorcorrection.fs (0x671a78). On paper the
 * Filter-off road is the lighter one. So the difference has to be counted on
 * the phone, not argued: how the frame splits into render passes, which of
 * them start without a clear (on a tiled GPU a pass that is not cleared LOADS
 * its whole target from memory first), how often the same framebuffer has its
 * texture swapped (0x8a6dc4 re-attaches and re-checks on every bind), what is
 * uploaded or re-specified, and - where the driver has timestamps - the GPU
 * milliseconds of every kind of pass.
 *
 * A pass here is one draw target: a framebuffer and the texture on its colour
 * slot. It is named by what that texture is - the game's own surfaces, read
 * from their slots (texture name at +0xb8, the field 0x8a6dc4 attaches):
 *
 *   screen  the game's screen framebuffer (its colour is our renderbuffer)
 *   scene   Color Correction Surface 0xac2460 - Game::Render draws into it
 *   hqx4    HQX4 Surface 0xac2430 - only with Filter on (never since hotfix 1.75)
 *   render  Render Surface 0xac2450
 *   room    Colormod Surface 0xac3be8 - the room itself (floor, walls, rocks,
 *           everything in it), enlarged since 1.75: the shimmer's cure
 *   fx      every other off-screen surface (light, shadows, water, ...)
 *   blit    ours: the screen renderbuffer copied into the window (gl.c)
 *
 * Frames are summed by road: a frame with an hqx4 pass is road B (Filter on),
 * any other is road A. Both are printed beside the frames line, a minute at a
 * time, so one minute with Filter off and one with it on give the two side by
 * side. Nothing here changes a call the game makes; it only watches the
 * wrappers in shim.c and asks GL a few questions of its own, never one that
 * waits for the card.
 */
#include <EGL/egl.h>

unsigned long shim_image_base(void);
int shim_screen_scene_scale(int *cw, int *ch);
int shim_screen_shot_ceiling_get(void);
extern long long g_gl_swap_ns;
extern long long g_gl_ns_draw, g_gl_ns_clear, g_gl_ns_bindfb, g_gl_tex_ns;
extern long long g_glw_ns[];

#define GL_TIMESTAMP_EXT_           0x8E28
#define GL_QUERY_COUNTER_BITS_EXT_  0x8864
#define GL_FRAMEBUFFER_             0x8D40
#define GL_DRAW_FRAMEBUFFER_        0x8CA9
#define GL_COLOR_ATTACHMENT0_       0x8CE0
#define GL_DEPTH_ATTACHMENT_        0x8D00
#define ATT_TYPE                    0x8CD0      /* FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE */
#define ATT_NAME                    0x8CD1      /* FRAMEBUFFER_ATTACHMENT_OBJECT_NAME */
#define ATT_DEPTH_SIZE              0x8216
#define GLW_KINDS                   14          /* shim.c's wrapped calls */

/*
 * bloom: OURS - the bloom composite redone at the canvas size into bloom.c's
 * own buffer (08.10.2026). The scene's pass is broken around it: the part
 * before it is stamped as scene, the canvas pass as bloom, and the scene's
 * pass after it starts with a load - which is counted, being its price on a
 * tiled card.
 *
 * glow: the game's Bloom Surface (0xac3ba8) as a target - while bloom is on the
 * scene is drawn into it before the composite reads it back (25.10.2026). At x1
 * that pass and the composite were lumped into "fx" and could not be told from
 * the room's other effects.
 */
enum { PC_SCREEN, PC_SCENE, PC_HQX4, PC_RENDER, PC_FX, PC_BLIT, PC_BLOOM, PC_ROOM, PC_GLOW, PC_N };
static const char *const PC_NAME[PC_N] = { "screen", "scene", "hqx4", "render", "fx", "blit", "bloom", "room", "glow" };

/*
 * THE CARD'S TIME WITHOUT ITS CLOCK (16.10.2026). Timestamps are an extension
 * a Mali may not offer, and then the card's side of a frame went unmeasured.
 * So one frame in PROBE_EVERY is taken apart the plain way: glFinish before
 * the frame and at the end of every pass, the wall time between two of them
 * given to the pass that just ended. A tiled card runs a frame pass by pass
 * anyway, so the split is the true one; what it costs is that one frame, made
 * slow on purpose - two seconds apart, a minute holds thirty of them.
 */
#define PROBE_EVERY 120
static int g_probe;                 /* this frame is taken apart */
static long long g_probe_t;

typedef struct {
    long frames;
    long binds, same, empty, attaches, reattaches, statuses, clears, late_clears;
    long passes[PC_N], draws[PC_N], loads[PC_N], dloads[PC_N];
    double mpx[PC_N], load_mpx[PC_N];
    double elems[PC_N];             /* indices (or vertices) handed to the draws: the client-array volume */
    long teximg, respec, texsub, readpix, finish, flush;
    double gl_cpu_ms, swap_ms;
    long swaps;
    double gpu_ms[PC_N], gpu_sum_ms;
    double fin_ms[PC_N], fin_sum_ms;  /* the probe frames' card time by target (glFinish) */
    long fin_in[PC_N], fin_frames;
    long gpu_in[PC_N];              /* timed frames the kind of target appeared in */
    long gpu_frames, gpu_lost;
    long gpu_stamps, gpu_bad;       /* stamps read, and of them lost or out of order */
    int shot, ceiling, mixed;
} pb_t;

/*
 * 2: frames of either road that drew the bloom composite (25.10.2026). A pill
 * lights it for sixty frames, and "while on" in the effect lines gave only the
 * frame's total; kept apart, the same lines by target say which pass the
 * extra time went to.
 */
static pb_t g_pb[3];                /* 0: road A, no hqx4 pass; 1: road B; 2: bloom on */
static int g_pf_bloom;              /* the frame being drawn has drawn the composite */
static pb_t g_pf;                   /* the frame being drawn */
static int g_pf_open;
static int g_last_path = -1;        /* the road of the last frame closed: its swap is added later */
static long long g_pf_cpu0;

#define FBS 16
static unsigned g_fb_draw;
static unsigned g_fb_color[FBS];    /* the texture on the colour slot; 0 = a renderbuffer or nothing */
static signed char g_fb_known[FBS];
static signed char g_fb_depth[FBS]; /* depth bits, 0 = none, -1 = not asked */
static unsigned g_screen_fb;        /* the framebuffer whose colour is a renderbuffer */
static unsigned char g_specified[8192];   /* a bit a texture name: storage already given */

typedef struct { int open; unsigned fb, tex; int cls, live, drew, ccol, cdep; long area; } pass_t;
static pass_t g_pass;

/* timestamps: one at the start of a frame and one at the end of every pass */
#define TS_SLOTS 4
#define TS_MAX 64
typedef struct { unsigned q[TS_MAX]; signed char cls[TS_MAX]; int n, state, path, over; long frame; } ts_t;
static ts_t g_ts[TS_SLOTS];         /* state: 0 free, 1 being filled, 2 waiting for the card */
static int g_ts_on = -1;            /* -1 not asked, 0 none, else the counter's bits */
static int g_ts_cur = -1;
static void (*ts_counter)(unsigned, unsigned);

static int pass_on(void) { return g_gpu_on && g_pf_open; }

static const char *egl_word(EGLint v) {
    switch (v) {
    case EGL_BUFFER_PRESERVED: return "preserved";
    case EGL_BUFFER_DESTROYED: return "destroyed";
    case EGL_BACK_BUFFER:      return "back";
    case EGL_SINGLE_BUFFER:    return "single";
    default:                   return "?";
    }
}

static void ts_setup(void) {
    g_ts_on = 0;
    int bits = 0;
    if (g_timer > 0) {
        ts_counter = (void (*)(unsigned, unsigned)) shim_android_symbol("glQueryCounterEXT");
        if (!ts_counter) ts_counter = (void (*)(unsigned, unsigned)) eglGetProcAddress("glQueryCounterEXT");
        void (*getq)(unsigned, unsigned, int *) =
            (void (*)(unsigned, unsigned, int *)) shim_android_symbol("glGetQueryivEXT");
        if (!getq) getq = (void (*)(unsigned, unsigned, int *)) eglGetProcAddress("glGetQueryivEXT");
        if (getq) getq(GL_TIMESTAMP_EXT_, GL_QUERY_COUNTER_BITS_EXT_, &bits);
        if (ts_counter && bits > 0) {
            g_ts_on = bits;
            for (int i = 0; i < TS_SLOTS; i++) {
                q_gen(TS_MAX, g_ts[i].q);
                for (int k = 0; k < TS_MAX; k++) if (!g_ts[i].q[k]) g_ts_on = 0;
            }
        }
    }
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLSurface surf = eglGetCurrentSurface(EGL_DRAW);
    EGLint swap = 0, rb = 0;
    if (dpy != EGL_NO_DISPLAY && surf != EGL_NO_SURFACE) {
        eglQuerySurface(dpy, surf, EGL_SWAP_BEHAVIOR, &swap);
        eglQuerySurface(dpy, surf, EGL_RENDER_BUFFER, &rb);
    }
    char line[200];
    snprintf(line, sizeof(line),
             "pass clock: %s (counter bits %d) | window: swap behaviour %s, render buffer %s",
             g_ts_on > 0 ? "timestamps on, GPU time by pass" : "no timestamps, passes counted only",
             bits, egl_word(swap), egl_word(rb));
    report_line("gpu", line);
}

static void ts_stamp(int cls) {
    if (g_ts_cur < 0 || g_ts_on <= 0) return;
    ts_t *t = &g_ts[g_ts_cur];
    if (t->n >= TS_MAX) { t->over = 1; return; }
    ts_counter(t->q[t->n], GL_TIMESTAMP_EXT_);
    t->cls[t->n++] = (signed char) cls;
}

/*
 * The frames the card has finished, never waiting for one that it has not.
 *
 * WHY "GPU by target" PRINTED HUNDREDS OF MILLIONS OF MILLISECONDS (1.72/1.73
 * reports: "scene 525813823.21 ms", "fx 502448538.35 ms" beside "the frame
 * 3.14 ms"). Two faults, one of them in the reading:
 *
 *   Only the LAST stamp of a frame was asked whether it had been written, and
 *   every one before it was read on trust. A stamp that never got written
 *   reads as nought; the next difference was then taken from nought - a whole
 *   timestamp of the card's clock, days of uptime - and a negative one was
 *   clamped to nought and became the new base. The frame's total (last minus
 *   first) stayed sane, which is exactly what the lines showed.
 *
 *   And a stamp was written AFTER the new target had been bound. On a tiled
 *   card a timestamp lands in the command stream of the pass it is issued in,
 *   and that was the NEXT pass - one the driver may never run at all when
 *   nothing is drawn into it. Now the stamp closing a pass is issued while
 *   that pass is still the target (shim_gpu_pass_leave_*, called by the
 *   wrappers before the real bind and attach).
 *
 * So every stamp is asked for itself; one that is not written, is nought, runs
 * backwards or lies more than a second from the frame's first is skipped, and
 * its time goes to the next good stamp's pass. The count of such stamps is
 * printed with the line, so a number that leans on them says so.
 */
static void ts_harvest(void) {
    if (g_ts_on <= 0) return;
    for (int i = 0; i < TS_SLOTS; i++) {
        ts_t *t = &g_ts[i];
        if (t->state != 2) continue;
        pb_t *b = &g_pb[t->path];
        if (t->over || t->n < 2) { t->state = 0; b->gpu_lost++; continue; }
        int ready = 0;
        q_geti(t->q[t->n - 1], GL_QUERY_AVAILABLE, &ready);
        if (!ready) {
            /* a last stamp never written would hold its slot for good, and four of them stop the clock */
            if (g_frame - t->frame > 4 * TS_SLOTS) { t->state = 0; b->gpu_lost++; }
            continue;
        }
        t->state = 0;
        if (g_disjoint) { b->gpu_lost++; continue; }
        long long first = 0, prev = 0;
        int have = 0, seen[PC_N] = {0};
        double part[PC_N] = {0};
        for (int k = 0; k < t->n; k++) {
            int ok = 0;
            long long v = 0;
            q_geti(t->q[k], GL_QUERY_AVAILABLE, &ok);
            if (ok) q_get64(t->q[k], GL_QUERY_RESULT_EXT, &v);
            b->gpu_stamps++;
            if (!ok || v <= 0 || (have && (v < prev || v - first > 1000000000LL))) { b->gpu_bad++; continue; }
            if (!have) { first = prev = v; have = 1; continue; }
            int c = t->cls[k];
            if (c >= 0 && c < PC_N) { part[c] += (double) (v - prev) / 1e6; seen[c] = 1; }
            prev = v;
        }
        if (!have || prev == first) { b->gpu_lost++; continue; }
        for (int c = 0; c < PC_N; c++)
            if (seen[c]) { b->gpu_ms[c] += part[c]; b->gpu_in[c]++; }
        b->gpu_sum_ms += (double) (prev - first) / 1e6;
        b->gpu_frames++;
    }
}

static long long gl_wall_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long) t.tv_sec * 1000000000LL + t.tv_nsec;
}

static unsigned surf_tex(unsigned long b, unsigned long slot) {
    const unsigned char *s = *(const unsigned char *const *) (b + slot);
    return s ? *(const unsigned *) (s + 0xb8) : 0;
}

static int classify(unsigned fb, unsigned tex) {
    if (fb == 0 || fb == g_screen_fb || !tex) return PC_SCREEN;
    unsigned long b = shim_image_base();
    if (b) {
        if (tex == surf_tex(b, 0xac3be8)) return PC_ROOM;
        if (tex == surf_tex(b, 0xac3ba8)) return PC_GLOW;
        if (tex == surf_tex(b, 0xac2460)) return PC_SCENE;
        if (tex == surf_tex(b, 0xac2430)) return PC_HQX4;
        if (tex == surf_tex(b, 0xac2450)) return PC_RENDER;
    }
    return PC_FX;
}

static void pass_close(void) {
    if (!g_pass.open) return;
    g_pass.open = 0;
    int c = g_pass.cls;
    if (!g_pass.live) { g_pf.empty++; return; }
    g_pf.passes[c]++;
    if (g_pass.drew) {
        double mpx = (double) g_pass.area / 1e6;
        g_pf.mpx[c] += mpx;
        if (!g_pass.ccol) { g_pf.loads[c]++; g_pf.load_mpx[c] += mpx; }
        if (!g_pass.cdep && g_pass.fb < FBS && g_fb_depth[g_pass.fb] > 0) g_pf.dloads[c]++;
    }
    ts_stamp(c);                    /* this pass ends here */
    if (g_probe) {                  /* a probe frame: the card's time for this pass, the plain way */
        glFinish();
        long long t = gl_wall_now();
        g_pf.fin_ms[c] += (double) (t - g_probe_t) / 1e6;
        g_probe_t = t;
    }
}

static void pass_retarget(void) {
    unsigned fb = g_fb_draw, tex = fb < FBS ? g_fb_color[fb] : 0;
    if (g_pass.open && g_pass.fb == fb && g_pass.tex == tex) { g_pf.same++; return; }
    pass_close();
    g_pass.open = 1;
    g_pass.fb = fb;
    g_pass.tex = tex;
    g_pass.cls = classify(fb, tex);
    g_pass.live = g_pass.drew = g_pass.ccol = g_pass.cdep = 0;
    g_pass.area = 0;
}

/* what is on a framebuffer, asked once, with it bound */
static void fb_learn(unsigned fb) {
    if (!fb || fb >= FBS || g_fb_known[fb]) return;
    g_fb_known[fb] = 1;
    GLint type = 0, name = 0, depth = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, ATT_TYPE, &type);
    if (type == GL_TEXTURE) {
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, ATT_NAME, &name);
        g_fb_color[fb] = (unsigned) name;
    } else if (type == GL_RENDERBUFFER && !g_screen_fb) {
        g_screen_fb = fb;
    }
    type = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER_, GL_DEPTH_ATTACHMENT_, ATT_TYPE, &type);
    if (type != GL_NONE)
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER_, GL_DEPTH_ATTACHMENT_, ATT_DEPTH_SIZE, &depth);
    g_fb_depth[fb] = (signed char) (type != GL_NONE ? (depth > 0 ? depth : 1) : 0);
}

static long long gl_cpu_now(void) {
    long long s = g_gl_ns_draw + g_gl_ns_clear + g_gl_ns_bindfb + g_gl_tex_ns;
    for (int k = 0; k < GLW_KINDS; k++) s += g_glw_ns[k];
    return s;
}

/* ------------------------------------------------- the wrappers' side (shim.c) */

/*
 * BEFORE the real bind and attach: when the target is about to change, the
 * pass that ends is closed - and stamped - while it is still the target, so
 * the stamp lands in that pass's commands and not in the next one's (see
 * ts_harvest). The after-hooks below then open the new pass.
 */
void shim_gpu_pass_leave_bind(unsigned target, unsigned fb) {
    if (target != GL_FRAMEBUFFER_ && target != GL_DRAW_FRAMEBUFFER_) return;
    if (!pass_on() || !g_pass.open) return;
    unsigned tex = fb < FBS ? g_fb_color[fb] : 0;
    if (g_pass.fb == fb && g_pass.tex == tex) return;
    pass_close();
}

void shim_gpu_pass_leave_attach(unsigned target, unsigned att, unsigned tex) {
    if ((target != GL_FRAMEBUFFER_ && target != GL_DRAW_FRAMEBUFFER_) || att != GL_COLOR_ATTACHMENT0_) return;
    if (!pass_on() || !g_pass.open) return;
    if (g_pass.fb == g_fb_draw && g_pass.tex == tex) return;
    pass_close();
}

/*
 * bloom.c, report builds: its canvas-sized pass, which the wrappers never see
 * (its binds are its own). Begin: called with the scene still the target, so
 * the scene so far is stamped where it ran. End: called with bloom.c's buffer
 * still the target, so the canvas pass is stamped as "bloom"; then the scene's
 * pass is taken up again as a new one that LOADS what was there - that load,
 * and bloom.c's composite into it, are what the redirect adds to the frame.
 */
static pass_t g_bloom_saved;
static int g_bloom_have;

void shim_gpu_pass_bloom_begin(int w, int h, int count) {
    if (!pass_on()) return;
    g_bloom_saved = g_pass;
    g_bloom_have = g_pass.open;
    pass_close();
    g_pass.open = 1;
    g_pass.fb = 0xFFFFFFFFu;            /* none of the game's: no depth asked about */
    g_pass.tex = 0;
    g_pass.cls = PC_BLOOM;
    g_pass.live = g_pass.drew = g_pass.ccol = g_pass.cdep = 1;   /* cleared, then the game's draw */
    g_pass.area = (long) w * h;
    g_pf_bloom = 1;
    g_pf.draws[PC_BLOOM]++;
    g_pf.elems[PC_BLOOM] += count;
}

void shim_gpu_pass_bloom_end(void) {
    if (!pass_on()) return;
    pass_close();
    if (!g_bloom_have) return;
    g_pass = g_bloom_saved;
    g_pass.open = 1;
    g_pass.live = g_pass.drew = 1;
    g_pass.ccol = g_pass.cdep = 0;      /* not cleared: the scene is loaded back */
    g_pass.area = (long) g_vw * g_vh;
    g_pf.draws[g_pass.cls]++;           /* bloom.c's composite over it */
    g_pf.elems[g_pass.cls] += 4;
}

/* gl.c, right after its blit and before it binds the game's target again */
void shim_gpu_pass_blit_done(void) {
    if (pass_on()) pass_close();
}

void shim_gpu_pass_bind(unsigned target, unsigned fb) {
    if (target != GL_FRAMEBUFFER_ && target != GL_DRAW_FRAMEBUFFER_) return;
    g_fb_draw = fb;
    if (!pass_on()) return;
    g_pf.binds++;
    fb_learn(fb);
    pass_retarget();
}

/*
 * The colour slot is followed even between frames and with no report open,
 * so the first frame that counts already knows what each framebuffer holds.
 */
void shim_gpu_pass_attach(unsigned target, unsigned att, unsigned tex) {
    if ((target != GL_FRAMEBUFFER_ && target != GL_DRAW_FRAMEBUFFER_) || att != GL_COLOR_ATTACHMENT0_) return;
    unsigned fb = g_fb_draw;
    int same = fb < FBS && g_fb_color[fb] == tex;
    if (fb < FBS) g_fb_color[fb] = tex;
    if (!pass_on()) return;
    g_pf.attaches++;
    if (same) g_pf.reattaches++;
    pass_retarget();
}

void shim_gpu_pass_rb(unsigned target, unsigned att, unsigned rb) {
    (void) target;
    if (att != GL_COLOR_ATTACHMENT0_ || !rb) return;
    if (g_fb_draw && g_fb_draw < FBS) { g_fb_color[g_fb_draw] = 0; g_screen_fb = g_fb_draw; }
}

void shim_gpu_pass_clear(unsigned mask) {
    if (!pass_on()) return;
    g_pf.clears++;
    if (!g_pass.open) pass_retarget();
    if (g_pass.drew) { g_pf.late_clears++; return; }
    g_pass.live = 1;
    if (mask & GL_COLOR_BUFFER_BIT) g_pass.ccol = 1;
    if (mask & GL_DEPTH_BUFFER_BIT) g_pass.cdep = 1;
}

void shim_gpu_pass_draw(int count) {
    if (!pass_on()) return;
    if (!g_pass.open) pass_retarget();
    if (shim_gpu_bloom_bound()) g_pf_bloom = 1;
    g_pf.draws[g_pass.cls]++;
    g_pf.elems[g_pass.cls] += count;
    if (!g_pass.drew) { g_pass.drew = 1; g_pass.live = 1; g_pass.area = (long) g_vw * g_vh; }
}

void shim_gpu_pass_status(void) { if (pass_on()) g_pf.statuses++; }

/* shim.c's numbering: 2 glTexSubImage2D, 3 glFinish, 4 glFlush, 5 glReadPixels */
void shim_gpu_pass_glw(int which) {
    if (!pass_on()) return;
    if (which == 2) g_pf.texsub++;
    else if (which == 3) g_pf.finish++;
    else if (which == 4) g_pf.flush++;
    else if (which == 5) g_pf.readpix++;
}

/* an upload into a texture that already has storage is a re-specification */
void shim_gpu_pass_teximage(unsigned target, int level) {
    if (!g_gpu_on) return;
    int was = 0;
    if (target == GL_TEXTURE_2D && level == 0) {
        GLint name = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &name);
        if (name > 0 && name < 65536) {
            was = (g_specified[name >> 3] >> (name & 7)) & 1;
            g_specified[name >> 3] |= (unsigned char) (1u << (name & 7));
        }
    }
    if (!pass_on()) return;
    g_pf.teximg++;
    if (was) g_pf.respec++;
}

void shim_gpu_pass_deleted(int n, const unsigned *names) {
    for (int i = 0; names && i < n; i++)
        if (names[i] < 65536) g_specified[names[i] >> 3] &= (unsigned char) ~(1u << (names[i] & 7));
}

/* gl.c, just before its blit: the screen pass ends and ours begins */
void shim_gpu_pass_blit(int w, int h) {
    if (!pass_on()) return;
    pass_close();
    g_pass.open = 1;
    g_pass.fb = 0;
    g_pass.tex = 0;
    g_pass.cls = PC_BLIT;
    g_pass.live = g_pass.drew = g_pass.ccol = g_pass.cdep = 1;   /* it writes every pixel */
    g_pass.area = (long) w * h;
    g_pf.draws[PC_BLIT]++;
}

/* --------------------------------------------------- the frame (gpu.c above) */

void shim_gpu_passes_open(void) {
    if (g_ts_on < 0) {
        for (int i = 0; i < FBS; i++) g_fb_depth[i] = -1;
        ts_setup();
    }
    if (g_last_path >= 0) {                     /* the swap that ended the frame before */
        g_pb[g_last_path].swap_ms += (double) g_gl_swap_ns / 1e6;
        g_pb[g_last_path].swaps++;
        g_last_path = -1;
    }
    ts_harvest();
    if (g_ts_cur >= 0) { g_ts[g_ts_cur].state = 0; g_ts_cur = -1; }   /* a frame that never ended */
    memset(&g_pf, 0, sizeof(g_pf));
    g_pf_open = 1;
    g_pf_bloom = 0;
    g_pass.open = 0;
    GLint fb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
    g_fb_draw = (unsigned) fb;
    g_pf_cpu0 = gl_cpu_now();
    if (g_ts_on > 0)
        for (int i = 0; i < TS_SLOTS; i++)
            if (g_ts[i].state == 0) {
                g_ts_cur = i;
                g_ts[i].n = 0; g_ts[i].over = 0; g_ts[i].state = 1; g_ts[i].frame = g_frame;
                break;
            }
    ts_stamp(-1);                               /* the frame starts */
    g_probe = (g_frame % PROBE_EVERY) == PROBE_EVERY / 2;
    if (g_probe) { glFinish(); g_probe_t = gl_wall_now(); g_probe_frame = g_frame; }   /* the frame before is out of the way */
    pass_retarget();
}

void shim_gpu_passes_close(void) {
    if (!g_pf_open) return;
    pass_close();
    g_pf_open = 0;
    int path = g_pf_bloom ? 2 : g_pf.passes[PC_HQX4] > 0 ? 1 : 0;
    if (g_ts_cur >= 0) { g_ts[g_ts_cur].state = 2; g_ts[g_ts_cur].path = path; g_ts_cur = -1; }
    long long cpu = gl_cpu_now() - g_pf_cpu0;
    pb_t *b = &g_pb[path];
    int cw = 0, ch = 0, shot = shim_screen_scene_scale(&cw, &ch);
    int ceiling = shim_screen_shot_ceiling_get();      /* the Filter is off for good (loader.c filter_gone) */
    if (b->frames && (b->shot != shot || b->ceiling != ceiling)) b->mixed = 1;
    b->shot = shot; b->ceiling = ceiling;
    b->frames++;
    b->binds += g_pf.binds; b->same += g_pf.same; b->empty += g_pf.empty;
    b->attaches += g_pf.attaches; b->reattaches += g_pf.reattaches;
    b->statuses += g_pf.statuses; b->clears += g_pf.clears; b->late_clears += g_pf.late_clears;
    for (int c = 0; c < PC_N; c++) {
        b->passes[c] += g_pf.passes[c]; b->draws[c] += g_pf.draws[c];
        b->loads[c] += g_pf.loads[c]; b->dloads[c] += g_pf.dloads[c];
        b->mpx[c] += g_pf.mpx[c]; b->load_mpx[c] += g_pf.load_mpx[c];
        b->elems[c] += g_pf.elems[c];
    }
    b->teximg += g_pf.teximg; b->respec += g_pf.respec; b->texsub += g_pf.texsub;
    b->readpix += g_pf.readpix; b->finish += g_pf.finish; b->flush += g_pf.flush;
    if (cpu > 0) b->gl_cpu_ms += (double) cpu / 1e6;
    if (g_probe) {
        g_probe = 0;
        b->fin_frames++;
        for (int c = 0; c < PC_N; c++) {
            if (g_pf.fin_ms[c] <= 0.0) continue;
            b->fin_ms[c] += g_pf.fin_ms[c];
            b->fin_in[c]++;
            b->fin_sum_ms += g_pf.fin_ms[c];
        }
    }
    g_last_path = path;
}

/* beside the frames line (gl.c): both roads over the window just closed */
void shim_gpu_passes_window(void) {
    static const char ROAD[3] = { 'A', 'B', 'G' };
    for (int p = 0; p < 3; p++) {
        pb_t *b = &g_pb[p];
        if (!b->frames) continue;
        double n = (double) b->frames;
        const char *road = p == 2 ? "G (bloom on, either road)" : p ? "B (an HQX4 pass)" : "A (no HQX4 pass)";
        char line[1100];
        snprintf(line, sizeof(line),
                 "road %s, %ld frames, shot x%d, ceiling %d%s | a frame: binds %.1f"
                 " (same target %.1f, nothing drawn %.1f), attaches %.1f (same texture %.1f),"
                 " status checks %.1f, clears %.1f (after drawing %.1f) | uploads %.2f"
                 " (re-specified %.2f), sub-uploads %.2f, readbacks %.2f, finish %.2f, flush %.2f"
                 " | in GL calls %.2f ms, swap %.2f ms",
                 road, b->frames, b->shot, b->ceiling, b->mixed ? " (mixed)" : "",
                 b->binds / n, b->same / n, b->empty / n, b->attaches / n, b->reattaches / n,
                 b->statuses / n, b->clears / n, b->late_clears / n,
                 b->teximg / n, b->respec / n, b->texsub / n, b->readpix / n, b->finish / n,
                 b->flush / n, b->gl_cpu_ms / n, b->swaps ? b->swap_ms / (double) b->swaps : 0.0);
        report_line("passes", line);

        /*
         * "elements" is what the draws handed GL to read out of the game's own
         * memory (it keeps no vertex buffers, shim.c): indices for the batches,
         * vertices for glDrawArrays - the volume the driver copies per frame.
         */
        int at = snprintf(line, sizeof(line), "road %c by target, a frame:", ROAD[p]);
        for (int c = 0; c < PC_N && at < (int) sizeof(line) - 1; c++) {
            if (!b->passes[c] && !b->draws[c]) continue;
            at += snprintf(line + at, sizeof(line) - (size_t) at,
                           " %s %.2f pass(es), %.1f draws, %.0f elements, %.2f Mpx, loaded %.2f (%.2f Mpx),"
                           " depth loaded %.2f;",
                           PC_NAME[c], b->passes[c] / n, b->draws[c] / n, b->elems[c] / n, b->mpx[c] / n,
                           b->loads[c] / n, b->load_mpx[c] / n, b->dloads[c] / n);
        }
        report_line("passes", line);

        /*
         * A kind of target is averaged over the timed frames it APPEARED in
         * ("in N"), not over all of them: bloom runs for sixty frames of a
         * minute and an average over the minute would hide it.
         */
        if (g_ts_on > 0 && b->gpu_frames) {
            double g = (double) b->gpu_frames;
            at = snprintf(line, sizeof(line),
                          "road %c GPU by target over %ld frames (%ld dropped; %ld of %ld stamps lost or"
                          " out of order, their time given to the pass after):",
                          ROAD[p], b->gpu_frames, b->gpu_lost, b->gpu_bad, b->gpu_stamps);
            for (int c = 0; c < PC_N && at < (int) sizeof(line) - 1; c++)
                if (b->gpu_in[c])
                    at += snprintf(line + at, sizeof(line) - (size_t) at, " %s %.2f ms in %ld,",
                                   PC_NAME[c], b->gpu_ms[c] / (double) b->gpu_in[c], b->gpu_in[c]);
            if (at < (int) sizeof(line) - 1)
                at += snprintf(line + at, sizeof(line) - (size_t) at, " the frame %.2f ms",
                               b->gpu_sum_ms / g);
        } else {
            at = snprintf(line, sizeof(line), "road %c GPU by target: not timed (%s)", ROAD[p],
                          g_ts_on > 0 ? (b->gpu_lost ? "every frame lost its stamps" : "no frame finished yet")
                                      : "no timestamps on this driver");
        }
        report_line("passes", line);

        /* the probe frames: the card's time by target, glFinish at every pass's end */
        if (b->fin_frames) {
            at = snprintf(line, sizeof(line), "road %c card time by target, glFinish-timed over %ld probe frames"
                          " (1 in %d, slow on purpose):", ROAD[p], b->fin_frames, PROBE_EVERY);
            for (int c = 0; c < PC_N && at < (int) sizeof(line) - 1; c++)
                if (b->fin_in[c])
                    at += snprintf(line + at, sizeof(line) - (size_t) at, " %s %.2f ms in %ld,",
                                   PC_NAME[c], b->fin_ms[c] / (double) b->fin_in[c], b->fin_in[c]);
            if (at < (int) sizeof(line) - 1)
                at += snprintf(line + at, sizeof(line) - (size_t) at, " the frame %.2f ms",
                               b->fin_sum_ms / (double) b->fin_frames);
        } else {
            at = snprintf(line, sizeof(line), "road %c card time by target: no probe frame yet", ROAD[p]);
        }
        if (at < (int) sizeof(line) - 1) {
            at += snprintf(line + at, sizeof(line) - (size_t) at, " | depth bits:");
            for (int f = 1; f < FBS && at < (int) sizeof(line) - 1; f++)
                if (g_fb_known[f])
                    at += snprintf(line + at, sizeof(line) - (size_t) at, " fb%d %d%s", f, g_fb_depth[f],
                                   (unsigned) f == g_screen_fb ? " (screen)" : "");
        }
        report_line("passes", line);
    }
    memset(g_pb, 0, sizeof(g_pb));
}
#endif /* ISAAC_DIAG */
