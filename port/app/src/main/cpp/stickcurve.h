/*
 * THE MOVEMENT STICK'S OUTPUT: THE DEAD ZONE, THE SENSITIVITY AND DIGITAL
 * (plan item 4.6, batch 3). Pure arithmetic, nothing of the game's memory - so
 * the very same code runs in the stick's update (sticks.c, shim_stick_value)
 * and on the desk (tools/stickmodel.py keeps a copy in Python; a host build of
 * this header runs stick_curve_selftest as it is).
 *
 * Where it sits: the stick's own update (0x862f04) takes the deflection d =
 * finger (+0x178) less grab (+0x180) and divides it by +0x1c4 (0x8bedbc, two
 * fdiv) at 0x863048. That one division becomes this. After it the game's own
 * rules run untouched: the cap at one (0x863060..0x863078), DIGITAL's "under
 * 0.85 is nought, else full" (0x8630a8..0x8630d0, 0x9464fc) and the axis zone
 * +0x1cc.
 *
 *   ANALOGUE   m = |d| / travel. Inside the zone (m <= dz) an exact +0.0f on
 *              both axes; outside it d/|d| * min((m - dz) / (s - dz), 1), where
 *              s = +0x1c4 / travel is MOVE SENS. It grows from nought at the
 *              zone's edge - no step - and is full at s. With dz = 0 it is the
 *              game's own d / +0x1c4 (the native road is taken then, bit for
 *              bit).
 *   DIGITAL    the stick is either still or at full pull, and the one thing to
 *              choose is where it starts: DEAD ZONE (the owner's variant B).
 *              On past dz, off only once back under dz - gap: two thresholds,
 *              so a thumb trembling on the line does not flicker run - stop -
 *              run. MOVE SENS is ANALOGUE's alone. The unit vector we give is
 *              past the game's 0.85, so its own rule makes it full.
 *
 * An exact +0 is what the game gets for "nothing": its getter 0x861c64 gives
 * +0 to all four directions from it (fcsel mi / fmaxnm), the movement vector
 * 0x515294 is their difference - 0 as well - and the overlay reads the getter
 * without asking whether the stick is held (0x8611a8, 0x8611c4). The native
 * DIGITAL holds a stick at exactly this zero (0x8630c4, the vector 0x95e7a0)
 * every time a thumb rests under 85%.
 */
#ifndef ISAAC_STICKCURVE_H
#define ISAAC_STICKCURVE_H

#include <math.h>

typedef struct { float x, y; } stick_v2;

enum { STICK_ANALOGUE = 1, STICK_DIGITAL = 2, STICK_BUTTONS = 3 };   /* Manager+0x55f18 */

/*
 * DIGITAL's gap between on and off, a share of the travel. Tuned on
 * tools/stickmodel.py with a thumb held on the line - a 9 Hz tremor of 0.5 to
 * 3 px, 0.6 px of sensor noise, a slow drift, at 5.33 px a unit and a travel
 * of 20 units: extra starts a minute at gap 0.04 / 0.05 / 0.06 are 5.6 / 0.8 /
 * 0 for 1.5 px of tremor and 82 / 16 / 2 for 3 px. 0.06 is the end of the
 * plan's range that still holds at 3 px, and it is 6 px to come back - the
 * skeptic's 0.12 was twice what the tremor needs.
 */
#define STICK_DIGITAL_GAP 0.06f

/* the game's own division, bit for bit: 0x8bedbc is two fdiv by one scalar */
static inline stick_v2 stick_native(const float *d, float div) {
    stick_v2 r = { d[0] / div, d[1] / div };
    return r;
}

static inline stick_v2 stick_curve_analogue(float dx, float dy, float travel, float sens, float dz) {
    stick_v2 r = { 0.0f, 0.0f };
    float len = sqrtf(dx * dx + dy * dy);
    float m = len / travel;
    if (!(m > dz) || !(len > 0.0f)) return r;           /* the zone, and a NaN too: +0, +0 */
    float k = sens > dz ? (m - dz) / (sens - dz) : 1.0f;
    if (k > 1.0f) k = 1.0f;
    r.x = dx / len * k;
    r.y = dy / len * k;
    return r;
}

/* *on is the stick's switch, kept by the caller for as long as the stick is held */
static inline stick_v2 stick_curve_digital(float dx, float dy, float travel, float dz, float gap, int *on) {
    stick_v2 r = { 0.0f, 0.0f };
    float len = sqrtf(dx * dx + dy * dy);
    float m = len / travel;
    float off = dz - gap;
    if (off < 0.0f) off = 0.0f;
    if (*on) { if (!(m > off)) *on = 0; }
    else if (m > dz) *on = 1;
    if (!*on || !(len > 0.0f)) return r;
    r.x = dx / len;
    r.y = dy / len;
    return r;
}

