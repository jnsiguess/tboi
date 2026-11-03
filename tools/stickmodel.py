# -*- coding: utf-8 -*-
"""The touch stick on the desk: the knob and the output, the old way and the new.

A model of the Repentance stick's update, copied from the image rather than
guessed (batch 3, the research notes of 30.09.2026, plan item 4.6):

  0x861b40  grab: held, +0x170 touch, +0x180 grab = the seat (fixed_sticks)
  0x862f04  update: d = finger - grab; value = d / (+0x1c4 = travel x sens),
            capped at one; DIGITAL: under 0.85 nought, else full (0x8630a8);
            knob target +0x1ac = value x travel (0x863388)
  0x863188  the knob's animation: t += 0.1 an update up to one; drawn =
            start + (target - start) x (1 + 2^(-10t) sin((t - 0.075) 2pi / 0.3))
            - easeOutElastic, constants 0x9472f4, 0x946348, 0x9473c8
  0x86314c  a lift: value and target to nought, start = the old target, t = 0
            (the movement stick, +0x1c9 = 1): the spring back

Three ways of drawing the knob are laid side by side for the owner:

  "as it was for you"  DEAD ZONE 0, MOVE SENS 0.55 - the port's zone code did
                       nothing, the game drew value x travel: 1.8 x the thumb
  "the old zone"       DEAD ZONE 0.25, MOVE SENS 0.8 (a fresh profile): the
                       finger held back inside the zone, pressed afresh outside
                       it, the knob drawn at the raw finger while held back
  "by the finger"      batch 3: the knob's target is the finger itself, cut at
                       the travel (sticks.c, shim_stick_knob)

and the output: DIGITAL's threshold (native 42.5%, the port's 68% before,
variant A 72%, variant B = DEAD ZONE 25%) and the hysteresis gap, tuned on a
trembling thumb held on the line. The curve itself is stickcurve.h's, copied.

    py -3 tools/stickmodel.py             pictures into other stuff/stickmodel/ in the project
    py -3 tools/stickmodel.py --selftest  the curve's properties, as stick_curve_selftest
    py -3 tools/stickmodel.py --capture   the capture circle against the ring, FULLSCREEN off
    py -3 tools/stickmodel.py --c         stickcurve.h itself, built for this PC (LLVM clang, no C
                                          runtime): its stick_curve_selftest, and the C curve against
                                          the Python one here
"""
import math, os, random, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'other stuff', 'stickmodel')
TRAVEL = 20.0            # 16 x 125%
RIM = 40.0               # "Analog Bounds" drawn at 125%: 64 texels x 1.25 x 0.5
KNOB_R = 20.0            # "Analog Stick" disc at 125%
PX_PER_UNIT = 5.33       # the owner's 3120 x 1440 on a 585 x 270 canvas
GAP = 0.06               # stickcurve.h STICK_DIGITAL_GAP

# the reference palette (dataviz skill): surface, inks, series
SURFACE = (252, 252, 251)
INK = (11, 11, 11)
INK2 = (82, 81, 78)
MUTED = (195, 194, 183)
FINGER = (42, 120, 214)      # blue, series 1
KNOB = (235, 104, 52)        # orange, series 2
ZONE = (27, 175, 122)        # aqua, series 3
BAD = (227, 73, 72)          # red, series 8 (the spring in mid-gesture)


def font(size, bold=False):
    for name in (("segoeuib.ttf" if bold else "segoeui.ttf"), "arial.ttf"):
        try:
            return ImageFont.truetype(os.path.join(r"C:\Windows\Fonts", name), size)
        except OSError:
            pass
    return ImageFont.load_default()


# ---------------------------------------------------------------- the curve (stickcurve.h)

def curve_analogue(dx, dy, travel, sens, dz):
    ln = math.hypot(dx, dy)
    m = ln / travel
    if not (m > dz) or not (ln > 0):
        return 0.0, 0.0
    k = (m - dz) / (sens - dz) if sens > dz else 1.0
    k = min(k, 1.0)
    return dx / ln * k, dy / ln * k


def curve_digital(dx, dy, travel, dz, gap, on):
    ln = math.hypot(dx, dy)
    m = ln / travel
    off = max(dz - gap, 0.0)
    if on:
        if not (m > off):
            on = False
    elif m > dz:
        on = True
    if not on or not (ln > 0):
        return (0.0, 0.0), on
    return (dx / ln, dy / ln), on


