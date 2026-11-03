# -*- coding: utf-8 -*-
"""The players' HUD: the port's calls through the image's renderers against the PC game's own draw.

For every case (canvas, HudOffset, Greed, line-up) three things run:
  1. desk.exe (the real hudgeom.c) prints the calls hudlayout.c makes on the phone;
  2. hudemu.py runs them through the iOS image's renderers - plus, for an entry the
     port leaves to the game, the calls the image's own loop makes (0x6ee64c) -
     and records every sprite, picture and text with its screen place and scale;
  3. pcemu.py runs the PC game's (Rep+ 1.9.7) layout choice 0x9a6570 and per-player
     draw 0x9a6fe0 on the same line-up, its corner table set to y 24 (the owner's
     top row), and records the same.
Then every PC piece must have an iOS piece of the same kind and art at the same
place and scale, and the other way round.

    py -3 tools/hudesk/hudcheck.py            all cases, a summary line each and the misses
    py -3 tools/hudesk/hudcheck.py -v NAME    one case, every piece
    py -3 tools/hudesk/hudcheck.py --draw DIR [NAME]   every case's pieces as boxes, PC red, the port green
"""
import os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hudemu as I  # noqa: E402
import pcemu as P  # noqa: E402
from unicorn.arm64_const import UC_ARM64_REG_SP  # noqa: E402

DESK = os.path.join(HERE, 'desk.exe')
NAME = '0 - The Fool'
HEARTS = 8
TEXT_W = 60

JACOB, ESAU, T_ISAAC, T_CAIN, T_BLUEBABY = 19, 20, 21, 23, 25

# hudlayout.c PA_WORDS
PA_WORDS = [(0x6e920c, (0x1E349001, 0x1E341001, 0x1E341001, 0x1E349001)),
            (0x6e9224, (0x1E349001, 0x1E341001, 0x1E341001, 0x1E349001)),
            (0x6e9260, (0x1E321001, 0x1E231001, 0x1E231001, 0x1E321001)),
            (0x6e9280, (0x1E371000, 0x1E345000, 0x1E345000, 0x1E371000)),
            (0x6e9284, (0x1E365001, 0x1E341001, 0x1E341001, 0x1E365001)),
            (0x6e92cc, (0x54001281, 0x54001281, 0x1400011B, 0x54001281)),
            (0x6e9330, (0x1400007B, 0x1400007B, 0x1400007B, 0x14000102))]


def fbits(v):
    return struct.unpack('<I', struct.pack('<f', v))[0]


# ---------------------------------------------------------------- the line-up
class Case:
    def __init__(s, name, types, trinkets=None, W=585.0, H=270.0, hud=1.0, greed=0, pockets=1, kinds=None, bag=False):
        s.name, s.W, s.H, s.hud, s.greed, s.pockets = name, W, H, hud, greed, pockets
        s.kinds = kinds or [0, 0, 0, 0]          # a pocket's kind: 0 card, 1 pill, 2 pocket active (id = active slot + 1)
        s.bag = bag                              # the pocket active in slot 2 is T.Cain's Bag of Crafting
        s.types = list(types) + [-1] * (8 - len(types))
        # a pair: Jacob in k, Esau in k + 4
        for k in range(4):
            if s.types[k] == JACOB and s.types[k + 4] < 0:
                s.types[k + 4] = ESAU
        s.trinkets = trinkets or [3 if t >= 0 else 0 for t in s.types]

    def present(self):
        return [k for k in range(8) if self.types[k] >= 0]


# ---------------------------------------------------------------- the phone's calls
def desk_ops(c):
    out = subprocess.run([DESK, str(c.W), str(c.H), str(c.hud), str(c.greed), ','.join(map(str, c.types)),
                          ''.join(str(b) for b in c.trinkets)], capture_output=True, text=True, check=True).stdout
    ops, drawn, layouts, corners = [], set(), {}, {}
    for line in out.splitlines():
        w = line.split()
        if w[0] == 'layout':
            k = int(w[2])
            layouts[k] = int(w[3])
            if w[5] == '1':
                drawn.add(k)
            corners[k] = tuple(map(float, w[7].split(',')))
        elif w[0] == 'op':
            x, y = map(float, w[7].split(','))
            ops.append(dict(op=w[1], entry=int(w[3]), slot=int(w[5]), x=x, y=y, s=float(w[9]), index=int(w[11]),
                            right=w[13] == 'right', more=int(w[15])))
    return ops, drawn, layouts, corners


