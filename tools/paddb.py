# -*- coding: utf-8 -*-
"""THE GAMEPAD MAPPING DATABASE FOR ANDROID (the owner's decision, 02.10.2026).

The port names a pad's buttons and axes the way SDL does on Android, and takes
the pad's layout from the community database SDL itself reads:

  1. SDL's own built-in Android mappings - the SDL_PLATFORM_ANDROID block of
     src/joystick/SDL_gamepad_db.h (libsdl-org/SDL);
  2. gamecontrollerdb.txt (mdqinc/SDL_GameControllerDB), its platform:Android
     lines, loaded after them - as SDL loads a mapping file after its built-ins
     at a higher priority, so a line of the same GUID replaces the earlier one
     IN PLACE (SDL_gamepad.c SDL_PrivateAddMappingForGUID).
     THE PORT DIFFERS HERE (the owner, 02.10.2026: the fewest problems for
     players): SDL's own lines win. A community line of a vendor:product (or
     GUID) SDL has a built-in line for is dropped; the rest go into a section
     of their own (@db) that PadDb consults only when SDL's section (@sdl)
     has nothing for the pad's vendor:product.

Both are zlib-licensed; the licence rides along as assets/paddb-LICENSE.txt.
The sources are vendored in tools/paddb/ (SOURCE.txt names the commits), so a
rebuild is deterministic; --fetch refreshes them from GitHub first.

    py -3 tools/paddb.py            # tools/paddb/* -> assets/paddb.txt + paddb-LICENSE.txt
    py -3 tools/paddb.py --check    # exit 1 if the asset is not what the sources make
    py -3 tools/paddb.py --fetch    # refresh tools/paddb/ from GitHub, then build
    py -3 tools/paddb.py --known    # the community lines dropped for pads SDL has its own line for

What is kept, and why (evidence in the research notes of 02.10.2026, SDL pad database):
  - only GUIDs that start with bus 0x0005 (05000000...): every SDL since
    277b033e (2022, and the same layout since 9e651b69, 2018) builds an Android
    GUID as SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_BLUETOOTH, ...)
    (src/joystick/android/SDL_sysjoystick.c Android_AddJoystick). The older
    lines carry the first 16 bytes of the device's descriptor or name as the
    GUID (SDL before 2018) - no SDL today can produce them, they never match;
  - hint: fields are settled here as SDL3 settles them with no hint set
    (SDL_gamepad.c SDL_PrivateAddGamepadMapping): a negated label hint means
    "already positional" and is dropped, a plain one converts a/b/x/y to
    positional, any other hint is evaluated with its default;
  - sdk>=: / sdk<=: / crc: fields stay - they depend on the phone and the
    device, the app settles them (PadDb.java);
  - platform:Android is dropped (every line is Android's).
Linux lines are NOT used: SDL numbers a Linux pad by its evdev codes, an
Android pad by Android key codes and Android axis ids after the system's key
layout files - the same index means a different control (see the notes).
"""
import os
import re
import sys
import json
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'tools', 'paddb')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
OUT = os.path.join(ASSETS, 'paddb.txt')
OUT_LICENSE = os.path.join(ASSETS, 'paddb-LICENSE.txt')

DB = os.path.join(SRC, 'gamecontrollerdb.txt')
HEADER = os.path.join(SRC, 'SDL_gamepad_db.h')
LICENSE = os.path.join(SRC, 'LICENSE.txt')
SOURCE = os.path.join(SRC, 'SOURCE.txt')

# SDL_joystick.c initial_gamecube_devices: SDL3 appends a GameCube label hint to their lines
GAMECUBE = {(0x0079, 0x1843), (0x0079, 0x1844), (0x0079, 0x1846), (0x057e, 0x0337), (0x057e, 0x2073),
            (0x05e3, 0x0681), (0x0926, 0x8888), (0x0e6f, 0x0185), (0x1a34, 0xf705), (0x20d6, 0xa711)}


def builtin_android_lines(text):
    """The string literals of the SDL_PLATFORM_ANDROID block of SDL_gamepad_db.h, in order."""
    lines = text.splitlines()
    out, inside = [], False
    for ln in lines:
        s = ln.strip()
        if s.startswith('#ifdef SDL_PLATFORM_ANDROID'):
            inside = True
            continue
        if inside and s.startswith('#'):
            break
        if inside:
            m = re.match(r'"([^"]*)"\s*,', s)
            if m:
                out.append(m.group(1))
    if not out:
        sys.exit('no SDL_PLATFORM_ANDROID block in %s' % HEADER)
    return out


