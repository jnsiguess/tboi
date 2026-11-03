# -*- coding: utf-8 -*-
"""The Tweaks screen in every language the game has, built with the tools that
already built the settings menu.

The owner's rule for this screen: whatever the settings menu and the credits
page already do, this does the same way. Every part below comes from an
earlier, approved implementation, and says which. All seven of the game's menu
sets are made at once, so nothing has to be redone per language later.

THE PAPER - the sheet's own page, rows 0..240, is ONE drawing; it is cut
nowhere but where a repeat cannot be seen. Longer is a band of its own rows
put in again, never a stretch: a band [a, b) goes in right after row b - 1
only where row b - 1 is the same row as a - 1, pixel for pixel, so what
follows the joint is exactly what followed before (bands_that_repeat finds
them). WIDER is the same done across, a band of the paper's own columns
repeated, cut where the torn top and bottom edges stand at the same height on
both sides of the joint.

The pieces the settings .anm2 stacks (Paper 0..128 at x 127, PaperMiddle4
133..153 at x 125, PaperBottom 96..240 at x 124) were used here until
14.10.2026: two of those joints are not the game's, and the owner saw both -
the head straight onto the foot (English, once RUMBLE left the page) and the
head onto PaperMiddle4 (every language): the torn sides stepped two texels.

THE TITLE - OPTIONS is lifted out of the Paper crop (the paper's own tone,
measured, put back; the star and the drips left). The new title is set a size
above the rows in the language's own face, and under it goes the game's own
strikeout stroke, cut from the middle, as on the credits page (rule_of).

THE WORDS - as the settings rows were made (lang.py, compose_like_native), with
each language's settings from lang.SETTINGS: the Team Meat cuts for the Latin
and Cyrillic sets, Mochiy Pop and Gamja Flower for Japanese and Korean, the
extended 16bold cut for Chinese - the faces the owner already chose there.
ON, OFF, the ticks and the cursor are the game's own pictures, named by the
game's own path, so each language set hands over its own.

Where the words are too long for the paper they are shortened in that
language's usual way, not squeezed (the owner, 16.09.2026).

Where everything stands is written to tweaks_layout.h, one entry per set; the
port picks the set by the game's language.

    py -3 tools/tweaks_page.py [--show]
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lang                    # noqa: E402  the settings rows' own typesetting
import credits_page            # noqa: E402  the credits headings and their stroke
import smoothing               # noqa: E402
import typeset                 # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# the menus as the port shows them (M8: the port's versions live in work/port-edit)
RES = os.path.join(ROOT, 'work', 'port-edit', 'repentance-resources')
MENU = os.path.join('gfx', 'ui', 'main menu')
ASSETS = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'assets')
HEADER = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'tweaks_layout.h')
SHOW = os.path.join(os.environ.get('TEMP', '.'), 'tweaks-page-preview.png')

# the pieces, as the settings .anm2 stacks them: crop rows, x on the screen
PAPER_X = 124                 # the leftmost piece; the page image starts here
PAPER_Y = 15                  # where the settings paper's top stands
TITLE_BOX = (58, 12, 164, 42)  # OPTIONS and its stroke, inside the Paper crop

TITLE_TOP = 15                # the top of the title, in paper rows (OPTIONS: 16)
RULE_GAP = 3
TITLE_ROWS_GAP = 10           # from the stroke down to the first row
BOTTOM_MARGIN = 20            # below the last row, about what the settings paper has
ROW_STEP = 20                 # the settings list's own row step (0x6e2b88)
GAP = 8
CURSOR_W = 22                 # the cursor's box and the space before a name
NATIVE_W, NATIVE_MARGIN = 231, 24    # the settings paper, and its margin each side
TICKS_W = 64
ONOFF_W = 40                  # the game's ON and OFF, turned on their side

# EID's language left Tweaks for EID's own settings page on MODS (the owner,
# 23.09.2026: every mod on MODS, with its settings)
# SPAWNER TROPHIES is the second row (the owner, 28.09.2026): spawnerlock of
# port-touch.ini, the other way round - ON lets a run with spawned items unlock
# RUMBLE left the page (hotfix 1.75, the owner): it is a slider of the game's
# own Options now, in the row that was FILTER, lettered by the game itself
# (loader.c rumble_menu_item) - so the fourth row is RENDER SCALE again
# FPS COUNTER under RENDER SCALE (the owner, 21.10.2026): a switch, tweaks.c R_FPS
# AIM LOCK below RENDER SCALE and FPS COUNTER, over DEAD ZONE (the owner, 22.10.2026)
KINDS = ['switch', 'switch', 'scale', 'switch', 'switch', 'ticks', 'ticks', 'ticks',
         'action', 'action', 'action']

# THE ACTIONS (the owner's items 9 and 14, 23.09.2026): the config file opened in
# the system's own file manager, and the saves imported and exported. A row of
# them shows one word, OPEN, and acts on a confirm alone.
ACTION_WORD = {'data': 'OPEN', 'dlc3.ru': 'ОТКРЫТЬ', 'dlc3.de': 'ÖFFNEN', 'dlc3.es': 'ABRIR',
               'dlc3.jp': 'ひらく', 'dlc3.kr': '열기', 'dlc3.zh': '打开'}

# THE MODS PAGE (the owner, 23.09.2026): the ported mods leave Tweaks for a
# page of their own behind the MODS item of the main menu. Their names are the
# last six of each set's list below; the page itself is the PC's own mods page,
# ModsMenu.png of that language (work/pc-resources, the game's ResourceExtractor):
# its head with the MODS title, a 32-unit piece per mod, its bottom.
MOD_ROWS = 8                  # the seven ported mods and EID, last (Range Fix, 04.10.2026)
PC = os.path.join(ROOT, 'work', 'pc-resources')
PC_SET = {'data': 'resources', 'dlc3.ru': 'resources-dlc3.ru', 'dlc3.de': 'resources-dlc3.de',
          'dlc3.es': 'resources-dlc3.es', 'dlc3.jp': 'resources-dlc3.jp',
          'dlc3.kr': 'resources-dlc3.kr', 'dlc3.zh': 'resources-dlc3.zh'}
MODS_HEAD = (0, 0, 256, 48)
MODS_PIECES = [(0, 48 + 32 * i, 256, 32) for i in range(5)]     # Piece1..Piece5
MODS_FOOT = (0, 208, 256, 48)
MODS_STEP = 32
TITLE_FROM = 10               # the head's rows the title is looked for in: below the torn edge
TITLE_DROP = 4                # (kept for the record: the title now has a fixed top, TITLE_Y)
# OUR TITLES ON THESE PAGES stand at one height, well clear of the torn top edge
# (the owner, 24.09.2026: "lower from the edge, everywhere"); the rows go down
# with them so the stroke keeps its air
TITLE_Y = 14                  # the title's top, from the top of the page picture
MODS_ROW0 = MODS_HEAD[3] - 4 + 6   # where the first row starts
TITLE_GAP_MODS = 6            # and this far between the word and its stroke
# the word the game's own main menu writes for the item, set by set (gamemenu.png)
MODS_WORD = {'data': 'MODS', 'dlc3.ru': 'МОДЫ', 'dlc3.de': 'MODS', 'dlc3.es': 'MODS',
             'dlc3.jp': 'MOD', 'dlc3.kr': '모드', 'dlc3.zh': '模组'}
ARROW_W = 16                  # the PC page's arrow, beside a mod that has settings
MODS_MARGIN = 20              # the torn edge is 12..14 wide; the words keep clear of it
MODS_PAPER_Y = 15             # where the page's top stands, as the Tweaks paper
# the PC page's own sketches: (crop x, y, w, h) and where ModsMenu.anm2 puts them
# about the head's bottom middle (pivot 16,16)
MODS_SKETCHES = [((126, 408, 127, 104), (-235, 82)), ((80, 256, 48, 80), (-142, -51)),
                 ((0, 256, 80, 96), (136, 199)), ((129, 257, 78, 79), (-187, 356))]

# THE PLAYER'S OWN MODS (the owner, 25.09.2026, the mock-up approved from
# tools/usermods_preview.py). The list grows at runtime, so the MODS page is not
# one picture any more: the game lays the paper itself from the pieces of
# port_mods_paper_<suffix>.png - the head, the five pieces and the foot, each as
# its left part, a band, and its right part; the band is repeated for width.
# The band is chosen once per sheet: its two ends meet with the torn edges at
# one height, and nothing drawn on the paper lies inside it (the first mock-up
# repeated the foot's scratch five times).
MODS_BAND_W = 24
SECTION_GAP = 14              # air above YOUR MODS: the port's own and the player's apart
# the words: the heading (with the stroke), ADD MOD, the game's files out (the
# owner, 25.09.2026: beside ADD MOD, not on Tweaks), then a mod's page - its
# rows, the second press of DELETE, the note that a change needs a restart -
# and last the heading of a mod's own settings (plan 4.10, part 2; the owner's
# order of 02.10.2026), made as YOUR MODS is, with its stroke. Chinese says
# 选项: in the 16bold cut the 置 of 设置 is solid ink (see the title below).
USER_WORDS = {
    'data': ['YOUR MODS', '+ ADD MOD', 'EXPORT GAME FILES', 'ENABLED', 'MOVE UP', 'MOVE DOWN', 'EXPORT', 'DELETE',
             'DELETE FOR SURE?', 'RESTART TO APPLY', 'SETTINGS'],
    'dlc3.ru': ['СВОИ МОДЫ', '+ ДОБАВИТЬ МОД', 'ВЫГРУЗИТЬ ФАЙЛЫ ИГРЫ', 'ВКЛЮЧЁН', 'ВЫШЕ', 'НИЖЕ', 'ВЫГРУЗИТЬ', 'УДАЛИТЬ',
                'ТОЧНО УДАЛИТЬ?', 'ПЕРЕЗАПУСТИТЬ ДЛЯ ПРИМЕНЕНИЯ', 'НАСТРОЙКИ'],
    'dlc3.de': ['EIGENE MODS', '+ MOD HINZUFÜGEN', 'SPIELDATEIEN SICHERN', 'AKTIV', 'NACH OBEN', 'NACH UNTEN', 'EXPORTIEREN',
                'LÖSCHEN', 'WIRKLICH LÖSCHEN?', 'NEU STARTEN ZUM ANWENDEN', 'EINSTELLUNGEN'],
    'dlc3.es': ['TUS MODS', '+ AÑADIR MOD', 'EXPORTAR ARCHIVOS', 'ACTIVO', 'SUBIR', 'BAJAR', 'EXPORTAR', 'BORRAR',
                'BORRAR DE VERDAD?', 'REINICIAR PARA APLICAR', 'OPCIONES'],
    # kana, as every Japanese word of ours (SETS below)
    'dlc3.jp': ['じぶんのMOD', '+ MODをついか', 'ゲームファイルのかきだし', 'ゆうこう', 'うえへ', 'したへ', 'かきだし', 'さくじょ',
                'ほんとうにさくじょ?', 'さいきどうしててきよう', 'せってい'],
    'dlc3.kr': ['내 모드', '+ 모드 추가', '게임 파일 내보내기', '사용', '위로', '아래로', '내보내기', '삭제',
                '정말 삭제?', '재시작해서 적용', '설정'],
    'dlc3.zh': ['我的模组', '+ 添加模组', '导出游戏文件', '启用', '上移', '下移', '导出', '删除',
                '确定删除?', '重启以生效', '选项'],
}
# the Extra frames: the MODS title, then the words above in their order
MX_NAMES = ['TITLE', 'HEADING', 'ADD', 'DUMP', 'ENABLED', 'UP', 'DOWN', 'EXPORT', 'DELETE', 'SURE', 'RESTART',
            'SETTINGS']

# the languages the description database can carry, as the EID row shows them
LANG_CODES = ['en_us', 'ru', 'de', 'fr', 'spa', 'it', 'pl', 'pt_br', 'pt', 'cs_cz', 'ro_ro',
              'tr_tr', 'uk_ua', 'bul', 'el_gr', 'nl_nl', 'vi', 'ja_jp', 'ko_kr', 'zh_cn',
              'ar']                   # ours, from the "Arabic EID" mod (tools/eid_arabic.py)
LANG_SHOWN = ['EN', 'RU', 'DE', 'FR', 'ES', 'IT', 'PL', 'BR', 'PT', 'CS', 'RO',
              'TR', 'UK', 'BG', 'EL', 'NL', 'VI', 'JA', 'KO', 'ZH', 'AR']

# The rule for words the game has (the owner, 23.09.2026): the game's own word
# where it translated it, our translation where it left English - as AIM LOCK
# is ZIELSPERRE in German. (RUMBLE followed it while it was a row here; since
# hotfix 1.75 the Options row shows the game's own lettering instead.)
# set, file suffix, title, row names (in KINDS order), Render Scale words
# (AUTO first; the AUTO of the language row is the same word; then POTATO -
# half the canvas, the owner's step for a weak phone, 25.09.2026: the gamers'
# own word where there is one - Kartoffel-PC, PC patata, 감자컴, 土豆 - and in
# Russian the adjective, as the other steps are; Japanese has no such slang and
# says low spec - then ORIGINAL, the canvas as iOS draws it, and the rest from
# LOW up. ORIGINAL went on 14.10.2026 and came back on 17.10.2026 (the owner:
# both one-canvas modes as iOS draws them, POTATO with its effects off). Since
# 25.10.2026 there are three steps (the owner): POTATO, ORIGINAL and ENHANCED,
# which is the old MEDIUM - "the most balanced, above it nothing much shows";
# LOW, HIGH, ULTRA and EXTREME are gone)
SETS = [
    ('data', 'en', 'TWEAKS',
     ['CONSOLE', 'CONSOLE TROPHIES', 'RENDER SCALE', 'FPS COUNTER', 'AIM LOCK', 'DEAD ZONE', 'MOVE SENSITIVITY',
      'AIM SENSITIVITY', 'CONFIG FILE', 'IMPORT SAVES', 'EXPORT SAVES',
      'SPECIALIST DANCE', 'PLANETARIUM CHANCE',
      'BOSS RUSH WAVES', 'BOSS BARS', 'COMPLETION MARKS', 'STATS+', 'RANGE FIX', 'ITEM DESCRIPTIONS'],
     ['AUTO', 'POTATO', 'ORIGINAL', 'ENHANCED']),
    ('dlc3.ru', 'ru', 'ТВИКИ',
     # AIM LOCK in the game's own Russian words (lang.NATIVE_TRANSLATIONS)
     ['КОНСОЛЬ', 'ТРОФЕИ КОНСОЛИ', 'МАСШТАБ РЕНДЕРА', 'СЧЁТЧИК КАДРОВ', 'БЛОК СТРЕЛЬБЫ', 'МЁРТВАЯ ЗОНА',
      'ЧУВСТ. ДВИЖЕНИЯ', 'ЧУВСТ. СТРЕЛЬБЫ', 'ФАЙЛ НАСТРОЕК',
      'ИМПОРТ СОХРАНЕНИЙ', 'ЭКСПОРТ СОХРАНЕНИЙ', 'ТАНЕЦ СПЕЦИАЛИСТА',
      'ШАНС ПЛАНЕТАРИЯ', 'ВОЛНЫ БОСС-РАША', 'ПОЛОСЫ БОССОВ',
      'ПОДСКАЗКА ОТМЕТОК', 'СТАТЫ+', 'ФИКС ДАЛЬНОСТИ', 'ОПИСАНИЯ ПРЕДМЕТОВ'],
     ['АВТО', 'КАРТОФЕЛЬНЫЙ', 'ОРИГИНАЛ', 'УЛУЧШЕННЫЙ']),
    ('dlc3.de', 'de', 'TWEAKS',
     ['KONSOLE', 'KONSOLE-TROPHÄEN', 'RENDER-SKALIERUNG', 'FPS-ANZEIGE', 'ZIELSPERRE', 'TOTZONE', 'EMPF. BEWEGUNG',
      'EMPF. ZIELEN', 'KONFIG-DATEI', 'SPIELSTÄNDE LADEN',
      'SPIELSTÄNDE SICHERN', 'SPEZIALISTEN-TANZ', 'PLANETARIUM-CHANCE',
      'BOSS-RUSH-WELLEN', 'BOSS-LEISTEN', 'ABSCHLUSSMARKEN', 'STATS+',
      'REICHWEITEN-FIX', 'ITEM-BESCHREIBUNGEN'],
     ['AUTO', 'KARTOFFEL', 'ORIGINAL', 'VERBESSERT']),
    ('dlc3.es', 'es', 'AJUSTES',
     ['CONSOLA', 'TROFEOS DE CONSOLA', 'ESCALA DE RENDER', 'CONTADOR DE FPS', 'BLOQUEO DE MIRA', 'ZONA MUERTA',
      'SENS. MOVIMIENTO', 'SENS. PUNTERÍA', 'ARCHIVO DE CONFIG',
      'IMPORTAR PARTIDAS', 'EXPORTAR PARTIDAS', 'BAILE ESPECIALISTA',
      'PROB. PLANETARIO', 'OLEADAS BOSS RUSH', 'BARRAS DE JEFES',
      'MARCAS PENDIENTES', 'ESTADÍSTICAS+', 'ARREGLO DE RANGO', 'DESCRIPCIONES'],
     ['AUTO', 'PATATA', 'ORIGINAL', 'MEJORADA']),
    # Japanese is written in KANA throughout, as the game's menus are and as our
    # own settings rows are (lang.WORDS: タイプ, こてい, ちいさい...). Kanji in this
    # face look like another hand beside the kana (the owner, 16.09.2026).
    ('dlc3.jp', 'jp', 'こまかいせってい',
     ['コンソール', 'コンソールのトロフィー', 'がしつ', 'FPSひょうじ', 'エイムロック', 'デッドゾーン', 'いどうのかんど',
      'ねらいのかんど', 'せっていファイル', 'セーブのよみこみ',
      'セーブのかきだし', 'スペシャリストダンス', 'プラネタリウムのかくりつ',
      'ボスラッシュウェーブ', 'ボスバー', 'クリアマークのヒント', 'ステータス+',
      'しゃていのしゅうせい', 'アイテムのせつめい'],
     ['じどう', 'ていスペック', 'オリジナル', 'こうがしつ']),
    ('dlc3.kr', 'kr', '세부 설정',
     ['콘솔', '콘솔 트로피', '렌더 배율', '프레임 표시', '조준 고정', '데드존', '이동 감도', '조준 감도',
      '설정 파일', '세이브 가져오기', '세이브 내보내기', '스페셜리스트 댄스', '천체관 확률', '보스 러시 웨이브', '보스 체력바',
      '완료 마크 표시', '스탯+', '사거리 수정', '아이템 설명'],
     ['자동', '감자', '원본', '향상됨']),
    # the title is 更多选项: in the 16bold cut the 置 of 设置 is solid ink before
    # any smoothing, the cut itself fills its middle in
    ('dlc3.zh', 'zh', '更多选项',
     ['控制台', '控制台奖杯', '渲染缩放', '帧率显示', '瞄准锁定', '摇杆死区', '移动灵敏度', '瞄准灵敏度',
      '配置文件', '导入存档', '导出存档', '专家之舞', '星象房概率', '头目车轮战波次', '头目血条',
      '通关标记提示', '属性+', '射程修复', '道具说明'],
     ['自动', '土豆', '原始', '增强']),
]

# the game's language number (Manager+0x564f8) -> which set shows the page;
# French has no menu set of its own and reads the English one, as the game does
GAME_LANGUAGE = {0: 'en', 2: 'jp', 3: 'en', 4: 'es', 5: 'de', 10: 'ru', 11: 'kr', 13: 'zh'}


# ------------------------------------------------------------------ paper
def sheet_of(set_name):
    return Image.open(os.path.join(RES, set_name, MENU, 'optionsmenu.png')).convert('RGBA')


SHEET_PAPER = (0, 0, 230, 240)          # the page on the sheet: columns 230 on are other pictures
REPEAT_ROWS = (90, 160)                 # where a band may lie: below the scratches, above the doodles


def row_differs(a, b):
    """0 when two rows of the sheet are the same picture, pixel for pixel."""
    ma, mb = a[:, 3] > 0, b[:, 3] > 0
    both = ma & mb
    return int((ma != mb).sum()) * 100 + int(np.abs(a[both, :3] - b[both, :3]).sum())


def bands_that_repeat(sheet):
    """Every band [a, b) whose last row is its first row's predecessor again."""
    a = np.asarray(sheet).astype(int)
    lo, hi = REPEAT_ROWS
    return [(r0, r1) for r0 in range(lo, hi) for r1 in range(r0 + 8, hi)
            if row_differs(a[r1 - 1], a[r0 - 1]) == 0]