def game_entry0_ops(c):
    """What the image's own loop draws for entry 0 (0x6ee64c..0x6eec84), as calls"""
    f = 1.0 - c.hud
    cx, cy = 68 - 20 * f, 24 - 12 * f
    bl = (-22 * f, 16 * f)
    t = c.types[0]
    ops = [dict(op='hearts', entry=0, slot=0, x=cx, y=cy, s=1.0, index=0, right=False),
           dict(op='active', entry=0, slot=1, x=cx - 53, y=cy - 12, s=1.0, index=0, right=False),
           dict(op='active', entry=0, slot=0, x=cx - 44, y=cy - 12, s=1.0, index=0, right=False),
           dict(op='trinket', entry=0, slot=1, x=20 + bl[0], y=c.H - 52 - 16 + bl[1], s=1.0, index=0, right=False),
           dict(op='trinket', entry=0, slot=0, x=44 + bl[0], y=c.H - 28 - 16 + bl[1], s=1.0, index=0, right=False)]
    if t == T_ISAAC:
        ops.append(dict(op='grid', entry=0, slot=0, x=58 - 20 * f, y=45 - 12 * f, s=1.0, index=0, right=False))
    if t == T_BLUEBABY:
        ops.append(dict(op='spells', entry=0, slot=0, x=58 - 20 * f, y=45 - 12 * f, s=1.0, index=0, right=False))
    ops.append(dict(op='pocket', entry=0, slot=0, x=c.W - 28 + 16 * f, y=c.H - 18 + 6 * f, s=1.0, index=0,
                    right=False))
    return ops


