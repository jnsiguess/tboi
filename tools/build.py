# -*- coding: utf-8 -*-
"""THE BUILD, one command (the owner, 25.09.2026: the resources got packed in
twice again and again). Every step the build has had to be remembered for:

  1. the pak: repacked when anything it is made of is newer, or when it is not
     whole (tools/pack.py --check walks it to its end: a build stopped while
     the pak was written left 167 MB of 1300 that looked fresh); Gradle never
     rebuilds it - to Gradle it is one file that did not change;
  2. the old APK deleted first: Gradle's incremental packaging writes the new
     1.3 GB pak into the old file and leaves the old one inside as dead space
     (2.76 GB instead of 1.40 - measured: every entry once, the rest dead);
  3. assembleDebug;
  4. the APK checked: its entries must add up to its size (no dead space) and
     resources.pak must be inside, stored;
  5. copied to tboi-report.apk in the project root.
  0 comes first: the pak's game files must be the IPA's (vanilla_pak, M8).

    py -3 tools/build.py            # all of it
    py -3 tools/build.py --install  # and adb install -r, if a phone is connected
    py -3 tools/build.py --release  # the clean build to play and hand out: tboi.apk in the project root

THE CLEAN BUILD (--release) is the same road with three more steps:
  - MainActivity.REPORT is set to false for the build and put back after it,
    whatever happens: no report in Downloads, no crash writer;
  - assembleRelease: the native layer as RelWithDebInfo with ISAAC_DIAG=0;
  - the built library is checked for the diagnostics' own strings - none may
    be left (the check the owner asked for on 11.09.2026, by the library, not
    by trusting the settings).
"""
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = os.path.join(ROOT, 'port')
APK = os.path.join(PORT, 'app', 'build', 'outputs', 'apk', 'debug', 'app-debug.apk')
OUT = os.path.join(ROOT, 'tboi-report.apk')
RELEASE_APK = os.path.join(PORT, 'app', 'build', 'outputs', 'apk', 'release', 'app-release.apk')
RELEASE_OUT = os.path.join(ROOT, 'tboi.apk')
ACTIVITY = os.path.join(PORT, 'app', 'src', 'main', 'java', 'ru', 'isaac', 'port', 'MainActivity.java')
REPORT_ON = 'private static final boolean REPORT = true;'
REPORT_OFF = 'private static final boolean REPORT = false;'
# strings only the diagnostic build carries (the handoff notes, section on checking the clean build).
# Not "NO METHOD": objc.c writes it to the tester's report too, which a release
# build may carry - with REPORT false that report is never opened, and it is silent
DIAG_STRINGS = [b'TRACE:', b'bag: pressed', b'the run is over - stick',
                # batch 1 of 30.09.2026: unlocks (spawner.c, statsplus.c, objc.c, mods.c)
                b'closed before the give', b'run mark:', b'LEAK:', b'probe clean', b'probe left a trace',
                b'persistent dirty before', b'changed in Tweaks', b'7achievement',
                # batch 1: mods (usermods.c, pack.c)
                b'ask %s %s -> %s from %s', b'legacy %s from %s', b'media self-test',
                b'mods.txt written (', b'marker seen at +', b'the first file after the marker',
                b'packuse.txt', b'read %ld of %ld', b'the last run closed right after reading',
                # batch 1: sound (audio.c)
                b'audio selftest', b'CROSS-TALK', b'WRONG SOUND', b'sound cache:', b'sound census',
                b'error model:', b'live stress',
                # batch 1: saves and settings (shim.c, saves.c, sticks.c)
                b'settings guard', b'CHANGED OUTSIDE the game', b'self-check %s: live',
                b'memory differs from the file', b'aimlock default', b'the shooting finger came up',
                b'rename over an existing file', b'handed instead', b'written in %ld ms',
                b'one-time clean-up', b'NOT READ, errno %d - left alone', b"written the game's way",
                # batch 1: tap zones (tweaks.c)
                b'strap zone:',
                # batch 2: devices (coop.c, devices.c, reset.c, straps.c, uikit.c, pad)
                b'SELF-CHECK own players', b'SELF-CHECK seat', b'SELF-CHECK join', b'SELF-CHECK pause list',
                b'dispatcher object [0xac2490]', b'by the game (%s)', b'palm cancel id', b'press at frame',
                b'p0 list: history', b'tjacob: frame', b'masks %d analog', b'chosen: [0xac2490]',
                b'while it joins: no',
                # batch 2: touch zones (screen.c, straps.c, tweaks.c, credits.c, spawner.c)
                b'zones: screen %d', b'render: %s sprite', b'table %s: canvas', b'box test: screen',
                b'tap: %s finger', b'miss: %s finger', b'tap: Tweaks page', b'finger space:',
                b'overlay finger:', b'pause strap: canvas', b'pad strap: anchor',
                # batch 2: keyboard (objc.c, menu.c, spawner.c, MainActivity)
                b'keys selftest', b'seat selftest', b'join slots at a new Game', b'esc -> pause',
                b'keys: delivered', b'keyboard: bindings from', b'field changed by UNKNOWN',
                b"zone: the game's DOWN zone", b'zone: not the table', b'select: canvas',
                b'the run starts with difficulty', b'K left the step alone',
                # batch 3: vibration (rumble.c)
                b'site 0x%lx from', b'bridge missing', b'haptics: bound',
                # batch 3: touch (sticks.c)
                b'stickcurve', b'frames at an exact zero', b'switch-ons in the hold', b'apart from the finger',
                b'capture / ring', b"the port's value asked", b'the file reads %.2f', b'the knob follows the finger',
                b'the value goes through the port', b"stays the game's", b'read: DEAD ZONE',
                # 02.10.2026: crashes (loader.c), spawner tabs (spawner.c, spawner_stats.c)
                b'setjmp: selftest', b'png selftest:', b'stat edit:',
                # 02.10.2026: EID bag and saves (eid_craft.c, saves.c)
                b'craft self-check', b'new floor %d/%d', b'stale rooms', b'recomputed: query', b'room table is FULL',
                b'a room not seen on this floor', b'shown after the bag filter', b"by the game's own rules",
                b'would hold the game', b'turns past the end',
                # 02.10.2026: the spawner's teleport (spawner_teleport.c)
                b'resume: byte', b'made from the special rooms', b'after: stage %d',
                # 02.10.2026: mods (usermods.c)
                b'read back ok', b'read back DIFFERS', b'not reachable (', b'has %s - skipped', b'mods.txt NOT READ',
                b'read on retry after', b'differs from the copy inside the app', b'mods.txt read: %d lines',
                b'answered from the cache', b'guard: done in', b'not right after a mod',
                b'an update of %s - kept at',
                # batch 6: the dance (mods.c)
                b'marker: bossportraits.xml asked', b'was never asked - the dance waits', b'came back with the run',
                # the dance's tainted update (mods.c dance_start, 04.10.2026)
                b'asks for track',
                # the dance's exit mid-dance (mods.c dance_cast_check, 13.10.2026)
                b'left the run under the dance',
                # Range Fix (rangefix.c, 04.10.2026)
                b'range fix: knife variant',
                # mods: language order and mods over each other (usermods.c, 04.10.2026)
                b"order: the game's language set", b'mods over each other:',
                # mods: every two of the list and the conflict popup (usermods.c THE PAIRS, tweaks.c, 05.10.2026)
                b'pairs worked out (both orders)', b'mods: popup: %s breaks with',
                # pad pictures: the family and vendorName of a slot (pad.c shim_pad_family, 1.72)
                b'draws as family',
                # AIM LOCK let go by a tap without a direction (sticks.c, 05.10.2026)
                b'a tap without a direction',
                # spawner: whom it is for (spawner.c, 05.10.2026)
                b'give target:',
                # co-op: seats after Continue, the reselect window, HUD corners (coop.c, 05.10.2026)
                b'SELF-CHECK hud corners', b'continue: seats', b'reselect window', b'chooser: pad', b'let go with', b'chooser: %s', b'continue: hand-out ours', b'beside %s: %s', b'bag screen swiped', b'bag screen held: lock', b'description swiped', b'description page %d/%d', b'popup paper',
                # spawner: Mom the way her room makes her (spawner.c, 05.10.2026)
                b'as its room lays it out', b'door Moms her door table knows', b'arena: room noted', b'arena: left the room', b'room put back:', b'her players are watched', b'arena: our ', b"final boss: sound effects' level", b'kfit %s: ', b'a vsync every', b'vsync not told', b'hud blocks', b'hud piece', b'SELF-CHECK hud blocks', b'UNHEARD:', b'CUT SHORT:', b'PLAY REFUSED:', b'stream opened (#', b'timing: xruns', b'the buffer grows from', b'error callback never came', b'isMMapUsed', b'FALLBACK:', b'ordinary-mixer fallback', b'with the fast mixer, as before', b'sound meter', b'pocket: finger on row', b'in row %d', b'brought forward, ctx', b'SILENCE began', b'SILENCE ended', b'SILENCE sources', b'SILENCE calls', b'arena: audio sfx level', b'plain boss: the colour roll', b'plain boss: gone', b'entities kept plain', b"off this room's floor - moved back",
                b'json: selftest',
                # batch 5: M4 (usermods.c entities_warn)
                b'is not a word the loader knows', b"is past the key's 12/12/8 bits",
                # batch 5: M3 (content.c)
                b'bestiary of %s: variant', b'a page of its own',
                # batch 5: M8 (portpatch.c, straps.c, loader.c)
                b'portpatch: %s %s in %s = %d', b'straps: layers title %d/%d', b'has no layer Option Value 2',
                # batch 6: mod settings (usermods.c)
                b"settings' layers in use", b'is not a value of this setting now', b'is no value of this setting now',
                b"laid over the mod's file", b"for the port's additions",
                # batch 5: M9 (usermods.c)
                b'without its sheets: the game', b'stands in for the iOS sheet',
                # 13.10.2026: the last stretch onto the window (stretch.c, walls walking)
                b'last stretch:',
                # 14.10.2026: quads on the pixel grid of their target (texel.c)
                b'corners on whole px', b'first off the grid',
                # 03.10.2026: touch objects per touch (uikit.c)
                b'on an object the game still holds', b'on an object still held',
                # 03.10.2026: aim lock rule and D-pad lock (sticks.c)
                b'aimlock rule', b'fire on release', b"the D-pad's padlock",
                # 03.10.2026: the bag's floor (eid_craft.c)
                b"previous floor's (loaded", b'first room is loaded', b'rooms from before the floor',
                # 03.10.2026: the unlock watch (spawner.c)
                b'LEAK: bestiary',
                # 03.10.2026: spawner final bosses (spawner.c)
                b'final boss: our', b'controls back',
                # 04.10.2026: spawner champions (spawner.c)
                b'as champion', b'in boss colour', b'champion byte',
                # 04.10.2026: co-op twins after Continue (coop.c)
                b'SELF-CHECK twins', b'continue: saved players', b'mended after the restore', b'twins: player %d type',
                b'after the restore (%s) has',
                # 04.10.2026: pad double tap and drop (pad.c)
                b"within the game's ten", b'one press, one swap', b'is not the call expected - left alone', b'keyboard double tap:',
                # 06.10.2026: render passes by road, Filter off / on (gpu.c pass probe)
                b'pass clock:', b'nothing drawn %.1f', b're-specified %.2f', b'by target, a frame:',
                b'GPU by target', b'depth loaded %.2f',
                # 25.10.2026: the pill's bloom by target (gpu.c)
                b'bloom on, either road',
                # 07.10.2026: the spawner's pickups, pills, health, counters, charge (spawner.c, spawner_stats.c)
                b'into the player: %d taken', b'as colour %d', b'health edit:', b'counter edit:', b'charge: player',
                # 07.10.2026 hotfix 1.72, performance: the frame's work (objc.c), the stream's life (audio.c),
                # the save writer's queue (safefile.c)
                b'+ present held',
                # 11.10.2026 hotfix 1.74: the speed cap's line (objc.c), the picture's time (gl.c)
                b'FASTER THAN iOS', b'pictures on their slot', b'presentation time: %s',
                # 13.10.2026 hotfix 1.75: the game's own clock (objc.c)
                b'fastest 2 s', b'slots skipped', b'frames past their slot', b'thread busy %',
                b'the buffer shrinks back', b'with the fast mixer again', b'the fast mixer is tried again',
                b'life thread did not start', b'the writer thread took the game', b'while its write was queued',
                b"the writer's queue was NOT emptied", b'still queued when the import began', b'broke off in memory',
                b'the writer thread did not start',
                # 08.10.2026: the keyboard's restart key (reset.c)
                b'restart keys held',
                # 11.10.2026: the keyboard table's numbering (objc.c, 1.74)
                b"the PC's numbering",
                # 11.10.2026: the restart owed (usermods.c, 1.74)
                b'reads from the disk %s this run', b'.adding and .old- folders left alone',
                # 13.10.2026: the add window's restart, and why (usermods.c, 1.75)
                b'add window: RESTART TO APPLY',
                # 08.10.2026: the bloom draw left to the game (bloom.c), the pass clock's stamps (gpu.c)
                b'left to the game - shot', b'stamps lost or', b'every frame lost its stamps',
                # 14.10.2026: the ground truth of game time (objc.c, trace.c, loader.c)
                b'ground truth over', b'in a run and unpaused', b'the whole window: ticks', b'interpolation passes',
                b'6the game update', b'8the interpolation pass']
