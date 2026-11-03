# -*- coding: utf-8 -*-
"""A third converter's output held to the expected one (04.10.2026).

    one output:
      py -3 tools/convdesk/compare.py <label | expected.json> <out.zip | unpacked folder> [--report <file>]
    a folder of outputs named by label (<label>.zip and <label>.zip.report.txt; no zip = nothing for the port):
      py -3 tools/convdesk/compare.py --dir <folder> [--only <label> ...]
    run the converter over the whole corpus and compare ({in}: the input zip, {out}: the zip to write;
    the converter writes <out>.report.txt beside it, as tools/modport.py does):
      py -3 tools/convdesk/compare.py --run "<command with {in} and {out}>" [--only <label> ...] [--work <dir>]
    options: --tree      xml/anm2 by the element tree, not by the bytes (a converter with its own serializer)
             --ref <zip> the reference's own output for the precise picture/xml diff (one output only;
                         by default <work>/py/<label>.zip from make_expected.py, used when it still is
                         the expected content, file by file)
             --max N     at most N lines per difference block (default 40)
             --adapted   the output is what the installer itself wrote (ModPort.convert with the installer's
                         label): mod.json's description is then {language: label, blank line, description}
                         for en, ru, de, es (UserMods.adaptedLabel) - held as such, not as a difference
    an installed mod's folder (mods/<folder>/ pulled from the phone) is taken as an output too: its report
    is <folder>/modport-report.txt (UserMods.ADAPTED_REPORT), which is not counted as one of its files.

What is held, in this order (the rules of tools/umdesk/test_converters.py for Python against Java):
  1. a zip or none - a script mod gets no zip, only the report;
  2. the report, line for line (a unified diff of it);
  3. the file names, exactly (the case too);
  4. each file: mod.json by its value (key by key); .png by its decoded RGBA pixels (the PNG encoders
     differ) - with the reference at hand: how many pixels, where, by how much; .xml/.anm2 by the bytes
     (Python and Java write the same), with --tree by the element tree - and either way the first
     differing elements by their path; the rest by the bytes (size, first differing offset).
Exit status 0 when everything is the same; 1 otherwise; the count of differences is printed last.
"""
import difflib
import json
import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus as C  # noqa: E402

MAX = 40


class Ref:
    """the reference's output zip, trusted file by file only where its bytes are the expected ones"""
    def __init__(self, path, expected):
        self.out = C.Output(path) if path and os.path.exists(path) else None
        self.files = expected['files']

    def read(self, n):
        if not self.out or n not in (self.out.names or []):
            return None
        d = self.out.read(n)
        import hashlib
        return d if hashlib.sha256(d).hexdigest() == self.files.get(n, {}).get('sha256') else None


def capped(lines, n=None):
    n = n or MAX
    return lines if len(lines) <= n else lines[:n] + ['... %d more lines' % (len(lines) - n)]


def path_list(tl):
    """element paths for a tree_list: 'AnimatedActor/Animations/Animation[3]/...'"""
    out, stack, count = [], [], [{}]
    for node in tl:
        if isinstance(node, str):
            stack.pop()
            count.pop()
            out.append(None)
            continue
        tag = node[0]
        i = count[-1].get(tag, 0)
        count[-1][tag] = i + 1
        stack.append('%s[%d]' % (tag, i) if i else tag)
        count.append({})
        out.append('/'.join(stack))
    return out


