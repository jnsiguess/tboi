# -*- coding: utf-8 -*-
"""What the game would load, before M8 and after it - and is it the same picture (plan 30.09, 4.9 M8).

BEFORE  the port's files as they were packed: the port's version of every
        registered file (work/port-edit, else work/app), the rest of work/app.
AFTER   vanilla (the IPA) for every game file, with port_patch/ laid over it
        exactly as portpatch.c does at start: files and "over" files put in,
        .anm2 patches applied (tools/anm2patch.py), the stick size variants
        built (tools/port_patch.py variant()).

The comparison is of what is DRAWN, not of bytes: for every .anm2 involved,
every animation, every frame tick, the ordered list of draws - each visible
layer frame with the actual pixels of its crop (from the sheet the game would
resolve), its position, pivot, scale, rotation, tint, offset and the next key
when interpolated - and the null frames. Two files that draw the same thing
the same way agree, whatever their layer numbers. The numbers are compared on
their own: every layer and null of BEFORE must keep its number AFTER (the
port's code still uses some of them as constants).

Involved = every patched target, every size variant, and every .anm2 of the
pack that shows a sheet whose pixels differ between BEFORE and AFTER.

  py -3 tools/effective.py            compare; exit 1 on any difference
  py -3 tools/effective.py --verbose  print every file compared
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anm2patch as ap
import port_additions as pa
import port_patch as pp

from PIL import Image
import numpy as np


# ================================================================== views


class View:
    def __init__(self, name):
        self.name = name
        self._img = {}

    def get(self, path):
        raise NotImplementedError

    def image(self, path):
        if path not in self._img:
            b = self.get(path)
            self._img[path] = None if b is None else np.array(Image.open(io.BytesIO(b)).convert("RGBA"))
        return self._img[path]


_app_names = {}


def app_lookup(path):
    """the work/app file of a game path, case ignored (None: not there)"""
    folder, leaf = path.rsplit("/", 1)
    if folder not in _app_names:
        d = os.path.join(pa.APP, *folder.split("/"))
        _app_names[folder] = {f.lower(): f for f in os.listdir(d)} if os.path.isdir(d) else {}
    f = _app_names[folder].get(leaf.lower())
    return os.path.join(pa.APP, *folder.split("/"), f) if f else None


_reg = None


def registered():
    global _reg
    if _reg is None:
        _reg = {e["path"].lower(): e["path"] for e in pa.entries()}
    return _reg


class Before(View):
    def __init__(self):
        View.__init__(self, "before")

    def get(self, path):
        r = registered().get(path.lower())
        if r:
            return pa.port_bytes(r)
        p = app_lookup(path)
        if p and pa.TAIL.search(p) is None:
            # a game file not registered: as packed (work/app) - and it must be vanilla
            return open(p, "rb").read()
        return None


class After(View):
    """vanilla + port_patch/ as portpatch.c lays it"""

    def __init__(self):
        View.__init__(self, "after")
        self.put = {}                       # lower path -> bytes
        self.reports = []
        idx = os.path.join(pp.OUT, pp.INDEX)
        lines = open(idx, encoding="utf-8").read().splitlines()
        for ln in lines:
            if not ln or ln.startswith("#"):
                continue
            f = ln.split("\t")
            if f[0] in ("file", "over"):
                self.put[f[1].lower()] = self._asset(f[2])
        for ln in lines:
            f = ln.split("\t")
            if f[0] == "patch":
                base = self._base(f[3] if len(f) > 3 else f[1])
                if base is None:
                    self.reports.append("patch %s: no base" % f[1])
                    continue
                rep = []
                self.put[f[1].lower()] = ap.apply(base, self._asset(f[2]), rep)
                self.reports += ["patch %s: %s" % (f[1], r) for r in rep]
        for ln in lines:
            f = ln.split("\t")
            if f[0] == "variants":
                base = self.get(f[1])
                folder = f[1].rsplit("/", 1)[0]
                for pct in f[2].split(","):
                    self.put[("%s/ios_buttons_%s.anm2" % (folder, pct)).lower()] = pp.variant(base, int(pct))

    @staticmethod
    def _asset(name):
        return open(os.path.join(pp.OUT, *name[len("port_patch/"):].split("/")), "rb").read()

    def _base(self, path):
        if path.lower() in self.put:
            return self.put[path.lower()]
        return self._vanilla(path)

    _vlow = None

    @classmethod
    def _vanilla(cls, path):
        if cls._vlow is None:
            cls._vlow = {k.lower(): k for k in pa.vanilla_index()}
        k = cls._vlow.get(path.lower())
        return pa.vanilla_bytes(k) if k else None

    def get(self, path):
        b = self.put.get(path.lower())
        if b is not None:
            return b
        b = self._vanilla(path)
        if b is not None:
            return b
        # a file of the pack that is neither the IPA's nor laid by the port: still packed as is
        p = app_lookup(path)
        if p and path.lower() not in registered():
            return open(p, "rb").read()
        return None


# ================================================================== drawing


def resolve_sheet(view, anm2_path, rel):
    for c in pp.sheet_path(anm2_path, rel):
        if view.get(c) is not None:
            return c
    return None


FRAME_KEYS = ("XPosition", "YPosition", "XPivot", "YPivot", "XScale", "YScale", "Rotation",
              "RedTint", "GreenTint", "BlueTint", "AlphaTint", "RedOffset", "GreenOffset", "BlueOffset")


def _f(n, k):
    v = n.get(k)
    try:
        return round(float(v), 4) if v is not None else None
    except ValueError:
        return v


def _at(frames, t):
    """(frame, next frame) shown at tick t"""
    at = 0
    for i, f in enumerate(frames):
        d = int(float(f.get("Delay") or 1))
        if t < at + d:
            return f, (frames[i + 1] if i + 1 < len(frames) else None), t - at
        at += d
    return None, None, 0


def draws(view, path, b):
    """{anim: [tick: (draw list, null list)]}, {layer name: id}, {null name: id}"""
    d = ap.parse(b)
    m = ap.Model(d)
    sheets = {x.get("Id"): x.get("Path") for x in m.sheets.kids if x.tag == "Spritesheet"}
    layers = {x.get("Id"): (x.get("Name"), x.get("SpritesheetId")) for x in m.layers.kids if x.tag == "Layer"}
    nulls = {x.get("Id"): x.get("Name") for x in m.nulls.kids if x.tag == "Null"}
    crop_cache = {}
    out = {}
    for a in m.anims.kids:
        if a.tag != "Animation":
            continue
        n = int(float(a.get("FrameNum") or 1))
        ticks = []
        las = a.find("LayerAnimations")
        nas = a.find("NullAnimations")
        root = [f for f in (a.find("RootAnimation").kids if a.find("RootAnimation") is not None else []) if f.tag == "Frame"]
        for t in range(max(n, 1)):
            rf, rn, rt = _at(root, t)
            rkey = (tuple(_f(rf, k) for k in FRAME_KEYS), rf.get("Visible") if rf is not None else None) if rf is not None else None
            dl = []
            for la in (las.kids if las is not None else []):
                if la.tag != "LayerAnimation" or la.get("Visible") == "false":
                    continue
                lname, sid = layers.get(la.get("LayerId"), (None, None))
                frames = [f for f in la.kids if f.tag == "Frame"]
                f, nxt, into = _at(frames, t)
                if f is None or f.get("Visible") == "false":
                    continue
                try:
                    x, y, w, h = (int(float(f.get(k))) for k in ("XCrop", "YCrop", "Width", "Height"))
                except (TypeError, ValueError):
                    continue
                spath = resolve_sheet(view, path, sheets.get(sid, ""))
                key = (spath, x, y, w, h)
                if key not in crop_cache:
                    img = view.image(spath) if spath else None
                    if img is None or w <= 0 or h <= 0:
                        crop_cache[key] = None
                    else:
                        c = np.zeros((h, w, 4), np.uint8)
                        H, W = img.shape[:2]
                        x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
                        if x1 > x0 and y1 > y0:
                            c[y0 - y:y1 - y, x0 - x:x1 - x] = img[y0:y1, x0:x1]
                        c[c[:, :, 3] == 0] = 0
                        crop_cache[key] = c.tobytes() if c[:, :, 3].any() else None
                pix = crop_cache[key]
                if pix is None:
                    continue                    # nothing to see
                interp = None
                if f.get("Interpolated") == "true" and nxt is not None:
                    interp = (tuple(_f(nxt, k) for k in FRAME_KEYS), into)
                dl.append((hash(pix), w, h, tuple(_f(f, k) for k in FRAME_KEYS), interp))
            nl = []
            for na in (nas.kids if nas is not None else []):
                if na.tag != "NullAnimation":
                    continue
                frames = [f for f in na.kids if f.tag == "Frame"]
                f, nxt, into = _at(frames, t)
                nl.append((nulls.get(na.get("NullId")), na.get("Visible"),
                           tuple(_f(f, k) for k in FRAME_KEYS + ("Visible",)) if f is not None else None))
            ticks.append((rkey, tuple(dl), tuple(sorted(nl, key=str))))
        out[a.get("Name")] = ticks
    ids_l = {nm: int(i) for i, (nm, s) in layers.items()}
    ids_n = {nm: int(i) for i, nm in nulls.items()}
    return out, ids_l, ids_n


# ================================================================== compare


def sheets_that_differ(before, after, paths):
    out = []
    for p in paths:
        a, b = before.get(p), after.get(p)
        if a == b:
            continue
        if a is None or b is None:
            out.append(p)
            continue
        ia, ib = before.image(p), after.image(p)
        if ia.shape != ib.shape:
            out.append(p)
            continue
        ma, mb = ia.copy(), ib.copy()
        ma[ma[:, :, 3] == 0] = 0
        mb[mb[:, :, 3] == 0] = 0
        if not np.array_equal(ma, mb):
            out.append(p)
    return out


def all_anm2_showing(sheet_path):
    """every .anm2 of work/app whose sheet list resolves to this sheet"""
    ed, st, rel = pp._set_of(sheet_path)
    leaf = rel.rsplit("/", 1)[1].lower()
    found = []
    roots = [os.path.join(pa.APP, ed, st)]
    if st != "data":
        pass
    for dp, dn, fn in os.walk(os.path.join(pa.APP, ed)):
        for f in fn:
            if not f.lower().endswith(".anm2") or pa.TAIL.search(f):
                continue
            full = os.path.join(dp, f)
            try:
                b = open(full, "rb").read()
            except OSError:
                continue
            if leaf.encode() not in b.lower():
                continue
            path = os.path.relpath(full, pa.APP).replace("\\", "/")
            for c in pp.sheet_path(path, ""):
                pass
            # does a sheet of it resolve to sheet_path?
            try:
                m = ap.Model(ap.parse(b))
            except Exception:
                continue
            for s in m.sheets.kids:
                if s.tag == "Spritesheet" and (s.get("Path") or "").lower().endswith(leaf):
                    for c in pp.sheet_path(path, s.get("Path")):
                        if c.lower() == sheet_path.lower():
                            found.append(path)
    return sorted(set(found))


def main(argv):
    verbose = "--verbose" in argv
    before, after = Before(), After()
    ok = True
    for r in after.reports:
        print("  REPORT", r)
        ok = False
    # files and over: bytes
    targets = []
    for ln in open(os.path.join(pp.OUT, pp.INDEX), encoding="utf-8").read().splitlines():
        f = ln.split("\t")
        if f[0] in ("file", "over"):
            if f[1].lower() not in registered():
                continue                    # a port sheet M8 made: judged by what is drawn from it
            same = before.get(f[1]) == after.get(f[1])
            ok &= same
            if verbose or not same:
                print("  %-5s %-70s %s" % (f[0], f[1], "same" if same else "DIFFERENT"))
        elif f[0] == "patch":
            targets.append(f[1])
        elif f[0] == "variants":
            folder = f[1].rsplit("/", 1)[0]
            targets += ["%s/ios_buttons_%s.anm2" % (folder, p) for p in f[2].split(",")]
    # registered game files the port no longer lays: what does the game show of them?
    sheets = []
    for e in pa.entries():
        p = e["path"]
        if p.endswith(".png"):
            sheets.append(p)
        elif p.endswith(".anm2") and p not in targets:
            print("  UNCOVERED anm2", p)
            ok = False
        elif not p.endswith((".png", ".anm2")) and after.get(p) != before.get(p):
            note = " (content.c fills it at start - content_sim --verify)" if p.endswith(".sta") else ""
            print("  differs %-60s%s" % (p, note))
            if not note:
                ok = False
    for s in sheets_that_differ(before, after, sheets):
        shows = all_anm2_showing(s)
        print("  sheet differs: %s - shown by %d anm2" % (s, len(shows)))
        targets += [x for x in shows if x not in targets]
    for t in targets:
        bb, ab = before.get(t), after.get(t)
        if bb is None or ab is None:
            print("  %-70s MISSING (%s)" % (t, "before" if bb is None else "after"))
            ok = False
            continue
        db, lb, nb = draws(before, t, bb)
        da, la, na = draws(after, t, ab)
        probs = []
        for name, i in lb.items():
            if la.get(name) != i:
                probs.append("layer %s %s -> %s" % (name, i, la.get(name)))
        for name, i in nb.items():
            if na.get(name) != i:
                probs.append("null %s %s -> %s" % (name, i, na.get(name)))
        if set(db) != set(da):
            probs.append("animations %s" % sorted(set(db) ^ set(da)))
        ticks = 0
        for an in db:
            if an not in da:
                continue
            if len(db[an]) != len(da[an]):
                probs.append("%s: %d ticks -> %d" % (an, len(db[an]), len(da[an])))
                continue
            for t_, (x, y) in enumerate(zip(db[an], da[an])):
                ticks += 1
                if x != y:
                    probs.append("%s tick %d draws differently" % (an, t_))
                    break
        ok &= not probs
        if verbose or probs:
            print("  %-70s %s" % (t, "same (%d animations, %d ticks)" % (len(db), ticks) if not probs else "; ".join(probs[:6])))
    print("EFFECTIVE", "SAME" if ok else "DIFFERENT")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
