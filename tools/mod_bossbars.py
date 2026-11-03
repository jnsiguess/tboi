# -*- coding: utf-8 -*-
"""Turn the "Enhanced Boss Bars" mod into port assets and a C table.

The mod's pictures are loaded by the game's sprite loader, and the mod swaps a
sprite's sheet at run time (ReplaceSpritesheet + LoadGraphics). The port does
not call that: for every sheet a bar or an icon can wear, an .anm2 with that
sheet already written in is made here, next to the picture, so loading the
sprite IS choosing the sheet.

Writes:
  port/app/src/main/assets/port_bossbars/bosshp_bars/...    the mod's bar tree
  port/app/src/main/assets/port_bossbars/bosshp_icons/...   the mod's icon tree
        (plus the generated port_*.anm2 beside the pictures they show)
  port/app/src/main/cpp/bossbars_data.h                     styles, bosses,
                                                            icon conditions

The assets land in the resource table as gfx/ui/bosshp_bars/... and
gfx/ui/bosshp_icons/..., the paths the mod itself uses.

Every style, both orientations and the notches are made; which of them show
is the [bossbars] section of port-touch.ini (modset.c).
"""
import os
import re
import shutil
import sys

MOD = r'Z:\Games\Steam\steamapps\common\The Binding of Isaac Rebirth\mods\enhanced boss bars_2635267643'
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.join(PROJECT, 'port', 'app', 'src', 'main')
OUT = os.path.join(ROOT, 'assets', 'port_bossbars')
HEADER = os.path.join(ROOT, 'cpp', 'bossbars_data.h')

ICON_PATH = 'gfx/ui/bosshp_icons/'
BAR_PATH = 'gfx/ui/bosshp_bars/'


# ----------------------------------------------------------------- Lua reader
class Func:
    """A Lua function literal, kept as its text."""
    def __init__(self, text):
        self.text = text


TOKEN = re.compile(r'''
    (?P<ws>\s+|--\[\[.*?\]\]--|--\[\[.*?\]\]|--[^\n]*)
  | (?P<str>"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')
  | (?P<num>-?\d+(?:\.\d+)?)
  | (?P<op>\.\.|==|~=|<=|>=|[{}\[\]=,();:.<>+\-*/%#])
  | (?P<name>[A-Za-z_][A-Za-z_0-9]*)
''', re.S | re.X)


def tokens(text):
    at = 0
    out = []
    while at < len(text):
        m = TOKEN.match(text, at)
        if not m:
            raise ValueError('cannot read Lua at %r' % text[at:at + 40])
        at = m.end()
        kind = m.lastgroup
        if kind == 'ws':
            continue
        out.append((kind, m.group(kind), m.start(), m.end()))
    return out


class Reader:
    def __init__(self, text, env):
        self.text = text
        self.t = tokens(text)
        self.i = 0
        self.env = env

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else (None, None, 0, 0)

    def take(self, value=None):
        tok = self.t[self.i]
        if value is not None and tok[1] != value:
            raise ValueError('expected %r, got %r near %r' % (value, tok[1], self.text[tok[2]:tok[2] + 60]))
        self.i += 1
        return tok

    def function(self):
        start = self.take('function')[2]
        depth = 1
        openers = {'function', 'if', 'do', 'while', 'for', 'repeat'}
        while depth:
            kind, value, _, end = self.take()
            if kind == 'name' and value in openers and value != 'for' and value != 'while':
                depth += 1
            elif kind == 'name' and value in ('end', 'until'):
                depth -= 1
        return Func(self.text[start:end])

    def value(self):
        left = self.term()
        while self.peek()[1] == '..':
            self.take('..')
            left = left + self.term()
        return left

    def term(self):
        kind, value, _, _ = self.peek()
        if kind == 'str':
            self.take()
            return value[1:-1]
        if kind == 'num':
            self.take()
            return float(value) if '.' in value else int(value)
        if value == '{':
            return self.table()
        if value == 'function':
            return self.function()
        if kind == 'name':
            if value in ('true', 'false', 'nil'):
                self.take()
                return {'true': True, 'false': False, 'nil': None}[value]
            if value in ('Vector', 'Color'):
                self.take()
                self.take('(')
                args = []
                while self.peek()[1] != ')':
                    args.append(self.value())
                    if self.peek()[1] == ',':
                        self.take(',')
                self.take(')')
                return (value, args)
            # a dotted name: path, barPath, HPBars.BarColorings.none ...
            parts = [self.take()[1]]
            while self.peek()[1] == '.':
                self.take('.')
                parts.append(self.take()[1])
            return self.env(parts)
        raise ValueError('unexpected %r' % value)

    def table(self):
        self.take('{')
        entries = {}
        index = 1
        while self.peek()[1] != '}':
            if self.peek()[1] == '[':
                self.take('[')
                key = self.value()
                self.take(']')
                self.take('=')
                entries[key] = self.value()
            elif self.peek()[0] == 'name' and self.peek(1)[1] == '=':
                key = self.take()[1]
                self.take('=')
                entries[key] = self.value()
            else:
                entries[index] = self.value()
                index += 1
            if self.peek()[1] in (',', ';'):
                self.take()
        self.take('}')
        return entries


