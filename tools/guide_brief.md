# Guide slides: brief for the native writers (stage 1 of 3)

The owner read the in-game GUIDE and found every language except English reading
like a machine translation, in places plain nonsense ("АНАЛОГ: где полный ход",
"ход до начала ходьбы"). Stage 2: one native writer per language rewrites the
slides **as a player of that language would say it**, not as a translation of
the English. Stage 3 merges the files into `tools/guide_text.py` and generates.

## What you get, what you hand back

| file | what |
|---|---|
| `tools/guide_brief.md` | this: what each slot means, where it sits, fixed terms, room |
| `tools/guide_lang/en.py` | the new English (plain-words rewrite): the meaning reference |
| `tools/guide_lang/<lang>.current.py` | your language as it stands now: read it, do not copy it |
| `tools/guide_lang/<lang>.py` | **your output**: `TEXT = dict(...)`, the same slots and shape as `en.py` |
| `tools/guide_check.py` | the fit checker, read-only, safe to run in parallel |

Languages and set keys in `guide_text.TEXT`: `en` = `'data'`, `ru` = `'dlc3.ru'`,
`de` = `'dlc3.de'`, `es` = `'dlc3.es'`, `jp` = `'dlc3.jp'`, `kr` = `'dlc3.kr'`,
`zh` = `'dlc3.zh'`.

### The checker

```
py -3 tools/guide_check.py ru tools/guide_lang/ru.py                      # FAIL / WARN per slot
py -3 tools/guide_check.py ru tools/guide_lang/ru.py --budget             # every line: used / room / lines
py -3 tools/guide_check.py ru tools/guide_lang/ru.py --png C:/scratch/ru.png   # all 19 slides, 2 px a unit
py -3 tools/guide_check.py ru --current                                   # the words guide_text.py has now
```

It lays every slide out with the generator's own code (`guide_slides.py`) and
reports: a letter the font lacks, a line wider than its room, too many lines,
and any slide the generator would refuse (overlap, off the paper, a note over
three lines, a table taller than the paper). Exit 0 means it fits. It writes
nothing but the `--png` you name (a slide that does not build is a red box),
and no bytecode, so six writers can run it at once. Work in a scratch folder
for previews, never in `port/app/src/main/assets` or `other stuff/`.

### File format

`tools/guide_lang/<lang>.py` is a Python module with `TEXT = dict(...)`, the
same keys as `en.py`:

- a **string** slot: one string;
- a **list** slot: exactly as many strings as in `en.py`;
- `special`, `map`: lists of 2-string tuples;
- `desc`: two lists of strings (column one, column two; one string a line);
- **table entries** (`tweaks`, `tweaks2`, `mods`, `user`, `page`): each is a
  string *or* a list of two strings (two lines, broken where you choose). A
  string too wide is broken by the tool at the room's width; the checker WARNs
  so you can break it yourself where it reads well. `tweaks[0]` must be a
  string: its second line is `aimlock_release`.
- `buttons`: each is `GESTURE: WHAT IT DOES`; the colon (`:` or `：`) splits it
  into two lines under the picture.

Japanese and Chinese have no spaces: lines break between any two characters
(not before `、。，！？）：；ー` and small kana). Japanese and Chinese join
`tweaks_notes[0]` and `aimlock_note` with nothing between them; the others with
a space.

Pad buttons are drawn, not named (stage 3): write `{VIEW}` `{MENU}` `{A}` `{B}`
`{LEFT}` `{RIGHT}` `{LB}` `{RB}` `{LT}` `{RT}` `{LS}` `{RS}` in a key or a note and the
generator puts the game's own button picture there (`guide_slides.PAD_ICON_CELLS`).

## Style rules (all languages)

1. **Say it as a player says it.** Read each line aloud: if a gamer of your
   language would not say it to a friend, rewrite it. No word-for-word calques
   of the English, no invented technical nouns ("ход" for stick travel),
   no bureaucratic phrasing.
