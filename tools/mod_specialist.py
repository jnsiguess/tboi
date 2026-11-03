# -*- coding: utf-8 -*-
"""Stage the "Specialist for Good Items" mod's files as port assets.

The mod's costumes and music are loaded by the game's own XML loaders in
their mod mode (mods.c), so the port needs:

  assets/port_specialist/                 every .anm2 and picture, same tree
  assets/port_specialist/costumes.xml     type="none" costumes only, in the
                                          order mods.c counts them (ids 132..)
  assets/port_specialist/music.xml        the four tracks (ids 119..122)

The anm2 files name their sheets with backslashes (sheets\\isaac.png); the
pack looks paths up with forward slashes, so they are rewritten. A picture no
staged anm2 names is left out (the mod still carries sheets/t_cain.png, the
Tainted Cain sheet before r1.4).

Everything lands under gfx/port_specialist/ in the resource table, which is
what anm2root and the music root below say.

Mod version 1.5.1 ("Taint to the Party", r1.4..r1.5.1): every tainted
character has a dance of its own, the tainted have their own remixes.
"""
import os
import re
import shutil
import sys

MOD = r'Z:\Games\Steam\steamapps\common\The Binding of Isaac Rebirth\mods\specialistforgooditems_2575911103'
PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(PROJECT, 'port', 'app', 'src', 'main', 'assets', 'port_specialist')
ROOT = 'gfx/port_specialist/'

# the order is the id order: mods.c uses COSTUME_FIRST + index (its C_* enum).
# The first 22 keep the order they had before the tainted update, so their ids
# do not move; the tainted follow in the mod's costumes2.xml order. The mod's
# xml also lists specialist_t_samson2.anm2, a file it does not ship and its Lua
# never asks for - left out.
COSTUMES = [
    'isaac', 'magdalene', 'cain', 'judas', 'dark_judas', 'xxx', 'eve', 'samson',
    'azazel', 'lazarus', 'lazarus2', 'eden', 'lost', 'lilith', 'keeper', 'apollyon',
    'forgor_bone', 'forgor_soul', 'bethany', 'jacob', 'esau', 't_cain',
    't_apollyon', 't_azazel', 't_bethany', 't_bluebaby', 't_eden', 't_eve',
    't_forgor_bone', 't_forgor_soul', 't_isaac', 't_jacob', 't_jacob2', 't_judas',
    't_keeper', 't_lazarus', 't_lazarus2', 't_lilith', 't_lost', 't_magdalene',
    't_samson',
]
BINKY = 'items/binky.anm2'

# the order is the id order: mods.c uses TRACK_FIRST + index (its T_* enum);
# names and files as the mod's music.xml has them
TRACKS = [
    ('specialist', 'arcadespecalist.ogg'),
    ('specialist_b', 'arcadespecalist_b.ogg'),
    ('paincialist', 'crackhousespecialist.ogg'),
    ('look_man_i_really_tried_to_fix_the_earrape_in_the_default_specialist_but_nothing_works_im_genuinely_clueless',
     'arcadespecalist_eden.ogg'),
]


def main():
    src = os.path.join(MOD, 'resources', 'the')
    if not os.path.isdir(src):
        sys.exit('the mod is not at ' + MOD)
    anm2s = ['specialist_%s.anm2' % n for n in COSTUMES] + [BINKY]
    for rel in anm2s:
        if not os.path.isfile(os.path.join(src, rel)):
            sys.exit('the mod has no ' + rel)
    for _, path in TRACKS:
        if not os.path.isfile(os.path.join(src, path)):
            sys.exit('the mod has no ' + path)

    # the sheets the staged anm2s name, as forward-slash paths relative to src
    texts, sheets = {}, set()
    for rel in anm2s:
        text = open(os.path.join(src, rel), encoding='utf-8').read()
        text = re.sub(r'(Path=")([^"]*)(")',
                      lambda m: m.group(1) + m.group(2).replace('\\', '/') + m.group(3), text)
        texts[rel] = text
        base = os.path.dirname(rel)
        for p in re.findall(r'<Spritesheet Path="([^"]*)"', text):
            sheet = os.path.normpath(os.path.join(base, p)).replace(os.sep, '/')
            if not os.path.isfile(os.path.join(src, sheet)):
                sys.exit('%s names %s, which the mod does not have' % (rel, sheet))
            sheets.add(sheet)

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    copied, skipped = 0, []
    for rel, text in texts.items():
        dst = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'w', encoding='utf-8', newline='\n').write(text)
        copied += 1
    for folder, _, files in os.walk(src):
        rel_dir = os.path.relpath(folder, src)
        for name in files:
            rel = name if rel_dir == '.' else os.path.join(rel_dir, name).replace(os.sep, '/')
            if not name.lower().endswith('.png'):
                continue
            if rel not in sheets:
                skipped.append(rel)
                continue
            dst = os.path.join(OUT, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(folder, name), dst)
            copied += 1
    for _, path in TRACKS:
        shutil.copyfile(os.path.join(src, path), os.path.join(OUT, path))
        copied += 1

    lines = ['<costumes anm2root="%s">' % ROOT]
    for name in COSTUMES:
        lines.append('\t<costume anm2path="specialist_%s.anm2" isFlying="true" priority="969" type="none"/>' % name)
    lines.append('\t<costume anm2path="%s" isFlying="true" priority="969" type="none"/>' % BINKY)
    lines.append('</costumes>')
    open(os.path.join(OUT, 'costumes.xml'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')

    lines = ['<music root="%s">' % ROOT]
    for name, path in TRACKS:
        lines.append('\t<track name="%s" path="%s" loop="true" />' % (name, path))
    lines.append('</music>')
    open(os.path.join(OUT, 'music.xml'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print('%d files staged, %d costumes, %d tracks; pictures no anm2 names, left out: %s'
          % (copied, len(COSTUMES) + 1, len(TRACKS), ', '.join(sorted(skipped)) or 'none'))


if __name__ == '__main__':
    main()