def paper(need_h):
    """The page at least need_h tall: the sheet's paper, and one band of it repeated."""
    sheet = without_title(sheet_of('data').crop(SHEET_PAPER))   # the same paper in every set
    base_h = sheet.height
    extra = need_h - base_h
    if extra <= 0:
        return sheet
    bands = bands_that_repeat(sheet)
    longest = max(r1 - r0 for r0, r1 in bands)
    times = -(-extra // longest)
    want = -(-extra // times)
    fits = [b for b in bands if b[1] - b[0] >= want]
    r0, r1 = min(fits, key=lambda b: (b[1] - b[0], b[0]))
    a = np.asarray(sheet)
    rows = [a[:r1]] + [a[r0:r1]] * times + [a[r1:]]
    return Image.fromarray(np.concatenate(rows, axis=0))


def without_title(piece):
    """OPTIONS and its stroke lifted out; the star and the drips left alone.

    The paper's tone is MEASURED, the middle value of the box itself. A tone
    written down by eye came out a shade off and left pale patches either side
    of the new title. Anything that is not that tone becomes it.
    """
    a = np.asarray(piece).astype(int)
    x0, y0, x1, y1 = TITLE_BOX
    box = a[y0:y1, x0:x1]
    opaque = box[box[..., 3] == 255][:, :3]
    tone = tuple(int(v) for v in np.median(opaque, axis=0))
    off = np.abs(box[..., :3] - np.array(tone)).sum(axis=2) > 6
    box[off & (box[..., 3] > 0)] = tone + (255,)
    a[y0:y1, x0:x1] = box
    return Image.fromarray(a.astype(np.uint8))


def widened(page, extra):
    """More of the paper's own width, put in the way the height was.

    A band of columns is repeated, carrying a stretch of the torn top and bottom
    edges with it. Where it is cut is chosen so the joint cannot be seen: the
    column the band starts on and the column after it must have both edges at
    the same height and the paper between as near the same as can be found -
    the credits stroke is cut the same way. A wide addition goes in as several
    bands of at most MAX_BAND, so there is always a good joint to be found, and
    the doodles at either side lie outside the range searched.
    """
    MAX_BAND = 40
    added = 0
    while added < extra:
        band = min(MAX_BAND, extra - added)
        a = np.asarray(page).astype(int)
        alpha = a[..., 3] > 0
        h, w = alpha.shape
        tops = [int(np.argmax(alpha[:, x])) if alpha[:, x].any() else h for x in range(w)]
        bots = [h - 1 - int(np.argmax(alpha[::-1, x])) if alpha[:, x].any() else 0
                for x in range(w)]
        best = None
        for x in range(66, 160 + added - band):
            y = x + band
            cost = (abs(tops[x] - tops[y]) + abs(bots[x] - bots[y])) * 40
            cost += int(np.abs(a[:, x, :3] - a[:, y, :3]).sum() // h)
            if best is None or cost < best[0]:
                best = (cost, x)
        x = best[1]
        a = np.concatenate([a[:, :x + band], a[:, x:x + band], a[:, x + band:]], axis=1)
        page = Image.fromarray(a.astype(np.uint8))
        added += band
    return page


def onoff_of(set_name):
    """ON and OFF as THIS set's own settings rows cut them.

    The words are drawn sideways in the column at the sheet's right edge, and
    their size is the language's: the Russian OFF is 54 long where the English
    OFF is 39. The first page cut every language at the English rectangles and
    the Russian word came out clipped at both ends - the same fault the settings
    menu had and was cured of by pointing its own markup at the Russian word
    (tools/aimlock_ru.py). So the rectangles are read from the set's own
    optionsmenu.anm2, the Fullscreen row's value layer: frame 0 is ON, frame 1
    OFF. Answers two (x, y, w, h).
    """
    import xml.etree.ElementTree as ET
    root = ET.parse(os.path.join(RES, set_name, MENU, 'optionsmenu.anm2')).getroot()
    for anim in root.iter('Animation'):
        if anim.get('Name') != 'Fullscreen':
            continue
        for la in anim.iter('LayerAnimation'):
            if la.get('LayerId') != '4':
                continue
            frames = [tuple(int(f.get(k)) for k in ('XCrop', 'YCrop', 'Width', 'Height'))
                      for f in la.iter('Frame')]
            return frames[0], frames[1]
    raise SystemExit('%s has no Fullscreen value layer' % set_name)


# ------------------------------------------------------------------ words
def word(set_name, text, target, bold):
    s = dict(lang.SETTINGS[set_name])
    im, _ = lang.compose_like_native(text, s['height'], s, target, bold=bold)
    return im.crop(im.getchannel('A').getbbox())


def title_art(set_name, text, gap=None):
    """The title, a size above the rows, with the game's stroke under it.

    The letters and the stroke are finished SEPARATELY, each by the path already
    approved for it, and only then put one above the other. The first page ran
    the whole title through the credits page's threshold after the Chinese had
    already been enlarged and grown, and the dense ideographs closed up - the
    owner could not read the title (16.09.2026).
    """
    s = lang.SETTINGS[set_name]
    if set_name == 'dlc3.zh':
        # AT ITS OWN SIZE, and smoothed the one way this port smooths: the
        # glyph's hard silhouette, and the inner corners of its staircase filled
        # with the second tone (smoothing.py). The first page ENLARGED the cut
        # four to three first; the resampling blurred the strokes together and
        # the threshold then closed the dense ideographs up - the owner could not
        # read the title (16.09.2026). The 16bold cut is the largest Chinese one
        # the game has, so the title stands the size of the rows here and is
        # told apart by its stroke, as the credits headings are.
        im = typeset.compose(text, None, lang=set_name, jitter=lang.JITTER, font=s['font'],
                             folder=s['folder'], thicken=False)
        letters, _ = smoothing.smooth(im.getchannel('A'), tone=credits_page.LIGHT + (255,),
                                      ink=credits_page.INK, silhouette=credits_page.SILHOUETTE)
    else:
        if s.get('direct'):
            # Japanese and Korean: their face drawn straight, six points above the
            # row, and smoothed as their own rows and native menus are - two tones,
            # lang.two_tone - not with the credits page's threshold
            a = lang.drawn_straight(text, s['font'], s.get('folder'), s['direct'] + 6)
            if s.get('grow'):
                a = credits_page.grown(a)
            letters, _ = lang.two_tone(a, s['tone'])
        else:
            # Latin and Cyrillic: the 16 cut, grown, as the credits headings are
            line, _ = credits_page.set_line(text, 'teammeatfont16.fnt')
            a = credits_page.grown(line)
            letters, _ = smoothing.smooth(a, tone=credits_page.LIGHT + (255,),
                                          ink=credits_page.INK,
                                          silhouette=credits_page.SILHOUETTE)
    letters = letters.crop(letters.getchannel('A').getbbox())

    credits_page.STROKE_THIN = 0                       # a title, not a credits heading
    rule_map = credits_page.rule_of(letters.width + 10, 1)
    rule, _ = smoothing.smooth(rule_map, tone=credits_page.LIGHT + (255,), ink=credits_page.INK,
                               silhouette=credits_page.SILHOUETTE)
    rule = rule.crop(rule.getchannel('A').getbbox())

    out = Image.new('RGBA', (max(letters.width, rule.width),
                             letters.height + (RULE_GAP if gap is None else gap) + rule.height), (0, 0, 0, 0))
    out.alpha_composite(letters, ((out.width - letters.width) // 2, 0))
    out.alpha_composite(rule, ((out.width - rule.width) // 2, letters.height + (RULE_GAP if gap is None else gap)))
    return out


# ------------------------------------------------------------------ one set
def build_set(set_name, suffix, title, names, scale_words, show):
    target = lang.native_share(Image.open(os.path.join(RES, set_name, MENU, 'optionsextra.png'))
                               .convert('RGBA'))
    scale = [word(set_name, w, target, False) for w in scale_words]
    langs = [scale[0]] + [word(set_name, w, target, False) for w in LANG_SHOWN]
    rows = [word(set_name, n, target, True) for n in names[:len(KINDS)]]
    action = word(set_name, ACTION_WORD[set_name], target, False)
    words = scale + langs + rows + [action]

    scale_w = max(im.width for im in scale)
    lang_w = max(im.width for im in langs)
    on, off = onoff_of(set_name)
    onoff_w = max(on[3], off[3])             # turned on their side: height is width
    value_ws = [{'switch': onoff_w, 'ticks': TICKS_W, 'scale': scale_w, 'lang': lang_w,
                 'action': action.width}[k] for k in KINDS]
    widest = max(CURSOR_W + n.width + GAP + v for n, v in zip(rows, value_ws))

    # a row as tall as its tallest word needs, and never less than the settings step
    tallest = max(im.height for im in words)
    step = max(ROW_STEP, tallest + 5)
    baseline = step - 3

    head = title_art(set_name, title)
    row0 = TITLE_TOP + head.height + TITLE_ROWS_GAP
    need_h = row0 + len(rows) * step + BOTTOM_MARGIN

    extra = max(0, widest + NATIVE_MARGIN * 2 - NATIVE_W)
    extra += extra % 2
    page = widened(paper(need_h), extra)
    page.alpha_composite(head, ((page.width - head.width) // 2, TITLE_TOP))
    page.save(os.path.join(ASSETS, 'port_tweaks_page_%s.png' % suffix))

    sheet = Image.new('RGBA', (max(im.width for im in words), sum(im.height for im in words)),
                      (0, 0, 0, 0))
    frames, y = [], 0
    for im in words:
        sheet.alpha_composite(im, (0, y))
        frames.append((0, y, im.width, im.height))
        y += im.height
    sheet.save(os.path.join(ASSETS, 'port_tweaks_words_%s.png' % suffix))
    write_anm2(suffix, page.size, frames, on, off)

    print('  %-8s paper %d x %d, rows %d apart from %d, widest line %d'
          % (set_name, page.width, page.height, step, row0, widest))
    if show:
        back = Image.new('RGBA', page.size, (170, 160, 158, 255))
        back.alpha_composite(page)
        yy = row0
        for n, v in zip(rows, value_ws):
            total = CURSOR_W + n.width + GAP + v
            x = page.width // 2 - total // 2 + CURSOR_W
            back.alpha_composite(n, (x, yy + baseline - n.height))
            yy += step
        back.save(SHOW.replace('.png', '-%s.png' % suffix))

    return dict(suffix=suffix, x=PAPER_X - extra // 2, w=page.width, h=page.height,
                step=step, baseline=baseline, row0=row0, frames=frames,
                on_w=on[3], off_w=off[3])


def mods_title(head):
    """The MODS title and its stroke, lifted off the PC page's head.

    Widening repeats a band of the paper's columns, and a band through the title
    repeats letters (the first build read McMODS and a Russian MODS with its last two letters twice). So the title comes
    off first - everything in the head's middle that is not the paper's own
    tone, the tone MEASURED as without_title measures it - the clean paper is
    widened, and the title goes back on in the middle, as the Tweaks title does.
    Answers the title piece, where it stood, and the head without it.
    """
    a = np.asarray(head).astype(int)
    # from row 10: above it is the torn top edge and its shading, which differ
    # from the paper's tone too - lifted with the title and put back shifted by
    # the widening, they stood as a second, broken edge (the owner, 24.09.2026)
    box = a[TITLE_FROM:44, 30:236]
    opaque = box[box[..., 3] == 255][:, :3]
    tone = np.median(opaque, axis=0)
    ink = (np.abs(box[..., :3] - tone).sum(axis=2) > 12) & (box[..., 3] > 0)
    ys, xs = np.nonzero(ink)
    x0, x1 = 30 + xs.min(), 30 + xs.max() + 1
    y0, y1 = TITLE_FROM + ys.min(), TITLE_FROM + ys.max() + 1
    title = head.crop((x0, y0, x1, y1))
    clean = a.copy()
    region = clean[y0:y1, x0:x1]
    off = (np.abs(region[..., :3] - tone).sum(axis=2) > 6) & (region[..., 3] > 0)
    region[off] = tuple(int(v) for v in tone) + (255,)
    clean[y0:y1, x0:x1] = region
    return title, (x0, y0), Image.fromarray(clean.astype(np.uint8))


def mods_paper(sheet, rows, extra, title=None):
    """The PC's mods page: its head, a piece per row (Piece1..Piece5 round), its foot,
    widened by `extra` with the title kept whole. `title`, when given, is a
    picture put where the PC's MODS stood - a mod's name on its settings page."""
    head = sheet.crop((MODS_HEAD[0], MODS_HEAD[1], MODS_HEAD[0] + MODS_HEAD[2],
                       MODS_HEAD[1] + MODS_HEAD[3]))
    native, (tx, ty), head = mods_title(head)
    parts = [MODS_PIECES[i % len(MODS_PIECES)] for i in range(rows)] + [MODS_FOOT]
    out = Image.new('RGBA', (256, MODS_HEAD[3] + sum(p[3] for p in parts)), (0, 0, 0, 0))
    out.alpha_composite(head, (0, 0))
    y = MODS_HEAD[3]
    for x0, y0, w, h in parts:
        out.alpha_composite(sheet.crop((x0, y0, x0 + w, y0 + h)), (0, y))
        y += h
    if extra:
        out = widened(out, extra)
    if title is None:
        # back in the middle by as much as the paper grew on its left
        out.alpha_composite(native, (tx + extra // 2, ty))
    else:
        # ours: in the middle, its stroke's foot where the PC's stroke stood -
        # and a little lower, with air between the word and the stroke (the
        # owner, 24.09.2026: the title and its stroke were stuck together)
        out.alpha_composite(title, ((out.width - title.width) // 2, TITLE_Y))
    return out


def mods_clean_strip(sheet):
    """the PC page's head (its title lifted off), five pieces and foot, one 256 x 256"""
    head = mods_title(sheet.crop(MODS_HEAD))[2]
    strip = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    strip.alpha_composite(head, (0, 0))
    strip.alpha_composite(sheet.crop((0, 48, 256, 256)), (0, 48))
    return strip


def mods_band(strip, width=MODS_BAND_W):
    """where the band starts: ends at one height, no drawing inside (see MODS_BAND_W)"""
    a = np.asarray(strip).astype(int)
    alpha = a[..., 3] > 0
    h, w = alpha.shape
    tops = [int(np.argmax(alpha[:, x])) for x in range(w)]
    bots = [h - 1 - int(np.argmax(alpha[::-1, x])) for x in range(w)]
    body = a[60:200, 60:200]
    tone = np.median(body[body[..., 3] == 255][:, :3], axis=0)
    marked = []
    for x in range(w):
        inner = a[tops[x] + 16:bots[x] - 16, x]
        marked.append(int(((np.abs(inner[:, :3] - tone).sum(axis=1) > 12) & (inner[:, 3] > 0)).sum()))
    best = None
    for x in range(40, 216 - width):
        y = x + width
        if any(marked[x:y + 1]):
            continue
        cost = (abs(tops[x] - tops[y]) + abs(bots[x] - bots[y])) * 40
        cost += int(np.abs(a[:, x, :3] - a[:, y, :3]).sum() // h)
        if best is None or cost < best[0]:
            best = (cost, x)
    return best[1]


def mods_stroke(width, which=1):
    """the game's stroke under a heading, as title_art finishes it"""
    credits_page.STROKE_THIN = 0
    rule, _ = smoothing.smooth(credits_page.rule_of(width, which), tone=credits_page.LIGHT + (255,),
                               ink=credits_page.INK, silhouette=credits_page.SILHOUETTE)
    return rule.crop(rule.getchannel('A').getbbox())


def section_heading(set_name, text, target):
    """a heading of the MODS pages: the word as the rows are lettered, the game's stroke under it"""
    head_word = word(set_name, text, target, True)
    rule = mods_stroke(head_word.width + 10)
    heading = Image.new('RGBA', (rule.width, head_word.height + 3 + rule.height), (0, 0, 0, 0))
    heading.alpha_composite(head_word, ((rule.width - head_word.width) // 2, 0))
    heading.alpha_composite(rule, (0, head_word.height + 3))
    return heading


def user_extras(set_name, target):
    """the Extra frames of one set, in MX_NAMES order"""
    texts = USER_WORDS[set_name]
    out = [title_art(set_name, MODS_WORD[set_name], gap=TITLE_GAP_MODS), section_heading(set_name, texts[0], target)]
    # the last is a button now, not a note (the owner, 25.09.2026: it looked like
    # one and did nothing): lettered as the rows are
    out += [word(set_name, t, target, True) for t in texts[1:10]]
    # a mod's SETTINGS heading, as YOUR MODS (plan 4.10, part 2)
    out.append(section_heading(set_name, texts[10], target))
    return out


def build_mods_paper(set_name, suffix, sheet, target):
    """port_mods_paper_<suffix>.png: the clean strip on top, the Extra words under it.
    Answers the band's start, the Pieces frames and the Extra frames."""
    strip = mods_clean_strip(sheet)
    band = mods_band(strip)
    extras = user_extras(set_name, target)
    im = Image.new('RGBA', (max([256] + [e.width for e in extras]),
                            256 + sum(e.height + 1 for e in extras)), (0, 0, 0, 0))
    im.alpha_composite(strip, (0, 0))
    extra_frames, y = [], 256
    for e in extras:
        im.alpha_composite(e, (0, y))
        extra_frames.append((0, y, e.width, e.height))
        y += e.height + 1
    im.save(os.path.join(ASSETS, 'port_mods_paper_%s.png' % suffix))
    parts = [(0, 48)] + [(48 + 32 * i, 32) for i in range(5)] + [(208, 48)]
    pieces = []
    for top, h in parts:
        pieces += [(0, top, band, h), (band, top, MODS_BAND_W, h), (band, top, 256 - band, h)]
    return band, pieces, extra_frames


def build_mods_set(set_name, suffix, names, show):
    """The MODS page of one set: the PC's page of that language, our rows on it."""
    sheet = Image.open(os.path.join(PC, PC_SET[set_name], MENU, 'modsmenu.png')).convert('RGBA')
    sheet.save(os.path.join(ASSETS, 'port_mods_sheet_%s.png' % suffix))
    target = lang.native_share(Image.open(os.path.join(RES, set_name, MENU, 'optionsextra.png'))
                               .convert('RGBA'))
    rows = [word(set_name, n, target, True) for n in names]
    on, off = onoff_of(set_name)
    onoff_w = max(on[3], off[3])
    widest = max(CURSOR_W + n.width + GAP + onoff_w + GAP + ARROW_W for n in rows)
    extra = max(0, widest + MODS_MARGIN * 2 - 256)
    extra += extra % 2
    # the title is ours, as on the settings pages: the PC's own MODS has a grey
    # stroke of its own, and every other page of ours has the ink's (24.09.2026)
    page = mods_paper(sheet, len(rows), extra,
                      title=title_art(set_name, MODS_WORD[set_name], gap=TITLE_GAP_MODS))
    page.save(os.path.join(ASSETS, 'port_mods_page_%s.png' % suffix))

    words = Image.new('RGBA', (max(im.width for im in rows), sum(im.height for im in rows)),
                      (0, 0, 0, 0))
    frames, y = [], 0
    for im in rows:
        words.alpha_composite(im, (0, y))
        frames.append((0, y, im.width, im.height))
        y += im.height
    words.save(os.path.join(ASSETS, 'port_mods_words_%s.png' % suffix))

    step = MODS_STEP
    baseline = step // 2 + 8               # a 15-high word stands in the piece's middle
    row0 = MODS_ROW0                       # below the title and its stroke, with air
    x = 240 - page.width // 2
    band, pieces, extra_frames = build_mods_paper(set_name, suffix, sheet, target)
    write_mods_anm2(suffix, page.size, frames, on, off, pieces=pieces, extras=extra_frames)
    print('  %-8s mods page %d x %d, widest line %d, band at %d'
          % (set_name, page.width, page.height, widest, band))
    if show:
        back = Image.new('RGBA', page.size, (170, 160, 158, 255))
        back.alpha_composite(page)
        for i, n in enumerate(rows):
            total = CURSOR_W + n.width + GAP + onoff_w
            nx = page.width // 2 - total // 2 + CURSOR_W
            back.alpha_composite(n, (nx, row0 + i * step + baseline - n.height))
        back.save(SHOW.replace('.png', '-mods-%s.png' % suffix))
    return dict(suffix=suffix, x=x, w=page.width, h=page.height, step=step, baseline=baseline,
                row0=row0, frames=frames, on_w=on[3], off_w=off[3], band=band, extras=extra_frames)


def main():
    show = '--show' in sys.argv
    built = [build_set(*s, show=show) for s in SETS]
    mods = [build_mods_set(s[0], s[1], s[3][len(KINDS):], show) for s in SETS]
    import modset_pages
    settings = [modset_pages.build(s[0], s[1], s[3][len(KINDS):], show) for s in SETS]
    write_header(built, mods, settings)


def write_mods_anm2(suffix, page_size, word_frames, on, off, page_png=None, words_png=None,
                    out_name=None, pieces=None, extras=None):
    """A MODS page's picture: the PC sheet's wall, sketches and arrow, our paper and
    words, and the settings sheet's cursor, ON and OFF - the ones the Tweaks page
    uses. The arrow is the PC mods page's own cursor, (272, 0) of ModsMenu.png: on
    the list it marks a mod that has a settings page."""
    page_png = page_png or 'port_mods_page_%s.png' % suffix
    words_png = words_png or 'port_mods_words_%s.png' % suffix
    out_name = out_name or 'port_mods_menu_%s.anm2' % suffix
    sheets = [(0, 'ui/main menu/OptionsMenu.png'), (1, 'port_mods_sheet_%s.png' % suffix),
              (2, page_png), (3, words_png)]
    # the MODS list's own: the paper in pieces and the words of the player's mods
    many = {'Words': word_frames}
    if pieces:
        sheets.append((4, 'port_mods_paper_%s.png' % suffix))
        many['Pieces'], many['Extra'] = pieces, extras
    mid_x, head_y = 240, MODS_PAPER_Y + MODS_HEAD[3]
    layers = [('Wall', 1, 0, 0, 0, 0, 256, 0, 10, 10, 0)]
    for i, ((cx, cy, w, h), (dx, dy)) in enumerate(MODS_SKETCHES):
        layers.append(('Sketch%d' % (i + 1), 1, mid_x + dx, head_y + dy, 16, 16, cx, cy, w, h, 0))
    layers += [('Paper', 2, 0, 0, 0, 0, 0, 0, page_size[0], page_size[1], 0),
               ('Cursor', 0, 0, 0, 16, 16, 235, 24, 18, 22, 0),
               ('On', 0, 0, 0, 16, 16) + tuple(on) + (90,),
               ('Off', 0, 0, 0, 16, 16) + tuple(off) + (90,),
               ('Words', 3, 0, 0, 0, 0, 0, 0, word_frames[0][2], word_frames[0][3], 0),
               ('Arrow', 1, 0, 0, 0, 8, 272, 0, 16, 16, 0)]
    anims = [('Wall', ['Wall']), ('Arrow', ['Arrow']),
             ('Sketches', ['Sketch%d' % (i + 1) for i in range(len(MODS_SKETCHES))]),
             ('Paper', ['Paper']), ('Cursor', ['Cursor']), ('On', ['On']), ('Off', ['Off']),
             ('Words', ['Words'])]
    if pieces:
        layers += [('Pieces', 4, 0, 0, 0, 0) + tuple(pieces[0]) + (0,),
                   ('Extra', 4, 0, 0, 0, 0) + tuple(extras[0]) + (0,)]
        anims += [('Pieces', ['Pieces']), ('Extra', ['Extra'])]
    T = '\t'
    out = ['<AnimatedActor>',
           T + '<Info CreatedBy="Isaac Android port" CreatedOn="23/09/2026" Version="2" Fps="30"/>',
           T + '<Content>', T * 2 + '<Spritesheets>']
    out += [T * 3 + '<Spritesheet Path="%s" Id="%d"/>' % (pth, i) for i, pth in sheets]
    out += [T * 2 + '</Spritesheets>', T * 2 + '<Layers>']
    out += [T * 3 + '<Layer Name="%s" Id="%d" SpritesheetId="%d"/>' % (l[0], i, l[1])
            for i, l in enumerate(layers)]
    out += [T * 2 + '</Layers>', T * 2 + '<Nulls/>', T * 2 + '<Events/>', T + '</Content>',
            T + '<Animations DefaultAnimation="Wall">']
    for anim, shown in anims:
        count = len(many[anim]) if anim in many else 1
        out.append(T * 2 + '<Animation Name="%s" FrameNum="%d" Loop="false">' % (anim, count))
        out += [T * 3 + '<RootAnimation>',
                T * 4 + '<Frame XPosition="0" YPosition="0" XScale="100" YScale="100" Delay="%d"'
                ' Visible="true" RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255"'
                ' RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0"'
                ' Interpolated="false"/>' % count,
                T * 3 + '</RootAnimation>', T * 3 + '<LayerAnimations>']
        for i, (name, sh, x, y, px, py, cx, cy, w, h, rot) in enumerate(layers):
            here = name in shown
            out.append(T * 4 + '<LayerAnimation LayerId="%d" Visible="%s">'
                       % (i, 'true' if here else 'false'))
            if here and name in many:
                for fx, fy, fw, fh in many[name]:
                    out.append(T * 5 + frame(0, 0, 0, 0, fx, fy, fw, fh, 0, 1))
            else:
                out.append(T * 5 + frame(x, y, px, py, cx, cy, w, h, rot, count, here))
            out.append(T * 4 + '</LayerAnimation>')
        out += [T * 3 + '</LayerAnimations>', T * 3 + '<NullAnimations/>', T * 3 + '<Triggers/>',
                T * 2 + '</Animation>']
    out += [T + '</Animations>', '</AnimatedActor>', '']
    open(os.path.join(ASSETS, out_name), 'w', newline='\n').write('\n'.join(out))


# ------------------------------------------------------------------ .anm2
# name, sheet, x, y, pivot x, pivot y, crop x, crop y, w, h, rotation - every one
# of the game's pieces at the place and pivot its own .anm2 gives it
BASE_LAYERS = [
    ('Wall',    0,   0,   0,  0,  0, 244,   1, 10, 10,  0),
    ('Sketch',  1,  32,  33, 16, 16,   0,   0, 80, 96,  0),
    ('Sketch2', 1, 116, 109, 16, 16,  80,   0, 48, 80,  0),
    ('Sketch3', 1, 395, 156, 16, 16, 128,   0, 80, 80,  0),
    ('Sketch5', 1,  -6, 231, 16, 16,   0,  96, 64, 48,  0),
    ('Paper',   2,   0,   0,  0,  0,   0,   0,  0,  0,  0),
    ('Slider',  0,   0,   0,  0,  0, 192, 400, 64, 16,  0),
    ('Cursor',  0,   0,   0, 16, 16, 235,  24, 18, 22,  0),
    ('On',      0,   0,   0, 16, 16, 235, 127, 22, 40, 90),
    ('Off',     0,   0,   0, 16, 16, 236,  74, 21, 39, 90),
    ('Words',   3,   0,   0,  0,  0,   0,   0,  0,  0,  0),
]

ANIMS = [('Wall', ['Wall']), ('Sketches', ['Sketch', 'Sketch2', 'Sketch3', 'Sketch5']),
         ('Paper', ['Paper']), ('Slider', ['Slider']), ('Cursor', ['Cursor']),
         ('On', ['On']), ('Off', ['Off']), ('Words', ['Words'])]


def frame(x, y, px, py, cx, cy, w, h, rot, delay, visible=True):
    return ('<Frame XPosition="%d" YPosition="%d" XPivot="%d" YPivot="%d" XCrop="%d" YCrop="%d"'
            ' Width="%d" Height="%d" XScale="100" YScale="100" Delay="%d" Visible="%s"'
            ' RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255" RedOffset="0"'
            ' GreenOffset="0" BlueOffset="0" Rotation="%d" Interpolated="false"/>'
            % (x, y, px, py, cx, cy, w, h, delay, 'true' if visible else 'false', rot))


def write_anm2(suffix, page_size, word_frames, on, off):
    sheets = [(0, 'ui/main menu/OptionsMenu.png'), (1, 'ui/main menu/sketch.png'),
              (2, 'port_tweaks_page_%s.png' % suffix), (3, 'port_tweaks_words_%s.png' % suffix)]
    layers = []
    for l in BASE_LAYERS:
        l = list(l)
        if l[0] == 'Paper':
            l[8], l[9] = page_size
        if l[0] == 'Words':
            l[8], l[9] = word_frames[0][2], word_frames[0][3]
        if l[0] in ('On', 'Off'):
            l[6], l[7], l[8], l[9] = on if l[0] == 'On' else off
        layers.append(l)
    out = ['<AnimatedActor>',
           '\t<Info CreatedBy="Isaac Android port" CreatedOn="16/09/2026" Version="2" Fps="30"/>',
           '\t<Content>', '\t\t<Spritesheets>']
    out += ['\t\t\t<Spritesheet Path="%s" Id="%d"/>' % (p, i) for i, p in sheets]
    out += ['\t\t</Spritesheets>', '\t\t<Layers>']
    out += ['\t\t\t<Layer Name="%s" Id="%d" SpritesheetId="%d"/>' % (l[0], i, l[1])
            for i, l in enumerate(layers)]
    out += ['\t\t</Layers>', '\t\t<Nulls/>', '\t\t<Events/>', '\t</Content>',
            '\t<Animations DefaultAnimation="Wall">']
    for anim, shown in ANIMS:
        count = {'Slider': 11, 'Words': len(word_frames)}.get(anim, 1)
        out.append('\t\t<Animation Name="%s" FrameNum="%d" Loop="false">' % (anim, count))
        out += ['\t\t\t<RootAnimation>',
                '\t\t\t\t<Frame XPosition="0" YPosition="0" XScale="100" YScale="100" Delay="%d"'
                ' Visible="true" RedTint="255" GreenTint="255" BlueTint="255" AlphaTint="255"'
                ' RedOffset="0" GreenOffset="0" BlueOffset="0" Rotation="0"'
                ' Interpolated="false"/>' % count,
                '\t\t\t</RootAnimation>', '\t\t\t<LayerAnimations>']
        for i, (name, sheet, x, y, px, py, cx, cy, w, h, rot) in enumerate(layers):
            here = name in shown
            out.append('\t\t\t\t<LayerAnimation LayerId="%d" Visible="%s">'
                       % (i, 'true' if here else 'false'))
            if here and name == 'Slider':
                for n in range(11):
                    out.append('\t\t\t\t\t' + frame(x, y, px, py, cx, 400 - 16 * n, w, h, rot, 1))
            elif here and name == 'Words':
                for fx, fy, fw, fh in word_frames:
                    out.append('\t\t\t\t\t' + frame(0, 0, 0, 0, fx, fy, fw, fh, 0, 1))
            else:
                out.append('\t\t\t\t\t' + frame(x, y, px, py, cx, cy, w, h, rot, count, here))
            out.append('\t\t\t\t</LayerAnimation>')
        out += ['\t\t\t</LayerAnimations>', '\t\t\t<NullAnimations/>', '\t\t\t<Triggers/>',
                '\t\t</Animation>']
    out += ['\t</Animations>', '</AnimatedActor>', '']
    open(os.path.join(ASSETS, 'port_tweaks_menu_%s.anm2' % suffix), 'w',
         newline='\n').write('\n'.join(out))


def write_header(built, mods, settings):
    import modset_pages as mp
    n_scale = len(SETS[0][4])
    n_lang = 1 + len(LANG_CODES)
    L = []
    L += ['/* Written by tools/tweaks_page.py - do not edit by hand, run the tool. */',
          '#ifndef TWEAKS_LAYOUT_H', '#define TWEAKS_LAYOUT_H', '',
          '#define TW_SETS %d' % len(built),
          '#define TW_ROW_COUNT %d' % len(KINDS),
          '#define TW_PAPER_Y %d' % PAPER_Y,
          '#define TW_GAP %d' % GAP,
          '#define TW_CURSOR_W %d' % CURSOR_W,
          '#define TW_TICKS_W %d' % TICKS_W,
          '#define TW_ONOFF_W %d' % ONOFF_W, '',
          '/* the Words frames: the Render Scale steps, then AUTO and the language',
          '   codes, then one name per row */',
          '#define TW_SCALE_WORDS %d' % n_scale,
          '#define TW_LANG_WORDS %d' % n_lang,
          '#define TW_NAME_FRAME %d' % (n_scale + n_lang),
          '#define TW_ACTION_FRAME %d' % (n_scale + n_lang + len(KINDS)), '',
          '/* the MODS page: one switch per ported mod, its names from frame 0; the',
          '   arrow beside a mod that has a settings page */',
          '#define MD_ROW_COUNT %d' % MOD_ROWS,
          '#define MD_NAME_FRAME 0',
          '#define MD_PAPER_Y %d' % MODS_PAPER_Y,
          '#define MD_ARROW_W %d' % ARROW_W, '',
          '/* the player\'s own mods on the MODS page: the paper laid from Pieces (head,',
          '   five pieces, foot; each left part, band, right part), the Extra frames */',
          '#define MD_BAND_W %d' % MODS_BAND_W,
          '#define MD_HEAD_H %d' % MODS_HEAD[3],
          '#define MD_FOOT_H %d' % (MODS_FOOT[3]),
          '#define MD_PIECE_H %d' % MODS_STEP,
          '#define MD_TITLE_Y %d' % TITLE_Y,
          '#define MD_SECTION_GAP %d' % SECTION_GAP,
          '#define MD_MARGIN %d' % MODS_MARGIN,
          '#define MX_COUNT %d' % len(MX_NAMES)]
    L += ['#define MX_%s %d' % (n, i) for i, n in enumerate(MX_NAMES)]
    L += ['',
          '/* the mods\' settings pages (tools/modset_pages.py): the digits a number is',
          '   put together from are frames 0.., in this order */',
          '#define SP_PAGES %d' % len(mp.PAGES),
          '#define SP_DIGITS "%s"' % mp.DIGITS,
          '#define SP_BOOL %d' % mp.BOOL, '#define SP_NUM %d' % mp.NUM,
          '#define SP_CHOICE %d' % mp.CHOICE, '',
          '/* one per language set and page: its file suffix, where its paper stands and',
          '   how big it is (settings-screen units), how far apart its rows are, where',
          '   the words sit on a row and where the first row starts, ON and OFF\'s',
          '   lengths, and the sizes of the Words frames */',
          'typedef struct {',
          '    const char *suffix;',
          '    short paper_x, paper_w, paper_h, step, baseline, row0, on_w, off_w;',
          '    const short *word_w, *word_h;',
          '} tw_set_t;', '']

    def arrays(tag, frames):
        L.append('static const short %s_W[] = { %s };' % (tag, ', '.join(str(f[2]) for f in frames)))
        L.append('static const short %s_H[] = { %s };' % (tag, ', '.join(str(f[3]) for f in frames)))

    def record(b, tag):
        return ('    { "%s", %d, %d, %d, %d, %d, %d, %d, %d, %s_W, %s_H },'
                % (b['suffix'], b['x'], b['w'], b['h'], b['step'], b['baseline'], b['row0'],
                   b['on_w'], b['off_w'], tag, tag))

    for k, b in enumerate(built):
        arrays('TWW%d' % k, b['frames'])
    for k, b in enumerate(mods):
        arrays('MDW%d' % k, b['frames'])
    for k, st in enumerate(settings):
        arrays('SPW%d' % k, st['frames'])
    L += ['', 'static const tw_set_t TW_SET[TW_SETS] = {']
    L += [record(b, 'TWW%d' % k) for k, b in enumerate(built)]
    L += ['};', '', 'static const tw_set_t MD_SET[TW_SETS] = {']
    L += [record(b, 'MDW%d' % k) for k, b in enumerate(mods)]
    L += ['};', '', '/* where each set\'s band starts, and the sizes of its Extra frames */',
          'static const short MD_BAND_X[TW_SETS] = { %s };' % ', '.join(str(b['band']) for b in mods),
          'static const short MX_W[TW_SETS][MX_COUNT] = {']
    L += ['    { %s },' % ', '.join(str(f[2]) for f in b['extras']) for b in mods]
    L += ['};', 'static const short MX_H[TW_SETS][MX_COUNT] = {']
    L += ['    { %s },' % ', '.join(str(f[3]) for f in b['extras']) for b in mods]
    L += ['};', '', 'static const tw_set_t SP_SET[SP_PAGES][TW_SETS] = {']
    for p in range(len(mp.PAGES)):
        L.append('  {')
        L += [record(st['records'][p], 'SPW%d' % k) for k, st in enumerate(settings)]
        L.append('  },')
    L += ['};', '']

    rows = settings[0]['rows']
    for st in settings[1:]:
        assert st['rows'] == rows, 'the settings rows differ between sets'
    L += ['/* a settings row: the modset id, the component of an RGB, its kind, the Words',
          '   frame of its label and of its first choice word, and how many choices */',
          'typedef struct { short ms, comp, kind, label, choice, count; } sp_row_t;', '',
          'static const sp_row_t SP_ROW[] = {']
    L += ['    { %d, %d, %d, %d, %d, %d },' % (r['ms'], r['comp'], r['kind'], r['label'],
                                               r['choice'], r['count']) for r in rows]
    L += ['};', '',
          '/* a page: the MODS row it belongs to, its first row in SP_ROW, how many */',
          'static const short SP_PAGE[SP_PAGES][3] = {']
    L += ['    { %d, %d, %d },' % pg for pg in settings[0]['pages']]
    L += ['};', '']

    L += ['/* the game\'s language number (Manager+0x564f8) -> the set; -1 means English */',
          'static int tw_set_for_language(int language) {',
          '    switch (language) {']
    index = {b['suffix']: i for i, b in enumerate(built)}
    for num, suffix in sorted(GAME_LANGUAGE.items()):
        L.append('        case %d: return %d;' % (num, index[suffix]))
    L += ['        default: return 0;', '    }', '}', '',
          '/* the description languages, in the order of their word frames after AUTO */',
          'static const char *const TW_LANG_CODE[] = {',
          '    "auto", ' + ', '.join('"%s"' % c for c in LANG_CODES), '};', '',
          '#endif', '']
    open(HEADER, 'w', newline='\n').write('\n'.join(L))

if __name__ == '__main__':
    main()
