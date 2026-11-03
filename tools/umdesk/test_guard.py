"""Desk run of THE GUARD (usermods.c, plan 02.10 item 1) on real mods, checked against modcheck.py.

    py -3 tools/umdesk/test_guard.py [work dir]

1. Each mod zip is unpacked into a desk config's mods/ (as the installer would), the desk
   stand runs the start and the first file request (the guard over the stacked set), and its
   verdicts are listed.
2. The engine's view after the guard - every mod file replaced by the guard's completed copy,
   refused files dropped - is checked again by modcheck.py: no E-ANIM / E-LAYER / E-TRACK /
   E-NTRACK / E-SHEETS / E-LIMIT / E-SHEETID may remain (E-NULL "renamed" is told, not fixed).
3. Synthetic cases: 65 layer tracks, 5000 frames, a truncated png, a png with data past its
   stream, a sheet path ../../resources/, an old items.xml, a file of the port's menus.
4. A second start answers everything from the cache (no file read).
5. M4 (batch 5): whole data tables past the loaders' fixed arrays are not handed out, on the
   report build (desk.exe) and the clean one (desk0.exe: build.sh desk0.exe 0) - players.xml id 41,
   items.xml collectible 733, challenges.xml 46 and the rest of THE LOADERS' FIXED TABLES; the
   game's own tables as a mod pass untouched; the report's legacy diff reads the mod's own file.
"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DESK = os.path.join(HERE, 'desk.exe')
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODCHECK = os.path.join(ROOT, 'разбор', '2026-10-02', 'modcheck')
SCRATCH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TEMP', '.'), 'umdesk-guard')
WWW = os.path.join(tempfile.gettempdir(), 'claude', 'Z--Isaac', '9171fab8-7bd7-4003-aa99-2cf2b370da2f', 'scratchpad', 'www')
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data')
failures = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        failures.append(what)


def run(cfg, root, *args, desk=None):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = root
    out = subprocess.run([desk or DESK, 'guard' if not args else 'map', cfg] + list(args), capture_output=True, text=True,
                         env=e, encoding='utf-8', errors='replace')
    return out.stdout + out.stderr


def unpack(zpath, dest):
    with zipfile.ZipFile(zpath) as z:
        for n in z.namelist():
            if n.endswith('/'):
                continue
            out = os.path.join(dest, n)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, 'wb') as f:
                f.write(z.read(n))


def verdicts(out):
    v = {}
    for line in out.splitlines():
        m = re.match(r'   VERDICT (fixed|refused)\s+(\S+) (\S*\|.*?) \| (.*)$', line)
        if m:
            v[(m.group(2), m.group(3))] = (m.group(1), m.group(4))
    return v


def guard_view(mods, root, ident, v, dest):
    """the mod as the engine meets it after the guard: completed copies in, refused files out"""
    src = os.path.join(mods, ident)
    shutil.copytree(src, dest)
    for (owner, key), (kind, why) in v.items():
        if owner != ident:
            continue
        s, path = key.split('|', 1)
        folder = 'resources' if not s else ('resources-' + s if s.startswith('dlc3') else 'resources.' + s[5:])
        # the mod file's real spelling: the key is lower case
        target = None
        for base in ('resources-dlc3', 'resources') if not s else (folder,):
            cand = os.path.join(dest, base, *path.split('/'))
            hit = find_ci(cand)
            if hit:
                target = hit
                break
        if not target:
            continue
        if kind == 'refused':
            os.remove(target)
        else:
            fixed = os.path.join(root, '..', 'content-cache', 'guard', owner, s or '_', *path.split('/'))
            shutil.copyfile(fixed, target)


def find_ci(path):
    """a path on a case-blind disk: os.path.exists answers already"""
    return path if os.path.exists(path) else None


def modcheck(where):
    out = subprocess.run([sys.executable, 'modcheck.py', where], capture_output=True, cwd=MODCHECK)
    return out.stdout.decode('utf-8', 'replace')


def png(w, h, extra_after_stream=b'', truncate=0):
    raw = b''.join(b'\x00' + b'\x80' * (w * 4) for _ in range(h))
    comp = zlib.compress(raw) + extra_after_stream

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', comp) + chunk(b'IEND', b'')
    return data[:-truncate] if truncate else data


def anm2(tracks, frames=1, sheet='x.png'):
    layers = ''.join('<Layer Name="L%d" Id="%d" SpritesheetId="0"/>' % (i, i) for i in range(tracks))
    la = ''.join('<LayerAnimation LayerId="%d" Visible="true">%s</LayerAnimation>' % (
        i, '<Frame XPosition="0" YPosition="0" Delay="1" Visible="true"/>' * frames) for i in range(tracks))
    return ('<AnimatedActor><Info/><Content><Spritesheets><Spritesheet Path="%s" Id="0"/></Spritesheets><Layers>%s</Layers>'
            '<Nulls/><Events/></Content><Animations DefaultAnimation="Idle"><Animation Name="Idle" FrameNum="%d" Loop="true">'
            '<RootAnimation><Frame XPosition="0" YPosition="0" Delay="%d" Visible="true"/></RootAnimation><LayerAnimations>%s'
            '</LayerAnimations><NullAnimations/><Triggers/></Animation></Animations></AnimatedActor>' % (sheet, layers, frames, frames, la))


shutil.rmtree(SCRATCH, ignore_errors=True)
root = os.path.join(SCRATCH, 'files', 'app')
os.makedirs(root)

# ---- 1-2: the real mods, one config each (and PT-BR raw + ported stacked)
for label, zips in (('reflashed-py', ['reflashed-py.zip']), ('ptbr-src', ['ptbr-src.zip']),
                    ('stack', ['ptbr-src.zip', 'reflashed-py.zip'])):
    print('\n%s' % label)
    cfg = os.path.join(SCRATCH, 'cfg-' + label)
    mods = os.path.join(cfg, 'mods')
    os.makedirs(mods)
    idents = []
    for z in zips:
        ident = os.path.splitext(z)[0]
        unpack(os.path.join(WWW, z), os.path.join(mods, ident))
        idents.append(ident)
    open(os.path.join(mods, 'mods.txt'), 'w').write("# the player's mods, the strongest first: on|off <folder>\n"
                                                   + ''.join('on %s\n' % i for i in idents))
    shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
    out = run(cfg, root)
    v = verdicts(out)
    t = re.search(r'guard: done in (\d+) ms', out)
    print('  guard: %d verdicts in %s ms' % (len(v), t.group(1) if t else '?'))
    for (owner, key), (kind, why) in sorted(v.items())[:60]:
        print('    %-7s %s %s - %s' % (kind, owner, key, why[:150]))
    for ident in idents:
        check(os.path.exists(os.path.join(mods, ident + '.check.txt')) == any(o == ident for o, _ in v),
              '%s.check.txt there exactly when there are findings' % ident)
    if label == 'stack':
        # the stacked set: PT-BR first, so its files win - none of reflashed's shadowed files is judged
        ptbr_keys = {k for o, k in v if o == 'ptbr-src'}
        check(all(o == 'ptbr-src' or k not in ptbr_keys for o, k in v), 'one verdict per path: the strongest mod\'s file')
        continue
    view = os.path.join(SCRATCH, 'view-' + label)
    guard_view(mods, root, idents[0], v, view)
    before = modcheck(os.path.join(mods, idents[0]))
    after = modcheck(view)
    kinds = ('E-ANIM', 'E-LAYER', 'E-TRACK', 'E-NTRACK', 'E-SHEETS', 'E-LIMIT', 'E-SHEETID')
    def count(rep):
        head = rep.splitlines()[0] if rep else ''
        m = re.search(r"findings (\{.*?\})", head)
        d = eval(m.group(1)) if m else {}
        return {k: d.get(k, 0) for k in kinds + ('E-NULL', 'W-NOSHEET')}
    b, a = count(before), count(after)
    print('  modcheck before: %s' % b)
    print('  modcheck after:  %s' % a)
    check(all(a[k] == 0 for k in kinds), 'no E-ANIM/LAYER/TRACK/NTRACK/SHEETS/LIMIT/SHEETID after the guard')
    for line in after.splitlines():
        if line.startswith('E-'):
            print('    left: ' + line[:200])
    # 4. the second start: from the cache
    out2 = run(cfg, root)
    m = re.search(r'guard %s: (\d+) files checked, (\d+) answered from the cache' % re.escape(idents[0]), out2)
    check(m is not None and m.group(1) == '0', 'second start: %s' % (m.group(0) if m else out2[-300:]))
    check(verdicts(out2) == v, 'the same verdicts from the cache')

# ---- 3: synthetic cases
print('\nsynthetic')
cfg = os.path.join(SCRATCH, 'cfg-synth')
mods = os.path.join(cfg, 'mods')
res = os.path.join(mods, 'synth', 'resources')
os.makedirs(os.path.join(res, 'gfx', 'ui', 'main menu'))
open(os.path.join(mods, 'synth', 'mod.json'), 'w').write('{"id": "synth", "name": "Synth"}')
open(os.path.join(res, 'gfx', '001.000_player.anm2'), 'w').write(anm2(65))        # a game file, 65 tracks
open(os.path.join(res, 'gfx', 'new_long.anm2'), 'w').write(anm2(2, frames=5000))        # not a game file, 5000 frames
open(os.path.join(res, 'gfx', 'ui', 'main menu', 'gamemenu.anm2'), 'w').write(open(os.path.join(GAME, 'gfx', 'ui', 'main menu', 'gamemenu.anm2'), encoding='utf-8').read())
open(os.path.join(res, 'gfx', '889.000_clickety clack.anm2'), 'w').write(
    open(os.path.join(GAME, 'gfx', '889.000_clickety clack.anm2'), encoding='utf-8').read().replace('Path="', 'Path="../../resources/gfx/', 1))
os.makedirs(os.path.join(res, 'gfx', 'monsters', 'classic'))
open(os.path.join(res, 'gfx', 'monsters', 'classic', 'cut.png'), 'wb').write(png(16, 16, truncate=20))
open(os.path.join(res, 'gfx', 'monsters', 'classic', 'extra.png'), 'wb').write(png(16, 16, extra_after_stream=b'\x00\x01\x02'))
open(os.path.join(res, 'gfx', 'monsters', 'classic', 'good.png'), 'wb').write(png(16, 16))
# an old items.xml: the game's with its last ten records cut
items = open(os.path.join(GAME, 'items.xml'), encoding='utf-8').read()
cut = items
for _ in range(10):
    i = cut.rfind('\n\t<', 0, cut.rfind('</items>'))
    cut = cut[:i] + '\n</items>\n'
open(os.path.join(res, 'items.xml'), 'w', encoding='utf-8').write(cut)
os.makedirs(os.path.join(res, 'gfx', 'ui'), exist_ok=True)
open(os.path.join(res, 'gfx', 'ui', 'hudstats2.png'), 'wb').write(png(256, 256))       # 4x of the game's 64x64
open(os.path.join(res, 'gfx', 'ui', 'buttons.png'), 'wb').write(png(320, 256))         # the game's size: a recolour
open(os.path.join(mods, 'mods.txt'), 'w').write("# x\non synth\n")
shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
out = run(cfg, root)
v = verdicts(out)
for (owner, key), (kind, why) in sorted(v.items()):
    print('    %-7s %s - %s' % (kind, key, why[:160]))
check(v.get(('synth', '|gfx/001.000_player.anm2'), ('',))[0] == 'refused', '65 layer tracks: the game\'s file')
check(v.get(('synth', '|gfx/new_long.anm2'), ('',))[0] == 'refused', '5000 frames, no game file: left out')
# M8 (02.10.2026): the port lays its additions over a mod's own menu file by name (shim_port_patch_file), so the
# game's gamemenu.anm2 brought by a mod is no longer refused; the port's patch goes over it when it is handed out
check(v.get(('synth', '|gfx/ui/main menu/gamemenu.anm2'), ('',))[0] != 'refused',
      'the port\'s menu file: a mod\'s own is taken now (the port\'s patch laid over it, M8)')
cc = v.get(('synth', '|gfx/889.000_clickety clack.anm2'), ('', ''))
check(cc[0] == 'fixed' and 'sheet paths' in cc[1], 'a sheet path ../../resources/: pointed at the set: %s' % (cc,))
fixed889 = os.path.join(SCRATCH, 'files', 'content-cache', 'guard', 'synth', '_', 'gfx', '889.000_clickety clack.anm2')
if os.path.exists(fixed889):
    check('../../resources' not in open(fixed889, encoding='utf-8').read(), 'the completed copy has no ../../resources/')
check(v.get(('synth', '|gfx/monsters/classic/cut.png'), ('',))[0] == 'refused', 'a truncated png: refused at the index')
check(v.get(('synth', '|gfx/ui/hudstats2.png'), ('',))[0] == 'refused', 'a 4x sheet of a file read by layer number: the game\'s')
check(('synth', '|gfx/ui/buttons.png') not in v, 'a same-size sheet of it: the mod\'s')
hs = v.get(('synth', '|gfx/ui/hudstats2.png'), ('', ''))
check('converter' in hs[1] and 'by hand' in hs[1], 'its reason tells the modder to convert or scale by hand: ...%s' % hs[1][-110:])
check('of the wrong size 1' in out, 'the page\'s count of wrong-size pictures: 1')
it = v.get(('synth', '|items.xml'), ('', ''))
check(it[0] == 'fixed' and 'records' in it[1], 'an old items.xml completed: %s' % (it,))
fixed_items = os.path.join(SCRATCH, 'files', 'content-cache', 'guard', 'synth', '_', 'items.xml')
if os.path.exists(fixed_items):
    n_game = len(re.findall(r'<(active|passive|familiar) ', items))
    n_fix = len(re.findall(r'<(active|passive|familiar) ', open(fixed_items, encoding='utf-8').read()))
    check(n_game == n_fix, 'the completed items.xml has the game\'s %d items (%d)' % (n_game, n_fix))
# the png whose stream has data after it: only at its first use
o = run(cfg, root, 'repentance-resources/data/gfx/monsters/classic/extra.png')
check('(the game\'s own)' in o and 'Extra compressed data' in o, 'data after the stream: the game\'s picture at first use')
o = run(cfg, root, 'repentance-resources/data/gfx/monsters/classic/good.png')
check('good.png' in o.split('== map')[1].splitlines()[0], 'a good picture: the mod\'s')
o = run(cfg, root, 'repentance-resources/data/gfx/monsters/classic/extra.png')
check('(the game\'s own)' in o, 'and the next start knows it from the cache')
note = open(os.path.join(mods, 'synth.check.txt'), encoding='utf-8').read() if os.path.exists(os.path.join(mods, 'synth.check.txt')) else ''
check('extra.png' in note and "player" in note, 'mods/synth.check.txt names them')

# ---- 5: M4, the loaders' fixed tables
def game_text(name):
    return open(os.path.join(GAME, name), encoding='utf-8', errors='replace').read()


def append(text, root_tag, record):
    """a record put before the root's end tag"""
    i = text.rfind('</%s>' % root_tag)
    return text[:i] + record + '\n' + text[i:]


