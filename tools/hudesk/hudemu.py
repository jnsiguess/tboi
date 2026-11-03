# -*- coding: utf-8 -*-
"""Runs the iOS image's own player-HUD renderers under unicorn and records what they draw.

The renderers (hearts 0x6e5f24, actives 0x6e6df8, trinkets 0x6e82f0, the pocket
0x6e8bc0, T.Isaac's grid 0x6ea554, T.???'s spells 0x6eaa30) are called with a fake
Game / HUD / entry / player memory. Every sprite they render ends in the anm2
Render 0x89e4c0 (sprite, &pos, &crop, &crop) or RenderLayer 0x89e550 (sprite,
layer, &pos, ...), every text in the font's draw 0x8be718 / 0x8be794: those are
stubbed and recorded, with the sprite's scale (+0xdc), FlipX (+0x140) and the
animation last set on it (SetFrame 0x89eab8). That is the screen position the
game really gives each piece - what hudgeom.c is checked against.

    py -3 tools/hudesk/hudemu.py            the self-test: one call of each renderer
"""
import os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import simload  # noqa: E402
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UcError  # noqa: E402
from unicorn.arm64_const import *  # noqa: E402,F401,F403

IMG = 0x100000000                # the image at its own vmaddr: a null pointer lands in empty memory, not in the image
simload.BASE = IMG

TRAMP = 0x80000000               # one `ret` per import: a foreign call returns at once
STACK = 0x90000000
STACK_SIZE = 0x100000
RET_MAGIC = 0x9f000000
HEAP = 0xa0000000                # strings and small objects the harness hands out
GAME = 0x300000000
GAME_SIZE = 0x400000
MANAGER = 0x310000000
PLAYERS = 0x320000000
PLAYER_SIZE = 0x10000

GAME_PTR = 0xac3b90
MANAGER_PTR = 0xac2428
CANVAS_W_AT, CANVAS_H_AT = 0xac24d8, 0xac24dc
HUD_OFF = 0x47fe0
ENTRY = 0x918
HEARTS_OBJ = 0xc4bf8

FN = dict(hearts=0x6e5f24, active=0x6e6df8, trinket=0x6e82f0, pocket=0x6e8bc0, grid=0x6ea554, spells=0x6eaa30)
RENDER, RENDER_LAYER, SET_FRAME = 0x89e4c0, 0x89e550, 0x89eab8
FONT_DRAW, FONT_DRAW_SCALED, FONT_WIDTH = 0x8be718, 0x8be794, 0x8bdf88
TEX_STUB = 0x9e000000            # a fake texture's draw (vtable + 0x40): (tex, &src quad, &dst quad, &colour)

_IMG = None


def image():
    global _IMG
    if _IMG is None:
        im = simload.Image(simload.BIN)
        im.apply_fixups()
        swamp = 0xDEAD000000000000
        mem = im.mem
        for seg in im.segs:
            if seg['name'] in ('__PAGEZERO', '__TEXT', '__LINKEDIT'):
                continue
            a = seg['vmaddr'] + im.slide - IMG
            for o in range(a, min(a + seg['vmsize'], len(mem) - 8), 8):
                v = struct.unpack_from('<Q', mem, o)[0]
                if v & 0xFFFF000000000000 == swamp:
                    struct.pack_into('<Q', mem, o, TRAMP + (v & 0xFFFFFF) * 4)
        _IMG = im
    return _IMG


def _x(s, i): return s.mu.reg_read(UC_ARM64_REG_X0 + i)
def _setx0(s, v): s.mu.reg_write(UC_ARM64_REG_X0, v & 0xffffffffffffffff)


def _std_string_set(s, at, text):
    """a libc++ short std::string (23 bytes, size in the last byte)"""
    b = text.encode()[:22]
    s.mu.mem_write(at, b + b'\0' * (23 - len(b)))
    s.w8(at + 23, len(b))


def _imp_strlen(s): _setx0(s, len(s.cstr(_x(s, 0))))
def _imp_assign(s): _std_string_set(s, _x(s, 0), s.cstr(_x(s, 1))); _setx0(s, _x(s, 0))
def _imp_copy(s): s.mu.mem_write(_x(s, 0), bytes(s.mu.mem_read(_x(s, 1), 24))); _setx0(s, _x(s, 0))
def _imp_nop(s): pass
def _imp_one(s): _setx0(s, 1)
def _imp_new(s): _setx0(s, s.alloc(max(16, _x(s, 0))))
def _imp_memcpy(s):
    n = _x(s, 2)
    if n:
        s.mu.mem_write(_x(s, 0), bytes(s.mu.mem_read(_x(s, 1), n)))
