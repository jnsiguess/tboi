# -*- coding: utf-8 -*-
"""The PC game's isaac-ng.exe for the desk: file offset <-> VA, references to an address, disassembly (capstone).

    py -3 tools/pcexe.py str <text>            the string's VA(s)
    py -3 tools/pcexe.py xref <va> [...]       instructions that carry the VA as an immediate or displacement
    py -3 tools/pcexe.py dis <va> [count]      disassembly from the VA
"""
import re, struct, sys
import capstone

EXE = r'Z:/Games/Steam/steamapps/common/The Binding of Isaac Rebirth/isaac-ng.exe'
d = open(EXE, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
machine = struct.unpack_from('<H', d, pe + 4)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
opt = pe + 24
magic = struct.unpack_from('<H', d, opt)[0]
BASE = struct.unpack_from('<I', d, opt + 28)[0] if magic == 0x10b else struct.unpack_from('<Q', d, opt + 24)[0]
SECS = []
for i in range(nsec):
    o = opt + optsz + 40 * i
    name = d[o:o + 8].rstrip(b'\0').decode()
    vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o + 8)
    SECS.append((name, BASE + va, vsize, raw, rsize))
X64 = machine == 0x8664
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64 if X64 else capstone.CS_MODE_32)
MD.detail = True


def off2va(o):
    for n, va, vs, raw, rs in SECS:
        if raw <= o < raw + rs:
            return va + o - raw
    return None


def va2off(v):
    for n, va, vs, raw, rs in SECS:
        if va <= v < va + rs:
            return raw + v - va
    return None


def text():
    return next(s for s in SECS if s[0] == '.text')


def xrefs(targets):
    n, va, vs, raw, rs = text()
    blob = d[raw:raw + rs]
    tb = {struct.pack('<I', t): t for t in targets}
    out = []
    for k, t in tb.items():
        for m in re.finditer(re.escape(k), blob):
            out.append((va + m.start(), t))
    return sorted(out)


def dis(v, count=40):
    o = va2off(v)
    for i in MD.disasm(d[o:o + count * 15], v):
        if count <= 0:
            break
        print('%08x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
        count -= 1


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'info':
        print('x64' if X64 else 'x86', hex(BASE), SECS)
    elif cmd == 'str':
        for m in re.finditer(re.escape(sys.argv[2].encode()) + b'\0', d):
            print(hex(off2va(m.start())), d[m.start() - 4:m.end() + 4])
    elif cmd == 'xref':
        for at, t in xrefs([int(a, 16) for a in sys.argv[2:]]):
            print('%08x -> %08x (operand bytes at)' % (at, t))
    elif cmd == 'dis':
        dis(int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 40)


def callers(target):
    n, va, vs, raw, rs = text()
    blob = d[raw:raw + rs]
    out = []
    i = 0
    while True:
        i = blob.find(b'\xe8', i)
        if i < 0:
            break
        if i + 5 <= len(blob):
            rel = struct.unpack_from('<i', blob, i + 1)[0]
            if va + i + 5 + rel == target:
                out.append(va + i)
        i += 1
    return out


if __name__ == '__main__' and sys.argv[1] == 'callers':
    for t in sys.argv[2:]:
        print(t, ' '.join(hex(c) for c in callers(int(t, 16))))
