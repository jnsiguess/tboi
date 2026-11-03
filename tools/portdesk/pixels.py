# -*- coding: utf-8 -*-
"""The port's retouch of gamemenu.png laid over whichever sheet wins (02.10.2026, the M8 regression).

The port clears the game's blank paper under its PORT CREDITS strap (gamemenu.png, 329..403 x 141..246).
Since M8 that was the "over" file alone - the game's sheet only; a mod's gamemenu.png of the game's size
brought the paper back under the strap. Now the retouch is also pixels (port_patch.pixels_make), and
portpatch.c lays them over a mod's sheet of the game's size (shim_port_pixels_file, from usermods.c).

Proved here, portpatch.c's own reader, retouch and writer (portdesk.exe) against the Python twin:
  1. VANILLA: vanilla + pixels == the port's "over" sheet, pixel for pixel, every set - so what the game
     draws with no mod is unchanged (effective.py's comparison of draws reduces to the sheet's pixels:
     the anm2 is the same);
  2. A MOD SHEET of the game's size (a recolour of the game's, in every PNG form libpng 1.0.2 reads -
     RGBA, RGB, RGB+tRNS, palette+tRNS, grey, grey+alpha, 16-bit, Adam7): the result is the mod's
     picture everywhere but the retouched pixels, which are the port's - the strap's place is clear;
     C == Python on every pixel; and what the menu's Main layer draws (its crop 0,0 480x270) is the
     mod's but there;
  3. A sheet of another size (Reflashed's 4x) is left alone (1);
  4. the reader alone on the game's own sheets of every type in the pack.

    bash tools/portdesk/build.sh && py -3 tools/portdesk/pixels.py
"""
import io
import os
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import port_additions as pa
import port_patch as pp

import numpy as np
from PIL import Image

EXE = os.path.join(HERE, "portdesk.exe")
TMP = tempfile.mkdtemp(prefix="pixels")
REFLASHED = os.path.join(pa.ROOT, "other stuff", "usermods", "ported", "reflashed.zip")
failures = []


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        failures.append(what)


def w(name, b):
    p = os.path.join(TMP, name)
    open(p, "wb").write(b)
    return p


def rgba_of(b):
    return np.array(Image.open(io.BytesIO(b)).convert("RGBA")).astype(int)


def c_pixels(png, patch, with_mode=False):
    out = os.path.join(TMP, "out.png")
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run([EXE, "pixels", w("in.png", png), w("patch", patch), out], capture_output=True)
    f = (r.stdout.decode().strip() or "-9 0").split()
    code, mode = int(f[0]), int(f[1]) if len(f) > 1 else 0
    got = open(out, "rb").read() if code == 0 else None
    return (code, got, mode) if with_mode else (code, got)


def c_png(png):
    out = os.path.join(TMP, "back.png")
    r = subprocess.run([EXE, "png", w("in2.png", png), out], capture_output=True)
    return open(out, "rb").read() if r.stdout.decode().startswith("0") else None


# ---- PNG forms PIL does not write: 16-bit and Adam7, by hand
def png_raw(width, height, ctype, depth, rows_bytes, interlace=False):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    ihdr = struct.pack(">IIBBBBB", width, height, depth, ctype, 0, 0, 1 if interlace else 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(rows_bytes, 9)) + chunk(b"IEND", b"")


def png16_rgba(arr8):
    h, wd, _ = arr8.shape
    a16 = (arr8.astype(np.uint16) * 257).astype(">u2")
    rows = b"".join(b"\x02" if y else b"\x00" for y in range(0))  # placeholder
    out = bytearray()
    prev = None
    for y in range(h):
        row = a16[y].tobytes()
        if prev is None:
            out += b"\x00" + row
        else:                                       # filter 2 (up), to exercise the filters
            out += b"\x02" + bytes((row[i] - prev[i]) & 255 for i in range(len(row)))
        prev = row
    return png_raw(wd, h, 6, 16, bytes(out))