def m4_case(desk, label, files):
    """one mod holding these whole tables, alone in its config: the guard's verdicts and the run's text"""
    cfg = os.path.join(SCRATCH, 'cfg-m4-%s-%s' % (os.path.basename(desk)[:-4], label))
    mods = os.path.join(cfg, 'mods')
    res = os.path.join(mods, 'm4', 'resources')
    os.makedirs(res)
    open(os.path.join(mods, 'm4', 'mod.json'), 'w').write('{"id": "m4", "name": "M4"}')
    for name, text in files.items():
        with open(os.path.join(res, name), 'w', encoding='utf-8', newline='') as f:
            f.write(text)
    open(os.path.join(mods, 'mods.txt'), 'w').write("# x\non m4\n")
    shutil.rmtree(os.path.join(SCRATCH, 'files', 'content-cache'), ignore_errors=True)
    out = run(cfg, root, desk=desk)
    return cfg, res, out, verdicts(out)


BAD = [   # label, file, root tag, record put in, the reason's start
    ('players41', 'players.xml', 'players', '<player id="41" name="X" skin="x.png" />', "player id 41 is not in the game's table 0..40"),
    ('items733', 'items.xml', 'items', '<passive id="733" name="X" gfx="x.png" />', "passive id 733 is not in the game's table 0..732"),
    ('challenge46', 'challenges.xml', 'challenges', '<challenge id="46" name="X" />', "challenge id 46 is not in the game's table 0..45"),
    ('trinket190', 'items.xml', 'items', '<trinket id="190" name="X" gfx="x.png" />', "trinket id 190 is not in the game's table 0..189"),
    ('unknowntag', 'items.xml', 'items', '<thing id="200" name="X" />', "thing id 200 is not in the game's table 0..131"),
    ('itemneg', 'items.xml', 'items', '<passive id="-3" name="X" gfx="x.png" />', "passive id -3 is not in the game's table 0..732"),
    ('card98', 'pocketitems.xml', 'pocketitems', '<card id="98" name="X" />', "card id 98 is not in the game's table 0..97"),
    ('pill50', 'pocketitems.xml', 'pocketitems', '<pilleffect id="50" name="X" />', "pilleffect id 50 is not in the game's table 0..49"),
    ('soundneg', 'sounds.xml', 'sounds', '<sound id="-1"><sample path="x.wav" /></sound>', "sound id -1 is not in the game's table 0..832"),
    ('wisp19', 'wisps.xml', 'wisps', '<wisp id="s19" hp="1" />', "wisp id 19 is not in the game's table 0..18"),
    ('wisp733', 'wisps.xml', 'wisps', '<wisp id="733" hp="1" />', "wisp id 733 is not in the game's table 0..732"),
    ('costume132', 'costumes2.xml', 'costumes', '<costume id="132" anm2path="x.anm2" type="none" />', "costume id 132 is not in the game's table 0..131"),
    ('meta190', 'items_metadata.xml', 'items', '<trinket id="190" quality="0" />', "trinket id 190 is not in the game's table 0..189"),
    ('boss104alt', 'bossportraits.xml', 'bosses', '<boss id="104" name="X"><alt stage="1" portrait="x.png" /></boss>', "boss id 104 is not in the game's table 0..103"),
    ('entity-id', 'players.xml', 'players', '<player id="&#52;1" name="X" />', "player id 41 is not in the game's table 0..40"),
]
OK = [    # label, file, root tag, record put in: what the loader itself skips or takes
    ('players-neg', 'players.xml', 'players', '<player id="-1" name="X" />'),
    ('sound833', 'sounds.xml', 'sounds', '<sound id="833"><sample path="x.wav" /></sound>'),
    ('card-neg', 'pocketitems.xml', 'pocketitems', '<card id="-5" name="X" />'),
    ('boss104', 'bossportraits.xml', 'bosses', '<boss id="104" name="X" />'),
    ('wisp-s18', 'wisps.xml', 'wisps', '<wisp id="s18" hp="1" />'),
    ('costume131', 'costumes2.xml', 'costumes', '<costume id="131" anm2path="x.anm2" type="none" />'),
]
TABLES = ['players.xml', 'items.xml', 'challenges.xml', 'pocketitems.xml', 'sounds.xml', 'wisps.xml', 'costumes2.xml',
          'items_metadata.xml', 'bossportraits.xml', 'entities2.xml']