def _imp_memset(s):
    n = _x(s, 2)
    if n:
        s.mu.mem_write(_x(s, 0), bytes([_x(s, 1) & 0xff]) * n)


def _imp_fn1(fn, single):
    import math

    def h(s):
        if single:
            v = f32(s.mu.reg_read(UC_ARM64_REG_S0))
            s.mu.reg_write(UC_ARM64_REG_S0, struct.unpack('<I', struct.pack('<f', fn(v)))[0])
        else:
            v = struct.unpack('<d', struct.pack('<Q', s.mu.reg_read(UC_ARM64_REG_D0)))[0]
            s.mu.reg_write(UC_ARM64_REG_D0, struct.unpack('<Q', struct.pack('<d', fn(v)))[0])
    return h


def _imp_snprintf(s):
    fmt = s.cstr(_x(s, 2))
    out = fmt.replace('%d', str(_x(s, 3) & 0xffffffff)).replace('%s', '')
    b = out.encode()[:max(0, _x(s, 1) - 1)]
    s.mu.mem_write(_x(s, 0), b + b'\0')
    _setx0(s, len(b))


import math as _m
IMPORTS = {
    '_strlen': _imp_strlen, '__ZdlPv': _imp_nop, '__Znwm': _imp_new, '__Znam': _imp_new, '_free': _imp_nop,
    '_malloc': _imp_new, '_memcpy': _imp_memcpy, '_memmove': _imp_memcpy, '_memset': _imp_memset,
    '__ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc': _imp_assign,
    '__ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEC1ERKS5_': _imp_copy,
    '__ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEaSERKS5_': _imp_copy,
    '___cxa_guard_acquire': _imp_one, '___cxa_guard_release': _imp_nop, '___cxa_guard_abort': _imp_nop,
    '_sinf': _imp_fn1(_m.sin, True), '_sin': _imp_fn1(_m.sin, False), '_cosf': _imp_fn1(_m.cos, True),
    '_cos': _imp_fn1(_m.cos, False), '_floorf': _imp_fn1(_m.floor, True), '_ceilf': _imp_fn1(_m.ceil, True),
    '_sqrtf': _imp_fn1(_m.sqrt, True), '_snprintf': _imp_snprintf,
}


def f32(v):
    return struct.unpack('<f', struct.pack('<I', v & 0xffffffff))[0]


