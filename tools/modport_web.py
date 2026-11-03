# -*- coding: utf-8 -*-
"""modport_web: the web converter as ONE html file, so it opens from a disk, a chat
attachment or any free static host (GitHub Pages and the like) with nothing else beside it.

    py -3 tools/modport_web.py

Reads other stuff/usermods/modport-web/src/: page.html (the page, with three markers),
fflate.js (the zip library, https://github.com/101arrowz/fflate, MIT), modport.js (the
rules, a copy of tools/modport.py in JavaScript) and gameref.bin (tools/modport_ref.py),
and writes other stuff/usermods/modport-web/modport.html. Rebuild after touching any of
them; rebuild gameref.bin first when the game's resources changed.
"""
import base64
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, 'other stuff', 'usermods', 'modport-web')
SRC = os.path.join(HERE, 'src')
OUT = os.path.join(HERE, 'modport.html')


def read(name, mode='r'):
    with io.open(os.path.join(SRC, name), mode, **({'encoding': 'utf-8'} if mode == 'r' else {})) as f:
        return f.read()


def script(js):
    return '<script>\n' + js.replace('</script', '<\\/script') + '\n</script>'


def main():
    page = read('page.html')
    ref = base64.b64encode(read('gameref.bin', 'rb')).decode('ascii')
    for marker, body in (('<!--FFLATE-->', script(read('fflate.js'))),
                         ('<!--MODPORT-->', script(read('modport.js'))),
                         ('<!--GAMEREF-->', '<script>window.GAMEREF_B64 = "%s";</script>' % ref)):
        if marker not in page:
            raise SystemExit('page.html has no %s marker' % marker)
        page = page.replace(marker, body)
    with io.open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(page)
    print('%s: %d bytes' % (OUT, os.path.getsize(OUT)))


if __name__ == '__main__':
    main()
