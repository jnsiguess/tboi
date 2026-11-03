/*
 * Desk test of bloom.c (08.10.2026): the redirect's rule, what it draws and
 * where, and that every piece of GL state the game left is found as it was.
 * bloom.c is compiled as it is, against the fake GL in fakegl.c. Built twice by
 * run.py: ISAAC_DIAG=0 (the build we play on) and =1 (the report build).
 */
#include <stdio.h>
#include <string.h>
#include "fakegl.h"
#define tell bloom_tell        /* the MSVC runtime has a tell() of its own */
#include "bloom.c"

/* ---------------------------------------------------- the port around it */
static int t_bound = 1, t_shot = 6, t_cw = 585, t_ch = 270;
int shim_gpu_bloom_bound(void) { return t_bound; }
int shim_screen_scene_scale(int *cw, int *ch) { *cw = t_cw; *ch = t_ch; return t_shot; }
int shim_report_active(void) { return 1; }
static char t_lines[16][256];
static int t_nlines;
void report_line(const char *kind, const char *text) {
    if (t_nlines < 16) snprintf(t_lines[t_nlines++], 256, "%s: %s", kind, text);
}
#if ISAAC_DIAG
static int t_begin, t_end, t_begin_w, t_begin_h;
void shim_gpu_pass_bloom_begin(int w, int h, int count) { (void) count; t_begin++; t_begin_w = w; t_begin_h = h; }
void shim_gpu_pass_bloom_end(void) { t_end++; }
#endif

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the state the game is in when it issues the composite, over the enlarged scene */
static void game_state(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 7);          /* the game's one framebuffer */
    glViewport(0, 0, w, h);
    glEnable(GL_BLEND); glEnable(GL_SCISSOR_TEST); glEnable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST); glEnable(GL_CULL_FACE);
    glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
    glUseProgram(17);                               /* the bloom program */
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);   /* client arrays: both kept at zero */
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 33);   /* the bloom surface */
    glActiveTexture(GL_TEXTURE0 + 2); glBindTexture(GL_TEXTURE_2D, 44);
}

static void predicate(void) {
    printf("== the rule (bloom_engages)\n");
    GLint vp[4];
    struct { int shot, cw, ch, x, y, w, h, want; const char *what; } T[] = {
        { 6, 585, 270, 0, 0, 3510, 1620, 1, "1440p FULLSCREEN on, x6" },
        { 4, 585, 270, 0, 0, 2340, 1080, 1, "1080p, x4" },
        { 6, 480, 270, 0, 0, 2880, 1620, 1, "FULLSCREEN off, x6" },
        { 3, 600, 270, 0, 0, 1800, 810, 1, "1600x720, x3" },
        { 2, 585, 270, 0, 0, 1170, 540, 1, "LOW, x2" },
        { 1, 585, 270, 0, 0, 585, 270, 0, "x1: Filter / ORIGINAL - the canvas already" },
        { 6, 585, 270, 0, 0, 585, 270, 0, "another effect on: canvas-sized target" },
        { 6, 585, 270, 280, 0, 3510, 1620, 0, "offset target: not the scene surface" },
        { 6, 585, 270, 0, 0, 3509, 1620, 0, "one pixel off" },
        { 6, 585, 270, 0, 0, 3120, 1440, 0, "the window, not the scene" },
        { 6, 0, 0, 0, 0, 0, 0, 0, "no canvas yet" },
        { 0, 585, 270, 0, 0, 0, 0, 0, "no multiplier" },
    };
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        vp[0] = T[i].x; vp[1] = T[i].y; vp[2] = T[i].w; vp[3] = T[i].h;
        int got = bloom_engages(T[i].shot, T[i].cw, T[i].ch, vp);
        CHECK(got == T[i].want, "%s: %d, want %d", T[i].what, got, T[i].want);
    }
}

static void redirect(void) {
    printf("== the redirect, x6 over 585 x 270\n");
    fake_reset(); t_nlines = 0;
    t_bound = 1; t_shot = 6; t_cw = 585; t_ch = 270;
    game_state(3510, 1620);
    fake_state_t before, after;
    fake_snapshot(&before);
    int took = shim_bloom_draw(fake_game_draw, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *) 0x1000);
    fake_snapshot(&after);
    char why[64] = "";
    CHECK(took == 1, "the draw was not taken");
    CHECK(fake_same(&before, &after, why, sizeof(why)), "state not put back: %s", why);
    CHECK(G.ndraws == 2, "%d draws, want the game's and the composite", G.ndraws);
    if (G.ndraws == 2) {
        fake_draw_t *g = &G.draws[0], *c = &G.draws[1];
        CHECK(g->kind == 0 && g->count == 6, "first is not the game's own draw");
        CHECK(g->fb != 7 && g->fb != 0, "the game's draw went to framebuffer %u, not ours", g->fb);
        CHECK(g->vp[0] == 0 && g->vp[1] == 0 && g->vp[2] == 585 && g->vp[3] == 270,
              "the game's draw ran at %d,%d %dx%d, not the canvas", g->vp[0], g->vp[1], g->vp[2], g->vp[3]);
        CHECK(!g->blend && !g->scissor, "the canvas pass blended or scissored");
        CHECK(g->program == 17 && g->tex0 == 33, "the game's draw lost its program or texture");
        CHECK(c->kind == 1 && c->fb == 7, "the composite did not go into the game's target");
        CHECK(c->vp[2] == 3510 && c->vp[3] == 1620, "the composite at %dx%d", c->vp[2], c->vp[3]);
        CHECK(c->blend && c->scissor, "the composite lost the game's blending or scissor");
        CHECK(!c->depth, "the composite ran with the depth test on");
        CHECK(c->program != 17, "the composite ran the bloom shader again");
        CHECK(c->tex0 != 33, "the composite read the game's surface, not ours");
    }
    CHECK(G.last_tex_w == 585 && G.last_tex_h == 270, "our buffer is %dx%d", G.last_tex_w, G.last_tex_h);
