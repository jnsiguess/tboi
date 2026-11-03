# -*- coding: utf-8 -*-
"""Build one zip that carries the whole port: sources and every resource.

The difference from zipsrc.py is the resources. That one takes the menu alone
and stays at 16 MB; this one takes work/ whole, so the archive is enough on its
own: unpack it, run tools/pack.py, and Gradle has everything it needs.

What is left out is only what is made again from what is inside:

    port/.../assets/resources.pak   tools/pack.py builds it out of work/app
    port/app/build, .cxx, .gradle   Gradle's own leavings
    the ipa, the ready apk, zips    the first is the owner's purchase and the
                                    source of work/app, the rest are output

Compression is chosen per file. Sound and video and pictures already carry
their own, and deflating them again costs minutes and saves nothing; text
(xml, anm2, wav, code) is squeezed the usual way.

    py -3 tools/zipfull.py
"""
import os
import sys
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = os.path.join(ROOT, 'isaac-port-full.zip')
INSIDE = 'isaac-port'

DOCUMENTS = ['CLAUDE.md', 'ПЕРЕДАЧА.md', 'ОТЧЁТ-ПРОВЕРКИ.md']

# loose working pictures at the root: the menu lettering was drawn against them
LOOSE_EXTS = {'.png', '.json'}

# folders of the project that are taken whole, minus what is built again
SKIP_DIRS = {'build', '.cxx', '.gradle', '.idea', 'outputs', 'assets'}
SKIP_EXTS = {'.apk', '.pak', '.zip', '.ipa', '.o'}

# already compressed: storing them is both faster and no larger
STORE_EXTS = {'.png', '.jpg', '.jpeg', '.ogg', '.ogv', '.mp3', '.mp4', '.gif',
              '.webp', '.zip', '.gz', '.car'}

README = u'''How to build this
=================

    py -3 tools/pack.py                    resources.pak out of work/app
    cd port
    JAVA_HOME=<Android Studio jbr> ./gradlew assembleRelease

The archive holds no resources.pak on purpose: it is a gigabyte and a third
that pack.py makes out of work/app in a couple of minutes.

The documents to read first are CLAUDE.md (the rules that always hold) and
ПЕРЕДАЧА.md (the state of the work and what is still open).
'''


def kind(path):
    return zipfile.ZIP_STORED if os.path.splitext(path)[1].lower() in STORE_EXTS \
        else zipfile.ZIP_DEFLATED


def wanted(rel):
    parts = rel.replace('\\', '/').split('/')
    if any(p in SKIP_DIRS for p in parts):
        return False
    return os.path.splitext(rel)[1].lower() not in SKIP_EXTS


def take_tree(files, folder, filtered):
    base_dir = os.path.join(ROOT, folder)
    if not os.path.isdir(base_dir):
        return
    for base, dirs, names in os.walk(base_dir):
        if filtered:
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for n in names:
            p = os.path.join(base, n)
            rel = os.path.relpath(p, ROOT)
            if not filtered or wanted(rel):
                files.append((p, rel))


def collect():
    files = []

    for d in DOCUMENTS:
        p = os.path.join(ROOT, d)
        if os.path.exists(p):
            files.append((p, d))

    for n in sorted(os.listdir(ROOT)):
        p = os.path.join(ROOT, n)
        if os.path.isfile(p) and os.path.splitext(n)[1].lower() in LOOSE_EXTS:
            files.append((p, n))

    take_tree(files, 'port', True)
    take_tree(files, 'tools', False)
    take_tree(files, 'menu-labels', False)
    take_tree(files, 'work', False)          # the resources, all of them
    return files


def build():
    files = collect()
    raw = sum(os.path.getsize(p) for p, _ in files)
    print('to pack: %d files, %.2f GB' % (len(files), raw / float(2 ** 30)))

    started = time.time()
    done = 0
    with zipfile.ZipFile(NAME, 'w', zipfile.ZIP_DEFLATED, allowZip64=True,
                         compresslevel=6) as z:
        z.writestr(INSIDE + '/HOW-TO-BUILD.txt', README.encode('utf-8'))
        for i, (full, rel) in enumerate(files):
            z.write(full, INSIDE + '/' + rel.replace('\\', '/'),
                    compress_type=kind(rel))
            done += os.path.getsize(full)
            if i % 2000 == 0:
                sys.stdout.write('  %5.1f%%\n' % (100.0 * done / raw))
                sys.stdout.flush()

    size = os.path.getsize(NAME)
    print('done: %d files, %.2f GB, in %d s'
          % (len(files) + 1, size / float(2 ** 30), time.time() - started))


if __name__ == '__main__':
    build()
