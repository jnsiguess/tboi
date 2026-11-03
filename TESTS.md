# What to test

A checklist for testers. At the top is what changed most recently and carries the
most risk; at the bottom is what was closed long ago and is only needed for
regression.

If something is wrong, write down **what you did, what you saw, and on which
phone** (model, Android version, screen aspect ratio). Without this, half of the
reports are useless: almost everything in this port depends on the screen shape.

---

## 1. Screen shape: the main risk

The game is designed for 480×270 units (16:9). The port extends the canvas: on a
screen **wider** than 16:9 the width grows, on a **narrower** one the height grows.
The entire layout depends on exactly this, so test on different screens, not on
one.

Needed: a regular phone (~19.5:9 or 20:9), a wide one (21:9), a tablet (16:10,
4:3), and something close to 16:9.

On each one, go through:

- **Main menu.** Nothing from neighbouring screens may show through at the edges:
  top, bottom, left, right. Check the neighbours too: Challenges, Stats,
  Stats → Movies, character select (both pages), save select, Special Seed,
  Custom Challenge, Controls.
- **Every menu item responds where it is drawn.** Especially character select
  (the rows are slanted and close together) and save select.
- **Options.** Open in place, scroll all the way down, the bottom edge of the
  backing is fully visible. Check both entries: from the main menu and from pause.
- **Run.** Sticks in place, the button row next to the right stick, counters and
  hearts in the corners, the map in its corner.
- **Pause.** Pause rows respond where they are drawn.
- **Cutscenes.** Intro, epilogue, credits: the picture is not distorted; bars on
  the sides or top and bottom, but not stretched or squashed.

You can fake the aspect ratio on your own phone without owning a tablet: add the
line `aspect = 16:10` (or `4:3`, `21:9`, `16:9`) to
`/sdcard/Android/media/com.tboi.rebirth/port-touch.ini` and restart the
game. The frame becomes that shape, centred, with the rest black. Remove the
line to go back to full screen.

## 2. Button row next to the right stick

Bombs, cards/pills, active items. The row sits against the right stick with a
constant gap.

- Three stick sizes (in the options): the gap looks the same on all three.
- The row grows **to the left** as buttons appear: pick up bombs, take an active
  item, take the School Bag (second active item), collect cards and pills.
- No button may overlap the stick or go past the left edge.
- Every button responds where it is drawn.

## 3. Tear colour

An engine clamp was removed here: previously, items that **lighten** the tear did
nothing. Check both directions.

- Lightening items, **Stye, Holy Light, Spider Bite**: the tear must become
  noticeably lighter.
- Darkening and tinting items, **Iron Bar** (darker), **Serpent's Kiss** (greener),
  **The Common Cold** (yellower), **Ball of Tar**, **Chemical Peel**: must work as
  before.
- Most importantly, **is anything else overexposed**: golden items and chests,
  boss overlays, the flash when taking damage, curses, fire, explosions. If
  something turned blinding white, report it here.

## 4. Reset and the Mr. Resetter achievement

- Long tap on the pause button: the run restarts with a new seed; the character
  and the challenge are kept.
- Short tap: normal pause. One press does one thing.
- **Ten** resets in a row award the Mr. Resetter achievement.
- A won run breaks the streak: after a win the count starts over.
- As Eden, a reset spends a token, and without tokens it does not work at all,
  same as on a gamepad.

## 5. Long tap on item buttons

- **Card or pill**: short tap uses it, long tap drops it. Test with several cards
  in the pockets, with pills, and with a mix.
- **School Bag**: long tap swaps the active items **without using** them.
  Especially with an item that cannot be used (not charged).
- No long tap may fire twice or together with a short tap.

## 6. Sticks

- Three types: **static** (stay in place), **dynamic** (appear under the finger
  and stay), **floating** (follow the finger). Switched in the options.
- Three sizes. When the size changes, the stick changes immediately, without a
  restart.
- The `insetx` / `insety` offsets in `port-touch.ini` move both sticks away from
  the edges.
- Aiming and shooting work in all directions; the stick does not stick.

## 7. Options and menus

- All eighteen option rows scroll and toggle.
- Our rows: **Touch Mode**, **Stick Type**, **Stick Scale**: toggle by tap, the
  value is shown as a word.
- The **Controls** menu: it can always be exited, even with no gamepad connected.
- The language switches, and the menu labels are readable in every language (Russian,
  English, German, Spanish, French, Japanese, Korean, Chinese).
- Brightness defaults to 90 and persists between launches.

## 8. Saves

- The game sees both its own saves (`rep_persistentgamedata1.dat`) and PC saves
  (`20260905.persistentgamedata1.dat`). Put a PC save into
  `Android/media/com.tboi.rebirth/saves/Repentance/` and check that the progress
  is picked up.
- If both are present, its own `rep_` save is used.
- Progress is not lost between launches. **Warning:** uninstalling the game can
  erase the saves; copy `Android/media/com.tboi.rebirth/saves/` elsewhere first.

## 9. Gamepad

- D-pad, both sticks, triggers, buttons, `Menu` / `Options`.
- Connecting and disconnecting on the fly does not hang the game.
- The device list is shown in the Controls submenu.

## 10. Regression: things closed long ago

Check quickly, but do check.

- The bomb button stays visible with a golden bomb at a count of zero.
- The daily run panel is removed from the main menu.
- The port's label in the top right corner of the main menu is in place and does
  not get in the way.
- App icon, version 1.4.
- Menu music does not disappear after sitting there a long time.
- Sound does not stutter or click.
- The game survives being minimised and restored, a phone call, screen lock.
- Long session: an hour to an hour and a half of play without a crash or a
  slowdown.

---

## What to put in a report

1. Phone, Android version, screen resolution and aspect ratio.
2. What you did, step by step, so that it can be repeated.
3. What you expected and what you got.
4. A screenshot or recording, if it is visible.
5. If it crashed, capture the log right after, before rebooting the phone:
   `adb logcat -d > crash.txt`.
