/*
 * The bloom composite, drawn at the size iOS draws it.
 *
 * WHAT WAS WRONG. Using any pill starts bloom for sixty frames
 * (Game::ShowBloom 0x893564; Game::Update 0x88dbf0 counts it down a frame at
 * a time). Game::Render (0x88e54c) lays the bloom surface over the scene in one
 * draw with the bloom shader, which reads the texture 49 times for every pixel
 * it covers (shaders/bloom.fs: an 8 x 6 loop plus the base read). The target
 * is whatever the frame loop pushed before Game::Render - the Color Correction
 * Surface (0x6718b0) - and that is one of the two surfaces the port draws
 * larger than the canvas (screen.c, worth_enlarging).
 *
 * On iOS that surface is the canvas, 585 x 270: 7.7 million texture reads a
 * frame. Here it is x4 on a 1080p phone and x6 on a 1440p one: 124 and 279
 * million. Measured on the owner's Adreno 830 (report, 11.09.2026): five pills,
 * five "bloom on" events, 60 frames each, the first frame 21 ms of GPU time
 * against 3.7 usual and 10 ms a frame on average. A weak GPU cannot keep sixty
 * frames of that at sixty a second - and because the effect is counted in
 * FRAMES, a slow frame makes it last longer: 165 ms a frame on a Samsung A17 is
 * ten seconds of slideshow, which is exactly what two testers' reports show.
 *
 * WHY DRAWING IT SMALL CHANGES NOTHING ON SCREEN. The bloom surface itself is
 * canvas-sized, and it keeps the game's NEAREST filter (the port turns on
 * linear filtering only for textures of exactly canvas x shot, shim.c). The
 * shader's step between reads is fixed per frame in screen pixels, not in
 * pixels of the target: at 0x88eda4 Ratio = (2, 2) / surface size / (area
 * pixels per canvas unit), written into every vertex. So on iOS the composite
 * is one fragment per texel, each at a texel centre, reading at those steps.
 * Drawing it into a canvas-sized buffer does exactly that, and laying the
 * result over the scene one texel to shot x shot pixels with NEAREST is how
 * iOS blows the whole frame up to the screen anyway. The work drops by the
 * square of the enlargement - 16 times on 1080p, 36 on 1440p.
 *
 * HOW. The game's own draw call is replayed, unchanged, into a buffer of our
 * own at the canvas size, with blending and scissor off and everything else as
 * the game left it. Then that buffer is drawn over the scene with the blending
 * the game had set for the composite - so the composite's colours meet the
 * scene through the same blend equation they always did. Every piece of GL
 * state touched is put back, because the game caches its state and must find
 * it as it left it.
 *
 * It applies only to the enlarged scene: the target has to be exactly
 * canvas x shot, starting at the origin - FULLSCREEN on or off alike (with it
 * off the scene is 480 x 270 times the multiplier, enlarged since 13.09). At
 * x1 (Filter on, ORIGINAL) and when another effect's canvas-sized surface is
 * the target, the game's draw goes through untouched: it is already the size
 * iOS draws (see bloom_engages below).
 *
 * RE-CHECKED 08.10.2026 (players after 1.73: "bloom on a pill still drops the
 * FPS, only POTATO does not"): the clean 1.73 libisaacport.so carries all of
 * it - shim_glDrawElements calls shim_bloom_draw unconditionally, and
 * shim_glShaderSource / shim_glAttachShader / shim_glUseProgram feed the
 * shader names to gpu.c with no report check in the way. Modelled over every
 * canvas and multiplier screen.c produces, the rule never leaves an enlarged
 * composite at full price (the research notes of 08.10.2026, bloom_model.py),
 * and on the desk the redirect puts every piece of state back
 * (tools/test_bloom173).
 * POTATO alone is quiet because it turns bloom OFF (EnableBloom); at x1 the
 * composite costs exactly what it costs on iOS - 49 reads a pixel of the
 * canvas - and what remains to be measured on a weak phone is that, the pass
 * the redirect splits, and the processor (the research notes of 08.10.2026,
 * bloom trisagion).
 */