2. **Labels are short.** Under a picture or beside a menu row: an imperative
   ("tap twice to remove it") or a short noun phrase ("vibration strength,
   0 = off"). No full stops on labels.
3. **Notes are 2-3 plain sentences**, at most three lines on the yellow note.
   Say what to do and what happens. Do not repeat what the paper above already
   shows; the note adds where it is and what is not obvious.
4. **No platform lists where "other versions of the game" is meant.** Save
   import takes saves from other versions of the game (PC, iOS, Repentance+,
   Afterbirth+ and more); do not list them on the slide. Exported saves can be
   read by the PC game: that much may be said.
5. **Fixed terms are exact.** A word the player sees in a menu (Options rows,
   Tweaks rows, MODS rows, the mod-page rows, the spawner card's tabs, item and
   character names) must be written **exactly** as in the tables below,
   including case and hyphens, so the player can find it. Your own words around
   them are free. Refer to a menu row in capitals where the menu letters it in
   capitals (Latin and Cyrillic).
6. **Same register everywhere.** Pick one form of address (ru: "ты" or
   imperative without pronoun; de: "du"; es: "tú"; jp: plain/da-dearu or
   polite, but one; kr: one speech level) and keep it on all 19 slides.
7. **Capitals:** in en/ru/de/es the slots marked CAPS below are written in
   capitals (the checker WARNs on lowercase). The small font draws lowercase
   as small capitals anyway, so a note written in normal case looks right.
8. **Keep it true.** Change the words, not the meaning. Where the meaning
   below says "not verified", do not add detail.

## Fonts and room

Rooms are in **units** (the slide is 480 units wide; the paper's usable band
is 36..354). One unit = one pixel of the font.

| set | labels, notes (small) | names under sticks (label) | titles |
|---|---|---|---|
| en, ru, de, es | `teammeatfont10` (~5.8 units a lowercase letter, ~7.4 a capital) | `teammeatfont16bold` | `teammeatfont20bold` (~16 a letter) |
| jp | `mplus_10r` (~9.8 a character) | `mplus_12b` | our kana lettering (`MochiyPopOne`, ~17.6 a character): **kana only, no kanji in titles** |
| kr | `kr_font12` (~8.4 a syllable) | `kr_font14` | our lettering (`GamjaFlower`, ~15.5) |
| zh | `teammeatfontextended10` (~11.5 a character) | `teammeatfontextended16bold` | `teammeatfontextended16bold` lettering (~18.5) |

So a note line holds about 54 Latin, 50 Cyrillic, 31 Japanese, 37 Korean or
26 Chinese characters; a note (3 lines) about 150 / 140 / 90 / 105 / 75.

The room per slot and language (units x lines). A list slot's room is the
narrowest of its entries; legend and table rooms depend on the width of the
fixed keys in your language, which is why they differ:

| slot | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| every title | 360 x1 | 360 x1 | 360 x1 | 360 x1 | 360 x1 | 360 x1 | 360 x1 |
| every note | 310 x3 | 310 x3 | 310 x3 | 310 x3 | 310 x3 | 310 x3 | 310 x3 |
| move_shoot | 150 x1 | 150 x1 | 150 x1 | 150 x1 | 150 x1 | 150 x1 | 150 x1 |
| types | 95 x2 | 95 x2 | 95 x2 | 95 x2 | 95 x2 | 95 x2 | 95 x2 |
| modes | 95 x2 | 95 x2 | 95 x2 | 95 x2 | 95 x2 | 95 x2 | 95 x2 |
| dtap | 190 x1 | 160 x1 | 179 x1 | 189 x1 | 206 x1 | 233 x1 | 230 x1 |
| buttons (one column) | 56 x4 | 56 x4 | 56 x4 | 56 x4 | 56 x4 | 56 x4 | 56 x4 |
| special | 72 x4 | 72 x4 | 72 x4 | 72 x4 | 72 x4 | 72 x4 | 72 x4 |
| map | 160 x1 | 190 x1 | 196 x1 | 175 x1 | 196 x1 | 211 x1 | 207 x1 |
| desc (a column) | 150 x5 | 150 x5 | 150 x5 | 150 x5 | 150 x5 | 150 x5 | 150 x5 |
| pad | 228 x1 | 216 x1 | 206 x1 | 220 x1 | 258 x1 | 252 x1 | 244 x1 |
| coop | 192 x1 | 192 x1 | 192 x1 | 192 x1 | 228 x1 | 196 x1 | 192 x1 |
| tweaks, aimlock_release | 186 x2 | 187 x2 | 167 x2 | 178 x2 | 237 x2 | 260 x2 | 246 x2 |
| tweaks2 | 177 x2 | 185 x2 | 178 x2 | 178 x2 | 207 x2 | 227 x2 | 247 x2 |
| spawn_tabs | 149 x1 | 148 x1 | 149 x1 | 142 x1 | 141 x1 | 158 x1 | 191 x1 |
| spawn_rows | 237 x1 | 237 x1 | 213 x1 | 222 x1 | 238 x1 | 242 x1 | 237 x1 |
| mods | 176 x2 | 182 x2 | 153 x2 | 178 x2 | 188 x2 | 216 x2 | 224 x2 |
| user | 178 x2 | 186 x2 | 178 x2 | 189 x2 | 188 x2 | 214 x2 | 235 x2 |
| page | 187 x2 | 178 x2 | 189 x2 | 203 x2 | 198 x2 | 228 x2 | 246 x2 |

Notes: the room of a legend row (`dtap`, `map`, `pad`, `coop`, `spawn_tabs`,
`spawn_rows`) is what is left beside the widest key; if you lengthen a key
you can write yourself (`map` keys, `coop_keys`, `shoot_key`), the room of every
row on that slide shrinks. `buttons`: the five columns share 282 units, so one
wide column steals from the others; `special` likewise (four columns, 288).
`types`/`modes`: the three columns share 286 units with the Options word above.

**Tables are also limited in height.** A row may take two lines, but not
every row can. How many rows may take two lines at once (besides the AIM LOCK
row, which always has two: `tweaks[0]` + `aimlock_release`):

| slide | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| TWEAKS (`tweaks[1..5]`) | 2 | 2 | 2 | 2 | 1 | 1 | 1 |
| MORE TWEAKS (`tweaks2`, 5 rows) | 4 | 2 | 3 | 2 | 3 | 3 | 3 |
| MODS / MORE MODS (`mods`, 4 a slide) | 4 | 4 | 4 | 4 | 4 | 4 | 4 |
| YOUR MODS (`user`, 3 rows) | 3 | 3 | 3 | 3 | 3 | 3 | 3 |
| A MOD'S PAGE (`page`, 6 rows) | 3 | 2 | 2 | 2 | 2 | 2 | 2 |

(Russian and German keys like `ИМПОРТ СОХРАНЕНИЙ` / `MOVE UP / MOVE DOWN`
already take two lines themselves, which is why their tables hold fewer.)

## The slides and their slots, in order

Previews of the current slides: `other stuff/guide/slides-<lang>.png` (19
slides, two a row). Your own words: the checker's `--png`. Each slide is a
pinned paper (a title, then pictures and words) with a yellow note under it.
The titles of slides 12, 14, 16 and 18 are menu words, fixed by the tables
below (the Tweaks title, ITEM SPAWN, MODS, YOUR MODS). Every other title is yours.

The **meaning** of each slot comes from the English (`en.py`) and from the
audit of 06.10.2026 (`ПЕРЕДАЧА.md`, "Гайд сверен с кодом"), which checked the
guide against the code. Do not add behaviour that is not written here.

### `titles` (10): slides 1-10. CAPS. One line, 360 wide.
Slide headings: MOVE AND SHOOT, STICK TYPES, TOUCH MODES, DOUBLE TAP, BUTTONS,
SPECIAL BUTTONS, MAP AND ITEMS, ITEM DESCRIPTIONS, YOUR LAYOUT (the on-screen
controls editor), GAMEPAD & KEYBOARD. Keep them short, like a game menu heading.
jp: **kana only**, because our title lettering has no kanji (the checker
catches a missing glyph).

### Slide 1 MOVE AND SHOOT (two stick pictures)
- `move_shoot` (2): the captions under the left (move) and right (fire) stick,
  in the big bold font. One line each, 150 wide. CAPS. One word each if possible.
- `notes[0]`: the left stick moves and the right stick shoots. Stick type, size
  and touch mode are set in Options, and the next two slides explain them.

### Slide 2 STICK TYPES (three stick knobs)
- `types` (3): the **second line** under each Options word STATIC / DYNAMIC /
  FLOATING. The word above is fixed and drawn as Options draws it. CAPS, 95
  wide, 2 lines. STATIC stays where you put it. DYNAMIC appears where your thumb
  lands. FLOATING follows your thumb as it moves.
- `notes[1]`: Options > TYPE, then one clause per type (the same three facts).

### Slide 3 TOUCH MODES (stick, stick, D-pad)
- `modes` (3): the second line under ANALOGUE / DIGITAL / BUTTONS. CAPS, 95
  wide, 2 lines.
  - ANALOGUE: the further you push, the faster you move.
  - DIGITAL: you either stand still or go at full speed, with nothing between.
  - BUTTONS: D-pads instead of sticks.
- `notes[2]`: Options > TOUCH MODE, then the three modes. ANALOGUE: push
  further to go faster. DIGITAL: still, or full speed once the stick is past
  the DEAD ZONE (the Tweaks row, which is the switch point). BUTTONS: D-pads
  instead of sticks.
  The current Russian "СТОП ИЛИ ПОЛНЫЙ ХОД" and "АНАЛОГ: где полный ход" are
  the kind of wording the owner rejected.

### Slide 4 DOUBLE TAP (picture: the Mars item)
- `dtap` (4): legend rows, one line each, in lowercase small font, beside the
  fixed keys STATIC, DYNAMIC, FLOATING, BUTTONS. They say how to double-tap on
  each kind of control:
  - STATIC: tap the same side of the stick twice.
  - DYNAMIC: tap once, then tap again and flick in the direction you want.
  - FLOATING: the same as DYNAMIC.
  - BUTTONS: tap an arrow twice.
- `notes[3]`: Mars dashes and Hemoptysis sneezes on a double tap in one
  direction. Both sticks work the same way. The second tap must come within
  half a second.

### Slide 5 BUTTONS (five pictures: pause, bomb, active item, pill/card, trinket)
- `buttons` (7): each is `GESTURE: ACTION`, drawn as two lines (the gesture
  over the action) under its picture. CAPS. A column is only 56 wide, so use one
  short word a line. The order:
  - [0] pause: TAP: PAUSE
  - [1] pause: HOLD: RESTART
  - [2] bomb: TAP: BOMB
  - [3] active item: TAP: USE
  - [4] pill/card: TAP: USE
  - [5] pill/card: HOLD: DROP
  - [6] trinket: HOLD: DROP

  The pause and pill/card columns carry two labels, which makes four lines.
  That is the most a column holds, so none of their words may wrap.
- `notes[4]`: hold pause to restart the run, and let go before the screen goes
  dark to cancel. The trinket is a button too: hold it to drop it, with no pill
  or card needed.

### Slide 6 SPECIAL BUTTONS (four pictures: the round twin button, Marked, Schoolbag, D infinity)
- `special` (4 pairs): (first line, second line) under each picture. CAPS, 72
  wide, at most 4 lines a column.
  - [0] the round button: holds the other body still.
  - [1] Marked: STOP FIRING.
  - [2] the second active item (Schoolbag): SWAP.
  - [3] D infinity: HOLD (to choose a die).

  Use the game's own item names (table below). Shorten only where the game's
  name will not fit, as the English "2ND ITEM" does.
- `notes[5]`: the round button by pause (Jacob and Esau, Strawman...) holds
  the other body still. For the Forgotten it swaps the bodies. Schoolbag: tap
  to swap. D infinity: hold to choose a die.

### Slide 7 MAP AND ITEMS (picture: the minimap)
- `map` (4 pairs): legend rows `(KEY, what it does)`, one line each, with the
  key right-aligned. They are gestures on the minimap:
  - TAP keeps the big map open.
  - HOLD shows it while you hold.
  - HOLD LONGER opens your inventory with every item described. This works
    **only when ITEM DESCRIPTIONS is on**.
  - SWIPE with a second finger scrolls that inventory.

  You write the keys (CAPS). A longer key takes room from all four rows.
- `notes[6]`: tap the minimap to keep the big map open. With ITEM DESCRIPTIONS
  on, hold it longer for the inventory with descriptions, and scroll it with
  another finger.

### Slide 8 ITEM DESCRIPTIONS (pictures: an item, the Bag of Crafting)
- `desc` (2 columns of lines): CAPS, 150 wide, at most 5 lines a column
  counting wraps.
  - Column 1 (2 lines): walk up to an item to see what it does.
  - Column 2 (4 lines): hold up the Bag of Crafting to see recipes;
    SWIPE: turn pages; a finger held on the recipes locks the list (stage 3 fix).
- `notes[7]`: descriptions appear by themselves. A finger held on the recipes
  locks the list, so it does not change while you pick things up. Settings: MODS > ITEM DESCRIPTIONS.

### Slide 9 YOUR LAYOUT (picture: the editor with GRID, RESET, CANCEL, DONE and the OPACITY slider, already in your language)
- `notes[8]`: Options > Controls > Touch. Drag the sticks, the buttons and the
  pause button wherever you like. OPACITY sets how see-through they are, and
  GRID snaps them to a grid. For the menu words, see "Game menu words" below.
  Write the editor's own words (GRID, OPACITY) as the editor shows them in your
  language: see slide 9 of `other stuff/guide/slides-<lang>.png`.

### Slide 10 GAMEPAD & KEYBOARD
- `pad` (7): legend rows, one line each, beside the fixed keys SELECT, R3, L3,
  L1 R1 L2 R2, TAB, `shoot_key`, ESC:
  - SELECT, held: the inventory with descriptions (only when ITEM
    DESCRIPTIONS is on).
  - R3: next page of the Bag of Crafting.
  - L3: lock the Bag of Crafting's recipe list (a toggle).
  - L1 R1 L2 R2, all four held: restart.
  - TAB, held: the inventory, as SELECT.
  - TAB + the shoot keys: turn the Bag of Crafting's pages; shoot left locks.
  - ESC: pause.
- `shoot_key`: the key cell "TAB + SHOOT", where SHOOT means the fire keys.
  CAPS. Keep it short: it widens the key column for every row.
- `notes[9]`: this slide lists only what the game's own controls screen does
  not show; the rest is in Options > Controls. In single-player, a controller
  that cannot rumble makes the phone vibrate instead.

### Slide 11 LOCAL CO-OP (picture: the game's character choice, Isaac between two arrows)
- `coop_title`: CAPS title.
- `coop_keys` (5): the key cells SELECT + START, ESC + TAB, LEFT / RIGHT,
  A / B, ENTER / BACKSPACE. Only LEFT / RIGHT is translatable: key names stay
  as they are printed on the pad or keyboard. CAPS.
- `coop` (5): the rows beside them:
  - [0] hold 1.5 s on a pad that has no player yet to join.
  - [1] the same on a keyboard (Esc+Tab).
  - [2] choose a character.
  - [3] confirm / cancel.
  - [4] confirm / cancel on a keyboard.

  Nobody has checked in code that Backspace cancels, so do not add detail.
- `coop_note`: at the start of a run, hold SELECT and START on a free pad for
  a second and a half, then pick a character with the arrows. Up to four
  players. "First room of a run" was not verified either, so "at the start of
  a run" is the safe wording.

### Slide 12 TWEAKS (title fixed: the Tweaks title). A table: the menu row's name (fixed, right-aligned), then your explanation
- `tweaks` (6) + `aimlock_release`. The rows:
  - AIM LOCK: `tweaks[0]` (line 1) and `aimlock_release` (line 2). It keeps
    shooting in the last direction after you lift your thumb.
    `aimlock_release` says how to stop it: a quick tap in the centre of the
    fire stick or D-pad.
  - DEAD ZONE: `[1]`. How far you push the **move** stick before you start
    walking. It is the move stick only. In DIGITAL it is also the point where
    you switch to full speed.
  - MOVE SENSITIVITY: `[2]`. How far to push for full speed. It matters only
    in ANALOGUE.
  - AIM SENSITIVITY: `[3]`. How far to push the fire stick before you shoot.
  - RENDER SCALE: `[4]`. A sharper picture or a smoother game. The owner said
    to keep this meaning as it is.
  - RUMBLE: `[5]`. A slider for vibration strength; 0 turns it off.

  Use plain words: answer the player's question "what does this do?". The
  owner rejected wording such as "ход до начала ходьбы" and "АНАЛОГ: где
  полный ход".
- `aimlock_note`: one or two sentences, added after `tweaks_notes[0]` on the
  same note. AIM LOCK works with touch only. To stop it, tap the middle of the
  fire stick or D-pad quickly, without sliding; on a moving stick, tap and
  lift. The next touch locks again.
- `tweaks_notes[0]`: where the page is: Options > TWEAKS, using your language's
  Tweaks title. Keep it this short, because the AIM LOCK note follows it.

### Slide 13 MORE TWEAKS
- `tweaks2_title`: CAPS title ("more tweaks").
- `tweaks2` (5): table entries beside ITEM SPAWN, SPAWNER TROPHIES, CONFIG
  FILE, IMPORT SAVES, EXPORT SAVES:
  - ITEM SPAWN: puts the item spawner on the pause screen (ON/OFF).
  - SPAWNER TROPHIES: when ON, runs where the spawner was used still unlock
    things; when OFF they unlock nothing.
  - CONFIG FILE: opens the port's folder (settings, mods, saves) in the phone's
    file manager.
  - IMPORT SAVES: loads saves from other versions of the game. **Do not list
    platforms.**
  - EXPORT SAVES: copies your saves into a folder.
- `tweaks_notes[1]`: import takes saves from other versions of the game, and
  the PC game can read exported saves. The next two slides explain the spawner.

### Slide 14 ITEM SPAWN (title fixed: the ITEM SPAWN row name; picture: the pause screen's ITEM SPAWN tab)
- `spawn_tabs` (4): one line each, beside the card's four tabs. The tabs are
  ITEMS, ENEMIES, TELEPORT and STATS, fixed and drawn in your language. Each
  line says what a tap does there:
  - ITEMS: a tap picks an item, and a second tap on it gives it to you.
  - ENEMIES: a tap picks, and a second tap spawns.
  - TELEPORT: a room goes on one tap, a floor on two.
  - STATS: type a value, or step it with the arrows.
- `spawn_notes[0]`: turn it on in TWEAKS > ITEM SPAWN, then open it in a run
  from the ITEM SPAWN tab on the pause screen. A run that used it unlocks
  nothing unless SPAWNER TROPHIES is on.

### Slide 15 SPAWNER CONTROLS
- `spawn_title`: CAPS title.
- `spawn_rows` (7): one line each, beside the fixed keys `< PLAYER 1 >` (in
  your language), `<  >`, `3.50`, RESET (in your language), `L1 / R1`, `TAB`
  and `ENTER / ESC`:
  - in co-op, choose whose stats you change.
  - the arrows step the value down or up.
  - tap the value again to type a number.
  - RESET undoes your changes.
  - L1/R1 switch tabs.
  - Tab switches tabs on a keyboard.
  - Enter/Esc confirm or close.
- `spawn_notes[1]`: search by name, in the game's language or in English, or
  by number. A stat you set lasts until the run ends. Enemies appear next to
  you.

### Slide 16 MODS (title fixed: MODS) and slide 17 MORE MODS
- `mods2_title`: CAPS title of slide 17.
- `mods` (8): table entries, four a slide, beside the MODS page's row names:
  - [0] SPECIALIST DANCE: when a quality 4 item is in the room, everyone dances
    to the music.
  - [1] PLANETARIUM CHANCE: the chance of a Planetarium on the next floor,
    shown under the stats.
  - [2] BOSS RUSH WAVES: the wave counter in Boss Rush.
  - [3] BOSS BARS: a health bar for each boss, with its icon and status.
  - [4] COMPLETION MARKS: the marks you still need show faintly on the
    completion note.
  - [5] STATS+: extra numbers beside the stats (damage multiplier, tear cap).
  - [6] RANGE FIX: melee hits enemies close to you, even with high range.
  - [7] ITEM DESCRIPTIONS: shows what an item does when you walk up to it (EID).
- `mods_notes[0]`: Main menu > MODS. These are PC mods remade for this port,
  with nothing to install. Tap a row to turn a mod on or off. Where a row has
  an arrow, the arrow opens that mod's settings.
- `mods_notes[1]`: RANGE FIX is for melee: bones, swords, the axe and the Bag
  of Crafting. ITEM DESCRIPTIONS can be shown in another language: tap its
  arrow, then go to the settings.

### Slide 18 YOUR MODS (title fixed: YOUR MODS)
- `user_key`: the key cell that stands for a mod's own row in the list (any
  mod's name), e.g. "MOD NAME". CAPS.
- `user` (3): beside + ADD MOD, EXPORT GAME FILES and `user_key`:
  - [0] pick a mod's .zip file; PC mods are converted for the port.
  - [1] copies the game's files out, as a starting point for your own mods.
  - [2] tap: on/off; arrow: the mod's page.
- `user_notes[0]`: Steam Workshop (PC) mods work if they change pictures,
  sounds and data; Lua scripts do not run. If you add a mod you already have,
  you can update it or install it beside the old one.

### Slide 19 A MOD'S PAGE
- `page_title`: CAPS title.
- `page` (6): beside ENABLED, MOVE UP / MOVE DOWN, EXPORT, DELETE, SETTINGS
  and RESTART TO APPLY:
  - the mod on or off.
  - when two mods change the same file, the one higher in the list wins.
  - saves the mod as a .zip.
  - tap twice to delete; the first tap asks DELETE FOR SURE?.
  - the mod's own options.
  - restarts the game so the changes take effect.
- `user_notes[1]`: a mod's name in red means the port does not use all of it.
  Red lines above the title on its page say why, or name the mod it conflicts
  with. Tap a line for details.

## Game menu words (the native menus; write them exactly)

From the game's own options sheets (`optionsmenu.png`, `optionsextra.png` of
each set) and the port's code. The Touch row of the Controls list is named
"Touch" in **every** language (`touchedit.c` writes it). The section heading
TOUCH MODE is drawn in English on every set's sheet. Its rows are MODE (ru
РЕЖИМ), TYPE and SIZE (table "Options rows").

