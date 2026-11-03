# -*- coding: utf-8 -*-
"""One zip of everything the port itself is, for another developer.

Unlike zipsrc.py and zipfull.py (archives of the working tree for ourselves),
this one carries the port and nothing of the game, and nothing of this machine:

  taken     the Android project (Gradle files, the manifest, res/, the native
            layer, the Java side, the port's own assets), the tools, the port's
            versions of the game files it changes (work/port-edit, and the one
            the pak carries, under work/app-port), the owner's lettering
            (menu-labels), the copy of the EID mod the description database is
            built from, the two inputs under "other stuff" the tools read, the
            notes, and MAP.md
  left out  the game: its image (isaac.arm64) and resources (work/app,
            resources.pak, the IPA) - tools/setup_from_ipa.py lays them out
            from the builder's own IPA; the tables of Repentance+ for PC the
            port carries for mods made for it (assets/port_patch/port-version-ref,
            out of the index too: the build goes without them, see MAP.md);
            anything built (APKs, the desks' .exe,
            .o, .pdb, Gradle and CMake output); reference copies (*.before-*,
            *.orig); local.properties (this machine's SDK path); reports from
            devices

The zip is checked as it is written: no file of the game's image or pak, no
reference copy, no machine path in a text file.

--english makes the English one: the documents are the English ones in docs-en/
(MAP.md, CLAUDE.md, TESTS.md) instead of the Russian notes, which stay out with
the analyses (разбор/); files under docs-en/overlay/ take the place of the
tree's files of the same path (the menu lettering's instructions, Russian in the
tree for the owner). The game's own translations stay - the Russian names and
descriptions are the port's Russian, not its documentation.

    py -3 tools/share_zip.py            -> isaac-port-<version>.zip in the root
    py -3 tools/share_zip.py --english  -> isaac-port-<version>-en.zip
"""
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INSIDE = 'isaac-port'

DOCUMENTS = ['MAP.md', 'CLAUDE.md', 'ПЕРЕДАЧА.md', 'ТЕСТЫ.md', 'ОТЧЁТ-ПРОВЕРКИ.md']
TREES = [
    'port',
    'tools',
    'work/port-edit',
    'menu-labels',
    'eid-mod',
    'other stuff/arabic-eid',
    'other stuff/console-strap',
    'разбор',
]
# the one game file the pak carries in the port's version (port_additions.IN_PAK_TRANSITION)
PORT_VERSIONS = {'work/app/rebirth-resources/data/stringtable.sta':
                 'work/app-port/rebirth-resources/data/stringtable.sta'}

SKIP_DIRS = {'build', '.cxx', '.gradle', '.idea', 'outputs', '__pycache__', '.vs'}
SKIP_NAMES = {'local.properties', 'isaac.arm64', 'resources.pak', 'Thumbs.db', '.DS_Store'}
SKIP_EXTS = {'.apk', '.pak', '.ipa', '.so', '.o', '.obj', '.exe', '.pdb', '.ilk', '.pyc', '.zip', '.log'}
# Repentance+ (PC) tables: the game's files, laid in by port_patch.py from the builder's own copy
OTHER_GAME = 'port/app/src/main/assets/port_patch/port-version-ref/'
PATCH_INDEX = 'port/app/src/main/assets/port_patch/index.txt'
REFERENCE = re.compile(r'\.before-|\.orig$|\.bak$|\.broken|\.do-')
CYRILLIC = re.compile('[Ѐ-ӿ]')
QUOTED = re.compile(r'«[^»\n]*»')
LEFT_OUT = []
ENGLISH = '--english' in sys.argv
EN = os.path.join(ROOT, 'docs-en')
EN_DOCUMENTS = ['MAP.md', 'CLAUDE.md', 'TESTS.md']
EN_OVERLAY = os.path.join(EN, 'overlay')
STORE_EXTS = {'.png', '.jpg', '.jpeg', '.ogg', '.gif', '.ttf', '.webp'}
TEXT_EXTS = {'.c', '.h', '.S', '.java', '.py', '.gradle', '.xml', '.txt', '.md', '.json', '.properties',
             '.cmake', '.ini', '.bat', '.sh', '.anm2', '.fnt', '.pro'}
# a user's home, or this tree's absolute place (any slashes) - the port finds its root itself
MACHINE = re.compile(r'[A-Za-z]:[\\/]+Users[\\/]|' +
                     r'[\\/]+'.join(re.escape(p) for p in re.split(r'[\\/]+', ROOT) if p), re.I)