def native(dx, dy, div):
    return dx / div, dy / div


# ---------------------------------------------------------------- the stick's update

def elastic(t):
    return 1.0 + 2.0 ** (-10.0 * t) * math.sin((t - 0.075) * 2.0 * math.pi / 0.3)


class Stick:
    """The update from 0x862f04 on, the movement stick (+0x1c9 = 1), static (grab = seat)."""

    def __init__(self, sens):
        self.sens = sens
        self.held = False
        self.target = (0.0, 0.0)
        self.start = (0.0, 0.0)
        self.t = 1.0
        self.drawn = (0.0, 0.0)
        self.value = (0.0, 0.0)

    def update(self, finger, knob_rule):
        """finger: None when the game is told it is up; knob_rule(d, value) -> target."""
        if finger is None:
            if self.held:                       # 0x86314c: the lift
                self.held = False
                self.start = self.target
                self.t = 0.0
                self.value = (0.0, 0.0)
                self.target = (0.0, 0.0)
        else:
            self.held = True
            dx, dy = finger                     # grab = seat = 0,0
            vx, vy = native(dx, dy, TRAVEL * self.sens)
            mag = math.hypot(vx, vy)
            if mag > 1:
                vx, vy = vx / mag, vy / mag
            self.value = (vx, vy)
            self.target = knob_rule((dx, dy), (vx, vy))
        # 0x863188: the animation
        self.t = min(self.t + 0.1, 1.0)
        if self.t == 0.0:
            self.drawn = self.start
        elif self.t >= 1.0:
            self.drawn = self.target
        else:
            e = elastic(self.t)
            self.drawn = (self.start[0] + (self.target[0] - self.start[0]) * e,
                          self.start[1] + (self.target[1] - self.start[1]) * e)
        return self.drawn


def game_knob(d, v):
    return v[0] * TRAVEL, v[1] * TRAVEL


def finger_knob(d, v):
    ln = math.hypot(*d)
    if ln > TRAVEL:
        return d[0] * TRAVEL / ln, d[1] * TRAVEL / ln
    return d


def run_scheme(trace, scheme):
    """-> [(finger, drawn knob, mid-gesture spring?)] per frame."""
    if scheme == "yours":
        st = Stick(0.55)
        return [(f, st.update(f, game_knob), False) for f in trace]
    if scheme == "finger":
        st = Stick(0.8)
        return [(f, st.update(f, finger_knob), False) for f in trace]
    # the old zone (sticks.c before batch 3): 0.25 of the travel round the seat on a static stick
    st = Stick(0.8)
    zone = 0.25 * TRAVEL
    out = []
    parked = False
    for i, f in enumerate(trace):
        inside = math.hypot(*f) <= zone
        if i == 0:
            parked = inside
        elif inside and not parked:
            parked = True                        # "let go": a lift at the seat
        elif not inside and parked:
            parked = False                       # "given back": pressed afresh here
        drawn = st.update(None if parked else f, game_knob)
        spring = (not parked) and st.t < 1.0
        if parked:
            drawn = f                            # knob_hold: the raw finger, written before the draw
        out.append((f, drawn, spring))
    return out


# ---------------------------------------------------------------- the traces

def trace_circle():
    pts = []
    for i in range(120):
        a = 2 * math.pi * i / 120
        r = 0.55 * TRAVEL + 0.35 * TRAVEL * math.sin(a * 2)
        pts.append((r * math.cos(a), r * math.sin(a)))
    return pts


def trace_swing():
    pts = []
    for rep in range(3):
        for i in range(10):
            x = 1.1 * TRAVEL * math.cos(math.pi * i / 9)
            pts.append((x, 0.08 * TRAVEL * math.sin(i)))
        for i in range(10):
            x = -1.1 * TRAVEL * math.cos(math.pi * i / 9)
            pts.append((x, -0.08 * TRAVEL * math.sin(i)))
    return pts


def trace_rest():
    random.seed(7)
    pts, x, y = [], 0.18 * TRAVEL, -0.12 * TRAVEL
    for i in range(120):
        x += random.gauss(0, 0.15)
        y += random.gauss(0, 0.15)
        pts.append((x + 0.3 * math.sin(i * 0.9), y + 0.3 * math.cos(i * 1.1)))
    return pts


