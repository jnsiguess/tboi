# -*- coding: utf-8 -*-
"""The port's three straps, as LAYERS of the menus' own .anm2 files.

A strap sticks out from under a menu paper: Tweaks on the settings paper, Item
Spawn on the pause paper, Port Credits on the title paper. Under the paper is a
matter of the order the game draws the layers in, so each strap is a layer of
the menu's own .anm2, drawn before the paper - nothing is laid over anything.

Each strap is SLIDE + 1 layers: in place, and slid out by 1, 2 ... SLIDE pixels.
A strap chosen from a pad slides out through them one pixel at a time, and back
the same way (straps.c shows one of them per frame). Whole pixels only: a strap
drawn between texels shimmers.

The strap has no art of its own under the paper, and a slid-out strap would
show its torn end, so the picture is lengthened on the left. The owner's rule
(18.09.2026): the brown outline is never more than two pixels tall. The first
build carried each row's leftmost colour on, and where the top outline steps
down that stacked three rows of brown. Now everything left of the first CLEAN
column - two of outline, the body, two of outline, the shadow, with no gaps - is
that column repeated, EXTEND columns further than the picture went. The torn end
was under the paper; the few of its pixels that showed beside the paper edge
were outline and body, which the clean column has in the same rows.

    settings  optionsmenu.anm2  animation Paper13, after the wall and sketches,
              before the paper pieces; frame at the paper head's (127, 15) plus
              the mock-up's (210, 49)
    pause     pausescreen.anm2  layers rendered by id, not by order (0x8a23b4
              renders 0..7): the strap copies every frame of the Paper layer -
              position, scale, tint, timing - with its pivot moved so it keeps
              the mock-up's (214, 85) of the paper crop through the slide-in
    title     gamemenu.anm2     animation Idle, first, before Main; frame at the
              owner's (321, 141). The plaque the title sheet carried is taken
              out of it (the owner's own backdrop with the strap's pixels
              cleared), since the plaque is a layer now.

Sources: menu-labels/owner-art/straps/ - options/ and pause screen/ (the owner's
straps and mock-ups), title/ (exported from his "other stuff/custom credits.psd":
paper.png at 321,141, button.png at 333,150, sheet.png = his 'edited' and
'shadow' layers, the title paper without the strap).

The .anm2 files and the title sheets are rebuilt from the copies kept beside
them (*.before-strap-layer, *.png.do-watermark), so running this twice gives the
same as running it once.
"""
import os
import re
import sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# M8 (plan 30.09): the game's files in work/app stay vanilla; the port's versions of
# them live in work/port-edit, and tools/port_patch.py makes port_patch/ out of them.
RES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
ART = os.path.join(ROOT, 'menu-labels', 'owner-art')
KEEP = '.before-strap-layer'

EXTEND = 6
SLIDE = 5
STEPS = SLIDE + 1                       # a layer per whole pixel of the slide, nought included
NAMES = ['PortStrap%d' % k for k in range(STEPS)]

# the Tweaks strap on the settings sheet, the spawner strap on the pause sheet
TWEAKS_AT = (127 + 210, 15 + 49)
SPAWNER_FROM_ANCHOR = (94, -3)          # paper top-left from the pause paper's pivot point
PAUSE_PAPER_PIVOT = (120, 88)
CREDITS_AT = (321, 141)
CREDITS_WORDS_AT = (333, 150)
# the owner's lettering with the smoothing he drew himself (two tones), not the
# PSD's layer - he pointed it out twice
CREDITS_WORDS = os.path.join(ART, 'credits button.png')
PLAQUE = (328, 141, 403, 246)           # the rectangle of the title sheet the owner changed

FRAME = ('<Frame XPosition="{x}" YPosition="{y}" XPivot="{px}" YPivot="{py}" XCrop="0" YCrop="0" '
         'Width="{w}" Height="{h}" XScale="100" YScale="100" Delay="{d}" Visible="{v}" RedTint="255" '
         'GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" '
         'Rotation="0" Interpolated="false" />')