def png_adam7_rgba(arr8):
    h, wd, _ = arr8.shape
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    out = bytearray()
    for k, (px, py, sx, sy) in enumerate(passes):
        sub = arr8[py::sy, px::sx]
        if sub.shape[0] == 0 or sub.shape[1] == 0:
            continue
        prev = None
        for y in range(sub.shape[0]):
            row = sub[y].astype(np.uint8).tobytes()
            if prev is None or k % 2:
                out += b"\x00" + row
            else:                                   # filter 4 (Paeth) on some passes
                bpp = 4
                f = bytearray(len(row))
                for i in range(len(row)):
                    a = row[i - bpp] if i >= bpp else 0
                    b = prev[i]
                    c = prev[i - bpp] if i >= bpp else 0
                    p = a + b - c
                    pa_, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if pa_ <= pb and pa_ <= pc else (b if pb <= pc else c)
                    f[i] = (row[i] - pr) & 255
                out += b"\x04" + bytes(f)
            prev = row
    return png_raw(wd, h, 6, 8, bytes(out), interlace=True)


def forms(arr8):
    """the mod's sheet in every form: (name, png bytes, the RGBA libpng would give)"""
    im = Image.fromarray(arr8.astype(np.uint8), "RGBA")
    out = []
    b = io.BytesIO(); im.save(b, "PNG"); out.append(("rgba8", b.getvalue()))
    rgb = im.convert("RGB")
    b = io.BytesIO(); rgb.save(b, "PNG"); out.append(("rgb8", b.getvalue()))
    b = io.BytesIO(); rgb.save(b, "PNG", transparency=(0, 0, 0)); out.append(("rgb8+trns", b.getvalue()))
    pal = im.convert("RGBA").quantize(colors=200, method=Image.FASTOCTREE)
    b = io.BytesIO(); pal.save(b, "PNG"); out.append(("palette+trns", b.getvalue()))
    gray = im.convert("L")
    b = io.BytesIO(); gray.save(b, "PNG"); out.append(("gray8", b.getvalue()))
    la = im.convert("LA")
    b = io.BytesIO(); la.save(b, "PNG"); out.append(("gray+alpha", b.getvalue()))
    out.append(("rgba16-up", png16_rgba(arr8)))
    out.append(("rgba8-adam7", png_adam7_rgba(arr8)))
    return out


# ---- renders of the main menu (04.10.2026): what Idle draws at its first tick - the game's gamemenu.anm2 with
# the port's patch, over a backdrop that stands for whatever is behind the menu (a hole shows it)
RENDERS = os.path.join(pa.ROOT, "разбор", "2026-10-04", "моды-рантайм-рендеры")
FIXTURE = os.path.join(HERE, "fixtures", "reflashed-converted-gamemenu.png")


def menu_render(sheet_rgba, set_name="data"):
    import effective as ef
    import anm2patch as ap
    t = "repentance-resources/%s/gfx/ui/main menu/gamemenu.anm2" % set_name
    d = ap.parse(ef.After().get(t))
    m = ap.Model(d)
    sheets = {x.get("Id"): x.get("Path") for x in m.sheets.kids if x.tag == "Spritesheet"}
    lsheet = {x.get("Id"): sheets[x.get("SpritesheetId")] for x in m.layers.kids if x.tag == "Layer"}
    strap = rgba_of(open(os.path.join(pp.OUT, "repentance-resources", set_name, "gfx", "ui", "main_menu",
                                      "port_strap_credits.png"), "rb").read())
    canvas = np.zeros((270, 480, 4), float)
    canvas[:, :] = (255, 0, 255, 255)                       # behind the menu: magenta - a hole shows it
    canvas[::8, :, :3] = (40, 40, 40)
    anim = next(a for a in d.root.find("Animations").kids if a.get("Name") == "Idle")
    for la in anim.find("LayerAnimations").kids:
        if la.get("Visible") == "false":
            continue
        f = next((x for x in la.kids if x.tag == "Frame"), None)
        if f is None or f.get("Visible") == "false":
            continue
        sp = lsheet[la.get("LayerId")].lower()
        if sp == "gamemenu.png":
            src = sheet_rgba
        elif sp == "port_strap_credits.png":
            src = strap
        else:                                               # the game's other sheets, as the pack has them
            other = os.path.join(pa.APP, "repentance-resources", "data", "gfx", "ui", "main menu", sp)
            if not os.path.isfile(other):
                continue
            src = rgba_of(open(other, "rb").read())
        x0 = int(float(f.get("XPosition"))) - int(float(f.get("XPivot") or 0))
        y0 = int(float(f.get("YPosition"))) - int(float(f.get("YPivot") or 0))
        cx, cy, cw, ch = (int(float(f.get(k))) for k in ("XCrop", "YCrop", "Width", "Height"))
        crop = src[cy:cy + ch, cx:cx + cw].astype(float)
        for yy in range(crop.shape[0]):
            Y = y0 + yy
            if not 0 <= Y < 270:
                continue
            for xx in range(crop.shape[1]):
                X = x0 + xx
                if 0 <= X < 480:
                    a = crop[yy, xx, 3] / 255.0
                    canvas[Y, X, :3] = canvas[Y, X, :3] * (1 - a) + crop[yy, xx, :3] * a
    return canvas.astype(np.uint8)


