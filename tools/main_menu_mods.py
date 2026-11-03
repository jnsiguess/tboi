# -*- coding: utf-8 -*-
"""The main menu's MODS item, put back the PC's way, in all seven sets.

The PC's Repentance menu has six items - NEW RUN, CONTINUE, CHALLENGES, MODS,
STATS, OPTIONS - and the iOS build five. The word itself never left: every
set's gamemenu.png still has MODS at (195, 475, 80, 39), in its own language
(MODS in each language, MOD, 모드, 模组). What left is the markup, so the markup is taken
from the PC's own gamemenu.anm2 (work/pc-resources/resources-dlc3, extracted
with the game's ResourceExtractor) and laid over ours, which keeps everything
the port added (the strap layers):

  - Item1..Item5 stand where the PC stands them - the list is tighter, to
    make room;
  - Item6, MODS, is a new layer, a copy of Item5's record with the PC's frame;
  - the Cursor gets the PC's six places;
  - the touch areas are the PC's, in the order the items stand on the paper:
    Area1..Area6 are the six items top to bottom (the PC names STATS Area6
    and OPTIONS Area5; the game here numbers an area by its place in the
    list, so ours are in list order), and AreaDailyRun moves from 6 to 7.
    Nothing in the image asks for a null by name (no name is in it), and the
    only index the main menu asks for by number is 0, the challenge button
    (0x7f9f0c).

The code that goes with it - six items instead of five - is loader.c,
main_menu_mods.

    py -3 tools/main_menu_mods.py
"""
import copy
import os
import shutil
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
RES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
PC = os.path.join(ROOT, 'work', 'pc-resources', 'resources-dlc3', 'gfx', 'ui', 'main menu',
                  'gamemenu.anm2')
MENU = os.path.join('gfx', 'ui', 'main menu')
SETS = ['data', 'dlc3.ru', 'dlc3.de', 'dlc3.es', 'dlc3.jp', 'dlc3.kr', 'dlc3.zh']

# our area -> the PC's area it takes its place from (list order)
AREAS = {'Area1': 'Area1', 'Area2': 'Area2', 'Area3': 'Area3', 'Area4': 'Area4',
         'Area5': 'Area6', 'Area6': 'Area5'}


def layer_frames(root):
    names = {l.get('Id'): l.get('Name') for l in root.iter('Layer')}
    out = {}
    for anim in root.iter('Animation'):
        for la in anim.iter('LayerAnimation'):
            out[names[la.get('LayerId')]] = la
    return out


def null_frames(root):
    names = {n.get('Id'): n.get('Name') for n in root.iter('Null')}
    out = {}
    for anim in root.iter('Animation'):
        for na in anim.iter('NullAnimation'):
            out[names[na.get('NullId')]] = na
    return out


def main():
    pc = ET.parse(PC).getroot()
    pc_layers = layer_frames(pc)
    pc_nulls = null_frames(pc)
    for s in SETS:
        path = os.path.join(RES, s, MENU, 'gamemenu.anm2')
        keep = path + '.before-mods'
        if not os.path.exists(keep):
            shutil.copyfile(path, keep)
        tree = ET.parse(keep)
        root = tree.getroot()
        ours = layer_frames(root)

        # the five items and the cursor at the PC's places
        for name in ('Item1', 'Item2', 'Item3', 'Item4', 'Item5'):
            for mine, theirs in zip(ours[name].iter('Frame'), pc_layers[name].iter('Frame')):
                for k in ('XPosition', 'YPosition'):
                    mine.set(k, theirs.get(k))
        cur = ours['Cursor']
        template = list(cur.iter('Frame'))[0]
        for f in list(cur):
            cur.remove(f)
        for theirs in pc_layers['Cursor'].iter('Frame'):
            f = copy.deepcopy(template)
            for k in ('XPosition', 'YPosition'):
                f.set(k, theirs.get(k))
            cur.append(f)

        # Item6: a new layer, a copy of Item5's record, right after it in the drawing order
        layers = root.find('.//Layers')
        new_id = str(max(int(l.get('Id')) for l in layers) + 1)
        item5 = [l for l in layers if l.get('Name') == 'Item5'][0]
        item6 = ET.Element('Layer', {'Name': 'Item6', 'Id': new_id,
                                     'SpritesheetId': item5.get('SpritesheetId')})
        # at the END of the list: a layer is reached by its place in the list, so every
        # record keeps its place and only the drawing order puts MODS after Item5
        layers.append(item6)
        pc6 = list(pc_layers['Item6'].iter('Frame'))[0]
        for anim in root.iter('Animation'):
            las = anim.find('LayerAnimations')
            if las is None:
                continue
            src = [la for la in las if la.get('LayerId') == item5.get('Id')][0]
            la6 = copy.deepcopy(src)
            la6.set('LayerId', new_id)
            f = list(la6.iter('Frame'))[0]
            for extra in list(la6)[1:]:
                la6.remove(extra)
            # the PC's pivot with its position: MODS is pinned at 16,16 there, and
            # Item5's 0,0 put the word 16 down and 16 right, over STATS (24.09.2026)
            for k in ('XPosition', 'YPosition', 'XPivot', 'YPivot', 'XCrop', 'YCrop', 'Width', 'Height'):
                f.set(k, pc6.get(k))
            las.insert(list(las).index(src) + 1, la6)

        # the touch areas: six in list order, the daily run seventh
        nulls = root.find('.//Nulls')
        daily = [n for n in nulls if n.get('Name') == 'AreaDailyRun'][0]
        old_daily = daily.get('Id')
        daily.set('Id', '7')
        nulls.append(ET.Element('Null', {'Id': '6', 'Name': 'Area6'}))
        nulls[:] = sorted(nulls, key=lambda n: int(n.get('Id')))
        for anim in root.iter('Animation'):
            nas = anim.find('NullAnimations')
            if nas is None:
                continue
            by_id = {na.get('NullId'): na for na in nas}
            dna = by_id.get(old_daily)
            if dna is not None:
                dna.set('NullId', '7')
            a5 = by_id.get('5')
            if a5 is not None:
                a6 = copy.deepcopy(a5)
                a6.set('NullId', '6')
                nas.append(a6)
            names = {'1': 'Area1', '2': 'Area2', '3': 'Area3', '4': 'Area4', '5': 'Area5',
                     '6': 'Area6'}
            for na in nas:
                ours_name = names.get(na.get('NullId'))
                if not ours_name:
                    continue
                theirs = list(pc_nulls[AREAS[ours_name]].iter('Frame'))[0]
                f = list(na.iter('Frame'))[0]
                for k in ('XPosition', 'YPosition', 'XScale', 'YScale', 'Rotation'):
                    f.set(k, theirs.get(k))
            nas[:] = sorted(nas, key=lambda n: int(n.get('NullId')))

        tree.write(path, encoding='utf-8', xml_declaration=True)
        print('  %-8s six items, Item6 is layer %s, areas 1..6 + daily 7' % (s, new_id))


if __name__ == '__main__':
    main()
