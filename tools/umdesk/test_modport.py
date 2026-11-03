"""Desk check of the converter's size rule (the owner, 02.10.2026): a mod's picture of a file the port
reads by layer number, k times the game's size, cut by the GAME's anm2 (the mod brings none of its own),
is BAKED DOWN by the converter (tools/modport.py), never scaled by the game - the pause widget's twin
completion_widget_pause.png too. Hotfix 1.71 (05.10.2026, as usermods.c guard_by_number_sheets): the mod's
own anm2 of such a file is KEPT, and the sheets it cuts are its layout, any size (Cooler Completion Marks).

The menus the port lays its additions over by name (gamemenu, optionsmenu, pausescreen, ios_buttons .anm2;
M8, 02.10.2026): a mod's own copy is ALWAYS kept (05.10.2026: the port's check at start decides whether its
additions fit it); a menu at x4 with its own x4 crops (Reflashed) keeps its anm2, the sheets are baked down
and its frames rescaled to them, so the mod's file or the game's cuts them right; a mod's
completion_widget.anm2 is kept (hotfix 1.71).

Never stricter than the port (the owner, 05.10.2026): a sheet the port's check would serve is kept - one of
another size cut by the mod's own menu anm2, a port-drawn picture of a size that is no multiple, a sheet
shorter than the game's; a room file is judged by its magic, not its room count; an older text table keeps
the mod's records whole and gets the game's missing ones added (usermods.c guard_table).

Never looser either (round 3, 05.10.2026): what the port's check at start would refuse is left out with its reason -
a menu .anm2 the port's rows do not fit whole (the same check as guard_port_patched), the sheets of such a file that
the game's frames cannot cut (guard_orphan_sheets), an .anm2 past the engine loader's limits.

    py -3 tools/umdesk/test_modport.py
"""
import io
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(ROOT, 'work', 'app', 'repentance-resources')
MODPORT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'modport.py')
failures = []


def check(cond, what):
    print(('  ok    ' if cond else '  FAIL  ') + what)
    if not cond:
        failures.append(what)


work = tempfile.mkdtemp(prefix='modport-')
mod = os.path.join(work, 'mod')
ui = os.path.join(mod, 'resources', 'gfx', 'ui')
os.makedirs(ui)
open(os.path.join(mod, 'metadata.xml'), 'w').write('<metadata><name>Size test</name><directory>sizetest</directory></metadata>')
game_hud = Image.open(os.path.join(GAME, 'data', 'gfx', 'ui', 'hudstats2.png')).convert('RGBA')
# a pixel-art x4: every pixel of the game's a 4x4 block - what a "4x" mod is
game_hud.resize((game_hud.width * 4, game_hud.height * 4), Image.NEAREST).save(os.path.join(ui, 'hudstats2.png'))
# the pause widget's twin at x2 with no anm2 of the mod: under the game's frames, baked down like its base
game_cw = Image.open(os.path.join(GAME, 'data', 'gfx', 'ui', 'completion_widget.png')).convert('RGBA')
game_cwp = Image.open(os.path.join(GAME, 'data', 'gfx', 'ui', 'completion_widget_pause.png')).convert('RGBA')
game_cwp.resize((game_cwp.width * 2, game_cwp.height * 2), Image.NEAREST).save(os.path.join(ui, 'completion_widget_pause.png'))
# a recolour at the game's size: the mod's, untouched
btn = Image.open(os.path.join(GAME, 'data', 'gfx', 'ui', 'buttons.png')).convert('RGBA')
r, g, b, a = btn.split()
recol = Image.merge('RGBA', (b, g, r, a))
recol.save(os.path.join(ui, 'buttons.png'))
# not a whole multiple: no way to bake it - kept as the game takes it, with a warning (the port serves it, 05.10.2026)
Image.new('RGBA', (300, 600), (9, 9, 9, 255)).save(os.path.join(ui, 'ScoreMenu.png'))
out = os.path.join(work, 'out.zip')
res = subprocess.run([sys.executable, MODPORT, mod, '--game', GAME, '--out', out],
                     capture_output=True, text=True, encoding='utf-8', errors='replace')
