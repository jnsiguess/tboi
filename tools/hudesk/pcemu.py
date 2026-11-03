# -*- coding: utf-8 -*-
"""Runs the PC game's (isaac-ng.exe, Repentance+ 1.9.7, 32-bit) player-HUD code under unicorn.

The same idea as hudemu.py for the iOS image: a fake Game / HUD / player memory,
the anm2 SetFrame 0x40a6e0 / Render 0x40a0d0 and every texture and font draw
stubbed and recorded, so the positions Rep+ really gives each piece come out of
its own code (the per-player draw 0x9a6fe0, its corners 0x9a6140 / 0x9a8230, the
layout choice 0x9a6570) rather than out of a reading of it.
"""
import os, struct, sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP, \
    UC_X86_REG_EBP, UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3, UC_X86_REG_FS

EXE = r'Z:/Games/Steam/steamapps/common/The Binding of Isaac Rebirth/isaac-ng.exe'

TRAMP = 0x70000000            # one stub per import
STACK = 0x60000000
STACK_SIZE = 0x100000
RET_MAGIC = 0x6f000000
HEAP = 0x50000000
GAME = 0x20000000             # [0xc71678]
MANAGER = 0x28000000          # [0xc7169c]
PLAYERS = 0x40000000
PLAYER_SIZE = 0x10000
TEX_STUB = 0x6e000000

GAME_PTR, MANAGER_PTR = 0xc71678, 0xc7169c
SCREEN_W_AT, SCREEN_H_AT = 0xc78dc4, 0xc78edc
SET_FRAME, RENDER = 0x40a6e0, 0x40a0d0
SET_ANIM = 0x40a5d0               # (name, reset): the hearts' and others' way to pick an animation
FONT_DRAW = 0xa1b2d0              # (font; x xmm2, y xmm3; &text, scale x, scale y, ...)
LAYER_RENDER = 0x409120           # one layer of an anm2 at &pos (the grid's slots, the spells' poops)
LINE_DRAW = 0xa1d710              # (&from, &to, &colour, &colour): a line (J&E compact pair's divider, 0x844182)
FONT_WIDTH = 0xa1ba10             # (font; &text, -1) -> width at scale 1
STATE_RENDER = 0x40a030           # an anm2's animation state (+0x30 main, +0x50 overlay) rendered at &pos

_PE = None


def pe():
    global _PE
    if _PE is None:
        d = open(EXE, 'rb').read()
        p = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, p + 6)[0]
        optsz = struct.unpack_from('<H', d, p + 20)[0]
        opt = p + 24
        base = struct.unpack_from('<I', d, opt + 28)[0]
        secs = []
        for i in range(nsec):
            o = opt + optsz + 40 * i
            name = d[o:o + 8].rstrip(b'\0').decode()
            vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
            secs.append((name, base + va, vsize, raw, rsize))
        imp_rva = struct.unpack_from('<I', d, opt + 96 + 8)[0]
        imports = []      # (iat address, dll, name)

        def rva2off(r):
            for n, va, vs, raw, rs in secs:
                if va - base <= r < va - base + max(vs, rs):
                    return raw + r - (va - base)
        o = rva2off(imp_rva)
        while True:
            oft, ts, fc, name_rva, ft = struct.unpack_from('<IIIII', d, o)
            if not name_rva:
                break
            dll = d[rva2off(name_rva):].split(b'\0', 1)[0].decode()
            t = rva2off(oft or ft)
            k = 0
            while True:
                v = struct.unpack_from('<I', d, t + 4 * k)[0]
                if not v:
                    break
                nm = ('#%d' % (v & 0xffff)) if v & 0x80000000 else d[rva2off(v) + 2:].split(b'\0', 1)[0].decode()
                imports.append((base + ft + 4 * k, dll, nm))
                k += 1
            o += 20
        _PE = dict(d=d, base=base, secs=secs, imports=imports)
    return _PE


def _malloc(s):
    s.mu.reg_write(UC_X86_REG_EAX, s.alloc(max(16, s.arg(0))))


def _memcpy(s):
    d, src, n = s.arg(0), s.arg(1), s.arg(2)
    if n:
        s.mu.mem_write(d, bytes(s.mu.mem_read(src, n)))
    s.mu.reg_write(UC_X86_REG_EAX, d)


def _memset(s):
    d, v, n = s.arg(0), s.arg(1), s.arg(2)
    if n:
        s.mu.mem_write(d, bytes([v & 0xff]) * n)
    s.mu.reg_write(UC_X86_REG_EAX, d)


