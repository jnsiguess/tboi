# -*- coding: utf-8 -*-
"""The parity corpus of the converter (04.10.2026): what goes in, how an output is described.

Shared by make_expected.py (writes expected/<label>.json from tools/modport.py and the web page's
modport.js) and compare.py (holds a third converter's output - the installer's ModPort.java - to them).

The corpus: the 25 workshop mods of the owner's Steam folder (label = the workshop number), the
Reflashed the owner installed (other stuff/usermods/ported/reflashed.zip - already converted once: the
second pass must leave it be) and the example mod (other stuff/usermods/example-mod.zip - our format).
Every converter is fed the SAME zip: a workshop folder is zipped once, deterministically (names sorted,
stored, a fixed date), into <work>/inputs/<label>.zip; a zip of the corpus is used as it is.

An output is described file by file, so a converter with its own PNG encoder or XML serializer can
still be held to the reference:
    size, sha256      the bytes;
    pixels            .png: sha256 of b"<w>x<h>" + the RGBA pixels (PIL convert('RGBA'));
    tree              .xml/.anm2: sha256 of the element tree (tag, attributes in their order, text and
                      tail stripped) - the page's serializer writes other bytes for the same tree;
    json              mod.json: the value itself.
"""
import hashlib
import io
import json
import os
import subprocess
import sys
import zipfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
EXPECTED = os.path.join(HERE, 'expected')
WS = r'Z:/Games/Steam/steamapps/workshop/content/250900'
USERMODS = os.path.join(ROOT, 'other stuff', 'usermods')
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
REF = os.path.join(USERMODS, 'modport-web', 'src', 'gameref.bin')
MODPORT_PY = os.path.join(ROOT, 'tools', 'modport.py')
MODPORT_JS = os.path.join(USERMODS, 'modport-web', 'src', 'modport.js')
NODE_SHIM = os.path.join(ROOT, 'tools', 'umdesk', 'modport_node.js')
WORK = os.path.join(os.environ.get('TEMP', '.'), 'convdesk')
FIXED_DATE = (2026, 10, 4, 0, 0, 0)


def corpus():
    """[(label, source path, kind)] - kind 'folder' (zipped on the desk), 'pcfolder' (the same without the
    mod.json a player wrote into it: the PC layout ADD MOD adapts) or 'zip' (used as it is)"""
    out = []
    if os.path.isdir(WS):
        for d in sorted(os.listdir(WS)):
            if os.path.isdir(os.path.join(WS, d)):
                out.append((d, os.path.join(WS, d), 'folder'))
    out.append(('reflashed-ported', os.path.join(USERMODS, 'ported', 'reflashed.zip'), 'zip'))
    out.append(('example-mod', os.path.join(USERMODS, 'example-mod.zip'), 'zip'))
    # the rules no workshop mod reaches (an older text table to merge, a bare & and a repeated attribute,
    # a room of another version, a palette and an interlaced grey picture to bake, one over 4096): the
    # synthetic mod of tools/umdesk/test_converters.py, made by its own function
    out.append(('synthetic', SYNTHETIC_SRC, 'synthetic'))
    # hotfix 1.71 (05.10.2026): a mod's own completion_widget.anm2 with its 393x500 sheets (the game's 256x512) and
    # the pause twin - kept as the mod's layout, not refused "has no file the port reads"
    if os.path.isdir(COOLER):
        out.append(('cooler-3361519802', COOLER, 'pcfolder'))
    return out


COOLER = os.path.join(ROOT, 'reports', '2026-10-05', 'hotfix171', 'cooler_completion_marks_rep+_3361519802',
                      'cooler_completion_marks_rep+_3361519802')


SYNTHETIC_SRC = os.path.join(ROOT, 'tools', 'umdesk', 'test_converters.py')


def make_synthetic(dst):
    """runs synthetic() of test_converters.py alone (the script itself would run the whole desk)"""
    import ast
    src = open(SYNTHETIC_SRC, encoding='utf-8').read()
    fn = next(n for n in ast.parse(src).body if isinstance(n, ast.FunctionDef) and n.name == 'synthetic')
    import numpy as np
    from PIL import Image
    g = {'os': os, 'zipfile': zipfile, 'np': np, 'Image': Image, 'GAME': GAME}
    exec(compile(ast.Module([fn], []), SYNTHETIC_SRC, 'exec'), g)
    g['synthetic'](dst)


def title_of(src, kind):
    """the mod's name from its card (metadata.xml / mod.json), for the reader only"""
    try:
        if kind == 'synthetic':
            return 'Synthetic rules (tools/umdesk/test_converters.py)'
        if kind in ('folder', 'pcfolder'):
            p = os.path.join(src, 'metadata.xml')
            if os.path.exists(p):
                raw = open(p, 'rb').read().replace(b'& ', b'&amp; ')
                e = ET.fromstring(raw).find('name')
                return (e.text or '').strip() if e is not None else ''
            return ''
        z = zipfile.ZipFile(src)
        names = z.namelist()
        j = [n for n in names if n.rsplit('/', 1)[-1] == 'mod.json']
        if j:
            name = json.loads(z.read(sorted(j, key=len)[0]).decode('utf-8')).get('name', '')
            if isinstance(name, dict):              # a name per language
                name = name.get('en') or next(iter(name.values()), '')
            return str(name)
    except Exception:
        pass
    return ''


