# -*- coding: utf-8 -*-
"""The spawner desk's data, from the port's own assets: the description
database's names (eid.bin) of collectibles, trinkets, cards and pills in the
seven sets' languages, and the advance of every character of the three
description faces (eid_default, the Korean and the Chinese one) - so the desk
measures text exactly as the game's font measure does (eid.c measure: the sum
of xadvance).

    py -3 tools/spawnerdesk/gen_tables.py      -> tools/spawnerdesk/tables.inc
"""
import io
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, '..', '..', 'port', 'app', 'src', 'main', 'assets')
LANGS = ['en_us', 'ru', 'de', 'spa', 'ja_jp', 'ko_kr', 'zh_cn']          # the sets en ru de es jp kr zh
FACES = ['eid_default.fnt', 'eid_korean_yoonwebdotum10_shadow.fnt', 'eid_cn_alt.fnt']
FACE_OF_SET = [0, 0, 0, 0, 0, 1, 2]
KINDS = [1, 2, 3, 4]


def eid():
    B = io.open(os.path.join(ASSETS, 'eid.bin'), 'rb').read()
    _m, _v, n, sp = struct.unpack_from('<4sIII', B, 0)

    def s(o):
        o += sp
        return B[o:B.index(b'\0', o)].decode('utf-8', 'replace')
    langs = {}
    for i in range(n):
        code = B[16 + i * 12:24 + i * 12].rstrip(b'\0').decode()
        off = struct.unpack_from('<I', B, 24 + i * 12)[0]
        secs = {}
        for k in range(struct.unpack_from('<I', B, off)[0]):
            kind, cnt, eo = struct.unpack_from('<III', B, off + 4 + k * 12)
            secs[kind] = {struct.unpack_from('<I', B, eo + r * 12)[0]: s(struct.unpack_from('<I', B, eo + r * 12 + 4)[0])
                          for r in range(cnt)}
        langs[code] = secs
    return langs


def advances(name):
    d = io.open(os.path.join(ASSETS, name), 'rb').read()
    at, out = 4, {}
    while at < len(d):
        t, n = d[at], struct.unpack_from('<I', d, at + 1)[0]
        b = d[at + 5:at + 5 + n]
        if t == 4:
            for k in range(0, len(b), 20):
                cid, _x, _y, _w, _h, _xo, _yo, xa, _pg, _c = struct.unpack_from('<IHHHHhhhBB', b, k)
                out[cid] = xa
        at += 5 + n
    return out


def c_str(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')


def main():
    L = eid()
    out = ['/* Written by tools/spawnerdesk/gen_tables.py - the desk only. */']
    rows = []
    for kind in KINDS:
        for rid in sorted(L['en_us'].get(kind, {})):
            names = []
            for lg in LANGS:
                v = L.get(lg, {}).get(kind, {}).get(rid)
                names.append(c_str(v) if v else 'NULL')
            rows.append('    { %d, %d, { %s } },' % (kind, rid, ', '.join(names)))
    out += ['static const struct { unsigned kind, id; const char *name[7]; } DESK_EID[] = {'] + rows + ['};', '']
    for i, f in enumerate(FACES):
        adv = advances(f)
        out.append('static const struct { unsigned cp; short adv; } DESK_ADV%d[] = {' % i)
        out += ['    { %d, %d },' % (k, adv[k]) for k in sorted(adv)]
        out.append('};')
    out.append('static const int DESK_FACE_OF_SET[7] = { %s };' % ', '.join(str(x) for x in FACE_OF_SET))
    io.open(os.path.join(HERE, 'tables.inc'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
    print('rows %d' % len(rows))


if __name__ == '__main__':
    main()
