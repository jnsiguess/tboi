# -*- coding: utf-8 -*-
"""THE GUIDE'S FIT CHECKER, for one language's words (the owner, 06.10.2026: the
slides rewritten natively in every language, not translated from English).

Read only: it never writes the assets, the header or the shared previews. It
takes a language file - a Python module with TEXT, a dict of the same shape as
one set of tools/guide_text.py - lays every slide out exactly as
tools/guide_slides.py does, and reports per slot:

    FAIL  a letter the slot's font has not got, or a pad button {NAME} the generator
          has no picture for (guide_slides.PAD_ICON_CELLS)
    FAIL  a line wider than the slot's room, or more lines than the slot holds
    FAIL  a slide that does not build (overlap, off the paper, the note > 3 lines,
          a table taller than the paper) - the generator's own checks
    WARN  lowercase in a slot the Latin and Cyrillic sets letter in capitals

    py -3 tools/guide_check.py ru tools/guide_lang/ru.py
    py -3 tools/guide_check.py ru tools/guide_lang/ru.py --png C:/scratch/ru.png
    py -3 tools/guide_check.py ru tools/guide_lang/ru.py --budget     # every slot's room, and its use
    py -3 tools/guide_check.py ru --current                           # the words guide_text.py has now
    py -3 tools/guide_check.py ru --current --family=ps               # one family of pad pictures only

Every slot and slide is checked in every family of pad pictures (guide_slides.FAMILIES:
xbox, ps, switch - a button is as wide as its family draws it); --png then writes the
xbox preview to the path given and the others beside it, <path>-ps.png, <path>-switch.png.

The language is one of en ru de es jp kr zh (the set keys: en = 'data', the rest
'dlc3.<lang>'). --png writes the 19 slides, two a row, at two pixels a unit, to
the path given and nowhere else; a slide that does not build stays blank. Safe to
run side by side for several languages: nothing is shared, no bytecode is written.
Exit status 0 when nothing FAILs.
"""
import sys
sys.dont_write_bytecode = True          # several checkers at once: no .pyc races in tools/__pycache__

import importlib.util
import io
import os

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import guide_slides as gs               # noqa: E402
import guide_text                       # noqa: E402
import lang                             # noqa: E402
import spawner_common as sc             # noqa: E402
import spawner_tabs                     # noqa: E402
import tweaks_page as tp                # noqa: E402
from PIL import Image                   # noqa: E402

SET_OF = {'en': 'data', 'ru': 'dlc3.ru', 'de': 'dlc3.de', 'es': 'dlc3.es',
          'jp': 'dlc3.jp', 'kr': 'dlc3.kr', 'zh': 'dlc3.zh'}

# the shape of a set: slot -> (kind, count); kinds:
#   str      one string
#   strs     a list of `count` strings
#   pairs    a list of `count` two-string pairs
#   entries  a list of `count` entries, each a string or a list of strings (lines written out)
#   desc     two lists of strings (the two columns of ITEM DESCRIPTIONS)
SHAPE = [
    ('titles', 'strs', 10), ('move_shoot', 'strs', 2), ('types', 'strs', 3), ('modes', 'strs', 3),
    ('dtap', 'strs', 4), ('buttons', 'strs', 7), ('special', 'pairs', 4), ('map', 'pairs', 4),
    ('desc', 'desc', 2), ('pad', 'strs', 8), ('shoot_key', 'str', 1), ('tweaks', 'entries', 6),
    ('aimlock_release', 'str', 1), ('aimlock_note', 'str', 1), ('tweaks2_title', 'str', 1),
    ('tweaks2', 'entries', 6), ('tweaks_notes', 'strs', 2), ('mods2_title', 'str', 1),
    ('mods', 'entries', 8), ('mods_notes', 'strs', 2), ('user_key', 'str', 1), ('user', 'entries', 3),
    ('page_title', 'str', 1), ('page', 'entries', 6), ('user_notes', 'strs', 2), ('coop_title', 'str', 1),
    ('coop_keys', 'strs', 5), ('coop', 'strs', 5), ('coop_note', 'str', 1), ('spawn_title', 'str', 1),
    ('spawn_tabs', 'strs', 4), ('spawn_rows', 'strs', 7), ('spawn_notes', 'strs', 2), ('notes', 'strs', 10),
]
# lettered in capitals in the Latin and Cyrillic sets (as the English is)
CAPS = {'titles', 'move_shoot', 'types', 'modes', 'buttons', 'special', 'desc', 'shoot_key', 'tweaks2_title',
        'mods2_title', 'user_key', 'page_title', 'coop_title', 'coop_keys', 'spawn_title'}

