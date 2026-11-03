# -*- coding: utf-8 -*-
"""The registry of what the port adds to the game's own files (plan 30.09, M8 and M12).

Since M8 (02.10.2026) the game's files in work/app - the pak - are the IPA's,
byte for byte. What the port changed or added in them lives in work/port-edit
(the authoring tools write there) and reaches the game as port_patch/ - .anm2
patches, port sheets, the port's versions of a few game files - laid over the
pak at start by portpatch.c (tools/port_patch.py makes it, tools/effective.py
proves it draws the same, tools/m8_vanilla.py did the move). This file says
WHAT the port changed in WHICH game file, who wrote it, and who in the port's
code relies on the numbers - which since M8 are read back by name at run time
(portpatch.c shim_port_layer), not assumed.

Vanilla is the IPA itself (TBOI Rebirth_v1.4.ipa): its zip directory carries a
size and CRC-32 for every file, so a game file is compared with vanilla
without unpacking anything.

  FAMILIES            the registered changes, by file family and language set
                      ("change": what the PORT'S VERSION is against vanilla)
  entries()           one dict per registered file (path, change, adds, tools...)
  port_path/bytes()   the port's version: work/port-edit, else work/app
  base_of()           the vanilla file a registered one is built over
  STRINGTABLE_FILL    the strings the port fills in stringtable.sta
  stringtable_patch() the same as a content_sim "fill" patch (step 5, PORT)
  vanilla_index()     {path: (size, crc32)} of the IPA's app folder
  IN_PAK_TRANSITION   game files the pak still carries in the port's version
  pak_check()         work/app against the IPA (build.py vanilla_pak)

Usage:
  py -3 tools/port_additions.py --check   every registered file's port version
                                          against the IPA and its declared anm2
                                          additions
  py -3 tools/port_additions.py --scan    work/port-edit and work/app against the
                                          IPA: every changed or new game file
                                          registered, every registered one really
                                          differs, and the pak vanilla
  py -3 tools/port_additions.py --pak     work/app (the pak) against the IPA: vanilla
                                          but IN_PAK_TRANSITION (build.py asks this)
  py -3 tools/port_additions.py --json    the registry as JSON
"""
import os, re, sys, json, zlib, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, "work", "app")
# M8: the port's own versions of the game files it changes, out of work/app (which
# goes back to vanilla). The tools that author the port's additions write here;
# tools/port_patch.py turns it into patches and port sheets.
PORT_EDIT = os.path.join(ROOT, "work", "port-edit")
IPA = os.path.join(ROOT, "TBOI Rebirth_v1.4.ipa")
IPA_APP = "Payload/The Binding of Isaac Rebirth.app/"

# pack.py keeps these out of the pack; they are reference copies, not game files
TAIL = re.compile(r"(\.orig$|\.before-|\.do-|REFERENCE|\.bak$)")

REP = ["data", "dlc3.de", "dlc3.es", "dlc3.jp", "dlc3.kr", "dlc3.ru", "dlc3.zh"]


def _strap(first, sheet):
    return [(first + i, "PortStrap%d" % i, sheet) for i in range(6)]


