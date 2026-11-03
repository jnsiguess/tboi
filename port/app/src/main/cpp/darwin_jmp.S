/*
 * setjmp / longjmp with Apple's jmp_buf, for the game's libpng (02.10.2026).
 *
 * The image imports exactly two of these names, `_setjmp` and `_longjmp` (the
 * C functions setjmp and longjmp; Apple's `__setjmp` would be C's _setjmp), and
 * calls them from five places, all libpng 1.0.2 built into the game:
 * setjmp at 0x8a6200 (the game's picture loader) and 0x8d3518
 * (png_create_read_struct), longjmp at 0x8a6780, 0x8a67b8 (the loader's own
 * refusals) and 0x8ca2d0 (png_default_error).
 *
 * Why they cannot be Android's. libpng keeps its jmp_buf at the very start of
 * png_struct and the error handler pointer right after it: error_fn at +0xc0
 * (png_set_error_fn, 0x8ca488: str x2, [x0, #0xc0]). Apple's arm64 jmp_buf is
 * int[48], 192 bytes, so it ends exactly there. Bionic's is long[32] and its
 * setjmp writes a checksum of the registers into word 24 — str x12, [x0, #0xc0]
 * in setjmp.o of the NDK r27 libc.a — which is error_fn. Every png_error
 * (0x8ca280: ldr x8, [x0, #0xc0]; cbz x8; blr x8) then jumps to the checksum.
 * On iOS and the PC the same broken picture is "libpng error: ...", a longjmp,
 * "Error loading png: ..." in the game's log and a missing picture.
 *
 * The layout is Apple's (libplatform, setjmp/arm64/setjmp.s), so nothing at or
 * beyond +0xc0 is ever touched:
 *
 *   0x00 x19 x20   0x10 x21 x22   0x20 x23 x24   0x30 x25 x26   0x40 x27 x28
 *   0x50 fp  lr    0x60 sp  (0x68 reserved, untouched)
 *   0x70 d8  d9    0x80 d10 d11   0x90 d12 d13   0xa0 d14 d15
 *   0xb0 signal mask (8 bytes: Android's sigset_t on arm64)
 *   0xb8 Apple's sigsetjmp flag — not written, setjmp does not use it
 *
 * Like Apple's setjmp (and Bionic's), the signal mask is saved and restored:
 * sigprocmask reads it into +0xb0 and longjmp puts it back. Apple's pointer
 * mangling of fp/lr/sp is a hardening detail nobody else reads; left out.
 */

#define JB_X19      0x00
#define JB_X21      0x10
#define JB_X23      0x20
#define JB_X25      0x30
#define JB_X27      0x40
#define JB_FP       0x50
#define JB_SP       0x60
#define JB_D8       0x70
#define JB_D10      0x80
#define JB_D12      0x90
#define JB_D14      0xa0
#define JB_SIGMASK  0xb0

#define LINUX_SIG_SETMASK  2
#define LINUX_SIG_BLOCK    0

    .text

/* int setjmp(jmp_buf) — saves the signal mask, then the registers */
    .globl shim_darwin_setjmp
    .type shim_darwin_setjmp, %function
    .align 4