/*
 * The whole decision. `zoned` is "the movement stick, and no double tap of the
 * port's is open for the touch player" - the caller knows which stick it is
 * and reads the sanction. The native road, the game's division as it is:
 *   - not zoned (the shooting stick has no zone at all, the owner's decision
 *     20; an open sanction wants the pull exactly as with zone 0);
 *   - BUTTONS (the D-pads put the finger on their points themselves);
 *   - ANALOGUE with the zone at 0 (the curve IS the division then);
 *   - anything that is not a number a stick can have.
 * DIGITAL keeps its threshold even at 0: the slider moves it monotonically
 * down to "any touch runs", rather than jumping back to the game's 85% of
 * MOVE SENS at the very bottom.
 */
static inline stick_v2 stick_output(const float *d, float div, float travel, float dz, int mode,
                                    int zoned, int *on) {
    if (!zoned || mode == STICK_BUTTONS || !(travel > 0.0f) || !(div > 0.0f) || !(dz >= 0.0f)
        || !isfinite(d[0]) || !isfinite(d[1]))
        return stick_native(d, div);
    if (mode == STICK_DIGITAL) return stick_curve_digital(d[0], d[1], travel, dz, STICK_DIGITAL_GAP, on);
    if (!(dz > 0.0f)) return stick_native(d, div);
    return stick_curve_analogue(d[0], d[1], travel, div / travel, dz);
}

/*
 * THE SELF-TEST (report builds and the desk): 0 when every property holds,
 * else the number of failures, the first described in `why`.
 */
#include <stdio.h>
#include <string.h>

static inline unsigned stick_bits(float f) { unsigned u; memcpy(&u, &f, 4); return u; }