| term | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| Options (menu title) | OPTIONS | НАСТРОЙКИ | OPTIONEN | OPCIONES | オプション | 설정 | 设置 |
| Controls (row) | Controls | Управление | Steuerung | Controles | コントロール | 조작 | 控制设置 |
| Touch (device row) | Touch | Touch | Touch | Touch | Touch | Touch | Touch |
| TOUCH MODE (heading) | TOUCH MODE | TOUCH MODE | TOUCH MODE | TOUCH MODE | TOUCH MODE | TOUCH MODE | TOUCH MODE |
| MODE (row) | MODE | РЕЖИМ | MODE | MODE | MODE | MODE | MODE |

So in Russian, "Настройки > Управление > Касание" and "Настройки > РЕЖИМ
КАСАНИЯ" (the current text) name rows that do not exist. The menu says
"Управление > Touch" and "РЕЖИМ". The editor's GRID / RESET / CANCEL / DONE /
OPACITY words are on the slide 9 picture in each language.

## Fixed terms per language (extracted from the sources; write them exactly)

Sources: `tools/lang.py` WORDS (our Options rows), `tools/tweaks_page.py` SETS / MODS_WORD / USER_WORDS (Tweaks, MODS and player-mod rows), `tools/spawner_tabs.py` TABS and `tools/spawner_stats.py` PLAYER / RESET (the spawner card), and the game's `stringtable.sta` (items, characters). Use the item and character names exactly; inflect them only where your grammar needs it, and keep them recognisable.

