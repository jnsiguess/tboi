/*
 * THE LAST STRETCH: the picture onto the window, with no walking texel.
 *
 * WHAT WALKED (the owner, 13.10.2026: "when moving in big rooms the textures
 * of walls and objects walk"). The frame ends with one textured quad that lays
 * the finished scene on the window - colorcorrection.fs (0x671a78's road; the
 * Filter option's hq4x.fs road is gone with the option, hotfix 1.75 - loader.c
 * filter_gone). The window is almost never a whole
 * multiple of what that quad reads: 3120 x 1440 is 5.333 pixels to the canvas
 * unit at ORIGINAL and POTATO, 0.889 to a scene pixel at x6, 1.333 at x4. The
 * engine samples every surface NEAREST (0x8db53c/0x8db54c), so a texel came
 * out 5 pixels wide or 6 by where it landed, and as the camera crept the extra
 * pixel ran along the walls and the rocks. The enlarged scene (screen.c) made
 * the texel smaller, never whole; the LINEAR filter meant for it was never
 * applied (shim.c, shim_screen_scene_texture); and at x1 nothing smoothed at
 * all. Measured on the desk for every mode and screen: the research notes of
 * 13.10.2026, walls_model.py.
 *
 * WHAT IS DONE. The shader reads "sharp bilinear" (port_patch:
 * colorcorrection.fs): a texel is a solid block, and only the one
 * screen pixel that straddles a border is blended, by how much each side
 * covers - edges sit where they belong, to a fraction of a pixel, in every
 * frame. That needs the hardware's bilinear read, so for this one draw the
 * texture's filter is LINEAR, and it is put back as it was right after: the
 * same surface is read elsewhere (the effect passes, the 1:1 copies, the
 * HQX4 composite) and those keep exactly what the game gave them. Every mode
 * goes through here: every multiplier, POTATO, FULLSCREEN off, the menus and
 * the cutscenes - the game has no other road onto the window for the scene.
 *
 * The cost is two filter writes a frame and one bilinear read a pixel where a
 * point read was: nothing a weak card notices.
 *
 * LIGHTER (21.10.2026, the renderer audit). What the shader cost was not the
 * read but the arithmetic around it, on every window pixel in every mode: a
 * size query and the divides by it, about three times the original pass's
 * work - 2-4 ms a frame on a Mali-G57 MP1 (Tecno Spark Go 2024). The size is
 * now handed in as a uniform (PortSharp: size, 1/size), and when the stretch
 * is a whole number - the scene lands on the window k pixels to a texel, as
 * on a 1080-tall screen at x1 (4.0) - no sharpening is done at all: the read
 * is the plain one and the filter stays the game's NEAREST. A shader without
 * the uniform (a mod's own colorcorrection.fs) is handled as before.
 */
#include <stdio.h>
#include <GLES3/gl3.h>
#include "diag.h"

int shim_gpu_stretch_bound(void);
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_screen_scene_scale(int *cw, int *ch);
void *shim_android_symbol(const char *name);

static GLint g_tex, g_min, g_mag, g_unit;   /* what the draw found, to put back */
static int g_on;
static GLint g_sharp_prog = -1, g_sharp_at = -1;   /* the program the location was asked of, and PortSharp in it */

int shim_screen_stretch_whole(int vw, int vh);

#if ISAAC_DIAG
/*
 * PROBE, report builds: which texture the last stretch read, how large it is,
 * the filter the game had left on it, and onto which viewport - once for every
 * new combination, so a switch of mode or multiplier is named as it happens.
 */
