# -*- coding: utf-8 -*-
"""Lay out the game's own files from the IPA, so a fresh tree builds.

The port carries none of the game: no image, no resources. Both come from the
iOS release 1.4 the builder owns (TBOI Rebirth_v1.4.ipa, its app folder is
"Payload/The Binding of Isaac Rebirth.app/"):

  work/isaac.arm64                 the app's executable, byte for byte - the
                                   Mach-O image every address in the code and
                                   the notes is an offset into
  port/app/src/main/assets/isaac.arm64
                                   the same, for the APK (loader.c maps it)
  work/app/                        every other file of the app folder - what
                                   tools/pack.py packs into resources.pak

  TBOI Rebirth_v1.4.ipa            the IPA itself, in the project root - the
                                   build checks the pak against it as vanilla
                                   (port_additions.py pak_check)

Left out of work/app: the executable itself and Frameworks/*.dylib (dylibs
the IPA was sideloaded with; the port never loads them).

One game file the pak carries in the port's version (port_additions.py
IN_PAK_TRANSITION): rebirth-resources/data/stringtable.sta. Its port version
travels in work/app-port/ and is laid over the IPA's here.

The image is checked against the one the port was made for (size and
SHA-256); another build of the game has other addresses, and nothing in the
port would land where it should.

    py -3 tools/setup_from_ipa.py "path/to/TBOI Rebirth_v1.4.ipa"
"""
import hashlib
import os
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IPA_NAME = 'TBOI Rebirth_v1.4.ipa'
APP_IN_IPA = 'Payload/The Binding of Isaac Rebirth.app/'
EXECUTABLE = 'The Binding of Isaac Rebirth'
IMAGE_SIZE = 11865296
PORT_VERSIONS = os.path.join(ROOT, 'work', 'app-port')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    z = zipfile.ZipFile(sys.argv[1])
    image = z.read(APP_IN_IPA + EXECUTABLE)
    if len(image) != IMAGE_SIZE or image[:4] != bytes.fromhex('cffaedfe'):
        sys.exit('this is not the 1.4 image the port was made for (%d bytes)' % len(image))
    digest = hashlib.sha256(image).hexdigest()
    want = open(os.path.join(ROOT, 'tools', 'image.sha256')).read().split()[0]
    if digest != want:
        sys.exit('the image differs from the one the port was made for:\n  %s\n  %s' % (digest, want))
    ipa = os.path.join(ROOT, IPA_NAME)
    if not os.path.exists(ipa) or not os.path.samefile(sys.argv[1], ipa):
        shutil.copyfile(sys.argv[1], ipa)
    for path in (os.path.join(ROOT, 'work', 'isaac.arm64'),
                 os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets', 'isaac.arm64')):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'wb').write(image)
    app = os.path.join(ROOT, 'work', 'app')
    n = 0
    for info in z.infolist():
        name = info.filename
        if not name.startswith(APP_IN_IPA) or name.endswith('/'):
            continue
        rel = name[len(APP_IN_IPA):]
        if rel == EXECUTABLE or rel.startswith('Frameworks/'):
            continue
        out = os.path.join(app, *rel.split('/'))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with z.open(info) as src, open(out, 'wb') as dst:
            shutil.copyfileobj(src, dst)
        n += 1
    laid = 0
    for dp, _, fn in os.walk(PORT_VERSIONS):
        for f in fn:
            src = os.path.join(dp, f)
            shutil.copyfile(src, os.path.join(app, os.path.relpath(src, PORT_VERSIONS)))
            laid += 1
    print('image %d bytes; work/app: %d files from the IPA, %d in the port\'s version' % (len(image), n, laid))
    print('next: py -3 tools/build.py')


if __name__ == '__main__':
    main()
