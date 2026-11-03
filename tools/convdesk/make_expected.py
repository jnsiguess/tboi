# -*- coding: utf-8 -*-
"""The expected outputs of the converter, from the two reference converters (04.10.2026).

    py -3 tools/convdesk/make_expected.py [--only <label> ...] [--work <dir>] [--jobs N] [--no-run]

For every mod of the corpus (corpus.py: 25 workshop mods, reflashed-ported, example-mod) runs
tools/modport.py (the reference; the game is work/app/repentance-resources) and the web page's
src/modport.js under node (tools/umdesk/modport_node.js; the game is src/gameref.bin, the very file the
APK carries) on the same input zip, and writes tools/convdesk/expected/<label>.json:

    label, title, source, input (the zip fed), input_digest (its content), tools (sha256 of modport.py,
    modport.js, gameref.bin, the node shim), zip (was one written), report (Python's report.txt, by
    line), files {name: size, sha256, pixels | tree | json} of Python's zip, and
    js {agrees, differences} - where the page differs from Python (a converter bug: reported, not fixed).

Outputs stay in <work>/py/<label>.zip and <work>/js/<label>.zip (+ .report.txt); compare.py uses the
Python one for a precise picture/xml diff when its content is still the expected one.
--no-run rebuilds the json from outputs already in <work>.
"""
import concurrent.futures as cf
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import corpus as C  # noqa: E402


def agree(py, js):
    """the page against Python: the same zip-or-none, report, names; pixels, trees, json, other bytes"""
    d = []
    if py['zip'] != js['zip']:
        d.append('zip written: Python %s, page %s' % (py['zip'], js['zip']))
    if py['report'] != js['report']:
        a, b = py['report'] or [], js['report'] or []
        for l in [x for x in a if x not in b]:
            d.append('report, only Python: ' + l)
        for l in [x for x in b if x not in a]:
            d.append('report, only page:   ' + l)
        if not [x for x in a if x not in b] and not [x for x in b if x not in a]:
            d.append('report: the same lines in another order')
    fa, fb = py['files'], js['files']
    for n in sorted(set(fa) - set(fb)):
        d.append('file only Python: ' + n)
    for n in sorted(set(fb) - set(fa)):
        d.append('file only page:   ' + n)
    for n in sorted(set(fa) & set(fb)):
        x, y = fa[n], fb[n]
        if x['sha256'] == y['sha256']:
            continue
        for key in ('json', 'pixels', 'tree'):
            if key in x:
                if x[key] != y.get(key):
                    d.append('%s: %s differ%s' % (n, key, ' (%s vs %s)' % (x.get('wh'), y.get('wh'))
                                                  if key == 'pixels' and x.get('wh') != y.get('wh') else ''))
                break
        else:
            d.append('%s: bytes differ' % n)
    return d


def side(work, who, label):
    out = os.path.join(work, who, label + '.zip')
    o = C.Output(out)
    return {'zip': o.names is not None, 'report': C.report_lines(out + '.report.txt'), 'files': o.files()}


def main():
    args = sys.argv[1:]
    only, work, jobs, no_run = [], C.WORK, 3, False
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--only':
            i += 1
            while i < len(args) and not args[i].startswith('--'):
                only.append(args[i])
                i += 1
            continue
        if a == '--work':
            work = args[i + 1]
            i += 1
        elif a == '--jobs':
            jobs = int(args[i + 1])
            i += 1
        elif a == '--no-run':
            no_run = True
        i += 1
    mods = [m for m in C.corpus() if not only or m[0] in only]
    for who in ('py', 'js'):
        os.makedirs(os.path.join(work, who), exist_ok=True)
    os.makedirs(C.EXPECTED, exist_ok=True)
    inputs = {}
    for label, src, kind in mods:
        inputs[label] = C.input_zip(label, src, kind, work)
    tools = C.tools_used()
    logs = {}

    def job(label, who):
        out = os.path.join(work, who, label + '.zip')
        for p in (out, out + '.report.txt'):
            if os.path.exists(p):
                os.remove(p)
        t = time.time()
        code, msg = (C.run_py if who == 'py' else C.run_js)(inputs[label], out)
        return label, who, code, msg.splitlines()[-1] if msg else '', time.time() - t

    if not no_run:
        # the heavy one first, so it does not finish last alone
        order = sorted(mods, key=lambda m: -os.path.getsize(inputs[m[0]]))
        with cf.ThreadPoolExecutor(jobs) as ex:
            futs = [ex.submit(job, label, who) for label, _, _ in order for who in ('py', 'js')]
            for f in cf.as_completed(futs):
                label, who, code, last, dt = f.result()
                logs[(label, who)] = (code, last)
                print('%-18s %-2s exit %d %5.0fs  %s' % (label, who, code, dt, last[:150]), flush=True)

    index = {'made': time.strftime('%Y-%m-%d %H:%M'), 'tools': tools, 'game': C.GAME, 'mods': {}}
    disagreements = 0
    for label, src, kind in mods:
        py, js = side(work, 'py', label), side(work, 'js', label)
        diffs = agree(py, js)
        if (label, 'py') in logs and logs[(label, 'py')][0] != 0:
            diffs.insert(0, 'Python exited %d: %s' % logs[(label, 'py')])
        if (label, 'js') in logs and logs[(label, 'js')][0] != 0:
            diffs.insert(0, 'page exited %d: %s' % logs[(label, 'js')])
        mid = (py['files'].get('mod.json') or {}).get('json', {})
        doc = {
            'label': label,
            'title': C.title_of(src, kind),
            'source': src,
            'input': inputs[label],
            'input_digest': C.digest_zip(inputs[label]),
            'tools': tools,
            'zip': py['zip'],
            'id': mid.get('id') if isinstance(mid, dict) else None,
            'report': py['report'],
            'files': py['files'],
            'js': {'agrees': not diffs, 'differences': diffs},
        }
        with open(os.path.join(C.EXPECTED, label + '.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(doc, f, ensure_ascii=False, indent=1, sort_keys=False)
        index['mods'][label] = {'title': doc['title'], 'zip': doc['zip'], 'id': doc['id'], 'files': len(doc['files']),
                                'report_lines': len(doc['report'] or []), 'js_agrees': not diffs,
                                'js_differences': len(diffs)}
        print('%-18s %-45s %s %5d files  page %s' % (label, doc['title'][:45], 'zip  ' if doc['zip'] else 'NO ZIP',
                                                     len(doc['files']), 'agrees' if not diffs else '%d DIFFERENCES' % len(diffs)))
        for d in diffs[:12]:
            print('      ' + d[:220])
        if len(diffs) > 12:
            print('      ... %d more in expected/%s.json' % (len(diffs) - 12, label))
        disagreements += bool(diffs)
    ip = os.path.join(C.EXPECTED, '_index.json')
    if only and os.path.exists(ip):
        old = json.load(open(ip, encoding='utf-8'))
        old['mods'].update(index['mods'])
        index['mods'] = old['mods']
    with open(ip, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(index, f, ensure_ascii=False, indent=1)
    print('\n%d mods, %d where the page and Python disagree' % (len(mods), disagreements))
    return 1 if disagreements else 0


if __name__ == '__main__':
    sys.exit(main())
