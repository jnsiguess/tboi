# -*- coding: utf-8 -*-
"""Where every EID picture's drawing really is, and how much of it is drawn.

The owner's tenth round (24.09.2026): the pictures stood taller than the line
and differed wildly - the Sad Onion small, Brimstone filling its frame - and
the arrow sat lower than the sword beside it. A frame says nothing about its
drawing: the drawing is somewhere inside it, of any size. So this measures,
once, on the desk:

  - every frame of every animation in the four EID sheets (the texels with
    alpha 128 and up, over all its visible layers), as the sprite would place
    it: the box relative to the point the sprite is drawn at, in units at a
    sprite scale of one, and the drawn area in square units;
  - every item's and trinket's own picture from the game's files, the same
    way, as the ItemIcon animation places it.

eid.c then puts every picture by its drawing: the big ones (items, trinkets,
cards, pills, transformations, the characters' heads) at one AREA in one box,
the small ones with their middles on the words' middle.

    py -3 tools/eid_boxes.py        # -> port/app/src/main/cpp/eid_boxes.h
"""
import os
import re
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
CPP = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp')
EDITIONS = ['repentance-resources', 'afterbirthplus-resources', 'afterbirth-resources', 'rebirth-resources']
SHEETS = ['eid_inline_icons', 'eid_transform_icons', 'eid_cardspills', 'eid_player_icons']
SOLID = 128


def attrs(tag):
    return dict(re.findall(r'(\w+)="([^"]*)"', tag))


def parse(path):
    """animations: name -> list of frames; a frame is a list of layer pieces"""
    s = open(path, encoding='utf-8').read()
    sheets = {a['Id']: a['Path'] for a in map(attrs, re.findall(r'<Spritesheet [^>]*>', s))}
    layers = {a['Id']: a['SpritesheetId'] for a in map(attrs, re.findall(r'<Layer [^>]*>', s))}
    out = {}
    for m in re.finditer(r'<Animation Name="([^"]*)"[^>]*>(.*?)</Animation>', s, re.S):
        name, body = m.group(1), m.group(2)
        root = re.search(r'<RootAnimation>\s*<Frame ([^>]*)>', body)
        ra = attrs(root.group(1)) if root else {}
        rx, ry = float(ra.get('XPosition', 0)), float(ra.get('YPosition', 0))
        rs = float(ra.get('XScale', 100)) / 100.0
        per = []
        for la in re.finditer(r'<LayerAnimation LayerId="(\d+)"[^>]*Visible="(\w+)"[^>]*>(.*?)</LayerAnimation>',
                              body, re.S):
            if la.group(2) != 'true':
                continue
            sheet = sheets[layers[la.group(1)]]
            per.append((la.group(1), sheet, [attrs(f) for f in re.findall(r'<Frame ([^>]*)>', la.group(3))]))
        count = max((len(f) for _, _, f in per), default=0)
        frames = []
        for k in range(count):
            pieces = []
            for lid, sheet, fs in per:
                if k >= len(fs):
                    continue
                f = fs[k]
                if f.get('Visible', 'true') != 'true':
                    continue
                pieces.append(dict(layer=lid, sheet=sheet, x=int(float(f['XCrop'])), y=int(float(f['YCrop'])),
                                   w=int(float(f['Width'])), h=int(float(f['Height'])),
                                   px=float(f['XPivot']), py=float(f['YPivot']),
                                   ox=float(f['XPosition']) * rs + rx, oy=float(f['YPosition']) * rs + ry,
                                   sc=float(f['XScale']) / 100.0 * rs))
            frames.append(pieces)
        out[name] = frames
    return out


_images = {}


def image(folder, name):
    key = os.path.join(folder, name)
    if key not in _images:
        _images[key] = Image.open(key).convert('RGBA') if os.path.exists(key) else None
    return _images[key]


def measure(pieces, folder, replace=None):
    """the box (x0, y0, x1, y1) in units at sprite scale one, and the drawn area"""
    x0 = y0 = 1e9
    x1 = y1 = -1e9
    area = 0.0
    for p in pieces:
        im = replace if (replace is not None and p['layer'] == '1') else image(folder, p['sheet'])
        if im is None:
            continue
        if replace is not None and p['layer'] == '1':
            crop = im
        else:
            crop = im.crop((p['x'], p['y'], p['x'] + p['w'], p['y'] + p['h']))
        a = crop.getchannel('A').point(lambda v: 255 if v >= SOLID else 0)
        bb = a.getbbox()
        if not bb:
            continue
        n = sum(1 for v in a.getdata() if v)
        sc = p['sc']
        x0 = min(x0, p['ox'] + (bb[0] - p['px']) * sc)
        y0 = min(y0, p['oy'] + (bb[1] - p['py']) * sc)
        x1 = max(x1, p['ox'] + (bb[2] - p['px']) * sc)
        y1 = max(y1, p['oy'] + (bb[3] - p['py']) * sc)
        area += n * sc * sc
    if x1 < x0:
        return None
    return x0, y0, x1 - x0, y1 - y0, area


