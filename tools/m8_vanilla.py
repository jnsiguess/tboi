# -*- coding: utf-8 -*-
"""work/app goes back to vanilla; the port's versions move to work/port-edit (plan 30.09, 4.9 M8).

The pak is packed from work/app, so work/app is the game's own files as the IPA
has them. What the port changed or added in them - registered in
port_additions.py - lives on in work/port-edit, where the authoring tools
(strap_layers' heirs, lang, labels, dpad_art...) now read and write, and
tools/port_patch.py turns it into port_patch/ (patches and port files laid over
the pak at start, portpatch.c).

For every registered file, in this order, each step only if not done yet:
  1. its folder (not recursively) copied into work/port-edit - the tools list
     language sets and read neighbours, so they find the same tree there;
  2. its reference copies (*.before-*, *.do-*, *.orig, REFERENCE) MOVED there:
     the tools build from them;
  3. the port's version kept beside the vanilla one as <name>.before-m8
     (the rule: the untouched copy stays next to the edited file);
  4. the IPA's bytes written (a changed file), or the file removed (one the IPA
     has not got).
port_additions.IN_PAK_TRANSITION is left alone.

It refuses to start unless the port's additions are already all in port_patch/
and give the same picture: port_patch.py --check, anm2patch --selftest and
effective.py must pass first - otherwise the revert would lose them.

  py -3 tools/m8_vanilla.py --plan
  py -3 tools/m8_vanilla.py --do
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import port_additions as pa


def _ref_copies(folder, leaf):
    out = []
    if not os.path.isdir(folder):
        return out
    for f in os.listdir(folder):
        if f.lower().endswith(".before-m8"):
            continue                                # the port's version kept beside the vanilla one: stays
        if f != leaf and f.lower().startswith(leaf.lower() + ".") and pa.TAIL.search(f):
            out.append(f)
        elif f != leaf and f.lower().startswith(leaf.lower()) and "REFERENCE" in f:
            out.append(f)
    return sorted(out)


def plan():
    steps = []
    mirrored = set()
    moved = set()
    for e in pa.entries():
        if e["path"] in pa.IN_PAK_TRANSITION:
            continue
        parts = e["path"].split("/")
        d = os.path.join(pa.APP, *parts[:-1])
        moved.update(os.path.join(d, f) for f in _ref_copies(d, parts[-1]))
    for e in pa.entries():
        rel = e["path"]
        if rel in pa.IN_PAK_TRANSITION:
            continue
        parts = rel.split("/")
        app_dir = os.path.join(pa.APP, *parts[:-1])
        edit_dir = os.path.join(pa.PORT_EDIT, *parts[:-1])
        leaf = parts[-1]
        # the folders the authoring tools work in (gfx/...) are mirrored whole; a data table is taken alone
        if app_dir not in mirrored and "/gfx/" not in "/" + rel:
            mirrored.add(app_dir)
            if os.path.isfile(os.path.join(app_dir, leaf)) and not os.path.exists(os.path.join(edit_dir, leaf)):
                steps.append(("copy", os.path.join(app_dir, leaf), os.path.join(edit_dir, leaf)))
        if app_dir not in mirrored:
            mirrored.add(app_dir)
            if os.path.isdir(app_dir):
                for f in sorted(os.listdir(app_dir)):
                    src = os.path.join(app_dir, f)
                    if (os.path.isfile(src) and not os.path.exists(os.path.join(edit_dir, f)) and src not in moved
                            and not f.lower().endswith(".before-m8")):
                        steps.append(("copy", src, os.path.join(edit_dir, f)))
        for f in _ref_copies(app_dir, leaf):
            steps.append(("move", os.path.join(app_dir, f), os.path.join(edit_dir, f)))
        app_file = os.path.join(app_dir, leaf)
        st = pa.app_state(rel)
        if st in ("modified", "added"):
            keep = app_file + ".before-m8"
            if not os.path.exists(keep):
                steps.append(("keep", app_file, keep))
            if st == "modified":
                steps.append(("vanilla", rel, app_file))
            else:
                steps.append(("remove", app_file, None))
    return steps


def preconditions():
    import subprocess
    py = sys.executable
    for args in (["port_patch.py", "--check"], ["anm2patch.py", "--selftest"], ["effective.py"]):
        r = subprocess.run([py, os.path.join(HERE, args[0])] + args[1:], capture_output=True, text=True)
        last = (r.stdout.strip().splitlines() or ["?"])[-1]
        print("  %-28s %s" % (" ".join(args), last))
        if r.returncode:
            return False
    return True


def do(steps):
    for kind, a, b in steps:
        if kind in ("copy", "keep"):
            os.makedirs(os.path.dirname(b), exist_ok=True)
            shutil.copy2(a, b)
        elif kind == "move":
            os.makedirs(os.path.dirname(b), exist_ok=True)
            if os.path.exists(b):
                if open(a, "rb").read() != open(b, "rb").read():
                    sys.exit("refused: %s and %s differ - which is the reference?" % (a, b))
                os.remove(a)
            else:
                shutil.move(a, b)
        elif kind == "vanilla":
            data = pa.vanilla_bytes(a)
            if data is None:
                sys.exit("refused: the IPA has no %s" % a)
            tmp = b + ".tmp-m8"
            open(tmp, "wb").write(data)
            os.replace(tmp, b)
        elif kind == "remove":
            os.remove(a)


def main(argv):
    steps = plan()
    count = {}
    for k, a, b in steps:
        count[k] = count.get(k, 0) + 1
    if "--plan" in argv or "--do" not in argv:
        for k, a, b in steps:
            if k != "copy":
                print("  %-7s %s%s" % (k, os.path.relpath(a, pa.ROOT) if os.path.isabs(a) else a,
                                       (" -> " + os.path.relpath(b, pa.ROOT)) if b else ""))
        print("M8 VANILLA plan: %s" % (", ".join("%s %d" % kv for kv in sorted(count.items())) or "nothing to do"))
        return 0
    if not steps:
        print("M8 VANILLA: nothing to do - work/app is already vanilla")
        return 0
    print("preconditions:")
    if not preconditions():
        sys.exit("refused: the port's additions are not all in port_patch/ yet")
    # the port's versions are copied out BEFORE anything in work/app changes
    do([s for s in steps if s[0] in ("copy", "move", "keep")])
    for e in pa.entries():
        if e["path"] in pa.IN_PAK_TRANSITION:
            continue
        p = os.path.join(pa.PORT_EDIT, *e["path"].split("/"))
        if pa.app_state(e["path"]) in ("modified", "added") and not os.path.isfile(p):
            sys.exit("refused: the port's version of %s did not reach work/port-edit" % e["path"])
    do([s for s in steps if s[0] in ("vanilla", "remove")])
    print("M8 VANILLA done: %s" % ", ".join("%s %d" % kv for kv in sorted(count.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