def tree_diff(exp_bytes, got_bytes):
    """the first differing elements, by path"""
    try:
        a, b = C.tree_list(exp_bytes), C.tree_list(got_bytes)
    except Exception as e:
        return ['does not parse: %s' % e]
    pa, pb = path_list(a), path_list(b)
    out = []
    sm = difflib.SequenceMatcher(None, [json.dumps(x) for x in a], [json.dumps(x) for x in b], autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == 'equal':
            continue
        for k in range(i1, i2):
            if pa[k]:
                out.append('- %s %s' % (pa[k], fmt_node(a[k])))
        for k in range(j1, j2):
            if pb[k]:
                out.append('+ %s %s' % (pb[k], fmt_node(b[k])))
        if len(out) > MAX:
            break
    return out or ['the trees are the same']


def fmt_node(n):
    attrs = ' '.join('%s="%s"' % (k, v) for k, v in n[1])
    return '<%s%s>%s' % (n[0], ' ' + attrs if attrs else '', (' text=%r' % n[2]) if n[2] else '')


def pixel_diff(exp_bytes, got_bytes):
    import numpy as np
    try:
        (wa, ha), pa = C.rgba(exp_bytes)
        (wb, hb), pb = C.rgba(got_bytes)
    except Exception as e:
        return ['unreadable: %s' % e]
    if (wa, ha) != (wb, hb):
        return ['size %dx%d, expected %dx%d' % (wb, hb, wa, ha)]
    a = np.frombuffer(pa, np.uint8).reshape(ha, wa, 4).astype(int)
    b = np.frombuffer(pb, np.uint8).reshape(hb, wb, 4).astype(int)
    d = np.abs(a - b)
    bad = d.max(axis=2) > 0
    ys, xs = np.nonzero(bad)
    out = ['%d of %d pixels differ, in x %d..%d, y %d..%d; the largest channel difference %d (by channel RGBA: %s)'
           % (len(xs), wa * ha, xs.min(), xs.max(), ys.min(), ys.max(), d.max(), [int(v) for v in d.reshape(-1, 4).max(axis=0)])]
    for k in range(min(3, len(xs))):
        x, y = xs[k], ys[k]
        out.append('at (%d,%d): expected %s, got %s' % (x, y, [int(v) for v in a[y, x]], [int(v) for v in b[y, x]]))
    return out


def json_diff(x, y, at=''):
    out = []
    if isinstance(x, dict) and isinstance(y, dict):
        for k in sorted(set(x) | set(y)):
            if k not in y:
                out.append('%s%s: missing (expected %r)' % (at, k, x[k]))
            elif k not in x:
                out.append('%s%s: not expected (got %r)' % (at, k, y[k]))
            else:
                out += json_diff(x[k], y[k], at + k + '.')
    elif x != y:
        out.append('%s: expected %r, got %r' % (at.rstrip('.') or 'value', x, y))
    return out


ADAPTED_REPORT = 'modport-report.txt'
ADAPTED_LANGS = ('en', 'ru', 'de', 'es')


def adapted_card(e, g):
    """the installer's card (UserMods.adaptedLabel put before the description) against the expected one"""
    if not isinstance(e, dict) or not isinstance(g, dict):
        return ['not a JSON object']
    out = json_diff({k: v for k, v in e.items() if k != 'description'},
                    {k: v for k, v in g.items() if k != 'description'})
    d, want = g.get('description'), e.get('description', '')
    if not isinstance(d, dict):
        return out + ["description: expected the installer's {language: label...}, got %r" % (d,)]
    if tuple(d) != ADAPTED_LANGS:
        out.append('description: languages %s, expected %s' % (list(d), list(ADAPTED_LANGS)))
    tail = ('\n\n' + want) if want else ''
    for lang, v in d.items():
        head = v[:len(v) - len(tail)] if isinstance(v, str) else ''
        if not isinstance(v, str) or not v.endswith(tail) or not head.strip() or '\n\n' in head:
            out.append('description.%s: expected "<label>%s", got %r' % (lang, tail.replace('\n', '\\n'), v))
    return out


def compare(exp, out_path, report_path=None, by_tree=False, ref_path=None, label=None, adapted=False):
    """prints the differences; returns their count"""
    label = label or exp['label']
    diffs = []

    def say(head, body=()):
        diffs.append(head)
        print('  DIFF  ' + head)
        for l in capped(list(body)):
            print('          ' + l)

    got = C.Output(out_path)
    if report_path is None:
        inside = os.path.join(out_path, ADAPTED_REPORT)
        report_path = inside if os.path.isdir(out_path) and os.path.exists(inside) else out_path.rstrip('/\\') + '.report.txt'
    if got.names and os.path.isdir(out_path):
        got.names = [n for n in got.names if n != ADAPTED_REPORT]
    ref = Ref(ref_path if ref_path is not None else os.path.join(C.WORK, 'py', label + '.zip'), exp)
    title = exp.get('title') or ''
    print('%s%s' % (label, ' (%s)' % title if title else ''))

    # 1. a zip or none
    if exp['zip'] != (got.names is not None):
        say('zip: expected %s, got %s' % ('a zip' if exp['zip'] else 'none (nothing for the port)',
                                          'a zip' if got.names is not None else 'none'))
    # 2. the report
    rep = C.report_lines(report_path)
    if rep is None:
        say('report: none at %s' % report_path)
    elif rep != exp['report']:
        ud = list(difflib.unified_diff(exp['report'], rep, 'expected', 'got', n=1, lineterm=''))
        say('report: %d lines differ' % sum(1 for l in ud if l[:1] in '+-' and l[:3] not in ('+++', '---')), ud)
    # 3. names
    ef = exp['files']
    gf = got.files() if got.names is not None else {}
    lower_e = {n.lower(): n for n in ef}
    for n in sorted(set(ef) - set(gf)):
        say('missing file: %s' % n)
    for n in sorted(set(gf) - set(ef)):
        hint = ' (expected as %s)' % lower_e[n.lower()] if n.lower() in lower_e else ''
        say('file not expected: %s%s' % (n, hint))
    # 4. per file
    same_pixels = same_tree = 0
    for n in sorted(set(ef) & set(gf)):
        e, g = ef[n], gf[n]
        if e['sha256'] == g['sha256']:
            continue
        r = ref.read(n)
        if 'json' in e:
            if adapted:
                body = adapted_card(e['json'], g.get('json'))
                if body:
                    say("%s: the installer's manifest differs" % n, body)
            elif e['json'] != g.get('json'):
                say('%s: the manifest differs' % n, json_diff(e['json'], g.get('json')))
        elif 'pixels' in e:
            if e['pixels'] == g.get('pixels'):
                same_pixels += 1
                continue
            body = pixel_diff(r, got.read(n)) if r is not None else [
                'size %s, expected %s; pixels differ (no reference output at hand for where)' % (g.get('wh'), e.get('wh'))]
            say('%s: pixels differ' % n, body)
        elif 'tree' in e:
            if e['tree'] == g.get('tree'):
                if by_tree:
                    same_tree += 1
                    continue
                say('%s: bytes differ, the element tree is the same (%d vs %d bytes; --tree accepts it)'
                    % (n, e['size'], g['size']), first_bytes(r, got.read(n)) if r is not None else [])
                continue
            body = tree_diff(r, got.read(n)) if r is not None else ['the trees differ (no reference output at hand for where)']
            say('%s: the element tree differs' % n, body)
        else:
            body = ['%d bytes, expected %d' % (g['size'], e['size'])]
            if r is not None:
                body += first_bytes(r, got.read(n))
            say('%s: bytes differ' % n, body)
    notes = []
    if same_pixels:
        notes.append('%d pictures: other bytes, the same pixels' % same_pixels)
    if same_tree:
        notes.append('%d xml/anm2: other bytes, the same tree' % same_tree)
    print('  %s%s' % ('the same' if not diffs else '%d differences' % len(diffs),
                      ' (%s)' % '; '.join(notes) if notes else ''))
    return len(diffs)


def stale(exp):
    """the reference converters changed since the expectation was made: say so once"""
    now = C.tools_used()
    moved = [k for k in ('modport.py', 'modport.js', 'gameref.bin') if exp.get('tools', {}).get(k) != now.get(k)]
    if moved:
        print('NOTE: %s changed since expected/ was made - rerun make_expected.py if the rules moved' % ', '.join(moved))


def first_bytes(a, b):
    if a is None or b is None:
        return []
    n = min(len(a), len(b))
    i = next((k for k in range(n) if a[k] != b[k]), n)
    lo = max(0, i - 40)
    return ['first difference at byte %d:' % i, 'expected ...%r' % a[lo:i + 60], 'got      ...%r' % b[lo:i + 60]]


def main():
    global MAX
    args = sys.argv[1:]
    if not args or args[0] in ('-h', '--help'):
        print(__doc__)
        return 2
    opt = {}
    pos, only = [], []
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--only':
            i += 1
            while i < len(args) and not args[i].startswith('--'):
                only.append(args[i])
                i += 1
            continue
        if a in ('--report', '--dir', '--run', '--work', '--ref', '--max'):
            opt[a] = args[i + 1]
            i += 2
            continue
        if a in ('--tree', '--adapted'):
            opt[a] = True
        else:
            pos.append(a)
        i += 1
    MAX = int(opt.get('--max', MAX))
    by_tree = bool(opt.get('--tree'))
    if '--work' in opt:
        C.WORK = opt['--work']
    total = 0
    if '--dir' in opt or '--run' in opt:
        labels = [m for m in C.corpus() if not only or m[0] in only]
        failed = []
        warned = False
        for label, src, kind in labels:
            ep = os.path.join(C.EXPECTED, label + '.json')
            if not os.path.exists(ep):
                print('%s: no expected/%s.json - run make_expected.py' % (label, label))
                failed.append(label)
                continue
            exp = C.load_expected(ep)
            if not warned:
                stale(exp)
                warned = True
            if '--run' in opt:
                inp = C.input_zip(label, src, kind, C.WORK)
                if C.digest_zip(inp) != exp['input_digest']:
                    print('%s: WARNING the input is not the one the expectation was made from - rerun make_expected.py' % label)
                od = os.path.join(C.WORK, 'third')
                os.makedirs(od, exist_ok=True)
                out = os.path.join(od, label + '.zip')
                for p in (out, out + '.report.txt'):
                    if os.path.exists(p):
                        os.remove(p)
                # split as a POSIX shell would: quote a path with spaces, write it with forward slashes
                cmd = [x.replace('{in}', inp).replace('{out}', out) for x in shlex.split(opt['--run'].replace('\\', '/'))]
                r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
                if r.returncode != 0:
                    print('%s: the converter exited %d: %s' % (label, r.returncode, (r.stdout + r.stderr).strip()[-600:]))
            else:
                out = os.path.join(opt['--dir'], label + '.zip')
            n = compare(exp, out, by_tree=by_tree, label=label, adapted=bool(opt.get('--adapted')))
            total += n
            if n:
                failed.append(label)
        print('\n%d mods, %d differ: %s' % (len(labels), len(failed), ', '.join(failed) or '-'))
    else:
        if len(pos) != 2:
            print(__doc__)
            return 2
        exp = C.load_expected(pos[0])
        stale(exp)
        total = compare(exp, pos[1], opt.get('--report'), by_tree, opt.get('--ref'), adapted=bool(opt.get('--adapted')))
    print('%d differences' % total)
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