# ---------------------------------------------------------------- iOS
def ios_run(c, ops):
    m = I.Machine(c.W, c.H)
    m.text_width = TEXT_W
    m.item(105)
    m.item(0x2c6)
    hud = I.GAME + I.HUD_OFF
    red = m.string('RedHeartFull')
    players = {}
    for k in c.present():
        e = hud + I.ENTRY * k
        p = I.PLAYERS + k * I.PLAYER_SIZE
        players[k] = p
        m.w64(e, p)
        m.w64(e + 8, hud)
        m.w16(e + 0x10, k)
        m.w32(p + 0x1738, c.types[k])
        m.w32(p + 0x19f0, k)
        for i in range(HEARTS):
            m.w8(e + 0x18 + i * 0x18, 1)
            m.w64(e + 0x18 + i * 0x18 + 8, red)
        for sl in (0, 1):
            m.w32(p + 0x1964 + sl * 0x1c, 105)
            m.w32(p + 0x1968 + sl * 0x1c, 3)
            m.w64(e + 0x258 + sl * 0x30 + 8, m.texture('active%d' % sl))
            if c.trinkets[k] >> sl & 1:
                m.w32(e + 0x318 + sl * 0x18, 5 + sl)
                m.w64(e + 0x320 + sl * 0x18, m.texture('trinket%d' % sl))
        for i in range(c.pockets):
            m.w32(p + 0x1c10 + 8 * i, 3 if c.kinds[i] == 2 else 1 + i)
            m.w32(p + 0x1c14 + 8 * i, c.kinds[i])
        for sl in (2, 3):                       # the pocket actives' slots
            m.w32(p + 0x1964 + sl * 0x1c, 0x2c6 if (c.bag and sl == 2) else 105)
            m.w32(p + 0x1968 + sl * 0x1c, 3)
            m.w64(e + 0x258 + sl * 0x30 + 8, m.texture('active%d' % sl))
        m.w8(e + 0x499, 1)
    for i in range(4):
        m.w8(I.GAME + 0x24d28 + 1 + i, 1)
    for k in range(4):                          # the pairs
        if k + 4 in players and c.types[k] == JACOB:
            m.w64(players[k] + 0x2580, players[k + 4])
            m.w64(players[k + 4] + 0x2580, players[k])
    m.mu.hook_add(I.UC_HOOK_CODE, lambda mu, a, sz, u: I._std_string_set(m, mu.reg_read(UC_ARM64_REG_SP) + 0x90, NAME),
                  begin=I.IMG + 0x6e951c, end=I.IMG + 0x6e951c)
    out = []
    pos = m.alloc(16)
    for o in ops:
        e = hud + I.ENTRY * o['entry']
        saved = struct.unpack('<h', m.mu.mem_read(e + 0x10, 2))[0]
        m.w16(e + 0x10, o['index'])
        m.wf(pos, o['x'])
        m.wf(pos + 4, o['y'])
        m.log.clear()
        s = o['s']
        if o['op'] == 'hearts':
            m.call(I.FN['hearts'], (e, hud + I.HEARTS_OBJ, pos), (s,))
        elif o['op'] == 'active':
            m.call(I.FN['active'], (e, o['slot'], pos, 0), (s, 1.0))
        elif o['op'] == 'trinket':
            m.call(I.FN['trinket'], (e, o['slot'], pos), (s,))
        elif o['op'] == 'pocket':
            # 0x6e8c24..0x6e8c60: flipped (the name right) = (scale < 0) XOR (index >= 1 and the first of his pair)
            p = players[o['entry']]
            twin = struct.unpack('<Q', m.mu.mem_read(p + 0x2580, 8))[0]
            main = (not twin) or not (struct.unpack('<i', m.mu.mem_read(twin + 0x19f0, 4))[0]
                                       < struct.unpack('<i', m.mu.mem_read(p + 0x19f0, 4))[0])
            flip_if_positive = o['index'] >= 1 and main
            sign = 1.0 if flip_if_positive == o['right'] else -1.0
            more = o.get('more', 0)
            hidden = []
            if more:                            # hudlayout.c: the pockets behind hidden for the call
                for i in (1, 2, 3):
                    hidden.append(m.r32(p + 0x1c10 + 8 * i))
                    m.w32(p + 0x1c10 + 8 * i, 0)
            pid0, kind0 = m.r32(p + 0x1c10), m.r32(p + 0x1c14)
            front = kind0 == 2 and 1 <= pid0 <= 4
            bag = front and m.r32(p + 0x1964 + (pid0 - 1) * 0x1c) == 0x2c6
            pa = (2 if bag else (1 if front else 0)) if more else (3 if bag else 0)
            if pa:                              # hudlayout.c pa_words(): Rep+'s pocket-active places for the call
                for at, v in PA_WORDS:
                    m.code_word(at, v[pa])
            m.call(I.FN['pocket'], (e, pos), (s * sign, 1.0))
            if pa:
                for at, v in PA_WORDS:
                    m.code_word(at, v[0])
                if pa == 2:
                    m.wf(pos, o['x'] + 69.0 * s); m.wf(pos + 4, o['y'] + 27.0 * s)
                    m.call(0x6e9934, (e, pos), (s,))
            if more:                            # hudlayout.c pockets_behind()
                for i in (1, 2, 3):
                    m.w32(p + 0x1c10 + 8 * i, hidden[i - 1])
                    pid, kind = hidden[i - 1], m.r32(p + 0x1c14 + 8 * i)
                    if not pid:
                        continue
                    px, py = o['x'] + 8.0 * i * s - 2.0, o['y'] + 12.0 * s
                    if kind < 2:
                        spr = e + 0x350 + i * 0x160
                        name = m.string('HUDSmall')
                        m.call(I.SET_FRAME, (spr, name, 0))
                        m.wf(pos, px); m.wf(pos + 4, py)
                        m.wf(spr + 0xdc, s); m.wf(spr + 0xe0, s)
                        m.call(I.RENDER, (spr, pos, I.IMG + 0x95e7a0, I.IMG + 0x95e7a0))
                        m.wf(spr + 0xdc, 1.0); m.wf(spr + 0xe0, 1.0)
                    elif kind == 2:
                        m.wf(pos, px - 4.0 * s); m.wf(pos + 4, py - 8.0 * s)
                        m.call(I.FN['active'], (e, pid - 1, pos, 0), (0.5 * s, 1.0))
        elif o['op'] in ('grid', 'spells'):
            m.call(I.FN[o['op']], (e, pos), (s,))
        m.w16(e + 0x10, saved)
        for r in m.log:
            out.append(dict(entry=o['entry'], kind=r['kind'], anim=r['anim'], pos=r['pos'], scale=r['scale'],
                            layer=r.get('layer', -1), op=o['op']))
    return out