#include <stdio.h>
#include <string.h>
#include <GLES3/gl3.h>
#include "diag.h"

int shim_gpu_bloom_bound(void);
int shim_screen_scene_scale(int *cw, int *ch);
int shim_report_active(void);
void report_line(const char *kind, const char *text);

static GLuint g_fbo, g_tex, g_prog, g_vao, g_vbo;
static int g_w, g_h;
static int g_broken;                        /* could not be built: the game draws it itself */

static const char *VS =
    "#version 300 es\n"
    "layout(location = 0) in vec2 p;\n"
    "out vec2 uv;\n"
    "void main() { uv = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";

static const char *FS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2 uv;\n"
    "uniform sampler2D t;\n"
    "out vec4 c;\n"
    "void main() { c = texture(t, uv); }\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    if (!s) return 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

static void tell(const char *text) {
    static int said;
    if (said < 6 && shim_report_active()) { said++; report_line("bloom", text); }
}

/* our buffer, texture, program and quad - made once, remade if the context lost them */
static int ready(int w, int h) {
    int have = g_prog && glIsProgram(g_prog) && g_tex && glIsTexture(g_tex)
               && g_fbo && glIsFramebuffer(g_fbo) && g_vao && glIsVertexArray(g_vao);
    if (have && w == g_w && h == g_h) return 1;

    GLint tex = 0, draw_fb = 0, read_fb = 0, vao = 0, abuf = 0, unit = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);

    int ok = 1;
    if (!have) {
        g_prog = 0; g_tex = 0; g_fbo = 0; g_vao = 0; g_vbo = 0;
        GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
        if (vs && fs) {
            g_prog = glCreateProgram();
            glAttachShader(g_prog, vs);
            glAttachShader(g_prog, fs);
            glLinkProgram(g_prog);
            GLint linked = 0;
            glGetProgramiv(g_prog, GL_LINK_STATUS, &linked);
            if (!linked) { glDeleteProgram(g_prog); g_prog = 0; }
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (!g_prog) ok = 0;

        if (ok) {
            GLint cur = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &cur);
            glUseProgram(g_prog);
            glUniform1i(glGetUniformLocation(g_prog, "t"), 0);
            glUseProgram((GLuint) cur);

            static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
            glGenVertexArrays(1, &g_vao);
            glBindVertexArray(g_vao);
            glGenBuffers(1, &g_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

            glGenTextures(1, &g_tex);
            glGenFramebuffers(1, &g_fbo);
        }
    }
    if (ok) {
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = 0;
        g_w = w; g_h = h;
    }

    glBindVertexArray((GLuint) vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint) abuf);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint) draw_fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint) read_fb);
    glBindTexture(GL_TEXTURE_2D, (GLuint) tex);
    glActiveTexture((GLenum) unit);

    char line[120];
    if (ok) snprintf(line, sizeof(line), "the composite is drawn at %d x %d, as on iOS", w, h);
    else snprintf(line, sizeof(line), "could not build the %d x %d buffer: the game draws the composite itself", w, h);
    tell(line);
    return ok;
}

/*
 * WHEN THE REDIRECT TAKES THE DRAW - the whole rule, apart so it can be tested
 * on the desk (tools/test_bloom173). The composite is redone small only when it
 * is drawn over the enlarged scene: a target of exactly canvas x shot from the
 * origin. Anything else is drawn by the game as it always was, and is already
 * at the size iOS draws it:
 *   shot 1 (Filter on, ORIGINAL, POTATO with bloom on) - the scene IS the canvas;
 *   with another effect on at the same time (pixelation, hallucination, Old
 *   TV) - Game::Render pushes their canvas-sized surfaces before the bloom
 *   surface (0x88e5f0..0x88e6b4) and the bloom composite lands in the top one
 *   (0x88ec20 pops back to it), not in the scene.
 * Checked against every canvas and multiplier screen.c can produce, FULLSCREEN
 * on and off (the research notes of 08.10.2026, bloom_model.py): the
 * enlarged colour correction surface is born canvas x shot from the same two
 * numbers this asks, and 0x8a6dc4 sets the viewport to exactly its size.
 */
