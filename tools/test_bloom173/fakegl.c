/*
 * A fake GL for the desk (tools/test_bloom173): a state machine holding exactly
 * the state bloom.c and gpu.c touch, a log of the draws with the state they
 * ran in, and a fake GPU clock behind the timer queries. Nothing is drawn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES3/gl3.h>
#include "fakegl.h"

fake_gl_t G;

static int next_name = 100;

void fake_reset(void) {
    memset(&G, 0, sizeof(G));
    G.active = GL_TEXTURE0;
    G.fb_complete = 1;
    next_name = 100;
}

void fake_snapshot(fake_state_t *s) {
    memcpy(s->vp, G.vp, sizeof(s->vp));
    memcpy(s->clear, G.clear, sizeof(s->clear));
    s->blend = G.blend; s->scissor = G.scissor; s->depth = G.depth; s->stencil = G.stencil; s->cull = G.cull;
    s->program = G.program; s->vao = G.vao; s->abuf = G.abuf; s->active = G.active;
    memcpy(s->tex, G.tex, sizeof(s->tex));
    s->draw_fb = G.draw_fb; s->read_fb = G.read_fb;
}

int fake_same(const fake_state_t *a, const fake_state_t *b, char *why, size_t room) {
#define D(cond, name) if (cond) { snprintf(why, room, "%s differs", name); return 0; }
    D(memcmp(a->vp, b->vp, sizeof(a->vp)), "viewport");
    D(memcmp(a->clear, b->clear, sizeof(a->clear)), "clear colour");
    D(a->blend != b->blend, "blend"); D(a->scissor != b->scissor, "scissor");
    D(a->depth != b->depth, "depth test"); D(a->stencil != b->stencil, "stencil test");
    D(a->cull != b->cull, "face culling"); D(a->program != b->program, "program");
    D(a->vao != b->vao, "vertex array"); D(a->abuf != b->abuf, "array buffer");
    D(a->active != b->active, "active texture unit");
    D(memcmp(a->tex, b->tex, sizeof(a->tex)), "texture bindings");
    D(a->draw_fb != b->draw_fb, "draw framebuffer"); D(a->read_fb != b->read_fb, "read framebuffer");
#undef D
    return 1;
}

static void log_draw(int kind, int count) {
    if (G.ndraws >= FAKE_DRAWS) return;
    fake_draw_t *d = &G.draws[G.ndraws++];
    d->kind = kind; d->count = count;
    d->fb = G.draw_fb; memcpy(d->vp, G.vp, sizeof(d->vp));
    d->blend = G.blend; d->scissor = G.scissor; d->depth = G.depth; d->program = G.program;
    d->vao = G.vao; d->tex0 = G.tex[0];
}

/* the game's own glDrawElements, as shim.c hands it to bloom.c */
void fake_game_draw(unsigned mode, int count, unsigned type, const void *idx) {
    (void) mode; (void) type; (void) idx;
    log_draw(0, count);
}

/* ---------------------------------------------------------------- GL */

