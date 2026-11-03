# -*- coding: utf-8 -*-
"""The port's additions to the game's files as patches and port files (plan 30.09, 4.9 M8).

Input: the registry (port_additions.py) - the port's version of every game
file it changes or adds (work/port-edit, else work/app) and vanilla (the IPA).
Output: port/app/src/main/assets/port_patch/ - what portpatch.c lays over the
pack at start, and index.txt that says what goes where:

    patch    <target> <asset> [<base>]   an .anm2 patch (tools/anm2patch.py) over
                                         the file the game would load at <target>
                                         (the pack's, or <base> when vanilla has
                                         no such file - jp/kr read data's)
    over     <target> <asset>            the port's version of a GAME file (a sheet
                                         the port retouched, a shader): put over
                                         the pack's; a mod's own file still wins,
                                         mods are mapped before the pack is asked
    file     <target> <asset>            a file of the port's own (its sheets, the
                                         language copies the IPA has not got)
    variants <base> <pct,pct,...>        the stick size variants ios_buttons_<pct>
                                         .anm2, built from the patched <base> (M9)
    pixels   <target> <asset>            the port's retouch of a game sheet as pixels,
                                         laid over a MOD's sheet of the game's size
                                         (the game's own gets the "over" file)
    inpak    <target>                    a port's version the pak itself still
                                         carries (nothing to lay: the export's
                                         registry, GameFiles.java, M12)

The order in the file is the order of work: files first (the patches' sheets),
then patches, then variants.

WHERE A PICTURE OF THE PORT GOES (measured 02.10.2026, see the batch 5 notes, M8):
  - pixels the port added where the game's sheet is empty, looked at by the
    port's own frames: the port's layers move to a PORT SHEET (port_<sheet>,
    the port's whole version of it); a game layer's frames in an animation the
    port changed move to a clone layer "Port <layer>" on that sheet, drawn
    right after it (the frame left on the game layer is made invisible, so the
    timing stays). A mod's sheet then cannot take the port's pictures away;
  - pixels the port changed that the game's own frames show (a retouch): the
    port's version of the sheet goes "over" - while no mod brings its own;
  - pixels nobody looks at: left behind.

Usage:
  py -3 tools/port_patch.py --make     write the tree and index.txt
  py -3 tools/port_patch.py --check    the tree on disk is what --make would write
  py -3 tools/port_patch.py --plan     print the decisions only
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anm2patch as ap
import port_additions as pa

OUT = os.path.join(pa.ROOT, "port", "app", "src", "main", "assets", "port_patch")
INDEX = "index.txt"

# The stick size variants (M9): every frame of a LayerAnimation of these
# animations gets XScale/YScale times pct/100 - measured on all twelve files the
# port carried (tools/dpad_art.py, twin_buttons.py made them): exactly these.
VARIANT_PCTS = (100, 105, 110, 115, 120, 125, 130, 135, 140, 150, 170, 175)
VARIANT_BASE = "rebirth-resources/data/gfx/ui/touch/ios_buttons.anm2"


def scaled_anim(name):
    return name.startswith("Analog ") and "Twin" not in name


def scale_number(raw, pct):
    """XScale/YScale times pct/100, written as portpatch.c writes it"""
    v = float(raw)
    f = v * pct / 100.0
    if abs(f - round(f)) < 1e-9:
        return "%d" % int(round(f))
    s = "%.4f" % f
    return s.rstrip("0").rstrip(".")


def variant(base, pct):
    d = ap.parse(base)
    for a in d.root.find("Animations").kids:
        if a.tag != "Animation" or not scaled_anim(a.get("Name") or ""):
            continue
        las = a.find("LayerAnimations")
        for la in (las.kids if las is not None else []):
            for f in la.kids:
                if f.tag != "Frame":
                    continue
                for k in ("XScale", "YScale"):
                    if f.get(k) is not None:
                        f.set(k, scale_number(f.get(k), pct))
    return ap.serialize(d)


# ================================================================ pictures


def _img(b):
    from PIL import Image
    import numpy as np
    return np.array(Image.open(io.BytesIO(b)).convert("RGBA")).astype(int)


def _set_of(path):
    """('repentance-resources', 'dlc3.ru', 'gfx/ui/...')"""
    ed, st, rel = path.split("/", 2)
    return ed, st, rel


def _lookup(index_paths, path):
    """case-insensitive key of path in a collection of paths"""
    low = path.lower()
    for k in index_paths:
        if k.lower() == low:
            return k
    return None


def sheet_path(anm2_path, sheet_rel):
    """the game's path of a sheet named in an anm2: same set, else the edition's data"""
    ed, st, rel = _set_of(anm2_path)
    folder = rel.rsplit("/", 1)[0]
    cand = ["%s/%s/%s/%s" % (ed, st, folder, sheet_rel)]
    if st != "data":
        cand.append("%s/data/%s/%s" % (ed, folder, sheet_rel))
    return cand


def _vanilla_sheet(cands):
    van = pa.vanilla_index()
    for c in cands:
        k = _lookup(van, c)
        if k:
            return k, pa.vanilla_bytes(k)
    return None, None


def _port_sheet(cands):
    for c in cands:
        ed, st, rel = _set_of(c)
        for root in (pa.PORT_EDIT, pa.APP):
            d = os.path.join(root, ed, st, *rel.split("/")[:-1])
            if os.path.isdir(d):
                leaf = rel.split("/")[-1].lower()
                for f in os.listdir(d):
                    if f.lower() == leaf:
                        return "%s/%s/%s/%s" % (ed, st, "/".join(rel.split("/")[:-1]), f), open(os.path.join(d, f), "rb").read()
    return None, None


class SheetDiff:
    """the port's version of one game sheet against vanilla"""

    def __init__(self, path, van, cur):
        import numpy as np
        self.path, self.van, self.cur = path, van, cur
        a, b = _img(van), _img(cur)
        h, w = b.shape[:2]
        hv, wv = a.shape[:2]
        self.size, self.vsize = (w, h), (wv, hv)
        changed = np.zeros((h, w), bool)
        added = np.zeros((h, w), bool)
        hh, ww = min(h, hv), min(w, wv)
        aa, bb = a[:hh, :ww], b[:hh, :ww]
        diff = (aa != bb).any(axis=2) & ~((aa[:, :, 3] == 0) & (bb[:, :, 3] == 0))
        changed[:hh, :ww] = diff
        added[:hh, :ww] = diff & (aa[:, :, 3] == 0)
        outside = np.ones((h, w), bool)
        outside[:hh, :ww] = False
        changed |= outside & (b[:, :, 3] > 0)
        added |= outside & (b[:, :, 3] > 0)
        self.changed, self.added = changed, added

    def touches(self, mask, f):
        try:
            x, y, w, h = (int(float(f.get(k))) for k in ("XCrop", "YCrop", "Width", "Height"))
        except (TypeError, ValueError):
            return False
        if w <= 0 or h <= 0:
            return False
        return bool(mask[max(0, y):max(0, y + h), max(0, x):max(0, x + w)].any())


