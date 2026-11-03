# -*- coding: utf-8 -*-
"""The guide's words in English (set 'data'), rewritten in plain words (stage 1,
06.10.2026: the owner found the slides unclear). Stage 3 merges it into
tools/guide_text.py. What each slot means: tools/guide_brief.md. Check:
    py -3 tools/guide_check.py en tools/guide_lang/en.py
"""

TEXT = dict(
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
            'sharp picture or smooth game', 'strength: now in Options'],
    aimlock_release='tap the centre to stop',
    aimlock_note='AIM LOCK is for touch: to stop it, tap the fire stick or D-pad in the centre and let '
                 'go without sliding. The next touch locks again.',
    tweaks2_title='MORE TWEAKS',
    tweaks2=['spawner on the pause screen', ['ON: spawner runs still', 'count for unlocks'],
             "opens the port's folder", ['load saves from other', 'versions of the game'],
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
        'Turn on TWEAKS > ITEM SPAWN; in a run, tap the ITEM SPAWN tab on the pause screen. Runs that '
        'use it unlock nothing unless SPAWNER TROPHIES is on.',
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
)
