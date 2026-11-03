# -*- coding: utf-8 -*-
"""modport: a PC mod of The Binding of Isaac: Repentance into a mod for the Android port.

    py -3 tools/modport.py <mod folder or zip> --game <the port's export, zip or folder> [--out <zip>]

The game reference is what MODS -> "the game's files out" writes (a zip with
resources/ and resources-dlc3.<lang>/ inside), unpacked or not; on the owner's
PC the iOS tree work/app/repentance-resources in the project works too.

What it does, in order (the owner, 28.09.2026: "a tool outside the game, not
another patch everybody has to download"):
  1. reads metadata.xml (Steam) or mod.json; finds resources/, resources-dlc3*/,
     content/ and main.lua;
  2. keeps only the formats the port loads (png anm2 xml wav ogg fnt stb sta ogv
     srt), drops the rest and says what was dropped (Lua, sources); of content/ it
     carries entities2.xml (the port merges new enemies into the game's) and drops the
     rest (new items and the like would write past the save's fixed tables);
  3. keeps a mod's own copy of the anm2 the port's code reads by layer and
     frame number (the completion widget, the HUD stats, the score card, the
     pad glyphs, the boss bars' status icons - BY_NUMBER_FILES): its sheets are
     cut by its own frames, whatever their size, as on the PC and in 1.666
     (hotfix 1.71, 05.10.2026, as usermods.c guard_by_number_sheets); only when
     the mod brings no copy of its own for resources/ do the game's frames cut
     the mod's sheets of these files, and those are held to the game's size
     below. The menus the port lays its rows, straps and buttons over BY
     NAME (gamemenu, optionsmenu, pausescreen, ios_buttons .anm2 - portpatch.c,
     M8) are always the mod's: the port's check at start (usermods.c
     guard_port_patched) decides whether its additions fit the file. The
     pictures the port draws at the game's layout are kept when they have the
     game's size, BAKED DOWN when they are the game's size times k
     (Reflashed's x4: the crops are the PC's, the mod's the PC's x4, measured
     28.09.2026) - an area mean of each k x k block, so pixel art stays crisp
     (shrink) - and an anm2 of the mod that cuts them is rescaled to match, so
     either file cuts them right (the port's check would fit the game's anm2 to
     a k-times sheet instead - the same on the screen). Any other size is kept
     as the game takes it, with a warning. The game never scales a mod's picture at run time (the
     owner, 02.10.2026). THE CONVERTER IS NEVER STRICTER THAN THE PORT (the
     owner, 05.10.2026: "extend, never cut what worked"): what the port's check
     would serve safely is kept; only what would break is fixed;
  4. checks every anm2 against the game's: layers, nulls and events the game's
     file has and the mod's has not are ADDED (empty, invisible), animations
     the mod lacks are CLONED from the mod's own default animation under the
     missing name - the game asks for them by name and number, and a missing
     one is a crash;
  5. a picture whose size is not the game's is the mod's layout when an anm2 of
     the mod cuts it, and is kept with a note when none does;
  6. a picture wider or taller than 4096 is halved (phones' GL limit) and every
     anm2 that cuts it is rescaled to match;
  7. xml: a text table (TEXT_XML) is COMPLETED as the port's check completes
     any table (usermods.c guard_table, 05.10.2026): the mod's records are
     kept whole, and the game's records it lacks (an older PC version - items
     would be missing and crash) are added at its root's end, by the same key
     (an entity by type.variant.subtype, the rest by id or name); one with
     MORE entries is kept with a warning (new content);
     the game's parser (rapidxml) forgives a bare &, a < inside a value and
     a repeated attribute, and translation mods lean on that (PT-BR: "Inf&mia",
     description="<3") - such a file is escaped the way a strict parser wants
     and read on, not dropped;
  8. a picture with its own fnt (a mod's font) or its own anm2 is the mod's
     layout and is never questioned; one the game's crops cut, that is the
     game's width but SHORTER (an older game's sheet - controls.png of
     Rebirth is 340 rows, Repentance's 3552) is kept with a note, as the game
     and 1.666 take it (the crops may reach past it); xml other than the text
     files is not parsed at all - the
     game's parser takes two roots (fxlayers.xml) and a strict one does not;
  9. stb room files are kept unless their magic is not the game's STB1: the
     Repentance loader reads no room of such a file (0x750eb0, "STB1" or
     nothing) and a room asked for there is then read through a null pointer
     (0x6cec7c) - a crash. The room count beside the magic is the mod's own;
 10. writes the zip the port takes (mod.json + resources/...) and report.txt; the id follows the
     installer's rules (id_word); a mod with nothing the port reads (a script mod) gets no zip, only
     the report saying so.

The campaign of 04.10.2026 (25 workshop mods, the research notes of 04.10.2026 (mods test)) added:
  - folders beside resources/ are named in the report (resources-dlc4/ is Repentance+'s,
    resources-repentogon/ REPENTOGON's), and content/ tables each with their own reason;
  - an anm2 of the mod that cuts a picture the mod ships k times the game's size with the
    GAME's crops (frames at 100%, inside the game's sheet) is fitted to the picture (crops and
    pivots times k, scale over k) - the mod forgot it, and the PC shows the top-left 1/k too;
  - the iOS-only anm2 of the game (IOS_ONLY_ANM2: the character menu's touch seed widget) is
    put into the mod fitted to the mod's k-times pictures it cuts - no PC mod brings it;
  - a note when a mod's anm2 has layers, nulls or animations the game's has not (a newer
    game's layout: Repentance+ menus, versus screens), a warning when a whole data table
    differs from the game's beyond its text (a table of another game version: Sam & Max's
    players.xml takes the starting items away), and a warning when a file replaces one the
    port's built-ins bring (the EID icon atlases, the boss bars' pictures).
"""
import io
import json
import os
import re
import struct
import sys
import zipfile
import xml.etree.ElementTree as ET

try:
    from PIL import Image
except ImportError:
    Image = None

KEEP_EXT = {'png', 'anm2', 'xml', 'wav', 'ogg', 'fnt', 'stb', 'sta', 'ogv', 'srt'}
# the port's own code reads these by layer and frame number (mods.c the completion widget and the
# planetarium frame of the HUD stats, eid.c the pad glyphs, bossbars.c the status icons; loader.c
# names the score card only as a self-test sample). Each reader takes its numbers from the file
# the game loads - a mod's own copy included - and steps aside where that copy has not what it
# needs, so a mod's own copy is the mod's, its sheets cut by its own frames (hotfix 1.71,
# 05.10.2026: the rule of 02.10 that always gave the game's file turned whole mods off - Cooler
# Completion Marks). Only the mod's sheets the GAME's frames cut - the mod brings no copy of the
# anm2 for resources/ - are held to the game's size (usermods.c guard_by_number_sheets)
BY_NUMBER_FILES = {
    'gfx/ui/completion_widget.anm2',
    'gfx/ui/scoremenu.anm2', 'gfx/ui/hudstats2.anm2', 'gfx/ui/buttons.anm2',
    'gfx/ui/bosshp_icons/statuseffect_icon.anm2',
}
# a sheet the game's code swaps in by name under one of them (the pause's widget, 0x7e9dfc):
# held to the same rule as that anm2's own sheets (usermods.c BY_NUMBER_TWINS)
BY_NUMBER_TWINS = {
    'gfx/ui/completion_widget.anm2': ('gfx/ui/completion_widget_pause.png',),
}
BY_NUMBER_KEPT = ("%s: the mod's own - the port's code reads this file by layer and frame number from the copy the game "
                  "loads, the mod's included, and steps aside where it has not what it needs; the sheets it cuts are "
                  "the mod's layout, whatever their size")
WIDGET_KEPT = ("; Completion Mark Reminder (TWEAKS) leaves a mod's widget alone unless the mod also brings "
               "gfx/port_completion_widget.png and port_completion_widget_pause.png cut for it")
# the port lays its rows, straps, D-pad and twin buttons over these BY NAME, a mod's own copy
# included (portpatch.c shim_port_patch_file, usermods.c port_patched; M8, 02.10.2026), and makes
# the sticks' size variants from the ios_buttons.anm2 that wins. A mod's own copy is always kept
# (05.10.2026): the port's check at start uses it when every addition fits it, and the game's file
# with the additions otherwise (usermods.c guard_port_patched) - the converter does not guess
PORT_PATCHED = {
    'gfx/ui/main menu/gamemenu.anm2', 'gfx/ui/main menu/optionsmenu.anm2', 'gfx/ui/pausescreen.anm2',
    'gfx/ui/touch/ios_buttons.anm2',
}
# the menus whose sheets the port's check holds to the game's size when the frames cutting them are
# the game's - no mod brings its own copy for resources/ (usermods.c guard_menu_sheets: the patched
# targets in repentance-resources; ios_buttons lives in the Rebirth set, and that pass skips it)
MENU_SIZED = {
    'gfx/ui/main menu/gamemenu.anm2', 'gfx/ui/main menu/optionsmenu.anm2', 'gfx/ui/pausescreen.anm2',
}
MENU_KEPT = ("%s: the mod's own - the port lays its rows, straps and buttons over it by name, and they all fit it "
             "(the port's check at start, run here)")
MENU_REFUSED = ("%s: menu file not used: %s - the port's check at start would use the game's file with the port's menu "
                "instead, so it is left out here")
ORPHAN_REFUSED = ("%s: a picture of the mod's %s, which the port would not use (see that file): %dx%d is neither the "
                  "game's %dx%d nor a whole multiple of it, so the game's frames cannot cut it - the game's picture is "
                  "used, so it is left out here")
# every file whose sheets the port draws at the game's layout when the game's anm2 is the one
# loaded (GameRef.port_cuts, modport_ref.py portcut - only the anm2 count there); the mod's
# optionsextra.png is no longer left out: as any picture, it is judged by its size
PORT_FILES = BY_NUMBER_FILES | PORT_PATCHED | {'gfx/ui/main menu/optionsextra.png'}

PORT_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'port', 'app', 'src', 'main')
TREES = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'work', 'app')
# the game reads these Rebirth-set folders too (the touch controls live only there): the
# reference takes them as the mod's resources/ as well - a mod replaces them by that path
EXTRA_SETS = ('gfx/ui/touch',)
# the Repentance engine mounts all four editions, the latest over the earlier (0x886434..0x886468):
# a file the game reads may live in an older set (the touch controls, old characters' sprites)
OLDER_EDITIONS = ('afterbirthplus-resources', 'afterbirth-resources', 'rebirth-resources')   # nearest first


