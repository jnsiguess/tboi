# -*- coding: utf-8 -*-
"""Jacob & Esau on a pad or a keyboard: the game's own decisions, run under unicorn.

The image's code is run as it is, from the file (offsets are the addresses, as
tools/dis.py reads them), with a fake player, his twin and an input that answers
"held" (0x671c5c) and "went down this update" (0x671c6c) from a table. Three
roads of the Repentance engine:

    pocket   0x52a800 -> 0x52a918   which action uses the card/pill/rune, and
                                    whether DROP (11) must be held for it
    active   0x528898 -> 0x5289b0   which action uses the active item, and
                                    whether a held DROP stops it
    walk     0x515294 -> 0x51530c / 0x5154fc   whether a held DROP freezes him

Every answer is the game's, read off its registers at the stop; nothing of
the decision is written here. The port's own patch of the drop road
(pad.c drop_once_per_press, 0x52ad84) is NOT on any of these roads - each asks
0x671c5c / 0x671c6c with its own `bl`, which the harness checks too.

    py -3 tools/twindesk.py
"""
import os, struct, sys
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UcError
from unicorn.arm64_const import *

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
raw = open(BIN, "rb").read()

IMG_SIZE = (len(raw) + 0xfff) & ~0xfff
STACK, STACK_SIZE = 0x40000000, 0x100000
GAME, MANAGER = 0x50000000, 0x58000000
JACOB, ESAU = 0x60000000, 0x60100000
PLAYER_SIZE = 0x10000
RET_MAGIC = 0x7f000000

GAME_PTR, MANAGER_PTR = 0xac3b90, 0xac2428
HELD, TRIG = 0x671c5c, 0x671c6c
STUB_ZERO = (0x89a0b4, 0x6a6658)          # "is the game paused / a transition on": no
DEVICE = 3                                 # a pad's controller index (not 0x10DEAD, the touch screen)

ACTION_NAMES = {8: 'BOMB', 9: 'ITEM', 10: 'PILLCARD', 11: 'DROP', 19: 'touch use (Jacob)',
                 21: 'touch use (twin)', 18: 'touch active (Jacob)', 20: 'touch active (Esau)'}


def word(at):
    return struct.unpack_from('<I', raw, at)[0]


def bl_target(at):
    w = word(at)
    assert w >> 26 == 0x25, hex(at)
    imm = w & 0x03ffffff
    if imm & 0x02000000:
        imm -= 0x04000000
    return at + imm * 4


class Desk:
    def __init__(s, held, trig):
        s.held, s.trig = set(held), set(trig)
        s.asked = []
        mu = s.mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        mu.mem_map(0, IMG_SIZE)
        mu.mem_write(0, raw)
        mu.mem_write(GAME_PTR, struct.pack('<Q', GAME))
        mu.mem_write(MANAGER_PTR, struct.pack('<Q', MANAGER))
        mu.mem_map(STACK, STACK_SIZE)
        mu.mem_map(GAME, 0x400000)
        mu.mem_map(MANAGER, 0x100000)
        mu.mem_map(RET_MAGIC, 0x1000)
        for p in (JACOB, ESAU):
            mu.mem_map(p, PLAYER_SIZE)
        s.player(JACOB, 19, ESAU, 0)
        s.player(ESAU, 20, JACOB, 1)
        mu.hook_add(UC_HOOK_CODE, s.on_code)

    def player(s, p, kind, twin, order):
        s.w32(p + 0x1738, kind)
        s.w64(p + 0x2580, twin)
        s.w32(p + 0x19f0, order)            # the order the twins were made in: Jacob first
        s.w32(p + 0x19ec, DEVICE)
        s.mu.mem_write(p + 0x400, b'\x01')  # walking roads: a body that moves (0x5152f8)
        s.w32(p + 0x404, 0)

    def w32(s, at, v): s.mu.mem_write(at, struct.pack('<I', v & 0xffffffff))
    def w64(s, at, v): s.mu.mem_write(at, struct.pack('<Q', v & 0xffffffffffffffff))

    def on_code(s, mu, addr, size, _):
        if addr in (HELD, TRIG):
            action = mu.reg_read(UC_ARM64_REG_X1) & 0xffffffff
            answer = action in (s.held if addr == HELD else s.trig)
            s.asked.append(('held' if addr == HELD else 'down', action, int(answer)))
            mu.reg_write(UC_ARM64_REG_X0, int(answer))
        elif addr in STUB_ZERO:
            mu.reg_write(UC_ARM64_REG_X0, 0)
        else:
            return
        mu.reg_write(UC_ARM64_REG_PC, mu.reg_read(UC_ARM64_REG_X30))

    def run(s, start, stops, x0, x1=0):
        mu = s.mu
        mu.reg_write(UC_ARM64_REG_SP, STACK + STACK_SIZE - 0x1000)
        mu.reg_write(UC_ARM64_REG_X0, x0)
        mu.reg_write(UC_ARM64_REG_X1, x1)
        mu.reg_write(UC_ARM64_REG_X30, RET_MAGIC)
        pc = start
        while True:
            try:
                mu.emu_start(pc, RET_MAGIC, count=1)
            except UcError as e:
                raise RuntimeError('fault at %x: %s' % (mu.reg_read(UC_ARM64_REG_PC), e))
            pc = mu.reg_read(UC_ARM64_REG_PC)
            if pc in stops or pc == RET_MAGIC:
                return pc

    def reg(s, n):
        return s.mu.reg_read(UC_ARM64_REG_X0 + n) & 0xffffffff