# Layers whose frame the game's code sets by number (the value of an option row,
# a slider, the cursor): a clone on a port sheet would not follow that frame, so
# their frames stay where they are and the sheet goes "over" instead.
DRIVEN = {"OnOff", "Adjustor1", "Cursor"}

# Files whose game layers may get clones on a port sheet. ios_buttons.anm2 may
# not: the game's touch code drives the native buttons' layers, and the PC has
# no ios_buttons.png a mod could bring - the port's pictures there go "over".
CLONES = {"optionsmenu.anm2": True, "gamemenu.anm2": True, "pausescreen.anm2": True, "ios_buttons.anm2": False}


def port_sheet_name(sheet_rel):
    folder, leaf = (sheet_rel.rsplit("/", 1) if "/" in sheet_rel else ("", sheet_rel))
    return (folder + "/" if folder else "") + "port_" + leaf.lower()


def plan_anm2(path):
    """the patch of one registered anm2 and what it needs: (patch bytes, needs, notes)

    needs: {"port": {game sheet path: port sheet target path}, "over": {game sheet path}}"""
    base = pa.base_of(path)
    van = pa.vanilla_bytes(base)
    cur = pa.port_bytes(path)
    c = ap.Model(ap.parse(cur))
    v = ap.Model(ap.parse(van))
    plain = ap.parse(ap.make(van, cur)).root
    changed_parts = {}                       # (anim, layer name) -> part node in the plain patch
    for op in plain.kids:
        if op.tag == "Animation":
            for part in op.kids:
                if part.tag == "LayerAnimation":
                    changed_parts[(op.get("Name"), part.get("Layer"))] = part
    vlayer_names = {x.get("Name") for x in v.layers.kids if x.tag == "Layer"}
    csheets = {x.get("Id"): x.get("Path") for x in c.sheets.kids if x.tag == "Spritesheet"}
    vsheet_paths = {(x.get("Path") or "").lower() for x in v.sheets.kids if x.tag == "Spritesheet"}
    diffs = {}
    for sid, rel in csheets.items():
        if rel.lower() not in vsheet_paths:
            continue                          # the port's own sheet
        cands = sheet_path(path, rel)
        vk, vb = _vanilla_sheet(cands)
        pk, pb = _port_sheet(cands)
        if vb is None or pb is None or vb == pb:
            continue
        diffs[sid] = SheetDiff(pk, vb, pb)
    rt = {"sheets": [], "layers": {}, "clones": {}}
    needs = {"port": {}, "over": set()}
    notes = []
    layers = {x.get("Id"): x for x in c.layers.kids if x.tag == "Layer"}
    port_paths = {}
    for sid, d in diffs.items():
        port_paths[sid] = port_sheet_name(csheets[sid])
    used_port = set()
    over = set()
    for a in c.anims.kids:
        if a.tag != "Animation":
            continue
        an = a.get("Name")
        las = a.find("LayerAnimations")
        for la in (las.kids if las is not None else []):
            lay = layers.get(la.get("LayerId"))
            if lay is None:
                continue
            sid = lay.get("SpritesheetId")
            d = diffs.get(sid)
            if d is None:
                continue
            lname = lay.get("Name")
            frames = [f for f in la.kids if f.tag == "Frame"]
            if lname not in vlayer_names:
                if any(d.touches(d.changed, f) for f in frames):
                    rt["layers"][lname] = port_paths[sid]
                    used_port.add(sid)
                continue
            part = changed_parts.get((an, lname))
            may_clone = CLONES.get(path.rsplit("/", 1)[1].lower(), False) and lname not in DRIVEN
            moved = [i for i, f in enumerate(frames) if may_clone and part is not None and d.touches(d.added, f)]
            if moved:
                rt["clones"][(an, lname)] = ("Port " + lname, port_paths[sid], moved)
                used_port.add(sid)
            shown = [i for i, f in enumerate(frames) if i not in moved and d.touches(d.changed, f)]
            if shown:
                over.add(sid)
                notes.append("%s/%s frames %s show the port's pixels of %s on a game layer - the sheet goes over"
                             % (an, lname, ",".join(map(str, shown)), csheets[sid]))
    for sid in sorted(used_port, key=int):
        rt["sheets"].append((csheets[sid], port_paths[sid]))
        needs["port"][diffs[sid].path] = diffs[sid]
    for sid in over:
        needs["over"].add(diffs[sid].path)
    needs["seen"] = {d.path for d in diffs.values()}
    for sid, d in diffs.items():
        if sid not in used_port and sid not in over:
            notes.append("%s: the port's pixels are not looked at by any frame here" % csheets[sid])
    patch = ap.make(van, cur, rt if (rt["layers"] or rt["clones"]) else None)
    return patch, needs, notes, diffs


