# -*- coding: utf-8 -*-
"""The controls guide's words, one set per language of the game's menus (the
owner, 25.09.2026: approved in English, then into every language).

Names the game has are the game's own: the items and characters from its
stringtable.sta (the Russian names of Schoolbag, Marked, D infinity
...), the stick types and touch modes from lang.WORDS (what our
Options rows show), the Tweaks rows and titles from tweaks_page.SETS.
Japanese titles are kana, as all our Japanese lettering is (lang.py); the
labels and notes are the game's own Japanese font, which carries kanji.

THE NATIVE ROUND (06.10.2026): every set rewritten by a native writer (tools/guide_lang/,
tools/guide_brief.md), then two fixes from the owner in every language: the recipe lock
said plainly (hold a finger on the recipes; L3 on a pad; TAB + shoot left on a keyboard),
and the pad's buttons drawn rather than named - {VIEW} {MENU} {A} {B} {LEFT} {RIGHT} {LB}
{RB} {LT} {RT} {LS} {RS} in a key or a note is that button's picture
(guide_slides.PAD_ICON_CELLS: the game's gfx/ui/buttons.png, as the EID mod draws them).

Per set:
    titles      ten, in the slides' order (the tenth is the Tweaks title)
    move_shoot  the two labels of the first slide
    types       the three stick types' second lines (their names are lang.WORDS)
    modes       the three touch modes' second lines
    buttons     TAP: PAUSE, HOLD: RESTART, TAP: BOMB, TAP: USE, TAP: USE, HOLD: DROP
    special     four (first line, second line)
    map         four (key, what it does)
    desc        two columns of two lines
    pad         eight: what Select, R3, L3, L1 R1 L2 R2 (drawn as the pad's buttons), TAB, TAB + shoot,
                ESC, R do (R held: the restart, as on the PC - reset.c, 08.10.2026); `shoot_key`
    tweaks      six: what AIM LOCK, DEAD ZONE, MOVE and AIM SENSITIVITY, RENDER SCALE, RUMBLE do
                (RENDER SCALE's line names ORIGINAL, the default since 18.10.2026)
                (batch 3, 01.10.2026: DEAD ZONE is the threshold in DIGITAL too, MOVE SENSITIVITY
                is for ANALOGUE only, RUMBLE is a slider; hotfix 1.75: RUMBLE left the Tweaks
                page for the game's own Options - the row that was FILTER - so its line says
                where it is now, and its key is the Options row's own lettering,
                guide_slides.OPTIONS_RUMBLE)
    aimlock_release, aimlock_note
                how AIM LOCK is let go of (the 20th round, 05.10.2026: a quick tap in the middle of the fire stick
                or D-pad, its whole dead zone; touch only): the AIM LOCK row's second line, and one sentence
                that follows tweaks_notes[0] in the first Tweaks note - their own keys, so the
                wording can change alone (the rule approved by the owner the same day)
    tweaks2_title, tweaks2
                the second Tweaks slide (the owner, 05.10.2026: "explain the tweaks and mods on
                slides of their own"): ITEM SPAWN, SPAWNER TROPHIES, CONFIG FILE, IMPORT SAVES,
                EXPORT SAVES; an entry is a line or a list of lines
    tweaks_notes
                the two Tweaks slides' notes (the first is where, then aimlock_note)
    mods2_title, mods, mods_notes
                the port's own mods on MODS, four a slide, in MOD_KEY order (dance planet waves
                bars | marks stats rangefix eid); the first slide is titled with MODS_WORD
    user_key, user, page_title, page, user_notes
                the player's own mods: the list (+ ADD MOD, EXPORT GAME FILES, a mod's row) and a
                mod's page (ENABLED, MOVE UP / DOWN, EXPORT, DELETE, SETTINGS, RESTART TO APPLY),
                their names tweaks_page.USER_WORDS; the notes: PC mods and Lua, update or beside,
                the check notes and mods/<id>.check.txt
    coop_title, coop_keys, coop, coop_note
                the LOCAL CO-OP slide (the owner, 28.09.2026): five keys (the last one the keyboard's confirm and cancel) and what
                they do, the game's own character choice under them, and its note; it stands before the Tweaks slide
    notes       ten, the yellow notes of the first ten slides (the co-op note is coop_note;
                the Tweaks note became tweaks_notes in the 20th round)
    spawn_title, spawn_tabs, spawn_rows, spawn_notes
                the two ITEM SPAWN slides after the Tweaks slide (the owner, 03.10.2026:
                "a slide in the GUIDE explaining how to use all of it"). The first is
                titled with the Tweaks row's own name and lists the four tabs, each
                lettered as the card letters it (tools/spawner_tabs.py), with what a tap
                does there (spawn_tabs); the second (spawn_title) is a legend of the
                card's controls - its keys are the card's own words where it has them
                (spawner_stats.PLAYER, RESET) - with seven lines (spawn_rows):
                co-op player, the arrows, typing, RESET, L1/R1, TAB, ENTER/ESC.
                spawn_notes: where the strap is and what it costs (SPAWNER TROPHIES);
                the search, how long a stat lasts, where enemies appear
"""

