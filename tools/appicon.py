# -*- coding: utf-8 -*-
"""The launcher icon, in the two shapes Android wants.

Android asks for the same picture twice and treats the two very differently:

    ic_launcher.png       the plain icon, shown as it is on older launchers
    ic_launcher_back.png  the background of the ADAPTIVE icon, which the
                          launcher crops to whatever shape it likes - a circle,
                          a squircle, a rounded square

Only the middle 72 of the adaptive icon's 108 units survive that crop, so a
picture drawn edge to edge loses its corners. The set already here solves it the
way it should be solved: the picture is put in the middle at 67 per cent, and
the space around it is filled with the picture MIRRORED. The mask then eats a
reflection instead of the drawing, and the icon reads whole on every launcher.
Measured off the existing files rather than assumed - the picture matches the
centre of the background at 67%, which is 72/108 to the pixel.

The foreground layer stays empty on purpose (see ic_launcher.xml): a picture
split between the two layers would be pulled apart by the launcher's parallax.

    py -3 tools/appicon.py [SOURCE]
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'res')
SOURCE = os.path.join(ROOT, 'IsaacIconFinal.png')
KEEP_IN = os.path.join(ROOT, 'menu-labels', 'icon-before')

# density: the plain icon's side, the adaptive background's side
SIZES = {'mdpi': (48, 108), 'hdpi': (72, 162), 'xhdpi': (96, 216),
         'xxhdpi': (144, 324), 'xxxhdpi': (192, 432)}

SAFE = 0.67                 # how much of the adaptive background the picture takes


def mirrored(pic, side):
    """The picture in the middle of a square of `side`, its own reflection around.

    Built from a three by three block - the picture with its flipped copies on
    every side - and cropped to the middle. That way the padding continues the
    drawing instead of cutting it off with a hard edge.
    """
    inner = max(1, round(side * SAFE))
    p = pic.resize((inner, inner), Image.LANCZOS)
    flip_x = p.transpose(Image.FLIP_LEFT_RIGHT)
    flip_y = p.transpose(Image.FLIP_TOP_BOTTOM)
    flip_xy = flip_x.transpose(Image.FLIP_TOP_BOTTOM)
    big = Image.new('RGBA', (inner * 3, inner * 3))
    for col, x in enumerate((0, inner, inner * 2)):
        for row, y in enumerate((0, inner, inner * 2)):
            tile = p if (col == 1 and row == 1) else (
                flip_xy if (col != 1 and row != 1) else (flip_x if col != 1 else flip_y))
            big.paste(tile, (x, y))
    off = inner + (inner - side) // 2
    return big.crop((off, off, off + side, off + side))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SOURCE
    pic = Image.open(src).convert('RGBA')
    print('source %s, %dx%d' % (os.path.basename(src), pic.size[0], pic.size[1]))
    if pic.size[0] != pic.size[1]:
        print('   NOT square - it will be squeezed, which is probably not wanted')
    for density, (plain, adaptive) in sorted(SIZES.items()):
        folder = os.path.join(RES, 'mipmap-' + density)
        if not os.path.isdir(folder):
            print('   %-8s no such folder - skipped' % density)
            continue
        for name, im in (('ic_launcher.png', pic.resize((plain, plain), Image.LANCZOS)),
                         ('ic_launcher_back.png', mirrored(pic, adaptive))):
            path = os.path.join(folder, name)
            # The kept copy goes OUTSIDE res/: gradle's resource merger walks
            # that tree and refuses any name that is not .xml or .png, so a
            # backup left beside the file fails the build outright.
            keep = os.path.join(KEEP_IN, '%s-%s' % (density, name))
            if os.path.exists(path) and not os.path.exists(keep):
                os.makedirs(KEEP_IN, exist_ok=True)
                with open(path, 'rb') as a, open(keep, 'wb') as b:
                    b.write(a.read())
            im.save(path)
        print('   %-8s plain %dx%d, adaptive %dx%d' % (density, plain, plain, adaptive, adaptive))


if __name__ == '__main__':
    main()
