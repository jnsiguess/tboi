# Isaac Repentance → Android

A port of **The Binding of Isaac: Repentance** from iOS to Android without source
code. Purchases and StoreKit are cut out; all DLC counts as purchased.

This file holds the rules that always apply. The structure of the port is
described in `MAP.md`.

## Working rules

- **Fix things only inside the application.** No ACLs, file attributes, `icacls`,
  `attrib`, `takeown`, registry or system settings.
- **No hacks.** Restore the real iOS behaviour instead of fitting a fix to the
  symptom. Almost every breakage here is a platform difference, not a game bug.
- **Code is English only**: identifiers, comments, log messages.
- **Measure, don't guess.** Every guess checked by a measurement saved hours;
  every unchecked one cost a crash and a rollback.

## Environment

- Python: only `py -3` (`python`/`python3` are Windows Store stubs).
- Shell: Git Bash. **Cyrillic variable names break in it**; use ASCII names. For
  paths passed to `adb shell`, `export MSYS_NO_PATHCONV=1` is required.
- `adb` from the Android SDK (`platform-tools/adb`).

**Build ONLY like this, with one command; never call gradlew by hand:**

```bash
py -3 tools/build.py              # pack, build, APK check, copy to tboi-report.apk in the project root
py -3 tools/build.py --install    # the same plus adb install -r
py -3 tools/build.py --release    # clean build to play and hand out: tboi.apk in the project root
```

`--release` sets `MainActivity.REPORT = false` for the duration of the build and
restores it afterwards, builds `assembleRelease`, and refuses if diagnostic
strings remain in `libisaacport.so`. The report build for test sessions is
`tboi-report.apk`, the clean one is `tboi.apk`; do not mix them up.

`build.py` does what was forgotten again and again when done by hand:
- **rebuilds the pack** itself when resources are newer (Gradle does not rebuild
  it: to Gradle it is one unchanged file, so a resource edit without `pack.py`
  went nowhere; an hour and a test round were lost, 2026-09-16);
- **deletes the old APK before building**: Gradle's incremental packaging appends
  the new pack to the old file and the old pack stays as dead weight, giving a
  2.76 GB APK instead of 1.40; this happened many times (2026-09-25);
- **checks the APK**: every file inside appears once, `resources.pak` is present
  and stored uncompressed, the file size equals the sum of its contents. Any
  mismatch is a refusal.

A normal APK is about 1.40 GB. If you see 2.7, it was built bypassing `build.py`.

The launch report (`Downloads/isaac-start.txt`, self-check, crash analysis) is
enabled by the `MainActivity.REPORT` flag. In the clean build it is `false`.

The `release` build is compiled as `RelWithDebInfo`; it has `ISAAC_DIAG=0` and
all probing turned off. **Check that a change lives outside the diagnostics**,
or it will fall off in the working build.

## Layout

```
port\app\src\main\cpp\   our layer: loader shim objc uikit gl trace cxxstr
                         audio darwin sticks menu safefile + objc_asm.S
work\isaac.arm64         the game image, Mach-O
work\app\                unpacked resources (~2.5 GB)
tools\                   dis.py xref.py who.py gotname.py objcmeth.py
                         callers.py fixups.py simload.py
menu-labels\             menu labels, atlases, reference copies, layout
```

On the device, resources live in `/data/data/com.tboi.rebirth/files/app/...`;
put files there with `run-as com.tboi.rebirth sh -c 'cat > path'`.
The port folder is `/sdcard/Android/media/com.tboi.rebirth/` (since 2026-09-28):
`port-touch.ini`, `mods/`, `shared/`, i.e. what people edit by hand;
`Android/data` is closed to file managers, `Android/media` is open. Saves and
`options.ini` are there too, in `saves/` (since 2026-10-01): one live copy plus a
silent backup inside the app (`files/save-backup/`, `saves.c`); cloud backup is
OFF (`allowBackup=false`); the folder root holds `.nomedia`. Everything is written
through `shim_safe_fopen/shim_safe_fclose` (Java: `MediaFs`): `.tmp` → fsync →
rename; if rename fails, `.ready` and an in-place write. Old locations migrate by
themselves on first launch (`PortDirs`, `MainActivity`).

## How to search

- `tools/dis.py ADDRESS N` disassembles, `tools/xref.py ADDRESS` shows who builds
  that address (`adrp+add` / `adrp+ldr`). An exact reference leads to the function
  faster than reading straight through.
- Compute image offsets as `pc − base`; the game writes the base to the log
  (`image: … landed at 0x…`). **The base differs on every launch**: take the crash
  and the base from the same run.
- **The image holds copies of the AB, AB+ and Repentance engines** (the edition is
  at `image+0xabec34`). Patch all copies at once, or Repentance only (the latter is
  less reliable). Repentance code works with the manager `[0xac2428]`;
  `[0xac1768]` is the AB+ copy.
- Log: `adb logcat -d -s isaac:*`. Our messages are in English.
- Before patching game code, collect **all branch targets** inside the range
  (`tools/into.py START END`): functions share tails, and a "free branch" does not
  mean free space.

## Image patches: mandatory checks

- **Verify the word before writing.** Every patch checks that the expected
  instruction is at the address and silently refuses if not. Several mistakes
  have already been caught this way.
