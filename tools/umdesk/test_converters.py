# -*- coding: utf-8 -*-
"""ONE SOURCE OF TRUTH: the three converters on the same mods (04.10.2026).

    py -3 tools/umdesk/test_converters.py [work dir] [--only <workshop id> ...]

The converter lives three times: tools/modport.py (the reference), the web page's modport.js and the
installer's ModPort.java (the phone adapts a PC mod at ADD MOD with it). For every mod of the owner's
workshop folder (25, Reflashed among them) and a synthetic mod for the rules those do not reach (an old
text table to merge, a bare & and a repeated attribute, a room of another version, a palette picture
and an interlaced grey one to bake), all three convert the same zip:

  - the reports must be the same text, line for line;
  - Python and Java: the same files, the same bytes - pictures by their pixels (the PNG writers differ);
  - the page and Java: the same files, xml by its tree (the page's serializer writes other bytes), the
    pictures by their pixels.
Java runs with a 96 MB heap (a phone gives an app 192-512): Reflashed converts in it.
Needs: py -3 with PIL and numpy; node; the JDK of Android Studio; tools/modport_ref.py's gameref.bin.
"""
import os
import shutil
import subprocess
import sys
import zipfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import compare_outputs as C  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
WS = r'Z:/Games/Steam/steamapps/workshop/content/250900'
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
REF = os.path.join(ROOT, 'other stuff', 'usermods', 'modport-web', 'src', 'gameref.bin')
JAVA_SRC = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'java', 'ru', 'isaac', 'port')
JDK = r'C:/Program Files/Android/Android Studio/jbr/bin'
SDK = (os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
       or os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk'))
AJ = os.path.join(SDK, 'platforms', 'android-35', 'android.jar')
args = [a for a in sys.argv[1:]]
only = []
if '--only' in args:
    only = args[args.index('--only') + 1:]
    args = args[:args.index('--only')]
WORK = args[0] if args else os.path.join(os.environ.get('TEMP', '.'), 'converters-desk')
failures = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        failures.append(what)


def zip_folder(src, dst):
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_STORED) as z:
        for dp, dn, fn in os.walk(src):
            for f in sorted(fn):
                full = os.path.join(dp, f)
                z.write(full, os.path.relpath(full, src).replace(os.sep, '/'))


def synthetic(dst):
    """the rules the workshop mods do not reach"""
    g = os.path.join(GAME, 'data')
    files = {}
    meta = ('<metadata><name>Synthetic &amp; rules</name><directory>Synthetic.Rules</directory><id>1</id>'
            '<description>bare & here</description><version>2</version></metadata>')
    files['metadata.xml'] = meta.replace('&amp;', '&').encode()          # a bare & in the card
    items = open(os.path.join(g, 'items.xml'), 'rb').read().decode('utf-8')
    # an old version: ten entries fewer, a translated name, a bare & and a repeated attribute
    import re
    entries = list(re.finditer(r'<passive [^>]*/>', items))
    for m in entries[-10:]:
        items = items.replace(m.group(0), '', 1)
    items = items.replace('name="#THE_SAD_ONION_NAME"', 'name="Cebola & Triste" name="x"', 1)
    files['resources/items.xml'] = items.encode('utf-8')
    # a room of another version
    stb = os.path.join(g, 'rooms', '00.special rooms.stb')
    if os.path.exists(stb):
        files['resources/rooms/00.special rooms.stb'] = b'STB0' + open(stb, 'rb').read()[4:]
    # a palette picture with tRNS, twice the game's sketch.png (the port draws it at the game's layout)
    sk = Image.open(os.path.join(g, 'gfx', 'ui', 'main menu', 'sketch.png')).convert('RGBA')
    big = sk.resize((sk.width * 2, sk.height * 2), Image.NEAREST)
    arr = np.array(big)
    arr[::3, ::5, 3] = 90                                              # not pixel art any more
    pal = Image.fromarray(arr, 'RGBA').quantize(colors=200)
    import io
    b = io.BytesIO()
    pal.save(b, 'PNG', transparency=0)
    files['resources/gfx/ui/main menu/sketch.png'] = b.getvalue()
    # an interlaced grey+alpha picture, twice the game's sketches2.png
    s2 = Image.open(os.path.join(g, 'gfx', 'ui', 'main menu', 'sketches2.png')).convert('LA')
    s2 = s2.resize((s2.width * 2, s2.height * 2), Image.BILINEAR)
    b = io.BytesIO()
    s2.save(b, 'PNG', interlace=1)
    files['resources/gfx/ui/main menu/sketches2.png'] = b.getvalue()
    # an RGB picture over the GL limit with an odd side (halved: its last column dropped)
    wide = Image.fromarray((np.indices((40, 4099)).sum(axis=0) % 251).astype('uint8')).convert('RGB')
    b = io.BytesIO()
    wide.save(b, 'PNG')
    files['resources/gfx/synthetic_wide.png'] = b.getvalue()
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_STORED) as z:
        for n, d in files.items():
            z.writestr(n, d)


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace', **kw)
    return r.returncode, (r.stdout + r.stderr).strip()


