/*
 * Desk test of gpu.c's pass clock (08.10.2026): "GPU by target" printed
 * hundreds of millions of milliseconds in the 1.72/1.73 reports beside a sane
 * frame total. This drives the probe the way shim.c's wrappers and gl.c do,
 * against the fake GL's clock, and checks
 *   - WHERE each stamp is issued: with the pass it closes still the target
 *     (the old gpu.c issued it after the next target was bound);
 *   - that with every stamp written the times by target are the ones put in;
 *   - that a stamp never written (read as nought) no longer turns into a
 *     timestamp of days, and is counted instead;
 *   - the bloom pass (bloom.c's canvas pass) under its own name.
 * Built twice by run.py: against gpu.c, and (-DOLD) against
 * gpu.c.before-bloom173, to show the old fault on the same frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "fakegl.h"
#ifdef OLD
#include "gpu.c.before-bloom173"
#else
#include "gpu.c"
#endif

/* ---------------------------------------------------- the port around it */
int shim_report_active(void) { return 1; }
static char t_last[4][1200];
void report_line(const char *kind, const char *text) {
    if (strcmp(kind, "passes")) return;
    memmove(t_last[0], t_last[1], sizeof(t_last[0]) * 3);
    snprintf(t_last[3], sizeof(t_last[3]), "%s: %s", kind, text);
}
static unsigned char *t_image;
unsigned long shim_image_base(void) { return (unsigned long) (unsigned long long) t_image; }
int shim_screen_scene_scale(int *cw, int *ch) { *cw = 585; *ch = 270; return 6; }
int shim_screen_filter_on(void) { return 0; }
int shim_screen_shot_ceiling_get(void) { return 0; }
long long g_gl_swap_ns, g_gl_ns_draw, g_gl_ns_clear, g_gl_ns_bindfb, g_gl_tex_ns;
long long g_glw_ns[GLW_KINDS];

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { FB_GAME = 7, FB_SCREEN = 1, FB_BLOOM = 9, TEX_SCENE = 501, TEX_FX = 503 };
#define MS 1000000LL

/* the wrappers' order, as shim.c has it */
static void w_bind(unsigned fb) {
#ifndef OLD
    shim_gpu_pass_leave_bind(GL_FRAMEBUFFER, fb);
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    shim_gpu_pass_bind(GL_FRAMEBUFFER, fb);
}
static void w_attach(unsigned tex) {
#ifndef OLD
    shim_gpu_pass_leave_attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex);
#endif
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    shim_gpu_pass_attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex);
}
static void w_viewport(int w, int h) { glViewport(0, 0, w, h); shim_gpu_viewport(w, h); }
static void w_clear(void) { shim_gpu_pass_clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }
static void w_draw(int count, long long cost) {
    shim_gpu_draw(count);
#ifdef OLD
    shim_gpu_pass_draw();
#else
    shim_gpu_pass_draw(count);
#endif
    G.gpu_now += cost;
}

/* the expected cost of every kind of target, put in per frame */
static const long long C_SCENE = 2 * MS, C_FX = MS / 4, C_SCREEN = 9 * MS / 10, C_BLIT = 15 * MS / 100,
                       C_BLOOM = 3 * MS / 10, C_COMPOSITE = 2 * MS / 10;

static int expected_class(unsigned fb, unsigned tex) {
    if (fb == FB_BLOOM) return 6;                    /* PC_BLOOM */
    if (fb == 0 || fb == FB_SCREEN) return 0;        /* PC_SCREEN (the blit's fb 0 is checked apart) */
    if (tex == TEX_SCENE) return 1;
    return 4;                                         /* PC_FX */
}

static int placed_wrong, placed_total;

/* every stamp of the frame just closed: was it issued with its own pass the target? */
static void check_placement(long frame) {
    for (int i = 0; i < TS_SLOTS; i++) {
        ts_t *t = &g_ts[i];
#ifdef OLD
        (void) frame;
        if (t->state != 2) continue;
#else
        if (t->state != 2 || t->frame != frame) continue;
#endif
        for (int k = 1; k < t->n; k++) {
            int c = t->cls[k];
            for (int s = G.nstamps - 1; s >= 0; s--) {
                if (G.stamps[s].q != t->q[k]) continue;
                int want = expected_class(G.stamps[s].fb, G.stamps[s].tex);
                if (c == 5) want = G.stamps[s].fb == 0 ? 5 : -2;   /* the blit's stamp: the window bound */
                placed_total++;
                if (want != c) placed_wrong++;
                break;
            }
        }
    }
}