report = open(out + '.report.txt', encoding='utf-8').read() if os.path.exists(out + '.report.txt') else res.stdout + res.stderr
z = zipfile.ZipFile(out)
names = {n.lower(): n for n in z.namelist()}
for line in report.splitlines():
    if 'hudstats2' in line or 'buttons' in line or 'scoremenu' in line.lower() or 'completion' in line:
        print('    report: ' + line[:220])
hud = names.get('resources/gfx/ui/hudstats2.png')
check(hud is not None, 'hudstats2.png is in the mod')
if hud:
    im = Image.open(io.BytesIO(z.read(hud))).convert('RGBA')
    check(im.size == game_hud.size, 'baked down to the game\'s %dx%d: %dx%d' % (game_hud.size + im.size))
    check(im.tobytes() == game_hud.tobytes(), 'pixel for pixel the game\'s 1x (BOX on an exact factor keeps pixel art crisp)')
cwp = names.get('resources/gfx/ui/completion_widget_pause.png')
check(cwp is not None and Image.open(io.BytesIO(z.read(cwp))).convert('RGBA').tobytes() == game_cwp.tobytes(),
      'the pause twin at x2, no anm2 of the mod: baked down to the game\'s %dx%d' % game_cwp.size)
bt = names.get('resources/gfx/ui/buttons.png')
check(bt is not None and Image.open(io.BytesIO(z.read(bt))).convert('RGBA').tobytes() == recol.tobytes(),
      'a recolour at the game\'s size: kept as it is')
check(names.get('resources/gfx/ui/scoremenu.png') is not None and 'ScoreMenu.png: 300x600 is not a multiple' in report
      and 'kept, as the game takes it' in report, 'not a whole multiple: kept as the game takes it, with a warning')
check('baked down to 64x64' in report, 'the report says it was baked down')
# a picture drawn finely at 4x (not blown-up pixels): the area mean, no LANCZOS fringe at the alpha edge
sys.path.insert(0, os.path.dirname(MODPORT))
import modport  # noqa: E402
fine = Image.new('RGBA', (8, 8), (0, 0, 0, 0))
for x in range(4):
    for y in range(8):
        fine.putpixel((x, y), (255, 255, 255, 255))
small = modport.shrink(fine, (2, 2))
check(small.getpixel((0, 0)) == (255, 255, 255, 255) and small.getpixel((1, 0))[3] == 0,
      'an opaque edge against transparency stays clean: %s %s' % (small.getpixel((0, 0)), small.getpixel((1, 0))))