def zip_folder(src, dst, skip=()):
    """a folder into a zip every converter reads the same: names sorted, stored, one date (skip: names left out)"""
    names = []
    for dp, dn, fn in os.walk(src):
        for f in fn:
            full = os.path.join(dp, f)
            rel = os.path.relpath(full, src).replace(os.sep, '/')
            if rel not in skip:
                names.append((rel, full))
    names.sort()
    tmp = dst + '.part'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED) as z:
        for rel, full in names:
            info = zipfile.ZipInfo(rel, FIXED_DATE)
            info.compress_type = zipfile.ZIP_STORED
            info.external_attr = 0o644 << 16
            with open(full, 'rb') as f:
                z.writestr(info, f.read())
    os.replace(tmp, dst)


def input_zip(label, src, kind, work=WORK):
    """the zip every converter of the corpus is fed for this mod"""
    if kind == 'zip':
        return src
    d = os.path.join(work, 'inputs')
    os.makedirs(d, exist_ok=True)
    dst = os.path.join(d, label + '.zip')
    if not os.path.exists(dst):
        if kind == 'synthetic':
            make_synthetic(dst + '.part')
            os.replace(dst + '.part', dst)
        else:
            zip_folder(src, dst, ('mod.json',) if kind == 'pcfolder' else ())
    return dst


def digest_zip(path):
    """sha256 of the zip's CONTENT (sorted names and each file's sha256), not of its container"""
    h = hashlib.sha256()
    with zipfile.ZipFile(path) as z:
        for n in sorted(z.namelist()):
            if n.endswith('/'):
                continue
            h.update(n.encode('utf-8') + b'\0' + hashlib.sha256(z.read(n)).hexdigest().encode() + b'\n')
    return h.hexdigest()


def sha_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def tools_used():
    out = {}
    for name, p in (('modport.py', MODPORT_PY), ('modport.js', MODPORT_JS), ('gameref.bin', REF),
                    ('modport_node.js', NODE_SHIM)):
        out[name] = sha_file(p) if os.path.exists(p) else None
    return out


# ------------------------------------------------------------------ describing an output

def tree_list(b):
    r = ET.fromstring(b)
    out = []

    def walk(e):
        out.append([e.tag, [list(kv) for kv in e.attrib.items()], (e.text or '').strip(), (e.tail or '').strip()])
        for c in e:
            walk(c)
        out.append('/' + e.tag)
    walk(r)
    return out


def tree_sha(b):
    try:
        return hashlib.sha256(json.dumps(tree_list(b), ensure_ascii=False).encode('utf-8')).hexdigest()
    except ET.ParseError as e:
        return 'unparsed: %s' % e


def rgba(b):
    from PIL import Image
    im = Image.open(io.BytesIO(b))
    im.load()
    return im.size, im.convert('RGBA').tobytes()


def pixels_sha(b):
    try:
        (w, h), px = rgba(b)
    except Exception as e:
        return 'unreadable: %s' % e
    return hashlib.sha256(b'%dx%d' % (w, h) + px).hexdigest()


def png_wh(b):
    import struct
    if len(b) >= 24 and b[12:16] == b'IHDR':
        return list(struct.unpack('>II', b[16:24]))
    return None


def describe(name, data):
    e = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    low = name.lower()
    if low == 'mod.json':
        try:
            e['json'] = json.loads(data.decode('utf-8'))
        except Exception as x:
            e['json'] = 'unparsed: %s' % x
    elif low.endswith('.png'):
        e['wh'] = png_wh(data)
        e['pixels'] = pixels_sha(data)
    elif low.endswith(('.xml', '.anm2')):
        e['tree'] = tree_sha(data)
    return e


class Output:
    """a converter's output: a zip, or a folder (the zip unpacked, as the installer leaves it)"""
    def __init__(self, path):
        self.path = path
        self.zip = None
        if os.path.isfile(path):
            self.zip = zipfile.ZipFile(path)
            self.names = sorted(n for n in self.zip.namelist() if not n.endswith('/'))
        elif os.path.isdir(path):
            self.names = []
            for dp, dn, fn in os.walk(path):
                for f in fn:
                    self.names.append(os.path.relpath(os.path.join(dp, f), path).replace(os.sep, '/'))
            self.names.sort()
        else:
            self.names = None              # no output at all (a converter that wrote no zip)

    def read(self, n):
        if self.zip:
            return self.zip.read(n)
        with open(os.path.join(self.path, n), 'rb') as f:
            return f.read()

    def files(self):
        return {n: describe(n, self.read(n)) for n in (self.names or [])}


def report_lines(path):
    if not path or not os.path.exists(path):
        return None
    with open(path, encoding='utf-8') as f:
        return f.read().split('\n')


def load_expected(label_or_path):
    p = label_or_path
    if not p.lower().endswith('.json'):
        p = os.path.join(EXPECTED, label_or_path + '.json')
    with open(p, encoding='utf-8') as f:
        return json.load(f)


# ------------------------------------------------------------------ the two reference converters

def run(cmd, timeout=3600):
    env = dict(os.environ, PYTHONIOENCODING='utf-8')
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace', env=env,
                       timeout=timeout)
    return r.returncode, (r.stdout + r.stderr).strip()


def run_py(inp, out):
    return run([sys.executable, MODPORT_PY, inp, '--game', GAME, '--out', out])


def run_js(inp, out):
    return run(['node', '--max-old-space-size=8192', NODE_SHIM, REF, inp, out])