# THE PORT'S RETOUCH OVER A MOD'S SHEET TOO (02.10.2026, the M8 regression: a mod's gamemenu.png of the
# game's size brought back the blank paper the port clears under its PORT CREDITS strap). For these
# sheets the retouch is also written as pixels - every pixel where the port's version differs from
# vanilla - and portpatch.c lays it over a MOD's sheet of the game's size (shim_port_pixels_file); the
# game's own still gets the "over" file, which is vanilla + these pixels exactly (pixels.py proves it).
# Only sheets whose retouch is the port's own place, not the game's words: the port's word fixes of
# optionsextra/optionsmenu would spoil a translation mod's own words.
#
# ONLY WHERE THE MOD KEPT THE GAME'S ART (04.10.2026, the 25-mod desk campaign): the retouch clears the
# game's blank paper - but a mod that draws its own opaque background there (Reflashed) got a hole
# punched in it, ~75x105, showing what is behind the menu. So each pixel carries the VANILLA value too,
# and a flag: inside the strap's footprint (where the PortStrap* layers, drawn under Main, show their own
# pixels). portpatch.c lays the whole retouch only when the mod kept the game's art under it - at least
# PIXELS_SHARE of the pixels in the game's opacity class (alpha > 8 or not) and, opaque, every channel
# within PIXELS_TOL of vanilla (vanilla, the language sheets, recolours: 1.0; Reflashed: 0.59) - and
# otherwise only the footprint's pixels: the strap still shows (it lies UNDER Main: not clearing at all
# would hide it), the mod's background stays around it.
#   ISAACPX2, u16 w, u16 h, u32 n, n x (u16 x, u16 y, u8 port rgba, u8 vanilla rgba, u8 flags: 1 strap)
PIXELS_ANY_SHEET = ("gfx/ui/main menu/gamemenu.png",)
PIXELS_MAGIC = b"ISAACPX2"
PIXELS_REC = 13
PIXELS_TOL, PIXELS_SHARE_NUM, PIXELS_SHARE_DEN = 48, 9, 10
STRAP_PREFIX = "PortStrap"