static int bloom_engages(int shot, int cw, int ch, const GLint *vp) {
    if (shot <= 1 || cw <= 0 || ch <= 0) return 0;
    return vp[0] == 0 && vp[1] == 0 && vp[2] == cw * shot && vp[3] == ch * shot;
}

#if ISAAC_DIAG
void shim_gpu_pass_bloom_begin(int w, int h, int count);
void shim_gpu_pass_bloom_end(void);

/*
 * Report builds: every way the bloom draw was left to the game, said once per
 * kind, with the numbers - so a tester's report answers "was it redirected on
 * this phone, and if not, why" without a guess (08.10.2026).
 */
static void left_to_game(int shot, int cw, int ch, const GLint *vp) {
    if (!shim_report_active()) return;
    static struct { int shot, cw, ch, vp[4]; } seen[8];
    static int n;
    for (int i = 0; i < n; i++)
        if (seen[i].shot == shot && seen[i].cw == cw && seen[i].ch == ch && seen[i].vp[0] == vp[0]
            && seen[i].vp[1] == vp[1] && seen[i].vp[2] == vp[2] && seen[i].vp[3] == vp[3]) return;
    if (n >= 8) return;
    seen[n].shot = shot; seen[n].cw = cw; seen[n].ch = ch;
    for (int k = 0; k < 4; k++) seen[n].vp[k] = vp[k];
    n++;
    const char *why =
        shot <= 1 ? "the scene is one canvas: the size iOS draws it" :
        (vp[0] == 0 && vp[1] == 0 && vp[2] == cw && vp[3] == ch)
            ? "into a canvas-sized surface (another effect is on): the size iOS draws it" :
        "NOT the enlarged scene and not the canvas: drawn at full price";
    char line[220];
    snprintf(line, sizeof(line), "left to the game - shot x%d, canvas %dx%d, viewport %d,%d %dx%d: %s",
             shot, cw, ch, vp[0], vp[1], vp[2], vp[3], why);
    report_line("bloom", line);
}
#endif

/* the game's draw, replaced when it is the bloom composite over the enlarged scene */
int shim_bloom_draw(void (*real)(unsigned, int, unsigned, const void *),
                    unsigned mode, int count, unsigned type, const void *idx) {
    if (g_broken || !real || !shim_gpu_bloom_bound()) return 0;
    int cw = 0, ch = 0;
    int shot = shim_screen_scene_scale(&cw, &ch);
    GLint vp[4] = { 0, 0, 0, 0 };
    if (shot > 1 || ISAAC_DIAG) glGetIntegerv(GL_VIEWPORT, vp);   /* the report names it at x1 too */
    if (!bloom_engages(shot, cw, ch, vp)) {
        DIAG(left_to_game(shot, cw, ch, vp));
        return 0;
    }
    if (!ready(cw, ch)) { g_broken = 1; return 0; }

    GLint draw_fb = 0, read_fb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
    GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    GLfloat clear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);

    /* 1. the game's own draw, into the canvas-sized buffer: its colours, unblended */
    DIAG(shim_gpu_pass_bloom_begin(cw, ch, count));    /* the scene so far, stamped while it is the target */
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, cw, ch);
    if (blend) glDisable(GL_BLEND);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    real(mode, count, type, idx);
    DIAG(shim_gpu_pass_bloom_end());                   /* our canvas pass, stamped while it is the target */

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint) draw_fb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint) read_fb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    if (blend) glEnable(GL_BLEND);
    if (scissor) glEnable(GL_SCISSOR_TEST);

    /* 2. those colours over the scene, a texel to shot x shot pixels, through the game's blending */
    GLint prog = 0, vao = 0, abuf = 0, unit = 0, tex = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &unit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
    if (depth) glDisable(GL_DEPTH_TEST);
    if (stencil) glDisable(GL_STENCIL_TEST);
    if (cull) glDisable(GL_CULL_FACE);

    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindTexture(GL_TEXTURE_2D, (GLuint) tex);
    glActiveTexture((GLenum) unit);
    glBindVertexArray((GLuint) vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint) abuf);
    glUseProgram((GLuint) prog);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    return 1;
}