GLuint glCreateShader(GLenum t) { (void) t; return (GLuint) next_name++; }
void glShaderSource(GLuint s, GLsizei n, const char *const *str, const GLint *len) { (void) s; (void) n; (void) str; (void) len; }
void glCompileShader(GLuint s) { (void) s; }
void glGetShaderiv(GLuint s, GLenum p, GLint *v) { (void) s; if (p == GL_COMPILE_STATUS) *v = 1; }
void glDeleteShader(GLuint s) { (void) s; }
GLuint glCreateProgram(void) { GLuint p = (GLuint) next_name++; G.is_program[p % FAKE_NAMES] = 1; return p; }
GLboolean glIsProgram(GLuint p) { return p && G.is_program[p % FAKE_NAMES]; }
GLboolean glIsTexture(GLuint t) { return t && G.is_texture[t % FAKE_NAMES]; }
GLboolean glIsFramebuffer(GLuint f) { return f && G.is_fb[f % FAKE_NAMES]; }
GLboolean glIsVertexArray(GLuint v) { return v && G.is_vao[v % FAKE_NAMES]; }
void glAttachShader(GLuint p, GLuint s) { (void) p; (void) s; }
void glLinkProgram(GLuint p) { (void) p; }
void glGetProgramiv(GLuint p, GLenum q, GLint *v) { (void) p; if (q == GL_LINK_STATUS) *v = 1; }
void glDeleteProgram(GLuint p) { G.is_program[p % FAKE_NAMES] = 0; }
void glUseProgram(GLuint p) { G.program = p; }
void glUniform1i(GLint l, GLint v) { (void) l; (void) v; }
GLint glGetUniformLocation(GLuint p, const char *n) { (void) p; (void) n; return 0; }
void glGenVertexArrays(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) { o[i] = (GLuint) next_name++; G.is_vao[o[i] % FAKE_NAMES] = 1; } }
void glBindVertexArray(GLuint v) { G.vao = v; }
void glGenBuffers(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) o[i] = (GLuint) next_name++; }
void glBindBuffer(GLenum t, GLuint b) { if (t == GL_ARRAY_BUFFER) G.abuf = b; }
void glBufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u) { (void) t; (void) n; (void) d; (void) u; }
void glEnableVertexAttribArray(GLuint i) { (void) i; }
void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void *p) { (void) i; (void) s; (void) t; (void) n; (void) st; (void) p; }
void glGenTextures(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) { o[i] = (GLuint) next_name++; G.is_texture[o[i] % FAKE_NAMES] = 1; } }
void glActiveTexture(GLenum u) { G.active = u; }
void glBindTexture(GLenum t, GLuint x) { if (t == GL_TEXTURE_2D) G.tex[(G.active - GL_TEXTURE0) & 7] = x; }
void glTexImage2D(GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *d) {
    (void) t; (void) l; (void) i; (void) b; (void) f; (void) ty; (void) d;
    G.teximages++; G.last_tex_w = w; G.last_tex_h = h;
}
void glTexParameteri(GLenum t, GLenum p, GLint v) { (void) t; (void) p; (void) v; }
void glGenFramebuffers(GLsizei n, GLuint *o) { for (int i = 0; i < n; i++) { o[i] = (GLuint) next_name++; G.is_fb[o[i] % FAKE_NAMES] = 1; } }
void glBindFramebuffer(GLenum t, GLuint f) {
    if (t == GL_FRAMEBUFFER || t == GL_DRAW_FRAMEBUFFER) G.draw_fb = f;
    if (t == GL_FRAMEBUFFER || t == GL_READ_FRAMEBUFFER) G.read_fb = f;
}
void glFramebufferTexture2D(GLenum t, GLenum a, GLenum tt, GLuint tex, GLint l) {
    (void) t; (void) tt; (void) l;
    if (a == GL_COLOR_ATTACHMENT0) G.attached[G.draw_fb % FAKE_NAMES] = tex;
}
GLenum glCheckFramebufferStatus(GLenum t) { (void) t; return G.fb_complete ? GL_FRAMEBUFFER_COMPLETE : 0x8CD6; }
void glGetFramebufferAttachmentParameteriv(GLenum t, GLenum a, GLenum p, GLint *v) {
    (void) t;
    if (a == GL_COLOR_ATTACHMENT0 && p == 0x8CD0) *v = G.draw_fb == G.screen_fb ? GL_RENDERBUFFER : GL_TEXTURE;
    else if (a == GL_COLOR_ATTACHMENT0 && p == 0x8CD1) *v = (GLint) G.attached[G.draw_fb % FAKE_NAMES];
    else *v = 0;                        /* no depth: iOS gives the game none either */
}
static GLboolean *flag(GLenum c) {
    switch (c) {
    case GL_BLEND: return &G.blend;
    case GL_SCISSOR_TEST: return &G.scissor;
    case GL_DEPTH_TEST: return &G.depth;
    case GL_STENCIL_TEST: return &G.stencil;
    case GL_CULL_FACE: return &G.cull;
    default: return NULL;
    }
}
GLboolean glIsEnabled(GLenum c) { GLboolean *f = flag(c); return f ? *f : 0; }
void glEnable(GLenum c) { GLboolean *f = flag(c); if (f) *f = 1; }
void glDisable(GLenum c) { GLboolean *f = flag(c); if (f) *f = 0; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { G.vp[0] = x; G.vp[1] = y; G.vp[2] = w; G.vp[3] = h; }
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { G.clear[0] = r; G.clear[1] = g; G.clear[2] = b; G.clear[3] = a; }
void glClear(GLbitfield m) { (void) m; G.clears++; G.last_clear_fb = G.draw_fb; }
void glDrawArrays(GLenum m, GLint f, GLsizei n) { (void) m; (void) f; log_draw(1, n); }
void glGetIntegerv(GLenum p, GLint *v) {
    switch (p) {
    case GL_VIEWPORT: memcpy(v, G.vp, sizeof(G.vp)); break;
    case GL_ACTIVE_TEXTURE: *v = (GLint) G.active; break;
    case GL_TEXTURE_BINDING_2D: *v = (GLint) G.tex[(G.active - GL_TEXTURE0) & 7]; break;
    case GL_DRAW_FRAMEBUFFER_BINDING: *v = (GLint) G.draw_fb; break;
    case GL_READ_FRAMEBUFFER_BINDING: *v = (GLint) G.read_fb; break;
    case GL_VERTEX_ARRAY_BINDING: *v = (GLint) G.vao; break;
    case GL_ARRAY_BUFFER_BINDING: *v = (GLint) G.abuf; break;
    case GL_CURRENT_PROGRAM: *v = (GLint) G.program; break;
    case 0x8FBB: *v = 0; break;         /* GPU_DISJOINT_EXT */
    default: *v = 0;
    }
}
void glGetFloatv(GLenum p, GLfloat *v) { if (p == GL_COLOR_CLEAR_VALUE) memcpy(v, G.clear, sizeof(G.clear)); }
const GLubyte *glGetString(GLenum p) {
    return (const GLubyte *) (p == GL_EXTENSIONS ? "GL_EXT_disjoint_timer_query" : "fake desk GPU");
}

/* ------------------------------------------------- timer queries (EXT) */

static void q_gen(int n, unsigned *o) { for (int i = 0; i < n; i++) o[i] = (unsigned) next_name++; }
/* the frame's elapsed-time query: begun at a frame's start, ended before its swap */
static unsigned q_active;
static long long q_began;
static void q_begin(unsigned t, unsigned q) { (void) t; q_active = q; q_began = G.gpu_now; G.q[q % FAKE_NAMES].written = 0; }
static void q_end(unsigned t) {
    (void) t;
    fake_query_t *r = &G.q[q_active % FAKE_NAMES];
    r->written = 1; r->value = 0; r->elapsed = G.gpu_now - q_began;
    if (!r->elapsed) r->elapsed = 1;
}
static void q_counter(unsigned q, unsigned t) {
    (void) t;
    fake_query_t *r = &G.q[q % FAKE_NAMES];
    r->elapsed = 0;
    if (G.lose_next || (G.lose_rate > 0.0 && rand() < (int) (G.lose_rate * RAND_MAX))) {
        G.lose_next = 0; r->written = 0; r->value = 0;
    }
    else { r->written = 1; r->value = G.gpu_now; }
    if (G.nstamps < FAKE_STAMPS) {
        G.stamps[G.nstamps].fb = G.draw_fb;
        G.stamps[G.nstamps].tex = G.attached[G.draw_fb % FAKE_NAMES];
        G.stamps[G.nstamps].q = q;
        G.nstamps++;
    }
}
static void q_geti(unsigned q, unsigned p, int *v) {
    if (p == 0x8867) *v = G.q[q % FAKE_NAMES].written;      /* QUERY_RESULT_AVAILABLE */
    else *v = 0;
}
static void q_get64(unsigned q, unsigned p, long long *v) {
    (void) p;
    fake_query_t *r = &G.q[q % FAKE_NAMES];
    /* an elapsed-time query reads as the frame's span; a stamp as its value (0 if never written) */
    *v = r->elapsed ? r->elapsed : r->value;
}
static void q_getiv(unsigned t, unsigned p, int *v) { (void) t; (void) p; *v = 64; }
static void get_shaderiv(unsigned s, unsigned p, int *v) { (void) s; *v = p == 0x8B4F ? GL_FRAGMENT_SHADER : 0; }

void *shim_android_symbol(const char *name) {
    if (!strcmp(name, "glGenQueriesEXT")) return (void *) q_gen;
    if (!strcmp(name, "glBeginQueryEXT")) return (void *) q_begin;
    if (!strcmp(name, "glEndQueryEXT")) return (void *) q_end;
    if (!strcmp(name, "glGetQueryObjectivEXT")) return (void *) q_geti;
    if (!strcmp(name, "glGetQueryObjecti64vEXT")) return (void *) q_get64;
    if (!strcmp(name, "glQueryCounterEXT")) return (void *) q_counter;
    if (!strcmp(name, "glGetQueryivEXT")) return (void *) q_getiv;
    if (!strcmp(name, "glGetShaderiv")) return (void *) get_shaderiv;
    return NULL;
}

/* -------------------------------------------------------------- EGL */

#include <EGL/egl.h>
void *eglGetProcAddress(const char *n) { (void) n; return NULL; }
EGLDisplay eglGetCurrentDisplay(void) { return EGL_NO_DISPLAY; }
EGLSurface eglGetCurrentSurface(EGLint w) { (void) w; return EGL_NO_SURFACE; }
unsigned eglQuerySurface(EGLDisplay d, EGLSurface s, EGLint a, EGLint *v) { (void) d; (void) s; (void) a; *v = 0; return 0; }