- **Do not rename strings in the image.** Animation names are requested by
  unrelated code too, and it crashes without its animations. Replace the contents
  of same-named animations in `.anm2`, not the names.
- **Check encodings against a sample from the image itself**: take a similar
  instruction, decode its fields, assemble yours and compare with the original.
- **A hook takes five words.** It must not be placed inside a jump-table block:
  blocks can be three instructions long, and the hook's tail lands on the
  neighbouring code. This has already produced a `SIGBUS` and a broken `AIM LOCK`.
- **Patching at runtime requires opening the page.** After `protect()`, `__TEXT`
  is read-only: `mprotect` exactly for the duration of the write, back again, and
  `__builtin___clear_cache`; otherwise you get either `SEGV_ACCERR` or a patch that
  "works every other time".
- **Take over someone else's field only after checking that nobody uses it.** The
  field of an "absent" menu item may be read by another menu list or by unrelated
  code: search the image both for `movz w?, #offset` and for accesses through the
  object base.

## Resources: what is easy to break

- **Keep a reference copy.** Before editing an atlas or `.anm2`, put an untouched
  copy next to it. Names are English only: `*-REFERENCE.png`, `*.orig`, and a copy
  "as it was before a given edit" gets the suffix `name.png.before-what-changed`.
  `pack.py` filters them out by the `.before-` suffix, so a new kind of file does
  not leak into the pack the way shaders once did. One atlas has already been
  ruined and restored with difficulty.
- **The EID description database is built only on top of the working
  `assets/eid.bin`.** `work/eid-full.bin` predates the 2026-09-16 translations:
  rebuilding from it silently loses six languages. New tables go through
  `tools/eid_overlay.py`. Typos and untranslated phrases are the `TYPOS`/`OURS`
  tables in `tools/eid_fixes.py`, applied as the last step after the overlay
  (2026-10-07).
- **Draw only at 100%.** Any other scale samples between texels, and the image
  shimmers while scrolling. Bake all downscaling in advance (`LANCZOS`); in `.anm2`
  set `XScale=YScale=100`.
- **Make new animations as copies of existing ones.** The engine addresses a layer
  by its entry index and crashes if the entry is missing; a full set of layers is
  required.
- **A layer's only frame** must cover the whole length of the animation: `Delay`
  equals `FrameNum`.
- The game's own labels: ink `(54,47,45)` at alpha 255, the edge a different shade
  at alpha ~102, 15–19% halftone pixels.

## Gamepad

Implemented: the `GCController` surface in `pad.c`, key and axis forwarding from
`MainActivity`, d-pad, both sticks, triggers, `Menu`/`Options`. The device list is
shown in the `Controls` submenu. **Each pad is its own controller** (slots 0–3).
Slot objects carry a generation: a pad that comes back gets new objects and the
old ones go silent; the game detects a disconnect by the object disappearing from
the `controllers` array. **The keyboard is not a pad:** the game has its own
keyboard device (vtable `0xa94958`) with bindings in Controls, and keys reach it
through the wrapper (`keyToGame` → `shim_keys_deliver`). The keyboard as a fifth
controller took all the keys away from that device (2026-09-28); do not repeat.

**The game reads the d-pad through the `up/down/left/right` buttons, not the axes.**
Sticks are the opposite: through the `xAxis`/`yAxis` axes. They are easy to mix
up: both use the same class, `GCControllerDirectionPad`.

A selector miss is logged by the layer as `NO METHOD [Class selector]`; when
investigating any gamepad silence, look there first, the answer is usually there.

## Co-op

Join goes through the game's own handler `0x567434`, but its question "is join
held" (action 26) is answered by our layer (`loader.c coop_join_ours`, trampoline
`0x8dab28` → `coop.c shim_coop_join_asked`): "yes" for one pass after Select+Start
is held for 1.5 s (`pad.c`); on the keyboard, Tab+Esc (`objc.c key_gesture`; the
entry for the game's own keyboard device is `coop.c shim_coop_join_keyboard`).
While both buttons are held together the game sees neither; a single button is
passed to the game on its second read, without delay.

- **Join slot k belongs to player k** and is drawn by the HUD next to that
  player's hearts. The slots are in the player manager `Game+0x25c40`, `+0x5c`,
  stride `0x14`, not in `Game+0x5c`.
- **The device dispatcher gives player 0 to any pressed device that has no
  player**, including the one choosing a character. Its three press queries
  (`0x674380`, `0x67438c`, `0x674534`) go through the trampolines `0x8dab38`/
  `0x8dab48` into `coop.c`; for the choosing device the answer is "no".
- The trampolines live in the dead body of the purchase check `0x8dab18..0x8dab9f`
  (taken: `18` game log; `28` (split by `x0`: join for the input system, device
  assignment on Continue `0x88b9f0` for the player manager, 2026-10-05); `38`;
  `48`; `58` marks a run that used the spawner; `68` stick knob; `78` stick
  output; `88` sound cache; `0x8dab98` is not a trampoline but a data word, the
  touch-button opacity, 2026-10-05, `loader.c touch_alpha_words`).
  **No free slots remain:** the next one must find another dead body or share a
  trampoline by its arguments.
- Game functions (seat a player, open join) are called **only from the frame
  thread**; pad state arrives from the Java thread.
