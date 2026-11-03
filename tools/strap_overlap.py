# -*- coding: utf-8 -*-
"""Is anything of the game drawn OVER a port strap? Measured on what the game loads.

A strap sticks out from under a menu paper (straps.c). The paper is meant to lie
over its left end; nothing else is - the wall's sketches, stains, cards are the
wall, and the strap hangs in front of the wall. The owner found the chest
sketch of the title sheet drawn over the Japanese PORT CREDITS strap (19th
pass, 03.10.2026): the strap layers stood before Main, and Main carries both
the paper and the wall's sketches.

For every strap, every language set and every step of its slide this draws the
screen's sprite as the game would (vanilla + port_patch/, tools/effective.py
After) in the game's order, and counts the strap's pixels that a later draw
covers - split into the PAPER (the one big piece of the paper sheet the strap
is meant to go under, with its shadow) and EVERYTHING ELSE that lands on a
strap pixel still showing (what lies on the paper over a part of the strap the
opaque paper already hides is the paper's business). Everything else must be
nought.

Every screen format: what is inside the sprite keeps its place on any canvas;
the one thing of the game that moves against it is the title's SELECT paper in
the canvas corner - checked on every canvas the port makes (corner_paper).
NOT measured on canvases: the settings straps (their sheet scrolls with the list;
at rest they hang at the paper's head, grid y 64..163, and the corner papers start
at canvas y 179 and lower) and the pause (no corner papers drawn there).

    py -3 tools/strap_overlap.py                 measure; exit 1 if anything else covers a strap
    py -3 tools/strap_overlap.py --pictures DIR  also write the strap's region, 4x, per set
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anm2patch as ap
import effective as ef

import numpy as np
from PIL import Image

SETS = ["data", "dlc3.de", "dlc3.es", "dlc3.jp", "dlc3.kr", "dlc3.ru", "dlc3.zh"]
STEPS = 6

# strap: anm2 (under repentance-resources/<set>/gfx/), animation, the strap's layer
# runs (each drawn alone, one step at a time), how the screen orders its draws, and
# the layers that are the paper the strap goes under (their biggest piece only, for
# a sheet that carries the wall's sketches beside the paper).
# Layers the game does not show on that screen are left out of the draw.
STRAPS = [
    dict(name="credits", anm2="gfx/ui/main menu/gamemenu.anm2", anim="Idle",
         runs=[("PortStrap", None)], order="list",
         paper={"Main": "piece"},
         hidden={"DailyPaper", "DailyText", "DailyTab"}),   # not on the title (the owner's screenshots)
    dict(name="tweaks", anm2="gfx/ui/main menu/optionsmenu.anm2", anim="Paper13",
         runs=[("PortStrap", None)], order="list",
         paper={"PaperBottom": "all", "PaperMiddle": "all", "PaperMiddle2": "all", "PaperMiddle3": "all",
                "PaperMiddle4": "all", "Paper": "all", "Title": "all", "Text": "all", "Cursor": "all",
                "Option Name": "all", "Option Name 2": "all", "Adjustor1": "all", "OnOff": "all"},
         hidden=set()),
    dict(name="guide", anm2="gfx/ui/main menu/optionsmenu.anm2", anim="Paper13",
         runs=[("GuideStrap", None)], order="list",
         paper={"PaperBottom": "all", "PaperMiddle": "all", "PaperMiddle2": "all", "PaperMiddle3": "all",
                "PaperMiddle4": "all", "Paper": "all", "Title": "all", "Text": "all", "Cursor": "all",
                "Option Name": "all", "Option Name 2": "all", "Adjustor1": "all", "OnOff": "all"},
         hidden=set()),
    # the pause draws its layers by id 0..7 (0x8a23b4); the strap is drawn from the
    # layer render just before layer 1, the paper (straps.c shim_straps_layer)
    dict(name="spawner", anm2="gfx/ui/pausescreen.anm2", anim="Idle",
         runs=[("PortStrap", None)], order="pause",
         paper={"Paper": "all", "Cursor": "all"},
         hidden=set()),
]


class Game:
    """the files the game loads: vanilla + port_patch/ (effective.After)"""

    def __init__(self):
        self.v = ef.After()

    def anm2(self, st, rel):
        for s in (st, "data"):
            p = "repentance-resources/%s/%s" % (s, rel)
            b = self.v.get(p)
            if b is not None:
                return p, b
        return None, None

    def sheet(self, st, folder, rel):
        for s in (st, "data"):
            p = "repentance-resources/%s/%s/%s" % (s, folder, rel)
            img = self.v.image(p) if self.v.get(p) is not None else None
            if img is not None:
                return img
        return None


def model(b):
    m = ap.Model(ap.parse(b))
    sheets = {x.get("Id"): x.get("Path") for x in m.sheets.kids if x.tag == "Spritesheet"}
    layers = {x.get("Id"): (x.get("Name"), x.get("SpritesheetId")) for x in m.layers.kids if x.tag == "Layer"}
    anims = {a.get("Name"): a for a in m.anims.kids if a.tag == "Animation"}
    return sheets, layers, anims


def num(f, k, d=0.0):
    try:
        return float(f.get(k))
    except (TypeError, ValueError):
        return d


def frame_at(la, t):
    at = 0
    frames = [f for f in la.kids if f.tag == "Frame"]
    for f in frames:
        d = int(num(f, "Delay", 1))
        if t < at + d:
            return f
        at += d
    return None


def place(canvas_shape, img, f, ox, oy):
    """the frame's crop as a full-canvas RGBA float layer (scale by nearest; no rotation - checked)"""
    H, W = canvas_shape
    out = np.zeros((H, W, 4), np.float32)
    x, y, w, h = (int(num(f, k)) for k in ("XCrop", "YCrop", "Width", "Height"))
    sx, sy, rot = num(f, "XScale", 100), num(f, "YScale", 100), num(f, "Rotation")
    if rot != 0:
        raise SystemExit("frame with rotation %s: not handled" % rot)
    crop = np.zeros((h, w, 4), np.float32)
    sh, sw = img.shape[:2]
    x0, y0, x1, y1 = max(0, x), max(0, y), min(sw, x + w), min(sh, y + h)
    if x1 > x0 and y1 > y0:
        crop[y0 - y:y1 - y, x0 - x:x1 - x] = img[y0:y1, x0:x1] / 255.0
    if (sx, sy) != (100, 100):
        nw, nh = max(1, int(round(w * sx / 100))), max(1, int(round(h * sy / 100)))
        crop = crop[(np.arange(nh) * h // nh)[:, None], (np.arange(nw) * w // nw)[None, :]]
        w, h = nw, nh
    crop[..., 3] *= num(f, "AlphaTint", 255) / 255.0
    for i, k in enumerate(("RedTint", "GreenTint", "BlueTint")):
        crop[..., i] = np.clip(crop[..., i] * num(f, k, 255) / 255.0 + num(f, k.replace("Tint", "Offset")) / 255.0, 0, 1)
    px = int(round(num(f, "XPosition") - num(f, "XPivot") * sx / 100)) + ox
    py = int(round(num(f, "YPosition") - num(f, "YPivot") * sy / 100)) + oy
    X0, Y0, X1, Y1 = max(0, px), max(0, py), min(W, px + w), min(H, py + h)
    if X1 > X0 and Y1 > Y0:
        out[Y0:Y1, X0:X1] = crop[Y0 - py:Y1 - py, X0 - px:X1 - px]
    return out


_pieces = {}


def biggest_piece(alpha):
    """the largest 8-connected piece of the visible pixels (kept: the same sheet comes again and again)"""
    key = hash(alpha.tobytes())
    if key not in _pieces:
        _pieces[key] = _biggest_piece(alpha)
    return _pieces[key]


def _biggest_piece(alpha):
    from collections import deque
    vis = alpha > 0
    seen = np.zeros_like(vis)
    best = None
    H, W = vis.shape
    for y0, x0 in zip(*np.nonzero(vis)):
        if seen[y0, x0]:
            continue
        q = deque([(y0, x0)])
        seen[y0, x0] = True
        pts = []
        while q:
            y, x = q.popleft()
            pts.append((y, x))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < H and 0 <= xx < W and vis[yy, xx] and not seen[yy, xx]:
                        seen[yy, xx] = True
                        q.append((yy, xx))
        if best is None or len(pts) > len(best):
            best = pts
    m = np.zeros_like(vis)
    if best:
        ys, xs = zip(*best)
        m[list(ys), list(xs)] = True
    return m


def over(dst, src):
    a = src[..., 3:4]
    dst[..., :3] = src[..., :3] * a + dst[..., :3] * (1 - a)
    dst[..., 3:4] = a + dst[..., 3:4] * (1 - a)


def measure(game, s, st, step, pictures=None, anm2=None, info=None):
    """{layer: covered strap pixels}, picture of the region

    anm2: bytes to draw instead of the file the game loads (a proposed change);
    info: a dict that gets the strap's mask, what of it still shows, the canvas
    and the sprite's origin in it (ox, oy)"""
    path, b = game.anm2(st, s["anm2"])
    if anm2 is not None:
        b = anm2
    if b is None:
        return None, None
    folder = s["anm2"].rsplit("/", 1)[0]
    sheets, layers, anims = model(b)
    a = anims[s["anim"]]
    las = [la for la in a.find("LayerAnimations").kids if la.tag == "LayerAnimation"]
    byname = {layers[la.get("LayerId")][0]: la for la in las}
    strap_names = set()
    for base, top in s["runs"]:
        for k in range(STEPS):
            strap_names.add("%s%d" % (base, k))
            if top:
                strap_names.add("%s%d" % (top, k))
    shown = set()
    for base, top in s["runs"]:
        shown.add("%s%d" % (base, step))
        if top:
            shown.add("%s%d" % (top, step))
    if s["order"] == "pause":
        ids = sorted(int(la.get("LayerId")) for la in las if layers[la.get("LayerId")][0] not in strap_names)
        seq = []
        for i in ids:
            if i > 7:
                continue
            if i == 1:
                seq += [byname[n] for n in sorted(shown) if n in byname]
            seq.append([la for la in las if int(la.get("LayerId")) == i][0])
    else:
        seq = [la for la in las if layers[la.get("LayerId")][0] not in strap_names or layers[la.get("LayerId")][0] in shown]
    H, W, ox, oy = 400, 600, 60, 60
    canvas = np.zeros((H, W, 4), np.float32)
    canvas[..., :3] = 0.55                     # the wall's grey behind the sprite
    canvas[..., 3] = 1
    strap = np.zeros((H, W), bool)
    hidden = np.zeros((H, W), bool)          # strap pixels an opaque paper pixel already hides
    cover = {}
    t = 0
    for la in seq:
        name, sid = layers[la.get("LayerId")]
        # a strap's own shown flag is set by straps.c every frame, whatever the file says
        if la.get("Visible") == "false" and name not in strap_names:
            continue
        if name in s["hidden"]:
            continue
        f = frame_at(la, t)
        if f is None or f.get("Visible") == "false":
            continue
        img = game.sheet(st, folder, sheets.get(sid, ""))
        if img is None:
            continue
        lay = place((H, W), img.astype(np.float32), f, ox, oy)
        al = lay[..., 3]
        is_strap = name in strap_names
        if not is_strap and strap.any():
            hit = (al > 0) & strap & ~hidden
            if hit.any():
                kind = s["paper"].get(name, "x")
                if kind == "all":
                    cover.setdefault("paper", 0)
                    cover["paper"] += int(hit.sum())
                    hidden |= hit & (al >= 1)
                elif kind == "piece":
                    piece = biggest_piece(al)
                    ph = hit & piece
                    rest = hit & ~piece
                    if ph.any():
                        cover["paper"] = cover.get("paper", 0) + int(ph.sum())
                        hidden |= ph & (al >= 1)
                    if rest.any():
                        cover[name + " (beside the paper)"] = int(rest.sum())
                else:
                    cover[name] = cover.get(name, 0) + int(hit.sum())
        over(canvas, lay)
        if is_strap:
            strap |= al > 0
    if info is not None:
        info.update(strap=strap, showing=strap & ~hidden, canvas=canvas, origin=(ox, oy))
    pic = None
    if strap.any():
        ys, xs = np.nonzero(strap)
        x0, y0, x1, y1 = max(0, xs.min() - 30), max(0, ys.min() - 20), min(W, xs.max() + 50), min(H, ys.max() + 20)
        pic = Image.fromarray((canvas[y0:y1, x0:x1] * 255).astype(np.uint8)).resize(((x1 - x0) * 4, (y1 - y0) * 4), Image.NEAREST)
    return cover, pic


def corner_paper(game, anm2_of=None):
    """{set: worst overlap} of the title's SELECT paper with the credits strap, on every canvas

    The SELECT paper is a sprite of its own (backselectwidget.anm2 "Select": crop
    96,0 112x128, pivot 16,16), drawn after the menu at (W - 80, H - 75) of the
    canvas, 20 lower while the canvas is under 560 wide and under 300 tall
    (screen.c shim_screen_papers); the title sheet stands centred on the canvas.
    Canvases: 270 tall and 480..720 wide, or 480 wide and 270..540 tall
    (screen.c canvas_for)."""
    out = {}
    for st in SETS:
        path, b = game.anm2(st, "gfx/ui/main menu/gamemenu.anm2")
        if anm2_of and st in anm2_of:
            b = anm2_of[st]
        sheets, layers, anims = model(b)
        las = {layers[la.get("LayerId")][0]: la for la in anims["Idle"].find("LayerAnimations").kids
               if la.tag == "LayerAnimation"}
        img = game.sheet(st, "gfx/ui/main menu", "port_strap_credits.png").astype(np.float32)
        masks = []
        for k in range(STEPS):
            m = np.zeros((600, 1000), bool)
            m |= place((600, 1000), img, frame_at(las["PortStrap%d" % k], 0), 0, 0)[..., 3] > 0
            masks.append(m)
        paper = game.sheet(st, "gfx/ui", "BackSelectWidget.png")[0:128, 96:208, 3] > 0
        worst = 0
        for cw, ch in [(w, 270) for w in range(480, 721)] + [(480, h) for h in range(271, 541)]:
            lowered = cw < 560 and ch < 300
            px = cw - 80 - 16 - (cw - 480) // 2
            py = ch - (55 if lowered else 75) - 16 - (ch - 270) // 2
            for m in masks:
                sub = m[py:py + 128, px:px + 112]
                worst = max(worst, int((sub & paper[:sub.shape[0], :sub.shape[1]]).sum()))
        out[st] = worst
    return out


def main(argv):
    pics = None
    if "--pictures" in argv:
        pics = argv[argv.index("--pictures") + 1]
        os.makedirs(pics, exist_ok=True)
    game = Game()
    bad = 0
    for s in STRAPS:
        for st in SETS:
            line = []
            for step in range(STEPS):
                cover, pic = measure(game, s, st, step)
                if cover is None:
                    line.append("no anm2")
                    break
                others = {k: v for k, v in cover.items() if k != "paper"}
                bad += sum(others.values())
                line.append("%d:%s" % (step, ",".join("%s=%d" % kv for kv in sorted(others.items())) or "ok"))
                if pics and pic is not None and step in (0, STEPS - 1):
                    pic.save(os.path.join(pics, "%s-%s-step%d.png" % (s["name"], st, step)))
            print("  %-8s %-8s %s" % (s["name"], st, "  ".join(line)))
    cp = corner_paper(game)
    print("  credits  SELECT paper over the strap, worst canvas: %s" % "  ".join("%s=%d" % kv for kv in cp.items()))
    bad += sum(cp.values())
    print("STRAPS", "CLEAR" if not bad else "COVERED (%d pixel-steps)" % bad)
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