def strap_footprint(sheet_target, shape):
    """the pixels of the sheet's space the port's strap layers (PortStrap*, in the port's own .anm2 beside the
    sheet, every animation, every visible frame) cover with their own opaque pixels - bool array (h, w)"""
    import numpy as np
    folder, leaf = sheet_target.rsplit("/", 1)
    anm = folder + "/" + leaf.rsplit(".", 1)[0] + ".anm2"
    b = pa.port_bytes(anm)
    foot = np.zeros(shape[:2], bool)
    if b is None:
        return foot
    d = ap.parse(b)
    m = ap.Model(d)
    sheets = {x.get("Id"): x.get("Path") for x in m.sheets.kids if x.tag == "Spritesheet"}
    layers = {x.get("Id"): x for x in m.layers.kids if x.tag == "Layer" and (x.get("Name") or "").startswith(STRAP_PREFIX)}
    for a in d.root.find("Animations").kids:
        las = a.find("LayerAnimations")
        for la in (las.kids if las is not None else []):
            lay = layers.get(la.get("LayerId"))
            if lay is None:
                continue
            sb = pa.port_bytes(folder + "/" + sheets[lay.get("SpritesheetId")])
            alpha = _img(sb)[:, :, 3] > 0
            for f in la.kids:
                if f.tag != "Frame" or f.get("Visible") == "false":
                    continue
                if float(f.get("XScale") or 100) != 100 or float(f.get("YScale") or 100) != 100 or float(f.get("Rotation") or 0):
                    raise ValueError("%s: a strap frame drawn scaled or turned - its footprint is not a crop" % anm)
                x0 = int(float(f.get("XPosition"))) - int(float(f.get("XPivot")))
                y0 = int(float(f.get("YPosition"))) - int(float(f.get("YPivot")))
                cx, cy, w, h = (int(float(f.get(k))) for k in ("XCrop", "YCrop", "Width", "Height"))
                crop = alpha[cy:cy + h, cx:cx + w]
                for yy in range(crop.shape[0]):
                    for xx in range(crop.shape[1]):
                        X, Y = x0 + xx, y0 + yy
                        if crop[yy, xx] and 0 <= X < shape[1] and 0 <= Y < shape[0]:
                            foot[Y, X] = True
    return foot


def pixels_make(vanilla_png, port_png, foot=None):
    """the port's retouch of a sheet as pixels (ISAACPX2, above)"""
    import struct
    import numpy as np
    v, p = _img(vanilla_png), _img(port_png)
    if v.shape != p.shape:
        raise ValueError("the port's version is not vanilla's size")
    ys, xs = np.nonzero((v != p).any(axis=2))
    out = [PIXELS_MAGIC, struct.pack("<HHI", v.shape[1], v.shape[0], len(xs))]
    for y, x in sorted(zip(ys.tolist(), xs.tolist())):
        flag = 1 if foot is not None and foot[y, x] else 0
        out.append(struct.pack("<HH9B", x, y, *[int(c) for c in p[y, x]], *[int(c) for c in v[y, x]], flag))
    return b"".join(out)


def pixels_records(patch):
    import struct
    if patch[:8] != PIXELS_MAGIC:
        raise ValueError("not a pixels patch")
    w, h, n = struct.unpack_from("<HHI", patch, 8)
    return w, h, [struct.unpack_from("<HH9B", patch, 16 + PIXELS_REC * i) for i in range(n)]


