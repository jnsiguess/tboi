/* the fake GL's state, for the desk tests (tools/test_bloom173) */
#ifndef FAKEGL_H
#define FAKEGL_H
#include <stddef.h>

#define FAKE_NAMES 512
#define FAKE_DRAWS 64
#define FAKE_STAMPS 4096

typedef struct { int kind, count; unsigned fb; int vp[4]; unsigned char blend, scissor, depth; unsigned program, vao, tex0; } fake_draw_t;
typedef struct { int written; long long value, elapsed; } fake_query_t;
typedef struct { unsigned fb, tex, q; } fake_stamp_t;

typedef struct {
    int vp[4];
    float clear[4];
    unsigned char blend, scissor, depth, stencil, cull;
    unsigned program, vao, abuf, active, tex[8], draw_fb, read_fb;
    unsigned char is_program[FAKE_NAMES], is_texture[FAKE_NAMES], is_fb[FAKE_NAMES], is_vao[FAKE_NAMES];
    unsigned attached[FAKE_NAMES];
    unsigned screen_fb;
    int fb_complete;
    int teximages, last_tex_w, last_tex_h, clears;
    unsigned last_clear_fb;
    fake_draw_t draws[FAKE_DRAWS];
    int ndraws;
    /* the fake GPU clock: the test moves it; a stamp takes its value unless lost */
    long long gpu_now;
    int lose_next;
    double lose_rate;               /* each stamp is never written with this chance */
    fake_query_t q[FAKE_NAMES];
    fake_stamp_t stamps[FAKE_STAMPS];
    int nstamps;
} fake_gl_t;

typedef struct {
    int vp[4];
    float clear[4];
    unsigned char blend, scissor, depth, stencil, cull;
    unsigned program, vao, abuf, active, tex[8], draw_fb, read_fb;
} fake_state_t;

extern fake_gl_t G;
void fake_reset(void);
void fake_snapshot(fake_state_t *s);
int fake_same(const fake_state_t *a, const fake_state_t *b, char *why, size_t room);
void fake_game_draw(unsigned mode, int count, unsigned type, const void *idx);
#endif
