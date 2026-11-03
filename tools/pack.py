# -*- coding: utf-8 -*-
"""The game's resources as one blob in the APK assets.

The format is our own rather than tar, and deliberately so: the game's
resources have long paths with brackets and spaces, while tar puts the name
into a hundred bytes and moves everything longer into GNU/PAX extensions.
Reading that in Java is an extra source of mistakes. Everything here is
straightforward, byte order is network order (which is what DataInputStream
reads):

    "ISAACPK1"          eight bytes of the tag
    int32  how many files
    then per file:
        int32  length of the path in UTF-8
        bytes  path from the resource root, separator /
        int64  length of the file
        bytes  the file itself

Backup copies (*.orig, *-REFERENCE*) and file manager litter we do not put in.

    py -3 tools/pack.py [set ...]
"""
import hashlib
import os
import struct
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT, 'work', 'app')
ASSETS = os.path.join(PROJECT, 'port', 'app', 'src', 'main', 'assets')
ASSETS_PAK = os.path.join(ASSETS, 'resources.pak')
SKIP = ('.orig', '.before-move', 'Thumbs.db', '.DS_Store')
# Our reference copies are put next to the edited file, appending a tail to the
# full name: `optionsextra.png.before-labels`, `optionsmenu.anm2.before-turn`. We
# catch them by pattern rather than by enumeration: tails get coined for every
# edit, and the list went stale faster than it grew. They rode into a
# distributed build once — 26 files for 0.9 MB.
#
# The pattern used to be the KNOWN resource extensions, and that let two
# shaders through: `hq4x.fs.before-filter` and its vertex half have extensions
# nobody had thought to list, so they went on riding into the pak (measured
# 16.09.2026). The tail is what marks a reference copy, so the tail is what we
# look for - a rule that cannot go stale when a new kind of file is edited.
RESOURCE_EXTS = ('.png.', '.anm2.', '.xml.', '.fnt.', '.wav.', '.ogg.', '.lua.')
BACKUP_MARK = '.before-'


def wanted(name):
    if name.endswith(SKIP):
        return False
    if BACKUP_MARK in name:
        return False
    if any(e in name for e in RESOURCE_EXTS):
        return False
    return 'REFERENCE' not in name



def whole():
    """Is the pak all there? Its table walked to the end, as pack.c walks it: every
    entry in place, the last one ending exactly at the end of the file. A pak cut
    short passed the age check - newer than everything, and a seventh of it."""
    size = os.path.getsize(ASSETS_PAK)
    with open(ASSETS_PAK, 'rb') as f:
        head = f.read(12)
        if len(head) < 12 or head[:8] not in (b'ISAACPK1', b'ISAACPK2'):
            return 'no tag'
        count = struct.unpack('>i', head[8:12])[0]
        pos = 12
        for i in range(count):
            f.seek(pos)
            b = f.read(4)
            if len(b) < 4:
                return 'ends at entry %d of %d' % (i, count)
            n = struct.unpack('>i', b)[0]
            pos += 4 + n
            f.seek(pos)
            b = f.read(8)
            if len(b) < 8:
                return 'ends at entry %d of %d' % (i, count)
            length = struct.unpack('>q', b)[0]
            pos += 8
            if length == -1:
                pos += 16
            else:
                pos += length
            if pos > size:
                return 'entry %d of %d runs past the end' % (i, count)
    if pos != size:
        return '%d bytes after the last entry' % (size - pos)
    return None


def stale():
    """Is the packed file older than anything it was packed from?

    Asked because of the hour lost on 16.09.2026: a resource was edited, the
    APK was rebuilt, and the pak was not - so the build carried the OLD picture
    and the owner spent a round of testing on a change that was never in it.
    Gradle cannot see this: to it the pak is one opaque asset that did not
    change. So it is asked here, out loud, before every build.
    """
    if not os.path.isfile(ASSETS_PAK):
        return 'the pak has not been built at all'
    broken = whole()
    if broken:
        return 'the pak is broken (%s) - repack before building' % broken
    packed = os.path.getmtime(ASSETS_PAK)
    newest, where = 0.0, None
    for base, _, names in os.walk(ROOT):
        for n in names:
            if not wanted(n):
                continue
            t = os.path.getmtime(os.path.join(base, n))
            if t > newest:
                newest, where = t, os.path.relpath(os.path.join(base, n), ROOT)
    if newest > packed:
        return 'the pak is older than %s - repack before building' % where
    return None


def collect_list():
    """All the resources in one list: path from the root and path on disk."""
    files = []
    for base, _, names in os.walk(ROOT):
        for name in names:
            if not wanted(name):
                continue
            full = os.path.join(base, name)
            path = os.path.relpath(full, ROOT).replace(os.sep, '/')
            files.append((path, full))
    files.sort()
    return files


def file_hash(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def pack_all():
    """One blob for all the sets, with repeats skipped.

    The DLC sets repeat each other almost by half: measurement gave 9244 files
    and 1214 MB of extra weight, 48.7% — the same videos, music and sprites
    lie there three and four times over. Inside a single set there are almost
    no repeats, which is why the blobs were merged into one: only that way is
    a duplicate visible.

    The blob's name is nowhere significant — `shim_pack_open` walks the assets
    by extension, and the set is the first link of every path anyway.
    """
    files = collect_list()
    # written aside and put in place only when whole: a build stopped half way
    # left a pak of 167 MB out of 1300, newer than every resource, and --check
    # called it up to date (25.09.2026)
    out_path = ASSETS_PAK + '.tmp'
    places = {}                     # hash -> (offset of the data, length)
    total = repeats = saved = 0
    with open(out_path, 'wb') as out:
        out.write(b'ISAACPK2')
        out.write(struct.pack('>i', len(files)))
        pos = 12
        for path, full in files:
            name = path.encode('utf-8')
            size = os.path.getsize(full)
            h = file_hash(full)
            out.write(struct.pack('>i', len(name)))
            out.write(name)
            pos += 4 + len(name)
            found = places.get(h)
            if found is not None and found[1] == size:
                off, ln = found
                out.write(struct.pack('>qqq', -1, off, ln))
                pos += 24
                repeats += 1
                saved += size
                continue
            out.write(struct.pack('>q', size))
            pos += 8
            places[h] = (pos, size)
            with open(full, 'rb') as src:
                while True:
                    chunk = src.read(1 << 20)
                    if not chunk:
                        break
                    out.write(chunk)
            pos += size
            total += size
    os.replace(out_path, ASSETS_PAK)
    blob = os.path.getsize(ASSETS_PAK)
    print('  files %d, of them repeats %d' % (len(files), repeats))
    print('  data %.1f MB, saved on repeats %.1f MB'
          % (total / 1048576.0, saved / 1048576.0))
    print('  blob %.1f MB' % (blob / 1048576.0))
    return blob


def remove_old():
    """Per-set blobs are no longer needed: everything lies in a single one."""
    for n in os.listdir(ASSETS):
        if n.endswith('.pak') and n != 'resources.pak':
            os.remove(os.path.join(ASSETS, n))
            print('  removed the previous blob %s' % n)


if __name__ == '__main__':
    if '--check' in sys.argv:
        why = stale()
        print(why if why else 'the pak is up to date')
        sys.exit(1 if why else 0)
    remove_old()
    pack_all()