# One family = one game file name in several language sets, changed the same way.
# change: "modified" (vanilla has it, the port changed it) or "added" (vanilla has no such file).
# adds (anm2): sheets [(id, path)], layers [(id, name, sheet id)], nulls [(id, name)], animations [name]
#   - the port's numbers: a mod's anm2 with another structure moves them (M8, audit 3.2).
FAMILIES = [
    dict(name="gamemenu.anm2", edition="repentance-resources", sets=REP, rel="gfx/ui/main menu/gamemenu.anm2",
         change="modified",
         adds=dict(sheets=[(4, "port_strap_credits.png")],
                   layers=_strap(12, 4) + [(18, "Item6", 0)],
                   nulls=[(6, "Area6"), (7, "AreaDailyRun")],      # vanilla: AreaDailyRun is null 6
                   animations=[]),
         tools=["strap_layers.py", "main_menu_mods.py"],
         reference=[".before-strap-layer", ".before-mods", ".before-daily"],
         consumers=["straps.c TITLE_STRAP 12, TITLE_CURSOR 6 (game layer)",
                    "loader.c main_menu_mods: 6 rows, touch zones cmp w0,#7, menu tables 0x959fac/0x959fc8",
                    "screen.c nulls Area1..6, AreaDailyRun, ChallengeButton (finger fix)",
                    "menu.c daily_sign_off: game layers 7, 8, 9",
                    "credits.c: game layer 0 = sheet crop 0,0 480x270; plaque hit zone in sheet coordinates"]),
    dict(name="gamemenu.png", edition="repentance-resources", sets=REP, rel="gfx/ui/main menu/gamemenu.png",
         change="modified", adds=None,
         what="pixels: the watermark (watermark.py) and the PORT CREDITS plaque (credits_menu.py); since 02.10 also "
              "as a pixels patch (port_patch.PIXELS_ANY_SHEET) laid over a mod's sheet of the game's size",
         tools=["watermark.py", "credits_menu.py", "strap_layers.py"],
         reference=[".do-watermark", ".before-straps"],
         consumers=["credits.c plaque hit zone 328..391 x 140..191 (sheet coordinates)"]),
    dict(name="optionsmenu.anm2", edition="repentance-resources", sets=["data", "dlc3.de", "dlc3.es", "dlc3.ru", "dlc3.zh"],
         rel="gfx/ui/main menu/optionsmenu.anm2", change="modified",
         adds=dict(sheets=[(3, "port_strap_tweaks.png"), (4, "port_strap_guide.png")],
                   layers=[(18, "Option Value 2", 2), (19, "PaperMiddle4", 0)] + _strap(20, 3)
                   + [(26 + i, "GuideStrap%d" % i, 4) for i in range(6)],
                   nulls=[], animations=[]),
         what="also: animations PenOpacity and Rumble carry the port's STICK TYPE / SCALE rows; Paper13 YScale (optwall.py);"
              " Filter carries the RUMBLE row - the game's own Rumble label (en) or our word on layer 16 (the rest),"
              " the cursor and the SFX slider, 11 frames (rumble_row.py, hotfix 1.75: the FILTER row is the RUMBLE slider)",
         tools=["strap_layers.py", "guide_strap.py", "labels.py", "lang.py", "retype.py", "optwall.py", "aimlock_ru.py",
                "rumble_row.py"],
         reference=[".orig", ".before-strap-layer", ".before-guide-strap", ".before-controlwords", ".before-verybig",
                    ".before-seam", ".before-labels", ".before-lang", ".do-floating", ".do-watermark",
                    ".before-rumblerow"],
         consumers=["loader.c touch_mode_words 0x6e4020 mov w1,#18 (layer 18 frames ANALOGUE/DIGITAL/BUTTONS)",
                    "loader.c stick_menu_items mov w1,#18; animations PenOpacity 0xa5adfd, Rumble 0xa5ae22 by name",
                    "loader.c touch_mode_tap: values 1..3 -> layer 18 frames",
                    "loader.c fullscreen_menu_item: animation Fullscreen, game layer 4 (OnOff)",
                    "loader.c rumble_menu_item: animation Filter (FrameNum >= 11), game layer 3 (Adjustor1) frames 0..10",
                    "straps.c SETTINGS_STRAP 20, GUIDE_STRAP 26; show_strap_in(..'Paper13'..)",
                    "menu.c insert_our_rows: rows 5, 6, 18, 19 -> animations PenOpacity, Fullscreen...",
                    "tweaks.c strap place on the settings paper (127,15), GUIDE_STRAP_DROP 52, _H 47"]),
    dict(name="optionsmenu.anm2 (jp, kr)", edition="repentance-resources", sets=["dlc3.jp", "dlc3.kr"],
         rel="gfx/ui/main menu/optionsmenu.anm2", change="added",
         adds=dict(sheets=[(3, "port_strap_tweaks.png"), (4, "port_strap_guide.png")],
                   layers=[(18, "Option Value 2", 2), (19, "PaperMiddle4", 0)] + _strap(20, 3)
                   + [(26 + i, "GuideStrap%d" % i, 4) for i in range(6)],
                   nulls=[], animations=[]),
         what="a whole language copy the IPA has not got (vanilla jp/kr read data/'s optionsmenu.anm2)",
         base="repentance-resources/data/gfx/ui/main menu/optionsmenu.anm2",
         tools=["lang.py", "labels.py", "guide_strap.py", "rumble_row.py"],
         reference=[".before-lang", ".before-labels", ".before-rumblerow"],
         consumers=["as optionsmenu.anm2 above"]),
    dict(name="optionsmenu.png", edition="repentance-resources", sets=["data", "dlc3.ru"], rel="gfx/ui/main menu/optionsmenu.png",
         change="modified", adds=None, what="pixels: AIM LOCK row (aimlock_ru.py in ru), strap seams",
         tools=["aimlock_ru.py", "strap_layers.py"], reference=[".before-aimlock", ".before-straps"], consumers=[]),
    dict(name="optionsextra.png", edition="repentance-resources", sets=REP, rel="gfx/ui/main menu/optionsextra.png",
         change="modified", adds=None,
         what="pixels: the words of layer 18 (ANALOGUE/DIGITAL/BUTTONS) and the port's row labels; 256x512 (PC: 256x256);"
              " the RUMBLE row's word at (128,264) in every set but data (rumble_row.py, hotfix 1.75)",
         tools=["labels.py", "lang.py", "retype.py", "rumble_row.py"],
         reference=[".before-labels", ".before-controlwords", ".before-verybig", ".do-floating", ".before-rumblerow"],
         consumers=["loader.c layer 18 frames (touch_mode_words)",
                    "optionsmenu.anm2 Filter layer 16 (Option Name 2): the RUMBLE word, rumble_row.LABEL_AT"]),
    dict(name="saveselectmenu.anm2 (zh)", edition="repentance-resources", sets=["dlc3.zh"],
         rel="gfx/ui/main menu/saveselectmenu.anm2", change="modified", adds=None,
         what="layer 10 Import (the PC's Rebirth save import, dead on iOS) hidden, Delete layer 5 at X 155 - "
              "centred on screen like data's (the owner, 19.10.2026)",
         tools=[], reference=[".before-noimport"], consumers=[]),
    dict(name="pausescreen.anm2", edition="repentance-resources", sets=["data", "dlc3.de", "dlc3.ru", "dlc3.zh"],
         rel="gfx/ui/pausescreen.anm2", change="modified",
         adds=dict(sheets=[(3, "port_strap_spawner.png")], layers=_strap(8, 3), nulls=[], animations=[]),
         tools=["strap_layers.py"], reference=[".before-strap-layer"],
         consumers=["straps.c PAUSE_STRAP 8, PAUSE_CURSOR 2, PAUSE_PAPER 1 (game layers 2 and 1)",
                    "straps.c: the pause draws layers 0..7 (0x8a23b4); the strap goes before layer 1",
                    "spawner.c 0x89e714(sprite+0x30, 1): game layer 1 (Paper)"]),
    dict(name="port_strap_credits.png", edition="repentance-resources", sets=REP,
         rel="gfx/ui/main menu/port_strap_credits.png", change="added", adds=None,
         what="the port's sheet inside the game's folder (gamemenu sheet 4)", tools=["strap_layers.py", "lang.py"],
         reference=[".before-lang"], consumers=["gamemenu.anm2 sheet 4"]),
    dict(name="port_strap_guide.png", edition="repentance-resources", sets=REP,
         rel="gfx/ui/main menu/port_strap_guide.png", change="added", adds=None,
         what="the port's sheet (optionsmenu sheet 4)", tools=["guide_strap.py", "lang.py"], reference=[".before-lang"],
         consumers=["optionsmenu.anm2 sheet 4"]),
    dict(name="port_strap_tweaks.png", edition="repentance-resources", sets=REP,
         rel="gfx/ui/main menu/port_strap_tweaks.png", change="added", adds=None,
         what="the port's sheet (optionsmenu sheet 3)", tools=["strap_layers.py", "lang.py"], reference=[".before-lang"],
         consumers=["optionsmenu.anm2 sheet 3"]),
    dict(name="port_strap_spawner.png", edition="repentance-resources", sets=REP,
         rel="gfx/ui/port_strap_spawner.png", change="added", adds=None,
         what="the port's sheet (pausescreen sheet 3)", tools=["strap_layers.py", "lang.py"], reference=[".before-lang"],
         consumers=["pausescreen.anm2 sheet 3"]),
    dict(name="titlemenu_ios.png", edition="repentance-resources", sets=["dlc3.de", "dlc3.es", "dlc3.jp", "dlc3.kr", "dlc3.ru", "dlc3.zh"],
         rel="gfx/ui/main menu/titlemenu_ios.png", change="added", adds=None,
         what="language copies of the iOS title sheet (vanilla has data/ only)", tools=["lang.py"], reference=[],
         consumers=["the engine takes it while the last input device is the touch pseudo-device 0x10dead (M9)"]),
    dict(name="ios_buttons.anm2", edition="rebirth-resources", sets=["data"], rel="gfx/ui/touch/ios_buttons.anm2",
         change="modified",
         adds=dict(sheets=[], layers=[], nulls=[],
                   animations=["Twin Swap", "Twin Soul", "Twin Dark Esau", "Analog DPad Move", "Analog DPad Shoot",
                               "Analog DPad Shoot Lock", "Analog DPad Blank Move", "Analog DPad Blank Shoot"]),
         tools=["twin_buttons.py", "dpad_art.py"], reference=[".before-twins", ".before-dpad", ".before-dpadlock"],
         consumers=["sticks.c dpad_dress: Analog DPad Move/Shoot/Shoot Lock frames 0..8, Blank",
                    "hud.c twin buttons by name"]),
    dict(name="ios_buttons_NNN.anm2", edition="rebirth-resources", sets=["data"],
         rel=["gfx/ui/touch/ios_buttons_%d.anm2" % n for n in (100, 105, 110, 115, 120, 125, 130, 135, 140, 150, 170, 175)],
         change="added", adds=None, what="12 size variants of ios_buttons.anm2 (M9: build from the winning file, delete these)",
         base="rebirth-resources/data/gfx/ui/touch/ios_buttons.anm2",
         tools=["twin_buttons.py", "dpad_art.py"], reference=[".orig", ".before-twins", ".before-dpad"],
         consumers=["sticks.c:2950 ios_buttons_<pct>.anm2", "touchedit.c:369 ios_buttons_100.anm2"]),
    dict(name="ios_buttons.png", edition="rebirth-resources", sets=["data", "data.jp"], rel="gfx/ui/touch/ios_buttons.png",
         change="modified", adds=None, what="pixels: analog d-pad and twin button art",
         tools=["dpad_art.py", "twin_buttons.py"], reference=[".before-dpad", ".before-twins", ".before-dpadlock"],
         consumers=[]),
    dict(name="stringtable.sta", edition=["repentance-resources", "rebirth-resources"], sets=["data"], rel="stringtable.sta",
         change="modified", adds=None, what="empty strings of 5 keys filled (STRINGTABLE_FILL) - by hand 06.09",
         tools=["(by hand 06.09)"], reference=[".orig"],
         consumers=["the prompts OK/YES/NO and RESELECT_CONTROLLER_* in every language"]),
    dict(name="colorcorrection.fs", edition="repentance-resources", sets=["data"], rel="shaders/colorcorrection.fs",
         change="modified", adds=None,
         what="sharp bilinear on the last stretch to the window - texels solid, the border pixel blended (13.10.2026); "
              "size handed in as the PortSharp uniform, none at a whole stretch (stretch.c LIGHTER, 21.10.2026)",
         tools=["(by hand 13.10, walls walking; 21.10 lighter)"], reference=[".before-175shimmer", ".before-x1scene"],
         consumers=["the frame's last pass onto the window (0x671a78 road), stretch.c turns LINEAR on for it"]),
]