def item_file(kind, name):
    for ed in EDITIONS:
        folder = os.path.join(ROOT, 'work', 'app', ed, 'data', 'gfx', 'items', kind)
        if not os.path.isdir(folder):
            continue
        for f in os.listdir(folder):
            if f.lower() == name.lower():
                return os.path.join(folder, f)
    return None


def items_of(header):
    """the EID_COLLECTIBLES and EID_TRINKETS rows: (id, file)"""
    s = open(header, encoding='utf-8').read()
    out = {}
    for table in ('EID_COLLECTIBLES', 'EID_TRINKETS'):
        body = s[s.index('static const eid_item_t %s[]' % table):]
        body = body[:body.index('};')]
        out[table] = [(int(i), g) for i, g in re.findall(r'\{\s*(\d+),\s*"([^"]*)"', body)]
    return out


def main():
    frames_out = []
    item_icon = None
    for sheet_no, sheet in enumerate(SHEETS):
        anims = parse(os.path.join(ASSETS, sheet + '.anm2'))
        for name, frames in sorted(anims.items()):
            if name == 'ItemIcon':
                item_icon = frames[0]
                continue
            for k, pieces in enumerate(frames):
                m = measure(pieces, ASSETS)
                if m:
                    frames_out.append((sheet_no, name, k) + m)
    # sheet 4: the game's own pad buttons (eid.c draws {{Button...}} from them for a
    # player on a pad), animation XboxOne - measured where the game keeps them
    ui = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'ui')
    pads = parse(os.path.join(ui, 'buttons.anm2'))
    for k, pieces in enumerate(pads.get('XboxOne', [])):
        m = measure(pieces, ui)
        if m:
            frames_out.append((4, 'XboxOne', k) + m)
    if item_icon is None:
        sys.exit('no ItemIcon animation')
    items = items_of(os.path.join(CPP, 'eid_icons.h'))
    item_rows, missing = [], []
    for table, kind, trinket in (('EID_COLLECTIBLES', 'collectibles', 0), ('EID_TRINKETS', 'trinkets', 1)):
        for iid, name in items[table]:
            path = item_file(kind, name)
            if not path:
                missing.append(name)
                continue
            # only the replaceable layer: the item's file goes there (eid.c, the sprite's
            # layer 1); layer 0 is the sheet's own corner and is not the item
            m = measure([p for p in item_icon if p['layer'] == '1'], ASSETS,
                        replace=Image.open(path).convert('RGBA'))
            if m:
                item_rows.append((iid, trinket) + m)
    arrow = [f for f in frames_out if f[0] == 0 and f[1] == 'ArrowUp' and f[2] == 0][0]

    L = ['/*',
         ' * Generated by tools/eid_boxes.py - do not edit.',
         ' *',
         ' * Where each EID picture\'s drawing is: its box relative to the point the',
         ' * sprite is drawn at, in units at a sprite scale of one, and the area it',
         ' * covers (texels with alpha 128 and up) in square units.',
         ' */',
         'typedef struct { unsigned char sheet; const char *anim; unsigned short frame;',
         '                 float x, y, w, h, area; } eid_frame_box_t;',
         'typedef struct { unsigned short id; unsigned char trinket; float x, y, w, h, area; } eid_item_box_t;',
         '',
         '/* the up arrow\'s drawing, the one the owner reads the line by: its middle is the words\' */',
         '#define EID_ARROW_MID_Y %.3ff' % (arrow[4] + arrow[6] / 2.0),
         '',
         'static const eid_frame_box_t EID_FRAME_BOXES[] = {']
    for r in frames_out:
        L.append('    { %d, "%s", %d, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff },' % r)
    L += ['};', '', 'static const eid_item_box_t EID_ITEM_BOXES[] = {']
    for r in sorted(item_rows, key=lambda r: (r[1], r[0])):
        L.append('    { %d, %d, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff },' % r)
    L += ['};', '']
    out = os.path.join(CPP, 'eid_boxes.h')
    open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(L))
    print(out, len(frames_out), 'frames,', len(item_rows), 'items;', len(missing), 'item files not found',
          missing[:5])
    print('arrow middle y %.3f' % (arrow[4] + arrow[6] / 2.0))


if __name__ == '__main__':
    main()