#if ISAAC_DIAG
    CHECK(t_begin == 1 && t_end == 1 && t_begin_w == 585 && t_begin_h == 270,
          "the pass probe heard begin %d end %d at %dx%d", t_begin, t_end, t_begin_w, t_begin_h);
#endif

    printf("== a second frame: nothing remade\n");
    int images = G.teximages;
    G.ndraws = 0;
    game_state(3510, 1620);
    took = shim_bloom_draw(fake_game_draw, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *) 0x1000);
    CHECK(took == 1 && G.teximages == images, "taken %d, %d uploads", took, G.teximages - images);

    printf("== the canvas changes (600 x 270, x3): the buffer follows\n");
    t_cw = 600; t_shot = 3; G.ndraws = 0;
    game_state(1800, 810);
    took = shim_bloom_draw(fake_game_draw, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *) 0x1000);
    CHECK(took == 1 && G.last_tex_w == 600 && G.last_tex_h == 270, "buffer %dx%d", G.last_tex_w, G.last_tex_h);
    CHECK(G.ndraws == 2 && G.draws[0].vp[2] == 600, "the game's draw at width %d", G.draws[0].vp[2]);
}

static void left_alone(void) {
    printf("== cases left to the game: no call made, nothing changed\n");
    struct { int bound, shot, w, h; const char *what; } T[] = {
        { 0, 6, 3510, 1620, "another program" },
        { 1, 1, 585, 270, "x1" },
        { 1, 6, 585, 270, "into a canvas-sized effect surface" },
        { 1, 6, 3120, 1440, "a viewport that is not the scene" },
    };
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        fake_reset(); t_nlines = 0;
        t_bound = T[i].bound; t_shot = T[i].shot; t_cw = 585; t_ch = 270;
        game_state(T[i].w, T[i].h);
        fake_state_t before, after;
        fake_snapshot(&before);
        int took = shim_bloom_draw(fake_game_draw, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *) 0x1000);
        fake_snapshot(&after);
        char why[64] = "";
        CHECK(took == 0, "%s: taken", T[i].what);
        CHECK(G.ndraws == 0, "%s: %d draws made", T[i].what, G.ndraws);
        CHECK(fake_same(&before, &after, why, sizeof(why)), "%s: %s", T[i].what, why);
#if ISAAC_DIAG
        if (T[i].bound) {
            CHECK(t_nlines == 1, "%s: %d report lines", T[i].what, t_nlines);
            if (t_nlines) printf("    %s\n", t_lines[0]);
        }
#else
        CHECK(t_nlines == 0, "%s: the play build reported", T[i].what);
#endif
    }

    printf("== a buffer that cannot be made: the game draws it, for good, state intact\n");
    /* a fresh bloom.c state is needed: the earlier tests built the buffer already */
    g_prog = 0; g_tex = 0; g_fbo = 0; g_vao = 0; g_vbo = 0; g_w = g_h = 0; g_broken = 0;
    fake_reset(); t_bound = 1; t_shot = 6; t_cw = 585; t_ch = 270;
    G.fb_complete = 0;
    game_state(3510, 1620);
    fake_state_t before, after;
    fake_snapshot(&before);
    int took = shim_bloom_draw(fake_game_draw, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *) 0x1000);
    fake_snapshot(&after);
    char why[64] = "";
    CHECK(took == 0 && g_broken == 1, "taken %d, broken %d", took, g_broken);
    CHECK(fake_same(&before, &after, why, sizeof(why)), "state: %s", why);
    G.fb_complete = 1;
    took = shim_bloom_draw(fake_game_draw, GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *) 0x1000);
    CHECK(took == 0, "taken after being marked broken");
}

int main(void) {
    printf("bloom.c on the desk, ISAAC_DIAG=%d\n", ISAAC_DIAG);
    predicate();
    redirect();
    left_alone();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
