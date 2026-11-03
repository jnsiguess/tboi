# -*- coding: utf-8 -*-
"""The port's signature, laid into the OPTIONS screen.

The main menu's mark needed no code and no anm2 (tools/watermark.py): that
screen draws one sheet 1:1 over the whole field, so a mark painted into the
sheet lands where it was painted. The options screen is not built that way -
every one of its pieces is a small crop placed by hand, and the wall behind
them is a TEN-PIXEL tile blown up 48 times (layer 0, crop 244,1 10x10 at
4800%). There is nothing to paint into.

So the mark gets a layer of its own, and this writes it:

    a spritesheet entry for our own file
    a layer bound to it, last, so nothing draws over it
    and a frame of that layer in EVERY animation

The last line is the one that matters. The engine reaches a layer by the
NUMBER of its entry and dies when the entry is missing, so an animation left
without one would not merely hide the mark - it would take the game down. The
options screen has 21 animations (one per row of the list), and all 21 are
written. The frame's Delay is set to the animation's own FrameNum, because a
single frame has to cover the whole length of the animation.

WHERE it lands. The screen shows a WINDOW of this anm2 and scrolls down it as
the list is walked, so the world is 480x450 and not the 480x270 of the screen -
that caught me out once, and the rectangles below are the whole of it:

    paper (with its middles)   x 124..357, y  15..436
    Sketch  (dagger, face)     x  16.. 96, y  17..113
    Sketch2 (splash)           x 100..148, y  93..173
    Sketch4 (little man)       x 351..399, y   3.. 67
    Sketch3 (book with rays)   x 379..459, y 140..220
    Sketch5 (skull)            x -22.. 42, y 215..263

Below y 220 the right-hand wall is empty all the way down, and that is where
the mark goes: (365, 260), level with the EXTRA HUD row when the list is
scrolled to it. The owner chose the place on a picture of the whole world.

The picture is laid down as it is: nothing here scales, turns or thresholds it.

    py -3 tools/watermark_options.py [--check]
"""
import os
import re
import shutil
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'work', 'app')
MENU = os.path.join('gfx', 'ui', 'main menu')
ANM2 = 'optionsmenu.anm2'
KEEP = ANM2 + '.do-watermark'           # pack.py skips anything with a dot inside the name
SHEET = 'optionswatermark.png'          # ours, copied in beside the game's own
MARK = os.path.join(ROOT, 'menu-labels', 'watermark-options.png')

AT = (365, 260)                         # the mark's top left corner, in the anm2's own world

LAYER_NAME = 'Watermark'


def sets():
    """Every set that has an options screen of its own - LANGUAGES INCLUDED.

    This used to look only in <edition>/data, and that is exactly why the mark
    showed in English and nowhere else: the six translated sets carry their own
    optionsmenu.anm2 (repentance-resources/dlc3.de, .es, .jp, .kr, .ru, .zh)
    and the game loads the one for the language it is set to. The main menu's
    mark never had this fault - watermark.py walked the language folders from
    the start - so the two behaved differently for no reason anybody could see.
    """
    for edition in sorted(os.listdir(APP)):
        base = os.path.join(APP, edition)
        if not os.path.isdir(base):
            continue
        for lang in sorted(os.listdir(base)):
            folder = os.path.join(base, lang, MENU)
            if os.path.exists(os.path.join(folder, ANM2)):
                yield edition + '/' + lang, folder


def frame(w, h, delay):
    """One frame of our layer: the whole picture, at AT, held for the animation."""
    return ('<Frame XPosition="%d" YPosition="%d" XPivot="0" YPivot="0" XCrop="0" YCrop="0" '
            'Width="%d" Height="%d" XScale="100" YScale="100" Delay="%d" Visible="true" '
            'RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" '
            'GreenOffset="0" BlueOffset="0" Rotation="0" Interpolated="false"/>'
            % (AT[0], AT[1], w, h, delay))


def sign(text, w, h):
    """The anm2 with our spritesheet, our layer, and a frame of it everywhere."""
    if LAYER_NAME in text:
        raise SystemExit('the anm2 already carries a layer called ' + LAYER_NAME)

    # \bId= and not \sId=, and both halves of that matter.
    #
    # SpritesheetId ends in the same two letters, and a greedy [^>]* walked
    # right past the layer's own number to it - the first run of this handed
    # the new layer an id that was already taken. A word boundary refuses that
    # just as well, because there is none inside "SpritesheetId".
    #
    # And it must not demand a SPACE before Id: the translated sets write their
    # attributes the other way round - <Spritesheet Id="0" Path="..."/> where
    # English has <Spritesheet Path="..." Id="0"/> - so a pattern anchored on
    # the space found nothing in them at all.
    sid = max(int(n) for n in re.findall(r'<Spritesheet [^>]*\bId="(\d+)"', text)) + 1
    lid = max(int(n) for n in re.findall(r'<Layer [^>]*\bId="(\d+)"', text)) + 1

    last_sheet = list(re.finditer(r'<Spritesheet [^>]*/>', text))[-1]
    text = (text[:last_sheet.end()]
            + '\n\t\t<Spritesheet Path="%s" Id="%d" />' % (SHEET, sid)
            + text[last_sheet.end():])

    last_layer = list(re.finditer(r'<Layer [^>]*/>', text))[-1]
    text = (text[:last_layer.end()]
            + '\n\t\t<Layer Name="%s" Id="%d" SpritesheetId="%d" />' % (LAYER_NAME, lid, sid)
            + text[last_layer.end():])

    done = [0]

    def one(mo):
        body = mo.group(0)
        num = int(re.search(r'FrameNum="(\d+)"', body).group(1))
        block = ('\n\t\t\t\t<LayerAnimation LayerId="%d" Visible="true">\n\t\t\t\t\t%s'
                 '\n\t\t\t\t</LayerAnimation>' % (lid, frame(w, h, num)))
        at = body.rindex('</LayerAnimations>')
        done[0] += 1
        return body[:at] + block + '\n\t\t\t' + body[at:]

    text = re.sub(r'<Animation Name="[^"]+".*?</Animation>', one, text, flags=re.S)
    return text, done[0], lid, sid


def main():
    check = '--check' in sys.argv
    if not os.path.exists(MARK):
        raise SystemExit('draw it first: ' + MARK)
    mark = Image.open(MARK).convert('RGBA')
    w, h = mark.size
    print('the mark is %dx%d, at %d,%d .. %d,%d of the anm2 world'
          % (w, h, AT[0], AT[1], AT[0] + w, AT[1] + h))

    for name, folder in sets():
        path = os.path.join(folder, ANM2)
        keep = os.path.join(folder, KEEP)
        source = keep if os.path.exists(keep) else path
        text = open(source, encoding='utf-8', errors='replace').read()
        if check:
            print('   %-30s would be signed (from %s)'
                  % (name, 'the kept copy' if source == keep else 'the anm2 itself'))
            continue
        if not os.path.exists(keep):
            shutil.copyfile(path, keep)
        signed, n, lid, sid = sign(text, w, h)
        open(path, 'w', encoding='utf-8', newline='').write(signed)
        mark.save(os.path.join(folder, SHEET))
        print('   %-30s signed: layer %d, sheet %d, in %d animations' % (name, lid, sid, n))


RETIRED = ("retired 02.10.2026 (plan 30.09, M8): never applied (no optionswatermark.png anywhere), and it wrote a layer into optionsmenu.anm2 of every edition, the game's files. A port mark on the settings paper would be a layer of the port's patch now: work/port-edit + tools/port_patch.py --make.")

if __name__ == '__main__':
    sys.exit(RETIRED)