def lua_table(text, name, env):
    m = re.search(r'^HPBars\.%s\s*=\s*' % re.escape(name), text, re.M)
    if not m:
        sys.exit('no HPBars.%s in the data file' % name)
    r = Reader(text[m.end():], env)
    return r.table()


# ------------------------------------------------------------------ the model
def main():
    data = open(os.path.join(MOD, 'bossbar_data.lua'), encoding='utf-8').read()
    colorings = {}

    def env(parts):
        if parts == ['path']:
            return ICON_PATH
        if parts == ['barPath']:
            return BAR_PATH
        if parts[:2] == ['HPBars', 'BarColorings']:
            return colorings[parts[2]]
        if parts[:2] == ['HPBars', 'ColoringFunctions']:
            return parts[2]
        # the game's own enums the data leans on (the API's values)
        known = {'StageType.STAGETYPE_REPENTANCE': 4, 'StageType.STAGETYPE_REPENTANCE_B': 5,
                 'LevelStage.STAGE4_1': 7, 'LevelStage.STAGE4_2': 8}
        if '.'.join(parts) in known:
            return known['.'.join(parts)]
        raise ValueError('unknown name %s' % '.'.join(parts))

    for k, v in lua_table(data, 'BarColorings', env).items():
        colorings[k] = v
    styles = lua_table(data, 'BarStyles', env)
    bosses = lua_table(data, 'BossDefinitions', env)

    src = os.path.join(MOD, 'resources', 'gfx', 'ui')
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(os.path.join(src, 'bosshp_bars'), os.path.join(OUT, 'bosshp_bars'))
    shutil.copytree(os.path.join(src, 'bosshp_icons'), os.path.join(OUT, 'bosshp_icons'))
    # sheet paths inside the mod's own anm2 files use backslashes in places
    for folder, _, files in os.walk(OUT):
        for name in files:
            if name.endswith('.anm2'):
                p = os.path.join(folder, name)
                t = open(p, encoding='utf-8').read()
                t2 = re.sub(r'(Path=")([^"]*)(")', lambda m: m.group(1) + m.group(2).replace('\\', '/') + m.group(3), t)
                if t2 != t:
                    open(p, 'w', encoding='utf-8', newline='\n').write(t2)

    def local(game_path):
        """gfx/ui/bosshp_icons/a/b.png -> the file under OUT"""
        rel = game_path[len('gfx/ui/'):]
        return os.path.join(OUT, rel.replace('/', os.sep))

    made = {}

    def with_sheet(anm2_game, sheet_game, tag):
        """an .anm2 like anm2_game, its every sheet replaced by sheet_game;
        written beside the sheet, answered as a game path"""
        key = (anm2_game, sheet_game)
        if key in made:
            return made[key]
        sheet_dir, sheet_leaf = sheet_game.rsplit('/', 1)
        if not os.path.isfile(local(sheet_game)):
            return None
        text = open(local(anm2_game), encoding='utf-8').read()
        text = re.sub(r'(<Spritesheet\s+Path=")[^"]*(")', lambda m: m.group(1) + sheet_leaf + m.group(2), text)
        base = os.path.splitext(sheet_leaf)[0]
        kind = os.path.splitext(anm2_game.rsplit('/', 1)[1])[0]
        leaf = 'port_%s__%s__%s.anm2' % (tag, re.sub(r'[^A-Za-z0-9_]', '_', base), kind)
        open(os.path.join(local(sheet_dir), leaf), 'w', encoding='utf-8', newline='\n').write(text)
        made[key] = sheet_dir + '/' + leaf
        return made[key]

    default = styles['Default']

    def style_of(name):
        merged = dict(default)
        merged.update(styles.get(name, {}))
        return merged

    def colour(c):
        if isinstance(c, tuple):
            args = [float(a) for a in c[1]] + [0.0] * 7
            return args[:7]
        return [1, 1, 1, 1, 0, 0, 0]

    # every style of the mod, in its own order: the config picks one by name,
    # and a boss definition may name one of its own
    style_names = list(styles.keys())
    style_rows = []

    def notch_of(s):
        if not s.get('notchAnm2') or s['notchAnm2'] == 'NONE':
            return None
        if s.get('notchSprite'):
            return with_sheet(s['notchAnm2'], s['notchSprite'], 'notch')
        return s['notchAnm2']

    for name in style_names:
        s = style_of(name)
        bar = with_sheet(s['barAnm2'], s['sprite'], 'bar')
        bar_v = with_sheet(s.get('verticalAnm2') or s['barAnm2'],
                           s.get('verticalSprite') or s['sprite'], 'barv')
        overlay = overlay_v = None
        if s.get('overlayAnm2'):
            overlay = (with_sheet(s['overlayAnm2'], s['overlaySprite'], 'overlay')
                       if s.get('overlaySprite') else s['overlayAnm2'])
        if s.get('verticalOverlayAnm2') or s.get('overlayAnm2'):
            anm2 = s.get('verticalOverlayAnm2') or s['overlayAnm2']
            sheet = s.get('verticalOverlaySprite') or s.get('overlaySprite')
            overlay_v = with_sheet(anm2, sheet, 'overlayv') if sheet else anm2
        style_rows.append({
            'name': name, 'bar': bar, 'bar_v': bar_v,
            'bar_animated': s.get('barAnimationType') == 'Animated',
            'overlay': overlay, 'overlay_v': overlay_v,
            'overlay_animated': s.get('overlayAnimationType') == 'Animated',
            'notch': notch_of(s),
            'notch_animated': s.get('notchAnimationType') == 'Animated',
            'colours': [colour(s['idleColoring']), colour(s['hitColoring']),
                        colour(s['healColoring']), colour(s['invincibleColoring'])],
            'icon': s['defaultIcon'],
        })

    icons = []
    icon_index = {}

    def icon(sheet, anm2):
        anm2 = anm2 or ICON_PATH + 'bosshp_icon_32px.anm2'
        path = with_sheet(anm2, sheet, 'icon')
        if path is None:
            print('  missing picture, skipped:', sheet)
            return -1
        if path not in icon_index:
            icon_index[path] = len(icons)
            icons.append(path)
        return icon_index[path]

    CONDITION = {
        'isHeadSegment': 'BB_HEAD', 'isMiddleSegment': 'BB_MIDDLE', 'isTailSegment': 'BB_TAIL',
        'isChild': 'BB_CHILD', 'isI1Equal': 'BB_I1_EQUAL', 'isI2Equal': 'BB_I2_EQUAL',
        'isHPSmaller': 'BB_HP_BELOW', 'isHPSmallerPercent': 'BB_HP_PERCENT_BELOW',
        'animationNameStartsWith': 'BB_ANIM_STARTS', 'animationNameEndsWith': 'BB_ANIM_ENDS',
        'animationNameContains': 'BB_ANIM_CONTAINS', 'animationNameEqual': 'BB_ANIM_EQUAL',
        'isStageType': 'BB_STAGE_TYPE', 'isAbsoluteStage': 'BB_STAGE',
    }

    def inline(func):
        """the three hand-written conditions of the Tuff Twins"""
        t = re.sub(r'\s+', ' ', func.text)
        parent_nil = 'entity.Parent == nil' in t
        parent_set = 'entity.Parent ~= nil' in t
        child_set = 'entity.Child ~= nil' in t
        child_nil = 'entity.Child == nil' in t
        i2 = re.search(r'I2 == (\d+)', t)
        if not i2:
            sys.exit('an inline condition this tool does not know: ' + t)
        if parent_nil and child_set:
            return 'BB_HEAD_I2', int(i2.group(1))
        if parent_set and child_set:
            return 'BB_MIDDLE_I2', int(i2.group(1))
        if parent_set and child_nil:
            return 'BB_TAIL_I2', int(i2.group(1))
        sys.exit('an inline condition this tool does not know: ' + t)

    boss_rows = []
    conds = []
    boss_colours = []                      # every boss's colour suffixes, one after another
    variants = []                          # (icon, suffix, the icon in that colour)

    def colour_variants(png, anm2, colours):
        base = icon(png, anm2)
        for suffix in colours:
            coloured = png.replace('.png', suffix + '.png')
            if not os.path.isfile(local(coloured)):
                continue                   # a few the mod names are not there; the plain one stays
            v = (base, suffix, icon(coloured, anm2))
            if v not in variants:
                variants.append(v)

    for key, d in bosses.items():
        if key == 'UNDEFINED' or not isinstance(d, dict):
            continue
        t, v = key.split('.')
        style = style_names.index(d['barStyle']) if isinstance(d.get('barStyle'), str) else -1
        anm2 = d.get('iconAnm2')
        sheet = d.get('sprite')
        colours = [c for _, c in sorted((d.get('bossColors') or {}).items())]
        first = len(conds)
        for c in (d.get('conditionalSprites') or {}).values():
            fn, png = c[1], c[2]
            args = c.get(3) or {}
            arg = args.get(1) if isinstance(args, dict) else None
            if isinstance(fn, Func):
                kind, number = inline(fn)
                conds.append((kind, number, None, icon(png, anm2)))
            else:
                conds.append((CONDITION[fn], arg if isinstance(arg, (int, float)) else 0,
                              arg if isinstance(arg, str) else None, icon(png, anm2)))
            colour_variants(png, anm2, colours)
        if sheet:
            colour_variants(sheet, anm2, colours)
        off = d.get('offset') or ('Vector', [-4, 0])
        colour_first = len(boss_colours)
        boss_colours.extend(colours)
        boss_rows.append({
            'type': int(t), 'variant': int(v), 'style': style,
            'icon': icon(sheet, anm2) if sheet else -1,
            'icon_animated': d.get('iconAnimationType') == 'Animated',
            'ignore_invincible': bool(d.get('ignoreInvincible')),
            'lokii': isinstance(d.get('sorting'), Func),
            'segmented': bool(d.get('forceSegmentation')),
            'off': (float(off[1][0]), float(off[1][1])),
            'cond_first': first, 'cond_count': len(conds) - first,
            'colour_first': colour_first, 'colour_count': len(colours),
        })
    boss_rows.sort(key=lambda r: (r['type'], r['variant']))
    style_icons = [icon(s['icon'], None) for s in style_rows]

    dirs = sorted({os.path.relpath(f, OUT).replace(os.sep, '/') for f, _, files in os.walk(OUT) if files})

    def c_str(s):
        return 'NULL' if s is None else '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')

    def fl(x):
        text = '%g' % x
        if '.' not in text and 'e' not in text:
            text += '.0'
        return text + 'f'

    def floats(values):
        return '{ %s }' % ', '.join(fl(x) for x in values)

    h = ['/* generated by tools/mod_bossbars.py from the Enhanced Boss Bars mod - do not edit */',
         '#ifndef BOSSBARS_DATA_H', '#define BOSSBARS_DATA_H', '',
         'enum {', '    BB_HEAD, BB_MIDDLE, BB_TAIL, BB_CHILD, BB_I1_EQUAL, BB_I2_EQUAL,',
         '    BB_HP_BELOW, BB_HP_PERCENT_BELOW, BB_ANIM_STARTS, BB_ANIM_ENDS, BB_ANIM_CONTAINS,',
         '    BB_ANIM_EQUAL, BB_STAGE_TYPE, BB_STAGE, BB_HEAD_I2, BB_MIDDLE_I2, BB_TAIL_I2,', '};', '',
         '/* colours: idle, hit, heal, invincible; each R G B A and the offsets RO GO BO */',
         'typedef struct {',
         '    const char *name;',
         '    const char *bar, *bar_vertical, *overlay, *overlay_vertical, *notch;',
         '    unsigned char bar_animated, overlay_animated, notch_animated;',
         '    short icon;                  /* the default icon of the style */',
         '    float colour[4][7];',
         '} bb_style_t;', '',
         '/* style -1: the configured one; icon -1: the default icon of the style */',
         'typedef struct {',
         '    unsigned short type, variant;',
         '    short style, icon;',
         '    unsigned char icon_animated, ignore_invincible, lokii, segmented;',
         '    float off_x, off_y;',
         '    short cond_first, cond_count;',
         '    short colour_first, colour_count;',
         '} bb_boss_t;', '',
         'typedef struct { unsigned char kind; float number; const char *text; short icon; } bb_cond_t;',
         'typedef struct { short icon; const char *suffix; short coloured; } bb_variant_t;', '']
    h.append('static const char *const BB_ICON[] = {')
    h += ['    %s,' % c_str(p) for p in icons]
    h += ['};', '#define BB_ICONS %d' % len(icons),
          '#define BB_PLAIN_ICON %s' % c_str(ICON_PATH + 'bosshp_icon_32px.anm2'), '']
    h.append('static const bb_style_t BB_STYLE[] = {')
    for st, si in zip(style_rows, style_icons):
        h.append('    { %s, %s, %s, %s, %s, %s, %d, %d, %d, %d, { %s } },' % (
            c_str(st['name']), c_str(st['bar']), c_str(st['bar_v']), c_str(st['overlay']),
            c_str(st['overlay_v']), c_str(st['notch']), st['bar_animated'], st['overlay_animated'],
            st['notch_animated'], si, ', '.join(floats(c) for c in st['colours'])))
    h += ['};', '#define BB_STYLES %d' % len(style_rows), '']
    h.append('static const char *const BB_COLOUR[] = {')
    h += ['    %s,' % c_str(c) for c in boss_colours] or ['    NULL,']
    h += ['};', '']
    h.append('static const bb_variant_t BB_VARIANT[] = {')
    h += ['    { %d, %s, %d },' % (a, c_str(sfx), b) for a, sfx, b in variants] or ['    { -1, NULL, -1 },']
    h += ['};', '#define BB_VARIANTS %d' % len(variants), '']
    h.append('static const bb_cond_t BB_COND[] = {')
    h += ['    { %s, %s, %s, %d },' % (k, fl(n), c_str(tx), ic) for k, n, tx, ic in conds] \
        or ['    { 0, 0.0f, NULL, -1 },']
    h += ['};', '']
    h.append('/* sorted by type, then variant */')
    h.append('static const bb_boss_t BB_BOSS[] = {')
    for r in boss_rows:
        h.append('    { %d, %d, %d, %d, %d, %d, %d, %d, %s, %s, %d, %d, %d, %d },' % (
            r['type'], r['variant'], r['style'], r['icon'], r['icon_animated'], r['ignore_invincible'],
            r['lokii'], r['segmented'], fl(r['off'][0]), fl(r['off'][1]), r['cond_first'],
            r['cond_count'], r['colour_first'], r['colour_count']))
    h += ['};', '#define BB_BOSSES %d' % len(boss_rows), '']
    h.append('/* asset folders under port_bossbars/, each shown as gfx/ui/<folder> */')
    h.append('static const char *const BB_DIRS[] = {')
    h += ['    %s,' % c_str(d) for d in dirs]
    h += ['};', '#define BB_DIR_COUNT %d' % len(dirs), '', '#endif', '']
    open(HEADER, 'w', encoding='utf-8', newline='\n').write('\n'.join(h))
    print('%d styles, %d bosses, %d conditions, %d icon sprites, %d colour variants, %d folders' % (
        len(style_rows), len(boss_rows), len(conds), len(icons), len(variants), len(dirs)))


if __name__ == '__main__':
    main()