def clean_column(paper):
    """the first column that is outline 2, body, outline 2, shadow - nothing missing"""
    src = paper.load()
    for x in range(paper.width):
        runs = []
        for y in range(paper.height):
            c = src[x, y]
            key = None if c[3] == 0 else c[:3]
            if runs and runs[-1][0] == key:
                runs[-1][1] += 1
            else:
                runs.append([key, 1])
        # clear above and below is allowed; a gap inside the paint is not
        while runs and runs[0][0] is None:
            runs.pop(0)
        while runs and runs[-1][0] is None:
            runs.pop()
        if (len(runs) == 4 and runs[0][0] == runs[2][0] and runs[0][1] == 2
                and runs[2][1] == 2 and runs[1][1] > 4):
            return x
    raise SystemExit('no clean column in the strap')


def lengthened(paper):
    ref = clean_column(paper)
    out = Image.new('RGBA', (paper.width + EXTEND, paper.height), (0, 0, 0, 0))
    out.alpha_composite(paper.crop((ref, 0, paper.width, paper.height)), (EXTEND + ref, 0))
    column = paper.crop((ref, 0, ref + 1, paper.height))
    for x in range(EXTEND + ref):
        out.alpha_composite(column, (x, 0))
    return out


def strap(paper_path, words_path, words_at):
    paper = Image.open(paper_path).convert('RGBA')
    words = Image.open(words_path).convert('RGBA')
    out = lengthened(paper)
    out.alpha_composite(words, (words_at[0] + EXTEND, words_at[1]))
    return out


def kept(path):
    """the untouched copy of a file, made on the first run"""
    keep = path + KEEP
    if not os.path.exists(keep):
        with open(path, 'rb') as a, open(keep, 'wb') as b:
            b.write(a.read())
    return open(keep, encoding='utf-8').read()


def add_sheet_and_layers(text, sheet_name, layer_names):
    # \sId, or a layer's SpritesheetId="2" would pass for its own id
    sheets = [int(i) for i in re.findall(r'<Spritesheet\b[^>]*\sId="(\d+)"', text)]
    layers = [int(i) for i in re.findall(r'<Layer\b[^>]*\sId="(\d+)"', text)]
    sheet_id = max(sheets) + 1
    first = max(layers) + 1
    # after the last sheet and the last layer, in their own indentation: the
    # files differ, and some close </Layers> on the last layer's own line
    last = list(re.finditer(r'\n(\t*)<Spritesheet [^>]*/>', text))[-1]
    text = (text[:last.end()] + '\n%s<Spritesheet Path="%s" Id="%d" />' % (last.group(1), sheet_name, sheet_id)
            + text[last.end():])
    last = list(re.finditer(r'\n(\t*)<Layer [^>]*/>', text))[-1]
    added = ''.join('\n%s<Layer Name="%s" Id="%d" SpritesheetId="%d" />' % (last.group(1), n, first + k, sheet_id)
                    for k, n in enumerate(layer_names))
    text = text[:last.end()] + added + text[last.end():]
    return text, first


def layer_block(layer_id, frames):
    return ('\t\t\t\t<LayerAnimation LayerId="%d" Visible="true">\n%s\t\t\t\t</LayerAnimation>\n'
            % (layer_id, ''.join('\t\t\t\t\t%s\n' % f for f in frames)))


def each_animation(text, fn):
    """fn(name, frame_count, body) -> body; applied to every animation"""
    def one(m):
        return m.group(1) + fn(m.group(2), int(m.group(3)), m.group(4)) + m.group(5)
    return re.sub(r'(<Animation Name="([^"]*)" FrameNum="(\d+)"[^>]*>)(.*?)(</Animation>)', one, text, flags=re.S)


def hidden(layer_id, w, h, frames):
    return layer_block(layer_id, [FRAME.format(x=0, y=0, px=0, py=0, w=w, h=h, d=frames, v='false')])


