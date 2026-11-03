# -*- coding: utf-8 -*-
"""The desk check of content.c against its spec (tools/content_sim.py), byte for byte.

    py -3 tools/contentdesk/run.py [--keep] [--only N] [--refuse]

1. Runs the model's own --verify and --test with content_sim.build wrapped: every
   build they make (the empty set over all 144 data files of every edition, the
   set/restore and add/unset round trips of every Repentance table, the stringtable
   port fill, the synthetic mods of the tests) is a case - its input files and the
   model's output bytes, report lines and base.
2. Writes each case's inputs to a scratch folder and runs desk.exe (the real
   content.c, built by build.sh) over all of them.
3. Compares: the bytes, the lines, the base. Prints every difference; exit 1 on any.

--refuse: desk.exe answers the port's refusal hook with "no" (it must change nothing).
"""
import io, os, shutil, subprocess, sys, contextlib, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import content_sim as cs  # noqa: E402

CASES = []
_orig_build = cs.build
_disk = {}


def _disk_file(d, name):
    k = (d, name)
    if k not in _disk:
        p = os.path.join(d, name)
        _disk[k] = open(p, "rb").read() if os.path.exists(p) else None
    return _disk[k]


def _recording_build(fname, mods, game=None, port=None):
    r = _orig_build(fname, mods, game, port)
    g = game or cs.Game()
    over = {}
    for name, data in g._files.items():
        if data is not None and data != _disk_file(g.dir, name):
            over[name] = data
    ms = []
    for m in mods:
        ms.append((m.id, m.get("resources/" + fname), m.get("content/" + fname), m.get("patch/" + fname)))
    CASES.append(dict(fname=fname, game=g.dir, other=g.other_dir, over=over, port=port, mods=ms,
                      data=r.data, lines=list(r.lines), base=r.base))
    return r