for desk in (DESK, os.path.join(HERE, 'desk0.exe')):
    tag = os.path.basename(desk)
    if not os.path.exists(desk):
        check(False, '%s is built (build.sh desk0.exe 0)' % tag)
        continue
    print('\nM4 on %s' % tag)
    # Every one of these tables is content.c's now (it builds what the game gets, a three-way merge, and checks the
    # bounds of the merged table itself, 04.10.2026): the guard does not refuse the mod's raw file by its bounds -
    # it used to, and Sam & Max's items.xml was "NOT USED" and counted as "the game's instead" while the game got
    # the right merged table. What the game gets has no record past the loader's table.
    for label, name, root_tag, record, why in BAD:
        cfg, res, out, v = m4_case(desk, label, {name: append(game_text(name), root_tag, record)})
        got = v.get(('m4', '|' + name.lower()), ('', ''))
        check(got[0] != 'refused', '%s: %s not refused by the guard (content.c builds it) - %s' % (label, name, got))
        o = run(cfg, root, 'repentance-resources/data/' + name, desk=desk)
        line = o.split('== map')[1].splitlines()[0] if '== map' in o else ''
        served = line.split(' -> ', 1)[1].strip() if ' -> ' in line else ''
        text = open(served, encoding='utf-8', errors='replace').read() if os.path.exists(served) else game_text(name)
        check(record not in text, '%s: what the game gets of %s has no record past its table (%s)' % (label, name, served[-60:]))
        b = re.search(r'   BOUNDS m4 ', out)
        check(b is None, "%s: no guard's bounds line on the page (content.c's line tells)" % label)
        out2 = run(cfg, root, desk=desk)
        check(verdicts(out2) == v, '%s: the same from the cache' % label)
    # the first challenge with no id: the stack's index
    t = game_text('challenges.xml')
    i = t.find('<challenge ')
    cfg, res, out, v = m4_case(desk, 'challenge-noid', {'challenges.xml': t[:i] + '<challenge name="X" />\n\t' + t[i:]})
    got = v.get(('m4', '|challenges.xml'), ('', ''))
    check(got[0] != 'refused', 'a first challenge with no id: not refused by the guard (content.c builds it) - %s' % (got,))
    o = run(cfg, root, 'repentance-resources/data/challenges.xml', desk=desk)
    line = o.split('== map')[1].splitlines()[0] if '== map' in o else ''
    served = line.split(' -> ', 1)[1].strip() if ' -> ' in line else ''
    text = open(served, encoding='utf-8', errors='replace').read() if os.path.exists(served) else t
    check('<challenge name="X" />' not in text, 'a first challenge with no id: not in what the game gets (%s)' % served[-60:])
    # a bestiary parent chain that comes back to itself
    cyc = ('<entity id="900" variant="0" name="A" anm2path="x.anm2"><bestiary parent="901.0" /></entity>'
           '<entity id="901" variant="0" name="B" anm2path="x.anm2"><bestiary parent="900.0" /></entity>')
    cfg, res, out, v = m4_case(desk, 'parents', {'entities2.xml': append(game_text('entities2.xml'), 'entities', cyc)})
    got = v.get(('m4', '|entities2.xml'), ('', ''))
    check(got[0] == 'refused' and 'comes back to itself' in got[1], 'a parent cycle in entities2.xml: refused - %s' % got[1][:100])
    for label, name, root_tag, record in OK:
        cfg, res, out, v = m4_case(desk, label, {name: append(game_text(name), root_tag, record)})
        got = v.get(('m4', '|' + name.lower()), ('', ''))
        check(got[0] != 'refused', '%s: what the loader skips itself is not refused (%s)' % (label, got))
    # the game's own tables, one value changed, as a legacy mod: handed out as they are
    files = {n: game_text(n) for n in TABLES}
    files['players.xml'] = files['players.xml'].replace('armor="1"', 'armor="2"', 1)
    cfg, res, out, v = m4_case(desk, 'valid', files)
    mine = {k: x for (o, k), x in v.items() if o == 'm4'}
    check(not any(x[0] == 'refused' for x in mine.values()), "the game's tables as a mod: nothing refused %s" % mine)
    for n in TABLES:
        o = run(cfg, root, 'repentance-resources/data/' + n, desk=desk)
        line = o.split('== map')[1].splitlines()[0] if '== map' in o else ''
        got = line.split(' -> ', 1)[1].strip() if ' -> ' in line else ''
        same = os.path.exists(got) and open(got, encoding='utf-8', errors='replace').read() == files[n]
        if n == 'players.xml':
            check(os.path.normcase(os.path.abspath(got)) == os.path.normcase(os.path.abspath(os.path.join(res, n))) and same,
                  "a valid legacy players.xml: the mod's own file, byte for byte (%s)" % got)
        else:
            check(same, "the game's %s as a mod: handed out unchanged (%s)" % (n, got))

# the report's diff of a legacy table reads the mod's own file, not the guard's completed copy
cfg = os.path.join(SCRATCH, 'cfg-synth')
o = run(cfg, root, 'repentance-resources/data/items.xml')
m = re.search(r'legacy items\.xml from synth: \d+ records; .*?(\d+) records added, (\d+) taken away', o)
check(m is not None and int(m.group(2)) >= 10, "the report's legacy diff counts the 10 records the mod took away: %s"
      % (m.group(0) if m else o[-300:]))

# the report tells (does not refuse) a gibFlags word the loader does not know: it reads a stray int for its bit
t = game_text('entities2.xml').replace('gibFlags="poop"', 'gibFlags="poop  blod"', 1)
cfg, res, out, v = m4_case(DESK, 'gibs', {'entities2.xml': t})
o = run(cfg, root, 'repentance-resources/data/entities2.xml')
check(v.get(('m4', '|entities2.xml'), ('',))[0] != 'refused' and 'gibFlags "blod" is not a word' in o
      and 'gibFlags "" is not a word' in o, 'an unknown gibFlags word (and the empty one of "  "): told in the report, not refused')

print('\n%d failures' % len(failures))
for f in failures:
    print('  ' + f)
sys.exit(1 if failures else 0)