static inline int stick_curve_selftest(char *why, size_t room) {
    int fails = 0;
    if (room) why[0] = 0;
#define STICK_FAIL(...) do { if (!fails++ && room) snprintf(why, room, __VA_ARGS__); } while (0)
    const float travel = 20.0f;                          /* 16 x 125% */
    const float sens[] = { 0.1f, 0.3f, 0.55f, 0.8f, 1.0f };
    const float zones[] = { 0.0f, 0.05f, 0.25f, 0.5f, 0.9f };
    const float dirs[][2] = { {1, 0}, {0, -1}, {-0.6f, 0.8f}, {0.28f, -0.96f}, {-1, 0} };
    for (unsigned si = 0; si < sizeof(sens) / sizeof(sens[0]); si++) {
        float div = travel * sens[si];
        /* 1. zone 0 in ANALOGUE, the shooting stick, BUTTONS: the game's division to the bit */
        for (unsigned di = 0; di < sizeof(dirs) / sizeof(dirs[0]); di++)
            for (float m = 0.0f; m <= 1.6f; m += 0.0625f) {
                float d[2] = { dirs[di][0] * m * travel, dirs[di][1] * m * travel };
                stick_v2 want = stick_native(d, div);
                int on = 0;
                stick_v2 a = stick_output(d, div, travel, 0.0f, STICK_ANALOGUE, 1, &on);
                stick_v2 b = stick_output(d, div, travel, 0.25f, STICK_ANALOGUE, 0, &on);
                stick_v2 c = stick_output(d, div, travel, 0.25f, STICK_BUTTONS, 1, &on);
                if (stick_bits(a.x) != stick_bits(want.x) || stick_bits(a.y) != stick_bits(want.y)
                    || stick_bits(b.x) != stick_bits(want.x) || stick_bits(b.y) != stick_bits(want.y)
                    || stick_bits(c.x) != stick_bits(want.x) || stick_bits(c.y) != stick_bits(want.y))
                    STICK_FAIL("native road differs at s %.2f m %.4f", (double) sens[si], (double) m);
            }
        for (unsigned zi = 1; zi < sizeof(zones) / sizeof(zones[0]); zi++) {
            float dz = zones[zi];
            for (unsigned di = 0; di < sizeof(dirs) / sizeof(dirs[0]); di++) {
                float ux = dirs[di][0], uy = dirs[di][1];
                /* 2. inside the zone: the bits of +0.0f on both axes, whatever the sign of d
                      (the edge itself is left out: a rounding either way is a hair either way) */
                for (int k = 0; k < 16; k++) {
                    float m = dz * (float) k / 16.0f;
                    float d[2] = { ux * m * travel, uy * m * travel };
                    int on = 0;
                    stick_v2 r = stick_output(d, div, travel, dz, STICK_ANALOGUE, 1, &on);
                    if (stick_bits(r.x) || stick_bits(r.y))
                        STICK_FAIL("inside the zone %08x %08x at dz %.2f m %.4f", stick_bits(r.x),
                                   stick_bits(r.y), (double) dz, (double) m);
                }
                {
                    float neg[2] = { -0.0f, -0.0f };
                    int on = 0;
                    stick_v2 r = stick_output(neg, div, travel, dz, STICK_ANALOGUE, 1, &on);
                    if (stick_bits(r.x) || stick_bits(r.y)) STICK_FAIL("-0 gives %08x %08x", stick_bits(r.x), stick_bits(r.y));
                }
                /* 3. no step at the edge, full at s, never past one, never falling */
                float prev = 0.0f;
                for (float m = dz; m <= 1.6f; m += 0.001f) {
                    float d[2] = { ux * m * travel, uy * m * travel };
                    int on = 0;
                    stick_v2 r = stick_output(d, div, travel, dz, STICK_ANALOGUE, 1, &on);
                    float mag = sqrtf(r.x * r.x + r.y * r.y);
                    if (mag > 1.0f + 1e-6f) STICK_FAIL("past one: %.7f at dz %.2f m %.4f", (double) mag, (double) dz, (double) m);
                    if (mag + 1e-6f < prev) STICK_FAIL("falls at dz %.2f m %.4f", (double) dz, (double) m);
                    if (m < dz + 0.0015f && sens[si] > dz + 0.01f && mag > 0.0015f / (sens[si] - dz) + 1e-4f)
                        STICK_FAIL("a step at the zone's edge: %.5f at dz %.2f", (double) mag, (double) dz);
                    if (mag > 0.0f && (r.x * ux + r.y * uy) < mag * 0.9999f)
                        STICK_FAIL("turned off its direction at dz %.2f m %.4f", (double) dz, (double) m);
                    prev = mag;
                }
                {
                    float m = sens[si] > dz ? sens[si] : dz + 0.001f;
                    float d[2] = { ux * m * travel * 1.00001f, uy * m * travel * 1.00001f };
                    int on = 0;
                    stick_v2 r = stick_output(d, div, travel, dz, STICK_ANALOGUE, 1, &on);
                    float mag = sqrtf(r.x * r.x + r.y * r.y);
                    if (fabsf(mag - 1.0f) > 1e-5f) STICK_FAIL("not full at s: %.6f (s %.2f dz %.2f)", (double) mag,
                                                             (double) sens[si], (double) dz);
                }
            }
        }
    }
    /* 4. DIGITAL: on past dz, held down to dz - gap, off under it; a unit vector or +0 */
    {
        const float dz = 0.25f, off = dz - STICK_DIGITAL_GAP;
        const float path[] = { 0.0f, off, dz - 0.001f, dz + 0.001f, off + 0.02f, off + 0.001f, dz,
                               off - 0.001f, off, dz - 0.01f, dz + 0.01f, 1.5f, 0.0f };
        const int want[]   = { 0,    0,   0,           1,           1,           1,            1,
                               0,            0,   0,           1,           1,    0 };
        int on = 0;
        for (unsigned k = 0; k < sizeof(path) / sizeof(path[0]); k++) {
            float d[2] = { -0.6f * path[k] * travel, 0.8f * path[k] * travel };
            stick_v2 r = stick_output(d, travel * 0.8f, travel, dz, STICK_DIGITAL, 1, &on);
            float mag = sqrtf(r.x * r.x + r.y * r.y);
            int is_on = mag > 0.5f;
            if (is_on != want[k] || (is_on && fabsf(mag - 1.0f) > 1e-6f) || (!is_on && (stick_bits(r.x) || stick_bits(r.y))))
                STICK_FAIL("DIGITAL step %u (m %.3f): %s, magnitude %.6f", k, (double) path[k],
                           is_on ? "on" : "off", (double) mag);
            if (is_on && mag < 0.85f) STICK_FAIL("DIGITAL on under the game's 0.85");
        }
        /* the zone at 0: any pull runs, the middle itself stands */
        on = 0;
        float d0[2] = { 0.0f, 0.0f }, d1[2] = { 0.02f, 0.0f };
        stick_v2 r0 = stick_output(d0, 16.0f, travel, 0.0f, STICK_DIGITAL, 1, &on);
        stick_v2 r1 = stick_output(d1, 16.0f, travel, 0.0f, STICK_DIGITAL, 1, &on);
        if (stick_bits(r0.x) || stick_bits(r0.y) || !(r1.x > 0.99f))
            STICK_FAIL("DIGITAL at zone 0: middle %.3f, a hair %.3f", (double) r0.x, (double) r1.x);
    }
#undef STICK_FAIL
    return fails;
}

#endif