JAVA_HOME = r'C:\Program Files\Android\Android Studio\jbr'
SDK = (os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
       or os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk'))
ADB = os.path.join(SDK, 'platform-tools', 'adb.exe')


def run(cmd, **kw):
    print('>', ' '.join(cmd) if isinstance(cmd, list) else cmd)
    r = subprocess.run(cmd, **kw)
    if r.returncode:
        sys.exit('failed: %s' % cmd)
    return r


def gradle(task):
    env = dict(os.environ, JAVA_HOME=JAVA_HOME)
    run([os.path.join(PORT, 'gradlew.bat'), task, '-q'], cwd=PORT, env=env)


def vanilla_pak():
    """0. The pak carries the game's files as the IPA has them (plan 30.09, M8): every
    file of work/app is compared with the IPA's zip directory (size and CRC-32; the
    CRCs are cached by size and mtime in work/pak-crc-cache.json). The port's own
    files and its changes to the game's live in assets/port_patch/ and are laid over
    the pak at start (portpatch.c); a game file changed in work/app would be packed
    over them unnoticed - so the build refuses. port_additions.IN_PAK_TRANSITION
    (stringtable.sta, until content.c fills it with no mods too) is let through.
    And port_patch/ must be what tools/port_patch.py makes of work/port-edit."""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import port_additions
    import port_patch
    bad = port_additions.pak_check(quiet=True)
    if bad:
        sys.exit("the pak would carry %d game file(s) that are not the IPA's - the port's versions go to "
                 "work/port-edit and tools/port_patch.py --make:\n  %s" % (len(bad), "\n  ".join(bad[:20])))
    if not port_patch.check():
        sys.exit('assets/port_patch/ is not what work/port-edit makes - run tools/port_patch.py --make')


def main():
    release = '--release' in sys.argv
    apk_path, out_path = (RELEASE_APK, RELEASE_OUT) if release else (APK, OUT)
    # 0. the game's files are vanilla, the port's are in port_patch/
    vanilla_pak()
    # 1. the pak
    fresh = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'pack.py'), '--check'])
    if fresh.returncode:
        run([sys.executable, os.path.join(ROOT, 'tools', 'pack.py')])
        run([sys.executable, os.path.join(ROOT, 'tools', 'pack.py'), '--check'])
    # 1b. no reference copies in the APK's assets (tools/asset_tails.py, M12 p.6)
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import asset_tails
    tails = asset_tails.check()
    if tails:
        sys.exit('reference copies in assets/ would ship in the APK: %s' % ', '.join(tails))
    # 1c. the gamepad database asset is the one tools/paddb.py makes (02.10.2026)
    run([sys.executable, os.path.join(ROOT, 'tools', 'paddb.py'), '--check'])
    # 1d. the converter's game reference for ADD MOD (ModPort.java, 04.10.2026): made from the tree the pak is
    # made of (tools/modport_ref.py, the web converter's gameref.bin), rewritten only when it changed
    gameref = os.path.join(PORT, 'app', 'src', 'main', 'assets', 'modport_gameref.bin')
    fresh_ref = gameref + '.new'
    run([sys.executable, os.path.join(ROOT, 'tools', 'modport_ref.py'), '--out', fresh_ref])
    if not os.path.exists(gameref) or open(gameref, 'rb').read() != open(fresh_ref, 'rb').read():
        os.replace(fresh_ref, gameref)
    else:
        os.remove(fresh_ref)
    # 2. no old APK for Gradle to write into
    if os.path.exists(apk_path):
        os.remove(apk_path)
    # 3. the build
    if release:
        # newline='' both ways: the file goes back byte for byte, line ends and all
        source = open(ACTIVITY, encoding='utf-8', newline='').read()
        if (REPORT_ON in source) == (REPORT_OFF in source):
            sys.exit('MainActivity.REPORT is not where the build expects it')
        try:
            open(ACTIVITY, 'w', encoding='utf-8', newline='').write(source.replace(REPORT_ON, REPORT_OFF))
            gradle('assembleRelease')
        finally:
            open(ACTIVITY, 'w', encoding='utf-8', newline='').write(source)
    else:
        gradle('assembleDebug')
    # 4. the APK as it must be
    z = zipfile.ZipFile(apk_path)
    entries = z.infolist()
    names = [i.filename for i in entries]
    if len(names) != len(set(names)):
        sys.exit('the APK holds an entry twice')
    pak = [i for i in entries if i.filename == 'assets/resources.pak']
    if not pak or pak[0].compress_type != zipfile.ZIP_STORED:
        sys.exit('resources.pak is missing from the APK or compressed')
    # the pak in the APK is the whole pak (pack.py --check has walked it to its end)
    here = os.path.getsize(os.path.join(PORT, 'app', 'src', 'main', 'assets', 'resources.pak'))
    if pak[0].file_size != here:
        sys.exit('the APK carries a pak of %d bytes, the pak is %d' % (pak[0].file_size, here))
    if release:
        lib = z.read('lib/arm64-v8a/libisaacport.so')
        left = [d.decode() for d in DIAG_STRINGS if d in lib]
        if left:
            sys.exit('the clean build still carries diagnostics: %s' % ', '.join(left))
    size = os.path.getsize(apk_path)
    held = sum(i.compress_size for i in entries)
    if size > held * 1.05 + (8 << 20):
        sys.exit('the APK is %d bytes but its entries are %d: dead space - delete it and build again'
                 % (size, held))
    # 5. out
    shutil.copyfile(apk_path, out_path)
    print('%s: %.2f GB, %d entries%s' % (out_path, size / 1e9, len(entries), ', clean' if release else ''))
    if '--install' in sys.argv:
        run([ADB, 'install', '-r', out_path])


if __name__ == '__main__':
    main()