# stringtable.sta, category Default: the port fills these where the game's string is empty
# (languages by the <languages> names of the file; English is the game's own and stays).
STRINGTABLE_FILL = {
    "PROMPT_OK": {"Japanese": "OK", "Korean": "확인", "Chinese (Simple)": "确定", "Russian": "ОК",
                  "German": "OK", "Spanish": "OK", "French": "OK"},
    "PROMPT_YES": {"Japanese": "はい", "Korean": "예", "Chinese (Simple)": "是", "Russian": "ДА",
                   "German": "JA", "Spanish": "SÍ", "French": "OUI"},
    "PROMPT_NO": {"Japanese": "いいえ", "Korean": "아니오", "Chinese (Simple)": "否", "Russian": "НЕТ",
                  "German": "NEIN", "Spanish": "NO", "French": "NON"},
    "RESELECT_CONTROLLER_HEADER": {
        "Japanese": "コントローラーを選択してください！", "Korean": "컨트롤러를 선택해주세요!",
        "Chinese (Simple)": "选择手柄！", "Russian": "Выберите контроллер!", "German": "Wähle einen Controller!",
        "Spanish": "¡Selecciona un mando!", "French": "Sélectionne une manette !"},
    "RESELECT_CONTROLLER_MESSAGE": {
        "Japanese": "使用したいコントローラーの\\nボタンを押してください。",
        "Korean": "사용할 컨트롤러의\\n아무 버튼이나 눌러주세요.",
        "Chinese (Simple)": "在想要使用的手柄上\\n按任意键。",
        "Russian": "Нажмите любую кнопку на контроллере, который хотите использовать.",
        "German": "Drücke eine beliebige Taste auf dem Controller, den du verwenden möchtest.",
        "Spanish": "Pulsa cualquier botón en el mando que quieras usar.",
        "French": "Appuie sur un bouton de la manette que tu souhaites utiliser."},
}
STRINGTABLE_ORDER = ["PROMPT_OK", "PROMPT_YES", "PROMPT_NO", "RESELECT_CONTROLLER_HEADER", "RESELECT_CONTROLLER_MESSAGE"]
LANG_ORDER = ["Japanese", "Korean", "Chinese (Simple)", "Russian", "German", "Spanish", "French"]