### Options rows (lang.WORDS)

| term | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| TYPE | TYPE | ТИП | TYP | TIPO | タイプ | 종류 | 类型 |
| SIZE | SIZE | РАЗМЕР | GRÖSSE | TAMAÑO | サイズ | 크기 | 大小 |
| STATIC | STATIC | СТАТИЧНЫЙ | STATISCH | ESTÁTICO | こてい | 고정 | 固定 |
| DYNAMIC | DYNAMIC | ДИНАМИЧНЫЙ | DYNAMISCH | DINÁMICO | ついじゅう | 동적 | 跟随 |
| SMALL | SMALL | МАЛЫЙ | KLEIN | PEQUEÑO | ちいさい | 작게 | 小 |
| MEDIUM | MEDIUM | СРЕДНИЙ | MITTEL | MEDIO | ふつう | 보통 | 中 |
| BIG | BIG | БОЛЬШОЙ | GROSS | GRANDE | おおきい | 크게 | 大 |
| ANALOGUE | ANALOGUE | АНАЛОГ | ANALOG | ANALÓGICO | アナログ | 아날로그 | 模拟 |
| DIGITAL | DIGITAL | ЦИФРОВОЙ | DIGITAL | DIGITAL | デジタル | 디지털 | 数字 |
| FLOATING | FLOATING | ПЛАВАЮЩИЙ | SCHWEBEND | FLOTANTE | ふゆう | 유동 | 浮动 |
| VERY BIG | VERY BIG | ОГРОМНЫЙ | SEHR GROSS | MUY GRANDE | とくだい | 매우 크게 | 特大 |
| BUTTONS | BUTTONS | КНОПКИ | TASTEN | BOTONES | ボタン | 버튼 | 按键 |