def body(report_path):
    return open(report_path, encoding='utf-8').read() if os.path.exists(report_path) else None


shutil.rmtree(WORK, ignore_errors=True)
os.makedirs(WORK)
classes = os.path.join(WORK, 'classes')
os.makedirs(classes)
code, out = run([os.path.join(JDK, 'javac.exe'), '-Xlint:none', '-encoding', 'UTF-8', '-d', classes, '-cp', AJ,
                 os.path.join(HERE, 'java', 'org', 'json', 'JSONObject.java'), os.path.join(HERE, 'java', 'org', 'json', 'JSONArray.java'),
                 os.path.join(HERE, 'java', 'org', 'json', 'JSONException.java'),
                 os.path.join(JAVA_SRC, 'UserMods.java'), os.path.join(JAVA_SRC, 'ModPort.java'), os.path.join(JAVA_SRC, 'ModPortXml.java'),
                 os.path.join(JAVA_SRC, 'ModPortPng.java'), os.path.join(HERE, 'java', 'ru', 'isaac', 'port', 'ConvertDesk.java')])
check(code == 0, 'javac: the installer\'s converter builds' + ('' if code == 0 else ': ' + out[:400]))

mods = []
for d in sorted(os.listdir(WS)):
    if only and d not in only:
        continue
    z = os.path.join(WORK, d + '.zip')
    zip_folder(os.path.join(WS, d), z)
    mods.append((d, z))
if not only or 'synthetic' in only:
    z = os.path.join(WORK, 'synthetic.zip')
    synthetic(z)
    mods.append(('synthetic', z))

for label, z in mods:
    print('\n' + label)
    outs = {}
    for who in ('py', 'java', 'js'):
        o = os.path.join(WORK, '%s.%s.zip' % (label, who))
        if who == 'py':
            code, msg = run([sys.executable, os.path.join(ROOT, 'tools', 'modport.py'), z, '--game', GAME, '--out', o])
            msg = msg.splitlines()[-1] if msg else ''
        elif who == 'java':
            code, msg = run([os.path.join(JDK, 'java.exe'), '-Xmx96m', '-cp', classes + ';' + AJ, 'ru.isaac.port.ConvertDesk', REF, z, o])
        else:
            code, msg = run(['node', '--max-old-space-size=8192', os.path.join(HERE, 'modport_node.js'), REF, z, o])
        print('    %-4s %s' % (who, msg[-300:]))
        outs[who] = (o, code)
    rp = {w: body(o + '.report.txt') for w, (o, c) in outs.items()}
    check(all(c == 0 for o, c in outs.values()), 'all three ran')
    check(rp['py'] is not None and rp['py'] == rp['java'], 'the reports: Python == Java')
    check(rp['py'] is not None and rp['py'] == rp['js'], 'the reports: Python == the page')
    if rp['py'] != rp['java'] or rp['py'] != rp['js']:
        a, b, c = (rp[w].splitlines() if rp[w] else [] for w in ('py', 'java', 'js'))
        for l in [x for x in a if x not in b][:5]:
            print('        only Python: ' + l[:200])
        for l in [x for x in b if x not in a][:5]:
            print('        only Java:   ' + l[:200])
        for l in [x for x in c if x not in a][:5]:
            print('        only page:   ' + l[:200])
    have = {w: os.path.exists(o) for w, (o, c) in outs.items()}
    check(len(set(have.values())) == 1, 'a zip from all three or from none (%s)' % have)
    if all(have.values()):
        d1 = C.compare(outs['py'][0], outs['java'][0], by_tree=False, quiet=True)
        check(not d1, 'Python and Java: the same files, the same bytes (pictures by pixels)' + ('' if not d1 else ': ' + '; '.join(d1[:4])))
        d2 = C.compare(outs['java'][0], outs['js'][0], by_tree=True, quiet=True)
        check(not d2, 'the page and Java: the same files, trees and pixels' + ('' if not d2 else ': ' + '; '.join(d2[:4])))

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