def renders(patch, van_png, over_png):
    os.makedirs(RENDERS, exist_ok=True)
    v = rgba_of(van_png)
    rec = v.copy()
    rec[:, :, 0], rec[:, :, 2] = v[:, :, 2], v[:, :, 0]
    cases = [("vanilla", van_png, v)]
    b = io.BytesIO(); Image.fromarray(rec.astype(np.uint8), "RGBA").save(b, "PNG")
    cases.append(("recolour", b.getvalue(), rec))
    ref = open(FIXTURE, "rb").read()
    cases.append(("reflashed", ref, rgba_of(ref)))
    w_, h_, recs = pp.pixels_records(patch)
    foot = np.zeros((h_, w_), bool)
    allpx = np.zeros((h_, w_), bool)
    for x, y, *rest in recs:
        allpx[y, x] = True
        if rest[-1] & 1:
            foot[y, x] = True
    tiles = []
    for name, png, rgba in cases:
        code, got, mode = c_pixels(png, patch, with_mode=True)
        py, py_mode = pp.pixels_apply(rgba, patch)
        g = rgba_of(got)
        check(code == 0 and mode == py_mode and np.array_equal(g, py), "%-9s C == Python, mode %d (%s)"
              % (name, mode, "whole" if mode == 2 else "the strap's footprint only"))
        if name == "reflashed":
            outside = allpx & ~foot
            check(mode == 1 and np.array_equal(g[outside], rgba[outside]),
                  "reflashed: its own background kept around the strap (%d px not cleared)" % int(outside.sum()))
            check(np.array_equal(g[foot], rgba_of(over_png)[foot]), "reflashed: the strap's place cleared (%d px)" % int(foot.sum()))
        else:
            check(mode == 2, "%s: the whole retouch" % name)
        before = menu_render(rgba if name != "vanilla" else rgba)          # no retouch: the mod's sheet as it is
        old = menu_render(pp.pixels_apply(rgba, patch, mode=2)[0])          # 02.10: the whole retouch always
        now = menu_render(g)
        # the strap shows where it lies: its opaque pixels inside the footprint are drawn
        hole = (now[:, :, 0] == 255) & (now[:, :, 1] == 0) & (now[:, :, 2] == 255)
        hole_old = (old[:, :, 0] == 255) & (old[:, :, 1] == 0) & (old[:, :, 2] == 255)
        hole_before = (before[:, :, 0] == 255) & (before[:, :, 1] == 0) & (before[:, :, 2] == 255)
        new_holes = int((hole & ~hole_before).sum())
        print("        %-9s backdrop pixels: as the mod has it %d, retouch always (02.10) %d, the rule %d"
              % (name, int(hole_before.sum()), int(hole_old.sum()), int(hole.sum())))
        check(new_holes == 0 or name != "reflashed", "%s: the rule opens no hole the mod's sheet has not" % name)
        tiles.append((name, before, old, now))
    W, H = 480, 270
    img = Image.new("RGB", (W * 3 + 20, (H + 16) * len(tiles)), (30, 30, 30))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(img)
    for i, (name, a, b2, c) in enumerate(tiles):
        for j, (lab, im) in enumerate((("mod's sheet, no retouch", a), ("retouch always (02.10)", b2), ("the rule (04.10)", c))):
            img.paste(Image.fromarray(im[:, :, :3]), (j * (W + 10), i * (H + 16) + 14))
            dr.text((j * (W + 10) + 4, i * (H + 16)), "%s: %s" % (name, lab), fill=(230, 230, 230))
    out = os.path.join(RENDERS, "strap-pixels.png")
    img.save(out)
    print("        render: " + out)