shim_darwin_setjmp:
    stp     x21, x30, [sp, #-16]!
    mov     x21, x0
    mov     x0, #LINUX_SIG_BLOCK        /* with a null set this only reads the mask */
    mov     x1, #0
    add     x2, x21, #JB_SIGMASK
    bl      sigprocmask
    mov     x0, x21
    ldp     x21, x30, [sp], #16         /* sp and lr are the caller's again */
    /* falls through */

/* int _setjmp(jmp_buf) — the registers only */
    .globl shim_darwin__setjmp
    .type shim_darwin__setjmp, %function
shim_darwin__setjmp:
    stp     x19, x20, [x0, #JB_X19]
    stp     x21, x22, [x0, #JB_X21]
    stp     x23, x24, [x0, #JB_X23]
    stp     x25, x26, [x0, #JB_X25]
    stp     x27, x28, [x0, #JB_X27]
    stp     x29, x30, [x0, #JB_FP]
    mov     x16, sp
    str     x16, [x0, #JB_SP]
    stp     d8, d9, [x0, #JB_D8]
    stp     d10, d11, [x0, #JB_D10]
    stp     d12, d13, [x0, #JB_D12]
    stp     d14, d15, [x0, #JB_D14]
    mov     w0, #0
    ret
    .size shim_darwin_setjmp, . - shim_darwin_setjmp

/* void longjmp(jmp_buf, int) — puts the signal mask back, then the registers */
    .globl shim_darwin_longjmp
    .type shim_darwin_longjmp, %function
    .align 4
shim_darwin_longjmp:
    mov     x19, x0                     /* x19, x20 come back from the buffer anyway */
    mov     w20, w1
    mov     x0, #LINUX_SIG_SETMASK
    add     x1, x19, #JB_SIGMASK
    mov     x2, #0
    bl      sigprocmask
    mov     x0, x19
    mov     w1, w20
    /* falls through */

/* void _longjmp(jmp_buf, int) — the registers only */
    .globl shim_darwin__longjmp
    .type shim_darwin__longjmp, %function
shim_darwin__longjmp:
    ldp     x19, x20, [x0, #JB_X19]
    ldp     x21, x22, [x0, #JB_X21]
    ldp     x23, x24, [x0, #JB_X23]
    ldp     x25, x26, [x0, #JB_X25]
    ldp     x27, x28, [x0, #JB_X27]
    ldp     x29, x30, [x0, #JB_FP]
    ldr     x16, [x0, #JB_SP]
    mov     sp, x16
    ldp     d8, d9, [x0, #JB_D8]
    ldp     d10, d11, [x0, #JB_D10]
    ldp     d12, d13, [x0, #JB_D12]
    ldp     d14, d15, [x0, #JB_D14]
    cmp     w1, #0
    csinc   w0, w1, wzr, ne             /* longjmp(b, 0) returns 1, as the standard says */
    ret
    .size shim_darwin_longjmp, . - shim_darwin_longjmp

/*
 * The round trip, measured (startup self-test of the report build).
 *
 * int shim_darwin_jmp_probe(void *buf): puts known values into x19..x28 and
 * d8..d15, calls setjmp(buf), wipes every one of them, moves the stack pointer,
 * calls longjmp(buf, 0) and checks on the second return that all of them, the
 * stack pointer and the frame pointer are back and that setjmp returned 1.
 * Returns 0 when everything held, otherwise a bit per failure: 1 the return
 * value, 2 the x registers, 4 the d registers, 8 sp or fp. The caller's own
 * callee-saved registers are kept in this frame and given back.
 */
    .globl shim_darwin_jmp_probe
    .type shim_darwin_jmp_probe, %function
    .align 4
shim_darwin_jmp_probe:
    stp     x29, x30, [sp, #-176]!
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    stp     x25, x26, [sp, #64]
    stp     x27, x28, [sp, #80]
    stp     d8, d9, [sp, #96]
    stp     d10, d11, [sp, #112]
    stp     d12, d13, [sp, #128]
    stp     d14, d15, [sp, #144]
    str     x0, [sp, #160]

    /* known values: 0x5A5A0000_000000nn in x19..x28, 0xD0D0... + n in d8..d15 */
    movz    x9, #0x5a5a, lsl #48
    add     x19, x9, #19
    add     x20, x9, #20
    add     x21, x9, #21
    add     x22, x9, #22
    add     x23, x9, #23
    add     x24, x9, #24
    add     x25, x9, #25
    add     x26, x9, #26
    add     x27, x9, #27
    add     x28, x9, #28
    movz    x10, #0xd0d0, lsl #48
    add     x11, x10, #8
    fmov    d8, x11
    add     x11, x10, #9
    fmov    d9, x11
    add     x11, x10, #10
    fmov    d10, x11
    add     x11, x10, #11
    fmov    d11, x11
    add     x11, x10, #12
    fmov    d12, x11
    add     x11, x10, #13
    fmov    d13, x11
    add     x11, x10, #14
    fmov    d14, x11
    add     x11, x10, #15
    fmov    d15, x11

    ldr     x0, [x29, #160]
    bl      shim_darwin_setjmp
    cbnz    w0, 1f

    /* first return: wipe everything the buffer has to bring back */
    ldr     x9, [x29, #160]
    mov     x19, #0
    mov     x20, #0
    mov     x21, #0
    mov     x22, #0
    mov     x23, #0
    mov     x24, #0
    mov     x25, #0
    mov     x26, #0
    mov     x27, #0
    mov     x28, #0
    fmov    d8, xzr
    fmov    d9, xzr
    fmov    d10, xzr
    fmov    d11, xzr
    fmov    d12, xzr
    fmov    d13, xzr
    fmov    d14, xzr
    fmov    d15, xzr
    mov     x29, #0
    sub     sp, sp, #64                 /* longjmp must take sp back up */
    mov     x0, x9
    mov     w1, #0                      /* 0 must come back as 1 */
    bl      shim_darwin_longjmp
    brk     #0x5a                       /* longjmp does not return here */

1:  mov     w12, #0
    cmp     w0, #1
    b.eq    2f
    orr     w12, w12, #1
2:  movz    x9, #0x5a5a, lsl #48
    mov     x13, #0                     /* the x registers: any difference ends up here */
    add     x15, x9, #19
    eor     x14, x19, x15
    orr     x13, x13, x14
    add     x15, x9, #20
    eor     x14, x20, x15
    orr     x13, x13, x14
    add     x15, x9, #21
    eor     x14, x21, x15
    orr     x13, x13, x14
    add     x15, x9, #22
    eor     x14, x22, x15
    orr     x13, x13, x14
    add     x15, x9, #23
    eor     x14, x23, x15
    orr     x13, x13, x14
    add     x15, x9, #24
    eor     x14, x24, x15
    orr     x13, x13, x14
    add     x15, x9, #25
    eor     x14, x25, x15
    orr     x13, x13, x14
    add     x15, x9, #26
    eor     x14, x26, x15
    orr     x13, x13, x14
    add     x15, x9, #27
    eor     x14, x27, x15
    orr     x13, x13, x14
    add     x15, x9, #28
    eor     x14, x28, x15
    orr     x13, x13, x14
    cbz     x13, 3f
    orr     w12, w12, #2
3:  movz    x10, #0xd0d0, lsl #48
    mov     x13, #0                     /* the d registers */
    fmov    x14, d8
    add     x15, x10, #8
    eor     x14, x14, x15
    orr     x13, x13, x14
    fmov    x14, d9
    add     x15, x10, #9
    eor     x14, x14, x15
    orr     x13, x13, x14
    fmov    x14, d10
    add     x15, x10, #10
    eor     x14, x14, x15
    orr     x13, x13, x14
    fmov    x14, d11
    add     x15, x10, #11
    eor     x14, x14, x15
    orr     x13, x13, x14
    fmov    x14, d12
    add     x15, x10, #12
    eor     x14, x14, x15
    orr     x13, x13, x14
    fmov    x14, d13
    add     x15, x10, #13
    eor     x14, x14, x15
    orr     x13, x13, x14
    fmov    x14, d14
    add     x15, x10, #14
    eor     x14, x14, x15
    orr     x13, x13, x14
    fmov    x14, d15
    add     x15, x10, #15
    eor     x14, x14, x15
    orr     x13, x13, x14
    cbz     x13, 4f
    orr     w12, w12, #4
4:  mov     x14, sp                     /* the frame is this function's own again */
    cmp     x14, x29
    b.ne    5f
    ldr     x15, [x29, #160]            /* reachable only through the right fp */
    cbnz    x15, 6f
5:  orr     w12, w12, #8
    mov     sp, x29                     /* best effort: fp is what the epilogue needs */
6:  mov     w0, w12
    ldp     x19, x20, [sp, #16]
    ldp     x21, x22, [sp, #32]
    ldp     x23, x24, [sp, #48]
    ldp     x25, x26, [sp, #64]
    ldp     x27, x28, [sp, #80]
    ldp     d8, d9, [sp, #96]
    ldp     d10, d11, [sp, #112]
    ldp     d12, d13, [sp, #128]
    ldp     d14, d15, [sp, #144]
    ldp     x29, x30, [sp], #176
    ret
    .size shim_darwin_jmp_probe, . - shim_darwin_jmp_probe

    .section .note.GNU-stack, "", %progbits