def convert(mod_dir, name):
    o = os.path.join(work, name + '.zip')
    r = subprocess.run([sys.executable, MODPORT, mod_dir, '--game', GAME, '--out', o],
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    rp = open(o + '.report.txt', encoding='utf-8').read() if os.path.exists(o + '.report.txt') else r.stdout + r.stderr
    zz = zipfile.ZipFile(o)
    return zz, {k.lower(): k for k in zz.namelist()}, rp


REB = os.path.join(ROOT, 'work', 'app', 'rebirth-resources')
MENU = os.path.join(GAME, 'data', 'gfx', 'ui', 'main menu')

# the menus the port lays its additions over by name (M8, portpatch.c): a mod's own copy at the game's
# size is KEPT - its anm2 and its pictures, optionsextra.png too; the by-number files still go
print('\nmenus at the game\'s size')
mod2 = os.path.join(work, 'samesize')
mm = os.path.join(mod2, 'resources', 'gfx', 'ui', 'main menu')
os.makedirs(mm)
os.makedirs(os.path.join(mod2, 'resources', 'gfx', 'ui', 'touch'))
open(os.path.join(mod2, 'metadata.xml'), 'w').write('<metadata><name>Same size menus</name><directory>samesize</directory></metadata>')
for f in ('gamemenu.anm2', 'optionsmenu.anm2'):
    t = open(os.path.join(MENU, f), encoding='utf-8').read()
    open(os.path.join(mm, f), 'w', encoding='utf-8').write(t.replace('<AnimatedActor', '<AnimatedActor Mark="samesize"', 1))
for f in ('gamemenu.png', 'optionsextra.png'):
    im = Image.open(os.path.join(MENU, f)).convert('RGBA')
    r_, g_, b_, a_ = im.split()
    Image.merge('RGBA', (b_, g_, r_, a_)).save(os.path.join(mm, f))
shutil.copyfile(os.path.join(REB, 'data', 'gfx', 'ui', 'touch', 'ios_buttons.anm2'),
                os.path.join(mod2, 'resources', 'gfx', 'ui', 'touch', 'ios_buttons.anm2'))
shutil.copyfile(os.path.join(GAME, 'data', 'gfx', 'ui', 'completion_widget.anm2'),
                os.path.join(mod2, 'resources', 'gfx', 'ui', 'completion_widget.anm2'))
z2, n2, rp2 = convert(mod2, 'samesize')
for line in rp2.splitlines():
    if 'menu' in line or 'touch' in line or 'completion' in line:
        print('    report: ' + line[:230])
for f in ('main menu/gamemenu.anm2', 'main menu/optionsmenu.anm2', 'main menu/gamemenu.png', 'main menu/optionsextra.png',
          'touch/ios_buttons.anm2'):
    check(('resources/gfx/ui/' + f) in n2, 'kept: ' + f)
k = n2.get('resources/gfx/ui/main menu/gamemenu.anm2')
check(k is not None and b'Mark="samesize"' in z2.read(k), 'gamemenu.anm2 is the mod\'s own')
k = n2.get('resources/gfx/ui/main menu/optionsextra.png')
check(k is not None and Image.open(io.BytesIO(z2.read(k))).size == Image.open(os.path.join(MENU, 'optionsextra.png')).size,
      'optionsextra.png at the game\'s size, untouched')
check('resources/gfx/ui/completion_widget.anm2' in n2, 'completion_widget.anm2 kept: the mod\'s own (hotfix 1.71)')
check('lays its rows, straps and buttons over it by name' in rp2, 'the report says the port lays its additions over the mod\'s menus')

# Reflashed's way: a menu drawn at x4 with its own anm2 cropping the x4 sheet - the anm2 is kept (the port's check
# decides on it), the sheets are baked down to the game's size and the anm2's frames on them rescaled to match
print('\nmenus at x4 (Reflashed)')
import xml.etree.ElementTree as ET  # noqa: E402
mod3 = os.path.join(work, 'x4')
m3 = os.path.join(mod3, 'resources', 'gfx', 'ui', 'main menu')
os.makedirs(m3)
open(os.path.join(mod3, 'metadata.xml'), 'w').write('<metadata><name>Four times</name><directory>x4</directory></metadata>')
gopt = Image.open(os.path.join(MENU, 'optionsmenu.png')).convert('RGBA')
gopt.resize((gopt.width * 4, gopt.height * 4), Image.NEAREST).save(os.path.join(m3, 'optionsmenu.png'))
gx = Image.open(os.path.join(MENU, 'optionsextra.png')).convert('RGBA')
gx.resize((gx.width * 4, gx.height * 4), Image.NEAREST).save(os.path.join(m3, 'optionsextra.png'))
x4root = ET.parse(os.path.join(MENU, 'optionsmenu.anm2')).getroot()
x4rel = 'resources/gfx/ui/main menu/optionsmenu.anm2'
x4ids = {sid for sid, sh in modport.anm2_sheets(x4root, x4rel).items() if sh.rsplit('/', 1)[-1] in ('optionsmenu.png', 'optionsextra.png')}
modport.fit_anm2_up(x4root, x4ids, 4)                 # the mod's own x4 crops, drawn at 25%
open(os.path.join(m3, 'optionsmenu.anm2'), 'wb').write(ET.tostring(x4root, encoding='utf-8', xml_declaration=True))
z3, n3, rp3 = convert(mod3, 'x4')
for line in rp3.splitlines():
    if 'menu' in line:
        print('    report: ' + line[:230])
k3 = n3.get(x4rel)
check(k3 is not None, 'x4: the mod' + chr(39) + 's optionsmenu.anm2 kept - the port' + chr(39) + 's check decides on it')
for f, g in (('optionsmenu.png', gopt), ('optionsextra.png', gx)):
    k = n3.get('resources/gfx/ui/main menu/' + f)
    im = Image.open(io.BytesIO(z3.read(k))).convert('RGBA') if k else None
    check(im is not None and im.size == g.size and im.tobytes() == g.tobytes(), 'x4: %s baked down to the game' % f + chr(39) + 's %dx%d' % g.size)
if k3:
    got = ET.fromstring(z3.read(k3))
    game_root = ET.parse(os.path.join(MENU, 'optionsmenu.anm2')).getroot()

    def crops(root):
        ls = {l.get('Id'): l.get('SpritesheetId') for l in root.findall('Content/Layers/Layer')}
        out = []
        for a in root.findall('Animations/Animation'):
            for la in a.findall('LayerAnimations/LayerAnimation'):
                if int(ls.get(la.get('LayerId'), -1)) in x4ids:
                    for fr in la.findall('Frame'):
                        out.append(tuple(int(float(fr.get(t, 0))) for t in ('XCrop', 'YCrop', 'Width', 'Height', 'XScale', 'YScale')))
        return out
    check(crops(got) == crops(game_root) and crops(got), 'x4: its frames rescaled to the baked sheets - the game' + chr(39) + 's crops again')
check('frames on sheets scaled down by 4 rescaled' in rp3, 'the report says the mod' + chr(39) + 's anm2 was rescaled')

# never stricter than the port (05.10.2026): what the port's check would serve is kept
print('\nnever stricter than the port')
mod5 = os.path.join(work, 'lenient')
os.makedirs(os.path.join(mod5, 'resources', 'gfx', 'ui', 'main menu'))
os.makedirs(os.path.join(mod5, 'resources', 'gfx', 'items', 'collectibles'))
os.makedirs(os.path.join(mod5, 'resources', 'gfx', 'backdrop'))
os.makedirs(os.path.join(mod5, 'resources', 'rooms'))
open(os.path.join(mod5, 'metadata.xml'), 'w').write('<metadata><name>Lenient</name><directory>lenient</directory></metadata>')
shutil.copyfile(os.path.join(MENU, 'optionsmenu.anm2'), os.path.join(mod5, 'resources', 'gfx', 'ui', 'main menu', 'optionsmenu.anm2'))
Image.new('RGBA', (300, 300), (1, 2, 3, 255)).save(os.path.join(mod5, 'resources', 'gfx', 'ui', 'main menu', 'sketch.png'))
mk = Image.open(os.path.join(GAME, 'data', 'gfx', 'items', 'collectibles', 'collectibles_394_marked.png'))
Image.new('RGBA', (mk.width + 3, mk.height + 3), (4, 5, 6, 255)).save(
    os.path.join(mod5, 'resources', 'gfx', 'items', 'collectibles', 'collectibles_394_marked.png'))
ctl = Image.open(os.path.join(GAME, 'data', 'gfx', 'backdrop', 'controls.png'))
Image.new('RGBA', (ctl.width, ctl.height // 2), (7, 8, 9, 255)).save(os.path.join(mod5, 'resources', 'gfx', 'backdrop', 'controls.png'))
stb = bytearray(open(os.path.join(GAME, 'data', 'rooms', '01.basement.stb'), 'rb').read())
stb[4] = (stb[4] + 1) & 0xff                          # STB1, another room count
open(os.path.join(mod5, 'resources', 'rooms', '01.basement.stb'), 'wb').write(bytes(stb))
items = ET.parse(os.path.join(GAME, 'data', 'items.xml')).getroot()
kids = list(items)
for e in kids[len(kids) // 2:]:
    items.remove(e)                                   # an older table: the second half missing
first = next(e for e in items if e.tag == 'passive')
first.set('quality', '0' if first.get('quality') != '0' else '1')
mod_quality = first.get('quality')
open(os.path.join(mod5, 'resources', 'items.xml'), 'wb').write(ET.tostring(items, encoding='utf-8', xml_declaration=True))
z5, n5, rp5 = convert(mod5, 'lenient')
for line in rp5.splitlines():
    print('    report: ' + line[:230])
k = n5.get('resources/gfx/ui/main menu/sketch.png')
check(k is not None and Image.open(io.BytesIO(z5.read(k))).size == (300, 300), 'sketch.png 300x300 under the mod' + chr(39) + 's own optionsmenu.anm2: kept as it is')
k = n5.get('resources/gfx/items/collectibles/collectibles_394_marked.png')
check(k is not None and Image.open(io.BytesIO(z5.read(k))).size == (mk.width + 3, mk.height + 3), 'a port-drawn picture of no whole multiple: kept (the port serves it)')
k = n5.get('resources/gfx/backdrop/controls.png')
check(k is not None, 'a sheet shorter than the game' + chr(39) + 's: kept, as the game and 1.666 take it')
check('resources/rooms/01.basement.stb' in n5, 'a room file STB1 with its own room count: kept')
k = n5.get('resources/items.xml')
if k:
    got = ET.fromstring(z5.read(k))
    gkeys = [e.get('id') + e.tag for e in ET.parse(os.path.join(GAME, 'data', 'items.xml')).getroot() if e.get('id') is not None]
    have = [e.get('id') + e.tag for e in got if e.get('id') is not None]
    same = next((e for e in got if e.tag == first.tag and e.get('id') == first.get('id')), None)
    check(sorted(have) == sorted(gkeys), 'items.xml: every record of the game' + chr(39) + 's there (%d of %d)' % (len(have), len(gkeys)))
    check(same is not None and same.get('quality') == mod_quality, 'items.xml: the mod' + chr(39) + 's own record kept whole (its quality %s)' % mod_quality)
else:
    check(False, 'items.xml is in the mod')
check('records of the game' + chr(39) + 's it lacks added' in rp5, 'the report says the table was completed')

# never looser than the port (round 3): a menu file the port's rows do not fit is left out, as the port would refuse
# it, with its sheets the game's frames cannot cut; a sheet of a whole multiple stays (baked); an .anm2 past the loader's
# limits is left out
print('\nnever looser than the port')
mod6 = os.path.join(work, 'refused')
m6 = os.path.join(mod6, 'resources', 'gfx', 'ui', 'main menu')
os.makedirs(m6)
open(os.path.join(mod6, 'metadata.xml'), 'w').write('<metadata><name>Refused</name><directory>refused</directory></metadata>')
om = open(os.path.join(MENU, 'optionsmenu.anm2'), encoding='utf-8').read()
om2 = om.replace('Name="OnOff"', 'Name="Toggle"', 1)
assert om2 != om
open(os.path.join(m6, 'optionsmenu.anm2'), 'w', encoding='utf-8').write(om2)
Image.new('RGBA', (300, 300), (1, 2, 3, 255)).save(os.path.join(m6, 'optionsextra.png'))
gopt6 = Image.open(os.path.join(MENU, 'optionsmenu.png')).convert('RGBA')
gopt6.resize((gopt6.width * 2, gopt6.height * 2), Image.NEAREST).save(os.path.join(m6, 'optionsmenu.png'))
big = ET.parse(os.path.join(GAME, 'data', 'gfx', '001.000_player.anm2')).getroot()
first_anim = big.find('Animations/Animation')
la = first_anim.find('LayerAnimations')
for i in range(70):
    ET.SubElement(la, 'LayerAnimation', LayerId=str(100 + i), Visible='false')
os.makedirs(os.path.join(mod6, 'resources', 'gfx'), exist_ok=True)
open(os.path.join(mod6, 'resources', 'gfx', '001.000_player.anm2'), 'wb').write(ET.tostring(big, encoding='utf-8'))
z6, n6, rp6 = convert(mod6, 'refused')
for line in rp6.splitlines():
    if 'menu' in line or 'player' in line:
        print('    report: ' + line[:230])
check('resources/gfx/ui/main menu/optionsmenu.anm2' not in n6 and 'optionsmenu.anm2: menu file not used' in rp6,
      'a menu file the port' + chr(39) + 's rows do not fit (OnOff renamed): left out, with the port' + chr(39) + 's reason')
check('resources/gfx/ui/main menu/optionsextra.png' not in n6 and 'which the port would not use' in rp6,
      'its optionsextra.png 300x300 (no whole multiple): left out - the game' + chr(39) + 's frames cannot cut it')
k = n6.get('resources/gfx/ui/main menu/optionsmenu.png')
check(k is not None and Image.open(io.BytesIO(z6.read(k))).size == gopt6.size, 'its optionsmenu.png x2: kept, baked to the game' + chr(39) + 's size')
check('resources/gfx/001.000_player.anm2' not in n6 and "past the engine loader's limits" in rp6, 'an .anm2 with 70+ layer tracks: left out')

# hotfix 1.71: the mod's OWN anm2 of a file read by number is kept, and the sheets it cuts are the mod's layout -
# Cooler Completion Marks' way (393x500 sheets for its completion_widget.anm2, the game's 256x512) and a x4
# hudstats2.png with the game's hudstats2.anm2 copied in (R0 fits that anm2 to the picture, as on any file)
print('\nown anm2 of the files read by number (hotfix 1.71)')
mod4 = os.path.join(work, 'own')
u4 = os.path.join(mod4, 'resources', 'gfx', 'ui')
os.makedirs(u4)
open(os.path.join(mod4, 'metadata.xml'), 'w').write('<metadata><name>Own widget</name><directory>ownwidget</directory></metadata>')
t = open(os.path.join(GAME, 'data', 'gfx', 'ui', 'completion_widget.anm2'), encoding='utf-8').read()
open(os.path.join(u4, 'completion_widget.anm2'), 'w', encoding='utf-8').write(t.replace('<AnimatedActor', '<AnimatedActor Mark="own"', 1))
own_cw = game_cw.resize((393, 500), Image.NEAREST)
own_cw.save(os.path.join(u4, 'completion_widget.png'))
own_cw.save(os.path.join(u4, 'completion_widget_pause.png'))
game_hud.resize((game_hud.width * 4, game_hud.height * 4), Image.NEAREST).save(os.path.join(u4, 'hudstats2.png'))
shutil.copyfile(os.path.join(GAME, 'data', 'gfx', 'ui', 'hudstats2.anm2'), os.path.join(u4, 'hudstats2.anm2'))
z4, n4, rp4 = convert(mod4, 'own')
for line in rp4.splitlines():
    if 'completion' in line or 'hudstats2' in line:
        print('    report: ' + line[:230])
k = n4.get('resources/gfx/ui/completion_widget.anm2')
check(k is not None and b'Mark="own"' in z4.read(k), 'completion_widget.anm2: the mod\'s own, kept')
for f in ('completion_widget.png', 'completion_widget_pause.png'):
    k = n4.get('resources/gfx/ui/' + f)
    check(k is not None and Image.open(io.BytesIO(z4.read(k))).convert('RGBA').tobytes() == own_cw.tobytes(),
          '%s: 393x500 under the mod\'s own anm2 - kept as it is' % f)
k = n4.get('resources/gfx/ui/hudstats2.png')
check(k is not None and Image.open(io.BytesIO(z4.read(k))).size == (game_hud.width * 4, game_hud.height * 4),
      'hudstats2.png x4 under the mod\'s own anm2: not baked down')
check('resources/gfx/ui/hudstats2.anm2' in n4 and 'hudstats2.png x4' in rp4, 'hudstats2.anm2 kept, fitted to its x4 picture (R0)')
check('nothing here for the port' not in rp4 and "the mod's own" in rp4, 'not refused; the report says the files are the mod\'s own')

print('\n%d failures' % len(failures))
sys.exit(1 if failures else 0)
