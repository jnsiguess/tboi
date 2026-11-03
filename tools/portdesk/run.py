# -*- coding: utf-8 -*-
"""portpatch.c against its twin tools/anm2patch.py, byte for byte (plan 30.09, 4.9 M8).

Cases: every patch of port_patch/index.txt over its base as the game would have
it, the result patched again (twice == once), the port's own file patched, the
PC's .anm2 of the same name patched (a mod's file with another structure: the
report lines must agree too), every stick size variant, and the numbers
shim_port_layer reads (every layer and null of every result, by name).

  bash tools/portdesk/build.sh && py -3 tools/portdesk/run.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
import anm2patch as ap
import effective as ef
import port_additions as pa
import port_patch as pp

EXE = os.path.join(HERE, "portdesk.exe")
PC = os.path.join(pa.ROOT, "work", "pc-resources")
TMP = tempfile.mkdtemp(prefix="portdesk")


def _w(name, b):
    p = os.path.join(TMP, name)
    open(p, "wb").write(b)
    return p


def c_apply(base, patch):
    out = os.path.join(TMP, "out")
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run([EXE, "apply", _w("base", base), _w("patch", patch), out], capture_output=True)
    lines = r.stdout.decode("utf-8", "replace").splitlines()
    if r.returncode:
        return None, lines
    return open(out, "rb").read(), lines


def c_variant(base, pct):
    out = os.path.join(TMP, "out")
    r = subprocess.run([EXE, "variant", _w("base", base), str(pct), out], capture_output=True)
    return open(out, "rb").read() if r.returncode == 0 else None


def c_number(b, kind, name):
    r = subprocess.run([EXE, "number", _w("num", b), kind, name], capture_output=True)
    return int(r.stdout.decode().strip())


def pc_twin(target):
    """the PC's file of the same name, the language set's or the plain one"""
    ed, st, rel = target.split("/", 2)
    lang = st.split(".")[-1] if st != "data" else ""
    for d in (["resources-dlc3." + lang] if lang else []) + ["resources-dlc3", "resources"]:
        p = os.path.join(PC, d, *rel.split("/"))
        if os.path.isfile(p):
            return p
    return None


def main():
    after = ef.After()
    ok = True
    n = 0
    for ln in open(os.path.join(pp.OUT, pp.INDEX), encoding="utf-8").read().splitlines():
        f = ln.split("\t")
        if f[0] == "patch":
            patch = after._asset(f[2])
            base = after._base(f[3] if len(f) > 3 else f[1])
            cases = [("base", base), ("twice", after.get(f[1])), ("port", pa.port_bytes(f[1]))]
            twin = pc_twin(f[1])
            if twin:
                cases.append(("pc", open(twin, "rb").read()))
            # all or nothing (02.10.2026): the game's own file and the port's own version land whole; the PC's twin
            # is told (the guard completes its missing nulls on the phone before this question is asked)
            base_path = os.path.join(TMP, "whole-base")
            open(base_path, "wb").write(base)
            for what, b in cases:
                if b is None:
                    continue
                r = subprocess.run([EXE, "whole", _w("mine", b), base_path, _w("wpatch", patch)], capture_output=True)
                verdict = r.stdout.decode("utf-8", "replace").strip()
                n += 1
                if what in ("base", "twice", "port") and verdict != "0":
                    ok = False
                    print("  NOT WHOLE %-6s %s: %s" % (what, f[1], verdict))
                elif what == "pc":
                    print("  pc whole? %-60s %s" % (f[1], verdict[:110]))
            for what, b in cases:
                if b is None:
                    continue
                rep = []
                try:
                    want = ap.apply(b, patch, rep)
                except ap.ParseError:
                    want = None
                got, lines = c_apply(b, patch)
                n += 1
                same = got == want and lines == rep
                ok &= same
                if not same:
                    print("  DIFFERENT %-6s %s  (C %s B, Python %s B)" % (what, f[1], len(got or b""), len(want or b"")))
                    for x, y in zip(lines + [""] * 9, rep + [""] * 9):
                        if x != y:
                            print("      C: %s\n      P: %s" % (x, y))
                            break
                elif what == "pc":
                    print("  pc %-66s %d report lines, same" % (f[1], len(rep)))
                # the numbers of the result
                if want is not None and what in ("base", "pc"):
                    m = ap.Model(ap.parse(want))
                    for part, tag, kind in ((m.layers, "Layer", "layer"), (m.nulls, "Null", "null")):
                        names = [x.get("Name") for x in part.kids if x.tag == tag]
                        for nm in names[:: max(1, len(names) // 6)] + ["NoSuchName"]:
                            exp = next((int(x.get("Id")) for x in part.kids if x.tag == tag and x.get("Name") == nm), -1)
                            got_n = c_number(want, kind, nm)
                            n += 1
                            if got_n != exp:
                                ok = False
                                print("  NUMBER %s %s %s: C %d, Python %d" % (f[1], kind, nm, got_n, exp))
        elif f[0] == "variants":
            base = after.get(f[1])
            for pct in f[2].split(","):
                n += 1
                want = pp.variant(base, int(pct))
                got = c_variant(base, int(pct))
                if got != want:
                    ok = False
                    print("  DIFFERENT variant %s" % pct)
    print("PORTDESK", "OK" if ok else "DIFFERENT", "(%d cases)" % n)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