### Tweaks page and MODS page rows (tweaks_page.SETS)

| term | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| (Tweaks title) | TWEAKS | ТВИКИ | TWEAKS | AJUSTES | こまかいせってい | 세부 설정 | 更多选项 |
| ITEM SPAWN | ITEM SPAWN | СПАВН ПРЕДМЕТОВ | ITEM-SPAWN | SPAWN DE OBJETOS | アイテムスポーン | 아이템 스폰 | 道具生成 |
| SPAWNER TROPHIES | SPAWNER TROPHIES | ТРОФЕИ СПАВНЕРА | SPAWN-TROPHÄEN | TROFEOS DEL SPAWN | スポーンのトロフィー | 스폰 트로피 | 生成器奖杯 |
| AIM LOCK | AIM LOCK | БЛОК СТРЕЛЬБЫ | ZIELSPERRE | BLOQUEO DE MIRA | エイムロック | 조준 고정 | 瞄准锁定 |
| RUMBLE | RUMBLE | ВИБРАЦИЯ | VIBRATION | VIBRACIÓN | ランブル | 진동 | 手柄振动 |
| RENDER SCALE | RENDER SCALE | МАСШТАБ РЕНДЕРА | RENDER-SKALIERUNG | ESCALA DE RENDER | がしつ | 렌더 배율 | 渲染缩放 |
| DEAD ZONE | DEAD ZONE | МЁРТВАЯ ЗОНА | TOTZONE | ZONA MUERTA | デッドゾーン | 데드존 | 摇杆死区 |
| MOVE SENSITIVITY | MOVE SENSITIVITY | ЧУВСТ. ДВИЖЕНИЯ | EMPF. BEWEGUNG | SENS. MOVIMIENTO | いどうのかんど | 이동 감도 | 移动灵敏度 |
| AIM SENSITIVITY | AIM SENSITIVITY | ЧУВСТ. СТРЕЛЬБЫ | EMPF. ZIELEN | SENS. PUNTERÍA | ねらいのかんど | 조준 감도 | 瞄准灵敏度 |
| CONFIG FILE | CONFIG FILE | ФАЙЛ НАСТРОЕК | KONFIG-DATEI | ARCHIVO DE CONFIG | せっていファイル | 설정 파일 | 配置文件 |
| IMPORT SAVES | IMPORT SAVES | ИМПОРТ СОХРАНЕНИЙ | SPIELSTÄNDE LADEN | IMPORTAR PARTIDAS | セーブのよみこみ | 세이브 가져오기 | 导入存档 |
| EXPORT SAVES | EXPORT SAVES | ЭКСПОРТ СОХРАНЕНИЙ | SPIELSTÄNDE SICHERN | EXPORTAR PARTIDAS | セーブのかきだし | 세이브 내보내기 | 导出存档 |
| SPECIALIST DANCE | SPECIALIST DANCE | ТАНЕЦ СПЕЦИАЛИСТА | SPEZIALISTEN-TANZ | BAILE ESPECIALISTA | スペシャリストダンス | 스페셜리스트 댄스 | 专家之舞 |
| PLANETARIUM CHANCE | PLANETARIUM CHANCE | ШАНС ПЛАНЕТАРИЯ | PLANETARIUM-CHANCE | PROB. PLANETARIO | プラネタリウムのかくりつ | 천체관 확률 | 星象房概率 |
| BOSS RUSH WAVES | BOSS RUSH WAVES | ВОЛНЫ БОСС-РАША | BOSS-RUSH-WELLEN | OLEADAS BOSS RUSH | ボスラッシュウェーブ | 보스 러시 웨이브 | 头目车轮战波次 |
| BOSS BARS | BOSS BARS | ПОЛОСЫ БОССОВ | BOSS-LEISTEN | BARRAS DE JEFES | ボスバー | 보스 체력바 | 头目血条 |
| COMPLETION MARKS | COMPLETION MARKS | ПОДСКАЗКА ОТМЕТОК | ABSCHLUSSMARKEN | MARCAS PENDIENTES | クリアマークのヒント | 완료 마크 표시 | 通关标记提示 |
| STATS+ | STATS+ | СТАТЫ+ | STATS+ | ESTADÍSTICAS+ | ステータス+ | 스탯+ | 属性+ |
| RANGE FIX | RANGE FIX | ФИКС ДАЛЬНОСТИ | REICHWEITEN-FIX | ARREGLO DE RANGO | しゃていのしゅうせい | 사거리 수정 | 射程修复 |
| ITEM DESCRIPTIONS | ITEM DESCRIPTIONS | ОПИСАНИЯ ПРЕДМЕТОВ | ITEM-BESCHREIBUNGEN | DESCRIPCIONES | アイテムのせつめい | 아이템 설명 | 道具说明 |
| (main menu) MODS | MODS | МОДЫ | MODS | MODS | MOD | 모드 | 模组 |