def _strlen(s):
    s.mu.reg_write(UC_X86_REG_EAX, len(s.cstr(s.arg(0))))


def _zero(s):
    s.mu.reg_write(UC_X86_REG_EAX, 0)


# cdecl imports: the caller cleans the stack, the stub's `ret` is right
IMPORTS = {'malloc': _malloc, '??2@YAPAXI@Z': _malloc, '??_U@YAPAXI@Z': _malloc, 'free': _zero, '??3@YAXPAX@Z': _zero,
           '??_V@YAXPAX@Z': _zero, 'memcpy': _memcpy, 'memmove': _memcpy, 'memset': _memset, 'strlen': _strlen}


# stdcall Windows calls met on the HUD's way: (arguments, answer)
WINAPI = {'EnterCriticalSection': (1, 0), 'LeaveCriticalSection': (1, 0), 'TryEnterCriticalSection': (1, 1),
          'TlsGetValue': (1, 0), 'TlsSetValue': (2, 1), 'GetLastError': (0, 0), 'SetLastError': (1, 0),
          'QueryPerformanceCounter': (1, 1), 'QueryPerformanceFrequency': (1, 1), 'GetTickCount': (0, 0),
          'GetCurrentThreadId': (0, 1), 'Sleep': (1, 0), 'OutputDebugStringA': (1, 0),
          'InitializeCriticalSection': (1, 0), 'DeleteCriticalSection': (1, 0)}


def f32b(v):
    return struct.unpack('<f', struct.pack('<I', v & 0xffffffff))[0]


