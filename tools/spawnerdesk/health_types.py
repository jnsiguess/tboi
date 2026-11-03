# -*- coding: utf-8 -*-
"""The game's own GetHealthType (0x4eeae4) run on unicorn for every player
type 0..40, against the table the desk's model uses (desk.c ht_of) and the
rows spawner_stats.c shows (shim_health_rows keys its choice on it).

    py -3 tools/spawnerdesk/health_types.py
"""
import os
import re
import sys

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_LR, UC_ARM64_REG_SP

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, 'work', 'isaac.arm64')
DESK = os.path.join(ROOT, 'tools', 'spawnerdesk', 'desk.c')
NAMES = ['red', 'soul', 'none', 'coin', 'bone']


def game_health_type(img, t):
    mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    size = (len(img) + 0xfff) & ~0xfff
    mu.mem_map(0, size)
    mu.mem_write(0, img)
    mu.mem_map(0x20000000, 0x10000)                       # the player
    mu.mem_map(0x30000000, 0x10000)                       # the stack
    mu.mem_write(0x20000000 + 0x1738, t.to_bytes(4, 'little'))
    mu.reg_write(UC_ARM64_REG_X0, 0x20000000)
    mu.reg_write(UC_ARM64_REG_SP, 0x30008000)
    mu.reg_write(UC_ARM64_REG_LR, 0x40000000)
    mu.mem_map(0x40000000, 0x1000)
    mu.emu_start(0x4eeae4, 0x40000000, count=200)
    return mu.reg_read(UC_ARM64_REG_X0) & 0xffffffff


def desk_table():
    src = open(DESK, encoding='utf-8').read()
    body = re.search(r'static int ht_of\(int type\) \{(.*?)\n\}', src, re.S).group(1)
    out = {}
    for cases, val in re.findall(r'((?:case \d+: )+)return (\d);', body):
        for c in re.findall(r'case (\d+):', cases):
            out[int(c)] = int(val)
    return out


def main():
    img = open(BIN, 'rb').read()
    desk = desk_table()
    bad = 0
    line = []
    for t in range(41):
        g = game_health_type(img, t)
        d = desk.get(t, 0)
        line.append('%d:%s' % (t, NAMES[g] if g < 5 else g))
        if g != d:
            bad += 1
            print('type %d: the game says %d, the desk model %d' % (t, g, d))
    print(' '.join(line))
    print('%d of 41 types differ' % bad)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