def stringtable_patch():
    """STRINGTABLE_FILL as a stringtable patch (content_sim step 5, fill mode)"""
    out = ['<stringtable>\n<category name="Default">\n']
    for k in STRINGTABLE_ORDER:
        out.append('<key name="%s">\n' % k)
        for lang in LANG_ORDER:
            out.append('<string lang="%s">%s</string>\n' % (lang, STRINGTABLE_FILL[k][lang]))
        out.append("</key>\n")
    out.append("</category>\n</stringtable>\n")
    return "".join(out).encode("utf-8")


def entries():
    """one dict per registered file: path (relative to work/app), family, change, adds, ..."""
    out = []
    for f in FAMILIES:
        eds = f["edition"] if isinstance(f["edition"], list) else [f["edition"]]
        rels = f["rel"] if isinstance(f["rel"], list) else [f["rel"]]
        for ed in eds:
            for s in f["sets"]:
                for rel in rels:
                    e = dict(f)
                    e["path"] = "%s/%s/%s" % (ed, s, rel)
                    e["family"] = f["name"]
                    out.append(e)
    return out


_vanilla = None
_zip = None


def vanilla_index():
    """{path relative to the .app: (size, crc32)} straight from the IPA's zip directory"""
    global _vanilla, _zip
    if _vanilla is None:
        _zip = zipfile.ZipFile(IPA)
        _vanilla = {}
        for i in _zip.infolist():
            if i.filename.startswith(IPA_APP) and not i.is_dir():
                _vanilla[i.filename[len(IPA_APP):]] = (i.file_size, i.CRC)
    return _vanilla