# ---------------------------------------------------------------- PC
def pc_run(c):
    m = P.Machine(c.W, c.H)
    m.text_width = TEXT_W
    for a in (0x6f9400, 0x6f95a0, 0xa14050, 0x4288a0):
        m.stub(a, 0)
    m.stub(0x7910f0, 0)                         # the charge 0 of 6: never full (0x84578e..0x8457b3)
    m.stub(0x791380, 6)
    hud = m.alloc(0x10000)
    m.wf(P.MANAGER + 0x2a388, c.hud)
    if c.greed:
        m.w32(P.GAME + 0x26614, 2)
    m.call(0x9a8230, hud)
    f = 1.0 - c.hud
    m.wf(hud + 0x5958, 24 - 12 * f)            # the top row at the image's y (the owner's word)
    m.wf(hud + 0x5960, 24 - 12 * f)
    cfg = m.alloc(0x200)
    for o in range(0, 0x200, 4):
        m.w32(cfg + o, 6)
    m.w32(cfg, 3)
    m.w32(cfg + 4, 105)
    m.stub(0x72fd10, cfg)
    vec = m.alloc(4 * 1000)                     # the item config's vector (Manager + 0x2a404, 0x847eed)
    m.w32(vec + 4 * 105, cfg)
    m.w32(vec + 4 * 0x2c6, cfg)
    m.w32(P.MANAGER + 0x2a404, vec); m.w32(P.MANAGER + 0x2a408, vec + 4 * 1000)
    for spr in (hud + 0x39a0, hud + 0x3778, hud + 0x55f0, hud + 0x5818, hud + 0x5704):  # hearts, bar, grid, spells, bag
        m.loaded(spr)
    red = m.string('RedHeartFull')
    players = {}
    for k in c.present():
        e = hud + 0x6dc * k
        p = P.PLAYERS + k * P.PLAYER_SIZE
        players[k] = p
        m.w32(e, p); m.w32(e + 4, hud); m.w16(e + 8, k)
        m.w32(p + 0x13c0, c.types[k]); m.w32(p + 0x161c, k)
        for i in range(HEARTS):
            m.w8(e + i * 16 + 0x10, 1); m.w32(e + i * 16 + 0x18, red)
        for sl in (0, 1):
            m.w32(p + 0x1580 + sl * 0x20, 105); m.w32(p + 0x1584 + sl * 0x20, 3)
            m.w32(e + 0x190 + sl * 0x1c, 0)    # the item's picture record (0x845e26): index, then the texture
            m.w32(e + 0x198 + sl * 0x1c, m.texture('active%d' % sl))
            if c.trinkets[k] >> sl & 1:
                r = e + 0x200 + sl * 0x18
                m.w32(r, 5 + sl); m.w32(r + 8, m.texture('trinket%d' % sl))
                m.wf(r + 0x10, 16.0); m.wf(r + 0x14, 16.0)
        for i in range(c.pockets):
            m.w32(p + 0x17a0 + 8 * i, 3 if c.kinds[i] == 2 else 1 + i); m.w32(p + 0x17a4 + 8 * i, c.kinds[i])
        for sl in (2, 3):
            m.w32(p + 0x1580 + sl * 0x20, 0x2c6 if (c.bag and sl == 2) else 105); m.w32(p + 0x1584 + sl * 0x20, 3)
            m.w32(e + 0x190 + sl * 0x1c, 0); m.w32(e + 0x198 + sl * 0x1c, m.texture('active%d' % sl))
        for i in range(4):
            m.loaded(e + 0x238 + i * 0x11c)    # the card, and the pockets behind it (0x8474b2: 0x11c apart)
    for k in range(4):
        if k + 4 in players and c.types[k] == JACOB:
            m.w32(players[k] + 0x1e68, players[k + 4])
            m.w32(players[k + 4] + 0x1e68, players[k])
    out, layouts = [], {}
    for k in c.present():
        m.log.clear()
        L = m.call(0x9a6570, hud, (k, players[k]))
        layouts[k] = L
        m.call(0x9a6fe0, hud, (k, L))
        for r in m.log:
            out.append(dict(entry=k, kind=r['kind'], anim=r['anim'], pos=r['pos'], scale=r['scale'], layer=-1))
    return out, layouts