TEXT = {
    'data': dict(
        titles=['MOVE AND SHOOT', 'STICK TYPES', 'TOUCH MODES', 'DOUBLE TAP', 'BUTTONS', 'SPECIAL BUTTONS',
                'MAP AND ITEMS', 'ITEM DESCRIPTIONS', 'YOUR LAYOUT', 'GAMEPAD & KEYBOARD'],
        move_shoot=['MOVE', 'SHOOT'],
        types=['STAYS PUT', 'JUMPS TO YOUR THUMB', 'FOLLOWS YOUR THUMB'],
        modes=['PUSH MORE, GO FASTER', 'STOP OR FULL SPEED', 'D-PADS'],
        dtap=['tap the same side twice', 'tap, then flick in one direction', 'the same as DYNAMIC',
              'tap an arrow twice'],
        buttons=['TAP: PAUSE', 'HOLD: RESTART', 'TAP: BOMB', 'TAP: USE', 'TAP: USE', 'HOLD: DROP', 'HOLD: DROP'],
        special=[('TWINS:', 'HOLD STILL'), ('MARKED:', 'STOP FIRING'), ('2ND ITEM:', 'SWAP'),
                 ('D INFINITY:', 'HOLD: PICK A DIE')],
        map=[('TAP', 'keeps the big map open'), ('HOLD', 'shows it while you hold'),
             ('HOLD LONGER', 'your items, described'), ('SWIPE', 'scroll with a second finger')],
        desc=[['WALK UP TO AN ITEM', 'TO SEE WHAT IT DOES'],
              ['HOLD UP THE BAG', 'TO SEE RECIPES', 'ON THE TOP HALF:', 'SWIPE TO TURN PAGES,', 'HOLD TO LOCK THEM']],
        pad=['hold: your items (descriptions on)', 'recipes, descriptions: next page', 'page back; hold: lock the recipes',
             'hold all four: restart', 'hold: your items (descriptions on)', 'Bag of Crafting: pages, shoot left: lock', 'pause', 'hold: restart'],
        shoot_key='TAB + SHOOT',
        tweaks=['keeps shooting after you let go', 'how far to push before you move',
                ['how far to push for full speed', 'ANALOGUE only'], 'how far to push before firing',
                'ORIGINAL is the iPhone look', 'strength: now in Options'],
        aimlock_release='tap the centre to stop',
        aimlock_note='AIM LOCK is for touch: to stop it, tap the fire stick or D-pad in the centre and let '
                     'go without sliding. The next touch locks again.',
        tweaks2_title='MORE TWEAKS',
        tweaks2=['spawner on the pause screen', ['ON: spawner runs still', 'count for unlocks'],
                 'frames a second at the top', "opens the port's folder", ['load saves from other', 'versions of the game'],
                 'copy your saves to a folder'],
        tweaks_notes=[
            'Options > TWEAKS.',
            'IMPORT SAVES takes saves from other versions of the game; the PC game reads the ones EXPORT '
            'SAVES makes. The spawner: the next two pages.'],
        mods2_title='MORE MODS',
        mods=[['a quality 4 item in the room:', 'everyone dances to the music'],
              ['your Planetarium chance for', 'the next floor, by the stats'],
              ['shows which wave you are on', 'in Boss Rush'],
              ['a health bar for every boss,', 'with its icon and status'],
              ['marks you still need show', 'faintly on the completion note'],
              ['extra numbers by the stats:', 'damage multiplier, tear cap'],
              ['melee hits enemies right next', 'to you, even with high range'],
              ['shows what an item does', 'when you walk up to it (EID)']],
        mods_notes=[
            'Main menu > MODS: PC mods rebuilt for this port, nothing to install. Tap a row to turn a mod on '
            'or off; where there is an arrow, it opens the settings.',
            'RANGE FIX helps melee: bones, swords, the axe and the Bag of Crafting. ITEM DESCRIPTIONS can '
            'speak another language: choose it behind its arrow.'],
        user_key='MOD NAME',
        user=[['pick one or more .zip mods;', 'PC mods are converted'],
              ["copies the game's files out,", 'to start your own mods'],
              ['tap: turn it on or off;', 'arrow: open its page']],
        page_title="A MOD'S PAGE",
        page=['turns the mod on or off', ['when two mods change one file,', 'the higher one wins'],
              'saves the mod as a .zip', 'tap twice to remove it',
              "the mod's own options", 'restart so changes take effect'],
        user_notes=[
            'Steam Workshop mods work if they change pictures, sounds or data; Lua scripts do not run. Add '
            'a mod you already have and you can update it or keep both.',
            'A red name means part of the mod is not used. Red lines above the title on its page say why, or '
            'name the mod it clashes with; tap one for details.'],
        coop_title='LOCAL CO-OP',
        coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
        coop=['hold 1.5 s on a spare pad to join', 'the same on a keyboard', 'pick a character',
              'confirm / cancel', 'confirm / cancel on a keyboard'],
        coop_note='At the start of a run, hold {VIEW} and {MENU} on a spare pad for a second and a half, then '
                  'pick a character with the arrows. Up to four players.',
        spawn_title='SPAWNER CONTROLS',
        spawn_tabs=['tap: pick; again: get it', 'tap: pick; again: spawn', 'room: one tap; floor: two',
                    'type a number or step it'],
        spawn_rows=['co-op: which player', 'count / value: set; stats: tap row or {A}', 'tap again to type a number', 'undo your changes',
                    'switch tabs', 'switch tabs (keyboard)', 'confirm / close'],
        spawn_notes=[
            'Turn on TWEAKS > CONSOLE; in a run, tap the CONSOLE tab on the pause screen. Runs that '
            'use it unlock nothing unless CONSOLE TROPHIES is on.',
            'ITEMS lists it all: pickups, pills, trinkets, loot too. ON THE GROUND drops things by you; '
            'arrows: how many. Search by name or number. Stats last the run.'],
        notes=[
            'Left stick moves, right stick shoots. Pick the stick type, size and touch mode in Options; '
            'the next two pages explain them.',
            'Options > TYPE. STATIC stays where you put it, DYNAMIC jumps to where your thumb lands, '
            'FLOATING follows your thumb around.',
            'Options > TOUCH MODE. ANALOGUE: push further to go faster. DIGITAL: still, or full speed past the '
            'DEAD ZONE. BUTTONS: D-pads instead of sticks.',
            'Mars dashes and Hemoptysis sneezes on a double tap in one direction. Both sticks work alike; '
            'the second tap must come within half a second.',
            'Hold pause to restart the run; let go before the screen goes dark to cancel. Your trinket is a '
            'button too: hold it to drop it, no pill or card needed.',
            "Round button: holds the other body still (Jacob and Esau, Strawman) or swaps the Forgotten's "
            'bodies. Schoolbag: tap to swap. D infinity: hold to pick a die.',
            'Tap the minimap to keep the big map open. With ITEM DESCRIPTIONS on, hold it longer to list '
            'your items with descriptions; scroll with a second finger.',
            'A long description is shown in pages: swipe across the top of the panel, or {LS} / {RS} on '
            'a gamepad. Settings: MODS > ITEM DESCRIPTIONS.',
            'Options > Controls > Touch: drag the sticks and buttons, pause included, wherever you like. '
            'OPACITY: how see-through they are. GRID lines them up.',
            "The rest is in Options > Controls. Jacob and Esau: hold {RT}, then {LT} uses Jacob's card or pill, {RB} Esau's (keyboard: Ctrl + Space / Q).",
        ],
    ),
    'dlc3.ru': dict(
        titles=['ХОДЬБА И СТРЕЛЬБА', 'ТИПЫ СТИКОВ', 'РЕЖИМЫ УПРАВЛЕНИЯ', 'ДВОЙНОЙ ТАП', 'КНОПКИ',
                'ОСОБЫЕ КНОПКИ', 'КАРТА И ПРЕДМЕТЫ', 'ОПИСАНИЯ ПРЕДМЕТОВ', 'СВОЯ РАСКЛАДКА',
                'ГЕЙМПАД И КЛАВИАТУРА'],
        move_shoot=['ХОДЬБА', 'СТРЕЛЬБА'],
        types=['СТОИТ НА МЕСТЕ', 'ПОЯВЛЯЕТСЯ ПОД ПАЛЬЦЕМ', 'ЕЗДИТ ЗА ПАЛЬЦЕМ'],
        modes=['ЧЕМ ДАЛЬШЕ, ТЕМ БЫСТРЕЕ', 'СРАЗУ НА ПОЛНУЮ', 'КРЕСТОВИНЫ'],
        dtap=['тап-тап в одну сторону', 'тап, потом тап со свайпом', 'так же, как динамичный',
              'тап-тап по стрелке'],
        buttons=['ЖМИ: ПАУЗА', 'ДЕРЖИ: ЗАНОВО', 'ЖМИ: БОМБА', 'ЖМИ: В ХОД', 'ЖМИ: В ХОД', 'ДЕРЖИ: ВЫБРОС',
                 'ДЕРЖИ: ВЫБРОС'],
        special=[('ВТОРОЕ ТЕЛО', 'СТОИТ'), ('ОТМЕЧЕННЫЙ', 'НЕ СТРЕЛЯТЬ'), ('ПОРТФЕЛЬ', 'СМЕНА'),
                 ('БЕСКОН. D', 'ДЕРЖИ: ВЫБОР КУБИКА')],
        map=[('ТАП', 'большая карта остаётся открытой'), ('ДЕРЖИ', 'карта видна, пока держишь'),
             ('ДОЛЬШЕ', 'инвентарь с описаниями (EID)'), ('СВАЙП', 'листай вторым пальцем')],
        desc=[['ПОДОЙДИ К ПРЕДМЕТУ -', 'УВИДИШЬ, ЧТО ОН ДЕЛАЕТ'],
              ['ДОСТАНЬ МЕШОК -', 'УВИДИШЬ РЕЦЕПТЫ', 'ПО ВЕРХУ ПАНЕЛИ:', 'СВАЙП - ЛИСТАТЬ,', 'ЗАЖМИ - ЗАКРЕПИТЬ']],
        pad=['держи: инвентарь (если включён EID)', 'рецепты, описания: листать вперёд', 'назад; держи: закрепить рецепты',
             'зажми все четыре: забег заново', 'держи: инвентарь, как {VIEW}', 'мешок: листать, влево - закрепить',
             'пауза', 'держи: забег заново'],
        shoot_key='TAB + СТРЕЛЬБА',
        tweaks=['стреляет, даже когда отпустишь', 'наклон стика, с которого идёшь',
                ['наклон для полной скорости;', 'только в режиме АНАЛОГ'], 'наклон, с которого стреляешь',
                'ОРИГИНАЛ - как на iPhone', 'сила: теперь в Настройках'],
        aimlock_release='снять: тапни в центр',
        aimlock_note='Только для сенсора. Снять: короткий тап в центр стика стрельбы или крестовины, '
                     'без сдвига. Новое касание включит снова.',
        tweaks2_title='ЕЩЁ ТВИКИ',
        tweaks2=['спавнер в меню паузы', ['вкл: забеги со спавнером', 'тоже дают достижения'],
                 'кадры в секунду вверху экрана', 'открывает папку порта', ['сохранения из других', 'версий игры'],
                 'копия сохранений в папку'],
        tweaks_notes=[
            'Настройки > ТВИКИ.',
            'ИМПОРТ СОХРАНЕНИЙ берёт сохранения из других версий игры, а выгруженные читает игра на ПК. '
            'Про спавнер - на следующих двух страницах.'],
        mods2_title='ЕЩЁ МОДЫ',
        mods=[['предмет качества 4 в комнате:', 'все танцуют под музыку'],
              ['шанс Планетария на следующем', 'этаже, рядом со статами'],
              ['какая сейчас волна', 'в Босс-раше'],
              ['полоска здоровья у каждого', 'босса, с иконкой и статусом'],
              ['недостающие отметки бледно', 'видны на листке персонажа'],
              ['доп. цифры у статов:', 'множитель урона, лимит слёз'],
              ['удар вплотную попадает', 'даже при большой дальности'],
              ['показывает, что делает предмет,', 'когда подходишь (EID)']],
        mods_notes=[
            'Главное меню > МОДЫ. Это моды с ПК, переделанные под порт, ставить ничего не нужно. Тап по '
            'строке - вкл/выкл, стрелка - настройки мода.',
            'ФИКС ДАЛЬНОСТИ - для ближнего боя: кости, мечи, топор, Созидательный мешок. Язык ОПИСАНИЙ '
            'ПРЕДМЕТОВ меняется в настройках за стрелкой.'],
        user_key='НАЗВАНИЕ МОДА',
        user=[['выбери один или несколько .zip;', 'моды с ПК переделаются сами'],
              ['выгружает файлы игры -', 'основа для своих модов'],
              ['тап: вкл / выкл;', 'стрелка: страница мода']],
        page_title='СТРАНИЦА МОДА',
        page=['включить или выключить мод', ['два мода меняют один файл -', 'побеждает тот, что выше'],
              'сохранить мод в .zip', 'тапни дважды, чтобы удалить',
              'настройки самого мода', 'перезапуск игры с изменениями'],
        user_notes=[
            'Моды из мастерской Steam идут, если меняют картинки, звуки и данные; Lua не запускается. '
            'Добавишь мод снова - обнови или поставь рядом.',
            'Красное название - порт берёт из мода не всё. Почему или с кем конфликт - в красных строках '
            'над заголовком его страницы; тап - подробности.'],
        coop_title='ЛОКАЛЬНЫЙ КООП',
        coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
        coop=['свободный пад: зажми на 1,5 с', 'то же с клавиатуры', 'выбор персонажа',
              'ок / отмена', 'ок / отмена с клавиатуры'],
        coop_note='В начале забега зажми {VIEW} и {MENU} на свободном паде на полторы секунды, потом выбери '
                  'персонажа стрелками. Играть можно вчетвером.',
        spawn_title='УПРАВЛЕНИЕ СПАВНЕРОМ',
        spawn_tabs=['тап: выбор, второй: взять', 'тап: выбор, второй: спавн', 'комната - 1 тап, этаж - 2',
                    'впиши число или жми < >'],
        spawn_rows=['кооп: чьи статы меняешь', 'задать; статы: тап по строке или {A}', 'тапни ещё раз, чтобы ввести число',
                    'вернуть всё как было', 'сменить вкладку', 'сменить вкладку с клавиатуры',
                    'ок / закрыть'],
        spawn_notes=[
            'Включи: ТВИКИ > КОНСОЛЬ. В забеге она на своей вкладке в паузе. Пока ТРОФЕИ '
            'КОНСОЛИ выключены, забег ничего не открывает.',
            'В ПРЕДМЕТАХ всё, и расходники, и лут. НА ПОЛУ - рядом с тобой, стрелки - сколько штук. '
            'Поиск по названию или номеру. Стат держится до конца забега.'],
        notes=[
            'Левый стик - ходьба, правый - стрельба. Тип стиков, размер и режим выбираются в Настройках, '
            'о них - на следующих двух страницах.',
            'Настройки > ТИП. Статичный стоит там, где его поставили, динамичный появляется там, куда '
            'ставишь палец, а плавающий ездит за пальцем.',
            'Настройки > РЕЖИМ. АНАЛОГ: чем дальше, тем быстрее. ЦИФРОВОЙ: за МЁРТВОЙ ЗОНОЙ сразу '
            'на полную. КНОПКИ: крестовины вместо стиков.',
            'Марс делает рывок, а Кровохарканье чихает, если дважды тапнуть в одну сторону. Так работают '
            'оба стика; второй тап - в течение полсекунды.',
            'Держи паузу, чтобы начать забег заново; отпустишь до затемнения - отмена. Брелок тоже кнопка: '
            'держи, чтобы выбросить, пилюля или карта не нужна.',
            'Круглая кнопка у паузы (Иаков и Исав, Чучело): второе тело замирает, а Забытый меняет '
            'тела. Портфель: тап. Бесконечный D: держи и выбирай.',
            'Тап по мини-карте - большая карта не закрывается. С ОПИСАНИЯМИ ПРЕДМЕТОВ держи дольше: '
            'откроется инвентарь, листай вторым пальцем.',
            'Длинное описание листается: проведи пальцем по верху панели или нажми {LS} / {RS} на '
            'геймпаде. Настройки: МОДЫ > ОПИСАНИЯ ПРЕДМЕТОВ.',
            'Настройки > Управление > Touch: двигай стики, кнопки и паузу куда удобно. НЕПРОЗРАЧНОСТЬ - '
            'насколько они видны, СЕТКА их выравнивает.',
            'Остальное - в Настройки > Управление. Иаков и Исав: держи {RT}, и {LT} тратит карту или пилюлю Иакова, {RB} - Исава (клавиатура: Ctrl + Space / Q).',
        ],
    ),
    'dlc3.de': dict(
        titles=['LAUFEN UND SCHIESSEN', 'STICK-TYPEN', 'TOUCH-MODI', 'DOPPELTIPPEN', 'TASTEN', 'SPEZIALTASTEN',
                'KARTE UND ITEMS', 'ITEM-BESCHREIBUNGEN', 'EIGENES LAYOUT', 'GAMEPAD & TASTATUR'],
        move_shoot=['LAUFEN', 'SCHIESSEN'],
        types=['BLEIBT AN SEINEM PLATZ', 'SPRINGT ZUM DAUMEN', 'FOLGT DEM DAUMEN'],
        modes=['STUFENLOSES TEMPO', 'STOPP ODER VOLLGAS', 'D-PADS STATT STICKS'],
        dtap=['zweimal auf eine Seite tippen', 'tippen, dann tippen und wischen', 'wie bei DYNAMISCH',
              'zweimal auf einen Pfeil tippen'],
        buttons=['TIPP: PAUSE', 'HALTEN: NEUSTART', 'TIPP: BOMBE', 'TIPP: NUTZEN', 'TIPP: NUTZEN',
                 'HALTEN: ABLEGEN', 'HALTEN: ABLEGEN'],
        special=[('ZWILLINGE:', 'ANDEREN ANHALTEN'), ('MARKIERT:', 'FEUER STOPPEN'), ('2. ITEM:', 'WECHSELN'),
                 ('W UNENDLICH:', 'HALTEN: WÜRFEL')],
        map=[('TIPPEN', 'große Karte bleibt offen'), ('HALTEN', 'zeigt sie, solange du hältst'),
             ('LÄNGER', 'deine Items mit Beschreibung'), ('WISCHEN', 'mit zweitem Finger scrollen')],
        desc=[['LAUF ZU EINEM ITEM', 'UND SIEH, WAS ES TUT'],
              ['TASCHE HOCHHALTEN', 'UND REZEPTE SEHEN', 'OBERE HÄLFTE:', 'WISCHEN: BLÄTTERN', 'HALTEN: SPERREN']],
        pad=['halten: Inventar (wenn EID an)', 'Rezepte, Beschreibungen: weiter',
             'zurück; halten: Rezepte sperren', 'alle vier halten: Neustart', 'halten: Inventar (wenn EID an)',
             'Tasche: blättern, links sperrt', 'Pause', 'halten: Neustart'],
        shoot_key='TAB + SCHIESSEN',
        tweaks=['feuert nach Loslassen weiter', 'wie weit, bis du losläufst',
                ['wie weit für Vollgas', 'nur bei ANALOG'], 'wie weit, bis du schießt',
                'ORIGINAL wie am iPhone', 'Stärke: jetzt in Optionen'],
        aimlock_release='stoppen: Mitte kurz antippen',
        aimlock_note="ZIELSPERRE gibt's nur für Touch. Stoppen: Schuss-Stick oder Steuerkreuz kurz mittig "
                     'antippen, nicht wischen. Neuer Tipp sperrt.',
        tweaks2_title='WEITERE TWEAKS',
        tweaks2=['Spawner im Pausenmenü', ['an: Runs mit Spawner', 'schalten trotzdem frei'],
                 'Bilder pro Sekunde oben', 'öffnet den Ordner des Ports', ['Spielstände aus anderen', 'Versionen des Spiels'],
                 'in einen Ordner kopieren'],
        tweaks_notes=[
            'Optionen > TWEAKS.',
            'SPIELSTÄNDE LADEN holt sie aus anderen Versionen des Spiels, gesicherte liest auch die '
            'PC-Version. Zum Spawner: die nächsten zwei Seiten.'],
        mods2_title='WEITERE MODS',
        mods=[['ein Q4-Item im Raum:', 'alle tanzen zur Musik'],
              ['Planetarium-Chance der', 'nächsten Etage, bei Werten'],
              ['zeigt die aktuelle Welle', 'im Boss Rush'],
              ['eine Lebensleiste pro Boss,', 'mit Symbol und Status'],
              ['fehlende Marken blass', 'auf dem Abschlusszettel'],
              ['zeigt Schadensfaktor und', 'Tränenlimit bei den Werten'],
              ['Nahkampf trifft auch nahe', 'Gegner bei viel Reichweite'],
              ['zeigt, was ein Item tut,', 'wenn du hingehst (EID)']],
        mods_notes=[
            'Hauptmenü > MODS: PC-Mods, für den Port nachgebaut, nichts zu installieren. Zeile antippen: '
            'an oder aus. Ein Pfeil öffnet die Einstellungen.',
            'REICHWEITEN-FIX: für Nahkampf mit Knochen, Schwertern, Axt und Handwerkstasche. Sprache der '
            'ITEM-BESCHREIBUNGEN: hinter ihrem Pfeil.'],
        user_key='MOD-NAME',
        user=[['eine oder mehrere .zip wählen;', 'PC-Mods werden umgewandelt'],
              ['kopiert die Spieldateien raus,', 'als Basis für eigene Mods'],
              ['tippen: an oder aus;', 'Pfeil: Seite des Mods']],
        page_title='MOD-SEITE',
        page=['Mod an oder aus', ['ändern zwei Mods eine Datei,', 'gewinnt der obere'],
              'speichert den Mod als .zip', 'zweimal tippen zum Löschen',
              'eigene Optionen des Mods', 'startet neu, damit es wirkt'],
        user_notes=[
            'Workshop-Mods vom PC laufen, wenn sie Bilder, Sounds oder Daten ändern; Lua-Skripte nicht. '
            'Mod schon drauf? Dann aktualisieren oder beide behalten.',
            'Roter Name: Der Mod wird nicht ganz genutzt. Rote Zeilen über dem Titel seiner Seite sagen, '
            'warum, oder mit welchem Mod er sich beißt. Antippen: Details.'],
        coop_title='LOKALER KOOP',
        coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
        coop=['auf freiem Pad 1,5 s: einsteigen', 'dasselbe auf der Tastatur', 'Charakter wählen',
              'bestätigen / abbrechen', 'bestätigen / abbrechen (Tastatur)'],
        coop_note='Zu Beginn eines Runs auf einem freien Pad {VIEW} und {MENU} anderthalb Sekunden halten, dann '
                  'mit den Pfeilen einen Charakter wählen. Bis zu vier Spieler.',
        spawn_title='SPAWNER-STEUERUNG',
        spawn_tabs=['wählen, nochmal: nehmen', 'wählen, nochmal: spawnen', 'Raum: 1x tippen, Etage: 2x',
                    'Zahl eingeben oder Pfeile'],
        spawn_rows=['Koop: wessen Werte du änderst', 'einstellen; Werte: Zeile oder {A}', 'nochmal tippen: Zahl eingeben',
                    'macht deine Änderungen rückgängig', 'Tab wechseln', 'Tab wechseln (Tastatur)',
                    'bestätigen / schließen'],
        spawn_notes=[
            'TWEAKS > KONSOLE einschalten, im Run in der Pause öffnen. Ohne '
            'KONSOLE-TROPHÄEN schaltet so ein Run nichts frei.',
            'ITEMS hat alles, auch Pickups und Beute. AUF DEN BODEN legt es neben dich, Pfeile: Anzahl. '
            'Suche nach Name oder Nummer. Werte gelten bis Run-Ende.'],
        notes=[
            'Linker Stick läuft, rechter schießt. Typ, Größe und Touch-Modus stellst du in den Optionen ein; '
            'die nächsten zwei Seiten erklären sie.',
            'Optionen > TYP. STATISCH bleibt, wo du ihn hinsetzt, DYNAMISCH springt dahin, wo dein Daumen '
            'landet, SCHWEBEND folgt deinem Daumen.',
            'Optionen > TOUCH MODE. ANALOG: je weiter, desto schneller. DIGITAL: Stillstand oder Vollgas ab '
            'der TOTZONE. TASTEN: Steuerkreuze statt Sticks.',
            'Doppeltippen in eine Richtung: Mars sprintet, Hämoptysis niest. Geht mit beiden Sticks; der '
            'zweite Tipp muss innerhalb einer halben Sekunde kommen.',
            'Pause halten startet den Run neu; vor dem Abdunkeln loslassen bricht ab. Dein Schmuckstück ist '
            'auch eine Taste: halten legt es ab, ohne Pille oder Karte.',
            'Runde Taste: hält den anderen still (Jakob und Esau, Strohmann), tauscht beim Vergessenen. '
            'Schulranzen: tippen. W unendlich: halten, Würfel wählen.',
            'Minikarte antippen: große Karte bleibt offen. Mit ITEM-BESCHREIBUNGEN länger halten: deine Items '
            'mit Beschreibung. Mit zweitem Finger scrollen.',
            'Lange Beschreibungen erscheinen seitenweise: oben über das Feld wischen oder {LS} / {RS} '
            'am Controller. Einstellungen: MODS > ITEM-BESCHREIBUNGEN.',
            'Optionen > Steuerung > Touch: zieh Sticks, Tasten und Pause, wohin du willst. DECKKRAFT: wie '
            'durchsichtig sie sind. RASTER richtet sie aus.',
            'Der Rest: Optionen > Steuerung. Jakob und Esau: {RT} halten, dann nutzt {LT} Jakobs Karte oder Pille, {RB} Esaus (Tastatur: Strg + Leertaste / Q).',
        ],
    ),
    'dlc3.es': dict(
        titles=['MOVERSE Y DISPARAR', 'TIPOS DE STICK', 'MODOS TÁCTILES', 'DOBLE TOQUE', 'BOTONES',
                'BOTONES ESPECIALES', 'MAPA Y OBJETOS', 'DESCRIPCIONES', 'CONTROLES A TU GUSTO',
                'MANDO Y TECLADO'],
        move_shoot=['MOVERSE', 'DISPARAR'],
        types=['NO SE MUEVE', 'APARECE BAJO TU DEDO', 'SIGUE A TU DEDO'],
        modes=['MÁS EMPUJAS, MÁS RÁPIDO', 'QUIETO O AL MÁXIMO', 'CRUCETAS EN VEZ DE STICKS'],
        dtap=['toca dos veces el mismo lado', 'toca y repite deslizando', 'igual que DINÁMICO',
              'toca una flecha dos veces'],
        buttons=['TOCA: PAUSA', 'MANTÉN: REINICIO', 'TOCA: BOMBA', 'TOCA: USAR', 'TOCA: USAR', 'MANTÉN: TIRAR',
                 'MANTÉN: TIRAR'],
        special=[('GEMELOS:', 'EL OTRO QUIETO'), ('EN LA MIRA:', 'ALTO EL FUEGO'), ('MOCHILA:', 'CAMBIAR'),
                 ('D INFINITO:', 'MANTÉN Y ELIGE')],
        map=[('TOCA', 'deja abierto el mapa grande'), ('MANTÉN', 'se abre mientras mantienes'),
             ('MANTÉN MÁS', 'inventario con descripciones'), ('DESLIZA', 'desplázalo con otro dedo')],
        desc=[['ACÉRCATE A UN OBJETO', 'PARA VER QUÉ HACE'],
              ['LEVANTA LA BOLSA', 'PARA VER RECETAS', 'ARRIBA DEL PANEL:', 'DESLIZA: PASA PÁGINA', 'MANTÉN: SE FIJAN']],
        pad=['mantén: inventario con descripciones', 'recetas y descripciones: avanzar',
             'retroceder; mantén: fijar recetas', 'mantén los cuatro: reiniciar',
             'mantén: inventario con descripciones', 'Bolsa de crafteo: páginas; izq.: fijar', 'pausa', 'mantén: reiniciar'],
        shoot_key='TAB + DISPARO',
        tweaks=['sigue disparando al soltar', 'cuánto empujar para moverte',
                ['cuánto empujar para ir al', 'máximo; solo en ANALÓGICO'], 'cuánto empujar para disparar',
                'ORIGINAL: como en iPhone', 'intensidad: ahora en Opciones'],
        aimlock_release='toca el centro para parar',
        aimlock_note='BLOQUEO DE MIRA, solo táctil: se quita tocando rápido y sin deslizar el centro del stick de disparo o la cruceta; otro toque lo fija.',
        tweaks2_title='MÁS AJUSTES',
        tweaks2=['pone el spawner en la pausa', ['activado: las partidas con', 'spawner desbloquean igual'],
                 'fotogramas por segundo arriba', 'abre la carpeta del port', ['carga partidas de otras', 'versiones del juego'],
                 'las copia a una carpeta'],
        tweaks_notes=[
            'Opciones > AJUSTES.',
            'IMPORTAR PARTIDAS trae partidas de otras versiones del juego; la versión de PC lee las exportadas. El spawner, en las dos páginas siguientes.'],
        mods2_title='MÁS MODS',
        mods=[['objeto de calidad 4 en la', 'sala: todos se ponen a bailar'],
              ['probabilidad de Planetario', 'del próximo piso, bajo las stats'],
              ['en qué oleada vas', 'en el Boss Rush'],
              ['una barra de vida por jefe,', 'con su icono y su estado'],
              ['las marcas que te faltan,', 'tenues en la nota de progreso'],
              ['números extra junto a stats:', 'mult. de daño, tope de lágrimas'],
              ['el cuerpo a cuerpo acierta', 'de cerca aun con mucho rango'],
              ['muestra qué hace un objeto', 'al acercarte (EID)']],
        mods_notes=[
            'Menú principal > MODS: mods de PC rehechos para el port, no hay que instalar nada. Toca una fila para activarla o no; la flecha abre sus opciones.',
            'ARREGLO DE RANGO es para cuerpo a cuerpo: huesos, espadas, hacha y Bolsa de crafteo. El idioma de DESCRIPCIONES se cambia desde su flecha.'],
        user_key='NOMBRE DEL MOD',
        user=[['elige uno o varios .zip;', 'los de PC se adaptan solos'],
              ['copia los archivos del juego', 'para hacer tus propios mods'],
              ['toca: activar o desactivar;', 'flecha: abre su página']],
        page_title='PÁGINA DEL MOD',
        page=['activa o desactiva el mod', ['si dos mods cambian un archivo,', 'gana el de más arriba'],
              'guarda el mod como .zip', 'toca dos veces para borrarlo',
              'las opciones propias del mod', 'reinicia el juego con los cambios'],
        user_notes=[
            'Los mods del Workshop de Steam sirven si cambian imágenes, sonidos o datos; Lua no se ejecuta. Si añades uno que ya tienes, actualízalo o ten los dos.',
            'Nombre en rojo: el port no usa todo el mod. Las líneas rojas sobre el título de su página dicen por qué o con qué mod choca; tócalas para más detalles.'],
        coop_title='COOP LOCAL',
        coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
        coop=['únete: 1,5 s en un mando libre', 'lo mismo con el teclado', 'elige personaje',
              'confirmar / cancelar', 'confirmar / cancelar (teclado)'],
        coop_note='Al empezar la partida, mantén {VIEW} y {MENU} en un mando libre durante segundo y medio; '
                  'luego elige personaje con las flechas. Pueden jugar hasta cuatro.',
        spawn_title='USO DEL SPAWNER',
        spawn_tabs=['elige; otro toque: tuyo', 'elige; otro toque: sale', 'sala: un toque; piso: dos',
                    'escribe o usa las flechas'],
        spawn_rows=['en coop: elige el jugador', 'ajustar; stats: toca la fila o {A}', 'tócalo otra vez para escribir',
                    'deshace tus cambios', 'cambiar de pestaña', 'cambiar de pestaña (teclado)',
                    'confirmar / cerrar'],
        spawn_notes=[
            'Actívala en AJUSTES > CONSOLA; en partida, ábrelo desde su pestaña en la pausa. Sin TROFEOS DE CONSOLA, no desbloqueas nada.',
            'OBJETOS lo tiene todo, también recogibles y botín. AL SUELO lo deja a tu lado; flechas: '
            'cuántos. Busca por nombre o número. Las stats duran la partida.'],
        notes=[
            'El stick izquierdo mueve y el derecho dispara. En Opciones eliges TIPO, TAMAÑO y TOUCH MODE; las dos páginas siguientes los explican.',
            'Opciones > TIPO. ESTÁTICO se queda donde lo pusiste, DINÁMICO aparece donde apoyas el dedo y FLOTANTE sigue a tu dedo mientras lo mueves.',
            'Opciones > TOUCH MODE. ANALÓGICO: cuanto más empujas, más rápido. DIGITAL: quieto, o al máximo pasada la ZONA MUERTA. BOTONES: crucetas.',
            'Con doble toque hacia un lado, Mars embiste y Hemoptisis estornuda. Va igual en ambos sticks; el segundo toque, antes de medio segundo.',
            'Mantén la pausa para reiniciar; suéltala antes de que la pantalla se oscurezca para cancelar. La baratija es otro botón: mantenla para tirarla, sin píldora ni carta.',
            'Botón redondo: deja quieto al otro (Jacob y Esaú, Hombre de trapo) o cambia de cuerpo con El Olvidado. Mochila: toca y cambia. D infinito: mantén y elige dado.',
            'Toca el minimapa para dejar abierto el mapa grande. Con DESCRIPCIONES activo, mantenlo más para ver tu inventario descrito; desplázalo con otro dedo.',
            'Las descripciones largas se muestran por páginas: desliza por arriba del panel o pulsa '
            '{LS} / {RS} en el mando. Ajustes: MODS > DESCRIPCIONES.',
            'Opciones > Controles > Touch: arrastra sticks y botones, pausa incluida, adonde quieras. OPACIDAD: cuánto se ven. REJILLA: los alinea.',
            'Lo demás, en Opciones > Controles. Jacob y Esaú: mantén {RT}; {LT} usa la carta o píldora de Jacob, {RB} la de Esaú (teclado: Ctrl + Espacio / Q).',
        ],
    ),
    'dlc3.jp': dict(
        titles=[
            'いどうとこうげき',
            'スティックのタイプ',
            'タッチモード',
            'ダブルタップ',
            'ボタン',
            'とくしゅなボタン',
            'マップとアイテム',
            'アイテムのせつめい',
            'ボタンのはいち',
            'ゲームパッドとキーボード',
        ],
        move_shoot=['移動', '攻撃'],
        types=['置いた場所に固定', '指を置いた所に出る', '指についてくる'],
        modes=['倒すほど速い', '止まるか全速', '十字キー'],
        dtap=['同じ側を2回タップ', '1回タップ、2回目は向きたい方へはじく', 'ついじゅうと同じ', '矢印を2回タップ'],
        buttons=['タップ：ポーズ', '長押し：リスタート', 'タップ：ボム', 'タップ：使う', 'タップ：使う',
                 '長押し：捨てる', '長押し：捨てる'],
        special=[('相方：', 'その場に止める'), ('照準マーク：', '射撃を止める'), ('2つ目：', '入れ替え'),
                 ('Ｄインフィニティー：', '長押しで選ぶ')],
        map=[
            ('タップ', '大きいマップを開いたままに'),
            ('長押し', '押している間だけ表示'),
            ('さらに長押し', '説明つきの持ち物一覧'),
            ('スワイプ', '別の指でスクロール'),
        ],
        desc=[['アイテムに近づくと', '効果がわかる'],
              ['バッグを掲げると', 'レシピが見られる', '上部をスワイプ：ページ送り', '上部を長押しで固定']],
        pad=[
            '長押し：持ち物一覧（アイテムのせつめいオン時）',
            'レシピ・説明：次のページ',
            '前のページ、長押し：レシピを固定',
            '4つ同時に長押し：リスタート',
            '長押し：持ち物一覧（アイテムのせつめいオン時）',
            'バッグ オブ クラフティング：めくる、左で固定',
            'ポーズ',
            '長押し：リスタート',
        ],
        shoot_key='TAB + 攻撃キー',
        tweaks=[
            '指を離しても撃ち続ける',
            '移動スティックをどこまで倒すと歩き出すか',
            'どこまで倒すと全速か（アナログのみ）',
            '攻撃スティックをどこまで倒すと撃つか',
            'オリジナルはiPhoneと同じ見た目', '振動の強さ（オプションへ移動）',
        ],
        aimlock_release='中央をタップで解除',
        aimlock_note='エイムロックはタッチ操作専用。止めるには攻撃スティックか十字キーの中央を、すべらせずに軽くタップ。'
                     '次に触れるとまたロックされる。',
        tweaks2_title='そのほかのせってい',
        tweaks2=[
            'ポーズ画面にスポーンを出す',
            ['オン：スポーンを使ったランでも', '実績が解除される'],
            '画面の上にFPSを表示',
            ['設定・MOD・セーブが入った', 'フォルダーを開く'],
            'ほかのバージョンのセーブを読み込む',
            'セーブをフォルダーにコピー',
        ],
        tweaks_notes=[
            'オプション＞こまかいせってい。',
            'ほかのバージョンのセーブを取り込める。書き出したセーブはPC版でも使える。スポーンについては次の2ページで。',
        ],
        mods2_title='MODのつづき',
        mods=[
            ['クオリティ4のアイテムがある部屋で', 'みんなが音楽に合わせて踊る'],
            ['次のフロアのプラネタリウム確率を', 'ステータスの下に表示'],
            'ボスラッシュで今何ウェーブ目か表示',
            ['ボスごとに体力バーを表示', '（アイコンと状態つき）'],
            ['まだ取っていないクリアマークが', 'メモにうすく表示される'],
            ['ステータスの横に追加の数値：', 'ダメージ倍率、連射の上限'],
            ['射程が長くても、近接攻撃が', 'すぐそばの敵に当たる'],
            'アイテムに近づくと効果を表示（EID）',
        ],
        mods_notes=[
            'メインメニュー＞MOD。PCのMODをこの移植版用に作り直したもので、インストールは不要。行をタップでオン/オフ、'
            '矢印がある行は矢印から設定を開ける。',
            'しゃていのしゅうせいは近接攻撃用：骨、剣、斧、バッグ オブ クラフティング。アイテムのせつめいは、'
            '矢印の先の設定で別の言語にもできる。',
        ],
        user_key='MOD名',
        user=[['MODの.zipを選ぶ（複数可）', 'PCのMODは移植版用に変換'],
              ['ゲームのファイルを書き出す', '自作MODのたたき台に'],
              ['タップ：オン/オフ', '矢印：MODのページ']],
        page_title='MODのページ',
        page=[
            'MODのオン/オフ',
            ['2つのMODが同じファイルを変えたら', '上にあるほうが優先'],
            'MODを.zipで保存',
            '2回タップで削除',
            'MODごとの設定',
            'ゲームを再起動して変更を反映',
        ],
        user_notes=[
            'SteamワークショップのMODも、画像・音・データを変えるものなら動く。Luaスクリプトは動かない。'
            '入っているMODをもう一度追加すると、更新するか両方残すか選べる。',
            '名前が赤いMODは、一部が使われていない。そのMODのページでタイトルの上の赤い行が理由や競合するMODを示す。'
            '行をタップすると詳細が見られる。',
        ],
        coop_title='きょうりょくプレイ',
        coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
        coop=['空いているパッドで1.5秒長押し：参加', 'キーボードでも同じ', 'キャラクターを選ぶ', '決定 / キャンセル',
              'キーボードで決定 / キャンセル'],
        coop_note='ランの開始時に、空いているパッドで{VIEW}と{MENU}を1.5秒長押しし、矢印でキャラクターを選ぶ。'
                  '最大4人まで遊べる。',
        spawn_title='スポーンのそうさ',
        spawn_tabs=['タップで選び、もう一度で入手', 'タップで選び、もう一度で出現', '部屋は1回、フロアは2回タップ',
                    '数値を入力か矢印で増減'],
        spawn_rows=[
            '協力プレイ：変更するプレイヤー',
            '個数・値を設定、能力値は行か{A}で決定',
            'もう一度タップで数値を入力',
            '変更を元に戻す',
            'タブを切り替え',
            'タブを切り替え（キーボード）',
            '決定 / 閉じる',
        ],
        spawn_notes=[
            'こまかいせってい＞コンソールをオンにし、ラン中はポーズ画面のタブから開く。'
            'コンソールのトロフィーがオフだと、使ったランでは何も解除されない。',
            'アイテムタブに全部ある（ピックアップ、ピル、トリンケット、おたからも）。ゆかに置くをオンにすると'
            '横に出る。矢印で個数。名前か番号で検索。ステータスはランが終わるまで続く。',
        ],
        notes=[
            '左スティックで移動、右スティックで攻撃。スティックのタイプ・サイズ・タッチモードはオプションで選べる。'
            '詳しくは次のページから。',
            'オプション＞タイプ。こていは置いた場所から動かない。ついじゅうは指を置いた所に出る。'
            'ふゆうは指の動きについてくる。',
            'オプション＞タッチモード。アナログは倒すほど速く歩く。デジタルはデッドゾーンを越えると全速、'
            '手前では止まったまま。ボタンはスティックの代わりに十字キー。',
            'マルスのダッシュやヘモプタスィスのくしゃみは、同じ方向へのダブルタップで出る。どちらのスティックでも同じ。'
            '2回目は0.5秒以内に。',
            'ポーズを長押しでランをリスタート。画面が暗くなる前に離せば中止。トリンケットもボタンになっていて、'
            '長押しで捨てられる。ピルやカードは要らない。',
            '丸いボタンはもう一方の体をその場に止める（ヤコブとエサウ、ストローマン）。ザ フォーガットンは体を入れ替える。'
            'スクール バッグ：タップで切り替え。Ｄインフィニティー：長押しでダイス選択。',
            'ミニマップをタップすると大きいマップが開いたままになる。アイテムのせつめいがオンなら、'
            'さらに長押しで説明つきの持ち物一覧。別の指でスクロールできる。',
            '長い説明はページに分かれる。パネルの上部をスワイプするか、パッドの{LS}/{RS}で切り替える。上部を長押しするとレシピが固定される。設定はMOD＞アイテムのせつめい。',
            'オプション＞コントロール＞Touch。スティックやボタン（ポーズも）を好きな所へドラッグ。'
            '不透明度で透け具合、グリッドで位置をそろえる。',
            'それ以外はオプション＞コントロールで。ヤコブとエサウ：{RT}を押したまま{LT}でヤコブ、{RB}でエサウのカードやピルを使う（キーボードはCTRL＋SPACE／Q）。',
        ],
    ),
    'dlc3.kr': dict(
        titles=['이동과 공격', '스틱 종류', '터치 모드', '더블 탭', '버튼', '특수 버튼',
                '지도와 아이템', '아이템 설명', '나만의 배치', '게임패드와 키보드'],
        move_shoot=['이동', '공격'],
        types=['놓은 자리 그대로', '닿는 곳에 나타남', '손가락을 따라감'],
        modes=['밀수록 빨라짐', '멈추거나 최고 속도', '스틱 대신 십자키'],
        dtap=['같은 쪽을 두 번 탭', '탭한 뒤, 다시 탭하며 원하는 방향으로 밀기', '동적과 같음',
              '같은 방향키를 두 번 탭'],
        buttons=['탭: 일시정지', '길게: 재시작', '탭: 폭탄', '탭: 사용', '탭: 사용', '길게: 버리기', '길게: 버리기'],
        special=[('쌍둥이:', '멈춰 세우기'), ('조준:', '공격 멈추기'), ('학교 가방:', '교체'),
                 ('D 인피니티:', '길게: 주사위 선택')],
        map=[('탭', '큰 지도를 계속 띄움'), ('길게', '누르는 동안만 보임'),
             ('더 길게', '가진 아이템과 설명 보기'), ('스와이프', '다른 손가락으로 스크롤')],
        desc=[['아이템에 다가가면', '효과가 나타남'],
              ['제작 가방을 들면', '레시피가 보임', '위쪽 스와이프: 페이지 넘기기', '위쪽을 길게 누르면', '목록 고정']],
        pad=['길게: 아이템과 설명 (아이템 설명 켬)', '레시피·설명: 다음 페이지', '이전 페이지, 길게: 레시피 고정',
             '네 개 모두 길게: 재시작', '길게: 아이템과 설명 (아이템 설명 켬)', '제작 가방: 페이지 넘기기, 왼쪽: 고정', '일시정지', '길게: 재시작'],
        shoot_key='TAB + 공격',
        tweaks=['손을 떼도 그 방향으로 계속 공격', '이동 스틱을 얼마나 밀어야 걷기 시작하는지',
                '얼마나 밀어야 최고 속도인지 (아날로그만)', '공격 스틱을 얼마나 밀어야 공격하는지',
                '원본은 아이폰과 같은 화면', '진동 세기, 이제 설정에 있음'],
        aimlock_release='가운데를 탭하면 멈춤',
        aimlock_note='조준 고정은 터치 전용이에요. 멈추려면 공격 스틱이나 십자키 가운데를 밀지 말고 '
                     '짧게 탭하세요. 다음 터치에서 다시 고정돼요.',
        tweaks2_title='그 밖의 설정',
        tweaks2=['일시정지 화면에 스폰 메뉴 추가', '켜면 스폰을 쓴 판도 해금 가능',
                 '화면 위에 초당 프레임 표시', '파일 관리자로 포트 폴더 열기', '다른 버전 게임의 세이브 가져오기',
                 '세이브를 폴더에 복사'],
        tweaks_notes=[
            '설정 > 세부 설정.',
            '세이브 가져오기는 다른 버전 게임의 세이브를 불러와요. 세이브 내보내기로 만든 파일은 '
            'PC판에서도 읽을 수 있어요. 스폰은 다음 두 페이지에서 설명해요.'],
        mods2_title='그 밖의 모드',
        mods=[['방에 4등급 아이템이 있으면', '다 같이 음악에 맞춰 춤'],
              ['다음 층의 천체관 확률을', '능력치 아래에 표시'],
              '보스 러시에서 지금 몇 번째 웨이브인지 표시',
              ['보스마다 체력바 표시,', '아이콘과 상태도 함께'],
              ['아직 못 딴 완료 마크가', '완료 노트에 흐리게 보임'],
              ['능력치 옆에 추가 수치:', '공격력 배율, 연사 상한'],
              ['사거리가 길어도 근접 공격이', '바로 앞의 적에게 맞음'],
              '아이템에 다가가면 효과 표시 (EID)'],
        mods_notes=[
            '메인 메뉴 > 모드. PC 모드를 이 포트에 맞게 다시 만든 것이라 설치할 필요가 없어요. 줄을 '
            '탭하면 켜고 꺼지며, 화살표가 있으면 그걸로 설정을 열어요.',
            '사거리 수정은 근접 공격용이에요: 뼈, 검, 도끼, 제작 가방. 아이템 설명은 다른 언어로도 '
            '볼 수 있어요. 화살표를 탭한 뒤 설정에서 고르세요.'],
        user_key='모드 이름',
        user=[['모드 .zip 고르기(여러 개 가능),', 'PC 모드는 포트용으로 변환'],
              ['게임 파일을 밖으로 복사,', '내 모드를 만들 때 바탕으로'],
              ['탭: 켜기/끄기,', '화살표: 모드 페이지']],
        page_title='모드 페이지',
        page=['모드 켜기/끄기', ['두 모드가 같은 파일을 바꾸면', '목록에서 위에 있는 쪽이 우선'],
              '모드를 .zip으로 저장', '두 번 탭하면 삭제',
              '모드 자체 설정', '게임을 다시 시작해 변경 적용'],
        user_notes=[
            'Steam 창작마당 모드도 그림, 소리, 데이터만 바꾸는 거라면 작동해요. Lua 스크립트는 '
            '실행되지 않아요. 이미 있는 모드를 다시 넣으면 업데이트하거나 둘 다 둘 수 있어요.',
            '이름이 빨간 모드는 포트가 일부를 쓰지 못한다는 뜻이에요. 모드 페이지 제목 위 빨간 줄에 '
            '이유나 충돌하는 모드가 나와요. 줄을 탭하면 자세히 볼 수 있어요.'],
        coop_title='로컬 협동',
        coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
        coop=['빈 패드에서 1.5초 길게: 참가', '키보드도 마찬가지', '캐릭터 고르기',
              '확인 / 취소', '키보드로 확인 / 취소'],
        coop_note='판이 시작될 때 빈 패드에서 {VIEW} 버튼과 {MENU} 버튼을 1.5초 누른 뒤, 좌우 화살표로 캐릭터를 '
                  '고르세요. 최대 4명까지 함께할 수 있어요.',
        spawn_title='스폰 조작',
        spawn_tabs=['탭: 선택, 다시 탭: 받기', '탭: 선택, 다시 탭: 소환', '방은 한 번, 층은 두 번 탭',
                    '값 입력 또는 화살표로 조절'],
        spawn_rows=['협동: 바꿀 플레이어 선택', '개수·값 설정, 능력치는 줄이나 {A}로 적용', '다시 탭하면 숫자 입력', '바꾼 값 되돌리기',
                    '탭 전환', '탭 전환 (키보드)', '확인 / 닫기'],
        spawn_notes=[
            '세부 설정 > 콘솔을 켜고, 판 도중 일시정지 화면의 콘솔 탭을 누르세요. 콘솔 '
            '트로피가 꺼져 있으면 콘솔을 쓴 판은 아무것도 해금되지 않아요.',
            '아이템 탭에 모든 것이 있어요(픽업, 알약, 장신구, 전리품까지). 바닥에를 켜면 옆에 놓여요. '
            '화살표로 개수. 이름이나 번호로 검색. 바꾼 능력치는 판이 끝날 때까지 유지돼요.'],
        notes=[
            '왼쪽 스틱으로 이동하고 오른쪽 스틱으로 공격해요. 스틱 종류, 크기, 터치 모드는 설정에서 '
            '바꿀 수 있고, 다음 두 페이지에서 설명해요.',
            '설정 > 종류. 고정은 배치한 자리에 그대로 있고, 동적은 손가락이 닿은 곳에 나타나고, '
            '유동은 손가락을 따라다녀요.',
            '설정 > TOUCH MODE. 아날로그는 멀리 밀수록 빨라요. 디지털은 멈춰 있다가 데드존을 넘기면 '
            '바로 최고 속도예요. 버튼은 스틱 대신 십자키를 써요.',
            '화성의 대시와 각혈의 재채기는 한 방향으로 더블 탭하면 나가요. 두 스틱 모두 똑같고, '
            '두 번째 탭은 0.5초 안에 해야 해요.',
            '일시정지를 길게 누르면 판을 재시작해요. 화면이 어두워지기 전에 손을 떼면 취소돼요. '
            '장신구도 버튼이라 길게 누르면 버려요. 알약이나 카드가 없어도 돼요.',
            '일시정지 옆 둥근 버튼은 다른 몸을 멈춰 세워요(야곱과 에사우, 밀짚 인형). 더 포가튼은 '
            '몸을 바꿔요. 학교 가방은 탭해서 교체, D 인피니티는 길게 눌러 주사위를 골라요.',
            '미니맵을 탭하면 큰 지도가 계속 열려 있어요. 아이템 설명이 켜져 있으면 더 길게 눌러 가진 '
            '아이템과 설명을 보고, 다른 손가락으로 스크롤하세요.',
            '긴 설명은 페이지로 나뉘어요. 패널 위쪽을 스와이프하거나 게임패드의 {LS} / {RS}를 누르세요. 위쪽을 길게 누르면 레시피가 고정돼요. 설정은 모드 > '
            '아이템 설명에서 해요.',
            '설정 > 조작 > Touch. 스틱과 버튼, 일시정지 버튼까지 원하는 곳으로 끌어 옮기세요. '
            '불투명도로 비치는 정도를, 격자로 칸 맞춤을 정해요.',
            '나머지는 설정 > 조작에 있어요. 야곱과 에사우: {RT}를 누른 채 {LT}는 야곱, {RB}는 에사우의 카드나 알약을 써요(키보드: Ctrl + Space / Q).',
        ],
    ),
    'dlc3.zh': dict(
        titles=['移动与射击', '摇杆类型', '触摸模式', '双击', '按键', '特殊按键', '地图与道具', '道具说明',
                '自定义布局', '手柄与键盘'],
        move_shoot=['移动', '射击'],
        types=['放在哪就在哪', '手指按哪就在哪', '跟着手指走'],
        modes=['推得越远越快', '不动或全速', '方向键代替摇杆'],
        dtap=['同一侧连点两下', '点一下，再按下并朝想去的方向滑', '和跟随一样', '同一个方向键点两下'],
        buttons=['点击：暂停', '长按：重开', '点击：炸弹', '点击：使用', '点击：使用', '长按：丢弃', '长按：丢弃'],
        special=[('双角色：', '定住另一个'), ('十字准星：', '停止射击'), ('书包：', '切换道具'),
                 ('无限面骰：', '长按选骰子')],
        map=[('点击', '保持显示大地图'), ('长按', '按住时显示，松手收起'),
             ('长按更久', '道具列表及说明（需开启道具说明）'), ('滑动', '用第二根手指滚动列表')],
        desc=[['走近道具', '就能看到它的作用'],
              ['举起合成宝袋', '查看合成配方', '上半部滑动：翻页', '手指按住上半部：锁定配方']],
        pad=['按住：道具列表及说明（需开启道具说明）', '配方和说明：下一页', '上一页；长按：锁定配方',
             '四键同时按住：重开', '按住：道具列表及说明（需开启道具说明）', '合成宝袋：上下翻页，向左锁定', '暂停', '按住：重开'],
        shoot_key='TAB + 射击',
        tweaks=['松手后继续朝原方向射击', '移动摇杆推多远才开始走', '推到多远算全速，仅对模拟有效',
                '射击摇杆推多远才开火', '原始：和iPhone上一样', '振动强度，已移到设置'],
        aimlock_release='快速轻点中心即可停止',
        aimlock_note='瞄准锁定只用于触屏：想停下时，快速轻点射击摇杆或方向键的中心，别滑动；下次触摸会重新锁定',
        tweaks2_title='更多选项（续）',
        tweaks2=['在暂停画面添加生成器', '开启后，用过生成器的局也能解锁', '在屏幕顶部显示帧率',
                 '用文件管理器打开移植版文件夹', '读取其他版本游戏的存档', '把存档复制到一个文件夹'],
        tweaks_notes=[
            '设置 > 更多选项；',
            '导入存档可读取其他版本游戏的存档，导出的存档电脑版也能读；生成器的用法见后两页'],
        mods2_title='更多模组',
        mods=[['房间里有4级道具时，', '全员跟着音乐跳舞'],
              ['下一层出现星象房的概率，', '显示在属性栏下方'],
              '显示头目车轮战当前第几波',
              ['每个头目单独一条血条，', '附带图标和状态'],
              ['还没拿到的通关标记，', '会在进度纸上淡淡显示'],
              ['在属性旁多显示几个数值：', '伤害倍率，射速上限'],
              ['射程再高，近战也能', '打中身边的敌人'],
              '走近道具时显示它的作用（EID）'],
        mods_notes=[
            '主菜单 > 模组；这些是为移植版重做的电脑版模组，无需安装；点击一行即可开关模组，带箭头的行点箭头进入设置',
            '射程修复针对近战：骨头，剑，斧头和合成宝袋；道具说明可以换成其他语言：点它的箭头，再进入选项'],
        user_key='模组名称',
        user=[['选择模组的.zip文件（可多选），', '电脑版模组会自动转换'],
              ['把游戏文件导出来，', '作为自制模组的基础'],
              '点击：开/关；箭头：模组页面'],
        page_title='模组页面',
        page=['开启或关闭这个模组', '两个模组改同一个文件时，靠上的生效',
              '把模组存成.zip文件', '点两次删除，第一次会问“确定删除?”',
              '模组自己的设置', '重启游戏，让更改生效'],
        user_notes=[
            'Steam创意工坊的模组，只要改的是图片，声音或数据就能用；Lua脚本不会运行；添加已有的模组时，可以更新，也可以与旧版并存',
            '模组名称变红，说明移植版没有用上它的全部内容；模组页面标题上方的红字会说明原因，或指出与哪个模组冲突；点击可查看详情'],
        coop_title='本地合作',
        coop_keys=['{VIEW} + {MENU}', 'ESC + TAB', '{LEFT} / {RIGHT}', '{A} / {B}', 'ENTER / BACKSPACE'],
        coop=['用空闲手柄按住1.5秒加入', '键盘同上', '选择角色', '确认 / 取消', '键盘上确认 / 取消'],
        coop_note='开局时，在空闲的手柄上同时按住{VIEW}和{MENU}一秒半，再用左右箭头选角色；最多四人同玩',
        spawn_title='生成器操作',
        spawn_tabs=['点一下选中，再点一下获得', '点一下选中，再点一下生成', '房间点一次，楼层点两次',
                    '输入数值，或用箭头增减'],
        spawn_rows=['合作时选择要改哪位玩家', '设定数量/数值；属性点该行或按{A}应用', '再点一下可直接输入数值', '撤销所有修改',
                    '切换标签页', '键盘上切换标签页', '确认 / 关闭'],
        spawn_notes=[
            '先在 更多选项 > 控制台 中开启，然后在游戏中点暂停画面的“控制台”标签；除非开启控制台奖杯，否则用过它的局不会解锁任何内容',
            '道具页包含一切：拾取物，胶囊，饰品，战利品；开启“放在地上”会放在你身边，箭头调数量；可按名称或编号搜索；'
            '改过的属性保持到本局结束'],
        notes=[
            '左摇杆移动，右摇杆射击；摇杆的类型和大小以及触摸模式都在设置里调整，后两页详细说明',
            '设置 > 类型；固定：摇杆待在你放的位置；跟随：手指按在哪，摇杆就出现在哪；浮动：摇杆跟着手指移动',
            '设置 > TOUCH MODE；模拟：推得越远走得越快；数字：推过摇杆死区就全速，否则不动；按键：用方向键代替摇杆',
            '朝同一方向双击，火星会冲刺，咯血症会打喷嚏；两个摇杆都可以，第二下要在半秒内点出',
            '长按暂停键重开本局，屏幕变黑前松手即可取消；饰品也是按键：长按即可丢弃，身上没有胶囊或卡牌也行',
            '暂停键旁的圆形按键（雅各和以扫，稻草人等）可定住另一个角色；遗骸则用它切换身体；书包：点击切换；无限面骰：长按选择骰子',
            '点击小地图让大地图保持打开；开启道具说明后，长按更久会列出所有道具及说明，用第二根手指滑动浏览',
            '较长的描述会分页显示：在面板上半部滑动，或按手柄的{LS}/{RS}翻页；手指按住上半部即可锁定配方；相关选项见 模组 > 道具说明',
            '设置 > 控制设置 > Touch：把摇杆和按键（包括暂停键）拖到任意位置；不透明度调整按键深浅，开启网格可自动对齐',
            '其余见 设置 > 控制设置；雅各和以扫：按住{RT}，再按{LT}用雅各的卡牌或胶囊，按{RB}用以扫的（键盘：Ctrl + 空格 / Q）',
        ],
    ),
}