def file_android_lines(text):
    out = []
    for ln in text.splitlines():
        ln = ln.strip()
        if not ln or ln.startswith('#'):
            continue
        m = re.search(r'platform:([^,]*),', ln)
        if m and m.group(1).lower() == 'android':        # SDL_PrivateIsGamepadPlatformMatch: strncasecmp
            out.append(ln)
    return out


def vidpid(guid):
    """SDL_GetJoystickGUIDInfo: the standard form (words 3 and 5 nought) has VID/PID, else none."""
    b = bytes.fromhex(guid)
    w = [b[i] | b[i + 1] << 8 for i in range(0, 16, 2)]
    if w[0] < 0x20 and w[3] == 0 and w[5] == 0:
        return w[2], w[4]
    return 0, 0


def positional_baxy(m):
    """SDL_ConvertMappingToPositionalBAXY: a<->b, x<->y."""
    swap = {',a:': ',b:', ',b:': ',a:', ',x:': ',y:', ',y:': ',x:'}
    return re.sub(r',[abxy]:', lambda k: swap[k.group(0)], m)


def positional_axby(m):
    """SDL_ConvertMappingToPositionalAXBY: b<->x."""
    swap = {',b:': ',x:', ',x:': ',b:'}
    return re.sub(r',[bx]:', lambda k: swap[k.group(0)], m)


def settle(line):
    """One mapping line as SDL3 takes it in, with no hint set; None when SDL3 would not add it."""
    guid = line.split(',', 1)[0].lower()
    if len(guid) != 32 or not re.fullmatch(r'[0-9a-f]{32}', guid):
        return None, 'bad guid'
    if not guid.startswith('0500'):
        return None, 'legacy'
    vid, pid = vidpid(guid)
    if (vid, pid) in GAMECUBE and 'SDL_GAMECONTROLLER_USE_GAMECUBE_LABELS' not in line:
        line = line.rstrip(',') + ',hint:SDL_GAMECONTROLLER_USE_GAMECUBE_LABELS:=1,'
    m = re.search(r'hint:(!?)([^,:]*)(?::=(-?\d+))?', line)
    if m:
        negate, hint, default = m.group(1) == '!', m.group(2), int(m.group(3) or 0)
        if hint == 'SDL_GAMECONTROLLER_USE_GAMECUBE_LABELS':
            if not negate:
                line = positional_axby(line)
        elif hint == 'SDL_GAMECONTROLLER_USE_BUTTON_LABELS':
            if not negate:
                line = positional_baxy(line)
        else:
            value = bool(default)
            if negate:
                value = not value
            if not value:
                return None, 'hint'
    fields = [f for f in line.split(',') if f]
    keep = [fields[0].lower(), fields[1]]
    for f in fields[2:]:
        if f.startswith('platform:') or f.startswith('hint:'):
            continue
        keep.append(f)
    return ','.join(keep) + ',', None


def build():
    with open(HEADER, encoding='utf-8') as f:
        builtin = builtin_android_lines(f.read())
    with open(DB, encoding='utf-8') as f:
        filed = file_android_lines(f.read())
    with open(SOURCE, encoding='utf-8') as f:
        source = [ln.rstrip() for ln in f if ln.strip()]
    sections, dropped, known = {'sdl': [], 'db': []}, {}, []
    counts = {}
    sdl_pads, sdl_guids = set(), set()
    for origin, lines in (('sdl', builtin), ('db', filed)):
        n = 0
        for ln in lines:
            out, why = settle(ln)
            if out is None:
                dropped[why] = dropped.get(why, 0) + 1
                continue
            guid = out[:32]
            pad = vidpid(guid)
            if origin == 'sdl':
                sdl_guids.add(guid)
                if pad != (0, 0):
                    sdl_pads.add(pad)
            elif guid in sdl_guids or pad in sdl_pads:
                # SDL knows this pad: its own line wins, the community line never overrides it
                # (the owner, 02.10.2026: the fewest problems; SDL itself would let the file win)
                known.append('%04x:%04x %s %s' % (pad[0], pad[1], guid, out.split(',')[1]))
                continue
            sections[origin].append(out)
            n += 1
        counts[origin] = (n, len(lines))
    dropped['known to SDL'] = len(known)
    head = [
        '# paddb.txt - SDL gamepad mappings for Android; made by tools/paddb.py, do not edit by hand',
        '# zlib licence: paddb-LICENSE.txt. Sources (tools/paddb/SOURCE.txt):',
    ]
    head += ['#   ' + s for s in source[:2]]
    head += [
        '# @sdl: SDL\'s built-in Android lines (%d of %d); @db: gamecontrollerdb.txt\'s platform:Android lines'
        ' (%d of %d) for pads SDL has no line of its own for.' % (counts['sdl'] + counts['db']),
        '# SDL\'s lines win: a community line of a vendor:product (or GUID) SDL knows is dropped here, and the'
        ' app looks in @db only when @sdl has nothing for the pad\'s vendor:product.',
        '# within a section a later line of the same GUID (and crc:) replaces the earlier one in place;'
        ' sdk>=:/sdk<=: lines that do not fit the phone are skipped first.',
        '# dropped: %s' % (', '.join('%d %s' % (v, k) for k, v in sorted(dropped.items())) or 'none')
        + ' (legacy = a pre-2018 GUID no SDL produces today; tools/paddb.py --known lists the others)',
    ]
    kept = ['@sdl'] + sections['sdl'] + ['@db'] + sections['db']
    return '\n'.join(head + kept) + '\n', counts, dropped, known