### Player mods (tweaks_page.USER_WORDS)

| term | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| YOUR MODS | YOUR MODS | СВОИ МОДЫ | EIGENE MODS | TUS MODS | じぶんのMOD | 내 모드 | 我的模组 |
| + ADD MOD | + ADD MOD | + ДОБАВИТЬ МОД | + MOD HINZUFÜGEN | + AÑADIR MOD | + MODをついか | + 모드 추가 | + 添加模组 |
| EXPORT GAME FILES | EXPORT GAME FILES | ВЫГРУЗИТЬ ФАЙЛЫ ИГРЫ | SPIELDATEIEN SICHERN | EXPORTAR ARCHIVOS | ゲームファイルのかきだし | 게임 파일 내보내기 | 导出游戏文件 |
| ENABLED | ENABLED | ВКЛЮЧЁН | AKTIV | ACTIVO | ゆうこう | 사용 | 启用 |
| MOVE UP | MOVE UP | ВЫШЕ | NACH OBEN | SUBIR | うえへ | 위로 | 上移 |
| MOVE DOWN | MOVE DOWN | НИЖЕ | NACH UNTEN | BAJAR | したへ | 아래로 | 下移 |
| EXPORT | EXPORT | ВЫГРУЗИТЬ | EXPORTIEREN | EXPORTAR | かきだし | 내보내기 | 导出 |
| DELETE | DELETE | УДАЛИТЬ | LÖSCHEN | BORRAR | さくじょ | 삭제 | 删除 |
| DELETE FOR SURE? | DELETE FOR SURE? | ТОЧНО УДАЛИТЬ? | WIRKLICH LÖSCHEN? | BORRAR DE VERDAD? | ほんとうにさくじょ? | 정말 삭제? | 确定删除? |
| RESTART TO APPLY | RESTART TO APPLY | ПЕРЕЗАПУСТИТЬ ДЛЯ ПРИМЕНЕНИЯ | NEU STARTEN ZUM ANWENDEN | REINICIAR PARA APLICAR | さいきどうしててきよう | 재시작해서 적용 | 重启以生效 |
| SETTINGS | SETTINGS | НАСТРОЙКИ | EINSTELLUNGEN | OPCIONES | せってい | 설정 | 选项 |