# ---------------------------------------------------------------- the comparison
def norm(r):
    return (r['entry'], r['kind'], r['anim'])


def compare(pc, ios, eps=0.05):
    left = list(ios)
    miss_pc, miss_ios = [], []
    for r in pc:
        hit = None
        for i, q in enumerate(left):
            same = norm(q) == norm(r) or (r['anim'] == 'layer' and q['entry'] == r['entry'] and q['kind'] == 'render')
            if same and abs(q['pos'][0] - r['pos'][0]) < eps and abs(q['pos'][1] - r['pos'][1]) < eps \
                    and abs(abs(q['scale'][0]) - abs(r['scale'][0])) < 0.01:
                hit = i
                break
        if hit is None:
            miss_pc.append(r)
        else:
            left.pop(hit)
    miss_ios = left
    return miss_pc, miss_ios


def fmt(r):
    return 'entry %d %-6s %-14s %7.2f,%7.2f scale %.2f %s' % (r['entry'], r['kind'], r['anim'], r['pos'][0],
                                                              r['pos'][1], abs(r['scale'][0]), r.get('op', ''))


# Known, accepted differences (the research notes of 05.10.2026, co-op HUD on PC): reported, not failed.
#  - the 1-unit white divider line Rep+ draws between a compact pair's Jacob and Esau (0x844182): the image's
#    HUD has no line primitive - not ported;
#  - T.Cain's bag preview in a compact row: its eight ingredient slots the image spaces 12 apart at any scale,
#    Rep+ 12 x scale (0x849942) - the Result and the bag itself are in Rep+'s places;
#  - Esau's (bottom right, full J&E layout) card name: Rep+ puts it at +13s - (width*s + 12) (0x8489b2..0x8489ef),
#    the image's renderer at -16s - width*s: 2.5 units further left at the half scale.
def known(miss_pc, miss_ios, c, layouts):
    notes = []
    for r in list(miss_pc):
        if r['kind'] == 'line':
            miss_pc.remove(r)
            notes.append('divider line not ported (%.0f,%.0f +%.0f)' % (r['pos'][0], r['pos'][1], r['scale'][0]))
    if c.bag:
        pc_idle = [r for r in miss_pc if r['anim'] == 'Idle']
        ios_idle = [r for r in miss_ios if r['anim'] == 'Idle']
        if pc_idle and len(pc_idle) == len(ios_idle):
            for r in pc_idle:
                miss_pc.remove(r)
            for r in ios_idle:
                miss_ios.remove(r)
            notes.append("bag ingredient slots at the image's spacing")
    for r in list(miss_pc):
        if r['kind'] != 'text' or layouts.get(r['entry']) != 1 or r['entry'] < 4:
            continue
        for q in list(miss_ios):
            if q['entry'] == r['entry'] and q['kind'] == 'text' and abs(q['pos'][1] - r['pos'][1]) < 0.05                     and abs((r['pos'][0] - q['pos'][0]) - 2.5) < 0.05:
                miss_pc.remove(r)
                miss_ios.remove(q)
                notes.append("Esau's card name 2.5 left of Rep+'s")
                break
    return notes


def run_case(c, verbose=False):
    ops, drawn, layouts, corners = desk_ops(c)
    if 0 not in drawn and 0 in c.present():
        ops = game_entry0_ops(c) + ops
    if not drawn and len(c.present()) < 2:
        ops = game_entry0_ops(c)
    ios = ios_run(c, ops)
    pc, pc_layouts = pc_run(c)
    miss_pc, miss_ios = compare(pc, ios)
    notes = known(miss_pc, miss_ios, c, pc_layouts)
    lay_ok = all(layouts.get(k) == pc_layouts.get(k) for k in c.present())
    ok = not miss_pc and not miss_ios and lay_ok
    print('%-4s %-48s pieces PC %3d iOS %3d  layouts %s%s' % ('OK' if ok else 'FAIL', c.name, len(pc), len(ios),
          ' '.join('%d:%d' % (k, pc_layouts[k]) for k in c.present()), '' if lay_ok else ' (desk %s)' % layouts))
    for n in sorted(set(notes)):
        print('     known: %s (x%d)' % (n, notes.count(n)))
    for r in miss_pc:
        print('     PC only : ' + fmt(r))
    for r in miss_ios:
        print('     iOS only: ' + fmt(r))
    if verbose:
        for r in pc:
            print('     PC      ' + fmt(r))
        for r in ios:
            print('     iOS     ' + fmt(r))
    return ok, miss_pc, miss_ios


