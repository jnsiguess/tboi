import sys
import os
from PIL import Image

inputdir = sys.path[0] + "\\input\\"

def isDrawnPixel(img, x, y):
    if x >= img.width or y >= img.height:
        return
    r, g, b, a = img.getpixel((x,y))
    return a == 255 and ((r == 0 and g == 0 and b == 0) or (r == 54 and g == 47 and b == 45))

def maybePutAntiAlias(img, x, y):
    if x >= img.width or y >= img.height:
        return
    r, g, b, a = img.getpixel((x,y))
    if a != 0:
        return
    
    up = isDrawnPixel(img, x, y-1)
    down = isDrawnPixel(img, x, y+1)
    left = isDrawnPixel(img, x-1, y)
    right = isDrawnPixel(img, x+1, y)

    if (up and left) or (up and right) or (down and left) or (down and right):
        img.putpixel( (x,y), (153, 134, 117, 255) )

def doAliasing(img):
    for x in range(img.width):
        for y in range(img.height):
            if isDrawnPixel(img, x, y):
                maybePutAntiAlias(img, x+1, y)
                maybePutAntiAlias(img, x-1, y)
                maybePutAntiAlias(img, x, y+1)
                maybePutAntiAlias(img, x, y-1)

for filename in os.listdir(inputdir):
    if filename.endswith(".png"):
        img = Image.open(os.path.join(inputdir, filename))
        doAliasing(img)
        img.save(filename)