def port_drawn():
    """the GAME's pictures the port itself draws at the game's layout, found in the port's
    sources rather than listed by hand (28.09.2026, the owner: "does it understand every
    conflict like the marks?"):
      - sheets cut by the port's own anm2 in assets (the guide and tweaks pages cut
        optionsmenu.png and sketch.png, the Marked button collectibles_394_marked.png);
      - sheets cut by the anm2 the port made inside the game's tree (ios_buttons_NNN.anm2);
      - pictures the port's C code points a sprite at by name (marked.c, touchedit.c, mods.c).
    A mod that redraws one of these at k times the size breaks the port's drawing of it.
    Empty when the port's sources are not next to this script (a copy handed out)."""
    out = set()
    assets = os.path.join(PORT_SRC, 'assets')
    if os.path.isdir(assets):
        for r, _, fs in os.walk(assets):
            for f in fs:
                if not f.lower().endswith('.anm2'):
                    continue
                full = os.path.join(r, f)
                rel = os.path.relpath(full, assets).replace(os.sep, '/')
                try:
                    root = ET.parse(full).getroot()
                except ET.ParseError:
                    continue
                for s in root.findall('Content/Spritesheets/Spritesheet'):
                    p = s.get('Path', '').replace('\\', '/')
                    out.add(norm(os.path.normpath(os.path.join('resources/gfx', os.path.dirname(rel), p)).replace(os.sep, '/')))
    # the anm2 the port made in the game's tree: the sticks' size variants, in the Rebirth set
    for tree in (os.path.join(TREES, 'rebirth-resources', 'data'), os.path.join(TREES, 'repentance-resources', 'data')):
        for r, _, fs in os.walk(os.path.join(tree, 'gfx')):
            for f in fs:
                if not re.search(r'_\d{3}\.anm2$', f):
                    continue
                full = os.path.join(r, f)
                rel = os.path.relpath(full, tree).replace(os.sep, '/')
                try:
                    root = ET.parse(full).getroot()
                except ET.ParseError:
                    continue
                out.update(anm2_sheets(root, 'resources/' + rel).values())
    return out | port_code()


def port_code():
    """the game's pictures the port's C code points a sprite at by name (marked.c, touchedit.c,
    mods.c): the port draws them at the game's size under an anm2 of its own"""
    out = set()
    cpp = os.path.join(PORT_SRC, 'cpp')
    if os.path.isdir(cpp):
        for f in os.listdir(cpp):
            if f.endswith('.c'):
                text = open(os.path.join(cpp, f), encoding='utf-8', errors='replace').read()
                for p in re.findall(r'"(gfx/[^"%]+\.png)"', text):
                    if not p.startswith(('gfx/port_', 'gfx/eid_')):
                        out.add('resources/' + p.lower())
    return out