### Spawner card (spawner_tabs.TABS, spawner_stats)

| term | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| ITEMS | ITEMS | ПРЕДМЕТЫ | ITEMS | OBJETOS | アイテム | 아이템 | 道具 |
| ENEMIES | ENEMIES | ВРАГИ | GEGNER | ENEMIGOS | てき | 적 | 敌人 |
| TELEPORT | TELEPORT | ТЕЛЕПОРТ | TELEPORT | TELEPORTE | テレポート | 텔레포트 | 传送 |
| STATS | STATS | СТАТЫ | WERTE | STATS | ステータス | 능력치 | 属性 |
| PLAYER 1 | PLAYER 1 | ИГРОК 1 | SPIELER 1 | JUGADOR 1 | プレイヤー1 | 플레이어 1 | 玩家1 |
| RESET | RESET | СБРОСИТЬ | ZURÜCKSETZEN | RESTABLECER | リセット | 초기화 | 重置 |

### Items and characters (the game's stringtable.sta)

| term | en | ru | de | es | jp | kr | zh |
|---|---|---|---|---|---|---|---|
| Mars | Mars | Марс | Mars | Mars | マルス | 화성 | 火星 |
| Hemoptysis | Hemoptysis | Кровохарканье | Hämoptysis | Hemoptisis | ヘモプタスィス | 각혈 | 咯血症 |
| Schoolbag | Schoolbag | Портфель | Schulranzen | Mochila | スクール バッグ | 학교 가방 | 书包 |
| Marked | Marked | Отмеченный | Markiert | En la Mira | 照準マーク | 조준 | 十字准星 |
| D infinity | D infinity | Бесконечный D | W unendlich | D infinito | Ｄインフィニティー | D 인피니티 | 无限面骰 |
| Bag of Crafting | Bag of Crafting | Созидательный мешок | Handwerkstasche | Bolsa de crafteo | バッグ オブ クラフティング | 제작 가방 | 合成宝袋 |
| Strawman | Strawman | Чучело | Strohmann | Hombre de trapo | ストローマン | 밀짚 인형 | 稻草人 |
| The Forgotten | The Forgotten | Забытый | Der Vergessene | El Olvidado | ザ フォーガットン | 더 포가튼 | 遗骸 |
| Jacob | Jacob | Иаков | Jakob | Jacob | ヤコブ | 야곱 | 雅各 |
| Esau | Esau | Исав | Esau | Esaú | エサウ | 에사우 | 以扫 |
| Isaac | Isaac | Исаак | Isaac | Isaac | アイザック | 아이작 | 以撒 |

