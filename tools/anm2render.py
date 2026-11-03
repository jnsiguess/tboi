# -*- coding: utf-8 -*-
"""Render one frame of an Isaac .anm2 animation into an RGBA image (tools/spawner_enemies.py).

Layers are drawn in the order the animation lists them; each frame's crop is
placed by pivot, scale, rotation and position, then the root frame, then an
optional outer transform (the bestiary's x, y, scale). Tint multiplies, the
colour offset adds. Sampling is nearest: the game draws its sprites 1:1.
"""
import math, os, re
import xml.etree.ElementTree as ET
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOTS = [os.path.join(ROOT, 'work', 'app', '%s-resources' % e, 'data') for e in ('repentance', 'afterbirthplus', 'afterbirth', 'rebirth')]
_index = None

def _build_index():
    global _index
    _index = {}
    for root in reversed(ROOTS):                    # later (repentance) wins
        for dp, dn, fn in os.walk(os.path.join(root, 'gfx')):
            for f in fn:
                full = os.path.join(dp, f)
                rel = os.path.relpath(full, root).replace(os.sep, '/').lower()
                _index[rel] = full

def find(rel):
    if _index is None: _build_index()
    rel = rel.replace(os.sep, '/').lower()
    rel = re.sub(r'/+', '/', rel)
    return _index.get(rel)

_sheets = {}
def sheet(path):
    if path not in _sheets:
        _sheets[path] = Image.open(path).convert('RGBA')
    return _sheets[path]

def _f(e, k, d=0.0):
    v = e.get(k)
    try: return float(v) if v is not None else d
    except ValueError: return d

def _frame_at(frames, t):
    at = 0
    for fr in frames:
        d = int(_f(fr, 'Delay', 1))
        if t < at + d: return fr
        at += d
    return frames[-1] if frames else None

class Anm2:
    def __init__(self, rel):
        self.path = find('gfx/' + rel)
        if not self.path: raise FileNotFoundError(rel)
        t = ET.parse(self.path).getroot()
        base = os.path.dirname(os.path.relpath(self.path, [r for r in ROOTS if self.path.startswith(r)][0])).replace(os.sep, '/')
        self.sheets = {}
        for s in t.iter('Spritesheet'):
            p = find(base + '/' + s.get('Path'))
            self.sheets[s.get('Id')] = p
        self.layers = {l.get('Id'): l.get('SpritesheetId') for l in t.iter('Layer')}
        anims = t.find('Animations')
        self.default = anims.get('DefaultAnimation')
        self.anims = {a.get('Name'): a for a in anims.findall('Animation')}

    def missing(self):
        return [k for k, v in self.sheets.items() if not v]

    def draw(self, canvas, anim, t=0, ox=0.0, oy=0.0, scale=1.0, origin=(512, 512)):
        a = self.anims.get(anim)
        if a is None: return False
        rootf = _frame_at(a.find('RootAnimation').findall('Frame'), t)
        rx, ry = (_f(rootf, 'XPosition'), _f(rootf, 'YPosition')) if rootf is not None else (0, 0)
        rsx = _f(rootf, 'XScale', 100) / 100 if rootf is not None else 1
        rsy = _f(rootf, 'YScale', 100) / 100 if rootf is not None else 1
        rrot = _f(rootf, 'Rotation') if rootf is not None else 0
        drawn = False
        for la in a.find('LayerAnimations').findall('LayerAnimation'):
            if la.get('Visible') == 'false': continue
            fr = _frame_at(la.findall('Frame'), t)
            if fr is None or fr.get('Visible') == 'false': continue
            sid = self.layers.get(la.get('LayerId'))
            sp = self.sheets.get(sid)
            if not sp: continue
            src = sheet(sp)
            cx, cy, w, h = (_f(fr, k) for k in ('XCrop', 'YCrop', 'Width', 'Height'))
            if w <= 0 or h <= 0: continue
            px, py = _f(fr, 'XPivot'), _f(fr, 'YPivot')
            sx, sy = _f(fr, 'XScale', 100) / 100, _f(fr, 'YScale', 100) / 100
            rot = math.radians(_f(fr, 'Rotation'))
            x, y = _f(fr, 'XPosition'), _f(fr, 'YPosition')
            tint = [_f(fr, k, 255) / 255 for k in ('RedTint', 'GreenTint', 'BlueTint', 'AlphaTint')]
            off = [_f(fr, k) / 255 for k in ('RedOffset', 'GreenOffset', 'BlueOffset')]
            piece = src.crop((int(cx), int(cy), int(cx + w), int(cy + h)))
            if tint != [1, 1, 1, 1] or off != [0, 0, 0]:
                r, g, b, al = piece.split()
                r = r.point(lambda v, m=tint[0], o=off[0]: min(255, int(v * m + o * 255)))
                g = g.point(lambda v, m=tint[1], o=off[1]: min(255, int(v * m + o * 255)))
                b = b.point(lambda v, m=tint[2], o=off[2]: min(255, int(v * m + o * 255)))
                al = al.point(lambda v, m=tint[3]: int(v * m))
                piece = Image.merge('RGBA', (r, g, b, al))
            # forward map: q = O + S*(Rr*(Sr*(Rl*(Sl*(p - pivot)) + pos) + rootpos) + bestiary offset)
            c, s_ = math.cos(rot), math.sin(rot)
            L = [[c * sx, -s_ * sy], [s_ * sx, c * sy]]
            cr, sr = math.cos(math.radians(rrot)), math.sin(math.radians(rrot))
            Rm = [[cr * rsx, -sr * rsy], [sr * rsx, cr * rsy]]
            M = [[scale * (Rm[0][0] * L[0][0] + Rm[0][1] * L[1][0]), scale * (Rm[0][0] * L[0][1] + Rm[0][1] * L[1][1])],
                 [scale * (Rm[1][0] * L[0][0] + Rm[1][1] * L[1][0]), scale * (Rm[1][0] * L[0][1] + Rm[1][1] * L[1][1])]]
            # translation: p=pivot maps to
            tx0 = Rm[0][0] * x + Rm[0][1] * y + rx
            ty0 = Rm[1][0] * x + Rm[1][1] * y + ry
            T = [origin[0] + scale * (tx0 + ox), origin[1] + scale * (ty0 + oy)]
            det = M[0][0] * M[1][1] - M[0][1] * M[1][0]
            if abs(det) < 1e-9: continue
            inv = [[M[1][1] / det, -M[0][1] / det], [-M[1][0] / det, M[0][0] / det]]
            # p = inv*(q - T) + pivot
            a_, b_, c_ = inv[0][0], inv[0][1], px - inv[0][0] * T[0] - inv[0][1] * T[1]
            d_, e_, f_ = inv[1][0], inv[1][1], py - inv[1][0] * T[0] - inv[1][1] * T[1]
            layer = piece.transform(canvas.size, Image.AFFINE, (a_, b_, c_, d_, e_, f_), resample=Image.NEAREST)
            canvas.alpha_composite(layer)
            drawn = True
        return drawn

def render(rel, anim=None, overlay=None, ox=0, oy=0, scale=1.0, t=0, size=1024):
    a = Anm2(rel)
    canvas = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    anim = anim if anim in a.anims else a.default
    ok = a.draw(canvas, anim, t, ox, oy, scale, (size // 2, size // 2))
    if overlay and overlay in a.anims:
        a.draw(canvas, overlay, t, ox, oy, scale, (size // 2, size // 2))
    box = canvas.getbbox()
    return (canvas.crop(box) if box else None), anim, a.missing()
