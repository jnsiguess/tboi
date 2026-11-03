# Map of the port of The Binding of Isaac: Repentance (Android 1.777)

A port of the iOS version of the game to Android **without the game's source
code**. The real iOS 1.4 executable (Mach-O, arm64) is taken as is and run on
Android: our own loader maps the image into memory, and everything the game asks
of iOS (Objective-C, UIKit, EAGL, OpenAL, GameController, files) is answered by
our layer. On top of that sits the "remaster": patches to the game's code made
directly in the image, and new features in C that call the game's functions by
their addresses.

This file is the entry point. Details, measurements and the reasoning behind
each finding are in the code comments. The working rules for the project are in
`CLAUDE.md`.

---

## 1. What the archive contains and what it does not

**Included**: everything made for the port.

| Path | What it is |
|---|---|
| `port/` | Android Studio / Gradle project: `app/src/main/cpp` (the C layer, ~60 files), `java/ru/isaac/port` (the Java side), `res/`, `assets/` (our resources) |
| `port/app/src/main/assets/` | our atlases and animations `port_*`, the EID fonts and description database (`eid*.fnt/png`, `eid.bin`), `port_patch/` (changes to game files, applied at startup) |
| `work/port-edit/` | sources of our changes to game files (our versions of `.anm2`/`.png`); `tools/port_patch.py` builds `assets/port_patch/` from them |
| `work/app-port/` | the only game file the pack ships in our version (`stringtable.sta`) |
| `tools/` | all pipelines and research tools (Python), test benches (`*desk/`, C/Java/Python) |
| `menu-labels/` | hand-drawn menu labels, atlases, reference copies, layout |
| `eid-mod/` | copy of the External Item Descriptions mod (Lua), from which `eid.bin` is built |
| `other stuff/arabic-eid/`, `other stuff/console-strap/` | inputs of two tools (the PixelAE Arabic font and mod; the CONSOLE strap artwork) |
| `MAP.md`, `CLAUDE.md`, `TESTS.md` | documentation: this map, the working rules, the tester checklist |

**Not included**: nothing from the game itself and nothing built:

- the game image (`isaac.arm64`) and its resources (`work/app`, `resources.pak`, `.ipa`);
- built APKs, `.so` files, built test benches (`.exe`, `.o`, `.pdb`), Gradle/CMake output;
- reference copies (`*.before-*`, `*.orig`): the working tree holds hundreds of them as a safety net;
- device reports, `local.properties`.

The game must come from your own `.ipa` of version **1.4** (`TBOI Rebirth_v1.4.ipa`):

```bash
py -3 tools/setup_from_ipa.py "path/to/TBOI Rebirth_v1.4.ipa"
```

The script puts the executable into `work/isaac.arm64` and `port/app/src/main/assets/`,
the rest of the app folder into `work/app/`, the `.ipa` itself into the project root
(the build checks the pack against it as the original), and verifies the image by
SHA-256 (`tools/image.sha256`). **A different build of the game means different
addresses**: everything said below about addresses holds only for this image.

One more part of the game is not in the archive: the tables of the PC version
**Repentance+ 1.9.7** (`achievements.xml`, `items.xml`, etc.). `content.c` uses them
for a three-way merge of mods made for Repentance+. The port builds and runs without
them; such mods just merge worse. To restore them: extract the resources of your
Steam copy with its `tools/ResourceExtractor` into
`work/pc-resources-repplus/resources/` and run `py -3 tools/port_patch.py --make`;
the tables land in `assets/port_patch/port-version-ref/`.

## 2. Build (Android)

```bash
py -3 tools/build.py              # report build: tboi-report.apk (with diagnostics)
py -3 tools/build.py --release    # clean build: tboi.apk
```

Build only through `build.py`; do not call Gradle by hand. It rebuilds the pack when
resources are newer, deletes the old APK (otherwise the pack is appended to the old
file: 2.7 GB instead of 1.4), checks the APK, and with `--release` turns the report
off and checks that no diagnostic strings remain in the library. Requirements:
Python 3 (Pillow, numpy; fontTools for the Arabic font), Android SDK 35 and NDK 27.
The SDK path goes into `port/local.properties` (`sdk.dir=...`; the file is
per-machine and not in the archive) or into the `ANDROID_HOME` variable.

Two builds: the **report** build (`Debug`, `ISAAC_DIAG=1`, flag `MainActivity.REPORT`)
writes `Downloads/isaac-start.txt` with frame timings, crashes and self-checks; the
**clean** build (`RelWithDebInfo`, `ISAAC_DIAG=0`) is for playing. Every change must
work in the clean build.

## 3. How the layer is built: four parts

### 3.1. iOS replacement layer

This part stands in for what iOS provides. It is also the most precise description
of **exactly what the game asks of iOS**.