static void frame(int bloom, double lose) {
    G.lose_rate = lose;
    shim_gpu_frame_open();
    G.nstamps = 0;
    w_bind(FB_GAME);
    w_attach(TEX_SCENE);
    w_viewport(3510, 1620);
    w_clear();
    for (int i = 0; i < 20; i++) w_draw(6, C_SCENE / 20);
#ifndef OLD
    if (bloom) {                                      /* bloom.c's redirect, as it calls the probe */
        shim_gpu_pass_bloom_begin(585, 270, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, FB_BLOOM);
        G.gpu_now += C_BLOOM;
        shim_gpu_pass_bloom_end();
        glBindFramebuffer(GL_FRAMEBUFFER, FB_GAME);
        G.gpu_now += C_COMPOSITE;
    }
#else
    (void) bloom;
#endif
    w_attach(TEX_FX);
    w_viewport(585, 270);
    w_clear();
    for (int i = 0; i < 5; i++) w_draw(6, C_FX / 5);
    w_bind(FB_SCREEN);
    w_viewport(3120, 1440);
    w_clear();
    for (int i = 0; i < 3; i++) w_draw(6, C_SCREEN / 3);
    /* gl.c's present */
    shim_gpu_pass_blit(3120, 1440);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    G.gpu_now += C_BLIT;
#ifndef OLD
    shim_gpu_pass_blit_done();
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, FB_SCREEN);
    shim_gpu_frame_close();
    check_placement(g_frame);
}

static double value_of(const char *line, const char *name) {
    char key[40];
    snprintf(key, sizeof(key), " %s ", name);
    const char *p = strstr(line, key);
    return p ? atof(p + strlen(key)) : -1.0;
}

static void run(const char *what, double lose, int bloom_frames) {
    printf("== %s\n", what);
    memset(t_last, 0, sizeof(t_last));
    placed_wrong = placed_total = 0;
    for (int f = 0; f < 240; f++) frame(f >= 60 && f < 60 + bloom_frames, lose);
    shim_gpu_frame_open();                            /* the last frames harvested */
    shim_gpu_frame_close();
    shim_gpu_frame_open();
    shim_gpu_frame_close();
    shim_gpu_passes_window();
    const char *gpu = NULL;
    for (int i = 0; i < 4; i++) if (strstr(t_last[i], "GPU by target")) gpu = t_last[i];
    printf("   %s\n", gpu ? gpu : "(no GPU-by-target line)");
    printf("   stamps issued with their own pass the target: %d of %d\n",
           placed_total - placed_wrong, placed_total);
    if (!gpu) { CHECK(0, "no line"); return; }
    double scene = value_of(gpu, "scene"), fx = value_of(gpu, "fx"), screen = value_of(gpu, "screen"),
           blit = value_of(gpu, "blit"), bloom = value_of(gpu, "bloom");
#ifdef OLD
    (void) scene; (void) fx; (void) screen; (void) blit; (void) bloom;
    printf("   (the old probe: shown, not judged)\n");
#else
    CHECK(placed_wrong == 0, "%d stamps issued with another target bound", placed_wrong);
    double big = 50.0;
    CHECK(scene >= 0 && scene < big && fx >= 0 && fx < big && screen >= 0 && screen < big && blit >= 0 && blit < big,
          "a time by target out of all proportion");
    if (lose == 0.0) {
        /* scene: its own 2 ms (+0.2 composite in bloom frames, averaged in); the first pass also
           carries the frame-start stamp's gap, which is nought here */
        double scene_want = (2.0 * 240 + 0.2 * bloom_frames) / 240.0;
        CHECK(scene > scene_want - 0.02 && scene < scene_want + 0.02, "scene %.3f, want %.3f", scene, scene_want);
        CHECK(fx > 0.24 && fx < 0.26, "fx %.3f, want 0.25", fx);
        CHECK(screen > 0.89 && screen < 0.91, "screen %.3f, want 0.90", screen);
        CHECK(blit > 0.14 && blit < 0.16, "blit %.3f, want 0.15", blit);
        if (bloom_frames) CHECK(bloom > 0.29 && bloom < 0.31, "bloom %.3f, want 0.30", bloom);
        CHECK(strstr(gpu, "0 of ") != NULL, "stamps counted as lost with none lost");
    }
#endif
}

int main(void) {
    /* unsigned long is 32 bits here: the fake image has to sit below 4 GB */
    t_image = VirtualAlloc((void *) 0x20000000, 0xac3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!t_image) { printf("no fake image below 4 GB\n"); return 2; }
    static unsigned char scene_surface[0x100];
    *(unsigned *) (scene_surface + 0xb8) = TEX_SCENE;
    *(unsigned char **) (t_image + 0xac2460) = scene_surface;
    fake_reset();
    G.gpu_now = 1000000000000LL;                      /* a card's clock is never at nought */
    G.screen_fb = FB_SCREEN;
    glBindFramebuffer(GL_FRAMEBUFFER, FB_SCREEN);
    shim_gpu_pass_rb(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 3);   /* the game's screen: a renderbuffer */
#ifdef OLD
    printf("the OLD pass clock (gpu.c.before-bloom173) on the desk\n");
#else
    printf("the pass clock (gpu.c) on the desk\n");
#endif
    srand(173);
    run("every stamp written, sixty bloom frames", 0.0, 60);
    run("one stamp in ten never written", 0.10, 60);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