class Machine:
    def __init__(s, W=585.0, H=270.0):
        im = image()
        s.im = im
        mu = s.mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        size = (len(im.mem) + 0xffff) & ~0xffff
        mu.mem_map(IMG, size)
        mu.mem_write(IMG, bytes(im.mem))
        n = len(im.imports)
        mu.mem_map(TRAMP, (n * 4 + 0xffff) & ~0xffff)
        mu.mem_write(TRAMP, struct.pack('<I', 0xD65F03C0) * n)
        mu.mem_map(STACK, STACK_SIZE)
        mu.mem_map(RET_MAGIC, 0x1000)
        mu.mem_map(HEAP, 0x100000)
        s.heap = HEAP
        mu.mem_map(GAME, GAME_SIZE)
        mu.mem_map(MANAGER, 0x400000)
        mu.mem_map(PLAYERS, PLAYER_SIZE * 8)
        s.w64(IMG + GAME_PTR, GAME)
        s.w64(IMG + MANAGER_PTR, MANAGER)
        s.wf(IMG + CANVAS_W_AT, W)
        s.wf(IMG + CANVAS_H_AT, H)
        s.W, s.H = W, H
        s.log = []
        s.frame = {}
        s.text_width = 60.0
        s.stubs = {IMG + RENDER: s._render, IMG + RENDER_LAYER: s._render_layer, IMG + SET_FRAME: s._set_frame,
                   IMG + FONT_DRAW: s._font, IMG + FONT_DRAW_SCALED: s._font_scaled, IMG + FONT_WIDTH: s._font_width}
        s.import_hooks = {}
        s.unknown_imports = {}
        for i, name in enumerate(im.imports):
            s.import_hooks[TRAMP + i * 4] = name
        mu.hook_add(UC_HOOK_MEM_UNMAPPED, s._unmapped)
        mu.hook_add(UC_HOOK_CODE, s._code)
        s.mapped_extra = []
        s.items = {}
        mu.mem_map(TEX_STUB, 0x1000)
        mu.mem_write(TEX_STUB, struct.pack('<I', 0xD65F03C0))
        s.stubs[TEX_STUB] = s._tex_draw
        s.tex_vtable = s.alloc(0x100)
        for o in range(0, 0x100, 8):
            s.w64(s.tex_vtable + o, TEX_STUB)
        s.tex_names = {}
        # the item config's vector (0x73f7f4: Manager + 0x55f30, {begin, end})
        s.cfg_vec = s.alloc(8 * 1000)
        s.w64(MANAGER + 0x55f30, s.cfg_vec)
        s.w64(MANAGER + 0x55f38, s.cfg_vec + 8 * 1000)

    def alloc(s, n):
        a = s.heap
        s.heap += (n + 15) & ~15
        return a

    def item(s, iid, kind=3, charges=6):
        """An item config: kind 3 active (charges at +0x?? filled broadly), kind 2 trinket"""
        if iid in s.items:
            return s.items[iid]
        c = s.alloc(0x200)
        for o in range(8, 0xb0, 4):
            s.w32(c + o, charges)
        s.w32(c, kind)
        s.w32(c + 4, iid)
        s.w32(c + 0xb0, 0)
        s.w64(s.cfg_vec + 8 * iid, c)
        s.items[iid] = c
        return c

    # --- memory
    def w64(s, a, v): s.mu.mem_write(a, struct.pack('<Q', v & 0xffffffffffffffff))
    def w32(s, a, v): s.mu.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def w16(s, a, v): s.mu.mem_write(a, struct.pack('<H', v & 0xffff))
    def w8(s, a, v): s.mu.mem_write(a, bytes([v & 0xff]))
    def wf(s, a, v): s.mu.mem_write(a, struct.pack('<f', v))
    def r64(s, a): return struct.unpack('<Q', s.mu.mem_read(a, 8))[0]
    def r32(s, a): return struct.unpack('<I', s.mu.mem_read(a, 4))[0]
    def rf(s, a): return struct.unpack('<f', s.mu.mem_read(a, 4))[0]
    def r8(s, a): return s.mu.mem_read(a, 1)[0]

    def cstr(s, a):
        out = b''
        while a and len(out) < 200:
            c = s.mu.mem_read(a, 1)[0]
            if not c:
                break
            out += bytes([c])
            a += 1
        return out.decode('latin1')

    def code_word(s, at, w):
        """Write a word of the image's code (an image offset) and drop unicorn's translated blocks over it"""
        s.w32(IMG + at, w)
        s.mu.ctl_remove_cache(IMG + at, IMG + at + 4)

    def string(s, text):
        a = s.heap
        b = text.encode() + b'\0'
        s.mu.mem_write(a, b)
        s.heap += (len(b) + 15) & ~15
        return a

    def _unmapped(s, mu, access, addr, size, value, user):
        page = addr & ~0xffff
        try:
            mu.mem_map(page, 0x10000)
        except UcError:
            return False
        s.mapped_extra.append(page)
        return True

    # --- stubs
    def _ret(s):
        s.mu.reg_write(UC_ARM64_REG_PC, s.mu.reg_read(UC_ARM64_REG_LR))

    def _code(s, mu, addr, size, user):
        f = s.stubs.get(addr)
        if f:
            f()
            s._ret()
            return
        if TRAMP <= addr < TRAMP + 0x100000:
            name = s.import_hooks.get(addr, '?')
            h = IMPORTS.get(name)
            if h:
                h(s)
            else:
                s.unknown_imports[name] = s.unknown_imports.get(name, 0) + 1

    def _vec(s, a):
        return (s.rf(a), s.rf(a + 4))

    def _sprite(s, spr):
        return dict(scale=(s.rf(spr + 0xdc), s.rf(spr + 0xe0)), flip=s.r8(spr + 0x140), anim=s.frame.get(spr, '?'))

    def _render(s):
        mu = s.mu
        spr = mu.reg_read(UC_ARM64_REG_X0)
        pos = s._vec(mu.reg_read(UC_ARM64_REG_X1))
        s.log.append(dict(kind='render', sprite=spr, layer=-1, pos=pos, **s._sprite(spr)))

    def _render_layer(s):
        mu = s.mu
        spr = mu.reg_read(UC_ARM64_REG_X0)
        layer = mu.reg_read(UC_ARM64_REG_X1) & 0xffffffff
        pos = s._vec(mu.reg_read(UC_ARM64_REG_X2))
        s.log.append(dict(kind='render', sprite=spr, layer=layer, pos=pos, **s._sprite(spr)))

    def _set_frame(s):
        mu = s.mu
        spr = mu.reg_read(UC_ARM64_REG_X0)
        name = mu.reg_read(UC_ARM64_REG_X1)
        s.frame[spr] = s.cstr(name) if name < 0x1000000000 else '?'
        mu.reg_write(UC_ARM64_REG_X0, 1)

    def _text(s, scaled):
        mu = s.mu
        sx = mu.reg_read(UC_ARM64_REG_S0)
        sy = mu.reg_read(UC_ARM64_REG_S1)
        x, y = f32(sx), f32(sy)
        sc = (f32(mu.reg_read(UC_ARM64_REG_S2)), f32(mu.reg_read(UC_ARM64_REG_S3))) if scaled else (1.0, 1.0)
        s.log.append(dict(kind='text', sprite=mu.reg_read(UC_ARM64_REG_X0), layer=-1, pos=(x, y), scale=sc,
                          flip=0, anim='text'))

    def _tex_draw(s):
        mu = s.mu
        tex = mu.reg_read(UC_ARM64_REG_X0)
        src = struct.unpack('<8f', mu.mem_read(mu.reg_read(UC_ARM64_REG_X1), 32))
        dst = struct.unpack('<8f', mu.mem_read(mu.reg_read(UC_ARM64_REG_X2), 32))
        s.log.append(dict(kind='image', sprite=tex, layer=-1, pos=(min(dst[0::2]), min(dst[1::2])),
                          scale=(max(dst[0::2]) - min(dst[0::2]), max(dst[1::2]) - min(dst[1::2])),
                          flip=0, anim=s.tex_names.get(tex, 'texture'), src=src, dst=dst))
        mu.reg_write(UC_ARM64_REG_X0, 0)

    def texture(s, name):
        """A fake texture object: its vtable's draw (+0x40) is recorded"""
        t = s.alloc(0x100)
        s.w64(t, s.tex_vtable)
        s.tex_names[t] = name
        return t

    def _font(s): s._text(False)
    def _font_scaled(s): s._text(True)

    def _font_width(s):
        s.mu.reg_write(UC_ARM64_REG_X0, int(s.text_width))

    # --- a call
    def call(s, fn, args=(), fargs=(), limit=2000000):
        mu = s.mu
        for i, v in enumerate(args):
            mu.reg_write(UC_ARM64_REG_X0 + i, v & 0xffffffffffffffff)
        for i, v in enumerate(fargs):
            mu.reg_write(UC_ARM64_REG_S0 + i, struct.unpack('<I', struct.pack('<f', v))[0])
        mu.reg_write(UC_ARM64_REG_SP, STACK + STACK_SIZE - 0x1000)
        mu.reg_write(UC_ARM64_REG_LR, RET_MAGIC)
        mu.reg_write(UC_ARM64_REG_X29, 0)
        mu.emu_start(IMG + fn if fn < IMG else fn, RET_MAGIC, count=limit)
        if mu.reg_read(UC_ARM64_REG_PC) != RET_MAGIC:
            raise RuntimeError('%x did not return (stopped at %x)' % (fn, mu.reg_read(UC_ARM64_REG_PC)))
        return mu.reg_read(UC_ARM64_REG_X0)


if __name__ == '__main__':
    m = Machine()
    hud = GAME + HUD_OFF
    entry = hud + ENTRY * 1
    player = PLAYERS
    m.w64(entry, player)
    m.w16(entry + 0x10, 1)
    red = m.string('RedHeartFull')
    for i in range(8):
        rec = entry + 0x18 + i * 0x18
        m.w8(rec, 1)
        m.w64(rec + 8, red)
    pos = m.heap
    m.heap += 16
    m.wf(pos, 450.0)
    m.wf(pos + 4, 24.0)
    try:
        m.call(FN['hearts'], (entry, hud + HEARTS_OBJ, pos), (1.0,))
    except UcError as e:
        print('stopped:', e, hex(m.mu.reg_read(UC_ARM64_REG_PC)))
    for r in m.log:
        print(r['kind'], r['anim'], '%.1f,%.1f' % r['pos'], 'scale %.2f,%.2f' % r['scale'], 'flip', r['flip'])