def vanilla_bytes(rel):
    vanilla_index()
    try:
        return _zip.read(IPA_APP + rel)
    except KeyError:
        return None


def port_path(rel):
    """where the port's version of a registered file lies: work/port-edit, else work/app"""
    p = os.path.join(PORT_EDIT, *rel.split("/"))
    if os.path.isfile(p):
        return p
    p = os.path.join(APP, *rel.split("/"))
    return p if os.path.isfile(p) else None


def port_bytes(rel):
    p = port_path(rel)
    return open(p, "rb").read() if p else None


def base_of(rel):
    """the vanilla file a registered file is built over: itself, or the family's base (None: a port file)"""
    if rel in vanilla_index():
        return rel
    for e in entries():
        if e["path"] == rel:
            return e.get("base")
    return None


def crc_of(path):
    c = 0
    with open(path, "rb") as fh:
        while True:
            b = fh.read(1 << 20)
            if not b:
                break
            c = zlib.crc32(b, c)
    return c


def _file_state(p, rel):
    if not p or not os.path.isfile(p):
        return "absent"
    v = vanilla_index().get(rel)
    if v is None:
        return "added"
    if os.path.getsize(p) != v[0] or cached_crc(p, rel) != v[1]:
        return "modified"
    return "identical"


_crc_mem = None