def pixels_kept(rgba, recs):
    """how many of the retouch's pixels the sheet keeps as the game's art (opacity class; channels within TOL)"""
    kept = 0
    for x, y, *rest in recs:
        m, v = rgba[y, x], rest[4:8]
        mo, vo = int(m[3]) > 8, v[3] > 8
        if mo == vo and (not mo or max(abs(int(m[c]) - v[c]) for c in range(3)) <= PIXELS_TOL):
            kept += 1
    return kept


def pixels_apply(rgba, patch, mode=None):
    """the pixels over an RGBA array (a copy) and the mode (2 whole, 1 the strap's footprint); None when it is
    not the patch's size. mode forces one."""
    w, h, recs = pixels_records(patch)
    if rgba.shape[1] != w or rgba.shape[0] != h:
        return None, 0
    if mode is None:
        mode = 2 if pixels_kept(rgba, recs) * PIXELS_SHARE_DEN >= len(recs) * PIXELS_SHARE_NUM else 1
    out = rgba.copy()
    for x, y, r, g, b, a, vr, vg, vb, va, flag in recs:
        if mode == 2 or flag & 1:
            out[y, x] = (r, g, b, a)
    return out, mode


# THE OTHER VERSION'S TABLES (04.10.2026): Repentance+'s data tables, extracted from the owner's PC game
# (work/pc-resources-repplus, tools/ResourceExtractor of Repentance+ 1.9.7), for the content tables the port
# knows (content_sim.T, read by the Repentance engine). stringtable.sta is not among them (a translation's keys
# of Repentance+ are harmless: the game never asks for them).
REPPLUS = os.path.join(pa.ROOT, "work", "pc-resources-repplus", "resources")


def version_refs():
    import content_sim as cs
    out = {}
    if not os.path.isdir(REPPLUS):
        return out
    for name, t in sorted(cs.T.items()):
        p = os.path.join(REPPLUS, name)
        if t.read and os.path.isfile(p):
            out[cs.OTHER_INNER + name] = open(p, "rb").read()
    return out


def asset_name(target, suffix=""):
    return "port_patch/" + target.replace(" ", "_") + suffix


def plan():
    """[(kind, target, asset name, bytes or None, extra)] and notes"""
    items, notes = [], []
    files, overs = {}, {}
    port_sheets = {}
    decided = set()                                     # sheets an anm2 of their own set has judged
    for e in pa.entries():
        path = e["path"]
        if path.endswith(".sta"):
            notes.append("%s: filled by content.c (step 5, port_additions.stringtable_patch) - not here" % path)
            continue
        if e["family"] == "ios_buttons_NNN.anm2":
            continue                                    # built at run time (variants)
        if path.endswith(".anm2"):
            patch, needs, n, _ = plan_anm2(path)
            notes += ["%s: %s" % (path, x) for x in n]
            base = pa.base_of(path)
            items.append(("patch", path, asset_name(path, ".patch"), patch, base if base != path else ""))
            for game_sheet, d in needs["port"].items():
                ed, st, rel = _set_of(game_sheet)
                folder = rel.rsplit("/", 1)[0]
                tgt = "%s/%s/%s/%s" % (ed, st, folder, "port_" + rel.rsplit("/", 1)[1].lower())
                port_sheets[tgt] = d.cur
            for game_sheet in needs["over"]:
                overs[game_sheet] = pa.port_bytes(game_sheet) or _port_sheet([game_sheet])[1]
            decided.update(needs["seen"])
            continue
        if e["change"] == "added":
            files[path] = pa.port_bytes(path)
        elif path.endswith(".png"):
            pass                                        # game sheets: decided by the anm2 that show them
        else:
            overs[path] = pa.port_bytes(path)           # hq4x.fs: the port's version of a game file
    # sheets of language sets the family's anm2 is read from (jp: rebirth data.jp ios_buttons.png)
    for e in pa.entries():
        path = e["path"]
        if not path.endswith(".png") or e["change"] != "modified":
            continue
        ed, st, rel = _set_of(path)
        folder, leaf = rel.rsplit("/", 1)
        tgt = "%s/%s/%s/port_%s" % (ed, st, folder, leaf.lower())
        data_tgt = "%s/data/%s/port_%s" % (ed, folder, leaf.lower())
        if tgt not in port_sheets and data_tgt in port_sheets:
            port_sheets[tgt] = pa.port_bytes(path)
        if path not in overs:
            ov = "%s/data/%s" % (ed, rel)
            if ov in overs and path not in decided:     # the data set's anm2 shows the port's pixels
                overs[path] = pa.port_bytes(path)
    out = []
    # the other version's tables (Repentance+), for content.c's three-way merge of a mod's whole table made for it
    # (content_sim.py, step 1a VERSION): laid into the pack under a path of the port's own, read by content.c only
    for t, data in version_refs().items():
        files[t] = data
    for t in sorted(files):
        out.append(("file", t, asset_name(t), files[t], ""))
    for t in sorted(port_sheets):
        out.append(("file", t, asset_name(t), port_sheets[t], ""))
    for t in sorted(overs):
        out.append(("over", t, asset_name(t), overs[t], ""))
    for t in sorted(overs):
        if _set_of(t)[2].lower() in PIXELS_ANY_SHEET:
            van = pa.vanilla_bytes(t)
            foot = strap_footprint(t, _img(van).shape)
            out.append(("pixels", t, asset_name(t, ".pixels"), pixels_make(van, overs[t], foot), ""))
    out += sorted(items, key=lambda x: x[1])
    out.append(("variants", VARIANT_BASE, "", None, ",".join(str(p) for p in VARIANT_PCTS)))
    # the port's versions the pak itself still carries (port_additions.IN_PAK_TRANSITION): nothing to lay,
    # a line for the export's registry (GameFiles.java, M12) - portpatch.c skips a line of two fields
    for t in sorted(pa.IN_PAK_TRANSITION):
        out.append(("inpak", t, "", None, ""))
    return out, notes