def fetch():
    def get(url):
        req = urllib.request.Request(url, headers={'User-Agent': 'isaac-port-paddb'})
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.read()

    def commit(repo, path, branch):
        url = 'https://api.github.com/repos/%s/commits?sha=%s&per_page=1' % (repo, branch)
        if path:
            url += '&path=' + path
        c = json.loads(get(url))[0]
        return c['sha'], c['commit']['committer']['date']

    db_sha, db_date = commit('mdqinc/SDL_GameControllerDB', '', 'master')
    sdl_sha, sdl_date = commit('libsdl-org/SDL', '', 'main')
    raw = 'https://raw.githubusercontent.com/%s/%s/%s'
    data = {
        DB: get(raw % ('mdqinc/SDL_GameControllerDB', db_sha, 'gamecontrollerdb.txt')),
        LICENSE: get(raw % ('mdqinc/SDL_GameControllerDB', db_sha, 'LICENSE')),
        HEADER: get(raw % ('libsdl-org/SDL', sdl_sha, 'src/joystick/SDL_gamepad_db.h')),
    }
    for path, blob in data.items():
        with open(path, 'wb') as f:
            f.write(blob)
    with open(SOURCE, 'w', encoding='utf-8', newline='\n') as f:
        f.write('gamecontrollerdb.txt  https://github.com/mdqinc/SDL_GameControllerDB  %s  %s\n' % (db_sha, db_date))
        f.write('SDL_gamepad_db.h      https://github.com/libsdl-org/SDL  src/joystick/SDL_gamepad_db.h  %s  %s\n'
                % (sdl_sha, sdl_date))
        f.write('LICENSE.txt           the zlib licence of SDL_GameControllerDB (SDL\'s own text; '
                'SDL_gamepad_db.h carries the same in its header)\n')
    print('fetched: db %s (%s), SDL %s (%s)' % (db_sha[:12], db_date, sdl_sha[:12], sdl_date))


def main():
    args = sys.argv[1:]
    if '--fetch' in args:
        fetch()
    text, counts, dropped, known = build()
    if '--known' in args:
        print('\n'.join(known))
        return
    with open(LICENSE, 'rb') as f:
        lic = f.read()
    if '--check' in args:
        ok = True
        for path, want in ((OUT, text.encode('utf-8')), (OUT_LICENSE, lic)):
            try:
                with open(path, 'rb') as f:
                    have = f.read()
            except OSError:
                have = None
            if have != want:
                print('%s is not what tools/paddb/ makes' % path)
                ok = False
        sys.exit(0 if ok else 1)
    with open(OUT, 'wb') as f:
        f.write(text.encode('utf-8'))
    with open(OUT_LICENSE, 'wb') as f:
        f.write(lic)
    print('paddb.txt: %d SDL built-in + %d gamecontrollerdb lines kept (of %d + %d); dropped %s'
          % (counts['sdl'][0], counts['db'][0], counts['sdl'][1], counts['db'][1],
             ', '.join('%d %s' % (v, k) for k, v in sorted(dropped.items())) or 'none'))


if __name__ == '__main__':
    main()