def pocket(who, held, trig):
    d = Desk(held, trig)
    d.run(0x52a800, {0x52a918}, who)
    return d.reg(20), d.asked


def active(who, held, trig):
    d = Desk(held, trig)
    d.run(0x528898, {0x5289b0, 0x528a14}, who, 0)
    return d.reg(21), d.asked


def walk_frozen(who, held):
    d = Desk(held, ())
    pc = d.run(0x515294, {0x51530c, 0x5154fc}, who)
    return pc == 0x5154fc


def main():
    # the calls the harness leans on are the image's own, unpatched by the port
    for at, fn in ((0x52a8b4, TRIG), (0x52a8e4, HELD), (0x528968, TRIG), (0x5289a0, HELD),
                   (0x5154f4, HELD)):
        assert bl_target(at) == fn, hex(at)
    assert bl_target(0x52ad84) == HELD      # the drop road, as the file has it (pad.c turns it to TRIG)

    fails = 0
    names = {JACOB: 'Jacob', ESAU: 'Esau'}
    pad = {9: 'L2', 10: 'R1', 11: 'R2'}
    kbd = {9: 'Space', 10: 'Q', 11: 'LCtrl'}
    print('pocket item (0x52a800): one press of each button, DROP held or not')
    want = {(JACOB, 9, True): 1, (ESAU, 10, True): 1}
    for who in (JACOB, ESAU):
        for a in (9, 10):
            for drop in (False, True):
                use, asked = pocket(who, {a, 11} if drop else {a}, {a})
                ok = use == want.get((who, a, drop), 0)
                fails += not ok
                print('  %-5s %-8s %-11s -> %s  %s   [pad %s%s / keys %s%s]' % (
                    names[who], ACTION_NAMES[a], 'DROP held' if drop else '', 'USES' if use else '-   ',
                    'ok' if ok else 'FAIL', 'R2 + ' if drop else '', pad[a], 'LCtrl + ' if drop else '', kbd[a]))
    print('active item (0x528898, slot 0):')
    want = {(JACOB, 9, False): 1, (ESAU, 10, False): 1}
    for who in (JACOB, ESAU):
        for a in (9, 10):
            for drop in (False, True):
                use, asked = active(who, {a, 11} if drop else {a}, {a})
                ok = use == want.get((who, a, drop), 0)
                fails += not ok
                print('  %-5s %-8s %-11s -> %s  %s' % (names[who], ACTION_NAMES[a], 'DROP held' if drop else '',
                                                      'USES' if use else '-   ', 'ok' if ok else 'FAIL'))
    print('walking (0x515294): a held DROP')
    for who, want_frozen in ((JACOB, False), (ESAU, True)):
        for drop in (False, True):
            fr = walk_frozen(who, {11} if drop else set())
            ok = fr == (want_frozen and drop)
            fails += not ok
            print('  %-5s %-11s -> %s  %s' % (names[who], 'DROP held' if drop else '', 'stands still' if fr else 'walks',
                                             'ok' if ok else 'FAIL'))
    print('FAILS: %d' % fails if fails else 'all as expected')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
