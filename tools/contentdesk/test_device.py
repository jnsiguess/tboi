# -*- coding: utf-8 -*-
"""content.c on the umdesk stand (the real usermods.c + content.c for Windows): the device side.

    py -3 tools/contentdesk/test_device.py

A desk config with several mods stacked in MODS, then the game's requests of the tables:
  1. what the game is handed equals the model's build (tools/content_sim.py) over the same mod
     folders, byte for byte, and the report's content: lines are the model's lines;
  2. a legacy file past its loader's table (the whole danger column: locusts, cutscenes, stages,
     seedmenu, recipes...) is refused and the next weaker one or the game's is used;
  3. a result the port refuses (a bestiary parent cycle a patch makes) is built again without
     that mod's patch;
  4. a bestiary anm2 the mod lacks: parent="<type>.0";
  5. <id>.check.txt carries each mod's lines; the MODS page's counts;
  6. a second start answers from content-cache/data (nothing rebuilt), a changed mod file rebuilds;
  7. the clean build (desk0.exe) hands the same bytes and prints no content: line.
"""
import os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import content_sim as cs  # noqa: E402

UM = os.path.join(ROOT, "tools", "umdesk")
GAME = os.path.join(ROOT, "work", "app", "repentance-resources", "data")
SCRATCH = os.path.join(os.environ.get("TEMP", "."), "contentdesk-device")
failures = []


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        failures.append(what)


def game(name):
    return open(os.path.join(GAME, name), "rb").read()


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(data if isinstance(data, bytes) else data.encode("utf-8"))


def run(desk, cfg, root, inner):
    e = {k: v for k, v in os.environ.items() if not k.startswith("DESK_")}
    e["DESK_ROOT"] = root
    p = subprocess.run([os.path.join(UM, desk), "map", cfg, inner], capture_output=True, env=e)
    out = p.stdout.decode("utf-8", "replace")
    m = re.search(r"== map \S+ -> (.*)", out)
    got = m.group(1).strip() if m else ""
    return out, got


def handed(got, name):
    if got == "(the game's own)":
        return game(name)
    return open(got, "rb").read() if got and os.path.exists(got) else None


ORDER = ["alpha", "cee", "bee", "dee", "eee", "fff"]          # strongest first


def make(cfg):
    mods = os.path.join(cfg, "mods")
    g = game("players.xml")
    # alpha: patches players (a list and a value), the strings, and a new enemy variant (its bestiary anm2 missing)
    put(os.path.join(mods, "alpha", "patch", "players.xml"),
        b'<players><player id="13" items-add="412"/><player id="2" coins="99"/><player id="41" coins="1"/></players>')
    put(os.path.join(mods, "alpha", "patch", "stringtable.sta"),
        '<stringtable><category name="Default"><key name="PROMPT_OK"><string lang="Russian">Ладно</string></key>'
        '<key name="ALPHA_NEW"><string>Hi</string></key></category></stringtable>')
    put(os.path.join(mods, "alpha", "content", "entities2.xml"),
        b'<entities anm2root="gfx/"><entity id="10" variant="77" name="Red Gaper" anm2path="monsters/red.anm2" baseHP="10" '
        b'portrait="x.png"><bestiary anm2path="bestiary/red.anm2"/></entity></entities>')
    put(os.path.join(mods, "alpha", "resources", "gfx", "monsters", "red.anm2"), b"<AnimatedActor/>")
    # cee: a whole players.xml past the table (refused: bee's is used)
    put(os.path.join(mods, "cee", "resources", "players.xml"),
        g.replace(b"</players>", b'\t<player id="41" name="Extra" skin="x.png"/>\r\n</players>'))
    # bee: an older players.xml (players 30..40 missing) + a patch that conflicts with alpha
    v = cs.parse(g)
    pl = [x for x in v if cs.name_of(g, x) == "player"]
    old = g[:pl[30].start] + b"\r\n</players>\r\n"
    put(os.path.join(mods, "bee", "resources", "players.xml"), old)
    put(os.path.join(mods, "bee", "patch", "players.xml"), b'<players><player id="2" coins="10"/><player id="35" bombs="9"/></players>')
    # dee: a patch whose bestiary parent makes a cycle (refused as a result: built again without it)
    put(os.path.join(mods, "dee", "patch", "entities2.xml"),
        b'<entities><entity id="10" variant="77"><bestiary parent="10.77"/></entity></entities>')
    # eee: the danger column's new rows
    for name, close, rec in (("cutscenes.xml", b"</cutscenes>", b'<cutscene id="27" name="x"/>'),
                             ("stages.xml", b"</stages>", b'<stage id="37" name="x" path="x.stb"/>'),
                             ("seedmenu.xml", b"</seedmenu>", b'<seed id="200" name="x"/>'),
                             ("locusts.xml", b"</locusts>", b'<locust id="733" color="x"/>')):
        b0 = game(name)
        i = b0.rfind(close)
        put(os.path.join(mods, "eee", "resources", name), b0[:i] + rec + b0[i:])
    b0 = game("recipes.xml")
    i = b0.rfind(b"</recipes>")
    put(os.path.join(mods, "fff", "resources", "recipes.xml"), b0[:i] + '<recipe input="ééééé" output="1"/>'.encode() + b0[i:])
    for m in ORDER:
        put(os.path.join(mods, m, "mod.json"), '{"id": "%s", "name": "%s"}' % (m, m.upper()))
    put(os.path.join(mods, "mods.txt"), "# mods\n" + "".join("on %s\n" % m for m in ORDER))