def cached_crc(p, rel):
    """crc_of(p), kept in work/pak-crc-cache.json by path, size and mtime (save_crc_cache() writes it)"""
    global _crc_mem
    if _crc_mem is None:
        _crc_mem = _crc_cache_load()
    st = os.stat(p)
    key = "%s|%s|%d|%d" % ("edit" if p.startswith(PORT_EDIT) else "app", rel, st.st_size, int(st.st_mtime))
    c = _crc_mem.get(key)
    if c is None:
        c = _crc_mem[key] = crc_of(p)
    return c


def save_crc_cache():
    if _crc_mem is not None:
        _crc_cache_save(_crc_mem)


def state(rel):
    """the PORT'S VERSION of a game file against vanilla: 'identical' | 'modified' | 'added' | 'absent'"""
    return _file_state(port_path(rel), rel)


def app_state(rel):
    """work/app's file (what goes into the pak) against vanilla"""
    return _file_state(os.path.join(APP, *rel.split("/")), rel)


def anm2_struct(b):
    """sheets [(id, path)], layers [(id, name, sheet)], nulls [(id, name)], animations [name]"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import content_sim as cs
    v = cs.parse(b)
    s = dict(sheets=[], layers=[], nulls=[], animations=[])
    for x in v:
        t = cs.name_of(b, x)
        par = cs.name_of(b, v[x.parent]) if x.parent >= 0 else ""
        if t == "Spritesheet" and par == "Spritesheets":
            s["sheets"].append((int(cs.get(b, x, "Id")), cs.get(b, x, "Path")))
        elif t == "Layer" and par == "Layers":
            s["layers"].append((int(cs.get(b, x, "Id")), cs.get(b, x, "Name"), int(cs.get(b, x, "SpritesheetId") or -1)))
        elif t == "Null" and par == "Nulls":
            s["nulls"].append((int(cs.get(b, x, "Id")), cs.get(b, x, "Name")))
        elif t == "Animation":
            s["animations"].append(cs.get(b, x, "Name"))
    return s


def check_entry(e):
    """[problems] of one registered file; [] when it is as registered"""
    probs = []
    st = state(e["path"])
    if st != e["change"]:
        probs.append("expected %s, found %s" % (e["change"], st))
    adds = e.get("adds")
    if adds and st in ("modified", "added"):
        cur = anm2_struct(port_bytes(e["path"]))
        van = None
        if st == "modified":
            van = anm2_struct(vanilla_bytes(e["path"]))
        for k in ("sheets", "layers", "nulls"):
            for item in adds[k]:
                if tuple(item) not in [tuple(c) for c in cur[k]]:
                    probs.append("%s %s missing" % (k[:-1], item))
            if van is not None:
                extra = [c for c in cur[k] if c not in van[k] and tuple(c) not in [tuple(a) for a in adds[k]]]
                if extra:
                    probs.append("unregistered %s %s" % (k, extra))
        for a in adds["animations"]:
            if a not in cur["animations"]:
                probs.append("animation %s missing" % a)
        if van is not None:
            extra = [a for a in cur["animations"] if a not in van["animations"] and a not in adds["animations"]]
            if extra:
                probs.append("unregistered animations %s" % extra)
    return probs


def check():
    ok = True
    for e in entries():
        p = check_entry(e)
        ok &= not p
        print("  %-70s %s" % (e["path"], "ok" if not p else "; ".join(p)))
    print("CHECK", "OK" if ok else "FAILED")
    return ok


# Game files the pak still carries in the port's version, until what replaces
# them at run time is in place (M8 transition). content.c fills stringtable.sta
# (step 5, stringtable_patch() below) - once that runs with no mods enabled too,
# these go back to vanilla and this list is empty: the end state.
IN_PAK_TRANSITION = ["rebirth-resources/data/stringtable.sta"]   # Repentance fills its own through content.c (02.10); whether it reads this one is not measured


def scan():
    """the port's versions (work/port-edit) against the IPA: every changed or new
    game file must be registered and every registered one must differ; and work/app
    - the pak - must be vanilla (IN_PAK_TRANSITION aside)"""
    reg = {e["path"]: e for e in entries()}
    van = vanilla_index()
    seen, unreg = set(), []
    for root in (PORT_EDIT, APP):
        if not os.path.isdir(root):
            continue
        for dp, dn, fn in os.walk(root):
            for f in fn:
                rel = os.path.relpath(os.path.join(dp, f), root).replace("\\", "/")
                if TAIL.search(rel) or rel in seen:
                    continue
                if root == APP and os.path.isfile(os.path.join(PORT_EDIT, *rel.split("/"))):
                    continue                    # the port's version is the one in port-edit
                seen.add(rel)
                if rel in reg:
                    continue
                st = _file_state(os.path.join(dp, f), rel) if rel in van else "added"
                if st != "identical":
                    unreg.append((rel, st))
    stale = [p for p in reg if p not in seen and port_path(p) is None]
    ok = not unreg and not stale
    for rel, st in sorted(unreg):
        print("  UNREGISTERED %-10s %s" % (st, rel))
    for p in sorted(stale):
        print("  REGISTERED BUT ABSENT %s" % p)
    save_crc_cache()
    bad = pak_check(quiet=True)
    for b in bad:
        print("  PAK NOT VANILLA", b)
    ok &= not bad
    print("SCAN", "OK" if ok else "FAILED", "(%d files compared, %d registered)" % (len(seen), len(reg)))
    return ok


def pak_check(quiet=False):
    """work/app - what pack.py packs - against vanilla: [problems]. A game file that
    differs, or a file the IPA has not got, is the port's and belongs in
    port_patch/ (tools/port_patch.py); IN_PAK_TRANSITION is let through."""
    van = vanilla_index()
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import pack
    bad = []
    cache = _crc_cache_load()
    for rel, full in pack.collect_list():
        v = van.get(rel)
        if v is None:
            bad.append("%s: not a game file (the port's files go to port_patch/)" % rel)
            continue
        st = os.stat(full)
        if st.st_size != v[0]:
            if rel not in IN_PAK_TRANSITION:
                bad.append("%s: %d bytes, vanilla %d" % (rel, st.st_size, v[0]))
            continue
        key = "%s|%d|%d" % (rel, st.st_size, int(st.st_mtime))
        c = cache.get(key)
        if c is None:
            c = crc_of(full)
            cache[key] = c
        if c != v[1] and rel not in IN_PAK_TRANSITION:
            bad.append("%s: not the IPA's bytes" % rel)
    _crc_cache_save(cache)
    if not quiet:
        for b in bad:
            print("  PAK NOT VANILLA", b)
    return bad


CRC_CACHE = os.path.join(ROOT, "work", "pak-crc-cache.json")


def _crc_cache_load():
    try:
        return json.load(open(CRC_CACHE, encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def _crc_cache_save(c):
    try:
        tmp = CRC_CACHE + ".tmp"
        json.dump(c, open(tmp, "w", encoding="utf-8"))
        os.replace(tmp, CRC_CACHE)
    except OSError:
        pass


def main(argv):
    if "--check" in argv:
        return 0 if check() else 1
    if "--scan" in argv:
        return 0 if scan() else 1
    if "--pak" in argv:
        bad = pak_check()
        print("PAK", "VANILLA" if not bad else "NOT VANILLA (%d files)" % len(bad))
        return 0 if not bad else 1
    if "--json" in argv:
        out = []
        for e in entries():
            d = {k: v for k, v in e.items() if k not in ("sets", "rel", "edition", "name")}
            out.append(d)
        print(json.dumps(dict(files=out, stringtable_fill=STRINGTABLE_FILL), ensure_ascii=False, indent=1))
        return 0
    print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
