# -*- coding: utf-8 -*-
"""THE PORT'S OWN COPIES OF THE GAME'S MENU FONTS (the owner, 05.10.2026).

The port's menu pages (Tweaks, MODS, a mod's page and its details, the
conflict popup) print in the game's teammeatfont16bold, 20bold and 10. Asked
by their own names, they are what the mods make of them: Reflashed brings all
five teammeatfont* as ASCII capitals only (95 glyphs, no Cyrillic, no
umlauts), and every Russian, German or Spanish line of the port lost its
letters. The game's own text keeps whatever the mods give it, as on the PC;
the port's text is the port's.

So the port carries copies of the game's three under names no mod brings -
port_menufont16bold, port_menufont20bold, port_menufont10 - the .fnt with its
page renamed to the copy's own sheet, so the sheet is the copy's too (a name
the game has loaded already would come from its texture cache, whoever's it
was). eid.c brings them into the resource table and draws with them.

    py -3 tools/menu_fonts.py          # -> port/app/src/main/assets/port_menufont*.fnt, *_0.png
"""
import os
import shutil
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'font')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
FACES = [('teammeatfont16bold', 'port_menufont16bold'), ('teammeatfont20bold', 'port_menufont20bold'),
         ('teammeatfont10', 'port_menufont10')]
# what the page languages need, and the game's faces have (the check that this is the game's file, not a mod's)
MUST = 'AZaz09' + 'АЯаяЁё' + 'ÄÖÜäöüß' + 'ÑñÁÉÍÓÚáéíóú¿¡' + '•…'


def blocks(d):
    at = 4
    while at + 5 <= len(d):
        kind, size = d[at], struct.unpack_from('<i', d, at + 1)[0]
        yield kind, d[at + 5:at + 5 + size]
        at += 5 + size


def chars(d):
    for kind, body in blocks(d):
        if kind == 4:
            return {struct.unpack_from('<I', body, i)[0] for i in range(0, len(body), 20)}
    return set()


def main():
    for game, port in FACES:
        d = open(os.path.join(GAME, game + '.fnt'), 'rb').read()
        assert d[:4] == b'BMF\x03', game
        have = chars(d)
        missing = [c for c in MUST if ord(c) not in have]
        assert not missing, (game, missing)
        pages = [b for k, b in blocks(d) if k == 3]
        assert len(pages) == 1 and pages[0].count(b'\0') == 1, (game, pages)
        page = (port + '_0.png').encode() + b'\0'
        out = bytearray(d[:4])
        for kind, body in blocks(d):
            if kind == 3:
                body = page
            out += bytes([kind]) + struct.pack('<i', len(body)) + body
        open(os.path.join(ASSETS, port + '.fnt'), 'wb').write(bytes(out))
        shutil.copyfile(os.path.join(GAME, game + '_0.png'), os.path.join(ASSETS, port + '_0.png'))
        print('%s -> %s.fnt, %s_0.png (%d glyphs)' % (game, port, port, len(have)))


if __name__ == '__main__':
    main()
