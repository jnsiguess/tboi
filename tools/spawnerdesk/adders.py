# -*- coding: utf-8 -*-
"""The game's own heart adders run on unicorn over a fake player and a fake
Game (zeroed memory; [0xac3b90] points at it), to see what each call the
STATS tab makes does to the fields it reads back. A call into code that
touches memory we did not lay out stops the run and is reported - such a case
is "not measured", never "passed".

    py -3 tools/spawnerdesk/adders.py
"""
import os
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_MEM_UNMAPPED
from unicorn.arm64_const import (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_LR, UC_ARM64_REG_SP,
                                 UC_ARM64_REG_PC)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
PLAYER, OTHER, GAME, STACK, END, HEAP = 0x20000000, 0x21000000, 0x30000000, 0x40000000, 0x50000000, 0x60000000
F = dict(max=0x16b8, red=0x16bc, eternal=0x16c0, soul=0x16c4, black=0x16c8, bone=0x2450, broken=0x2470,
         rotten=0x2474, soulcharge=0x2478, blood=0x247c, golden=0x1dc4, coins=0x16e0, bombs=0x16dc, keys=0x16d4)


IMPORTS = 0x70000000                                      # every import: "mov x0, #0; ret"


def loaded_image():
    """The image laid out at its own addresses with its chained fixups applied
    (tools/simload.py's walk), the imports pointed at harmless stubs."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import simload
    simload.BASE = 0
    im = simload.Image(BIN)
    d = im.raw
    base, _ = im.fix
    (_v, starts_off, imports_off, symbols_off, imports_count, _f, _sf) = struct.unpack_from('<7I', d, base)
    st = base + starts_off
    for i in range(struct.unpack_from('<I', d, st)[0]):
        info = struct.unpack_from('<I', d, st + 4 + i * 4)[0]
        if not info:
            continue
        s = st + info
        (_size, page_size, _pf, _so, _mv, page_count) = struct.unpack_from('<IHHQIH', d, s)
        starts = struct.unpack_from('<%dH' % page_count, d, s + 22)
        seg = im.segs[i]
        for pi, first in enumerate(starts):
            if first == 0xFFFF:
                continue
            addr = seg['vmaddr'] + pi * page_size + first
            while True:
                raw = struct.unpack_from('<Q', im.mem, addr - im.lo)[0]
                nxt = (raw >> 51) & 0xFFF
                if (raw >> 63) & 1:
                    v = IMPORTS + (raw & 0xFFFFFF) * 8
                else:
                    v = raw & 0xFFFFFFFFF
                struct.pack_into('<Q', im.mem, addr - im.lo, v)
                if nxt == 0:
                    break
                addr += nxt * 4
    return im, imports_count


_IMAGE = None


class Run:
    def __init__(self, img):
        global _IMAGE
        if _IMAGE is None:
            _IMAGE = loaded_image()
        im, nimports = _IMAGE
        self.mu = mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        size = (len(im.mem) + 0xfff) & ~0xfff
        mu.mem_map(0, size)                                # the port maps the image at 0: offsets are addresses
        mu.mem_write(0, bytes(im.mem))
        mu.mem_map(IMPORTS, (nimports * 8 + 0xfff) & ~0xfff)
        mu.mem_write(IMPORTS, struct.pack('<II', 0xD2800000, 0xD65F03C0) * nimports)
        for base, n in ((PLAYER, 0x10000), (OTHER, 0x10000), (GAME, 0x300000), (STACK, 0x20000), (END, 0x1000),
                        (HEAP, 0x100000)):
            mu.mem_map(base, n)
        mu.mem_write(0xac3b90, struct.pack('<Q', GAME))            # Game
        mu.mem_write(0xac2428, struct.pack('<Q', HEAP))            # the manager
        mu.mem_write(HEAP + 0x204d0 + 1, bytes([1]))               # the run is marked first: progress read-only (spawner.c lock_the_run)
        self.fault = None

        def unmapped(uc, access, addr, size, value, data):
            self.fault = 'unmapped access at 0x%x (pc 0x%x)' % (addr, uc.reg_read(UC_ARM64_REG_PC))
            return False
        mu.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)

    def put(self, field, v, who=PLAYER):
        self.mu.mem_write(who + F[field], struct.pack('<i', v))

    def get(self, field, who=PLAYER):
        return struct.unpack('<i', self.mu.mem_read(who + F[field], 4))[0]

    def call(self, fn, *args):
        regs = [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2]
        self.mu.reg_write(regs[0], PLAYER)
        for r, a in zip(regs[1:], args):
            self.mu.reg_write(r, a & 0xffffffffffffffff)
        self.mu.reg_write(UC_ARM64_REG_SP, STACK + 0x18000)
        self.mu.reg_write(UC_ARM64_REG_LR, END)
        self.fault = None
        try:
            self.mu.emu_start(fn, END, count=200000)
        except UcError as e:
            return 'stopped: %s %s' % (e, self.fault or '')
        return None


def fresh(img, ptype, **fields):
    r = Run(img)
    r.mu.mem_write(PLAYER + 0x1738, struct.pack('<i', ptype))
    # the player's collectible counts (HasCollectible reads [+0x1ab8][id]): an empty run, nothing held
    r.mu.mem_write(PLAYER + 0x1ab8, struct.pack('<Q', HEAP + 0x80000))
    r.mu.mem_write(OTHER + 0x1ab8, struct.pack('<Q', HEAP + 0x80000))
    for k, v in fields.items():
        r.put(k, v)
    return r


def forgotten(img):
    """The Forgotten (16) with his Soul (17) at +0x2460: the soul hearts land on the Soul."""
    r = fresh(img, 16, bone=2, red=2)
    r.mu.mem_write(OTHER + 0x1738, struct.pack('<i', 17))
    r.mu.mem_write(PLAYER + 0x2460, struct.pack('<Q', OTHER))
    r.mu.mem_write(OTHER + 0x2460, struct.pack('<Q', PLAYER))
    stop = r.call(0x4ef2d0, 2)
    return stop, r.get('soul'), r.get('soul', OTHER)


CASES = [
    # (what, player type, start fields, function, args, fields to read, expected)
    ('Isaac: AddHearts(+1, as is)', 0, dict(max=6, red=3), 0x4f0338, (1, 1), ['red'], dict(red=4)),
    ('Isaac: AddHearts(-2, as is)', 0, dict(max=6, red=3), 0x4f0338, (-2 & 0xffffffff, 1), ['red'], dict(red=1)),
    ('T.Magdalene: AddHearts(+1, as is) not doubled', 22, dict(max=8, red=2), 0x4f0338, (1, 1), ['red'], dict(red=3)),
    ('T.Magdalene: AddHearts(+1, flag 0) doubled', 22, dict(max=8, red=2), 0x4f0338, (1, 0), ['red'], dict(red=4)),
    ('Keeper: AddHearts(+2) a whole coin', 14, dict(max=4, red=0), 0x4f0338, (2, 1), ['red'], dict(red=2)),
    # its own write only: the health-up bookkeeping after it (0x4efb40, items against +0x1928) needs a real
    # player's items and on zeroed memory asks for nonsense - not measured here
    ('Isaac: AddMaxHearts(+2), its own write', 0, dict(max=6, red=6), 0x4ee8a0, (2, 0), ['max@first'], dict(max=8)),
    ('Bethany: AddSoulCharge(+3)', 18, dict(max=6, red=6), 0x4f08d0, (3,), ['soulcharge'], dict(soulcharge=3)),
    ('Isaac: AddBrokenHearts(+1)', 0, dict(max=6, red=6), 0x5059ac, (1,), ['broken'], dict(broken=1)),
    ('Isaac: AddGoldenHearts(+1)', 0, dict(max=6, red=6), 0x552ab8, (1,), ['golden'], dict(golden=1)),
    ('Isaac: AddCoins(+5)', 0, dict(coins=3), 0x4f0a14, (5,), ['coins'], dict(coins=8)),
    ('Isaac: AddBombs(+2)', 0, dict(bombs=1), 0x4f0ca0, (2,), ['bombs'], dict(bombs=3)),
    ('Isaac: AddKeys(-1)', 0, dict(keys=2), 0x4f0d20, (-1 & 0xffffffff,), ['keys'], dict(keys=1)),
    ('Isaac: AddSoulHearts(+2)', 0, dict(max=6, red=6), 0x4ef2d0, (2,), ['soul'], dict(soul=2)),
    ('Isaac: AddSoulHearts(-1)', 0, dict(max=6, red=6, soul=3), 0x4ef2d0, (-1 & 0xffffffff,), ['soul'], dict(soul=2)),
    ('Isaac: AddBlackHearts(+2): soul 2, mask bit 0', 0, dict(max=6, red=6), 0x4ef078, (2,), ['soul', 'black'],
     dict(soul=2, black=1)),
    ('Bethany: AddSoulHearts(+2) becomes charges', 18, dict(max=6, red=6), 0x4ef2d0, (2,), ['soul', 'soulcharge'],
     dict(soul=0, soulcharge=2)),
    ('Isaac: AddRottenHearts(2, as is)', 0, dict(max=6, red=2), 0x554794, (2, 1), ['red', 'rotten'],
     dict(red=4, rotten=1)),
    # its own write only, as AddMaxHearts: the bookkeeping after it needs a real player's trinkets
    ('Isaac: AddBoneHearts(+1), its own write', 0, dict(max=6, red=6), 0x4ef5c4, (1,), ['bone@first'], dict(bone=1)),
    ('Isaac: AddEternalHearts(+1)', 0, dict(max=6, red=6), 0x4f0590, (1,), ['eternal'], dict(eternal=1)),
    ('T.Bethany: AddBloodCharge(+4)', 36, dict(soul=4), 0x4f04c4, (4,), ['blood'], dict(blood=4)),
]


def main():
    img = open(BIN, 'rb').read()
    ok = measured = 0
    for what, ptype, start, fn, args, read, want in CASES:
        r = fresh(img, ptype, **start)
        first = {}
        for k in read:
            if k.endswith('@first'):
                field = k[:-6]

                def w(uc, access, addr, size, value, data, field=field):
                    if addr == PLAYER + F[field] and field not in first:
                        first[field] = value
                from unicorn import UC_HOOK_MEM_WRITE
                r.mu.hook_add(UC_HOOK_MEM_WRITE, w)
        stop = r.call(fn, *args)
        got = {k[:-6]: first.get(k[:-6]) for k in read if k.endswith('@first')}
        got.update({k: r.get(k) for k in read if not k.endswith('@first')})
        if stop:
            print('NOT MEASURED  %-48s %s' % (what, stop))
            continue
        measured += 1
        good = got == want
        ok += good
        print('%s  %-48s %s' % ('ok  ' if good else 'DIFF', what, got if good else 'got %s, want %s' % (got, want)))
    stop, body, soul = forgotten(img)
    if stop:
        print('NOT MEASURED  The Forgotten: AddSoulHearts(+2) goes to The Soul  %s' % stop)
    else:
        measured += 1
        good = body == 0 and soul == 2
        ok += good
        print('%s  %-48s body %d, The Soul %d' % ('ok  ' if good else 'DIFF', 'The Forgotten: AddSoulHearts(+2) to The Soul',
                                                 body, soul))
    print('%d measured, %d as expected' % (measured, ok))
    sys.exit(0 if ok == measured else 1)


if __name__ == '__main__':
    main()