| File | What it does |
|---|---|
| `loader.c` | Mach-O loader: segment mapping, chained fixups, import binding, ObjC class checks; **all patches to the game's code live here too** (see 3.2) |
| `objc.c`, `objc_asm.S` | our own Objective-C runtime: `objc_msgSend`, classes, selectors; a miss is logged as `NO METHOD [Class selector]` |
| `uikit.c` | UIKit and Foundation: exactly what the game calls |
| `shim.c` | other iOS/libc functions, files, `shim_safe_fopen` |
| `darwin.c`, `darwin_jmp.S` | pieces of libc that only Apple has (`__sincos_stret`, `setjmp/longjmp` with the Apple layout) |
| `cxxstr.c`, `cxxstream.c` | `std::string` and C++ streams in the Apple libc++ layout (`std::__1`) |
| `gl.c` | EAGL → EGL: the game's renderbuffer becomes the window |
| `audio.c` | OpenAL on top of AAudio |
| `pad.c`, `devices.c` | GameController (`GCController`) on top of Android events, the device book |
| `pack.c` | resources read straight from the APK without unpacking |
| `safefile.c`, `saves.c` | file writes (.tmp → fsync → rename), save migration and import |
| Java: `MainActivity`, `InputBook`, `PadDb`, `JoyCons`, `KeyMap`, `Haptics`, `MediaFs`, `PortDirs`, `ExitReasons`… | window, input, vibration, the port folder `Android/media/com.tboi.rebirth/` |

### 3.2. Patches to the game's code

The port changes the game's behaviour in these ways:

- **A word in the image.** `loader.c patch_word(offset, was_a, was_b, becomes)`: before
  writing, it checks that the expected instruction is at the address; otherwise the
  patch is silently skipped. The `__TEXT` page is made writable only for the duration
  of the write, then the instruction cache is flushed. There are dozens of patches;
  each one is annotated in the code: what, why, which words.
- **Hook with continuation.** `trace.c`: the first four instructions of a function are
  moved to our page, and a branch to the handler is placed at the function's start;
  the handler does its work and returns into the copy. The handler is selected by a
  "tag", a letter or digit (`'O'` is end of frame, `'X'` is the HUD pass, etc.). A
  function that starts with `adrp` cannot be hooked (that instruction depends on its
  own address).
- **Trampolines** in the dead body of the purchase check `0x8dab18..0x8dab9f`: short
  branches from the game's code into ours (`coop.c`, sound, sticks). No free slots
  remain.

All addresses are **offsets from the image base** (`pc − base`; the base differs on
every launch, and the game writes it to the log). The image holds **three copies of
the engine**: Afterbirth, Afterbirth+ and Repentance (which one runs is at
`image+0xabec34`); Repentance code works with the manager `[0xac2428]`, `[0xac1768]`
is the AB+ copy. A patch goes either to all copies or to Repentance only.

### 3.3. Remaster features