def is_note(rel):
    return not ENGLISH and (rel in DOCUMENTS or rel.startswith('разбор/'))


def wanted(rel):
    parts = rel.split('/')
    name = parts[-1]
    # names are English everywhere but in the notes (the owner's rule)
    if CYRILLIC.search(rel) and not is_note(rel):
        LEFT_OUT.append(rel)
        return False
    if rel.startswith(OTHER_GAME):
        return False
    if any(p in SKIP_DIRS for p in parts[:-1]):
        return False
    if name in SKIP_NAMES or REFERENCE.search(name):
        return False
    if os.path.splitext(name)[1].lower() in SKIP_EXTS:
        return False
    return True


def collect():
    files = []
    if ENGLISH:
        files += [(os.path.join(EN, d), d) for d in EN_DOCUMENTS]
    else:
        files += [(os.path.join(ROOT, d), d) for d in DOCUMENTS if os.path.exists(os.path.join(ROOT, d))]
    for tree in TREES:
        if ENGLISH and tree == 'разбор':
            continue
        for base, dirs, names in os.walk(os.path.join(ROOT, tree)):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
            for n in sorted(names):
                full = os.path.join(base, n)
                rel = os.path.relpath(full, ROOT).replace(os.sep, '/')
                if wanted(rel):
                    files.append((full, rel))
    for src, rel in PORT_VERSIONS.items():
        files.append((os.path.join(ROOT, src), rel))
    if ENGLISH:
        over = {}
        for dp, _, fn in os.walk(EN_OVERLAY):
            for n in fn:
                full = os.path.join(dp, n)
                over[os.path.relpath(full, EN_OVERLAY).replace(os.sep, '/')] = full
        have = {rel for _, rel in files}
        lost = sorted(set(over) - have)
        if lost:
            sys.exit('docs-en/overlay has files the tree has not: %s' % ', '.join(lost))
        files = [(over.get(rel, full), rel) for full, rel in files]
    return files


def version():
    text = open(os.path.join(ROOT, 'port', 'app', 'build.gradle'), encoding='utf-8').read()
    return re.search(r'versionName\s+"([^"]+)"', text).group(1)


def main():
    files = collect()
    machine = []
    for full, rel in files:
        # the notes are this machine's diary and say so; the code must not depend on it
        if is_note(rel) or (rel.endswith('.md') and not ENGLISH):
            continue
        if os.path.splitext(rel)[1] in TEXT_EXTS or rel.endswith(('gradlew',)):
            try:
                if MACHINE.search(open(full, encoding='utf-8', errors='replace').read()):
                    machine.append(rel)
            except OSError:
                pass
    if machine and '--allow-paths' not in sys.argv:
        print('machine paths in %d files (pass --allow-paths to pack anyway):' % len(machine))
        for m in machine:
            print('   ', m)
        sys.exit(1)
    if ENGLISH:
        # the English documents and overlay must be English all through, but for a Russian
        # label quoted as the thing to draw («РЕЖИМ» in the lettering's instructions)
        said = [rel for full, rel in files if (rel in EN_DOCUMENTS or full.startswith(EN_OVERLAY))
                and CYRILLIC.search(QUOTED.sub('', open(full, encoding='utf-8', errors='replace').read()))]
        if said and '--allow-cyrillic' not in sys.argv:
            sys.exit('Cyrillic in the English documents (--allow-cyrillic to pack anyway): %s' % ', '.join(said))
    name = os.path.join(ROOT, 'isaac-port-%s%s.zip' % (version(), '-en' if ENGLISH else ''))
    total = 0
    with zipfile.ZipFile(name, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for full, rel in files:
            kind = zipfile.ZIP_STORED if os.path.splitext(rel)[1].lower() in STORE_EXTS else zipfile.ZIP_DEFLATED
            if rel == PATCH_INDEX:
                lines = open(full, encoding='utf-8').read().splitlines(True)
                kept = ''.join(l for l in lines if '\tport-version-ref/' not in l)
                z.writestr(INSIDE + '/' + rel, kept.encode('utf-8'), compress_type=kind)
            else:
                z.write(full, INSIDE + '/' + rel, compress_type=kind)
            total += os.path.getsize(full)
    print('%s: %d files, %.1f MB of %.1f MB' % (name, len(files), os.path.getsize(name) / 1048576.0,
                                                total / 1048576.0))
    if LEFT_OUT:
        print('left out for a Cyrillic name (not a note): %d' % len(LEFT_OUT))
        for rel in LEFT_OUT:
            print('   ', rel)


if __name__ == '__main__':
    main()