def box_down(rgba, k):
    """THE BOX RULE, one for modport.py, modport.js (boxDown) and the port's installer (ModPort.java
    boxDown), integer-exact so the three agree pixel for pixel (04.10.2026): `rgba` is an h x w x 4
    uint8 array, both sides multiples of k. When every k x k block is one colour (pixel art blown up
    k times) each block gives that colour back exactly; otherwise each block becomes the mean of its
    pixels in premultiplied alpha: alpha = round(sum a / k^2), colour = round(sum(c * a) / sum a)
    (0 where the block is clear), round half up."""
    import numpy as np
    h, w = rgba.shape[0] // k, rgba.shape[1] // k
    blocks = rgba.reshape(h, k, w, k, 4).transpose(0, 2, 1, 3, 4).reshape(h, w, k * k, 4)
    if (blocks == blocks[:, :, :1, :]).all():
        return blocks[:, :, 0, :].copy()
    a = blocks[..., 3].astype(np.int64)
    asum = a.sum(axis=2)
    out = np.zeros((h, w, 4), dtype=np.uint8)
    n = k * k
    out[..., 3] = (2 * asum + n) // (2 * n)
    for c in range(3):
        num = (blocks[..., c].astype(np.int64) * a).sum(axis=2)
        val = np.where(asum > 0, (2 * num + asum) // np.maximum(2 * asum, 1), 0)
        out[..., c] = val
    return out


def shrink(im, size):
    """a picture baked down to `size` (the owner, 02.10.2026: by the converter, never by the game).

    The factor is a whole k (the game's size times k: Reflashed's x4; or 2, halving a sheet over the
    GL limit): box_down - pixel art comes back pixel for pixel, a picture drawn finely at k gets the
    true area mean in premultiplied alpha, no ringing or fringe at hard and alpha edges (LANCZOS is
    CLAUDE.md's rule for the port's own art at any ratio, not this). Halving a side of odd length
    drops its last row or column first (04.10.2026: one rule the three converters share, where it
    was LANCZOS before)."""
    import numpy as np
    w, h = im.size
    k = w // size[0] if size[0] and size[1] else 0
    if not k or h // size[1] != k:
        return im.convert('RGBA').resize(tuple(size), Image.LANCZOS)      # never asked for: not a whole factor
    a = np.array(im.convert('RGBA'))[:size[1] * k, :size[0] * k]
    return Image.fromarray(box_down(a, k))


def png_bytes(im):
    buf = io.BytesIO()
    im.save(buf, 'PNG')
    return buf.getvalue()


def folder_scaled(n, mod_sizes, game, k):
    """did the mod redraw this picture's folder at k times the game's size? (three or more
    siblings at exactly k times, and more of them than at the game's size)"""
    folder = norm(n).rsplit('/', 1)[0] + '/'
    at_k = at_1 = 0
    for other, size in mod_sizes.items():
        if not other.startswith(folder) or '/' in other[len(folder):] or other == norm(n):
            continue
        g = game.png_size(other)
        if not g:
            continue
        if tuple(size) == (g[0] * k, g[1] * k):
            at_k += 1
        elif tuple(size) == tuple(g):
            at_1 += 1
    return at_k >= 3 and at_k > at_1
PORT_CODE = port_code()
MAX_TEX = 4096
# the text tables a translation brings, often an older version's: completed as the port's check completes them
# (complete_table, usermods.c guard_table); the entry tags count the entries against the game's
TEXT_XML = {
    'items.xml': ('passive|active|familiar|trinket|null', 'id', ('name', 'description')),
    'pocketitems.xml': ('card|pilleffect|rune|pill', 'id', ('name', 'description', 'hud')),
    'entities2.xml': ('entity', None, ('name',)),
    'minibosses.xml': ('miniboss', 'id', ('name',)),
    'stages.xml': ('stage', 'id', ('name',)),
    'challenges.xml': ('challenge', 'id', ('name',)),
    'curses.xml': ('curse', 'id', ('name',)),
    'babies.xml': ('baby', 'id', ('name',)),
    'achievements.xml': ('achievement', 'id', ('text',)),
}


def norm(p):
    return p.replace('\\', '/').strip('/').lower()


def id_word(s):
    """a word made to the installer's id rules (UserMods.idWord): lowercase latin letters, digits, '-'
    and '_', starting with a letter or digit, at most 40; accents come off, anything else is one '-'.
    "" when nothing is left. The installer refuses any other id ("_berserk" of Tainted Samson Berserk
    Meter, whose directory is "_berserk", was refused, 04.10.2026)."""
    import unicodedata
    n = ''.join(c for c in unicodedata.normalize('NFD', str(s)) if not unicodedata.combining(c))
    n = re.sub(r'[^a-z0-9_-]+', '-', n.lower())
    n = re.sub(r'-{2,}', '-', n)
    n = re.sub(r'^[^a-z0-9]+', '', n)[:40]
    return re.sub(r'[-_]+$', '', n)


# the folders of other games beside resources/ (a mod made for several): the port is Repentance
OTHER_GAME_SETS = {
    'resources-dlc4': "Repentance+'s folder (the port is Repentance)",
    'resources-repentogon': "REPENTOGON's folder (the port has no REPENTOGON)",
}
# what content/ of a PC mod adds, and why none of it but entities2.xml can come (the reason per table)
CONTENT_WHY = {
    'items.xml': "new items would be written past the save's fixed tables",
    'pocketitems.xml': "new cards, runes and pills would be written past the save's fixed tables",
    'players.xml': "new characters would be written past the save's fixed tables",
    'challenges.xml': "new challenges would be written past the save's fixed tables",
    'itempools.xml': 'pools for new items, which do not come',
    'costumes2.xml': 'new costumes are put on by a script or by new items, neither of which come',
    'music.xml': 'new music is played by a script, which does not run',
    'sounds.xml': 'new sounds are played by a script, which does not run',
}
# the game's own anm2 that only the iOS game has and the iOS Repentance code asks for (measured 04.10.2026:
# the strings of the image, xrefs in the Repentance copy, against the PC Repentance 1.7.9 resources):
# the character menu's touch seed widget (0x5e54b0, drawn on touch instead of SeedSelectionWidget.anm2,
# 0x5e8da4..0x5e8e40). A PC mod never brings it, so it cuts whatever the mod made of the sheets it shares
# with the PC game - at the game's crops
IOS_ONLY_ANM2 = ('resources/gfx/ui/extratouchwidgets.anm2',)


VARIANT = re.compile(r'^(.*)_[a-z0-9]+\.png$', re.I)


def variant_base(p):
    """"<base>_<suffix>.png" -> "<base>.png": a sheet the game swaps in BY CODE under the
    base's anm2, so no anm2 names it (measured 28.09.2026: completion_widget_pause.png by
    0x7e9dfc, titlemenu_2.png by the title, titlemenu_ios.png on iOS). Such a sheet must
    have its base's layout, whatever the base's fate."""
    m = VARIANT.match(p)
    return m.group(1) + '.png' if m else None


class Report:
    def __init__(self):
        self.lines = []
        self.counts = {}

    def add(self, kind, text):
        self.counts[kind] = self.counts.get(kind, 0) + 1
        self.lines.append('%-8s %s' % (kind, text))

    def text(self):
        head = ['modport report', '']
        for k in sorted(self.counts):
            head.append('%6d  %s' % (self.counts[k], k))
        return '\n'.join(head + [''] + self.lines) + '\n'


class GameRef:
    """the game's files by the mod's path: resources/<p> -> data/<p>, resources-dlc3.ru/<p> -> dlc3.ru/<p>"""
    def __init__(self, where):
        self.zip = None
        self.root = None
        self.names = {}
        if os.path.isfile(where) and where.lower().endswith('.zip'):
            self.zip = zipfile.ZipFile(where)
            for n in self.zip.namelist():
                self.names[norm(n)] = n
        elif os.path.isdir(os.path.join(where, 'data')) and not os.path.isdir(os.path.join(where, 'resources')):
            self.root = where                      # the iOS tree: data/, dlc3.ru/ ...
            self.ios = True
        else:
            self.root = where                      # the export, unpacked: resources/, resources-dlc3.ru/
            self.ios = False
        if self.root:
            for r, _, fs in os.walk(self.root):
                for f in fs:
                    full = os.path.join(r, f)
                    rel = norm(os.path.relpath(full, self.root))
                    if getattr(self, 'ios', False):
                        set_, _, path = rel.partition('/')
                        rel = ('resources/' + path) if set_ == 'data' else ('resources-' + set_ + '/' + path)
                    self.names[rel] = full
            # the iOS tree: the older editions' data under it, as the game mounts them - a
            # path Repentance has stays Repentance's, the rest comes from the nearest older set
            if getattr(self, 'ios', False):
                apps = os.path.dirname(os.path.normpath(self.root))
                for ed in OLDER_EDITIONS:
                    older = os.path.join(apps, ed, 'data')
                    for r, _, fs in os.walk(older):
                        for f in fs:
                            if '.before-' in f or 'REFERENCE' in f or f.endswith('.orig'):
                                continue
                            full = os.path.join(r, f)
                            rel = 'resources/' + norm(os.path.relpath(full, older))
                            self.names.setdefault(rel, full)

    def has(self, modpath):
        return norm(modpath) in self.names

    def cuts(self, modpath):
        """does some anm2 of the game cut this picture? (the sheets of every game anm2, read once)"""
        if not hasattr(self, '_cut'):
            self._cut = set()
            self._portcut = set()
            for rel in list(self.names):
                if not rel.endswith('.anm2'):
                    continue
                try:
                    root = ET.fromstring(self.read(rel))
                except ET.ParseError:
                    continue
                sheets = set(anm2_sheets(root, rel).values())
                self._cut.update(sheets)
                # the port's own files, and the anm2 it made in the game's tree (_NNN size variants)
                if rel.split('/', 1)[-1] in PORT_FILES or re.search(r'_\d{3}\.anm2$', rel):
                    self._portcut.update(sheets)
            self._portcut.update(port_drawn())
        return norm(modpath) in self._cut

    def port_cuts(self, modpath):
        """does the port itself draw this picture at the game's layout? (its own files, its own
        anm2, its code - see port_drawn)"""
        self.cuts(modpath)
        return norm(modpath) in self._portcut

    def by_number(self):
        """{anm2: [pictures]} of BY_NUMBER_FILES in this game (by_number_sheets), read once"""
        if not hasattr(self, '_bynumber'):
            self._bynumber = by_number_sheets(self.read)
        return self._bynumber

    def menu_sized(self):
        """{anm2: [pictures]} of MENU_SIZED in this game (menu_sheets), read once"""
        if not hasattr(self, '_bymenu'):
            self._bymenu = menu_sheets(self.read)
        return self._bymenu

    def png_size(self, modpath):
        """the game's picture size by the mod's path, None when the game has no such png"""
        d = self.read(modpath)
        if not d or len(d) < 24 or d[12:16] != b'IHDR':
            return None
        return struct.unpack('>II', d[16:24])

    def read(self, modpath):
        n = self.names.get(norm(modpath))
        if n is None:
            return None
        if self.zip:
            return self.zip.read(n)
        return open(n, 'rb').read()


BARE_AMP = re.compile(rb'&(?!(?:[A-Za-z][A-Za-z0-9]*|#[0-9]+|#x[0-9A-Fa-f]+);)')
ATTR_VALUE = re.compile(rb'="([^"]*)"')
TAG = re.compile(rb'<([A-Za-z_][\w.:-]*)((?:\s+[\w.:-]+\s*=\s*"[^"]*")+)(\s*/?>)')
ATTR = re.compile(rb'\s+([\w.:-]+)\s*=\s*"[^"]*"')


def lenient_xml(data):
    """the mod's xml the way the game's parser (rapidxml) reads it. A bare &, a <
    inside a value and a repeated attribute are forgiven there (the first
    attribute wins) and fatal to a strict parser: escaped here, so the file
    parses and is written in a form both take. Returns (data, [what was fixed])."""
    fixes = []
    n = len(BARE_AMP.findall(data))
    if n:
        data = BARE_AMP.sub(b'&amp;', data)
        fixes.append('%d bare &' % n)
    lt = [0]

    def value(m):
        v = m.group(1)
        if b'<' in v:
            lt[0] += v.count(b'<')
            v = v.replace(b'<', b'&lt;')
        return b'="' + v + b'"'
    data = ATTR_VALUE.sub(value, data)
    if lt[0]:
        fixes.append('%d < inside a value' % lt[0])
    dups = [0]

    def tag(m):
        names = [a.group(1) for a in ATTR.finditer(m.group(2))]
        if len(names) == len(set(names)):
            return m.group(0)
        seen = set()
        out = b''
        for a in ATTR.finditer(m.group(2)):
            if a.group(1) in seen:
                dups[0] += 1
                continue
            seen.add(a.group(1))
            out += a.group(0)
        return b'<' + m.group(1) + out + m.group(3)
    data = TAG.sub(tag, data)
    if dups[0]:
        fixes.append('%d repeated attribute' % dups[0])
    return data, fixes


def parse_xml(data, rel, rep):
    """(root, data as written): strict first, then the game's way, with a report line"""
    try:
        return ET.fromstring(data), data
    except ET.ParseError:
        pass
    fixed, fixes = lenient_xml(data)
    try:
        root = ET.fromstring(fixed)
    except ET.ParseError:
        return None, data
    rep.add('repaired', "%s: %s - the game's parser forgives these, a strict one does not; written escaped"
            % (rel, ', '.join(fixes) or 'parse quirks'))
    return root, fixed


def fnt_pages(data):
    """the picture files a BMFont (binary or text) draws from"""
    if data[:3] == b'BMF':
        p = 4
        pages = []
        while p + 5 <= len(data):
            kind = data[p]
            size = struct.unpack('<I', data[p + 1:p + 5])[0]
            block = data[p + 5:p + 5 + size]
            if kind == 3:
                pages = [x.decode('latin-1') for x in block.split(b'\0') if x]
            p += 5 + size
        return pages
    return re.findall(r'page\s+id=\d+\s+file="([^"]+)"', data.decode('latin-1', 'replace'))


def anm2_parts(root):
    c = root.find('Content')
    layers = {int(l.get('Id')): l for l in c.findall('Layers/Layer')} if c is not None else {}
    nulls = {int(n.get('Id')): n for n in c.findall('Nulls/Null')} if c is not None else {}
    events = {int(e.get('Id')): e for e in c.findall('Events/Event')} if c is not None else {}
    sheets = {int(s.get('Id')): s for s in c.findall('Spritesheets/Spritesheet')} if c is not None else {}
    anims = {a.get('Name'): a for a in root.findall('Animations/Animation')}
    return layers, nulls, events, sheets, anims


def child(parent, tag):
    e = parent.find(tag)
    if e is None:
        e = ET.SubElement(parent, tag)
    return e


def repair_anm2(mod_root, game_root, rel, rep):
    """the mod's anm2 made complete against the game's: returns (changed, fatal)"""
    ml, mn, me, ms, ma = anm2_parts(mod_root)
    gl, gn, ge, gs, ga = anm2_parts(game_root)
    changed = False
    content = child(mod_root, 'Content')
    # layers the game has and the mod has not: added, and an invisible track in every animation
    for lid, layer in gl.items():
        if lid in ml:
            continue
        new = ET.SubElement(child(content, 'Layers'), 'Layer', Name=layer.get('Name'), Id=str(lid),
                            SpritesheetId=str(min(ms) if ms else 0))
        for a in ma.values():
            la = child(a, 'LayerAnimations')
            ET.SubElement(la, 'LayerAnimation', LayerId=str(lid), Visible='false')
        rep.add('repaired', '%s: layer %d "%s" added (empty)' % (rel, lid, layer.get('Name')))
        changed = True
    for nid, null in gn.items():
        if nid in mn:
            continue
        ET.SubElement(child(content, 'Nulls'), 'Null', Name=null.get('Name'), Id=str(nid), ShowRect='false')
        for a in ma.values():
            na = child(a, 'NullAnimations')
            ET.SubElement(na, 'NullAnimation', NullId=str(nid), Visible='false')
        rep.add('repaired', '%s: null %d "%s" added (empty)' % (rel, nid, null.get('Name')))
        changed = True
    for eid, ev in ge.items():
        if eid in me:
            continue
        ET.SubElement(child(content, 'Events'), 'Event', Name=ev.get('Name'), Id=str(eid))
        rep.add('repaired', '%s: event %d "%s" added' % (rel, eid, ev.get('Name')))
        changed = True
    # animations the mod lacks: the mod's own default animation under the missing name
    anims_el = mod_root.find('Animations')
    if anims_el is not None and ma:
        default = anims_el.get('DefaultAnimation')
        source = ma.get(default)
        if source is None:
            source = next(iter(ma.values()))
        import copy
        for name in ga:
            if name in ma:
                continue
            clone = copy.deepcopy(source)
            clone.set('Name', name)
            anims_el.append(clone)
            rep.add('repaired', '%s: animation "%s" was missing, cloned from "%s"' % (rel, name, source.get('Name')))
            changed = True
    return changed, False


def anm2_sheets(root, anm_rel):
    """the pictures an anm2 cuts, by the mod's path"""
    out = {}
    base = os.path.dirname(anm_rel)
    for s in root.findall('Content/Spritesheets/Spritesheet'):
        p = s.get('Path', '').replace('\\', '/')
        full = norm(os.path.normpath(os.path.join(base, p)).replace('\\', '/'))
        out[int(s.get('Id'))] = full
    return out


def game_sheets(read, rels, twins):
    """{anm2: [pictures]} of the anm2 at rels by the game's own files of resources/ (read: a mod path ->
    bytes or None), the twins included; the paths without the set folder ('gfx/ui/hudstats2.png')"""
    out = {}
    for rel in sorted(rels):
        sheets = []
        d = read('resources/' + rel)
        try:
            root = ET.fromstring(d) if d else None
        except ET.ParseError:
            root = None
        if root is not None:
            for s in anm2_sheets(root, 'resources/' + rel).values():
                inner = s.split('/', 1)[-1]
                if inner not in sheets:
                    sheets.append(inner)
        sheets.sort()
        for t in twins.get(rel, ()):
            if t not in sheets:
                sheets.append(t)
        out[rel] = sheets
    return out


def by_number_sheets(read):
    """the pictures of BY_NUMBER_FILES and their twins (game_sheets); the game reference carries them as
    'bynumber' (modport_ref.py) for the page and the installer"""
    return game_sheets(read, BY_NUMBER_FILES, BY_NUMBER_TWINS)


def menu_sheets(read):
    """the pictures of MENU_SIZED (game_sheets); the game reference carries them as 'bymenu'"""
    return game_sheets(read, MENU_SIZED, {})


def held_by(p, sheets, own):
    """the anm2 of `sheets` ({anm2: [pictures]}) that cut picture p (a mod path, any set) with the GAME's frames
    here - the mod brings no copy of its own for them (own) - and the ones the mod's own copy cuts: (game's, own)"""
    inner = norm(p).split('/', 1)[-1]
    owners = [a for a, s in sorted(sheets.items()) if inner in s]
    return [a for a in owners if a not in own], [a for a in owners if a in own]


def by_number_free(p, by_number, own):
    """is picture p (a mod path, any set) cut only by anm2 of BY_NUMBER_FILES the mod brings its own copy of
    (own: those anm2, for resources/), or their twin? Then its frames are the mod's and its size is the mod's
    choice; a picture of one the mod brings no copy of is cut by the game's frames (usermods.c
    guard_by_number_sheets: find_entry("", anm2) - a mod's copy for all sets)"""
    inner = norm(p).split('/', 1)[-1]
    owners = [a for a, s in by_number.items() if inner in s]
    return bool(owners) and all(a in own for a in owners)


# ---------------------------------------------------------------- the port's check at start, run here (round 3)
# The converter mirrors what the port's check (usermods.c guard_file, guard_port_patched, guard_orphan_sheets)
# does with the files it writes - never stricter, never looser (the owner, 05.10.2026): what the port would
# refuse is left out here with the port's reason, so a converted mod installs with no red line, and
# everything the port would use is kept.

ANM_TRACKS_MAX = 64                     # usermods.c: the engine loader's limits (anm_limits)
ANM_FRAMES_MAX = 5000
KFIT_K_MAX = 16                         # usermods.c: the largest k THE GAME'S FRAMES FITTED takes
ALL_SETS = ('resources', 'resources-dlc3')   # the mod's folders for all sets (usermods.c walk: the set "")


def num_of(v):
    """portpatch.c num_of: a whole number of up to 9 digits with an optional sign, else -1"""
    if not v:
        return -1
    i, neg = 0, False
    if v[0] in '+-':
        neg, i = v[0] == '-', 1
    if i >= len(v):
        return -1
    n = 0
    for c in v[i:]:
        if c < '0' or c > '9':
            return -1
        n = n * 10 + ord(c) - 48
        if n > 100000000:
            return -1
    return -n if neg else n


def anm_limits(root):
    """usermods.c anm_limits: past the engine loader's limits - its reason, or None"""
    anims = root.find('Animations')
    if anims is None:
        return None
    for a in anims.findall('Animation'):
        nm = a.get('Name') or ''
        for null in (False, True):
            lst = a.find('NullAnimations' if null else 'LayerAnimations')
            n = len(lst.findall('NullAnimation' if null else 'LayerAnimation')) if lst is not None else 0
            if n > ANM_TRACKS_MAX:
                return 'past the engine loader\'s limits: animation "%s" has %d %s tracks (%d at most)' % (
                    nm, n, 'null' if null else 'layer', ANM_TRACKS_MAX)
            for t in (list(lst) if lst is not None else []):
                f = len(t.findall('Frame'))
                if f >= ANM_FRAMES_MAX:
                    return 'past the engine loader\'s limits: animation "%s" has a track of %d frames (%d at most)' % (
                        nm, f, ANM_FRAMES_MAX - 1)
        r = a.find('RootAnimation')
        f = len(r.findall('Frame')) if r is not None else 0
        if f >= ANM_FRAMES_MAX:
            return 'past the engine loader\'s limits: animation "%s" has %d root frames (%d at most)' % (nm, f, ANM_FRAMES_MAX - 1)
    return None


def anm_model(root):
    """what portpatch.c's patch and its all-or-nothing check read of an .anm2: sheets [id, path], layers
    [id, name, sheet id], nulls [id, name], animations [name, [layer ids drawn], [null ids drawn]] - the
    attribute values as written (None: absent); None when it has no Content or no Animations (model_of)"""
    c, an = root.find('Content'), root.find('Animations')
    if c is None or an is None:
        return None

    def part(tag):
        x = c.find(tag)
        return list(x) if x is not None else []
    return {
        'sheets': [[e.get('Id'), e.get('Path')] for e in part('Spritesheets') if e.tag == 'Spritesheet'],
        'layers': [[e.get('Id'), e.get('Name'), e.get('SpritesheetId')] for e in part('Layers') if e.tag == 'Layer'],
        'nulls': [[e.get('Id'), e.get('Name')] for e in part('Nulls') if e.tag == 'Null'],
        'anims': [[a.get('Name'),
                   [x.get('LayerId') for x in (a.find('LayerAnimations') if a.find('LayerAnimations') is not None else []) if x.tag == 'LayerAnimation'],
                   [x.get('NullId') for x in (a.find('NullAnimations') if a.find('NullAnimations') is not None else []) if x.tag == 'NullAnimation']]
                  for a in an if a.tag == 'Animation'],
    }


def patch_ops(root):
    """a port patch (assets/port_patch/...patch) as the operations portpatch.c pp_apply runs, in order"""
    ops = []
    for op in root:
        if op.tag == 'Spritesheet':
            ops.append(['sheet', op.get('Path')])
        elif op.tag == 'Layer':
            ops.append(['layer', op.get('Name'), op.get('Spritesheet'), op.get('SheetOf')])
        elif op.tag == 'Null':
            ops.append(['null', op.get('Name'), op.get('Before')])
        elif op.tag == 'Animation':
            parts = []
            for x in op:
                if x.tag in ('RootAnimation', 'Triggers'):
                    parts.append(['keep'])
                elif x.tag == 'LayerAnimation':
                    parts.append(['layer', x.get('Layer'), x.get('After')])
                elif x.tag == 'NullAnimation':
                    parts.append(['null', x.get('Null'), x.get('After')])
                else:
                    parts.append(['other', x.tag])
            ops.append(['anim', op.get('Name'), parts])
        else:
            ops.append(['other', op.tag])
    return ops


def _first(lst, i, want, fold=False):
    """portpatch.c by_attr: the first entry whose field i is want (ASCII case folded for a sheet path)"""
    if want is None:
        return None
    for e in lst:
        v = e[i]
        if v is None:
            continue
        if (v.lower() == want.lower() and len(v) == len(want)) if fold else v == want:
            return e
    return None


def _max_id(lst):
    m = -1
    for e in lst:
        v = num_of(e[0])
        if v > m:
            m = v
    return m


def sim_patch(model, ops):
    """portpatch.c pp_apply on a model: (the first line it would say, the model after) - one line means the
    patch did not land whole"""
    import copy
    m = copy.deepcopy(model)
    says = []
    sheets, layers, nulls, anims = m['sheets'], m['layers'], m['nulls'], m['anims']
    for op in ops:
        if op[0] == 'sheet':
            if op[1] is None or _first(sheets, 1, op[1], True):
                continue
            sheets.append([str(_max_id(sheets) + 1), op[1]])
        elif op[0] == 'layer':
            name, sp, of = op[1], op[2], op[3]
            sh = _first(sheets, 1, sp, True) if sp else None
            if not sh and of:
                o = _first(layers, 1, of)
                if o:
                    for x in sheets:
                        if o[2] is not None and x[0] is not None and x[0] == o[2]:
                            sh = x                   # the last one, as the C loop has it
            if not sh:
                says.append('layer %s: no sheet %s - refused' % (name or '', sp or ''))
                continue
            if name is None:
                continue
            ly = _first(layers, 1, name)
            if not ly:
                layers.append([str(_max_id(layers) + 1), name, sh[0]])
            else:
                ly[2] = sh[0]
        elif op[0] == 'null':
            name, bf = op[1], op[2]
            if name is None or _first(nulls, 1, name):
                continue
            before = _first(nulls, 1, bf) if bf else None
            if bf and not before:
                says.append('null %s: no null %s to go before - appended' % (name, bf))
            if before:
                nid = num_of(before[0])
                at = next(i for i, x in enumerate(nulls) if x is before)
                for x in nulls:
                    v = num_of(x[0])
                    if v >= nid:
                        x[0] = str(v + 1)
                for a in anims:
                    for i, v in enumerate(a[2]):
                        if num_of(v) >= nid:
                            a[2][i] = str(num_of(v) + 1)
                nulls.insert(at, [str(nid), name])
            else:
                nulls.append([str(_max_id(nulls) + 1), name])
        elif op[0] == 'anim':
            name = op[1]
            if name is None:
                continue
            a = _first(anims, 0, name)
            if not a:
                a = [name, [], []]
                anims.append(a)
            for part in op[2]:
                if part[0] == 'keep':
                    continue
                if part[0] == 'other':
                    says.append('%s: unknown part <%s> - skipped' % (name, part[1]))
                    continue
                is_layer = part[0] == 'layer'
                lst, box = (layers, a[1]) if is_layer else (nulls, a[2])
                target, after = part[1], part[2]
                ref = _first(lst, 1, target)
                if not ref:
                    says.append('%s: %s %s is not in the file - refused' % (name, part[0], target or ''))
                    continue
                if ref[0] in box:
                    continue
                if after:
                    an = _first(lst, 1, after)
                    if not (an and an[0] in box):
                        says.append('%s: %s %s - %s is not drawn here, appended' % (name, part[0], target, after))
                box.append(ref[0])
        else:
            says.append('unknown operation <%s> - skipped' % op[1])
    return (says[0] if says else None), m


def patch_complete(m, target):
    """portpatch.c pp_complete: the mod's patched model against the game's patched file - why not, or None"""
    adds = target['adds']
    for name, gid in target['game']['layers']:
        y = _first(m['layers'], 1, name)
        if not y:
            return "the port's menu needs layer %s" % (name or '')
        if name not in adds and num_of(y[0]) != gid:
            return "layer %s is number %d here, the game's code draws it as %d" % (name, num_of(y[0]), gid)
    for name, gid in target['game']['nulls']:
        y = _first(m['nulls'], 1, name)
        if not y:
            return "the port's menu needs null %s" % (name or '')
        if num_of(y[0]) != gid:
            return "null %s is number %d here, the menu's touch zones go by %d" % (name, num_of(y[0]), gid)
    for name in target['game']['anims']:
        if not _first(m['anims'], 0, name):
            return "the port's menu needs animation %s" % (name or '')
    return None


def menu_fit(root, target):
    """portpatch.c pp_apply_whole for a mod's menu .anm2 against one target: why the port would not use it, or None"""
    model = anm_model(root)
    if model is None:
        return 'it does not read as an .anm2'
    said, patched = sim_patch(model, target['ops'])
    if said:
        return "the port's menu does not fit it: " + said
    return patch_complete(patched, target)


def port_patch_targets():
    """the port's patched menu files (assets/port_patch/index.txt "patch" lines), each with its set, its path,
    the patch's operations, the layers it adds and the game's patched file's shape - what guard_port_patched
    checks a mod's copy against; [] when the port's sources are not next to this script (a copy handed out).
    The game reference carries it as 'portpatch' (modport_ref.py)"""
    global _PORT_PATCH
    if _PORT_PATCH is not None:
        return _PORT_PATCH
    out = []
    assets = os.path.join(PORT_SRC, 'assets')
    index = os.path.join(assets, 'port_patch', 'index.txt')
    if os.path.isfile(index):
        for line in open(index, encoding='utf-8'):
            f = line.rstrip('\r\n').split('\t')
            if len(f) < 3 or f[0] != 'patch':
                continue
            target, asset = f[1], f[2]
            base = f[3] if len(f) >= 4 and f[3] else target
            try:
                ops = patch_ops(ET.parse(os.path.join(assets, *asset.split('/'))).getroot())
                game = anm_model(ET.parse(os.path.join(TREES, *base.split('/'))).getroot())
            except (OSError, ET.ParseError):
                continue
            if game is None:
                continue
            _, g = sim_patch(game, ops)
            edition, set_, rel = target.split('/', 2)
            out.append({
                'target': target, 'set': set_.lower(), 'rel': rel.lower(), 'ops': ops,
                'adds': [op[1] for op in ops if op[0] == 'layer'],
                'game': {'layers': [[x[1], num_of(x[0])] for x in g['layers']],
                         'nulls': [[x[1], num_of(x[0])] for x in g['nulls']],
                         'anims': [x[0] for x in g['anims']]},
            })
    _PORT_PATCH = out
    return out


_PORT_PATCH = None


def target_candidates(target, have):
    """the mod's copies that answer a patched target, in THE ORDER (usermods.c order_pick, one mod): the
    language's dlc3 folder, the folders for all sets (resources-dlc3 over resources: one key), the
    language's data folder; have: {mod path lower: mod path} of the kept .anm2"""
    rel = target['rel']
    allsets = have.get('resources-dlc3/' + rel) or have.get('resources/' + rel)
    if '.' in target['set']:
        lang = target['set'].split('.', 1)[1]
        c = [have.get('resources-dlc3.%s/%s' % (lang, rel)), allsets, have.get('resources.%s/%s' % (lang, rel))]
    else:
        c = [allsets]
    return [x for x in c if x]


def sheet_wanted_by_own(rel, anm2_texts):
    """usermods.c sheet_wanted_by_own: another .anm2 of the mod that is used, in the sheet's folder, names it
    (path="<leaf>"); anm2_texts: {mod path: bytes} of the kept .anm2"""
    d, _, leaf = rel.rpartition('/')
    d = d + '/' if d else ''
    want = ('path="%s"' % leaf).lower()
    for n, data in anm2_texts.items():
        inner = n.lower().split('/', 1)[1] if '/' in n else ''
        if not inner.startswith(d) or '/' in inner[len(d):]:
            continue
        if want in data.decode('utf-8', 'replace').lower():
            return True
    return False


def scale_anm2_for(root, sheet_ids, rep, rel, k):
    """frames on sheets scaled down by k: crops and pivots divided, scale multiplied"""
    for a in root.findall('Animations/Animation'):
        for la in a.findall('LayerAnimations/LayerAnimation'):
            lid = la.get('LayerId')
            layer = None
            for l in root.findall('Content/Layers/Layer'):
                if l.get('Id') == lid:
                    layer = l
            if layer is None or int(layer.get('SpritesheetId', -1)) not in sheet_ids:
                continue
            for f in la.findall('Frame'):
                for a in ('XCrop', 'YCrop', 'Width', 'Height', 'XPivot', 'YPivot'):
                    if f.get(a) is not None:
                        f.set(a, str(int(round(float(f.get(a)) / float(k)))))
                for a in ('XScale', 'YScale'):
                    if f.get(a) is not None:
                        f.set(a, str(float(f.get(a)) * float(k)))
    rep.add('rescaled', '%s: frames on sheets scaled down by %d rescaled' % (rel, k))


def fit_anm2_up(root, sheet_ids, k):
    """frames on sheets drawn k times bigger than their crops say: crops and pivots multiplied, scale
    divided (the inverse of scale_anm2_for) - the same picture on the screen, cut from a k-times sheet"""
    layer_sheet = {l.get('Id'): l.get('SpritesheetId') for l in root.findall('Content/Layers/Layer')}
    n = 0
    for a in root.findall('Animations/Animation'):
        for la in a.findall('LayerAnimations/LayerAnimation'):
            if int(layer_sheet.get(la.get('LayerId'), -1)) not in sheet_ids:
                continue
            for f in la.findall('Frame'):
                for key in ('XCrop', 'YCrop', 'Width', 'Height', 'XPivot', 'YPivot'):
                    if f.get(key) is not None:
                        v = float(f.get(key)) * k
                        f.set(key, str(int(v)) if v == int(v) else str(v))
                for key in ('XScale', 'YScale'):
                    v = float(f.get(key, 100)) / k
                    f.set(key, str(int(v)) if v == int(v) else str(v))
                n += 1
    return n


def game_scale_frames(root, rel, sheet, gsize, k):
    """does this anm2 cut `sheet` with crops of the game's sheet, while the sheet is k times the game's?
    Every crop on it inside the game's sheet size (a few pixels of slack, as the game's own crops have),
    and the frames drawn at the game's scale, not at 1/k of it: the median of sqrt(|XScale * YScale|)
    above 100 / sqrt(k) - the game's own frames squash and stretch (60..140) or blow up (giantbook
    poofs at 800), a mod's k-times layout draws at about 100 / k. None when it does not cut it at all"""
    ids = {sid for sid, s in anm2_sheets(root, rel).items() if s == sheet}
    if not ids:
        return None
    layer_sheet = {l.get('Id'): l.get('SpritesheetId') for l in root.findall('Content/Layers/Layer')}
    scales = []
    for a in root.findall('Animations/Animation'):
        for la in a.findall('LayerAnimations/LayerAnimation'):
            if int(layer_sheet.get(la.get('LayerId'), -1)) not in ids:
                continue
            for f in la.findall('Frame'):
                w, h = float(f.get('Width', 0)), float(f.get('Height', 0))
                if w <= 0 or h <= 0:
                    continue
                if float(f.get('XCrop', 0)) + w > gsize[0] + 8 or float(f.get('YCrop', 0)) + h > gsize[1] + 8:
                    return False
                scales.append(abs(float(f.get('XScale', 100)) * float(f.get('YScale', 100))) ** 0.5)
    if not scales:
        return False
    scales.sort()
    return scales[len(scales) // 2] > 100.0 / (k ** 0.5)


def whole_k(size, gsize):
    """the whole k (>= 2) a picture is of the game's, by both sides; 0 when it is not"""
    if not gsize or not size or size[0] % gsize[0] or size[1] % gsize[1]:
        return 0
    k = size[0] // gsize[0]
    return k if k >= 2 and size[1] // gsize[1] == k else 0


def png_size_of(data):
    """(w, h) from a png's header, None when it is not one"""
    if not data or len(data) < 24 or data[12:16] != b'IHDR':
        return None
    return struct.unpack('>II', data[16:24])


def anm2_extras(mod_root, game_root):
    """what the mod's anm2 has and the game's has not, by name: (layers, nulls, animations)"""
    ml, mn, _, _, ma = anm2_parts(mod_root)
    gl, gn, _, _, ga = anm2_parts(game_root)
    gln = {l.get('Name') for l in gl.values()}
    gnn = {x.get('Name') for x in gn.values()}
    return ([l.get('Name') or '#%d' % i for i, l in sorted(ml.items()) if l.get('Name') not in gln],
            [x.get('Name') or '#%d' % i for i, x in sorted(mn.items()) if x.get('Name') not in gnn],
            [a for a in ma if a not in ga])


TEXT_ATTRS = {'name', 'description', 'text', 'hud', 'birthright'}


def table_skew(mod_root, game_root):
    """entries of a whole data table (by tag and id) whose values other than text differ from the game's:
    [(tag, id, [what])] - a table from another version of the game changes the game here"""
    def keyed(r):
        out = {}
        for e in r.iter():
            if e.get('id') is not None:
                out.setdefault((e.tag, e.get('id')), e)
        return out
    gk, mk = keyed(game_root), keyed(mod_root)
    rows = []
    for key, ge in gk.items():
        me = mk.get(key)
        if me is None:
            rows.append((key[0], key[1], ['gone']))
            continue
        what = []
        for a, v in ge.attrib.items():
            if a in TEXT_ATTRS:
                continue
            if me.get(a) is None:
                what.append('%s %s -> -' % (a, v))
            elif me.get(a) != v:
                what.append('%s %s -> %s' % (a, v, me.get(a)))
        for a, v in me.attrib.items():
            if a not in TEXT_ATTRS and ge.get(a) is None:
                what.append('%s - -> %s' % (a, v))
        gkids, mkids = len(list(ge)), len(list(me))
        if gkids != mkids:
            what.append('%d child entries -> %d' % (gkids, mkids))
        if what:
            rows.append((key[0], key[1], what))
    for key in mk:
        if key not in gk:
            rows.append((key[0], key[1], ['new']))
    return rows


def skew_report(rep, n, data, gdata):
    """a whole data table against the game's: its entries whose values other than text differ"""
    try:
        mroot = ET.fromstring(lenient_xml(data)[0])
        groot = ET.fromstring(lenient_xml(gdata)[0])
    except ET.ParseError:
        return
    rows = table_skew(mroot, groot)
    if not rows:
        return
    sample = '; '.join('%s %s: %s' % (t, i, ', '.join(w[:3])) for t, i, w in rows[:4])
    rep.add('warning', "%s: %d entries differ from the game's beyond their text (%s%s) - the whole table replaces the "
                       "game's: if it was copied from another version of the game, that version's values come too" % (n, len(rows), sample,
                                                                                        '; ...' if len(rows) > 4 else ''))


def port_own_files():
    """the files the port's built-ins bring into the game's tree under game paths (eid.c, bossbars.c,
    mods.c bring_in) - a mod's file on one of these paths replaces the built-in's own: {mod path: bytes}.
    Empty when the port's sources are not next to this script (a copy handed out)."""
    out = {}
    assets = os.path.join(PORT_SRC, 'assets')
    if not os.path.isdir(assets):
        return out
    names = set(os.listdir(assets))
    for f in names:
        full = os.path.join(assets, f)
        if not os.path.isfile(full) or not f.startswith('eid_'):
            continue
        low = f.lower()
        if low.endswith('.fnt') or (low.endswith('.png') and re.search(r'_\d+\.png$', low) and not
                                    os.path.exists(os.path.join(assets, f[:-4] + '.anm2'))):
            out['resources/font/' + low] = full          # the EID faces and their pages
        elif low.endswith(('.anm2', '.png')):
            out['resources/gfx/' + low] = full           # the EID icon atlases, each beside its anm2
    bars = os.path.join(assets, 'port_bossbars')
    for r, _, fs in os.walk(bars):
        for f in fs:
            full = os.path.join(r, f)
            out[('resources/gfx/ui/' + os.path.relpath(full, bars).replace(os.sep, '/')).lower()] = full
    return out


ROOM_REFUSED = ("%s: room file format %r is not the game's %r - the game's loader reads no room of such a file, "
                "and a room the game then asks for there is read through a null pointer, a crash: the game's stays")
TABLE_COMPLETED = ("%s: an older table - %d records of the game's it lacks added at its end; the mod's own records "
                   "are kept whole, as the port's own check at start does")


def c_long(v, default):
    """strtol(v, NULL, 10) as the port's check reads a number: leading space, a sign, the digits; 0 for none"""
    if v is None:
        return default
    m = re.match(r'[ \t\n\v\f\r]*([+-]?[0-9]+)', v)
    return int(m.group(1)) if m else 0


def rec_key(e):
    """a record's key in a whole table, as usermods.c rec_key: an entity by type.variant.subtype, the rest by its
    tag (active, passive and familiar are one class, "item") and id, Id, ID, name or Name; "" for none"""
    cls = 'item' if e.tag in ('active', 'passive', 'familiar') else e.tag
    if cls == 'entity':
        return 'entity[%d.%d.%d]' % (c_long(e.get('id'), -1), c_long(e.get('variant'), 0), c_long(e.get('subtype'), 0))
    for a in ('id', 'Id', 'ID', 'name', 'Name'):
        if e.get(a) is not None:
            return '%s[%s]' % (cls, e.get(a))
    return ''


def complete_table(m, g):
    """the game's records (children of the root) the mod's table lacks, by rec_key, appended to the mod's root
    in the game's order, as usermods.c guard_table adds them before the root's end; the number added"""
    if m.tag != g.tag:
        return 0
    import copy
    have = {rec_key(e) for e in m}
    added = 0
    for e in list(g):
        k = rec_key(e)
        if not k or k in have:
            continue
        m.append(copy.deepcopy(e))
        added += 1
    return added


def count_entries(r, tags):
    tagset = set(tags.split('|'))
    return sum(1 for e in r.iter() if e.tag in tagset)


def load_mod(where):
    """(files {modpath: bytes-or-path}, meta dict, notes)"""
    files = {}
    meta = {}
    notes = []
    if os.path.isfile(where) and where.lower().endswith('.zip'):
        z = zipfile.ZipFile(where)
        names = z.namelist()
        # the folder everything sits in, if one
        prefix = ''
        tops = set(n.split('/')[0] for n in names if '/' in n)
        if len(tops) == 1 and not any(n in ('mod.json', 'metadata.xml') for n in names):
            prefix = next(iter(tops)) + '/'
        for n in names:
            if n.endswith('/') or not n.startswith(prefix):
                continue
            files[n[len(prefix):]] = ('zip', z, n)
    else:
        for r, _, fs in os.walk(where):
            for f in fs:
                full = os.path.join(r, f)
                files[os.path.relpath(full, where).replace(os.sep, '/')] = ('file', full)
    def read(entry):
        if entry[0] == 'zip':
            return entry[1].read(entry[2])
        return open(entry[1], 'rb').read()
    if 'metadata.xml' in files:
        try:
            raw = read(files['metadata.xml'])
            try:
                x = ET.fromstring(raw)
            except ET.ParseError:
                x = ET.fromstring(lenient_xml(raw)[0])      # PC cards often carry a bare &
            for k in ('name', 'directory', 'id', 'version', 'description'):
                e = x.find(k)
                if e is not None and e.text:
                    meta[k] = e.text.strip()
        except ET.ParseError:
            notes.append('metadata.xml does not parse')
    if 'mod.json' in files:
        try:
            j = json.loads(read(files['mod.json']).decode('utf-8'))
            meta.update(j)
            if isinstance(j, dict) and j.get('id'):
                meta['_json_id'] = str(j['id'])
        except Exception:
            notes.append('mod.json does not parse')
    return files, meta, notes, read


def main():
    args = sys.argv[1:]
    if not args or '--game' not in args:
        print(__doc__)
        sys.exit(2)
    mod_where = args[0]
    game_where = args[args.index('--game') + 1]
    out = args[args.index('--out') + 1] if '--out' in args else None
    rep = Report()
    game = GameRef(game_where)
    files, meta, notes, read = load_mod(mod_where)
    for n in notes:
        rep.add('note', n)
    if any(n.lower().endswith('main.lua') for n in files):
        rep.add('dropped', 'main.lua: the port runs no scripts; whatever the script did is not carried')
    # what lies outside resources*/ and content/: said once per folder, not dropped silently (a mod made
    # for several games brings resources-dlc4/ for Repentance+, resources-repentogon/ for REPENTOGON)
    outside = {}
    for n in files:
        low = n.lower()
        top = low.split('/')[0] if '/' in low else ''
        if top == 'resources' or top.startswith('resources-dlc3') or top.startswith('resources.') or top == 'content':
            continue
        if low in ('mod.json', 'metadata.xml', 'main.lua'):
            continue
        outside.setdefault(top, []).append(n)
    for top in sorted(outside):
        names = outside[top]
        if top in OTHER_GAME_SETS:
            rep.add('dropped', '%s/ (%d files): %s - not taken' % (top, len(names), OTHER_GAME_SETS[top]))
        elif top:
            rep.add('dropped', '%s/ (%d files): not read by the port%s' % (
                top, len(names), ' (scripts)' if all(x.lower().endswith('.lua') for x in names) else ''))
        else:
            rep.add('dropped', '%d files beside the card (%s): not read by the port' % (
                len(names), ', '.join(sorted(names)[:4]) + (', ...' if len(names) > 4 else '')))
    # content/: the port merges content/entities2.xml into the game's (usermods.c, new enemies
    # built on the game's types); the rest of content/ would write past the save's fixed tables
    content_kept = {}
    game_types = set()
    gent = game.read('resources/entities2.xml')
    if gent:
        for e in ET.fromstring(gent).iter('entity'):
            if e.get('id', '').lstrip('-').isdigit():
                game_types.add(int(e.get('id')))
    for n in sorted(files):
        low = n.lower()
        if not low.startswith('content/') or low.endswith('/'):
            continue
        if low != 'content/entities2.xml':
            why = CONTENT_WHY.get(low.rsplit('/', 1)[-1], "the port merges no other content")
            rep.add('dropped', '%s: the port takes only content/entities2.xml; %s' % (n, why))
            continue
        root, data = parse_xml(read(files[n]), n, rep)
        if root is None:
            rep.add('dropped', '%s: does not parse' % n)
            continue
        ents = list(root.iter('entity'))
        new_types = sorted({int(e.get('id')) for e in ents if e.get('id', '').lstrip('-').isdigit()
                            and int(e.get('id')) not in game_types})
        rep.add('kept', '%s: %d entities for the port to merge into the game\'s' % (n, len(ents)))
        if new_types and game_types:
            rep.add('warning', '%s: types %s are not the game\'s (a script\'s creatures) - the port leaves them out'
                    % (n, ', '.join(str(t) for t in new_types[:12])))
        content_kept['content/entities2.xml'] = data

    # the resource files, by set folder
    taken = {}                                   # modpath (resources/...) -> bytes
    refused_anm2 = []                            # the mod's .anm2 the port would not use: their sheets judged below
    anm2_roots = {}                              # modpath -> ET root (mod's, possibly repaired)
    sheets_of = {}                               # modpath of a png -> [anm2 modpaths cutting it]
    for n in sorted(files):
        low = n.lower()
        top = low.split('/')[0]
        if not (top == 'resources' or top.startswith('resources-dlc3') or top.startswith('resources.')):
            continue
        ext = low.rsplit('.', 1)[-1] if '.' in low else ''
        if ext not in KEEP_EXT:
            rep.add('dropped', '%s: not a format the port loads' % n)
            continue
        inner = low.split('/', 1)[1] if '/' in low else ''
        data = read(files[n])
        taken[n] = data
        if ext == 'anm2':
            root, data = parse_xml(data, n, rep)
            if root is None:
                rep.add('dropped', '%s: the anm2 does not parse' % n)
                del taken[n]
                refused_anm2.append(n)
                continue
            taken[n] = data
            anm2_roots[n] = root
            if inner in BY_NUMBER_FILES:
                rep.add('kept', BY_NUMBER_KEPT % n + (WIDGET_KEPT if inner == 'gfx/ui/completion_widget.anm2' else ''))
            for sid, sheet in anm2_sheets(root, n).items():
                sheets_of.setdefault(sheet, []).append(n)
        elif ext == 'fnt':
            for page in fnt_pages(data):
                sheet = norm(os.path.normpath(os.path.join(os.path.dirname(n), page)).replace('\\', '/'))
                sheets_of.setdefault(sheet, []).append(n)

    # anm2 against the game's
    unplayed = []
    for n, root in list(anm2_roots.items()):
        gdata = game.read(n)
        if gdata is None:
            rep.add('kept', '%s: new to the game (a mod\'s own animation)' % n)
            continue
        groot = ET.fromstring(gdata)
        # animations the mod's file has and the game's has not are never asked for here: a newer game's
        # layout (Repentance+'s versus screens bring SceneCoop for online co-op) - said once below
        xa = anm2_extras(root, groot)[2]
        if xa:
            unplayed.append('%s (%s%s)' % (n.rsplit('/', 1)[-1], ', '.join(xa[:3]), ', ...' if len(xa) > 3 else ''))
        changed, fatal = repair_anm2(root, groot, n, rep)
        if changed:
            taken[n] = ET.tostring(root, encoding='utf-8', xml_declaration=True)

    if unplayed:
        rep.add('note', "%d anm2 have animations the game's files have not - never played here (made for another version "
                        "of the game, or for a script): %s%s" % (len(unplayed), '; '.join(unplayed[:6]),
                                                                  '; ...' if len(unplayed) > 6 else ''))

    # the port's check at start, run here (usermods.c guard_file, guard_port_patched): an .anm2 past the engine
    # loader's limits, and a menu file the port's rows do not fit whole, are not used by the port - left out
    for n in sorted(anm2_roots):
        why = anm_limits(anm2_roots[n])
        if why:
            rep.add('dropped', '%s: %s - the port would not use it (the game\'s file stands in): left out' % (n, why))
            del taken[n], anm2_roots[n]
            refused_anm2.append(n)
    have = {n.lower(): n for n in anm2_roots}
    refused_why = {}
    for t in port_patch_targets():
        for n in target_candidates(t, have):
            if n in refused_why:
                continue
            why = menu_fit(anm2_roots[n], t)
            if not why:
                break
            refused_why[n] = why
    for n in sorted(anm2_roots):
        if n.lower().split('/', 1)[-1] not in PORT_PATCHED:
            continue
        if n in refused_why:
            rep.add('dropped', MENU_REFUSED % (n, refused_why[n]))
            del taken[n], anm2_roots[n]
            refused_anm2.append(n)
        else:
            rep.add('kept', MENU_KEPT % n)
    for sheet in list(sheets_of):
        sheets_of[sheet] = [a for a in sheets_of[sheet] if a in taken]
        if not sheets_of[sheet]:
            del sheets_of[sheet]

    # pictures: size against the game's, and the GL limit
    # the files read by number the mod brings its own copy of for all sets: the sheets they cut are its layout
    own_by_number = {n.lower().split('/', 1)[1] for n in anm2_roots
                     if n.lower().split('/')[0] in ALL_SETS and n.lower().split('/', 1)[1] in BY_NUMBER_FILES}
    by_number = game.by_number()
    # the menus the mod brings its own copy of for all sets (always kept): the port's check decides on them
    own_menu = {n.lower().split('/', 1)[1] for n in anm2_roots
                if n.lower().split('/')[0] in ALL_SETS and n.lower().split('/', 1)[1] in MENU_SIZED}
    by_menu = game.menu_sized()
    shrunk = {}                                  # normalized png path -> k it was scaled down by
    mod_sizes = {}                               # normalized png path -> (w, h) as the mod ships it
    for n in taken:
        d = taken[n]
        if n.lower().endswith('.png') and len(d) >= 24 and d[12:16] == b'IHDR':
            mod_sizes[norm(n)] = struct.unpack('>II', d[16:24])
    # the sheets of an .anm2 the port would not use are cut by the game's .anm2 in its place (usermods.c
    # guard_orphan_sheets): the game's size or a whole k of it (2..16, the port fits the game's frames) are fine;
    # any other size nothing cuts right - the game's picture is used, so it is left out here, unless another .anm2
    # of the mod that is used names it
    orphans = {}                                 # normalized png path -> the reason
    kept_anm2 = {a: taken[a] for a in taken if a.lower().endswith('.anm2')}
    for a in refused_anm2:
        gdata = game.read(a)
        try:
            groot = ET.fromstring(gdata) if gdata else None
        except ET.ParseError:
            groot = None
        if groot is None:
            continue
        for sheet in anm2_sheets(groot, a).values():
            rel = sheet.split('/', 1)[-1]
            for pn in sorted(taken):
                if not pn.lower().endswith('.png') or norm(pn).split('/', 1)[-1] != rel or norm(pn) in orphans:
                    continue
                ms, gs = mod_sizes.get(norm(pn)), game.png_size(pn)
                if not ms or not gs or tuple(ms) == tuple(gs):
                    continue
                k = ms[0] // gs[0] if gs[0] and gs[1] and not ms[0] % gs[0] and not ms[1] % gs[1] else 0
                if 2 <= k <= KFIT_K_MAX and ms[1] // gs[1] == k:
                    continue
                if sheet_wanted_by_own(rel, kept_anm2):
                    continue
                orphans[norm(pn)] = ORPHAN_REFUSED % (pn, a.rsplit('/', 1)[-1], ms[0], ms[1], gs[0], gs[1])
    for n in list(taken):
        if not n.lower().endswith('.png'):
            continue
        if norm(n) in orphans:
            rep.add('dropped', orphans[norm(n)])
            del taken[n]
            continue
        if Image is None:
            break
        try:
            im = Image.open(io.BytesIO(taken[n]))
        except Exception:
            rep.add('dropped', '%s: not a readable png' % n)
            del taken[n]
            continue
        size = im.size
        base = variant_base(n)
        base_norm = norm(base) if base else None
        odd = False                              # kept at a size the port's layout does not fit
        gdata = game.read(n)
        if gdata is not None:
            try:
                gsize = Image.open(io.BytesIO(gdata)).size
            except Exception:
                gsize = None
            # a real twin: the game has both, nothing of the game's names the variant (code does),
            # and the game's two are the same size - grid_pit_corpse.png has its own anm2 and
            # door_downpour_mirror_mask.png its own size, and they are not twins
            twin = bool(base_norm and gsize and not game.cuts(n) and game.has(base)
                        and Image.open(io.BytesIO(game.read(base))).size == gsize)
            # a picture of a file read by number the mod brings its own copy of (and its twin) is cut by the
            # mod's frames: not the port's layout (hotfix 1.71)
            port_like = ((game.port_cuts(n) and not by_number_free(n, by_number, own_by_number))
                         or (twin and game.port_cuts(base) and not by_number_free(base, by_number, own_by_number)))
            cut_like = game.cuts(n) or (twin and game.cuts(base))
            # a size of its own is a mod's choice, not a fault: the game's crops
            # reach into whatever is there (Reflashed's 520x520 backdrops, its
            # fonts with their own fnt, its 2x2 blanks that switch a texture off)
            if gsize and gsize != size and port_like:
                # the port draws this picture at the game's layout (the game's anm2 of a file read by
                # number or of a menu, the port's own anm2, its code): a picture that is the game's times
                # k (Reflashed: x4) is baked down to the game's size, so the crops fit - and an anm2 of
                # the mod that cuts it is rescaled below; any other size is kept as the game takes it
                k = size[0] // gsize[0] if size[0] % gsize[0] == 0 and size[1] % gsize[1] == 0 and size[0] // gsize[0] == size[1] // gsize[1] else 0
                if k > 1 and norm(n) in PORT_CODE and folder_scaled(n, mod_sizes, game, k):
                    # the mod drew the whole folder at k times, so it carries the game's anm2 for
                    # them at k too (items on pedestals): the game's drawing wins
                    rep.add('warning', "%s: %dx%d, the game's %dx%d times %d like the rest of its folder; kept for the game, but the port's own drawing of this picture (a touch button or a built-in mod) will show it %d times too big" % (n, size[0], size[1], gsize[0], gsize[1], k, k))
                elif k > 1:
                    taken[n] = png_bytes(shrink(im, gsize))
                    shrunk[norm(n)] = k
                    rep.add('rescaled', "%s: %dx%d is the game's %dx%d times %d; the port draws it at the game's layout - baked down to %dx%d (area mean of each %dx%d block)" % (n, size[0], size[1], gsize[0], gsize[1], k, gsize[0], gsize[1], k, k))
                elif held_by(n, by_number, own_by_number)[0] or held_by(n, by_menu, own_menu)[0]:
                    # the game's frames cut it here; the port's check serves it as it is (05.10.2026: no sheet is
                    # refused for its size any more), so it is kept, said louder
                    rep.add('warning', "%s: %dx%d is not a multiple of the game's %dx%d, and the game's frames cut it here (the mod brings no %s of its own) - kept, as the game takes it; those frames will not fit it: redraw it at the game's size, or bring the anm2 cut for it" % (n, size[0], size[1], gsize[0], gsize[1], ', '.join(x.rsplit('/', 1)[-1] for x in held_by(n, by_number, own_by_number)[0] + held_by(n, by_menu, own_menu)[0])))
                    odd = True
                elif held_by(n, by_menu, own_menu)[1]:
                    rep.add('kept', "%s: %dx%d, cut by the mod's own %s - the mod's layout (if the port's check gives that file up, the game's picture stands in for this one)" % (n, size[0], size[1], ', '.join(x.rsplit('/', 1)[-1] for x in held_by(n, by_menu, own_menu)[1])))
                    odd = True
                else:
                    rep.add('warning', "%s: %dx%d is not a multiple of the game's %dx%d - kept, as the game takes it; the port draws it at the game's layout, so its crops will not fit it: redraw it at the game's size" % (n, size[0], size[1], gsize[0], gsize[1]))
                    odd = True
                if not odd:
                    continue
            # a variant of a sheet the mod's own anm2 cuts: made the size the mod's base has,
            # since the game swaps it in by code under that very anm2 (Reflashed's
            # titlemenu_3.png is 1x next to a 4x titlemenu.png and its 4x anm2)
            # a real twin: the game has both, nothing of the game's names the variant (code does),
            # and the game's two are the same size - grid_pit_corpse.png has its own anm2 and
            # door_downpour_mirror_mask.png its own size, and they are not twins
            if odd:
                pass
            elif twin and norm(n) not in sheets_of and base_norm in sheets_of and base_norm in mod_sizes and tuple(mod_sizes[base_norm]) != size:
                bw, bh = mod_sizes[base_norm]
                up = bw % size[0] == 0 and bh % size[1] == 0 and bw // size[0] == bh // size[1]
                down = size[0] % bw == 0 and size[1] % bh == 0 and size[0] // bw == size[1] // bh
                if up or down:
                    im = im.convert('RGBA').resize((bw, bh), Image.NEAREST) if up else shrink(im, (bw, bh))
                    taken[n] = png_bytes(im)
                    rep.add('rescaled', "%s: %dx%d made %dx%d, the size of the mod's %s - the game swaps this sheet in by code under the mod's anm2 for it" % (n, size[0], size[1], bw, bh, os.path.basename(base)))
                    size = (bw, bh)
                else:
                    rep.add('warning', "%s: %dx%d while the mod's %s is %dx%d - the game swaps this sheet in by code under that anm2, and the crops will not fit" % (n, size[0], size[1], os.path.basename(base), bw, bh))
            # (a twin at the size of its base the mod's own anm2 cuts is that anm2's layout too: nothing to say)
            elif gsize and gsize != size and norm(n) not in sheets_of and not (
                    twin and base_norm in sheets_of and tuple(mod_sizes.get(base_norm, ())) == tuple(size)):
                if ((size[0] == gsize[0] and size[1] < gsize[1]) or (size[1] == gsize[1] and size[0] < gsize[0])) and cut_like:
                    # kept, as the game and 1.666 take it (the owner, 05.10.2026: never stricter than the port)
                    rep.add('warning', "%s: %dx%d is a shorter sheet than the game's %dx%d (an older game's picture?) - kept, as the game takes it; where the game's crops reach past it the picture shows wrong: redo it from the export" % (n, size[0], size[1], gsize[0], gsize[1]))
                else:
                    rep.add('note', '%s: %dx%d where the game has %dx%d (no anm2 or fnt of the mod cuts it)' % (n, size[0], size[1], gsize[0], gsize[1]))
        if max(size) > MAX_TEX:
            im = shrink(im, (max(1, size[0] // 2), max(1, size[1] // 2)))
            taken[n] = png_bytes(im)
            shrunk[norm(n)] = 2
            rep.add('rescaled', '%s: %dx%d is over %d, halved' % (n, size[0], size[1], MAX_TEX))
    tk = {norm(t): t for t in taken}         # the mod's spelling by the normalized path
    for sheet, k in shrunk.items():
        gsize = game.png_size(sheet)
        for anm in sheets_of.get(sheet, []):
            root = anm2_roots.get(anm)
            if root is None:
                continue
            # baked down to the game's size, a mod anm2 that cuts it with the game's crops already fits
            # (the mod redrew the picture k times bigger and forgot this file)
            if gsize and tuple(gsize) == tuple(png_size_of(taken.get(tk.get(sheet))) or ()) and \
                    game_scale_frames(root, anm, sheet, gsize, k):
                rep.add('kept', "%s: cuts %s with the game's crops - fits the picture baked down to the game's size as it is"
                        % (anm, sheet.rsplit('/', 1)[-1]))
                continue
            ids = {sid for sid, s in anm2_sheets(root, anm).items() if s == sheet}
            scale_anm2_for(root, ids, rep, anm, k)
            taken[anm] = ET.tostring(root, encoding='utf-8', xml_declaration=True)
    # a mod anm2 that cuts a picture the mod ships k times the game's size with the GAME's crops (frames at
    # 100%, inside the game's sheet): the mod redrew the picture and not this file - on the PC too it draws
    # the top-left 1/k of the art (Reflashed: seedselectionwidget, giantbook poofs, the character page swap).
    # Its frames on that picture are fitted to it: crops and pivots times k, scale over k - the same size on
    # the screen, the mod's art whole
    for anm, root in anm2_roots.items():
        if anm not in taken:
            continue
        fitted = []
        for sid, sheet in sorted(anm2_sheets(root, anm).items()):
            key = tk.get(sheet)
            gsize = game.png_size(sheet)
            k = whole_k(png_size_of(taken.get(key)), gsize) if key else 0
            if k and norm(sheet) not in shrunk and game_scale_frames(root, anm, sheet, gsize, k):
                fit_anm2_up(root, {s for s, x in anm2_sheets(root, anm).items() if x == sheet}, k)
                fitted.append('%s x%d' % (sheet.rsplit('/', 1)[-1], k))
        if fitted:
            taken[anm] = ET.tostring(root, encoding='utf-8', xml_declaration=True)
            rep.add('repaired', "%s: its frames cut %s with the game's crops - the mod redrew the picture bigger and not "
                                "this file (the PC shows the top-left part too); fitted: crops times k, scale over k"
                    % (anm, ', '.join(fitted)))
    # the game's anm2 that only the iOS game has (IOS_ONLY_ANM2) cut the mod's pictures with the game's crops:
    # a copy of the game's file goes into the mod, its frames on a picture the mod ships k times the game's
    # size fitted to it - the iOS-only widget then shows the mod's art, as the PC's own widget does
    for rel in IOS_ONLY_ANM2:
        if rel in tk:
            continue
        gdata = game.read(rel)
        if not gdata:
            continue
        groot = ET.fromstring(gdata)
        fitted = []
        for sid, sheet in sorted(anm2_sheets(groot, rel).items()):
            key = tk.get(sheet)
            gsize = game.png_size(sheet)
            k = whole_k(png_size_of(taken.get(key)), gsize) if key else 0
            if k:
                fit_anm2_up(groot, {s for s, x in anm2_sheets(groot, rel).items() if x == sheet}, k)
                fitted.append('%s x%d' % (sheet.rsplit('/', 1)[-1], k))
        if fitted:
            taken[rel] = ET.tostring(groot, encoding='utf-8', xml_declaration=True)
            rep.add('repaired', "%s: the iOS game's own widget (no PC mod brings it) cuts %s of the mod - the game's file "
                                "put in, its frames fitted to the mod's picture" % (rel, ', '.join(fitted)))
    # the iOS game swaps some sheets for a "<name>_ios.png" of its own by code (measured
    # 28.09.2026: the title asks titlemenu.anm2, titlemenu.png, then titlemenu_ios.png):
    # the mod's picture goes under that name too, or the game's 1x sheet meets the mod's anm2
    for n in list(taken):
        if not n.lower().endswith('.png'):
            continue
        ios = n[:-4] + '_ios.png'
        # only a sheet the game swaps in BY CODE: no anm2 of the game cuts it, and it is the base's
        # size (titlemenu_ios.png); credits_page*_ios.png have an anm2 of their own (credits_ios.anm2)
        if (game.has(ios) and not game.cuts(ios) and game.png_size(ios) and game.png_size(ios) == game.png_size(n)
                and not any(norm(t) == norm(ios) for t in taken)):
            taken[ios] = taken[n]
            rep.add('repaired', '%s: the game asks for this iOS copy of %s by code; the mod\'s picture put under that name too' % (ios, os.path.basename(n)))

    # xml: a text table completed as the port's check completes it (the mod's records whole, the game's it lacks
    # added); more entries than the game's - kept with a warning
    for n in list(taken):
        low = n.lower()
        leaf = low.rsplit('/', 1)[-1]
        if not low.endswith('.xml'):
            continue
        gdata = game.read(n)
        if gdata is None:
            rep.add('kept', '%s: new to the game' % n)
            continue
        spec = TEXT_XML.get(leaf)
        if spec is None:
            rep.add('kept', '%s: replaces the game\'s whole' % n)
            skew_report(rep, n, taken[n], gdata)
            continue
        root, data = parse_xml(taken[n], n, rep)
        if root is None:
            rep.add('dropped', '%s: does not parse' % n)
            del taken[n]
            continue
        taken[n] = data
        groot = ET.fromstring(gdata)
        mc, gc = count_entries(root, spec[0]), count_entries(groot, spec[0])
        added = complete_table(root, groot)
        if added:
            taken[n] = ET.tostring(root, encoding='utf-8', xml_declaration=True)
            rep.add('merged', TABLE_COMPLETED % (n, added))
        if mc > gc:
            rep.add('warning', '%s: %d entries where the game has %d - new content, kept whole' % (n, mc, gc))
            skew_report(rep, n, taken[n], gdata)
        elif not added:
            rep.add('kept', '%s: %d entries, as the game' % (n, mc) if mc == gc else
                    "%s: %d entries where the game has %d, none of the game's missing by its key - kept whole" % (n, mc, gc))

    # rooms
    for n in list(taken):
        if not n.lower().endswith('.stb'):
            continue
        gdata = game.read(n)
        if gdata is None:
            rep.add('kept', '%s: new room file' % n)
            continue
        # the magic only: the room count after it is the mod's own (05.10.2026)
        if taken[n][:4] != gdata[:4]:
            rep.add('dropped', ROOM_REFUSED % (n, taken[n][:4], gdata[:4]))
            del taken[n]

    # files the port's built-ins bring into the game's tree (the EID icon atlases and faces, the boss bars'
    # pictures): a mod's copy replaces the built-in's own - the PC versions of those very mods bring them
    own = port_own_files()
    same, other = [], []
    for n in sorted(taken):
        mine = own.get(norm(n))
        if mine is not None:
            (same if open(mine, 'rb').read() == taken[n] else other).append(n)
    if same:
        rep.add('note', "%d files are the port's built-in's own, byte for byte (%s%s) - the mod's copies change nothing"
                % (len(same), ', '.join(x.rsplit('/', 1)[-1] for x in same[:3]), ', ...' if len(same) > 3 else ''))
    for n in other:
        rep.add('warning', "%s: replaces the port's built-in's own file (a different one) - the built-in will draw "
                           "the mod's; the PC version of a mod the port has built in is not needed" % n)

    # the manifest and the zip: the id by the installer's rules (UserMods.idWord), the card's id first,
    # then the PC card's directory, name and workshop number
    mid = ''
    for cand in (meta.get('_json_id'), meta.get('directory'), meta.get('name'), meta.get('id')):
        mid = id_word(cand or '')
        if mid:
            break
    mid = mid or 'mod'
    if not taken and not content_kept:
        # nothing the port reads: a script mod (or one of content/ only) - the installer would refuse the zip
        rep.add('refused', 'nothing here for the port: %s - no zip written, the installer would refuse it ("has no file '
                           'the port reads")' % ('a script mod; the port runs no scripts' if any(
                               x.lower().endswith('.lua') for x in files) else 'no resources/ the port reads'))
        report_at = (out or mid + '.zip') + '.report.txt'
        with open(report_at, 'w', encoding='utf-8', newline='\n') as f:
            f.write(rep.text())
        print(rep.text())
        print('no zip: nothing for the port; the report in %s' % report_at)
        return
    manifest = {
        'id': mid,
        'name': meta.get('name', mid),
        'version': str(meta.get('version', '1.0')),
        'author': meta.get('author', 'Unknown'),
        'description': meta.get('description', ''),
    }
    if not out:
        out = mid + '.zip'
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('mod.json', json.dumps(manifest, ensure_ascii=False, indent=2))
        for n, data in content_kept.items():
            z.writestr(n, data)
        for n in sorted(taken):
            data = taken[n]
            stored = n.lower().endswith(('.png', '.ogg', '.ogv', '.wav'))
            z.writestr(n, data, compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED)
    # the report goes BESIDE the zip: inside it would only be carried along unused
    # (UserMods.java reads mod.json, metadata.xml and preview.png at a mod's root)
    with open(out + '.report.txt', 'w', encoding='utf-8', newline='\n') as f:
        f.write(rep.text())
    print(rep.text())
    print('%d files into %s, the report in %s' % (len(taken), out, out + '.report.txt'))


if __name__ == '__main__':
    main()
