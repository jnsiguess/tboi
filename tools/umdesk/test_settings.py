# -*- coding: utf-8 -*-
"""Desk cases for mod.json and the mods' settings (plan 30.09, 4.10 parts 1 and 2), on the real usermods.c +
content.c + json.c (tools/umdesk/build.sh).

    bash tools/umdesk/build.sh && bash tools/umdesk/build.sh desk0.exe 0 && py -3 tools/umdesk/test_settings.py

The sample mod is tools/umdesk/sample-settings (a switch, a choice and a number):
  resources/gfx/shared.txt     "base"; blood-tears=on -> "blood"; style=classic/retro -> "classic"/"retro"
                               (style is declared after blood-tears: the stronger of the two)
  options/outline/0 and /2     the number picks the largest folder not above it: 1 -> 0, 3 -> 2
  patch/players.xml            Isaac coins=5; style=retro lays its own patch over it: coins=7 bombs=9

Cases:
  1. defaults: the values, the layers in use, which file the game is handed;
  2. mod-settings.ini: a step writes only what is not the default; stepping back takes the section away;
     RESTART TO APPLY follows the values against the start; a choice goes round, a number stops at its ends;
  3. values from the file: the layers, the later setting the stronger, the number's folder;
  4. a value the mod no longer has: the default, a report line, the file left alone;
  5. lines the port does not know (other sections, unknown keys, comments) survive a write; DELETE takes the
     mod's section away and nothing else;
  6. the second patch slot: an option's patch/ over the mod's own patch/, no conflict between them; content-cache
     rebuilds when a setting changes; the guard's cache is keyed by the layers too;
  7. mod.json: names in the page's language (jp/kr/zh read en), a broken file, a refused setting, check.txt lines
     ("level [code]: text"), a mod that is off still gets them;
  8. shim_usermods_peek answers as map does, with no guard run; a mod's .anm2 for a path the port patches comes
     back with the port's patch (one copy per language set), one that is no .anm2 is the game's;
  9. stringtable.sta with no mod at all: the game's file with the port's keys filled (M8);
 10. the clean build (desk0.exe) hands the same files and prints no usermods report line about settings.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SAMPLE = os.path.join(HERE, 'sample-settings')
GAME = os.path.join(ROOT, 'work', 'app')
ID = 'settings-sample'
failures = []


def check(cond, what, detail=''):
    print(('  ok    ' if cond else '  FAIL  ') + what + ('' if cond or not detail else '\n        ' + detail[:900]))
    if not cond:
        failures.append(what)


def run(cfg, *args, desk='desk.exe', env=None):
    e = {k: v for k, v in os.environ.items() if not k.startswith('DESK_')}
    e['DESK_ROOT'] = os.path.join(cfg, 'root')
    e.update(env or {})
    p = subprocess.run([os.path.join(HERE, desk)] + list(args[:1]) + [cfg] + list(args[1:]), capture_output=True, env=e)
    return p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace')


def fresh(name, extra=None):
    cfg = os.path.join(TOP, name)
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(os.path.join(cfg, 'root'))
    shutil.copytree(SAMPLE, os.path.join(cfg, 'mods', ID))
    for path, data in (extra or {}).items():
        full = os.path.join(cfg, 'mods', *path.split('/'))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        open(full, 'wb').write(data if isinstance(data, bytes) else data.encode('utf-8'))
    return cfg


def ini(cfg):
    p = os.path.join(cfg, 'mods', 'mod-settings.ini')
    return open(p, encoding='utf-8').read() if os.path.exists(p) else None


def put_ini(cfg, text):
    open(os.path.join(cfg, 'mods', 'mod-settings.ini'), 'w', encoding='utf-8', newline='\n').write(text)


def handed(cfg, inner, env=None, desk='desk.exe'):
    out = run(cfg, 'map', inner, env=env, desk=desk)
    m = re.search(r'== map \S.* -> (.*)', out)
    got = m.group(1).strip() if m else ''
    data = open(got, 'rb').read().decode('utf-8', 'replace') if got and os.path.exists(got) else None
    return out, got, data


def sets(out):
    """{key: (value, start)} of the sample mod from a `settings` run"""
    res = {}
    for m in re.finditer(r'   SET %s \d+ kind=\d label="[^"]*" value=(-?\d+) start=(-?\d+)' % ID, out):
        pass
    for line in out.splitlines():
        m = re.match(r'   SET (\S+) (\d+) kind=(\d) label="([^"]*)" value=(-?\d+) start=(-?\d+)', line)
        if m and m.group(1) == ID:
            res[int(m.group(2))] = (int(m.group(5)), int(m.group(6)), m.group(4), line)
    return res


def mod_line(out, mid=ID):
    m = re.search(r'   MOD %s (.*)' % re.escape(mid), out)
    return m.group(1) if m else ''


TOP = os.path.join(tempfile.gettempdir(), 'umdesk-settings')
os.makedirs(TOP, exist_ok=True)

print('1. defaults')
cfg = fresh('defaults')
out = run(cfg, 'settings')
st = sets(out)
check([st[k][0] for k in range(3)] == [0, 1, 1], 'values: blood-tears off, style clean (1), outline 1', out)
check('layers=outline/0' in mod_line(out), 'the layers in use: outline 1 -> folder 0', mod_line(out))
check('changed=0' in out, 'nothing changed: no RESTART TO APPLY', out)
check(ini(cfg) is None, 'mod-settings.ini is not written at the start')
_, _, d = handed(cfg, 'repentance-resources/data/gfx/shared.txt')
check(d == 'base', 'shared.txt: the mod\'s own', str(d))
_, _, d = handed(cfg, 'repentance-resources/data/gfx/outline.txt')
check(d == '0', 'outline.txt: folder 0', str(d))
_, got, d = handed(cfg, 'repentance-resources/data/gfx/tears.txt')
check(d is None, 'tears.txt: blood-tears off - no layer, the game\'s (none)', got)

print('2. the page steps a setting')
out = run(cfg, 'step', '0', '0', '1')
check(ini(cfg) == '[%s]\nblood-tears=on\n' % ID, 'a step writes only what is not the default', repr(ini(cfg)))
check('changed=1' in out, 'RESTART TO APPLY: the value is not the start\'s', out[:400])
out = run(cfg, 'step', '0', '0', '1')
check(ini(cfg) == '', 'stepping back: the section goes (the file stays, empty)', repr(ini(cfg)))
check('changed=0' in out, 'back to the start\'s value: no RESTART TO APPLY', out[:400])
out = run(cfg, 'step', '0', '1', '1', '0', '1', '1', '0', '2', '1', '0', '2', '1', '0', '2', '1')
st = sets(out)
check(st[1][0] == 0 and st[2][0] == 3, 'a choice goes round (clean -> retro -> classic), a number stops at 3', str(st))
check(ini(cfg) == '[%s]\nstyle=classic\noutline=3\n' % ID, 'the file: style=classic, outline=3', repr(ini(cfg)))
out = run(cfg, 'step', '0', '2', '-1', '0', '2', '-1', '0', '2', '-1', '0', '2', '-1', '0', '2', '-1')
check(sets(out)[2][0] == 0, 'a number stops at 0 going down', str(sets(out)))

print('3. values from the file: the layers')
cfg = fresh('layers')
put_ini(cfg, '[%s]\nblood-tears=on\nstyle=retro\noutline=3\n' % ID)
out = run(cfg, 'settings')
check('layers=blood-tears/on,style/retro,outline/2' in mod_line(out), 'layers: blood on, style retro, outline 3 -> folder 2',
      mod_line(out))
_, _, d = handed(cfg, 'repentance-resources/data/gfx/shared.txt')
check(d == 'retro', 'shared.txt: style (declared later) over blood-tears', str(d))
_, _, d = handed(cfg, 'repentance-resources/dlc3.ru/gfx/tears.txt')
check(d == 'blood', 'tears.txt: blood-tears\' layer, for a language set too', str(d))
_, _, d = handed(cfg, 'repentance-resources/data/gfx/outline.txt')
check(d == '2', 'outline.txt: folder 2', str(d))
_, _, d = handed(cfg, 'repentance-resources/data/gfx/sample.txt')
check(d == 'base', 'sample.txt: no layer has it - the mod\'s own', str(d))
put_ini(cfg, '[%s]\nstyle=CLASSIC\n' % ID)
out = run(cfg, 'settings')
check('layers=style/classic,outline/0' in mod_line(out) or 'layers=outline/0' in mod_line(out),
      'a value in other case: read as json.c reads it', mod_line(out))

print('4. a value the mod no longer has')
cfg = fresh('stale')
text = '[%s]\nstyle=sepia\noutline=2\n' % ID
put_ini(cfg, text)
out = run(cfg, 'settings')
st = sets(out)
check(st[1][0] == 1 and st[2][0] == 2, 'style=sepia reads as the default (clean), outline=2 stands', str(st))
check('is not a value of this setting now' in out, 'a report line says so', out[:1500])
check(ini(cfg) == text, 'the file is left as it is')

print('5. lines the port does not know survive')
cfg = fresh('strangers', {'other/mod.json': '{"id": "other", "name": "Other"}'})
text = '; the player\'s own note\n[zeta]\nx=1\n\n[%s]\nunknown=5\nstyle=retro\n# hand\n\n[gone-mod]\ny=2\n' % ID
put_ini(cfg, text)
order = [l.split()[2] for l in run(cfg, 'start').splitlines() if re.match(r'   \d+ (on |off) ', l)]
idx = order.index(ID) if ID in order else -1
out = run(cfg, 'step', str(idx), '0', '1')
want = '; the player\'s own note\n[zeta]\nx=1\n\n[%s]\nblood-tears=on\nstyle=retro\nunknown=5\n# hand\n\n[gone-mod]\ny=2\n' % ID
check(ini(cfg) == want, 'other sections, an unknown key and comments stay; ours rewritten', repr(ini(cfg)))
out = run(cfg, 'remove', str(idx))
check(ini(cfg) == '; the player\'s own note\n[zeta]\nx=1\n\n\n[gone-mod]\ny=2\n'
      or ini(cfg) == '; the player\'s own note\n[zeta]\nx=1\n\n# hand\n\n[gone-mod]\ny=2\n'
      or '[%s]' % ID not in (ini(cfg) or ''), 'DELETE: the mod\'s section goes, the rest stays', repr(ini(cfg)))
check('[zeta]' in (ini(cfg) or '') and '[gone-mod]' in (ini(cfg) or ''), 'DELETE: other sections stay', repr(ini(cfg)))

print('6. the second patch slot; the caches follow the settings')
cfg = fresh('patch')
put_ini(cfg, '[%s]\nstyle=retro\n' % ID)
out, got, d = handed(cfg, 'repentance-resources/data/players.xml')
isaac = re.search(r'<player id="0"[^>]*>', d or '')
isaac = isaac.group(0) if isaac else ''
check('coins="7"' in isaac and 'bombs="9"' in isaac, 'retro: its patch over the mod\'s (coins 7, bombs 9)', isaac)
note = open(os.path.join(cfg, 'mods', ID + '.check.txt'), encoding='utf-8').read() \
    if os.path.exists(os.path.join(cfg, 'mods', ID + '.check.txt')) else ''
check('conflict' not in note and 'conflict' not in out, 'no conflict between a mod\'s own layers', note[:600])
guard = os.path.join(cfg, 'root', '..', 'content-cache', 'guard', ID + '.txt')
head = open(guard, encoding='utf-8').readline().strip() if os.path.exists(guard) else ''
check(head == 'guard 7 style/retro,outline/0', 'the guard\'s cache names the layers', head)
put_ini(cfg, '')
out, got, d = handed(cfg, 'repentance-resources/data/players.xml')
isaac = re.search(r'<player id="0"[^>]*>', d or '')
isaac = isaac.group(0) if isaac else ''
check('coins="5"' in isaac and 'bombs="9"' not in isaac, 'style back at clean: the mod\'s patch alone (coins 5) - rebuilt',
      isaac)
head = open(guard, encoding='utf-8').readline().strip() if os.path.exists(guard) else ''
check(head == 'guard 7 outline/0', 'the guard\'s cache is made again for the new layers', head)
key = open(os.path.join(cfg, 'content-cache', 'data', 'players.xml.txt'), encoding='utf-8').read()
check('layers %s outline/0' % ID in key and 'patch' in key, 'content-cache\'s key: the layers and each file\'s path', key[:500])

print('7. mod.json: languages, refusals, check.txt')
bad_json = '{"id": "broken", "name": "Broken", "settings": [1,],}'
refused = ('{"id": "ref", "name": {"en": "Refuser", "ru": "Отказник"}, "settings": ['
           '{"key": "Bad", "kind": "switch", "label": "x", "default": false},'
           '{"key": "good", "kind": "switch", "label": "Good one", "default": true, "restart": false}]}')
cfg = fresh('manifest', {'broken/mod.json': bad_json, 'broken/resources/gfx/b.txt': 'b',
                         'ref/mod.json': refused, 'ref/resources/gfx/r.txt': 'r',
                         'offmod/mod.json': '{"id": "offmod", "name": "Off", "version": 1.2}',
                         'offmod/resources/gfx/o.txt': 'o'})
open(os.path.join(cfg, 'mods', 'mods.txt'), 'w').write('on %s\non broken\non ref\noff offmod\n' % ID)
for lang, want in (('0', 'Settings Sample'), ('1', 'Пример настроек'), ('2', 'Einstellungsbeispiel'),
                   ('3', 'Settings Sample'), ('4', 'Settings Sample'), ('6', 'Settings Sample')):
    out = run(cfg, 'settings', env={'DESK_LANG': lang})
    check(('name="%s"' % want) in mod_line(out), 'set %s shows "%s"' % (lang, want), mod_line(out))
out = run(cfg, 'settings', env={'DESK_LANG': '1'})
check('label="Кровавые слёзы"' in out and 'shows="Чистая"' in out, 'Russian labels and choice words', out[:900])
# hotfix 1.71 (the owner: leniently): trailing commas are a slip - the mod is read and named, the broken setting off
check('broken=0' in mod_line(out, 'broken') and 'name="Broken"' in mod_line(out, 'broken')
      and 'reason=setting_not_object' in mod_line(out, 'broken'),
      'a mod.json with slips: read leniently, named, the broken setting alone off', mod_line(out, 'broken'))
check('refused=1' in mod_line(out, 'ref') and 'reason=key_bad' in mod_line(out, 'ref') and 'name="Отказник"' in mod_line(out, 'ref'),
      'a refused setting: one, key_bad; the rest of the file read', mod_line(out, 'ref'))
check(len([l for l in out.splitlines() if l.startswith('   SET ref ')]) == 1, 'the refused setting is not on the page')
handed(cfg, 'repentance-resources/data/gfx/r.txt')
note = open(os.path.join(cfg, 'mods', 'ref.check.txt'), encoding='utf-8').read()
check('refused [key_bad]: setting 1' in note and 'note [restart_ignored]: setting 2 "good"' in note,
      'check.txt: "level [code]: text" lines, restart:false not taken', note[:800])
note = open(os.path.join(cfg, 'mods', 'broken.check.txt'), encoding='utf-8').read()
check('note [syntax]:' in note and 'read leniently' in note and 'refused [setting_not_object]' in note,
      'check.txt of the sloppy one: note [syntax], the setting refused', note[:500])
p = os.path.join(cfg, 'mods', 'offmod.check.txt')
note = open(p, encoding='utf-8').read() if os.path.exists(p) else ''
check('note [field_type]' in note or 'note [' in note, 'a mod that is off gets its mod.json lines', note[:500])
check('the mod is off' in note, '... and says it is off', note[:500])

print('8. peek and the port\'s patch over a mod\'s .anm2')
anm = '<AnimatedActor>\n\t<Info/>\n</AnimatedActor>\n'
cfg = fresh('portpatch', {ID + '/resources/gfx/ui/main menu/optionsmenu.anm2': anm,
                          ID + '/resources/gfx/ui/pausescreen.anm2': 'not an animation'})
out = run(cfg, 'peek', 'repentance-resources/data/gfx/ui/main menu/optionsmenu.anm2')
check('optionsmenu.anm2' in out and '(guard ran: 0)' in out and 'port-patched' not in out,
      'peek: the mod\'s own file, no guard run', out[-400:])
env = {'DESK_PORT_PATCH': 'gfx/ui/main menu/optionsmenu.anm2'}
out, got, d = handed(cfg, 'repentance-resources/dlc3.ru/gfx/ui/main menu/optionsmenu.anm2', env=env)
check(d is not None and '<!-- port patch -->' in d and 'port-patched/repentance-resources/dlc3.ru/' in got.replace('\\', '/'),
      'map: the port\'s patch laid over the mod\'s file, the set\'s own copy', got)
out, got, d = handed(cfg, 'repentance-resources/data/gfx/ui/main menu/optionsmenu.anm2')
check(d is not None and '<!-- port patch -->' not in d, 'no port patch for the path: the mod\'s file (as the guard left it)', got)
out, got, d = handed(cfg, 'repentance-resources/data/gfx/ui/pausescreen.anm2', env={'DESK_PORT_PATCH': 'gfx/ui/pausescreen.anm2'})
check(got == "(the game's own)", 'a mod file that is no .anm2: the game\'s (the guard or the patch refuses it)', got)

print('9. stringtable.sta with no mod at all')
pack = os.path.join(TOP, 'pack') + os.sep
d0 = os.path.join(pack, 'repentance-resources', 'data')
os.makedirs(d0, exist_ok=True)
van = os.path.join(GAME, 'repentance-resources', 'data', 'stringtable.sta.orig')
shipped = os.path.join(GAME, 'repentance-resources', 'data', 'stringtable.sta')
if os.path.exists(van):
    shutil.copy(van, os.path.join(d0, 'stringtable.sta'))
    cfg = os.path.join(TOP, 'nomods')
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(os.path.join(cfg, 'root'))
    out, got, d = handed(cfg, 'repentance-resources/data/stringtable.sta', env={'DESK_PACK': pack.replace('\\', '/')})
    check(got != "(the game's own)" and open(got, 'rb').read() == open(shipped, 'rb').read(),
          'no mods: the vanilla file + the port\'s keys = the shipped stringtable.sta', got + out[-600:])
else:
    print('  (no vanilla stringtable.sta.orig - skipped)')

print('10. the clean build')
cfg = fresh('clean')
put_ini(cfg, '[%s]\nstyle=retro\noutline=3\n' % ID)
o1, g1, d1 = handed(cfg, 'repentance-resources/data/gfx/shared.txt', desk='desk0.exe')
o2, g2, d2 = handed(cfg, 'repentance-resources/data/gfx/outline.txt', desk='desk0.exe')
check(d1 == 'retro' and d2 == '2', 'desk0: the same layers', '%s %s' % (d1, d2))
check("settings' layers in use" not in o1 and 'written (' not in o1, 'desk0: no NOTE line (the report lines are the run\'s)', o1[:500])

print('\n%d failures' % len(failures))
for f in failures:
    print('  - ' + f)
sys.exit(1 if failures else 0)
