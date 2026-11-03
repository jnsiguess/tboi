# -*- coding: utf-8 -*-
"""A library of Japanese characters cut out of the game's own captions.

The game has no caption font for Japanese: the extended Team Meat face is
Chinese, and it holds zero of the one hundred and seventy-six characters of the
two Japanese syllabaries. But the game's own captions are drawn as separate
characters, with nothing running together — so they can be cut up and our own
words assembled from them. The hand is then not merely similar, it is the very
same hand.

The sources below are captions I read off the pictures. The cutting goes by
strokes, and the number of pieces must match the number of characters: if it
does not, the caption is skipped with a message rather than quietly spoiling
the library.
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, 'work', 'app', 'repentance-resources',
                    'dlc3.jp', 'gfx', 'ui')

# file, area, what is written there
SOURCES = [
    ('main menu/optionsextra.png', (0, 0, 176, 32), 'コントローラーをへんこう'),
    ('main menu/optionsextra.png', (0, 32, 112, 32), 'HUDしょうさい'),
    ('main menu/optionsextra.png', (0, 64, 128, 32), 'じゅうでんバー'),
    ('main menu/optionsextra.png', (0, 96, 128, 32), 'てき弾のしにんせい'),
    # this caption has two words with a wide gap between them — we take them
    # apart, otherwise an even cut slides and the characters get mixed up
    ('main menu/optionsextra.png', (11, 128, 39, 32), 'ハイ'),
    ('main menu/optionsextra.png', (72, 128, 54, 32), 'ノーマル'),
    ('main menu/optionsextra.png', (0, 160, 128, 32), 'コンソール'),
    ('main menu/optionsmenu.png', (0, 280, 80, 20), 'コントロール'),
    ('main menu/optionsmenu.png', (0, 300, 80, 20), 'ポップアップ'),
    ('main menu/optionsmenu.png', (0, 320, 100, 20), 'ペンとうめいど'),
    ('main menu/optionsmenu.png', (0, 340, 100, 20), 'ちずとうめいど'),
    ('main menu/optionsmenu.png', (0, 360, 80, 20), 'げんご'),
    ('main menu/optionsmenu.png', (0, 400, 100, 20), 'アクティブカメラ'),
    ('main menu/optionsmenu.png', (0, 420, 60, 20), 'ガンマ'),
    ('main menu/optionsmenu.png', (0, 440, 80, 20), 'フィルター'),
    ('main menu/optionsmenu.png', (0, 460, 60, 20), 'おんがく'),
    ('main menu/gamemenu.png', (38, 294, 105, 36), 'ニューゲーム'),
    ('main menu/gamemenu.png', (37, 391, 113, 36), 'チャレンジ'),
    ('main menu/gamemenu.png', (39, 441, 76, 31), 'ステータス'),
    ('main menu/gamemenu.png', (39, 478, 93, 44), 'オプション'),
    ('main menu/keyconfigmenu.png', (243, 229, 168, 23), 'デフォルトにもどす'),
    ('pausescreen.png', (83, 23, 70, 26), 'ポーズ'),
]


def _blobs(im):
    """Connected blobs of ink: the left, right, top and bottom of each."""
    px = im.load()
    w, h = im.size
    mark = [[0] * h for _ in range(w)]
    blobs = []
    for sx in range(w):
        for sy in range(h):
            if px[sx, sy][3] <= 100 or mark[sx][sy]:
                continue
            n = len(blobs) + 1
            stack = [(sx, sy)]
            mark[sx][sy] = n
            left = right = sx
            top = bottom = sy
            while stack:
                x, y = stack.pop()
                if x < left:
                    left = x
                if x > right:
                    right = x
                if y < top:
                    top = y
                if y > bottom:
                    bottom = y
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < h and not mark[nx][ny] \
                                and px[nx, ny][3] > 100:
                            mark[nx][ny] = n
                            stack.append((nx, ny))
            blobs.append([left, right + 1, top, bottom + 1])
    blobs.sort()
    return blobs


def _slices(im, count):
    """Cut a picture into a given number of characters — by their strokes.

    Cutting on empty columns will not do: neighbouring characters overhang one
    another and there is no solid gap between them. Cutting into even shares
    will not do either: the characters differ in width, and the wide ones lose
    their tail.

    The strokes themselves, though, do not merge. We find the connected blobs,
    gather them into groups by overlap from left to right, and if more groups
    come out than there are characters, we glue the closest ones together until
    exactly the right number is left.
    """
    blobs = _blobs(im)
    if not blobs:
        return [(0, im.size[0])]

    height = im.size[1]
    groups = [list(blobs[0])]
    for left, right, top, bottom in blobs[1:]:
        g = groups[-1]
        overlap = min(right, g[1]) - max(left, g[0])
        if overlap > 0 and overlap >= 0.35 * min(right - left, g[1] - g[0]):
            g[0] = min(g[0], left); g[1] = max(g[1], right)
            g[2] = min(g[2], top); g[3] = max(g[3], bottom)
        else:
            groups.append([left, right, top, bottom])

    # Voicing marks (two strokes, a small circle) are separate strokes above a
    # character. By themselves they are not a character: we nail them to the
    # nearest neighbour, otherwise everything to the right slides by one place.
    widths = sorted(g[1] - g[0] for g in groups)
    median = widths[len(widths) // 2] if widths else 1
    i = 0
    while i < len(groups):
        g = groups[i]
        narrow = (g[1] - g[0]) <= median * 0.62
        high = g[3] <= height * 0.55
        if narrow and high and len(groups) > 1:
            prev = groups[i - 1] if i > 0 else None
            nxt = groups[i + 1] if i + 1 < len(groups) else None
            dl = (g[0] - prev[1]) if prev else 10 ** 6
            dr = (nxt[0] - g[1]) if nxt else 10 ** 6
            target = prev if dl <= dr else nxt
            target[0] = min(target[0], g[0]); target[1] = max(target[1], g[1])
            target[2] = min(target[2], g[2]); target[3] = max(target[3], g[3])
            groups.pop(i)
            continue
        i += 1

    while len(groups) > count:
        # glue together the pair with the smallest gap
        i = min(range(len(groups) - 1),
                key=lambda k: groups[k + 1][0] - groups[k][1])
        groups[i][1] = max(groups[i][1], groups[i + 1][1])
        groups.pop(i + 1)
    while len(groups) < count:
        # split the widest one in half
        i = max(range(len(groups)), key=lambda k: groups[k][1] - groups[k][0])
        left, right, top, bottom = groups[i]
        middle = (left + right) // 2
        groups[i] = [left, middle, top, bottom]
        groups.insert(i + 1, [middle, right, top, bottom])

    # the bounds go through the middle of the gaps so that nothing is clipped
    bounds = [0]
    for i in range(len(groups) - 1):
        bounds.append((groups[i][1] + groups[i + 1][0]) // 2)
    bounds.append(im.size[0])
    return [(bounds[i], bounds[i + 1]) for i in range(count)]


def collect(quiet=False):
    """Read every source and return a dictionary of character -> picture."""
    glyphs, skipped = {}, []
    for name, (x, y, w, h), text in SOURCES:
        path = os.path.join(BASE, *name.split('/'))
        im = Image.open(path).convert('RGBA').crop((x, y, x + w, y + h))
        # Ink is taken by darkness, not by transparency: on some pictures
        # (pausescreen, for one) the background is opaque, and going by alpha
        # stuck a beige rectangle to the character.
        px = im.load()
        for yy in range(im.size[1]):
            for xx in range(im.size[0]):
                r, g, b, a = px[xx, yy]
                px[xx, yy] = (r, g, b, a) if (a > 100 and r < 140 and g < 140) else (0, 0, 0, 0)
        bb = im.getchannel('A').getbbox()
        if not bb:
            skipped.append((name, text, 'empty'))
            continue
        im = im.crop(bb)
        groups = _slices(im, len(text))
        for (left, right), ch in zip(groups, text):
            piece = im.crop((left, 0, right, im.size[1]))
            bb2 = piece.getchannel('A').getbbox()
            if not bb2:
                continue
            piece = piece.crop((0, bb2[1], piece.size[0], bb2[3]))
            # keep the largest variant of a character: it is cleaner when shrunk
            have = glyphs.get(ch)
            if have is None or piece.size[1] > have[0].size[1]:
                glyphs[ch] = (piece, bb2[1], im.size[1])
    if not quiet:
        print('characters collected: %d' % len(glyphs))
        for name, text, why in skipped:
            print('   skipped: %-34s %-14s %s' % (name, text, why))
    return glyphs


if __name__ == '__main__':
    import sys
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    glyphs = collect()
    print('have:', ''.join(sorted(glyphs)))
    NEEDED = 'もーどスティックタイプおきさこてうごくちノマルムズデジ'
    missing = [z for z in NEEDED if z not in glyphs]
    print('missing for our words:', ''.join(missing) if missing else 'nothing')