## Where the room is tight (read before writing)

- **`buttons`**: the columns are 56 wide. Russian `ДЕРЖАТЬ:`, `ПРИНЯТЬ`, German
  `NEUSTART` and Spanish `REINICIAR` are wider than that today. The tool widens
  the column, and the row still fits only because the other columns are
  narrow. The pause and pill/card columns may not wrap at all. Look for short
  gesture words (ru e.g. `ЖМИ:` / `ДЕРЖИ:`; de `TIPP:` / `HALTEN:`).
- **`special`** (72 wide): `ОТМЕЧЕННЫЙ`, `ZWILLINGE:`, `EINFRIEREN`,
  `UNENDLICH:` and `Ｄインフィニティー：` already exceed one column. Up to four
  lines, so a name may take two lines, but then the action gets only two.
- **`types` / `modes`** (95 x 2): the Options word sits above and is fixed;
  `STEUERKREUZE` is already wider than 95.
- **`dtap` in Russian** (160): the key `ДИНАМИЧНЫЙ` / `ПЛАВАЮЩИЙ` is wide, which
  leaves 160 for "tap once, then tap again and flick". Expect one short clause.
- **`map` in English** (160): the keys are yours, so a shorter key buys room.
- **Tweaks table in jp / kr / zh**: only one of DEAD ZONE ... RUMBLE may take
  two lines (the AIM LOCK row already has two). The English uses two lines for
  MOVE SENSITIVITY ("how far to push for full speed / ANALOGUE only"). In
  CJK, fit that into one line (the room is 237-260, so it fits).
- **MORE TWEAKS in ru / es**: only two of the five rows may take two lines, and
  in Russian `ИМПОРТ СОХРАНЕНИЙ` / `ЭКСПОРТ СОХРАНЕНИЙ` already push the key
  column wide (room 185).
- **A MOD'S PAGE in ru / de / es / CJK**: two two-line rows at most, and MOVE
  UP / MOVE DOWN is already one of them.
- **German `mods`** (153 wide): long compound keys (`ITEM-BESCHREIBUNGEN`)
  leave the least room of any language. Two lines of about 26 characters.
- **Notes in zh / jp**: about 75 / 90 characters for the whole note. The
  `tweaks_notes[0] + aimlock_note` note is the hardest: the location plus the
  whole AIM LOCK release rule.