class Machine:
    def __init__(s, W=480.0, H=270.0):
        P = pe()
        mu = s.mu = Uc(UC_ARCH_X86, UC_MODE_32)
        lo = P['base']
        hi = max(va + max(vs, rs) for n, va, vs, raw, rs in P['secs'])
        mu.mem_map(lo, ((hi - lo) + 0xffff) & ~0xffff)
        mu.mem_write(lo, P['d'][:0x1000])
        for n, va, vs, raw, rs in P['secs']:
            mu.mem_write(va, P['d'][raw:raw + min(rs, max(vs, rs))])
        s.imp_name = {}
        mu.mem_map(TRAMP, 0x10000)
        for i, (iat, dll, nm) in enumerate(P['imports']):
            t = TRAMP + i * 8
            mu.mem_write(t, b'\xc3')
            mu.mem_write(iat, struct.pack('<I', t))
            s.imp_name[t] = (dll, nm)
        mu.mem_map(STACK, STACK_SIZE)
        mu.mem_map(RET_MAGIC, 0x1000)
        mu.mem_map(HEAP, 0x400000)
        mu.mem_map(GAME, 0x100000 * 8)
        mu.mem_map(MANAGER, 0x100000 * 8)
        mu.mem_map(PLAYERS, PLAYER_SIZE * 8)
        mu.mem_map(TEX_STUB, 0x1000)
        mu.mem_map(0x7ffd0000, 0x10000)          # a TEB for fs:[...]
        s.heap = HEAP
        s.w32(GAME_PTR, GAME)
        s.w32(MANAGER_PTR, MANAGER)
        s.wf(SCREEN_W_AT, W)
        s.wf(SCREEN_H_AT, H)
        s.W, s.H = W, H
        s.log = []
        s.frame = {}
        s.text_width = 60
        s.stubs = {SET_FRAME: (s._set_frame, 8), RENDER: (s._render, 12), SET_ANIM: (s._set_frame, 8),
                   STATE_RENDER: (s._state_render, 12)}
        s.sprites = set()
        s.tex_vtable = s.alloc(0x200)
        s.tex_names = {}
        s.tex_slot_args = {}
        for k in range(0x200 // 4):
            mu.mem_write(TEX_STUB + k * 8, b'\xc3')
            s.w32(s.tex_vtable + 4 * k, TEX_STUB + k * 8)
        s.unknown_imports = {}
        s.stub(FONT_DRAW, fn=s._font)
        s.stub(LINE_DRAW, fn=s._line)
        s.stub(LAYER_RENDER, fn=s._layer)
        s.stub(FONT_WIDTH, fn=lambda: s.mu.reg_write(UC_X86_REG_EAX, int(s.text_width)))
        mu.hook_add(UC_HOOK_MEM_UNMAPPED, s._unmapped)
        mu.hook_add(UC_HOOK_CODE, s._code)

    # --- memory
    def w32(s, a, v): s.mu.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def w16(s, a, v): s.mu.mem_write(a, struct.pack('<H', v & 0xffff))
    def w8(s, a, v): s.mu.mem_write(a, bytes([v & 0xff]))
    def wf(s, a, v): s.mu.mem_write(a, struct.pack('<f', v))
    def r32(s, a): return struct.unpack('<I', s.mu.mem_read(a, 4))[0]
    def rf(s, a): return struct.unpack('<f', s.mu.mem_read(a, 4))[0]
    def r8(s, a): return s.mu.mem_read(a, 1)[0]

    def alloc(s, n):
        a = s.heap
        s.heap += (n + 15) & ~15
        return a

    def cstr(s, a):
        out = b''
        while a and len(out) < 200:
            c = s.mu.mem_read(a, 1)[0]
            if not c:
                break
            out += bytes([c])
            a += 1
        return out.decode('latin1')

    def string(s, text):
        a = s.alloc(len(text) + 1)
        s.mu.mem_write(a, text.encode() + b'\0')
        return a

    def stub(s, addr, value=0, fn=None):
        """Make a game function return `value` at once (its own `ret N` is found by disassembly)"""
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        d = bytes(s.mu.mem_read(addr, 0x4000))
        n = 0
        for i in md.disasm(d, addr):
            if i.mnemonic == 'ret':
                n = int(i.op_str, 16) if i.op_str else 0
                break

        def h():
            if fn:
                fn()
            else:
                s.mu.reg_write(UC_X86_REG_EAX, value)
        s.stubs[addr] = (h, n)

    def texture(s, name):
        t = s.alloc(0x100)
        s.w32(t, s.tex_vtable)
        s.tex_names[t] = name
        return t

    def _unmapped(s, mu, access, addr, size, value, user):
        page = addr & ~0xffff
        try:
            mu.mem_map(page, 0x10000)
        except UcError:
            return False
        return True

    def _pop_ret(s, nargs_bytes):
        mu = s.mu
        sp = mu.reg_read(UC_X86_REG_ESP)
        ret = s.r32(sp)
        mu.reg_write(UC_X86_REG_ESP, sp + 4 + nargs_bytes)
        mu.reg_write(UC_X86_REG_EIP, ret)

    def arg(s, i):
        return s.r32(s.mu.reg_read(UC_X86_REG_ESP) + 4 + 4 * i)

    def _code(s, mu, addr, size, user):
        st = s.stubs.get(addr)
        if st:
            st[0]()
            s._pop_ret(st[1])
            return
        if TEX_STUB <= addr < TEX_STUB + 0x1000:
            s._tex(((addr - TEX_STUB) // 8) * 4)
            return
        if TRAMP <= addr < TRAMP + 0x10000:
            dll, nm = s.imp_name.get(addr, ('?', '?'))
            h = IMPORTS.get(nm)
            if nm in WINAPI:                     # stdcall: the callee pops its arguments
                n, v = WINAPI[nm]
                s.mu.reg_write(UC_X86_REG_EAX, v)
                s._pop_ret(4 * n)
            elif h:
                h(s)
            else:
                s.unknown_imports[nm] = s.unknown_imports.get(nm, 0) + 1

    def _vec(s, a):
        return (s.rf(a), s.rf(a + 4))

    def _set_frame(s):
        spr = s.mu.reg_read(UC_X86_REG_ECX)
        s.frame[spr] = s.cstr(s.arg(0))
        s.mu.reg_write(UC_X86_REG_EAX, 1)

    def _render(s):
        spr = s.mu.reg_read(UC_X86_REG_ECX)
        pos = s._vec(s.arg(0))
        # the scale PC code puts on a sprite before rendering: +0x9c / +0xa0 (0x8441ae)
        s.log.append(dict(kind='render', sprite=spr, pos=pos, scale=(s.rf(spr + 0x9c), s.rf(spr + 0xa0)),
                          flip=s.r8(spr + 0x100), anim=s.frame.get(spr, '?')))

    def sprite(s, name=''):
        """A fake anm2 sprite that counts as loaded (+0x109), scale 1"""
        a = s.alloc(0x400)
        s.loaded(a)
        return a

    def loaded(s, a):
        """Make the anm2 at `a` count as loaded, scale 1, with a main animation state whose layers a caller
        walking them itself (the grid 0x849263, the spells 0x84a519) finds: +0x30 the animation (+0x80 layer
        count), +0x34 the state (+0x18 the layers' states, +0x38.. the frame of each), +0x38 the layer data"""
        s.w8(a + 0x109, 1)
        s.wf(a + 0x9c, 1.0)
        s.wf(a + 0xa0, 1.0)
        s.sprites.add(a)
        anim, state, data, layers = s.alloc(0x100), s.alloc(0x100), s.alloc(0x40), s.alloc(0x100)
        s.w32(anim + 0x80, 4)
        s.w32(state + 0x18, layers)
        for i in range(4):
            s.w32(state + 0x38 + 4 * i, i)
            s.w32(data + 4 * i, s.alloc(0x40))
        s.w32(a + 0x30, anim)
        s.w32(a + 0x34, state)
        s.w32(a + 0x38, data)

    def _state_render(s):
        st = s.mu.reg_read(UC_X86_REG_ECX)
        if (st - 0x50) in s.sprites:
            return                      # the overlay state of a sprite of ours: nothing plays on it
        spr = st - 0x30
        pos = s._vec(s.arg(0))
        s.log.append(dict(kind='render', sprite=spr, pos=pos, scale=(s.rf(spr + 0x9c), s.rf(spr + 0xa0)),
                          flip=s.r8(spr + 0x100), anim=s.frame.get(spr, '?')))

    def _font(s):
        mu = s.mu
        x = f32b(mu.reg_read(UC_X86_REG_XMM2) & 0xffffffff)
        y = f32b(mu.reg_read(UC_X86_REG_XMM3) & 0xffffffff)
        sx, sy = f32b(s.arg(1)), f32b(s.arg(2))
        s.log.append(dict(kind='text', sprite=mu.reg_read(UC_X86_REG_ECX), pos=(x, y), scale=(sx, sy), flip=0,
                          anim='text'))

    def _layer(s):
        s.log.append(dict(kind='render', sprite=s.mu.reg_read(UC_X86_REG_ECX), pos=s._vec(s.arg(0)), scale=(1.0, 1.0),
                          flip=0, anim='layer'))

    def _line(s):
        a, b = s._vec(s.arg(0)), s._vec(s.arg(1))
        s.log.append(dict(kind='line', sprite=0, pos=a, scale=(b[0] - a[0], b[1] - a[1]), flip=0, anim='line'))

    def _tex(s, slot):
        tex = s.mu.reg_read(UC_X86_REG_ECX)
        args = [s.arg(i) for i in range(3)]
        quads = []
        for a in args[:2]:
            try:
                quads.append(struct.unpack('<8f', s.mu.mem_read(a, 32)))
            except UcError:
                quads.append(None)
        dst = quads[1] if quads[1] else quads[0]
        pos = (min(dst[0::2]), min(dst[1::2])) if dst else (0, 0)
        size = (max(dst[0::2]) - pos[0], max(dst[1::2]) - pos[1]) if dst else (0, 0)
        s.log.append(dict(kind='image', sprite=tex, slot=slot, quads=quads, pos=pos, scale=size, flip=0,
                          anim=s.tex_names.get(tex, 'texture')))
        s._pop_ret(s.tex_slot_args.get(slot, 12))

    def call(s, fn, this=0, args=(), fargs=(), limit=3000000):
        mu = s.mu
        sp = STACK + STACK_SIZE - 0x2000
        for v in reversed(args):
            sp -= 4
            s.w32(sp, v)
        sp -= 4
        s.w32(sp, RET_MAGIC)
        mu.reg_write(UC_X86_REG_ESP, sp)
        mu.reg_write(UC_X86_REG_EBP, 0)
        mu.reg_write(UC_X86_REG_ECX, this)
        regs = [UC_X86_REG_XMM0, UC_X86_REG_XMM1, UC_X86_REG_XMM2, UC_X86_REG_XMM3]
        for i, v in enumerate(fargs):
            if v is not None:
                mu.reg_write(regs[i], struct.unpack('<I', struct.pack('<f', v))[0])
        mu.emu_start(fn, RET_MAGIC, count=limit)
        if mu.reg_read(UC_X86_REG_EIP) != RET_MAGIC:
            raise RuntimeError('%x did not return (stopped at %x)' % (fn, mu.reg_read(UC_X86_REG_EIP)))
        return mu.reg_read(UC_X86_REG_EAX)