def model(cfg, name, port=None):
    mods = [cs.Mod.from_dir(os.path.join(cfg, "mods", m)) for m in ORDER]
    return cs.build(name, mods, port=port)


def main():
    shutil.rmtree(SCRATCH, ignore_errors=True)
    cfg = os.path.join(SCRATCH, "cfg")
    root = os.path.join(SCRATCH, "files", "app")
    os.makedirs(root)
    make(cfg)
    print("1. the game is handed the model's bytes")
    out, got = run("desk.exe", cfg, root, "repentance-resources/data/players.xml")
    first_out = out
    want = model(cfg, "players.xml")
    data = handed(got, "players.xml")
    check(data == want.data, "players.xml: the model's bytes (%s)" % got)
    lines = [l.split("[report] content: ", 1)[1] for l in out.splitlines() if "[report] content: players.xml:" in l]
    check(lines == want.lines, "players.xml: the report's lines are the model's\n        C     %s\n        model %s" % (lines, want.lines))
    check(any("conflict player[2] coins: bee and alpha - alpha wins" in l for l in lines), "the conflict line")
    check(any("legacy cee: refused" in l for l in lines) and want.base == "bee", "cee refused, bee is the base")
    import port_additions
    out, got = run("desk.exe", cfg, root, "repentance-resources/data/stringtable.sta")
    want = model(cfg, "stringtable.sta", port=port_additions.stringtable_patch())
    check(handed(got, "stringtable.sta") == want.data, "stringtable.sta: the model's bytes with the port's fill")

    print("2. the danger column")
    for name in ("cutscenes.xml", "stages.xml", "seedmenu.xml", "locusts.xml", "recipes.xml"):
        out, got = run("desk.exe", cfg, root, "repentance-resources/data/" + name)
        check(got == "(the game's own)", "%s: refused, the game's own (%s)" % (name, got))

    print("3. a result refused (bestiary cycle) and 4. the bestiary's anm2")
    out, got = run("desk.exe", cfg, root, "repentance-resources/data/entities2.xml")
    data = handed(got, "entities2.xml") or b""
    lines = [l.split("[report] content: ", 1)[1] for l in out.splitlines() if "[report] content: entities2.xml:" in l]
    check(any("result refused - entity 10.77.0" in l for l in lines), "the result's cycle is told: %s" % [l for l in lines if "result" in l])
    check(any("without content/ and patch/ of dee" in l for l in lines), "built again without dee's patch")
    check(b'variant="77" name="Red Gaper"' in data and b'parent="10.77"' not in data, "the merged variant stays, the cycle is gone")
    check(b'<bestiary parent="10.0"/>' in data, "the missing bestiary anm2: parent=10.0")
    check(any("bestiary of alpha: variant 10.77" in l for l in first_out.splitlines()), "the report's bestiary line of the variant (the build's run)")

    print("5. the notes")
    note = lambda m: open(os.path.join(cfg, "mods", m + ".check.txt"), encoding="utf-8").read() \
        if os.path.exists(os.path.join(cfg, "mods", m + ".check.txt")) else ""
    check("players.xml: conflict player[2] coins: bee and alpha - alpha wins" in note("alpha")
          and "conflict player[2] coins" in note("bee"), "the conflict line in both mods' notes")
    check("players.xml: legacy cee: refused" in note("cee"), "cee's note says its players.xml was refused")
    check("player[41] refused - no such record" in note("alpha"), "alpha's note: the record it named is none")
    check("players.xml: base bee," in note("bee"), "bee's note: its players.xml is the base")
    check("entities2.xml: result built again without content/ and patch/ of dee" in note("dee"), "dee's note says its patch was left out")

    print("6. the cache")
    keyf = os.path.join(SCRATCH, "files", "content-cache", "data", "players.xml.txt")
    t0 = os.path.getmtime(keyf)
    time.sleep(1.1)
    out, got = run("desk.exe", cfg, root, "repentance-resources/data/players.xml")
    check(os.path.getmtime(keyf) == t0 and handed(got, "players.xml") == model(cfg, "players.xml").data,
          "a second start: from the cache, the same bytes")
    put(os.path.join(cfg, "mods", "alpha", "patch", "players.xml"), b'<players><player id="13" items-add="45"/></players>')
    out, got = run("desk.exe", cfg, root, "repentance-resources/data/players.xml")
    check(os.path.getmtime(keyf) != t0 and handed(got, "players.xml") == model(cfg, "players.xml").data,
          "a changed patch: built again, the model's bytes")

    print("7. the clean build")
    shutil.rmtree(os.path.join(SCRATCH, "files", "content-cache"), ignore_errors=True)
    out, got = run("desk0.exe", cfg, root, "repentance-resources/data/players.xml")
    check(handed(got, "players.xml") == model(cfg, "players.xml").data, "desk0: the same bytes")
    check("content:" not in out, "desk0: no content: line")
    print("\n%d failures" % len(failures))
    for f in failures:
        print("  " + f.splitlines()[0])
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