def cases():
    out = []
    line_ups = [
        ('2p Isaac + Isaac', [0, 0]),
        ('3p singles', [0, 0, 0]),
        ('4p singles', [0, 0, 0, 0]),
        ('solo J&E', [JACOB]),
        ('J&E P1 + 1', [JACOB, 0]),
        ('J&E P1 + 2', [JACOB, 0, 0]),
        ('J&E P1 + 3', [JACOB, 0, 0, 0]),
        ('Isaac + J&E', [0, JACOB]),
        ('4p all J&E', [JACOB, JACOB, JACOB, JACOB]),
        ('4p Isaac, J&E, J&E, T.Isaac', [0, JACOB, JACOB, T_ISAAC]),
        ('3p T.Isaac, T.???, T.Cain', [T_ISAAC, T_BLUEBABY, T_CAIN]),
        ('4p T.Cain, T.Isaac, T.???, T.Isaac', [T_CAIN, T_ISAAC, T_BLUEBABY, T_ISAAC]),
        ('2p T.Isaac + T.???', [T_ISAAC, T_BLUEBABY]),
        ('2p Isaac + T.Isaac', [0, T_ISAAC]),
        ('2p T.??? + Isaac', [T_BLUEBABY, 0]),
        ('2p T.Isaac + J&E', [T_ISAAC, JACOB]),
        ('3p J&E, T.???, T.Isaac', [JACOB, T_BLUEBABY, T_ISAAC]),
        ('4p Isaac, T.Cain, J&E, T.???', [0, T_CAIN, JACOB, T_BLUEBABY]),
    ]
    for name, t in line_ups:
        out.append(Case(name, t))
    # trinkets: none, one each slot
    out.append(Case('4p singles, trinkets 0/1/2/3', [0, 0, 0, 0], trinkets=[0, 1, 2, 3, 0, 0, 0, 0]))
    out.append(Case('J&E P1 + 1, Esau one trinket', [JACOB, 0], trinkets=[3, 3, 0, 0, 1, 0, 0, 0]))
    for hud in (0.5, 0.0):
        out.append(Case('4p singles HudOffset %.1f' % hud, [0, 0, 0, 0], hud=hud))
        out.append(Case('4p all J&E HudOffset %.1f' % hud, [JACOB, JACOB, JACOB, JACOB], hud=hud))
        out.append(Case('3p T.Isaac, T.???, T.Cain HudOffset %.1f' % hud, [T_ISAAC, T_BLUEBABY, T_CAIN], hud=hud))
    out.append(Case('Greed 2p', [0, 0], greed=1))
    out.append(Case('Greed 4p', [0, 0, 0, 0], greed=1))
    out.append(Case('Greed J&E P1 + 1', [JACOB, 0], greed=1))
    out.append(Case('Greed 4p all J&E', [JACOB, JACOB, JACOB, JACOB], greed=1))
    out.append(Case('Greed 3p T.Isaac, T.???, T.Cain', [T_ISAAC, T_BLUEBABY, T_CAIN], greed=1))
    out.append(Case('Greed 2p T.??? + Isaac', [T_BLUEBABY, 0], greed=1))
    out.append(Case('4p singles, three pockets each', [0, 0, 0, 0], pockets=3))
    out.append(Case('2p Isaac + Isaac, three pockets each', [0, 0], pockets=3))
    out.append(Case('4p singles, card + pill + pocket active', [0, 0, 0, 0], pockets=3, kinds=[0, 1, 2, 0]))
    out.append(Case('4p singles, a pocket active first', [0, 0, 0, 0], pockets=2, kinds=[2, 0, 0, 0]))
    out.append(Case('2p, a pocket active first', [0, 0], pockets=1, kinds=[2, 0, 0, 0]))
    out.append(Case('solo J&E, pocket actives first', [JACOB], pockets=1, kinds=[2, 0, 0, 0]))
    out.append(Case('4p T.Cain with the bag first', [T_CAIN, T_CAIN, T_CAIN, T_CAIN], pockets=1, kinds=[2, 0, 0, 0],
                    bag=True))
    out.append(Case('2p T.Cain with the bag first', [T_CAIN, T_CAIN], pockets=1, kinds=[2, 0, 0, 0], bag=True))
    out.append(Case('J&E P1 + 2, card + pocket active + pill', [JACOB, 0, 0], pockets=3, kinds=[0, 2, 1, 0]))
    out.append(Case('16:9 4p all J&E', [JACOB, JACOB, JACOB, JACOB], W=480.0))
    out.append(Case('4:3 4p singles', [0, 0, 0, 0], W=360.0))
    return out