def all_hidden(first, w, h, frames):
    return ''.join(hidden(first + k, w, h, frames) for k in range(STEPS))


def with_hidden(body, first, w, h, frames):
    """the hidden layers before the animation's closing tag, however it is laid out

    Three animations of the settings file (PenOpacity, Rumble, TouchMode - ours,
    written by earlier tools) close </LayerAnimations> on the last layer's own
    line; a replace anchored on its indentation missed them and left them with
    no entry for the strap layers at all (found 18.09.2026).
    """
    at = body.index('</LayerAnimations>')
    return body[:at] + '\n' + all_hidden(first, w, h, frames) + '\t\t\t' + body[at:]


def settings(folder, image):
    path = os.path.join(RES, folder, 'gfx', 'ui', 'main menu', 'optionsmenu.anm2')
    text, first = add_sheet_and_layers(kept(path), 'port_strap_tweaks.png', NAMES)
    w, h = image.size

    def fn(name, frames, body):
        if name != 'Paper13':
            return with_hidden(body, first, w, h, frames)
        block = ''.join(layer_block(first + k, [FRAME.format(
            x=TWEAKS_AT[0] - EXTEND + k, y=TWEAKS_AT[1], px=0, py=0, w=w, h=h, d=frames, v='true')])
            for k in range(STEPS))
        # after the sketches (layer 10), before the first paper piece
        at = re.search(r'<LayerAnimation LayerId="10".*?</LayerAnimation>\n', body, re.S)
        return body[:at.end()] + block + body[at.end():]
    open(path, 'w', encoding='utf-8', newline='\n').write(each_animation(text, fn))
    return first


def pause(folder, image):
    path = os.path.join(RES, folder, 'gfx', 'ui', 'pausescreen.anm2')
    if not os.path.exists(path):
        return None
    text, first = add_sheet_and_layers(kept(path), 'port_strap_spawner.png', NAMES)
    w, h = image.size
    # The strap's top-left is the paper's PIVOT point plus SPAWNER_FROM_ANCHOR -
    # the point the finger is judged against (spawner.c, strap_place) - so its own
    # pivot is minus that. The first build measured the offset from the crop's
    # corner instead, 120 left and 88 up of the pivot, and the strap stuck out of
    # the paper's top edge while the finger found it where it should have been.
    pivot_x = -(SPAWNER_FROM_ANCHOR[0] - EXTEND)
    pivot_y = -SPAWNER_FROM_ANCHOR[1]

    def fn(name, frames, body):
        # NOT a self-closing entry: the Cursor animation (the overlay the game plays
        # at the chosen item) lists the paper as <LayerAnimation LayerId="1" .../>,
        # and a pattern that took that tag ran on to the cursor's own frames - the
        # strap then followed the cursor under the paper (the owner, 19.09.2026)
        paper = re.search(r'<LayerAnimation LayerId="1"[^>/]*>(.*?)</LayerAnimation>', body, re.S)
        if not paper:
            return with_hidden(body, first, w, h, frames)
        blocks = ''
        for k in range(STEPS):
            copies = []
            for f in re.findall(r'<Frame [^>]*/>', paper.group(1)):
                f = re.sub(r'XPivot="[^"]*"', 'XPivot="%d"' % (pivot_x - k), f)
                f = re.sub(r'YPivot="[^"]*"', 'YPivot="%d"' % pivot_y, f)
                f = re.sub(r'XCrop="[^"]*"', 'XCrop="0"', f)
                f = re.sub(r'YCrop="[^"]*"', 'YCrop="0"', f)
                f = re.sub(r'Width="[^"]*"', 'Width="%d"' % w, f)
                f = re.sub(r'Height="[^"]*"', 'Height="%d"' % h, f)
                copies.append(f)
            blocks += layer_block(first + k, copies)
        return body[:paper.end()] + '\n' + blocks.rstrip('\n') + body[paper.end():]
    open(path, 'w', encoding='utf-8', newline='\n').write(each_animation(text, fn))
    return first


