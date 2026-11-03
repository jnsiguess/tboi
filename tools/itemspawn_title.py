# -*- coding: utf-8 -*-
"""The ITEM SPAWN heading of the spawner's card, in the game's own hand.

The owner (18.09.2026): the card's heading is to be the game's main font with
the stroke under it, as the Tweaks page's title is; the rows stay in the
description face. So the heading is made exactly the way tweaks_page.py makes
its title - title_art, one per language set - and written beside a one-layer
.anm2 the port draws as a sprite (spawner.c).

    py -3 tools/itemspawn_title.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tweaks_page              # noqa: E402

# set -> the heading; the strap says it and so does the card. ITEM SPAWN until
# 25.10.2026, CONSOLE since (the owner: "rename Item Spawn to Console in every
# language, everywhere")
TITLES = {
    'data': 'CONSOLE',
    'dlc3.ru': 'КОНСОЛЬ',
    'dlc3.de': 'KONSOLE',
    'dlc3.es': 'CONSOLA',
    'dlc3.jp': 'コンソール',
    'dlc3.kr': '콘솔',
    'dlc3.zh': '控制台',
}

ANM2 = '''<AnimatedActor>
\t<Info CreatedBy="Isaac Android port" CreatedOn="19/09/2026" Version="2" Fps="30"/>
\t<Content>
\t\t<Spritesheets>
\t\t\t<Spritesheet Path="port_itemspawn_title_%(suffix)s.png" Id="0"/>
\t\t</Spritesheets>
\t\t<Layers>
\t\t\t<Layer Name="Title" Id="0" SpritesheetId="0"/>
\t\t</Layers>
\t\t<Nulls/>
\t\t<Events/>
\t</Content>
\t<Animations DefaultAnimation="Idle">
\t\t<Animation Name="Idle" FrameNum="1" Loop="false">
\t\t\t<RootAnimation>
\t\t\t\t<Frame XPosition="0" YPosition="0" XScale="100" YScale="100" Delay="1" Visible="true" RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0" Interpolated="false"/>
\t\t\t</RootAnimation>
\t\t\t<LayerAnimations>
\t\t\t\t<LayerAnimation LayerId="0" Visible="true">
\t\t\t\t\t<Frame XPosition="0" YPosition="0" XPivot="0" YPivot="0" XCrop="0" YCrop="0" Width="%(w)d" Height="%(h)d" XScale="100" YScale="100" Delay="1" Visible="true" RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0" Interpolated="false"/>
\t\t\t\t</LayerAnimation>
\t\t\t</LayerAnimations>
\t\t\t<NullAnimations/>
\t\t\t<Triggers/>
\t\t</Animation>
\t</Animations>
</AnimatedActor>
'''


def main():
    sizes = []
    for set_name, suffix, _title, _names, _scale in tweaks_page.SETS:
        art = tweaks_page.title_art(set_name, TITLES[set_name])
        art.save(os.path.join(tweaks_page.ASSETS, 'port_itemspawn_title_%s.png' % suffix))
        with open(os.path.join(tweaks_page.ASSETS, 'port_itemspawn_title_%s.anm2' % suffix), 'w',
                  encoding='utf-8', newline='\n') as f:
            f.write(ANM2 % {'suffix': suffix, 'w': art.width, 'h': art.height})
        sizes.append((suffix, art.width, art.height))
        print('  %-3s %d x %d' % (suffix, art.width, art.height))
    with open(os.path.join(tweaks_page.ROOT, 'port', 'app', 'src', 'main', 'cpp',
                           'spawner_title.h'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* Written by tools/itemspawn_title.py - do not edit by hand, run the tool. */\n')
        f.write('#ifndef SPAWNER_TITLE_H\n#define SPAWNER_TITLE_H\n\n')
        f.write('/* one per language set, in the order of tweaks_layout.h: width and height */\n')
        f.write('static const struct { const char *suffix; short w, h; } SP_TITLE[%d] = {\n' % len(sizes))
        for suffix, w, h in sizes:
            f.write('    { "%s", %d, %d },\n' % (suffix, w, h))
        f.write('};\n\n#endif\n')


if __name__ == '__main__':
    main()