This is C code that reads the game's structures and calls its functions by address.
It depends on Android only through our layer (touch input, drawing through the
game's functions, settings files).

| File | Feature |
|---|---|
| `eid.c`, `eid_craft.c`, `eid_glitch.c`, `eid_*.h` | item descriptions (like the EID mod): panel, 21 languages, right-to-left Arabic, Bag of Crafting, TMTRAINER items |
| `spawner.c`, `spawner_stats.c`, `spawner_teleport.c`, `spawner_*.h` | CONSOLE (formerly the spawner): item search and spawning, stats, teleport between rooms and floors |
| `tweaks.c`, `tweaks_layout.h` | the Tweaks screen: all port settings, styled like the game's menus |
| `menu.c`, `straps.c`, `credits.c` | our rows in the options menu, straps under the menu papers, port credits |
| `screen.c`, `stretch.c`, `bloom.c` | canvas and scene scale (×1 and up, POTATO/ORIGINAL/ENHANCED modes), sharp stretching, bloom at the iOS size |
| `hud.c`, `hudlayout.c`, `hudgeom.c/.h` | HUD: offset without breaking touch input, co-op HUD layout as in Repentance+ |
| `coop.c` | second-player join (the game's own handler `0x567434` + our answer to "is join held") |
| `sticks.c`, `stickcurve.h`, `touchedit.c` | touch sticks, response curve, in-game touch layout editor |
| `items.c`, `pocket.c`, `bag.c`, `marked.c`, `twintrinkets.c`, `reset.c` | inventory on long-tapping the map, dropping a card/pill, School Bag, Marked, Jacob & Esau trinkets, run reset |
| `rumble.c` | game vibration → haptic feedback |
| `mods.c`, `modset.c`, `statsplus.c`, `bossbars.c`, `rangefix.c` | PC mods without Lua: Boss Rush Wave Counter, Stats+, Enhanced Boss Bars, Range Fix and others |
| `usermods.c`, `content.c`, `json.c`, `portpatch.c`, `modcheck_jni.c` | user resource mods, building data tables from the game and mods, applying `port_patch/` |
| `text.c` | the engine reads UTF-8 (new languages: Chinese, Korean, Arabic in EID) |

### 3.4. Diagnostics (report build only)

`gpu.c` (GPU timings per pass), `texel.c` (pixel grid), `lifediff.c`,
`probe.c` (searching for fields in memory), parts of `trace.c`/`objc.c`/`gl.c` under `ISAAC_DIAG`.

## 4. Resources

- **Game files are not changed.** Since version M8, `work/app` is byte-for-byte the
  files from the `.ipa` (except `stringtable.sta`). Our changes live in
  `work/port-edit/`; the registry is `tools/port_additions.py` (what is changed in
  which file and what depends on it); `tools/port_patch.py --make` turns them into
  `assets/port_patch/`: `.anm2` patches, our own sheets, our versions of a few files.
  `portpatch.c` applies them at startup.
- **Our own resources** are `assets/port_*` (menus, Tweaks, CONSOLE, guide, credits,
  icons), built by `tools/*_page.py`, `tools/guide_*.py`, `tools/spawner_*.py` and
  others. Labels exist in 7 sets: en ru de es jp kr zh.
- **The description database** `assets/eid.bin` (format `EIDB`, 21 languages) is built
  from `eid-mod/`; new tables via `tools/eid_overlay.py`, typo fixes via
  `tools/eid_fixes.py`, Arabic via `tools/eid_arabic.py --into-bin` and the font via
  `tools/eid_arabic_font.py`. **Rebuild only on top of the working `eid.bin`.**
- Rules: draw only at 100% (bake any downscaling in advance, LANCZOS); make new
  animations as copies of existing ones (the engine addresses layers by index); never
  rename the game's animation names.

## 5. How to search the image

| Tool | What it does |
|---|---|
| `tools/dis.py ADDRESS N` | disassembly |
| `tools/xref.py ADDRESS` | who builds this address (`adrp+add` / `adrp+ldr`) |
| `tools/into.py START END` | all branch targets inside a range; run before any patch |
| `tools/callers.py`, `who.py`, `gotname.py`, `objcmeth.py`, `fixups.py` | callers, import names, ObjC methods, fixups |
| `tools/simload.py` | image loader in Python (the prototype of `loader.c`) |

A hook takes five words; never place one inside a jump-table block.

## 6. File index

The first header line of every source, as written in the code (`tools/map_index.py`).

### `port/app/src/main/cpp/` (91)

| File | Header |
|---|---|
| `audio.c` | Audio: an OpenAL layer on top of AAudio. |
| `bag.c` | The School Bag: switching the two active items without using one. |
| `bagswipe.h` | The Bag of Crafting's screen under a finger: a sideways swipe turns a page, |
| `bloom.c` | The bloom composite, drawn at the size iOS draws it. |
| `bossbars.c` | ENHANCED BOSS BARS, WITHOUT LUA. |
| `bossbars_data.h` | generated by tools/mod_bossbars.py from the Enhanced Boss Bars mod - do not edit |
| `content.c` | DATA TABLES BUILT FROM THE GAME'S FILE AND THE MODS (plan 30.09, 4.9 M3, |
| `content.h` | content.c - a data table of the game, built from the game's file and the |
| `content_table.h` | generated by tools/contentdesk/gen_table.py from tools/content_sim.py (the key table, T) and |
| `coop.c` | The game's own local co-op, watched - report builds only (27.09.2026). |
| `credits.c` | THE PORT'S CREDITS: the plaque on the main menu, and the page it opens. |
| `cxxstr.c` | std::string, Apple style. |
| `cxxstream.c` | The C++ streams the game needs, in the layout the game was built for. |
| `darwin.c` | The pieces of libc that only Apple has. |
| `darwin_jmp.S` | setjmp / longjmp with Apple's jmp_buf, for the game's libpng (02.10.2026). |
| `devices.c` | THE BOOK OF DEVICES, THE LAYER'S SIDE (batch 2, U4, 01.10.2026). |
| `devices.h` | The layer's book of the game's input devices (devices.c, batch 2, U4). |
| `diag.h` | One switch for all the diagnostics. |
| `eid.c` | External Item Descriptions: what the thing on the pedestal actually does. |
| `eid_boxes.h` | Generated by tools/eid_boxes.py - do not edit. |
| `eid_charge.h` | Cards and pills: what kind, and what a mimic pays for them. |
| `eid_craft.c` | BAG OF CRAFTING: what the pickups around can be crafted into. |
| `eid_doors.h` | Written by tools/eid_entities.py from entities2.xml - do not edit. |
| `eid_entities.h` | Written by tools/eid_entities.py from the EID mod's EID.XMLEntityNames - do not edit. |
| `eid_glitch.c` | GLITCHED ITEMS: what a TMTRAINER collectible does, in words. |
| `eid_icons.h` | Generated by tools/eid_icons.py from the mod's own Lua - do not edit. |
| `eid_luck.h` | What a luck-based chance is worth at this much luck. |
| `eid_rules.h` | The mod's conditional descriptions, one row per condition. |
| `eid_wisps.h` | A wisp's damage: (damage + stage damage * (floor - 1)) * (30 / fire |
| `gl.c` | The real drawing surface: EGL instead of EAGL. |
| `gpu.c` | The GPU's own clock, and who spent it. Report builds only. |
| `guide_layout.h` | Written by tools/guide_slides.py - do not edit by hand, run the tool. |
| `hud.c` | The HUD offset, and the touch zones the game forgets to move with it. |
| `hudgeom.c` | The players' HUD as Repentance+ 1.9.7 lays it out offline - see hudgeom.h. |
| `hudgeom.h` | The players' HUD as Repentance+ 1.9.7 lays it out offline (local co-op), for |
| `hudlayout.c` | THE PLAYERS' HUD AS REPENTANCE+ LAYS IT OUT OFFLINE (the owner's word, |
| `items.c` | The pause inventory: everything the player is carrying, on a long press of |
| `json.c` | json.c - a small strict JSON reader (RFC 8259) and mod.json on top of it |
| `json.h` | json.h - a small strict JSON reader (RFC 8259) and the mod.json reader on |
| `lifediff.c` | WHAT A MINIMISE CHANGES IN THE RUN. Report builds only (the owner's item 5, |
| `loader.c` | A Mach-O image loader for Android. |
| `marked.c` | Marked on the touch screen: a button of our own in the overlay's row. |
| `menu.c` | Options menu: the order of the rows and the rows we add ourselves. |
| `modcheck_jni.c` | modcheck_jni.c - the installer's judge of mod.json (plan 30.09, M6 and |
| `mods.c` | TWO PC MODS, WITHOUT LUA. |
| `modset.c` | THE SETTINGS OF THE PORTED MODS, in sections of port-touch.ini. |
| `modset.h` | The settings of the ported mods, as they stand in the sections of |
| `objc.c` | The Objective-C runtime, stage one: the talking one. |
| `objc_asm.S` | Message dispatch: the parts that cannot be written in C. |
| `pack.c` | The game's resources, straight out of the APK, with no unpacking. |
| `pad.c` | Gamepads: a GameController surface on top of Android events. |
| `pages.h` | The moves a pad or a keyboard makes on one of the port's own pages. They are |
| `pocket.c` | Dropping a card or a pill from the touch screen: a long press on its button. |
| `portpatch.c` | The port's additions laid over the game's own files at start (plan 30.09, |
| `probe.c` | Memory inspector: finding the game's fields inside the live process. |
| `rangefix.c` | RANGE FIX, WITHOUT LUA (04.10.2026). |
| `reset.c` | Resetting a run from the touch screen (a long press on the pause button) and |
| `rumble.c` | RUMBLE, given back (the owner's list, item 18, 23.09.2026), and made into |
| `safefile.c` | The one writer for every file that holds the player's state (P1, 30.09.2026), |
| `saves.c` | A save file brought from somewhere else. |
| `screen.c` | The screen: the canvas the game lays out on, and what follows from its size. |
| `shim.c` | A stand-in for iOS: everything Android does not provide by itself. |
| `spawner.c` | THE ITEM SPAWNER - the search. |
| `spawner_bosses.h` | Written by tools/spawner_bosses.py - do not edit by hand, run the tool. |
| `spawner_enemies.h` | Written by tools/spawner_enemies.py - do not edit by hand, run the tool. |
| `spawner_floors.h` | Written by tools/spawner_floors.py - do not edit by hand, run the tool. |
| `spawner_layouts.h` | Written by tools/spawner_layouts.py - do not edit by hand, run the tool. |
| `spawner_pickups.h` | Written by tools/spawner_pickups.py - do not edit by hand, run the tool. |
| `spawner_rooms.h` | Written by tools/spawner_rooms.py - do not edit by hand, run the tool. |
| `spawner_stats.c` | THE STATS TAB OF THE SPAWNER - an exact value for a stat (the owner, |
| `spawner_strings.h` | Written by tools/spawner_stats.py - do not edit by hand, run the tool. |
| `spawner_tabs.h` | Written by tools/spawner_tabs.py - do not edit by hand, run the tool. |
| `spawner_teleport.c` | THE TELEPORT TAB OF THE SPAWNER (the owner, 30.09.2026): every room type in |
| `spawner_title.h` | Written by tools/itemspawn_title.py - do not edit by hand, run the tool. |
| `statsplus.c` | STATS+, WITHOUT LUA. |
| `stickcurve.h` | THE MOVEMENT STICK'S OUTPUT: THE DEAD ZONE, THE SENSITIVITY AND DIGITAL |
| `sticks.c` | The sticks: where they sit, whether they are visible, and whether we hold |
| `strap_sizes.h` | Written by tools/strap_lang/strap_install.py - do not edit by hand, run the tool. |
| `straps.c` | THE PORT'S STRAPS AS MENU ITEMS. |
| `stretch.c` | THE LAST STRETCH: the picture onto the window, with no walking texel. |
| `texel.c` | WHERE THE QUADS LAND ON THE PIXEL GRID. Report builds only (14.10.2026). |
| `text.c` | Text: teaching the engine to read UTF-8. |
| `touchedit.c` | THE TOUCH CONTROLS EDITOR: the way in, and the empty place it opens on. |
| `touchedit_tools.h` | Written by tools/editor_tools_sheet.py - do not edit by hand. |
| `trace.c` | A hook that continues: find out whether one of the game's functions is |
| `trinket_art.h` | Generated by tools/trinket_art.py - do not edit by hand. |
| `tweaks.c` | THE TWEAKS SCREEN, opened from a strap on the settings screen. |
| `tweaks_layout.h` | Written by tools/tweaks_page.py - do not edit by hand, run the tool. |
| `twintrinkets.c` | Jacob and Esau: one drop button, both twins' trinkets on it (04.10.2026). |
| `uikit.c` | A stand-in for UIKit and Foundation: only what the game asks for. |
| `usermods.c` | USER MODS: THE RESOURCE MODS A PLAYER ADDS HIMSELF (the owner, 23-25.09.2026). |

### `port/app/src/main/java/ru/isaac/port/` (19)

| File | Header |
|---|---|
| `ExitReasons.java` | WHY THE LAST RUN ENDED, AS THE SYSTEM SAW IT - in every build (02.10.2026). |
| `FileKinds.java` | THE KINDS OF FILE THE EXPORT OFFERS, ONE TABLE (plan 30.09, M12). The |
| `GameFiles.java` | THE GAME'S OWN FILES, OUT (the owner, 25.09.2026): what a player needs to |
| `Haptics.java` | THE ONE OWNER OF THE VIBRATORS (batch 3, P9-A/B/V, 01.10.2026). |
| `InputBook.java` | ONE BOOK OF THE INPUT DEVICES (batch 2, U4, 01.10.2026). |
| `JoyCons.java` | TWO JOY-CONS, ONE PAD (the owner's choice, 06.10.2026: "option A"). |
| `KeyMap.java` | GENERATED by tools/keymap.py - do not edit by hand, run the tool. |
| `MainActivity.java` | The port's reconnaissance: hand the game image to the loader and give it a |
| `MediaFs.java` | THE ONE RULE FOR WRITING A FILE, ON THE JAVA SIDE (P1, 30.09.2026) - the |
| `ModPort.java` | THE CONVERTER INSIDE THE INSTALLER (the owner, 04.10.2026): a mod not in the port's format - a PC mod |
| `ModPortPng.java` | PICTURES FOR THE CONVERTER INSIDE THE INSTALLER (ModPort, 04.10.2026), row by row: a phone has |
| `ModPortXml.java` | XML AS tools/modport.py SEES IT (ModPort, the converter inside the installer, 04.10.2026). |
| `PadDb.java` | A PAD'S LAYOUT, THE WAY SDL KNOWS IT (the owner's decision, 02.10.2026). |
| `PadIcons.java` | WHICH PAD PICTURES A PAD GETS (the owner, 1.72): the GUIDE's slides show a |
| `PortDirs.java` | THE PORT'S FOLDER, ONE PER PROCESS (P3, 30.09.2026): Android/media/<app>. |
| `PortFilesProvider.java` | THE PORT'S FILES IN THE SYSTEM'S OWN FILE MANAGER (the owner's item 14, |
| `RestartActivity.java` | THE GAME STARTED AGAIN AFTER AN IMPORT (the owner's eighth round, 24.09.2026: |
| `SaveConverter.java` | THE PERSISTENT SAVES, IN AND OUT (the owner's item 9, 23.09.2026). |
| `UserMods.java` | ADDING A USER MOD (the owner, 25.09.2026): a zip in the port's own format, |

### `tools/` (183)

| File | Header |
|---|---|
| `aimlock_ru.py` | Shrink ON/OFF in the Russian AIM LOCK — this time properly. |
| `anm2patch.py` | The port's additions to the game's .anm2 files, kept as PATCHES (plan 30.09, 4.9 M8). |
| `anm2render.py` | Render one frame of an Isaac .anm2 animation into an RGBA image (tools/spawner_enemies.py). |
| `appicon.py` | The launcher icon, in the two shapes Android wants. |
| `asset_tails.py` | THE APK'S ASSETS CARRY NO REFERENCE COPIES (plan 30.09, M12 p.6). |
| `build.py` | THE BUILD, one command (the owner, 25.09.2026: the resources got packed in |
| `callers.py` | Who calls whom: the start of the function at an address, and its callers. |
| `content_sim.py` | The reference model of content.c: how a data table of Repentance is built |
| `credits_lang.py` | The PORT CREDITS page in every language set (the owner, 30.09.2026: the page |
| `credits_menu.py` | The PORT CREDITS plaque into the main menu's sheet. |
| `credits_page.py` | The PORT CREDITS page: the grown sheet with the words set into it. |
| `credits_paper.py` | The torn sheet made longer by LENGTHENING ITS LEVEL RUNS. |
| `dis.py` | Disassemble a function of the image with hints: strings from adrp+add and call targets. |
| `dpad_art.py` | The touch D-pads (the owner's item 19, 23.09.2026): the game's own DPad of |
| `editor_buttons_preview.py` | Variants of the touch editor's four tool buttons, to be chosen by eye (the |
| `editor_tools_sheet.py` | The touch editor's four tool buttons: an icon over a word, in the game's |
| `effective.py` | What the game would load, before M8 and after it - and is it the same picture (plan 30.09, 4.9 M8). |
| `eid_additions.py` | Translated rows back into eid.bin. |
| `eid_arabic.py` | Arabic for the descriptions: the "Arabic EID" mod made into an EID language. |
| `eid_arabic_font.py` | The Arabic face of the descriptions: eid_default with the Arabic letters added. |
| `eid_boxes.py` | Where every EID picture's drawing really is, and how much of it is drawn. |
| `eid_charge.py` | What a card or a pill is, and what it costs a mimic, out of the game's XML. |
| `eid_entities.py` | The entity names EID prints in a glitched item's description. |
| `eid_fixes.py` | Mistakes in the description texts, put right in eid.bin itself. |
| `eid_font_preview.py` | Draw real description text with a BMFont, so a face can be judged by eye. |
| `eid_glyphs.py` | Which characters a language needs, and whether the font has them. |
| `eid_icon_sizes_preview.py` | The EID's pictures as they are and as proposed, at the phone's own scale. |
| `eid_icons.py` | The mod's markup tables, turned into a C table the port can read. |
| `eid_lang.py` | What the mod's language packs are missing, and what we translate ourselves. |
| `eid_luck.py` | The mod's luck formulas, turned into C. |
| `eid_more.py` | Extra tables from the mod's own Lua, appended to eid.bin. |
| `eid_names.py` | Item names in the game's own words, for the languages the mod has not got. |
| `eid_overlay.py` | Lay a few sections of one eid.bin over another, and nothing else. |
| `eid_pack.py` | External Item Descriptions: the JSON database into a form the port can read. |
| `eid_rules.py` | The mod's conditional descriptions, turned into a table the port can walk. |
| `eid_todo.py` | What each description language lacks, cut into pieces for translators. |
| `eid_trim.py` | Throw out of eid.bin the languages the game cannot be set to. |
| `eid_wisps.py` | Book of Virtues and Abyss: the blocks they add, built ahead of time. |
| `fixups.py` | Parsing the Mach-O chained fixups (LC_DYLD_CHAINED_FIXUPS). |
| `gap.py` | Where a row's name ends on screen and its value begins. |
| `gotname.py` | The name of an external function, from the address of its slot in the jump table. |
| `greedcheck.py` | What a persistent save says about Greedier - read only, nothing is written. |
| `guide_check.py` | THE GUIDE'S FIT CHECKER, for one language's words (the owner, 06.10.2026: the |
| `guide_preview.py` | The in-game controls guide (the owner's item 22): a picture of its slides to |
| `guide_slides.py` | The in-game controls guide: its slides, built (the owner's item 22, the look |
| `guide_strap.py` | The GUIDE strap: a second strap on the settings paper, under TWEAKS (the |
| `guide_text.py` | The controls guide's words, one set per language of the game's menus (the |
| `into.py` | Who branches INTO a given region of code. |
| `itemspawn_title.py` | The ITEM SPAWN heading of the spawner's card, in the game's own hand. |
| `jpglyphs.py` | A library of Japanese characters cut out of the game's own captions. |
| `keymap.py` | The hardware keyboard by the PLACE of a key: Linux scan code -> HID usage. |
| `labels.py` | The owner's labels into the atlas: shrink, edge, place, fix the markup. |
| `lang.py` | A whole language: the menu labels and the title phrase. |
| `m8_vanilla.py` | work/app goes back to vanilla; the port's versions move to work/port-edit (plan 30.09, 4.9 M8). |
| `main_menu_mods.py` | The main menu's MODS item, put back the PC's way, in all seven sets. |
| `map_index.py` | The file index at the end of MAP.md, written from the files themselves. |
| `menu_fonts.py` | THE PORT'S OWN COPIES OF THE GAME'S MENU FONTS (the owner, 05.10.2026). |
| `menu_safe.py` | Where a new strap may be drawn on a menu sheet, measured rather than guessed. |
| `mod_bossbars.py` | Turn the "Enhanced Boss Bars" mod into port assets and a C table. |
| `mod_specialist.py` | Stage the "Specialist for Good Items" mod's files as port assets. |
| `modport.py` | modport: a PC mod of The Binding of Isaac: Repentance into a mod for the Android port. |
| `modport_ref.py` | modport_ref: the game reference the web converter (other stuff/usermods/modport-web) reads. |
| `modport_web.py` | modport_web: the web converter as ONE html file, so it opens from a disk, a chat |
| `mods_popup.py` | THE CONFLICT POPUP'S PICTURE (the owner, 05.10.2026: variant A of |
| `modset_pages.py` | The settings pages of the ported mods: one page a mod, behind its arrow on |
| `modsettings_preview.py` | One player's mod's page with the mod's own settings - a picture to agree on |
| `objcmeth.py` | Methods of an Objective-C class with their addresses in the image. |
| `optwall.py` | The options paper's backing, stretched to match the longer scroll. |
| `pack.py` | The game's resources as one blob in the APK assets. |
| `paddb.py` | THE GAMEPAD MAPPING DATABASE FOR ANDROID (the owner's decision, 02.10.2026). |
| `pcexe.py` | The PC game's isaac-ng.exe for the desk: file offset <-> VA, references to an address, disassembly (capstone). |
| `popupedges.py` | Every edge of the game's popup paper, on every canvas the port makes. |
| `port_additions.py` | The registry of what the port adds to the game's own files (plan 30.09, M8 and M12). |
| `port_patch.py` | The port's additions to the game's files as patches and port files (plan 30.09, 4.9 M8). |
| `ptrs.py` | The image's own pointers (chained-fixup rebases): where they stand and what |
| `retype.py` | Retype OUR added labels in the game's font and lay them out along the line. |
| `rumble_row.py` | THE GAME'S FILTER ROW BECOMES RUMBLE (hotfix 1.75, the owner): the port's |
| `setup_from_ipa.py` | Lay out the game's own files from the IPA, so a fresh tree builds. |
| `share_zip.py` | — |
| `simload.py` | A trial load of the image: map it, apply the fixups, read the classes. |
| `smoothing.py` | How this game smooths a drawn letter, counted off the untouched sheets. |
| `spawner_bosses.py` | The bosses the spawner can put in a room, out of the game's entities2.xml. |
| `spawner_common.py` | What the spawner's resource tools share: the seven language sets, the game's |
| `spawner_enemies.py` | The spawner's ENEMIES tab: every monster of the game's bestiary, its icon, its |
| `spawner_floors.py` | The spawner's TELEPORT tab, floors column: every floor of a run, and the |
| `spawner_layouts.py` | The spawner's ENEMIES tab: bosses that are more than one entity, laid out |
| `spawner_pickups.py` | The spawner's ITEMS list beyond the collectibles (the owner, 07.10.2026, |
| `spawner_rooms.py` | The spawner's TELEPORT tab, rooms column: every room type the game has, with |
| `spawner_stats.py` | The spawner's STATS tab pictures and the card's own words in the seven sets. |
| `spawner_tabs.py` | The spawner card's tab row and the enemy list's section headings, in the |
| `stickmodel.py` | The touch stick on the desk: the knob and the output, the old way and the new. |
| `strap_layers.py` | The port's three straps, as LAYERS of the menus' own .anm2 files. |
| `strap_overlap.py` | Is anything of the game drawn OVER a port strap? Measured on what the game loads. |
| `stroka.py` | Draw a menu line the way the game draws it: with the frame's rotation. |
| `test_straps.py` | Placeholder straps on the menu sheets, so the buttons can be built and tapped. |
| `tool_icons.py` | The editor's four buttons into their sheet, by the menu's own procedure. |
| `touchedit_desk.py` | The touch editor's slider and pause button on the desk (hotfix 1.71). |
| `trinket_art.py` | Where the art of each trinket lies inside its 32x32 picture -> cpp/trinket_art.h. |
| `tweaks_page.py` | The Tweaks screen in every language the game has, built with the tools that |
| `twin_buttons.py` | The twins' round buttons, one picture per pair of characters (the owner's |
| `twindesk.py` | Jacob & Esau on a pad or a keyboard: the game's own decisions, run under unicorn. |
| `typeset.py` | Typeset a label in the game's font so that it comes out like a drawn one. |
| `uncrush.py` | Apple's "crushed" PNG back into an ordinary one. |
| `upscale_preview.py` | What an edge-aware upscaler would do to the picture, rendered on the desk. |
| `usermods_preview.py` | The MODS page with the player's own mods, and one mod's page - a picture to |
| `watermark.py` | The port's signature, laid into the main menu's backdrop. |
| `watermark_options.py` | The port's signature, laid into the OPTIONS screen. |
| `who.py` | Direct calls of the given address: BL and tail B. |
| `xref.py` | Who in the code assembles the given address (adrp+add / adrp+ldr). |
| `zipfull.py` | Build one zip that carries the whole port: sources and every resource. |
| `zipsrc.py` | Build a zip with the port's sources. |
| `contentdesk/gen_table.py` | content_table.h for content.c, generated from the reference model's key table. |
| `contentdesk/run.py` | The desk check of content.c against its spec (tools/content_sim.py), byte for byte. |
| `contentdesk/test_device.py` | content.c on the umdesk stand (the real usermods.c + content.c for Windows): the device side. |
| `convdesk/compare.py` | A third converter's output held to the expected one (04.10.2026). |
| `convdesk/corpus.py` | The parity corpus of the converter (04.10.2026): what goes in, how an output is described. |
| `convdesk/make_expected.py` | The expected outputs of the converter, from the two reference converters (04.10.2026). |
| `eidlendesk/mockups.py` | Pictures of the ways a too-long EID panel could be shown (07.10.2026). |
| `eidlendesk/run.py` | The EID length desk (07.10.2026): the REAL eid.c built for Windows with host |
| `guide_lang/de.current.py` | The guide's words as tools/guide_text.py has them now: set 'dlc3.de' (de). |
| `guide_lang/de.py` | The guide's words in German (set 'dlc3.de'), written natively (stage 2, |
| `guide_lang/en.current.py` | The guide's words as tools/guide_text.py has them now: set 'data' (en). |
| `guide_lang/en.py` | The guide's words in English (set 'data'), rewritten in plain words (stage 1, |
| `guide_lang/es.current.py` | The guide's words as tools/guide_text.py has them now: set 'dlc3.es' (es). |
| `guide_lang/es.py` | The guide's words in Spanish (set 'dlc3.es'), written natively for players in |
| `guide_lang/jp.current.py` | The guide's words as tools/guide_text.py has them now: set 'dlc3.jp' (jp). |
| `guide_lang/jp.py` | The guide's words in Japanese (set 'dlc3.jp'), written natively (stage 2, |
| `guide_lang/kr.current.py` | The guide's words as tools/guide_text.py has them now: set 'dlc3.kr' (kr). |
| `guide_lang/kr.py` | The guide's words in Korean (set 'dlc3.kr'), written natively (stage 2, |
| `guide_lang/ru.current.py` | The guide's words as tools/guide_text.py has them now: set 'dlc3.ru' (ru). |
| `guide_lang/ru.py` | The guide's words in Russian (set 'dlc3.ru'), written natively (stage 2, |
| `guide_lang/zh.current.py` | The guide's words as tools/guide_text.py has them now: set 'dlc3.zh' (zh). |
| `guide_lang/zh.py` | The guide's words in Simplified Chinese (set 'dlc3.zh'), written natively |
| `hudesk/draw.py` | Draw desk cases (tools/hudesk/cases.json, written by desk.exe from the real |
| `hudesk/hudcheck.py` | The players' HUD: the port's calls through the image's renderers against the PC game's own draw. |
| `hudesk/hudemu.py` | Runs the iOS image's own player-HUD renderers under unicorn and records what they draw. |
| `hudesk/pcemu.py` | Runs the PC game's (isaac-ng.exe, Repentance+ 1.9.7, 32-bit) player-HUD code under unicorn. |
| `hudesk/pocket_options.py` | Where the touch pocket buttons could stand in co-op: option pictures for the |
| `portdesk/pixels.py` | The port's retouch of gamemenu.png laid over whichever sheet wins (02.10.2026, the M8 regression). |
| `portdesk/run.py` | portpatch.c against its twin tools/anm2patch.py, byte for byte (plan 30.09, 4.9 M8). |
| `resetdesk/run.py` | The reset desk (08.10.2026): the REAL reset.c built for Windows with host clang |
| `spawnerdesk/adders.py` | The game's own heart adders run on unicorn over a fake player and a fake |
| `spawnerdesk/gen_tables.py` | The spawner desk's data, from the port's own assets: the description |
| `spawnerdesk/health_types.py` | The game's own GetHealthType (0x4eeae4) run on unicorn for every player |
| `spawnerdesk/run.py` | The spawner desk (07.10.2026): the REAL spawner.c and spawner_stats.c built |
| `strap_lang/pause_variants.py` | The pause screen for Korean and Chinese: the full word in place, the full word |
| `strap_lang/strap_base.py` | PREVIEW ONLY: the four straps in ru, de, es, jp, kr, zh, by the port's own |
| `strap_lang/strap_install.py` | The straps of every language into the game's files - and nothing else. |
| `strap_lang/strap_lang.py` | Straps in six languages, filling the area the owner's own lettering fills. |
| `strap_lang/strap_onscreen.py` | The lengthened straps laid on the owner's own mock-ups, under the paper the |
| `test_bagswipe/run.py` | Build and run the desk test of the Bag of Crafting's swipe and of the held |
| `test_bloom173/run.py` | Desk tests of the bloom redirect and the pass clock (08.10.2026). Host clang |
| `test_dance/run.py` | Desk test of the Specialist Dance on every way out of a run (1.75). mods.c's |
| `test_perf172/phase.py` | THE GRID ON THE PANEL'S BEAT (20.10.2026): what reaches the screen, not what is drawn. |
| `test_perf172/rates175.py` | Hotfix 1.75, THE GAME'S OWN CLOCK: the owner's acceptance on the desk, in one short run. |
| `test_perf172/restore_phaselock.py` | Re-applies THE GRID ON THE PANEL'S BEAT to objc.c (from objc.c.before-phaselock) - 21.10.2026, after objc.c |
| `test_perf172/run.py` | Desk tests of hotfix 1.72's performance items. Host clang with the MSVC |
| `tweaksdesk/test_list256.py` | THE MODS PAGE WITH 256 MODS (hotfix 1.75, MODS_MAX 64 -> 256; usermods.c's own side: tools/umdesk/test_list256.py). |
| `tweaksdesk/test_longnames.py` | Desk cases for long names on the MODS list (the owner, 06.10.2026: a long name blew the page past both edges of the |
| `tweaksdesk/test_multimod.py` | ADD MOD while the MODS page is open, with no restart after it (hotfix 1.73), on the real tweaks.c + usermods.c |
| `tweaksdesk/test_page.py` | Desk cases for a player's mod's page on MODS with the mod's own settings (plan 4.10, part 2; the owner's order of |
| `tweaksdesk/test_popup.py` | Desk cases for a mod's page without the wall of notes, its details and the conflict popup (the owner's choices of |
| `tweaksdesk/test_restart174.py` | RESTART TO APPLY on the MODS page after ADD MOD (hotfix 1.74), on the real tweaks.c + usermods.c |
| `tweaksdesk/test_restart175.py` | RESTART TO APPLY on the MODS page after ADD MOD, the owner's road (hotfix 1.75; usermods.c ADD MOD OWES ITS |
| `umdesk/compare_outputs.py` | Two converter outputs (zips) compared the way test_converters.py needs it. |
| `umdesk/export_inputs.py` | The export desk's inputs (run_export.sh): a pack (ISAACPK1, tools/pack.py's format) of a slice of work/app - |
| `umdesk/mods666.py` | The reader of mods.txt in the build of 1.666 (tboi.apk of 01.10.2026), copied from its code: |
| `umdesk/test_conflicts.py` | THE MODS OVER EACH OTHER (task 4 of 04.10.2026, the research notes of 04.10.2026 on mods at run time, section 4): the notes on the |
| `umdesk/test_converters.py` | ONE SOURCE OF TRUTH: the three converters on the same mods (04.10.2026). |
| `umdesk/test_guard.py` | Desk run of THE GUARD (usermods.c, plan 02.10 item 1) on real mods, checked against modcheck.py. |
| `umdesk/test_hotfix171.py` | Desk cases of hotfix 1.71 (05.10.2026, the owner: what worked in 1.666 works the same or better; a mod's file is |
| `umdesk/test_list.py` | Desk cases for mods.txt and the start's cleanup (plan 02.10, A1-A6, B3), on the real usermods.c. |
| `umdesk/test_list256.py` | THE LIST OF 256 (hotfix 1.75, the owner: MODS_MAX 64 -> 256; the game itself has no such limit), on the real |
| `umdesk/test_menu_whole.py` | A mod's menu .anm2, all or nothing (02.10.2026, the owner's concern for the mods people already made). |
| `umdesk/test_modport.py` | Desk check of the converter's size rule (the owner, 02.10.2026): a mod's picture of a file the port |
| `umdesk/test_multimod.py` | ADD MOD with no restart after it (hotfix 1.73), on the real usermods.c: the game runs on while the installer |
| `umdesk/test_order.py` | THE ORDER of the mods' folders, as on the PC (task 3 of 04.10.2026, the research notes of 04.10.2026 on mods at run time, section 3), |
| `umdesk/test_pairs.py` | THE PAIRS and what the page shows of a mod (the owner, 05.10.2026; the research notes of 05.10.2026 on how mod conflicts look), on the |
| `umdesk/test_real_mods.py` | What the real mods we keep get, file by file, after pack 5 (M8/M9, all-or-nothing menus, the strap's pixels). |
| `umdesk/test_restart174.py` | RESTART TO APPLY worked out, not set (hotfix 1.74, THE RESTART OWED in usermods.c), on the real usermods.c. |
| `umdesk/test_restart175.py` | RESTART TO APPLY after ADD MOD in the phone's own order of calls (hotfix 1.75; the owner on 1.74: "the restart |
| `umdesk/test_settings.py` | Desk cases for mod.json and the mods' settings (plan 30.09, 4.10 parts 1 and 2), on the real usermods.c + |
| `umdesk/test_title_ios.py` | The title's iOS sheet (plan 30.09, M9): what TitleMenu_iOS.png answers when mods touch the title. |