def title(folder, image, backdrop):
    path = os.path.join(RES, folder, 'gfx', 'ui', 'main menu', 'gamemenu.anm2')
    text, first = add_sheet_and_layers(kept(path), 'port_strap_credits.png', NAMES)
    w, h = image.size

    def fn(name, frames, body):
        if name != 'Idle':
            return with_hidden(body, first, w, h, frames)
        block = ''.join(layer_block(first + k, [FRAME.format(
            x=CREDITS_AT[0] - EXTEND + k, y=CREDITS_AT[1], px=0, py=0, w=w, h=h, d=frames, v='true')])
            for k in range(STEPS))
        return body.replace('\t\t\t<LayerAnimations>\n', '\t\t\t<LayerAnimations>\n' + block, 1)
    open(path, 'w', encoding='utf-8', newline='\n').write(each_animation(text, fn))

    # the sheet: the untouched one with the owner's rectangle laid in - his
    # PSD's 'edited' and 'shadow' layers, the title paper without the strap
    sheet_path = os.path.join(RES, folder, 'gfx', 'ui', 'main menu', 'gamemenu.png')
    sheet = Image.open(sheet_path + '.do-watermark').convert('RGBA')
    sheet.paste(backdrop.crop(PLAQUE), PLAQUE[:2])
    sheet.save(sheet_path)
    return first


def main():
    tweaks = strap(os.path.join(ART, 'straps', 'options', 'paper.png'),
                   os.path.join(ART, 'straps', 'options', 'button.png'), (9, 9))
    spawner = strap(os.path.join(ART, 'straps', 'pause screen', 'paper.png'),
                    os.path.join(ART, 'straps', 'pause screen', 'button.png'), (18, 10))
    credits = strap(os.path.join(ART, 'straps', 'title', 'paper.png'),
                    CREDITS_WORDS,
                    (CREDITS_WORDS_AT[0] - CREDITS_AT[0], CREDITS_WORDS_AT[1] - CREDITS_AT[1]))
    backdrop = Image.open(os.path.join(ART, 'straps', 'title', 'sheet.png')).convert('RGBA')

    firsts = {}
    for folder in sorted(os.listdir(RES)):
        if not os.path.isdir(os.path.join(RES, folder, 'gfx', 'ui', 'main menu')):
            continue
        ui = os.path.join(RES, folder, 'gfx', 'ui')
        tweaks.save(os.path.join(ui, 'main menu', 'port_strap_tweaks.png'))
        spawner.save(os.path.join(ui, 'port_strap_spawner.png'))
        credits.save(os.path.join(ui, 'main menu', 'port_strap_credits.png'))
        got = (settings(folder, tweaks), pause(folder, spawner), title(folder, credits, backdrop))
        firsts.setdefault(got, []).append(folder)
    for (s, p, t), folders in firsts.items():
        print('layers: settings %s, pause %s, title %s  <- %s' % (s, p, t, ', '.join(folders)))
    if len({(s, t) for s, _, t in firsts}) != 1 or len({p for _, p, _ in firsts if p is not None}) != 1:
        sys.exit('the layer ids differ between the language sets - the port reads one id each')


RETIRED = ("retired 02.10.2026 (plan 30.09, M8): this rebuilt the menus' .anm2 and the title sheet from the copies of 17.09 (*.before-strap-layer, *.png.do-watermark) and so undid every edit made after - guide_strap, main_menu_mods, lang, labels. The straps are in work/port-edit and in port_patch/ now; change them there (tools/guide_strap.py, tools/strap_lang) and run tools/port_patch.py --make. Its numbers and helpers stay importable (guide_strap.py uses RES, FRAME, clean_column...).")

if __name__ == '__main__':
    sys.exit(RETIRED)
