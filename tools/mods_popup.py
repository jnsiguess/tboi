# -*- coding: utf-8 -*-
"""THE CONFLICT POPUP'S PICTURE (the owner, 05.10.2026: variant A of
other stuff/mods-conflict-mockup - the game's own big prompt, three buttons).

The game's prompt for the Rebirth save (gfx/ui/main menu/prompt_rebirthdata.png)
carries a blank paper of its own - the frame its ShowBlank animation ends on,
(0, 208) 400 x 192 - and the pointer beside YES and NO, (480, 0) 32 x 32. Those
two, and the shade every popup of the game lays over the screen (genericpopup's
Shade: its black swatch at 200 of 255), go into the port's own sheet, so no mod
that redraws the game's prompt can take them away: the popup is the port's
text on the game's paper. Taken from the game's own files (work/app), the
English set's - the blank paper has no words.

    py -3 tools/mods_popup.py      # -> port/app/src/main/assets/port_mods_popup.png, .anm2

Every animation lists every layer (a layer an animation leaves out is one the
engine may look for by number), the unused ones invisible; one frame each,
its Delay the animation's length.
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(ROOT, 'work', 'app', 'repentance-resources', 'data', 'gfx', 'ui')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')

PAPER = (0, 208, 400, 192)          # prompt_rebirthdata.png, ShowBlank's last frame
POINTER = (480, 0, 32, 32)          # its Cursor layer
SHADE = (0, 16, 10, 10)             # genericpopup.png, the Shade layer
SHEET = 'port_mods_popup.png'
# (layer name, where it lies in our sheet, pivot, alpha)
LAYERS = [('Shade', (432, 0, 10, 10), (0, 0), 200), ('Paper', (0, 0, 400, 192), (200, 96), 255),
          ('Pointer', (400, 0, 32, 32), (16, 16), 255)]


def frame(crop, pivot, alpha, visible=True):
    x, y, w, h = crop
    return ('<Frame XPosition="0" YPosition="0" XPivot="%d" YPivot="%d" XCrop="%d" YCrop="%d" Width="%d" Height="%d" '
            'XScale="100" YScale="100" Delay="1" Visible="%s" RedTint="255" GreenTint="255" BlueTint="255" '
            'AlphaTint="%d" RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0" Interpolated="false"/>'
            % (pivot[0], pivot[1], x, y, w, h, 'true' if visible else 'false', alpha))


def main():
    prompt = Image.open(os.path.join(UI, 'main menu', 'prompt_rebirthdata.png')).convert('RGBA')
    generic = Image.open(os.path.join(UI, 'genericpopup.png')).convert('RGBA')
    sheet = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
    x, y, w, h = PAPER
    paper = prompt.crop((x, y, x + w, y + h))
    assert paper.getbbox(), 'the blank paper is not where the anm2 says'
    sheet.paste(paper, (0, 0))
    x, y, w, h = POINTER
    pointer = prompt.crop((x, y, x + w, y + h))
    assert pointer.getbbox(), 'no pointer'
    sheet.paste(pointer, (400, 0))
    x, y, w, h = SHADE
    shade = generic.crop((x, y, x + w, y + h))
    assert all(p[:3] == (0, 0, 0) and p[3] == 255 for p in shade.getdata()), 'the shade swatch is not black'
    sheet.paste(shade, (432, 0))
    sheet.save(os.path.join(ASSETS, SHEET))

    layers = ''.join('<Layer Name="%s" Id="%d" SpritesheetId="0"/>' % (n, i) for i, (n, _, _, _) in enumerate(LAYERS))
    anims = ''
    for k, (name, _, _, _) in enumerate(LAYERS):
        tracks = ''
        for i, (_, crop, pivot, alpha) in enumerate(LAYERS):
            tracks += '<LayerAnimation LayerId="%d" Visible="true">%s</LayerAnimation>' % (
                i, frame(crop, pivot, alpha, visible=(i == k)))
        anims += ('<Animation Name="%s" FrameNum="1" Loop="true"><RootAnimation>'
                  '<Frame XPosition="0" YPosition="0" XScale="100" YScale="100" Delay="1" Visible="true" RedTint="255" '
                  'GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0" GreenOffset="0" BlueOffset="0" '
                  'Rotation="0" Interpolated="false"/></RootAnimation><LayerAnimations>%s</LayerAnimations>'
                  '<NullAnimations/><Triggers/></Animation>' % (name, tracks))
    anm2 = ('<AnimatedActor><Info CreatedBy="tools/mods_popup.py" CreatedOn="05.10.2026" Version="1" Fps="30"/>'
            '<Content><Spritesheets><Spritesheet Path="%s" Id="0"/></Spritesheets><Layers>%s</Layers><Nulls/><Events/>'
            '</Content><Animations DefaultAnimation="Paper">%s</Animations></AnimatedActor>\n' % (SHEET, layers, anims))
    open(os.path.join(ASSETS, 'port_mods_popup.anm2'), 'w', encoding='utf-8', newline='\n').write(anm2)
    print('port_mods_popup.png, port_mods_popup.anm2')


if __name__ == '__main__':
    main()