# ---------------------------------------------------------------- pictures of the emulated frames
def picture(c, pc, ios, path, k=3):
    """Every piece both games drew, as boxes: the PC's in red, the image's in green (where they agree: both)"""
    from PIL import Image, ImageDraw
    im = Image.new('RGB', (int(c.W * k), int(c.H * k) + 24), (24, 22, 26))
    d = ImageDraw.Draw(im)
    d.text((6, 4), '%s  (canvas %.0fx%.0f, HudOffset %.1f%s)  red: PC Rep+ 1.9.7, green: the port on the iOS image' %
           (c.name, c.W, c.H, c.hud, ', Greed' if c.greed else ''), fill=(220, 220, 220))
    d.rectangle([0, 24, c.W * k - 1, 24 + c.H * k - 1], outline=(80, 80, 80))

    def box(r):
        x, y = r['pos']
        if r['kind'] == 'image':
            return x, y, x + r['scale'][0], y + r['scale'][1]
        s = abs(r['scale'][0]) or 1.0
        if r['kind'] == 'text':
            return x, y, x + TEXT_W * s, y + 8 * s
        if r['anim'] in ('BarEmpty', 'BarOverlay6'):
            return x - 4 * s, y - 16 * s, x + 4 * s, y + 16 * s
        if r['anim'] in ('HUD', 'HUDSmall'):
            return x - 8 * s, y - 12 * s, x + 8 * s, y + 12 * s
        return x - 7 * s, y - 6 * s, x + 7 * s, y + 7 * s

    for rows, col, w in ((pc, (230, 60, 60), 3), (ios, (60, 220, 90), 1)):
        for r in rows:
            if r['kind'] == 'line':
                x, y = r['pos']
                d.line([x * k, 24 + y * k, (x + r['scale'][0]) * k, 24 + y * k], fill=col, width=w)
                continue
            x0, y0, x1, y1 = box(r)
            d.rectangle([x0 * k, 24 + y0 * k, x1 * k, 24 + y1 * k], outline=col, width=w)
    im.save(path)


def draw_all(out_dir, words=()):
    os.makedirs(out_dir, exist_ok=True)
    for c in cases():
        if words and not all(w.lower() in c.name.lower() for w in words):
            continue
        ops, drawn, layouts, corners = desk_ops(c)
        if 0 not in drawn and 0 in c.present():
            ops = game_entry0_ops(c) + ops
        ios = ios_run(c, ops)
        pc, _ = pc_run(c)
        name = ''.join(ch if ch.isalnum() else '-' for ch in c.name).strip('-').lower()
        picture(c, pc, ios, os.path.join(out_dir, name + '.png'))
        print('drew', name)


if __name__ == '__main__' and '--draw' in sys.argv:
    i = sys.argv.index('--draw')
    draw_all(sys.argv[i + 1], [a for a in sys.argv[i + 2:]])
    sys.exit(0)

if __name__ == '__main__':
    verbose = '-v' in sys.argv
    words = [a for a in sys.argv[1:] if a != '-v']
    good = bad = 0
    for c in cases():
        if words and not all(w.lower() in c.name.lower() for w in words):
            continue
        ok = run_case(c, verbose)[0]
        good += ok
        bad += not ok
    print('cases %d, matched %d, failed %d' % (good + bad, good, bad))
