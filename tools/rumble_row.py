# -*- coding: utf-8 -*-
"""THE GAME'S FILTER ROW BECOMES RUMBLE (hotfix 1.75, the owner): the port's
optionsmenu.anm2 (work/port-edit, every language set) gets the Filter
animation filled with the game's own Rumble row and the game's own slider.

The image keeps naming the animation "Filter" (image strings are never
renamed; the port's STICK SCALE row already holds the "Rumble" animation's
content), so only the CONTENT of Filter changes, and only with the game's
pictures, taken from the IPA's own files of that set (jp and kr read data's):

    layer 14 Option Name   the vanilla Rumble animation's label frame - RUMBLE
                           (English in en, de, ru), VIBRACIÓN, ランブル, 진동, 手柄振动
    layer 5  Cursor        the vanilla Rumble animation's cursor frame
    layer 3  Adjustor1     the vanilla SFX animation's eleven slider frames
                           (0..10, OptionsMenu.png 192,400..240), moved right of
                           the label: its ink starts SLIDER_GAP units after the
                           label's ink ends (the game's own rows keep 7..10;
                           SFX's slider X 2 is the leftmost it goes)
    layer 4  OnOff         no frames: the row has no ON/OFF any more

THE OWNER'S LETTERING (hotfix 1.75, second word): the game's own RUMBLE label
stays where it reads in the set's own language - English. Every other set
gets OUR lettering at its neighbours' size, drawn the way the port draws its
own Options rows (lang.compose_like_native with the set's lang.SETTINGS; the
Latin and Cyrillic sets in the 12 cut, as our TYPE, SIZE and MODE are - the
16 cut was tried and was far too big beside the rows round it), with the Tweaks page's words for RUMBLE: the Russian one,
VIBRATION, VIBRACIÓN, ランブル, 진동, 手柄振动. The word goes onto the set's
optionsextra.png at LABEL_AT (in the band lang.py keeps for the port, clear
on all seven sheets and of every frame on them; lang.py leaves the box alone
when it clears that band) and is shown on layer 16, Option Name 2, the layer
our TYPE and SIZE names use; layer 14 is then left without frames. The
label, the gap and the slider are centred in the row's room, as lang.py
centres our rows, with the cursor 6 before the label and never further left
than MAP OPACITY's.

FrameNum becomes 11 (the slider's frames, as SFX), and every other layer's
single frame covers it (Delay = FrameNum - CLAUDE.md). The rest of the file is
left byte for byte; a reference copy <file>.before-rumblerow is kept beside it.
loader.c rumble_menu_item asks for Filter's FrameNum >= 11 before it uses it.

    py -3 tools/rumble_row.py           write work/port-edit (once; idempotent)
    py -3 tools/rumble_row.py --check   say what would change, write nothing
    py -3 tools/rumble_row.py --mockup DIR [en ru ...]
                                        the Options screen drawn from the port's
                                        files (the IPA's where the port has none),
                                        before and after, x4 nearest, into DIR
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import anm2patch as ap                      # noqa: E402
import port_additions as pa                 # noqa: E402

REL = "gfx/ui/main menu/optionsmenu.anm2"
SHEET = "gfx/ui/main menu/optionsmenu.png"
SETS = ["data", "dlc3.de", "dlc3.es", "dlc3.jp", "dlc3.kr", "dlc3.ru", "dlc3.zh"]
OWN_ANM2 = {"dlc3.jp": "data", "dlc3.kr": "data"}      # vanilla jp/kr read data's optionsmenu.anm2
FRAMES = 11
SLIDER_GAP = 8
SLIDER_X_MIN = 2
TAIL = ".before-rumblerow"
EXTRA = "gfx/ui/main menu/optionsextra.png"
LABEL_AT = (128, 264)                       # our word's box on optionsextra.png
LABEL_ROOM = (118, 22)                      # cleared before the word goes down
# the row's room: as wide as lang.py's (182), but started where the cursor still
# stands where MAP OPACITY's does (-78, the widest native row) - from lang.py's
# -80 the cursor of the 16-cut VIBRATION ran over the paper's torn edge
ROW_LEFT, ROW_RIGHT = -72, 110
OUR_WORD = {"dlc3.ru": "ВИБРАЦИЯ", "dlc3.de": "VIBRATION", "dlc3.es": "VIBRACIÓN",
            "dlc3.jp": "ランブル", "dlc3.kr": "진동", "dlc3.zh": "手柄振动"}
# the Team Meat sets keep the 12 cut our TYPE / SIZE / MODE rows use (lang.SETTINGS):
# the 16 cut stood as tall as FILTER but read far too big beside the rows round it
# (the owner, 14.10.2026) - measured against them in --measure
BIG_CUT = set()


def our_label(set_name, extra):
    """the word as our Options rows are lettered, cropped to its ink"""
    import lang
    st = dict(lang.SETTINGS[set_name])
    if set_name in BIG_CUT:
        st["font"] = "teammeatfont16.fnt"
    im, _ = lang.compose_like_native(OUR_WORD[set_name], st["height"], st, lang.native_share(extra))
    return im.crop(im.getchannel("A").getbbox())


def vanilla(path):
    import zipfile
    z = zipfile.ZipFile(pa.IPA)
    return z.read(pa.IPA_APP + path)


def anim(doc, name):
    for a in doc.root.find("Animations").kids:
        if a.tag == "Animation" and a.get("Name") == name:
            return a
    raise SystemExit("no animation " + name)


def layer_anim(a, lid):
    for la in a.find("LayerAnimations").kids:
        if la.get("LayerId") == str(lid):
            return la
    raise SystemExit("no LayerAnimation %s in %s" % (lid, a.get("Name")))


def ink_right(sheet, f):
    x, y, w, h = (int(f.get(k)) for k in ("XCrop", "YCrop", "Width", "Height"))
    al = sheet[y:y + h, x:x + w, 3]
    xs = np.nonzero(al.max(axis=0) > 0)[0]
    return int(f.get("XPosition")) - int(f.get("XPivot")) + int(xs.max())


def ink_left(sheet, f):
    x, y, w, h = (int(f.get(k)) for k in ("XCrop", "YCrop", "Width", "Height"))
    al = sheet[y:y + h, x:x + w, 3]
    xs = np.nonzero(al.max(axis=0) > 0)[0]
    return int(xs.min())


def node_bytes(n, depth, nl):
    out = []

    def w(n, depth):
        out.append(b"\t" * depth + b"<" + n.tag.encode("latin-1"))
        for name, raw, q in n.attrs:
            out.append(b" " + name.encode("latin-1") + b"=" + q + raw + q)
        if not n.kids:
            out.append(b" />" + nl)
            return
        out.append(b">" + nl)
        for k in n.kids:
            w(k, depth + 1)
        out.append(b"\t" * depth + b"</" + n.tag.encode("latin-1") + b">" + nl)

    w(n, depth)
    return b"".join(out)


def rumble_filter(set_name):
    """the new Filter animation of one set, and where the old one stands in the port's file"""
    path = os.path.join(pa.PORT_EDIT, "repentance-resources", set_name, *REL.split("/"))
    raw = open(path, "rb").read()
    doc = ap.parse(raw)
    v = ap.parse(vanilla("repentance-resources/%s/%s" % (OWN_ANM2.get(set_name, set_name), REL)))
    sheet = np.array(Image.open(os.path.join(pa.PORT_EDIT, "repentance-resources", set_name,
                                             *SHEET.split("/"))).convert("RGBA"))
    v_rumble, v_sfx = anim(v, "Rumble"), anim(v, "SFX")
    label = layer_anim(v_rumble, 14).find("Frame").copy()
    cursor = layer_anim(v_rumble, 5).find("Frame").copy()
    slider = [f.copy() for f in layer_anim(v_sfx, 3).kids if f.tag == "Frame"]
    assert len(slider) == FRAMES, (set_name, len(slider))
    x = max(SLIDER_X_MIN, ink_right(sheet, label) + SLIDER_GAP - ink_left(sheet, slider[0]))
    for f in slider:
        f.set("XPosition", str(x))
        f.set("Delay", "1")
    for f in (label, cursor):
        f.set("Delay", str(FRAMES))
    label16 = None
    extra_out = None
    if set_name in OUR_WORD:
        extra_path = os.path.join(pa.PORT_EDIT, "repentance-resources", set_name, *EXTRA.split("/"))
        extra = Image.open(extra_path).convert("RGBA")
        word = our_label(set_name, extra)
        w, h = word.size
        assert w <= LABEL_ROOM[0] and h <= LABEL_ROOM[1], (set_name, word.size)
        new_extra = extra.copy()
        new_extra.paste(Image.new("RGBA", LABEL_ROOM, (0, 0, 0, 0)), LABEL_AT)
        new_extra.paste(word, LABEL_AT)                 # plain, as lang._place: the tone stays the tone
        extra_out = (extra_path, extra, new_extra)
        slider_left = ink_left(sheet, slider[0])
        slider_ink = int(np.nonzero(sheet[int(slider[0].get("YCrop")):int(slider[0].get("YCrop")) + 16,
                                          192:256, 3].max(axis=0) > 0)[0].max()) - slider_left + 1
        total = w + SLIDER_GAP + slider_ink
        nx = ROW_LEFT + max(0, (ROW_RIGHT - ROW_LEFT - total) // 2)
        label16 = label.copy()
        for k, val in (("XCrop", LABEL_AT[0]), ("YCrop", LABEL_AT[1]), ("Width", w), ("Height", h),
                       ("XPivot", 0), ("YPivot", 0), ("XPosition", nx), ("YPosition", max(0, 9 - h // 2))):
            label16.set(k, str(val))
        cursor.set("XPosition", str(nx - 6))
        cursor.set("YPosition", "14")
        x = nx + w + SLIDER_GAP - slider_left
        for f in slider:
            f.set("XPosition", str(x))

    a = anim(doc, "Filter").copy()
    old_frames = a.get("FrameNum")
    a.set("FrameNum", str(FRAMES))
    root = a.find("RootAnimation").find("Frame")
    root.set("Delay", str(FRAMES))
    for la in a.find("LayerAnimations").kids:
        lid = la.get("LayerId")
        if lid == "3":
            la.kids = slider
        elif lid == "4":
            la.kids = []
        elif lid == "5":
            la.kids = [cursor]
        elif lid == "14":
            la.kids = [] if label16 is not None else [label]
        elif lid == "16" and label16 is not None:
            la.kids = [label16]
        else:
            fr = [k for k in la.kids if k.tag == "Frame"]
            if len(fr) == 1 and fr[0].get("Delay") == old_frames:
                fr[0].set("Delay", str(FRAMES))     # the straps' single frames cover the animation
            elif fr:
                raise SystemExit("%s: layer %s of Filter has %d frames - not expected" % (set_name, lid, len(fr)))
    start = raw.find(b'<Animation Name="Filter"')
    start = raw.rfind(b"\n", 0, start) + 1
    end = raw.find(b"</Animation>", start)
    end = raw.find(b"\n", end) + 1
    return path, raw, start, end, node_bytes(a, 2, doc.nl), x, extra_out


# ------------------------------------------------------------------ the mockup
# The list as the image builds it (0x6e1ae4: CONTROLS, AIM LOCK, TOUCH MODE,
# SFX, MUSIC, MAP OPACITY, FILTER, GAMMA, POP-UPS, LANGUAGE, ACTIVE CAM, EXTRA
# HUD, HUD OFFSET, FOUND HUD, CHARGE BARS, BULLET VISIBILITY) with the port's
# two rows after TOUCH MODE (menu.c insert_our_rows); row k stands at frame k
# of the null OptionPositions of Paper13 (0x6e3c90), the cursor (layer 5)
# shows on the chosen row only (0x6e3fb8).
# the animation each row NAMES as the port draws it: 5 STICK TYPE names PenOpacity,
# 6 STICK SCALE names Rumble, 18 the wide canvas names Fullscreen (loader.c)
ROW_ANIM = ["Controls", "ChangeController", "SFX", "Music", "MapOpacity", "PenOpacity", "Rumble", "Filter",
            "Gamma", "Popups", "Language", "ActiveCam", "ExtraHUD", "HUDOffset", "FoundHUD", "Rumble",
            "ChargeBars", "BulletVisibility", "Fullscreen", "TouchMode"]
MAIN_LIST = [0, 18, 19, 5, 6, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17]
# what each row's value layer is set to, {layer: frame}: a fresh port's values
VALUES = {18: {4: 0}, 19: {18: 1}, 5: {18: 0}, 6: {18: 0}, 2: {3: 3}, 3: {3: 3}, 4: {3: 5},
          7: {3: 7}, 9: {4: 0}, 11: {4: 0}, 12: {4: 1}, 13: {4: 0, 3: 10}, 14: {4: 0}, 16: {4: 0}, 17: {4: 0}}
FILTER_OFF = {4: 1}                     # the game's own FILTER row, off (0x6e3f90: eor #1 of 0)
STRAP_SHOWN = ("PortStrap0", "GuideStrap0")   # the straps at rest (straps.c)


def _sheet_file(set_name, name):
    for base in (pa.PORT_EDIT, pa.APP):
        for st in (set_name, "data"):
            d = os.path.join(base, "repentance-resources", st, "gfx", "ui", "main menu")
            if os.path.isdir(d):
                for f in os.listdir(d):
                    if f.lower() == name.lower():
                        return os.path.join(d, f)
    raise SystemExit("no sheet %s for %s" % (name, set_name))


def _draw(canvas, sheets, layer_sheet, a, at, frames_of, hidden, cursor):
    """one animation at `at`, each layer at the frame frames_of gives it (0 else), nearest"""
    import math
    for la in a.find("LayerAnimations").kids:
        lid = la.get("LayerId")
        fr = [k for k in la.kids if k.tag == "Frame"]
        if not fr or la.get("Visible") == "false" or lid in hidden or (lid == "5" and not cursor):
            continue
        f = fr[min(frames_of.get(int(lid), 0), len(fr) - 1)]
        if f.get("Visible") == "false":
            continue
        cx, cy, w, h = (float(f.get(k)) for k in ("XCrop", "YCrop", "Width", "Height"))
        piece = sheets[layer_sheet[lid]].crop((int(cx), int(cy), int(cx + w), int(cy + h)))
        al = float(f.get("AlphaTint") or 255) / 255.0
        if al < 1.0:
            r, g, b, alpha = piece.split()
            piece = Image.merge("RGBA", (r, g, b, alpha.point(lambda v: int(v * al))))
        px, py = float(f.get("XPivot") or 0), float(f.get("YPivot") or 0)
        sx, sy = float(f.get("XScale") or 100) / 100, float(f.get("YScale") or 100) / 100
        rot = math.radians(float(f.get("Rotation") or 0))
        x, y = float(f.get("XPosition") or 0) + at[0], float(f.get("YPosition") or 0) + at[1]
        c, s_ = math.cos(rot), math.sin(rot)
        m = [[c * sx, -s_ * sy], [s_ * sx, c * sy]]
        det = m[0][0] * m[1][1] - m[0][1] * m[1][0]
        ia, ib, id_, ie = m[1][1] / det, -m[0][1] / det, -m[1][0] / det, m[0][0] / det
        layer = piece.transform(canvas.size, Image.AFFINE,
                                (ia, ib, px - ia * x - ib * y, id_, ie, py - id_ * x - ie * y),
                                resample=Image.NEAREST)
        canvas.alpha_composite(layer)


def options_screen(set_name, anm2_bytes, rumble_values, chosen):
    """the main menu's Options paper and its eighteen rows, 480 x 470 units"""
    doc = ap.parse(anm2_bytes)
    content = doc.root.find("Content")
    sheets = {s.get("Id"): Image.open(_sheet_file(set_name, s.get("Path"))).convert("RGBA")
              for s in content.find("Spritesheets").kids}
    layers = content.find("Layers").kids
    layer_sheet = {l.get("Id"): l.get("SpritesheetId") for l in layers}
    hidden = {l.get("Id") for l in layers
              if (l.get("Name").startswith("PortStrap") or l.get("Name").startswith("GuideStrap"))
              and l.get("Name") not in STRAP_SHOWN}
    canvas = Image.new("RGBA", (480, 470), (0, 0, 0, 255))
    paper = anim(doc, "Paper13")
    _draw(canvas, sheets, layer_sheet, paper, (0, 0), {}, hidden, True)
    null = [k for k in paper.find("NullAnimations").kids if k.get("NullId") == "0"][0]
    spots = [(float(f.get("XPosition")), float(f.get("YPosition"))) for f in null.kids if f.tag == "Frame"]
    for k, row in enumerate(MAIN_LIST):
        values = rumble_values if row == 7 else VALUES.get(row, {})
        _draw(canvas, sheets, layer_sheet, anim(doc, ROW_ANIM[row]), spots[k], values, hidden, k == chosen)
    return canvas, spots[MAIN_LIST.index(7)]


def mockup(out_dir, wanted):
    os.makedirs(out_dir, exist_ok=True)
    suffix = {"data": "en", "dlc3.ru": "ru", "dlc3.de": "de", "dlc3.es": "es", "dlc3.jp": "jp",
              "dlc3.kr": "kr", "dlc3.zh": "zh"}
    chosen = MAIN_LIST.index(7)
    for set_name in SETS:
        if wanted and set_name not in wanted and suffix[set_name] not in wanted:
            continue
        path = os.path.join(pa.PORT_EDIT, "repentance-resources", set_name, *REL.split("/"))
        now = open(path, "rb").read()
        before, _ = options_screen(set_name, open(path + TAIL, "rb").read(), FILTER_OFF, chosen)
        after, spot = options_screen(set_name, now, VALUES[7], chosen)
        screen = (0, 0, 480, 270)
        after.crop(screen).resize((1920, 1080), Image.NEAREST).save(
            os.path.join(out_dir, "options-rumble-%s.png" % suffix[set_name]))
        pair = Image.new("RGBA", (480 * 2 + 8, 270), (255, 255, 255, 255))
        pair.paste(before.crop(screen), (0, 0))
        pair.paste(after.crop(screen), (488, 0))
        pair.resize((pair.width * 4, pair.height * 4), Image.NEAREST).save(
            os.path.join(out_dir, "options-rumble-%s-before-after.png" % suffix[set_name]))
        # the row at every step of its slider, 0 (off) to 10
        x0, y0 = int(spot[0]) - 100, int(spot[1]) - 1     # one row: the list steps 20 units
        steps = Image.new("RGBA", (200, 11 * 20), (0, 0, 0, 255))
        for v in range(11):
            im, _ = options_screen(set_name, now, {3: v}, chosen)
            steps.paste(im.crop((x0, y0, x0 + 200, y0 + 20)), (0, v * 20))
        steps.resize((steps.width * 4, steps.height * 4), Image.NEAREST).save(
            os.path.join(out_dir, "options-rumble-%s-steps.png" % suffix[set_name]))
        print("  %-8s options-rumble-%s.png, -before-after.png, -steps.png" % (set_name, suffix[set_name]))


def main():
    if "--mockup" in sys.argv:
        i = sys.argv.index("--mockup")
        mockup(sys.argv[i + 1], sys.argv[i + 2:])
        return
    check = "--check" in sys.argv
    for set_name in SETS:
        path, raw, start, end, new, x, extra = rumble_filter(set_name)
        out = raw[:start] + new + raw[end:]
        sheet_same = extra is None or np.array_equal(np.array(extra[1]), np.array(extra[2]))
        same = out == raw and sheet_same
        print("%-8s Filter -> RUMBLE (%s), slider at x %d: %s" % (
            set_name, OUR_WORD.get(set_name, "the game's own label"), x,
            "already so" if same else ("would change" if check else "written")))
        if same or check:
            continue
        if not sheet_same:
            ref = extra[0] + TAIL
            if not os.path.exists(ref):
                open(ref, "wb").write(open(extra[0], "rb").read())
            extra[2].save(extra[0])
        if out == raw:
            continue
        ref = path + TAIL
        if not os.path.exists(ref):
            open(ref, "wb").write(raw)
        open(path, "wb").write(out)
        # the result must still parse, and every other animation must be untouched
        d0, d1 = ap.parse(raw), ap.parse(out)
        a0 = [ap.canon(k) for k in d0.root.find("Animations").kids if k.get("Name") != "Filter"]
        a1 = [ap.canon(k) for k in d1.root.find("Animations").kids if k.get("Name") != "Filter"]
        assert a0 == a1, set_name
        assert ap.canon(d0.root.find("Content")) == ap.canon(d1.root.find("Content")), set_name


if __name__ == "__main__":
    main()