# the slides in their order: (what it is, the slots on it)
SLIDES = ['MOVE AND SHOOT', 'STICK TYPES', 'TOUCH MODES', 'DOUBLE TAP', 'BUTTONS', 'SPECIAL BUTTONS',
          'MAP AND ITEMS', 'ITEM DESCRIPTIONS', 'YOUR LAYOUT', 'GAMEPAD & KEYBOARD', 'LOCAL CO-OP',
          'TWEAKS', 'MORE TWEAKS', 'CONSOLE', 'SPAWNER CONTROLS', 'MODS', 'MORE MODS', 'YOUR MODS',
          "A MOD'S PAGE"]

# where things stand (guide_slides.py); kept here by name so the report can say why
TITLE_MAX = 360             # build(): a title wider than this is refused
LEGEND_LEFT = 92            # dtap / map legends: right of the picture (inside=(92, 356))
RIGHT = 355                 # Slide.check: nothing past the right arrow
NOTE_LINES = 3


# ------------------------------------------------------------------ loading
def load_text(path):
    spec = importlib.util.spec_from_file_location('guide_lang_under_check', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if not hasattr(mod, 'TEXT') or not isinstance(mod.TEXT, dict):
        raise SystemExit('%s: no TEXT dict' % path)
    return mod.TEXT


def is_str(v):
    return isinstance(v, str)


def shape_errors(t):
    out = []
    for slot, kind, n in SHAPE:
        if slot not in t:
            out.append('%s: missing' % slot)
            continue
        v = t[slot]
        if kind == 'str':
            if not is_str(v):
                out.append('%s: must be a string' % slot)
            continue
        if not isinstance(v, (list, tuple)) or len(v) != n:
            out.append('%s: must be a list of %d' % (slot, n))
            continue
        for i, e in enumerate(v):
            where = '%s[%d]' % (slot, i)
            if kind == 'strs' and not is_str(e):
                out.append('%s: must be a string' % where)
            elif kind == 'pairs' and not (isinstance(e, (list, tuple)) and len(e) == 2 and all(map(is_str, e))):
                out.append('%s: must be a pair of strings' % where)
            elif kind in ('entries', 'desc') and not (is_str(e) and kind == 'entries' or
                                                     isinstance(e, (list, tuple)) and e and all(map(is_str, e))):
                out.append('%s: must be %s' % (where, 'a string or a list of strings' if kind == 'entries'
                                               else 'a list of strings'))
    for slot in t:
        if slot not in dict((s, k) for s, k, _ in SHAPE):
            out.append('%s: not a slot of the guide' % slot)
    tw = t.get('tweaks')
    if isinstance(tw, (list, tuple)) and tw and not is_str(tw[0]):
        out.append('tweaks[0]: must be a string (aimlock_release is its second line)')
    for i, b in enumerate(t.get('buttons') or []):
        if is_str(b) and len(gs.gesture_and_deed(b)) != 2:
            out.append('buttons[%d]: "GESTURE: WHAT IT DOES" - the colon splits it into two lines' % i)
    return out


# ------------------------------------------------------------------ the checks
class Report(object):
    def __init__(self):
        self.rows = []                      # (level, slot, message)

    def add(self, level, slot, msg):
        self.rows.append((level, slot, msg))

    def fails(self):
        return [r for r in self.rows if r[0] == 'FAIL']


def chars_of_title_face(set_name):
    """the code points the title lettering of a set can draw (CJK: tp.word's face)"""
    s = lang.SETTINGS[set_name]
    path = os.path.join(s.get('folder', lang.FONTS), s['font'])
    return sc._ttf_chars(path) if path.lower().endswith('.ttf') else sc._bmfont_chars(path)


class Checker(object):
    def __init__(self, code, text, budget=False):
        self.code = code
        self.set_name = SET_OF[code]
        self.t = text
        self.pen = gs.Pen(self.set_name)
        self.pen.text = text
        self.sheet = gs.load(gs.gp.MENU, 'collectionmenu.png')
        self.r = Report()
        self.budget = budget
        self.lines = []                     # the --budget table: (slot, text, used, room, lines, most lines)
        self.title_chars = None if self.pen.big is not None else chars_of_title_face(self.set_name)
        self.note_width = gs.Slide(self.sheet).note.width - 44

    # ---- measuring
    def w(self, text, font=None):
        return (font or self.pen.small).draw(text).width if text else 0

    def missing(self, slot, text, font=None):
        font = font or self.pen.small
        for u in gs.units(text):                 # a pad button {NAME}: one the generator can draw?
            if gs.is_icon(u) and u[1:-1] not in gs.PAD_ICON_CELLS:
                self.r.add('FAIL', slot, 'no pad button %s (there are %s)'
                           % (u, ' '.join(sorted(gs.PAD_ICON_CELLS))))
                return True
        miss = sorted({ch for ch in gs.without_icons(text) if ch != ' ' and ord(ch) not in font.font.chars})
        if miss:
            self.r.add('FAIL', slot, 'the font %s has no %s (in %r)' % (font.name, ''.join(miss), text))
        return bool(miss)

    def caps(self, slot, text):
        if self.set_name in ('data', 'dlc3.ru', 'dlc3.de', 'dlc3.es') and any(c.islower() for c in text):
            self.r.add('WARN', slot, 'lowercase in a slot lettered in capitals: %r' % text)

    def line(self, slot, text, room, font=None, most=1):
        """one line that must not be wider than `room`"""
        if self.missing(slot, text, font):
            return
        used = self.w(text, font)
        self.lines.append((slot, text, used, room, 1, most))
        if used > room:
            self.r.add('FAIL', slot, '%r is %d wide, %d at most' % (text, used, room))

    def wrapped(self, slot, texts, width, most, font=None):
        """texts broken at `width` the way the slide breaks them; at most `most` lines in all"""
        out = []
        for text in texts:
            if self.missing(slot, text, font):
                return []
            out += self.pen.wrap(text, width, font)
        for text in texts:
            self.lines.append((slot, text, self.w(text, font), width, len(self.pen.wrap(text, width, font)), most))
        if len(out) > most:
            self.r.add('FAIL', slot, '%d lines, %d at most (each line %d wide at most): %s'
                       % (len(out), most, width, ' | '.join(out)))
        for l in out:
            if self.w(l, font) > width:
                self.r.add('WARN', slot, 'a word wider than the column (%d): %r - the column widens for it, '
                           'the slide build says whether the row still fits' % (width, l))
        return out

    def title(self, slot, text):
        self.caps(slot, text)
        if self.pen.big is not None:
            if self.missing(slot, text, self.pen.big):
                return
        else:
            miss = sorted({ch for ch in text if ch != ' ' and ord(ch) not in self.title_chars})
            if miss:
                self.r.add('FAIL', slot, 'the title lettering has no %s (in %r)' % (''.join(miss), text))
                return
        used = self.pen.title(text).width
        self.lines.append((slot, text, used, TITLE_MAX, 1, 1))
        if used > TITLE_MAX:
            self.r.add('FAIL', slot, 'the title %r is %d wide, %d at most' % (text, used, TITLE_MAX))

    def note(self, slot, text):
        if self.missing(slot, text):
            return
        lines = self.pen.wrap(text, self.note_width)
        self.lines.append((slot, text, self.w(text), self.note_width, len(lines), NOTE_LINES))
        if len(lines) > NOTE_LINES:
            self.r.add('FAIL', slot, 'the note takes %d lines, %d at most: %s'
                       % (len(lines), NOTE_LINES, ' | '.join(lines)))

    def legend(self, slot, keys, texts, total_max, key_slot=None):
        """rows of KEY - what it does, as one centred block (guide_slides.legend)"""
        bad = False
        for i, k in enumerate(keys):
            if key_slot:
                self.caps('%s[%d]' % (key_slot, i), k)
            bad |= self.missing('%s[%d]' % (key_slot or slot, i), k)
        for i, t in enumerate(texts):
            bad |= self.missing('%s[%d]' % (slot, i), t)
        if bad:
            return
        key_w = max(self.w(k) for k in keys)
        room = total_max - key_w - 8
        for i, t in enumerate(texts):
            used = self.w(t)
            self.lines.append(('%s[%d]' % (slot, i), t, used, room, 1, 1))
            if used > room:
                self.r.add('FAIL', '%s[%d]' % (slot, i), '%r is %d wide, %d at most beside keys %d wide'
                           % (t, used, room, key_w))

    def table(self, slot, keys, entries, first=0):
        """a Tweaks / mods table (guide_slides.table): the keys are the menu's own words,
        an entry's line breaks at the room left beside the widest key"""
        f = self.pen.small
        key_lines = [gs.column_lines(self.pen, gs.lines_of(k), gs.KEY_MAX) for k in keys]
        key_w = max(f.draw(k).width for ks in key_lines for k in ks)
        room = gs.INSIDE[1] - gs.INSIDE[0] - 4 - key_w - 8
        for i, e in enumerate(entries):
            where = '%s[%d]' % (slot, first + i)
            if slot == 'tweaks' and i == 0:
                where = 'tweaks[0] + aimlock_release'
            lines = gs.lines_of(e)
            if any(self.missing(where, l) for l in lines):
                continue
            broken = gs.column_lines(self.pen, lines, room)
            for l in lines:
                self.lines.append((where, l, self.w(l), room, len(self.pen.wrap(l, room)), 2))
            if len(broken) > 2:
                self.r.add('FAIL', where, 'takes %d lines beside its key, 2 at most (%d wide each): %s'
                           % (len(broken), room, ' | '.join(broken)))
            elif len(broken) > len(lines):
                self.r.add('WARN', where, 'broken by the tool at %d: %s - break it yourself (a list of '
                           'two strings) or shorten it' % (room, ' | '.join(broken)))

    # ---- the slots, slide by slide
    def run_slots(self):
        t, pen = self.t, self.pen
        for i, s in enumerate(t['titles']):
            self.title('titles[%d]' % i, s)
        for slot in ('coop_title', 'tweaks2_title', 'spawn_title', 'mods2_title', 'page_title'):
            self.title(slot, t[slot])
        for i, n in enumerate(t['notes']):
            self.note('notes[%d]' % i, n)
        self.note('coop_note', t['coop_note'])
        joint = '' if self.set_name in gs.UNSPACED else ' '
        self.note('tweaks_notes[0] + aimlock_note', t['tweaks_notes'][0] + joint + t['aimlock_note'])
        self.note('tweaks_notes[1]', t['tweaks_notes'][1])
        for slot in ('spawn_notes', 'mods_notes', 'user_notes'):
            for i, n in enumerate(t[slot]):
                self.note('%s[%d]' % (slot, i), n)
        # MOVE AND SHOOT: under the sticks, centred at 110 and 280, in the 16bold cut
        for i, s in enumerate(t['move_shoot']):
            self.caps('move_shoot[%d]' % i, s)
            self.line('move_shoot[%d]' % i, s, 150, pen.label)
        # STICK TYPES / TOUCH MODES: three columns, each the second line under an Options word
        for slot in ('types', 'modes'):
            for i, s in enumerate(t[slot]):
                self.caps('%s[%d]' % (slot, i), s)
                self.wrapped('%s[%d]' % (slot, i), [s], gs.column_width(3), 2)
        # DOUBLE TAP: STATIC / DYNAMIC / FLOATING / BUTTONS (Options words) - what to do
        names = [gs.word_of(pen, k) for k in (2, 3, 9, 11)]
        self.legend('dtap', names, t['dtap'], 2 * (RIGHT - (LEGEND_LEFT + 356) / 2.0))
        # BUTTONS: five columns; each label is GESTURE: over WHAT IT DOES
        w5 = gs.column_width(5)
        cols = [[0, 1], [2], [3], [4, 5], [6]]
        for col in cols:
            parts = []
            for i in col:
                self.caps('buttons[%d]' % i, t['buttons'][i])
                parts += gs.gesture_and_deed(t['buttons'][i])
            self.wrapped('buttons[%s]' % ','.join(map(str, col)), parts, w5, 4)
        # SPECIAL BUTTONS: four columns, (first line, second line)
        for i, pair in enumerate(t['special']):
            for s in pair:
                self.caps('special[%d]' % i, s)
            self.wrapped('special[%d]' % i, list(pair), gs.column_width(4), 4)
        # MAP AND ITEMS: KEY - what it does, right of the minimap
        self.legend('map', [k for k, _ in t['map']], [v for _, v in t['map']],
                    2 * (RIGHT - (LEGEND_LEFT + 356) / 2.0), key_slot='map(key)')
        # ITEM DESCRIPTIONS: two columns 150 wide, under the two pictures
        for i, col in enumerate(t['desc']):
            for s in col:
                self.caps('desc[%d]' % i, s)
            self.wrapped('desc[%d]' % i, list(col), 150, 5 if i == 0 else 5)
        # GAMEPAD & KEYBOARD: the keys are fixed but for shoot_key
        self.caps('shoot_key', t['shoot_key'])
        self.legend('pad', gs.pad_keys(t['shoot_key']), t['pad'],
                    2 * (RIGHT - sum(gs.INSIDE) / 2.0))
        # LOCAL CO-OP
        self.legend('coop', t['coop_keys'], t['coop'], 2 * (RIGHT - sum(gs.INSIDE) / 2.0), key_slot='coop_keys')
        # TWEAKS: the page's own row names
        names = gs.tp_names(self.set_name)
        tw = t['tweaks']
        self.table('tweaks', gs.tweaks_keys(self.set_name),
                   [[tw[0], t['aimlock_release']], tw[1], tw[2], tw[3], tw[4], tw[5]])
        self.table('tweaks2', [names[0], names[1], names[3], names[8], names[9], names[10]], t['tweaks2'])
        # ITEM SPAWN: the card's four tabs, lettered as the card letters them, right of the strap
        tabs = [pen.option_word(x) for x in spawner_tabs.TABS[pen.suffix]]
        key_w = max(x.width for x in tabs)
        room = 236 - key_w - 8
        for i, s in enumerate(t['spawn_tabs']):
            self.line('spawn_tabs[%d]' % i, s, room)
        # SPAWNER CONTROLS: fixed keys
        self.legend('spawn_rows', gs.spawner_keys(pen.suffix), t['spawn_rows'], 2 * (RIGHT - sum(gs.INSIDE) / 2.0))
        # MODS: the MODS page's own names, four a slide
        mods = names[len(tp.KINDS):len(tp.KINDS) + tp.MOD_ROWS]
        self.table('mods', mods[:4], t['mods'][:4])
        self.table('mods', mods[4:], t['mods'][4:], first=4)
        # YOUR MODS and A MOD'S PAGE
        uw = tp.USER_WORDS[self.set_name]
        self.caps('user_key', t['user_key'])
        self.missing('user_key', t['user_key'])
        self.table('user', [uw[1], uw[2], t['user_key']], t['user'])
        move = '%s / %s' % (uw[4], uw[5])
        if self.w(move) > gs.KEY_MAX:
            move = ['%s /' % uw[4], uw[5]]
        self.table('page', [uw[3], move, uw[6], uw[7], uw[10], uw[9]], t['page'])

    # ---- the slides themselves, by the generator
    def slides_of(self):
        t = self.t
        set_name = self.set_name
        titles = t['titles'] + [t['coop_title'], gs.tweaks_title(set_name), t['tweaks2_title'],
                                gs.tp_names(set_name)[0], t['spawn_title'], tp.MODS_WORD[set_name],
                                t['mods2_title'], tp.USER_WORDS[set_name][0], t['page_title']]
        joint = '' if set_name in gs.UNSPACED else ' '
        tweaks_notes = [t['tweaks_notes'][0] + joint + t['aimlock_note']] + list(t['tweaks_notes'][1:])
        notes = list(t['notes']) + [t['coop_note']] + tweaks_notes + list(t['spawn_notes']) + \
            list(t['mods_notes']) + list(t['user_notes'])
        return list(zip(titles, gs.DRAW, notes))

    def run_slides(self):
        built = []
        for k, (title, draw, note) in enumerate(self.slides_of()):
            try:
                built.append(gs.build(title, draw, note, self.pen, self.sheet))
            except SystemExit as e:
                self.r.add('FAIL', 'slide %d %s' % (k + 1, SLIDES[k]), str(e))
                built.append(None)
            except Exception as e:                       # a slot of the wrong shape slipped through
                self.r.add('FAIL', 'slide %d %s' % (k + 1, SLIDES[k]), '%s: %s' % (type(e).__name__, e))
                built.append(None)
        return built

    def png(self, built, path):
        count = len(built)
        cols = 2
        rows = (count + cols - 1) // cols
        K = gs.K
        cw, ch = 480 * K + 20, 270 * K + 20
        out = Image.new('RGBA', (cols * cw + 20, rows * ch + 20), (20, 18, 17, 255))
        for k, b in enumerate(built):
            at = (20 + (k % cols) * cw, 20 + (k // cols) * ch)
            if b is None:
                out.alpha_composite(Image.new('RGBA', (480 * K, 270 * K), (90, 30, 30, 255)), at)
                continue
            out.alpha_composite(gs.screen_of(b[0], b[1], self.sheet, k, count), at)
        out.save(path)


def main():
    args = [a for a in sys.argv[1:]]
    flags = [a for a in args if a.startswith('--')]
    plain = [a for a in args if not a.startswith('--')]
    png = None
    if '--png' in args:
        i = args.index('--png')
        png = args[i + 1]
        plain.remove(png)
    if not plain or plain[0] not in SET_OF:
        raise SystemExit(__doc__)
    code = plain[0]
    if '--current' in flags:
        text, source = guide_text.TEXT[SET_OF[code]], 'guide_text.py %s' % SET_OF[code]
    else:
        if len(plain) < 2:
            raise SystemExit('which language file? ' + __doc__)
        text, source = load_text(plain[1]), plain[1]
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    print('%s (%s): %s' % (code, SET_OF[code], source))
    errs = shape_errors(text)
    if errs:
        for e in errs:
            print('FAIL  shape  %s' % e)
        raise SystemExit(1)
    # every family of pad pictures (guide_slides.FAMILIES): a button is as wide as its
    # family draws it, so a line that fits with the Xbox's may not with Sony's OPTIONS
    families = [f.split('=', 1)[1] for f in flags if f.startswith('--family=')] or gs.FAMILIES
    failed = 0
    for family in families:
        if family not in gs.FAMILIES:
            raise SystemExit('no family %s (there are %s)' % (family, ' '.join(gs.FAMILIES)))
        gs.set_family(family)
        c = Checker(code, text)
        c.run_slots()
        built = c.run_slides()
        if '--budget' in flags:
            print('\n[%s] %-29s %5s %5s %5s  %s' % (family, 'slot', 'used', 'room', 'lines', 'text'))
            for slot, s, used, room, n, most in c.lines:
                mark = ' <' if (n > most or (n == 1 and used > room)) else ''
                print('%-34s %5d %5d %2d/%-2d  %s%s' % (slot, used, room, n, most, s, mark))
            print()
        for level, slot, msg in c.r.rows:
            print('%s  [%s] %s  %s' % (level, family, slot, msg))
        fails = c.r.fails()
        failed += len(fails)
        print('%s [%s]: %d FAIL, %d WARN' % (code, family, len(fails), len(c.r.rows) - len(fails)))
        if png:
            stem, ext = os.path.splitext(png)
            path = png if family == 'xbox' else '%s-%s%s' % (stem, family, ext)
            c.png(built, path)
            print('preview: %s' % path)
    gs.set_family('xbox')
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