def main():
    items, _ = pp.plan()
    pix = [(t, data) for kind, t, a, data, e in items if kind == "pixels"]
    overs = {t: data for kind, t, a, data, e in items if kind == "over"}
    check(len(pix) == 7, "pixels for gamemenu.png in all seven sets: %d" % len(pix))
    for t, patch in pix:
        print("\n" + t)
        van = pa.vanilla_bytes(t)
        # 1. vanilla + pixels == the "over" sheet
        code, got = c_pixels(van, patch)
        check(code == 0 and np.array_equal(rgba_of(got), rgba_of(overs[t])), "vanilla + pixels == the port's sheet (C)")
        py_out, py_mode = pp.pixels_apply(rgba_of(van), patch)
        check(py_mode == 2 and np.array_equal(py_out, rgba_of(overs[t])), "vanilla + pixels == the port's sheet (Python, whole)")
        if not t.split("/")[1] in ("data", "dlc3.de"):
            continue
        # 2. a mod's recolour of the game's sheet, every form
        v = rgba_of(van)
        mod = v.copy()
        mod[:, :, 0], mod[:, :, 2] = v[:, :, 2], v[:, :, 0]         # red and blue swapped: a recolour
        mod[:, :, :3] = np.clip(mod[:, :, :3] * 0.9 + 12, 0, 255).astype(int)
        mask = (rgba_of(overs[t]) != v).any(axis=2)
        for name, png in forms(mod):
            # what libpng gives for it: the reader alone, and PIL (16-bit and Adam7: the 8-bit picture itself)
            want_in = mod if name in ("rgba16-up", "rgba8-adam7") else rgba_of(png)
            back = c_png(png)
            same_read = back is not None and np.array_equal(rgba_of(back), want_in)
            code, got, mode = c_pixels(png, patch, with_mode=True)
            if code != 0:
                check(False, "%s: patched (code %d)" % (name, code))
                continue
            g = rgba_of(got)
            want, py_mode = pp.pixels_apply(want_in, patch)
            check(same_read and mode == py_mode and np.array_equal(g, want),
                  "%-13s read as libpng would, and C == Python on every pixel (mode %d)" % (name, mode))
            # a sheet with no alpha is opaque where the game's is clear: its own background - only the footprint
            opaque = name in ("rgb8", "gray8") or (name == "rgb8+trns")
            if opaque:      # its own black where the game's is clear: decided by the share like any other
                print("        %-13s opaque where the game's is clear: %s" % (name, "the whole retouch" if mode == 2 else "the strap's place only"))
            else:
                check(mode == 2, "%-13s the game's art kept: the whole retouch" % name)
            # what Main draws (0,0 480x270): the mod's but at the cleared pixels, the port's there
            w_, h_, recs = pp.pixels_records(patch)
            cleared = np.zeros(mask.shape, bool)
            for x, y, *rest in recs:
                if mode == 2 or rest[-1] & 1:
                    cleared[y, x] = True
            crop = (slice(0, 270), slice(0, 480))
            outside = ~cleared[crop]
            check(np.array_equal(g[crop][outside], want_in[crop][outside]) and
                  np.array_equal(g[crop][cleared[crop]], rgba_of(overs[t])[crop][cleared[crop]]),
                  "%-13s Main draws the mod's picture, the cleared place the port's (%d px)" % (name, int(cleared[crop].sum())))
    # 2a. the rule's renders: vanilla, a recolour, Reflashed (converted: its own background at the game's size)
    print("\nrenders")
    data = [x for x in pix if x[0].split("/")[1] == "data"][0]
    renders(data[1], pa.vanilla_bytes(data[0]), overs[data[0]])
    # 3. another size: left alone
    with zipfile.ZipFile(REFLASHED) as z:
        big = z.read("resources/gfx/ui/main menu/gamemenu.png")
    code, _ = c_pixels(big, pix[0][1])
    check(code == 1, "Reflashed's 4x gamemenu.png (%s): not the game's size - left alone (%d)" % (Image.open(io.BytesIO(big)).size, code))
    code, _ = c_pixels(b"not a png", pix[0][1])
    check(code == -1, "not a PNG: -1")
    # 4. the reader on the pack's own sheets of every colour type
    seen = {}
    for root, dirs, files in os.walk(os.path.join(pa.APP, "repentance-resources", "data", "gfx", "ui")):
        for f in files:
            if not f.endswith(".png"):
                continue
            p = os.path.join(root, f)
            b = open(p, "rb").read()
            kind = (b[24], b[25], b[28]) if len(b) > 29 else None
            if kind in seen and seen[kind] >= 3:
                continue
            seen[kind] = seen.get(kind, 0) + 1
            back = c_png(b)
            check(back is not None and np.array_equal(rgba_of(back), rgba_of(b)),
                  "reader: %s (depth %s, colour type %s, interlace %s)" % (os.path.relpath(p, pa.APP), *kind))
    print("\n%d failures" % len(failures))
    for f in failures:
        print("  - " + f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
