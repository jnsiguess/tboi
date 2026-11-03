/*
 * Message dispatch: the parts that cannot be written in C.
 *
 * objc_msgSend must leave the arguments untouched and jump into the method it
 * found rather than call it — otherwise the method returns to the wrong place.
 * So the argument registers have to be saved, the address looked up, the
 * registers restored and the jump taken. Eight integer, eight floating-point
 * and x8 for an indirect return.
 */
    .text

/* ---------- retain variants named after a register ---------- */

/*
 * Apple's compiler calls objc_retain_x19 instead of objc_retain when the
 * object already sits in x19: that saves shuffling it through x0. Argument and
 * result share the same register, so with an identity retain and an empty
 * release all of them are simply a return. This cannot be written in C: there
 * is no way to say "do nothing with x19" there.
 */
    .globl shim_objc_ret_noop
    .align 2
shim_objc_ret_noop:
    ret

/* ---------- objc_msgSend ---------- */

    .globl shim_objc_msgSend
    .align 4
shim_objc_msgSend:
    cbz     x0, Lnil                    /* a message to nil is silent and yields zero */
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    sub     sp, sp, #240
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    str     x8, [sp, #64]
    stp     x9, x10, [sp, #208]     /* needed when taking tables apart */
    stp     x11, x12, [sp, #224]
    stp     q0, q1, [sp, #80]
    stp     q2, q3, [sp, #112]
    stp     q4, q5, [sp, #144]
    stp     q6, q7, [sp, #176]

    bl      shim_objc_lookup            /* x0 = self, x1 = sel are already in place */
    mov     x16, x0

    ldp     q6, q7, [sp, #176]
    ldp     q4, q5, [sp, #144]
    ldp     q2, q3, [sp, #112]
    ldp     q0, q1, [sp, #80]
    ldp     x11, x12, [sp, #224]
    ldp     x9, x10, [sp, #208]
    ldr     x8, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #240
    ldp     x29, x30, [sp], #16

    cbz     x16, Lnil                   /* no such method — silently zero */
    br      x16
Lnil:
    /*
     * A message to nothing answers with nothing - and Apple means all of it:
     * x0 and x1 for a pair of words, d0 to d3 for a small structure of floats.
     * Clearing x0 alone left the rest of an answer from the call before.
     */
    mov     x0, #0
    mov     x1, #0
    fmov    d0, xzr
    fmov    d1, xzr
    fmov    d2, xzr
    fmov    d3, xzr
    ret

/* ---------- objc_msgSendSuper2 ---------- */

/*
 * Here the first argument is a {receiver, class} pair, and the search must
 * start from the superclass. The substitution happens after the lookup: the
 * method must receive the receiver, not the pair itself.
 */
    .globl shim_objc_msgSendSuper2
    .align 4
shim_objc_msgSendSuper2:
    cbz     x0, Lnil2
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    sub     sp, sp, #208
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    str     x8, [sp, #64]
    stp     q0, q1, [sp, #80]
    stp     q2, q3, [sp, #112]
    stp     q4, q5, [sp, #144]
    stp     q6, q7, [sp, #176]

    bl      shim_objc_lookup_super      /* x0 = the pair, x1 = sel */
    mov     x16, x0

    ldp     q6, q7, [sp, #176]
    ldp     q4, q5, [sp, #144]
    ldp     q2, q3, [sp, #112]
    ldp     q0, q1, [sp, #80]
    ldr     x8, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #208
    ldp     x29, x30, [sp], #16

    ldr     x0, [x0]                    /* swap the pair for the receiver */
    cbz     x16, Lnil2
    br      x16
Lnil2:
    mov     x0, #0
    ret

/* ---------- variadic arguments: Apple's convention ---------- */

/*
 * On arm64 Apple puts ALL variadic arguments on the stack, while Android puts
 * them in x2..x7. The game is built for iOS, so its sprintf(dst, fmt, ...)
 * leaves the arguments at [sp], and Android's ordinary sprintf cannot see
 * them.
 *
 * Here we catch the pointer to them: on entry sp is still untouched and points
 * exactly at the first argument. The rest of the work is done in C.
 */
    .globl shim_sprintf
    .align 2
shim_sprintf:
    mov     x2, sp
    b       shim_sprintf_impl

    .globl shim_snprintf
    .align 2
shim_snprintf:
    mov     x3, sp
    b       shim_snprintf_impl

    .globl shim_fprintf
    .align 2
shim_fprintf:
    mov     x2, sp
    b       shim_fprintf_impl

    .globl shim_sscanf
    .align 2
shim_sscanf:
    mov     x2, sp
    b       shim_sscanf_impl

/*
 * +[NSString stringWithFormat:] is variadic as well. The method is reached
 * through shim_objc_msgSend, and that one restores sp before the jump, so here
 * it points exactly at the caller's arguments, just as it does for sprintf.
 */
    .globl shim_str_with_format_abi
    .align 2
shim_str_with_format_abi:
    mov     x3, sp
    b       shim_str_with_format

/*
 * Hooking the game's own logger: Log(level, format, ...). Apple-style, the
 * arguments sit on the stack, so we catch sp on entry.
 */
    .globl shim_game_log
    .align 2
shim_game_log:
    mov     x2, sp
    b       shim_game_log_impl

/* ---------- entry point of a hook that continues ---------- */

/*
 * The slot index arrives in x17 — put there by the jump written over the start
 * of the function. The arguments must reach the original untouched, so we save
 * everything, ask for the continuation address and jump there.
 */
    .globl shim_trace_entry
    .align 4
shim_trace_entry:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    sub     sp, sp, #208
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    str     x8, [sp, #64]
    /* the caller's own return address, still in x30 here: index 9 of the
       saved array, in the spare eight bytes before q0. Without it a hook
       cannot say WHO called it, and that was the whole question about the
       temporary effects (13.09.2026). */
    str     x30, [sp, #72]
    stp     q0, q1, [sp, #80]
    stp     q2, q3, [sp, #112]
    stp     q4, q5, [sp, #144]
    stp     q6, q7, [sp, #176]

    mov     x7, sp                      /* pointer to the saved registers */
    mov     x6, x5                      /* shift the arguments: room for the index */
    mov     x5, x4                      /* top down, or we trample a neighbour */
    mov     x4, x3
    mov     x3, x2
    mov     x2, x1
    mov     x1, x0
    mov     x0, x17
    bl      shim_trace_hit
    mov     x16, x0

    ldp     q6, q7, [sp, #176]
    ldp     q4, q5, [sp, #144]
    ldp     q2, q3, [sp, #112]
    ldp     q0, q1, [sp, #80]
    ldr     x8, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #208
    ldp     x29, x30, [sp], #16

    cbz     x16, Ltrace_ret
    br      x16
Ltrace_ret:
    ret

/* ---------- results wider than sixteen bytes ---------- */

/*
 * By the arm64 convention the room for such a result arrives in x8. That
 * cannot be written in C, so here the address is moved into a spare argument
 * register and an ordinary function does the work.
 */
    .globl shim_str_to_string_abi
    .align 2
shim_str_to_string_abi:
    mov     x1, x8
    b       shim_str_to_string

    .globl shim_str_concat_cs_abi
    .align 2
shim_str_concat_cs_abi:
    mov     x2, x8
    b       shim_str_concat_cs

    .globl shim_strbuf_str_get_abi
    .align 2
shim_strbuf_str_get_abi:
    mov     x1, x8
    b       shim_strbuf_str_get

/*
 * getloc hands back a locale, and a locale is a class with a destructor, so the
 * caller passes the room for it in x8 rather than taking it in x0. C cannot say
 * that; two instructions can.
 */
    .globl shim_ios_getloc_abi
    .align 2
shim_ios_getloc_abi:
    mov     x1, x0
    mov     x0, x8
    b       shim_ios_getloc

/* ---------- retain named after a register ---------- */

/*
 * There used to be a plain return here, and that was a mistake. Apple's
 * compiler calls objc_retain_xN when the object already sits in xN, but it
 * takes the RESULT from x0. You can see it right in the game's code: straight
 * after the call it stores x0 into a field. With an empty return, self landed
 * there, and the touch handler received the controller itself instead of the
 * set of fingers.
 */
    .globl shim_objc_retain_x1
    .align 2
shim_objc_retain_x1:
    mov     x0, x1
    ret

    .globl shim_objc_retain_x2
    .align 2
shim_objc_retain_x2:
    mov     x0, x2
    ret

    .globl shim_objc_retain_x8
    .align 2
shim_objc_retain_x8:
    mov     x0, x8
    ret

    .globl shim_objc_retain_x19
    .align 2
shim_objc_retain_x19:
    mov     x0, x19
    ret

    .globl shim_objc_retain_x20
    .align 2
shim_objc_retain_x20:
    mov     x0, x20
    ret

    .globl shim_objc_retain_x21
    .align 2
shim_objc_retain_x21:
    mov     x0, x21
    ret

    .globl shim_objc_retain_x23
    .align 2
shim_objc_retain_x23:
    mov     x0, x23
    ret

    .globl shim_objc_retain_x24
    .align 2
shim_objc_retain_x24:
    mov     x0, x24
    ret
