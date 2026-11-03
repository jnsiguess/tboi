/*
 * One switch for all the diagnostics.
 *
 * Everything the port carries purely for observation — hooks inside the game's
 * code, reading pixels back from framebuffers, state snapshots taken before a
 * draw, a log line for every Objective-C message — was needed to reach a
 * playable state and will be needed again. So it is not deleted, it is turned
 * off: in the build we play on ISAAC_DIAG is zero and not one extra call per
 * frame survives.
 *
 * Stubs that FIX behaviour (binding buffer zero, our own implementations of
 * Apple's symbols) are a different matter and always run.
 */
#ifndef ISAAC_DIAG_H
#define ISAAC_DIAG_H

#ifndef ISAAC_DIAG
#define ISAAC_DIAG 1
#endif

#if ISAAC_DIAG
#define DIAG(...) do { __VA_ARGS__; } while (0)
#else
#define DIAG(...) do { } while (0)
#endif

#endif