def extra():
    """edge cases beyond the model's own tests: whatever the model makes of them, C must make the same"""
    M, B = cs.Mod, cs.build
    g = cs.Game()
    P = "players.xml"
    B(P, [M("q", {"patch/" + P: b'<players><player id="0" name=\'a"b\' skin="x\'y&quot;"/><player id="1" name="it\'s"/></players>'})], g)
    B(P, [M("dup", {"patch/" + P: b'<players><player id="2" coins="1" coins="2" unset="hp  armor"/><player id="2" items="1,2" items-add="3" items-remove="1"/></players>'})], g)
    B(P, [M("lst", {"patch/" + P: b'<players><player id="3" items-remove="1,1" items-add="5, 6"/><player id="4" unset="items" items-add="7"/></players>'})], g)
    B(P, [M("u8", {"patch/" + P: b'<players><player id="5" name="\xff\xfe\xc3(\xe2\x82 ok"/><player id="6" \xe9t\xe9="1"/></players>'})], g)
    B(P, [M("bad", {"patch/" + P: b'<players><player id="7" coins="1"'})], g)
    B(P, [M("bad2", {"patch/" + P: b'<players><!-- open'})], g)
    B(P, [M("root", {"patch/" + P: b'<players portraitroot="x/" unset="root" frob="1" items-add="1"><nope/><player/><player id="8" remove="true"/></players><other/>'})], g)
    B(P, [M("kid", {"patch/" + P: b'<players><player id="9"><hair gfx="h.png"/><Background gfx="b.png"/><other x="1"/></player></players>'})], g)
    B(P, [M("refs", {"patch/" + P: b'<players><player id="10" card="999" pill="3" trinket="-1" costume="7" items-add="-584,abc"/></players>'})], g)
    B(P, [M("neg", {"patch/" + P: b'<players><player id="11" items-add="-1"/><player id="12" items=" -5"/><player id="13" items-add="1"/></players>'})], g)
    B(P, [M("leg-bad", {"resources/" + P: b'<players><player id="1"'}), M("leg-ok", {"resources/" + P: g.file(P)})], g)
    B(P, [M("leg-skip", {"resources/" + P: g.file(P).replace(b"</players>", b'<player id="x41"/></players>')})], g)
    B("costumes2.xml", [M("c", {"patch/costumes2.xml": b'<costumes><costume id="1" type="passive" priority="3"/><costume id="1" type="active" priority="4"/><costume id="5" priority="1"/></costumes>'})], g)
    I = "itempools.xml"
    B(I, [M("p1", {"patch/" + I: b'<ItemPools><Pool Name="treasure"><Item Id="1" remove="true"/><Item Id="1" Weight="2"/><Item Id="999" remove="true"/><Item Id="5" DecreaseBy="x" Weight="1"/><Item Weight="1"/></Pool></ItemPools>'}),
          M("p2", {"patch/" + I: b'<ItemPools><Pool Name="treasure"><Item Id="2" Weight="9"/><Item Id="3" remove="true"/></Pool><Pool Name="shop"><Item Id="12" Weight="1" DecreaseBy="1" RemoveOn="0.1"/></Pool></ItemPools>'})], g)
    B("bosspools.xml", [M("bp", {"patch/bosspools.xml": b'<bosspools><pool name="basement"><boss id="9999" weight="1"/><boss id="19" weight="2"/></pool></bosspools>'})], g)
    E = "entities2.xml"
    B(E, [M("e1", {"content/" + E: b'<entities anm2root="gfx/monsters/"><entity id="10" variant="78" anm2path="x.anm2"/><entity id="10" variant="5000"/><entity id="10" variant="79" subtype="300"/></entities>'}),
          M("e2", {"content/" + E: b'<entities anm2root="other/"><entity id="10" variant="78" anm2path="y.anm2"/><entity id="10" variant="80"/></entities>'}),
          M("e3", {"content/" + E: b'<entities/>'}), M("e4", {"content/" + E: b'<entities><entity'}),
          M("e5", {"patch/" + E: b'<entities><entity id="10" variant="78" name="patched"/><entity id="10" baseHP="5"><bestiary parent="10.1"/><gibs blood="1"/></entity></entities>'})], g)
    S = cs.STRINGTABLE
    B(S, [M("s1", {"patch/" + S: '<stringtable><category name="Default"><key name="PROMPT_OK"><string>A</string><string/><string lang="ru">Б</string><string lang="xx">x</string><string>1</string><string>2</string><string>3</string><string>4</string><string>5</string><string>6</string><string>7</string></key><key name="NEW_ONE"><string lang="Japanese">j</string></key><key name="NEW_ONE"/></category><category name="Nope"/></stringtable>'.encode()}),
          M("s2", {"patch/" + S: b'<stringtable><category name="Default"><key name="PROMPT_OK"><string>B</string></key></category></stringtable>'})], g)
    B(S, [M("sl", {"resources/" + S: b'<notatable/>'}), M("sl2", {"resources/" + S: g.file(S)})], g, port=b'<stringtable><category name="Default"><key name="PROMPT_OK"><string>Z</string></key></category></stringtable>')
    B("unknown.xml", [M("x", {"patch/unknown.xml": b"<a/>"})], g)
    B("translations.xml", [M("x", {"resources/translations.xml": b"<t/>"}), M("y", {"resources/translations.xml": b"<u/>"})], g)
    B("challenges.xml", [M("ch", {"patch/challenges.xml": b'<challenges version="2"><challenge id="1" startingitems-add="-584" roomfilter-add="1,1" achievements-remove="x"/></challenges>'})], g)
    B("fxlayers.xml", [M("fx", {"patch/fxlayers.xml": b'<fxParams><params stage="1" shadowAlpha="0.5"><colorModifier r="1"/></params></fxParams><fxLayers><fx id="1" layer="2"/></fxLayers>'})], g)
    # M1 mended (02.10): odd forms of a whole players.xml / challenges.xml with ids that are no items
    gp = g.file(P)
    B(P, [M("m1", {"resources/" + P: gp.replace(b'<player id="13"', b'<player items="9999" id="13" items=\'1, 9999 ,abc,,2\' ', 1)})], g)
    B(P, [M("m2", {"resources/" + P: b'<players><player id="0" items="9999,-1"/><player id="1" name=" " items="x 43\t61"/>'
                                       b'<player id="2" name="a&quot;b" items="1,1,9999,9999"/><player id="3" items=""/><player items="0"/></players>'})], g)
    B(P, [M("m3", {"resources/" + P: b'<players><player id="4" name="\xe9\xff" items="9999" trinket="999" pocketActive="9999"/></players>'})], g)
    C = "challenges.xml"
    B(C, [M("mc", {"resources/" + C: g.file(C).replace(b'id="2" startingitems="209,6,236,291"',
                                                       b'id="2" startingitems="-0,+5,-9999" startingitems2="9999,-584" startingtrinkets="-2,0,999"', 1)})], g)
    B(C, [M("mc2", {"resources/" + C: b'<challenges><challenge id="1" startingitems="1,9999"/><challenge startingitems="9999"/></challenges>'}),
          M("mc3", {"patch/" + C: b'<challenges><challenge id="1" startingitems-add="2"/></challenges>'})], g)
    B("recipes.xml", [M("r", {"resources/recipes.xml": g.file("recipes.xml").replace(b"</recipes>", '<recipe input="аб" output="1"/></recipes>'.encode())})], g)
    # 1a VERSION (04.10.2026): whole tables made for Repentance+, merged three ways
    W = r"Z:/Games/Steam/steamapps/workshop/content/250900/3794486410"
    if os.path.isdir(W):
        sm = M.from_dir(W)
        for f in ("players.xml", "items.xml", "pocketitems.xml"):
            B(f, [sm], g)
    for f in sorted(cs.T):
        o = g.other(f)
        if o is None or not cs.T[f].read:
            continue
        B(f, [M("rp-" + f, {"resources/" + f: o})], g)                       # Repentance+'s own file as a mod
        B(f, [M("crlf", {"resources/" + f: o.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")})], g)
    rp = g.other(P)
    if rp is not None:
        odd = rp.replace(b'<player id="2" ', b"<player id='2' items='1' ", 1).replace(b'<player id="5" ', b'<player id="5" name="Eve" name="x\'y&quot;" ', 1)
        odd = odd.replace(b"<players ", b'<players frob="1" ', 1)
        B(P, [M("odd", {"resources/" + P: odd}), M("weak", {"resources/" + P: rp})], g)
        B(P, [M("dup", {"resources/" + P: rp.replace(b"</players>", b'<player id="2" name="twice"/></players>', 1)})], g)
        B(P, [M("bad", {"resources/" + P: rp[:len(rp) // 2]}), M("rp2", {"resources/" + P: rp})], g)
    ri = g.other("items.xml")
    if ri is not None:
        B("items.xml", [M("own", {"resources/items.xml": ri.replace(b"</items>", b'<null id="140" name="mine"/></items>', 1)})], g)



def record():
    cs.build = _recording_build
    sink = io.StringIO()
    with contextlib.redirect_stdout(sink):
        ok_v = cs.verify()
        ok_t = cs.tests()
        n = len(CASES)
        extra()
    cs.build = _orig_build
    print("model: %d extra edge cases" % (len(CASES) - n))
    return ok_v, ok_t


def write_cases(top):
    dirs = []
    for i, c in enumerate(CASES):
        d = os.path.join(top, "%04d" % i)
        os.makedirs(d, exist_ok=True)
        spec = ["fname=" + c["fname"], "game=" + c["game"].replace("\\", "/")]
        if c.get("other"):
            spec.append("other=" + c["other"].replace("\\", "/"))
        if c["port"] is not None:
            open(os.path.join(d, "port.bin"), "wb").write(c["port"])
            spec.append("port=port.bin")
        for name, data in c["over"].items():
            os.makedirs(os.path.join(d, "over"), exist_ok=True)
            open(os.path.join(d, "over", name), "wb").write(data)
        for k, (mid, leg, con, pat) in enumerate(c["mods"]):
            md = os.path.join(d, "mods", str(k))
            os.makedirs(md, exist_ok=True)
            for nm, data in (("legacy", leg), ("content", con), ("patch", pat)):
                if data is not None:
                    open(os.path.join(md, nm), "wb").write(data)
            spec.append("mod=" + mid)
        open(os.path.join(d, "case.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(spec) + "\n")
        dirs.append(d.replace("\\", "/"))
    lst = os.path.join(top, "list.txt")
    open(lst, "w", encoding="utf-8", newline="\n").write("\n".join(dirs) + "\n")
    return lst, dirs


def first_diff(a, b):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i
    return n


def main(argv):
    ok_v, ok_t = record()
    print("model: --verify %s, --test %s, %d builds recorded" % ("OK" if ok_v else "FAILED", "OK" if ok_t else "FAILED",
                                                                  len(CASES)))
    if "--only" in argv:
        keep = int(argv[argv.index("--only") + 1])
        sel = [CASES[keep]]
        CASES[:] = sel
    top = tempfile.mkdtemp(prefix="contentdesk-")
    try:
        lst, dirs = write_cases(top)
        exe = os.path.join(HERE, "desk.exe")
        args = [exe, lst] + (["refuse"] if "--refuse" in argv else [])
        p = subprocess.run(args, capture_output=True)
        if p.returncode:
            print("desk.exe failed:", p.returncode, p.stderr.decode("utf-8", "replace")[-2000:])
            return 1
        bad = 0
        for c, d in zip(CASES, dirs):
            got_p = os.path.join(d, "got.bin")
            got = open(got_p, "rb").read() if os.path.exists(got_p) else None
            got_lines = open(os.path.join(d, "got_lines.txt"), "rb").read()
            got_base = open(os.path.join(d, "got_base.txt"), "rb").read().decode("utf-8")
            want_lines = "".join(l + "\n" for l in c["lines"]).encode("utf-8")
            what = []
            if got != c["data"]:
                if got is None or c["data"] is None:
                    what.append("data: %s vs %s" % ("none" if got is None else len(got), "none" if c["data"] is None else len(c["data"])))
                else:
                    i = first_diff(got, c["data"])
                    what.append("data differs at %d (C %d bytes, model %d): C %r | model %r" % (
                        i, len(got), len(c["data"]), got[max(0, i - 40):i + 60], c["data"][max(0, i - 40):i + 60]))
            if got_lines != want_lines:
                gl, wl = got_lines.decode("utf-8", "replace").split("\n"), want_lines.decode("utf-8").split("\n")
                for j in range(max(len(gl), len(wl))):
                    a = gl[j] if j < len(gl) else "<none>"
                    b = wl[j] if j < len(wl) else "<none>"
                    if a != b:
                        what.append("line %d:\n        C     %s\n        model %s" % (j, a, b))
                        break
            if got_base != c["base"]:
                what.append("base: C %s, model %s" % (got_base, c["base"]))
            if what:
                bad += 1
                if bad <= 25:
                    print("case %s %s [%s] (%s):" % (os.path.basename(d), c["fname"], c["game"][-40:],
                                                    ",".join(m[0] for m in c["mods"])))
                    for w in what:
                        print("    " + w)
        print("\nDESK %s: %d cases, %d differ" % ("OK" if not bad else "FAILED", len(CASES), bad))
        return 0 if not bad and ok_v and ok_t else 1
    finally:
        if "--keep" in argv:
            print("cases kept in", top)
        else:
            shutil.rmtree(top, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