static void tell(GLint tex, GLint min, GLint mag) {
    if (!shim_report_active()) return;
    static struct { int tw, th, vw, vh; } seen[16];
    static int n;
    GLint vp[4] = { 0, 0, 0, 0 }, tw = 0, th = 0, fb = 0;
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
    /* the texture's own size: glGetTexLevelParameteriv is GLES 3.1, asked for by name */
    void (*level)(GLenum, GLint, GLenum, GLint *) =
        (void (*)(GLenum, GLint, GLenum, GLint *)) shim_android_symbol("glGetTexLevelParameteriv");
    if (level) {
        level(GL_TEXTURE_2D, 0, 0x1000 /* GL_TEXTURE_WIDTH */, &tw);
        level(GL_TEXTURE_2D, 0, 0x1001 /* GL_TEXTURE_HEIGHT */, &th);
    }
    for (int i = 0; i < n; i++)
        if (seen[i].tw == tw && seen[i].th == th
            && seen[i].vw == vp[2] && seen[i].vh == vp[3]) return;
    if (n >= 16) return;
    seen[n].tw = tw; seen[n].th = th;
    seen[n].vw = vp[2]; seen[n].vh = vp[3];
    n++;
    int cw = 0, ch = 0, shot = shim_screen_scene_scale(&cw, &ch);
    char line[260];
    snprintf(line, sizeof(line),
             "last stretch: colorcorrection reads texture %d (%d x %d, scene %d x %d at x%d) onto viewport "
             "%d x %d at %d,%d of framebuffer %d - the game left it %s/%s, read LINEAR + sharp edges",
             tex, tw, th, cw * shot, ch * shot, shot,
             vp[2], vp[3], vp[0], vp[1], fb,
             min == GL_LINEAR ? "LINEAR" : min == GL_NEAREST ? "NEAREST" : "other",
             mag == GL_LINEAR ? "LINEAR" : mag == GL_NEAREST ? "NEAREST" : "other");
    report_line("screen", line);
}
#endif

/* before the game's draw: the stretch's texture is read LINEAR for this draw only */
void shim_stretch_begin(void) {
    g_on = 0;
    if (!shim_gpu_stretch_bound()) return;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &g_unit);
    if (g_unit != GL_TEXTURE0) glActiveTexture(GL_TEXTURE0);     /* Texture0 is unit 0 */
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &g_tex);
    if (g_tex) {
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &g_min);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &g_mag);
        DIAG(tell(g_tex, g_min, g_mag));
        /* LIGHTER: the size handed in, or no sharpening where the stretch is whole */
        GLint prog = 0, vp[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        if (prog != g_sharp_prog) {
            g_sharp_prog = prog;
            g_sharp_at = prog ? glGetUniformLocation((GLuint) prog, "PortSharp") : -1;
        }
        glGetIntegerv(GL_VIEWPORT, vp);
        int whole = g_sharp_at >= 0 && shim_screen_stretch_whole(vp[2], vp[3]);
        if (g_sharp_at >= 0) {
            if (whole) {
                glUniform4f(g_sharp_at, 0.0f, 0.0f, 0.0f, 0.0f);
            } else {
                static void (*level)(GLenum, GLint, GLenum, GLint *);
                static int looked;
                GLint tw = 0, th = 0;
                if (!looked) {
                    looked = 1;
                    level = (void (*)(GLenum, GLint, GLenum, GLint *)) shim_android_symbol("glGetTexLevelParameteriv");
                }
                if (level) {
                    level(GL_TEXTURE_2D, 0, 0x1000 /* GL_TEXTURE_WIDTH */, &tw);
                    level(GL_TEXTURE_2D, 0, 0x1001 /* GL_TEXTURE_HEIGHT */, &th);
                }
                if (tw > 0 && th > 0)
                    glUniform4f(g_sharp_at, (float) tw, (float) th, 1.0f / (float) tw, 1.0f / (float) th);
                else
                    glUniform4f(g_sharp_at, -1.0f, -1.0f, 0.0f, 0.0f);   /* the shader asks for the size itself */
            }
        }
        if (!whole) {
            if (g_min != GL_LINEAR) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            if (g_mag != GL_LINEAR) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            g_on = 1;
        }
    }
    if (g_unit != GL_TEXTURE0) glActiveTexture((GLenum) g_unit);
}

/* after it: the filter as the game had it, so every other read of the surface is untouched */
void shim_stretch_end(void) {
    if (!g_on) return;
    g_on = 0;
    GLint now = 0;
    if (g_unit != GL_TEXTURE0) glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &now);
    if (now == g_tex) {
        if (g_min != GL_LINEAR) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_min);
        if (g_mag != GL_LINEAR) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_mag);
    }
    if (g_unit != GL_TEXTURE0) glActiveTexture((GLenum) g_unit);
}