# ---------------------------------------------------------------- drawing

def panel(dr, box, frames, title, sub, zone=None):
    x0, y0, x1, y1 = box
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2 + 18
    k = min(x1 - x0, y1 - y0 - 60) / 2 / (RIM + 6)
    dr.text((x0 + 8, y0 + 2), title, fill=INK, font=font(22, True))
    dr.text((x0 + 8, y0 + 30), sub, fill=INK2, font=font(16))
    def P(p):
        return cx + p[0] * k, cy + p[1] * k
    dr.ellipse((cx - RIM * k, cy - RIM * k, cx + RIM * k, cy + RIM * k), outline=MUTED, width=3)
    dr.ellipse((cx - TRAVEL * k, cy - TRAVEL * k, cx + TRAVEL * k, cy + TRAVEL * k), outline=MUTED, width=1)
    if zone:
        r = zone * TRAVEL * k
        dr.ellipse((cx - r, cy - r, cx + r, cy + r), outline=ZONE, width=2)
    fl = [P(f) for f, _, _ in frames]
    kl = [P(d) for _, d, _ in frames]
    dr.line(kl, fill=KNOB, width=5)             # the knob under, the finger over: one line where they agree
    dr.line(fl, fill=FINGER, width=2)
    for (f, d, spring), p in zip(frames, kl):
        if spring:
            dr.ellipse((p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6), outline=BAD, width=3)
    past = sum(1 for _, d, _ in frames if math.hypot(*d) > TRAVEL + 0.01)
    ratio = [math.hypot(*d) / math.hypot(*f) for f, d, _ in frames if math.hypot(*f) > 2]
    springs = sum(1 for _, _, s in frames if s)
    dr.text((x0 + 8, y1 - 46), "ход ручки / ход пальца: %.2f" % (sorted(ratio)[len(ratio) // 2] if ratio else 0),
            fill=INK2, font=font(15))
    dr.text((x0 + 8, y1 - 26), "кадров с ручкой за ходом: %d, пружина посреди жеста: %d" % (past, springs),
            fill=BAD if past or springs else INK2, font=font(15))


def legend(dr, x, y):
    items = [(FINGER, "путь пальца"), (KNOB, "путь ручки"), (ZONE, "мёртвая зона"), (MUTED, "кольцо и ход ручки"),
             (BAD, "пружина посреди жеста")]
    for col, name in items:
        dr.line((x, y + 11, x + 28, y + 11), fill=col, width=4)
        dr.text((x + 36, y), name, fill=INK, font=font(17))
        x += 60 + int(dr.textlength(name, font=font(17)))


def picture_knob():
    traces = [("Медленный круг", trace_circle()), ("Мах обод — обод", trace_swing()),
              ("Палец у середины", trace_rest())]
    schemes = [("yours", "Как было у вас", "DEAD ZONE 0, MOVE SENS 0.55: ручку рисует игра", None),
               ("oldzone", "Старая зона", "DEAD ZONE 0.25, MOVE SENS 0.8 (новый профиль)", 0.25),
               ("finger", "По пальцу (пачка 3)", "любые DEAD ZONE и MOVE SENS", 0.25)]
    W, H = 1500, 1560
    im = Image.new("RGB", (W, H), SURFACE)
    dr = ImageDraw.Draw(im)
    dr.text((24, 16), "Ручка стика: как было и как станет", fill=INK, font=font(34, True))
    dr.text((24, 60), "Модель обновления стика из образа (0x862f04, 0x863188). Статичный стик 125%, "
            "ход ручки 20 единиц, кольцо 40.", fill=INK2, font=font(18))
    legend(dr, 24, 94)
    cw, ch = W // 3, (H - 140) // 3
    for r, (tname, tr) in enumerate(traces):
        for c, (key, name, sub, zone) in enumerate(schemes):
            box = (c * cw + 6, 140 + r * ch, (c + 1) * cw - 6, 140 + (r + 1) * ch - 8)
            dr.rectangle(box, outline=(230, 229, 224))
            panel(dr, box, run_scheme(tr, key), "%s — %s" % (name, tname.lower()), sub,
                  zone if key != "yours" else None)
    path = os.path.join(OUT, "knob.png")
    im.save(path)
    return path


# ---------------------------------------------------------------- DIGITAL

def trembling(m0, amp_px, seconds=20, seed=1):
    """A thumb held at m0 (share of the travel): drift, 9 Hz tremor, sensor noise; per frame."""
    rnd = random.Random(seed)
    unit = 1.0 / (TRAVEL * PX_PER_UNIT)         # one pixel as a share of the travel
    out, drift = [], 0.0
    for i in range(seconds * 60):
        drift = 0.995 * drift + rnd.gauss(0, 0.15 * unit)
        tremor = amp_px * unit * math.sin(2 * math.pi * 9.0 * i / 60 + rnd.gauss(0, 0.3))
        out.append(m0 + drift + tremor + rnd.gauss(0, 0.6 * unit))
    return out


def toggles(ms, dz, gap):
    on, n, states = False, 0, []
    for m in ms:
        _, now = curve_digital(m * TRAVEL, 0.0, TRAVEL, dz, gap, on)
        if now and not on:
            n += 1
        on = now
        states.append(on)
    return n, states


def tune_gap():
    gaps = [0.0, 0.02, 0.03, 0.04, 0.05, 0.06, 0.08, 0.12]
    amps = [0.5, 1.0, 1.5, 2.0, 3.0]
    table = {}
    for a in amps:
        for g in gaps:
            tot = 0
            for seed in range(8):
                n, _ = toggles(trembling(0.25, a, seed=seed), 0.25, g)
                tot += max(n - 1, 0)                  # the first switch-on is the thumb arriving
            table[(a, g)] = tot / 8.0 / 20.0 * 60.0   # extra switch-ons a minute
    return gaps, amps, table


def picture_digital():
    W, H = 1500, 1180
    im = Image.new("RGB", (W, H), SURFACE)
    dr = ImageDraw.Draw(im)
    dr.text((24, 16), "DIGITAL: где персонаж срывается с места", fill=INK, font=font(34, True))
    # the thresholds on the travel
    x0, x1, y = 90, W - 90, 150
    dr.line((x0, y, x1, y), fill=MUTED, width=6)
    for i in range(11):
        xx = x0 + (x1 - x0) * i / 10
        dr.line((xx, y - 10, xx, y + 10), fill=MUTED, width=2)
        dr.text((xx - 16, y + 16), "%d%%" % (i * 10), fill=INK2, font=font(16))
    marks = [(0.425, INK2, "iOS: 42,5% (0,85 × 0,5)"), (0.68, BAD, "порт до сих пор: 68% (0,85 × MOVE SENS 0,8)"),
             (0.25 + 0.85 * (0.8 - 0.25), KNOB, "(А) 72%: DEAD ZONE + 0,85 × (SENS − ZONE)"), (0.25, ZONE, "(Б) 25% = DEAD ZONE — выбрано")]
    for i, (v, col, text) in enumerate(sorted(marks)):
        xx = x0 + (x1 - x0) * v
        dr.polygon([(xx, y - 14), (xx - 10, y - 34), (xx + 10, y - 34)], fill=col)
        dr.text((xx - 10, y - 66 - 26 * (i % 2)), text, fill=INK, font=font(17))
    dr.text((x0, y + 46), "Доля хода ручки от центра до обода. Левее порога персонаж стоит, правее — бежит "
            "на полной скорости.", fill=INK2, font=font(17))
    dr.text((x0, y + 70), "В (Б) MOVE SENS в DIGITAL ни на что не влияет — он только для ANALOGUE.",
            fill=INK2, font=font(17))
    # the hysteresis: a trembling thumb on the line, gap 0 against 0.05
    ms = trembling(0.25, 1.5, seconds=4, seed=3)
    rows = [(0.0, "без гистерезиса"), (GAP, "гистерезис %.2f хода (≈%.0f px)" % (GAP, GAP * TRAVEL * PX_PER_UNIT))]
    gy = 300
    dr.text((24, gy - 44), "Палец дрожит ровно на пороге 25%% (4 с): дрожь 1,5 px, шум датчика 0,6 px, "
            "%.2f px на единицу" % PX_PER_UNIT, fill=INK, font=font(20, True))
    for r, (g, name) in enumerate(rows):
        n, st = toggles(ms, 0.25, g)
        top = gy + r * 260
        dr.text((24, top), "%s: персонаж срывается с места %d раз" % (name, n), fill=INK, font=font(19, True))
        px0, px1, py0, py1 = 90, W - 90, top + 34, top + 190
        lo, hi = 0.17, 0.30
        def Y(m):
            return py1 - (m - lo) / (hi - lo) * (py1 - py0)
        for v, col in ((0.25, ZONE), (max(0.25 - g, 0), KNOB)):
            if lo <= v <= hi:
                dr.line((px0, Y(v), px1, Y(v)), fill=col, width=2)
        pts = [(px0 + (px1 - px0) * i / (len(ms) - 1), Y(m)) for i, m in enumerate(ms)]
        dr.line(pts, fill=FINGER, width=2)
        for i, on in enumerate(st):
            xx = px0 + (px1 - px0) * i / (len(ms) - 1)
            if on:
                dr.line((xx, py1 + 8, xx, py1 + 26), fill=INK, width=2)
        dr.text((px1 + 6, Y(0.25) - 10), "вкл", fill=ZONE, font=font(15))
        if g:
            dr.text((px1 + 6, Y(0.25 - g) - 10), "выкл", fill=KNOB, font=font(15))
        dr.text((24, py1 + 6), "бег", fill=INK2, font=font(15))
    # the tuning table
    gaps, amps, table = tune_gap()
    ty = 850
    dr.text((24, ty), "Подбор зазора: лишних срывов с места в минуту, палец держат на пороге (8 проб по 20 с)",
            fill=INK, font=font(20, True))
    cx0 = 250
    for j, g in enumerate(gaps):
        dr.text((cx0 + j * 150, ty + 36), "зазор %.2f" % g, fill=INK2 if g != GAP else INK, font=font(17, g == GAP))
    for i, a in enumerate(amps):
        yy = ty + 66 + i * 34
        dr.text((24, yy), "дрожь %.1f px" % a, fill=INK2, font=font(17))
        for j, g in enumerate(gaps):
            v = table[(a, g)]
            col = BAD if v >= 3 else (INK if v >= 0.5 else ZONE)
            dr.text((cx0 + j * 150 + 20, yy), "%.0f" % v, fill=col, font=font(17, g == GAP))
    path = os.path.join(OUT, "digital.png")
    im.save(path)
    return path, gaps, amps, table


# ---------------------------------------------------------------- the capture circle, FULLSCREEN off

def capture_table():
    """The static stick's capture (4 travels, window units) against its ring (picture scale), in px."""
    rows = []
    for name, (w, h) in [("16:9", (1920, 1080)), ("owner 3120x1440", (3120, 1440)), ("19.5:9", (2340, 1080)),
                         ("21:9", (2520, 1080)), ("4:3", (2048, 1536)), ("16:10", (2560, 1600))]:
        for full in ((True, False) if w / h > 16 / 9 - 0.01 else (False,)):
            if full:
                cw, ch = 270.0 * w / h, 270.0
                aw, ah = w, h
            else:
                cw, ch = 480.0, 270.0
                s = min(w / 480.0, h / 270.0)
                aw, ah = 480.0 * s, 270.0 * s
            sx, sy = aw / w * 0.5, ah / h * 0.5          # 0x861ec8..0x861f30, K = 1
            px, py = w / cw, h / ch                      # the stick pass: the window
            cap = (4 * TRAVEL * px, 4 * TRAVEL * py)
            ring = (64 * 1.25 * sx * px, 64 * 1.25 * sy * py)
            knob_edge = (TRAVEL * px + 32 * 1.25 * sx * px, TRAVEL * py + 32 * 1.25 * sy * py)
            rows.append((name, full, cap, ring, knob_edge))
    return rows


def selftest():
    fails = 0
    for s in (0.1, 0.3, 0.55, 0.8, 1.0):
        for dz in (0.05, 0.25, 0.5, 0.9):
            prev = 0
            for i in range(1601):
                m = i / 1000.0
                x, y = curve_analogue(m * TRAVEL * 0.6, m * TRAVEL * 0.8, TRAVEL, s, dz)
                mag = math.hypot(x, y)
                if m <= dz * 0.999 and mag != 0: fails += 1
                if mag > 1 + 1e-9 or mag + 1e-9 < prev: fails += 1
                prev = mag
    on = False
    off = 0.25 - GAP
    for m, want in ((off, 0), (0.251, 1), (off + 0.01, 1), (off - 0.001, 0), (0.24, 0), (0.26, 1)):
        _, on = curve_digital(m * TRAVEL, 0, TRAVEL, 0.25, GAP, on)
        fails += on != bool(want)
    print("stickcurve: selftest %s" % ("ok" if not fails else "FAIL, %d checks" % fails))
    return fails


HOST_C = r"""
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static int tiny_snprintf(char *out, size_t room, const char *fmt, ...) {
    size_t n = 0; for (; fmt[n] && n + 1 < room; n++) out[n] = fmt[n]; if (room) out[n] = 0; return (int) n;
}
void *memcpy(void *d, const void *s, size_t n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { char *a = d; while (n--) *a++ = (char) c; return d; }
int _fltused = 0;
#undef isfinite
#define isfinite __builtin_isfinite
#define sqrtf __builtin_sqrtf
#define fabsf __builtin_fabsf
#define snprintf tiny_snprintf
#include "HEADER"
__declspec(dllexport) int selftest(char *why, int room) { return stick_curve_selftest(why, (size_t) room); }
__declspec(dllexport) void output(const float *d, float div, float travel, float dz, int mode, int zoned,
                                  int *on, float *out) {
    stick_v2 r = stick_output(d, div, travel, dz, mode, zoned, on); out[0] = r.x; out[1] = r.y; }
"""


def host_c():
    import ctypes, subprocess, tempfile
    header = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "port", "app", "src", "main", "cpp",
                          "stickcurve.h")
    tmp = tempfile.mkdtemp(prefix="stickcurve")
    src, dll = os.path.join(tmp, "curve.c"), os.path.join(tmp, "curve.dll")
    open(src, "w").write(HOST_C.replace("HEADER", os.path.abspath(header).replace("\\", "/")))
    subprocess.check_call([os.path.join(os.environ.get("ProgramFiles", "C:/Program Files"), "LLVM", "bin", "clang.exe"), "--target=x86_64-pc-windows-msvc", "-O2",
                           "-fno-math-errno", "-shared", "-nostdlib", "-Wl,/noentry", src, "-o", dll])
    lib = ctypes.CDLL(dll)
    why = ctypes.create_string_buffer(200)
    fails = lib.selftest(why, 200)
    print("stickcurve.h selftest (C, this PC): %s" % ("ok" if not fails else "FAIL, %d: %s" % (fails, why.value)))
    f2 = ctypes.c_float * 2
    rnd, worst = random.Random(1), 0.0
    for _ in range(20000):
        dx, dy = rnd.uniform(-30, 30), rnd.uniform(-30, 30)
        s, dz = rnd.choice([0.1, 0.3, 0.55, 0.8, 1.0]), rnd.choice([0.05, 0.25, 0.5])
        out, on = f2(), ctypes.c_int(0)
        lib.output(f2(dx, dy), ctypes.c_float(TRAVEL * s), ctypes.c_float(TRAVEL), ctypes.c_float(dz), 1, 1,
                   ctypes.byref(on), out)
        px, py = curve_analogue(dx, dy, TRAVEL, s, dz)
        worst = max(worst, abs(px - out[0]), abs(py - out[1]))
    print("the C curve against this model: worst difference %.2g" % worst)
    return fails


def main():
    if "--c" in sys.argv:
        sys.exit(1 if host_c() else 0)
    if "--selftest" in sys.argv:
        sys.exit(1 if selftest() else 0)
    if "--capture" in sys.argv:
        print("%-20s %-4s %-17s %-17s %-10s %-17s" % ("screen", "FULL", "capture px", "ring px", "cap/ring", "knob edge px"))
        for name, full, cap, ring, ke in capture_table():
            print("%-20s %-4s %6.0f x %-8.0f %6.0f x %-8.0f %.2f x %.2f  %6.0f x %-6.0f" % (
                name, "on" if full else "off", cap[0], cap[1], ring[0], ring[1], cap[0] / ring[0], cap[1] / ring[1],
                ke[0], ke[1]))
        return
    os.makedirs(OUT, exist_ok=True)
    selftest()
    print(picture_knob())
    path, gaps, amps, table = picture_digital()
    print(path)
    print("extra switch-ons a minute, thumb held on the line (rows: tremor px; columns: gap):")
    print("        " + " ".join("%6.2f" % g for g in gaps))
    for a in amps:
        print("  %4.1f  " % a + " ".join("%6.1f" % table[(a, g)] for g in gaps))


if __name__ == "__main__":
    main()