def index_text(items):
    lines = ["# the port's additions laid over the pack at start (portpatch.c); written by tools/port_patch.py"]
    for kind, target, asset, data, extra in items:
        if kind == "variants":
            lines.append("variants\t%s\t%s" % (target, extra))
        elif kind == "inpak":
            lines.append("inpak\t%s" % target)
        elif kind == "patch":
            lines.append("patch\t%s\t%s%s" % (target, asset, ("\t" + extra) if extra else ""))
        else:
            lines.append("%s\t%s\t%s" % (kind, target, asset))
    return ("\n".join(lines) + "\n").encode("utf-8")


def make(write=True):
    items, notes = plan()
    want = {INDEX: index_text(items)}
    for kind, target, asset, data, extra in items:
        if data is not None:
            want[asset[len("port_patch/"):]] = data
    if not write:
        return want, notes
    for rel, b in want.items():
        p = os.path.join(OUT, *rel.split("/"))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        if not os.path.isfile(p) or open(p, "rb").read() != b:
            with open(p, "wb") as f:
                f.write(b)
    # what is no longer made goes
    for dp, dn, fn in os.walk(OUT):
        for f in fn:
            rel = os.path.relpath(os.path.join(dp, f), OUT).replace("\\", "/")
            if rel not in want:
                os.remove(os.path.join(dp, f))
    return want, notes


def check():
    want, notes = make(write=False)
    bad = []
    for rel, b in want.items():
        p = os.path.join(OUT, *rel.split("/"))
        if not os.path.isfile(p) or open(p, "rb").read() != b:
            bad.append(rel)
    have = set()
    if os.path.isdir(OUT):
        for dp, dn, fn in os.walk(OUT):
            for f in fn:
                have.add(os.path.relpath(os.path.join(dp, f), OUT).replace("\\", "/"))
    extra = sorted(have - set(want))
    for r in bad:
        print("  STALE", r)
    for r in extra:
        print("  EXTRA", r)
    print("PORT_PATCH", "OK" if not bad and not extra else "STALE - run tools/port_patch.py --make",
          "(%d files)" % len(want))
    return not bad and not extra


def main(argv):
    if "--plan" in argv:
        items, notes = plan()
        for kind, target, asset, data, extra in items:
            print("%-8s %-70s %s %s" % (kind, target, len(data) if data is not None else "", extra))
        for n in notes:
            print("  note:", n)
        return 0
    if "--make" in argv:
        want, notes = make()
        print("port_patch: %d files written to %s" % (len(want), OUT))
        return 0
    if "--check" in argv:
        return 0 if check() else 1
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
