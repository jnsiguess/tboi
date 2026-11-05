#!/usr/bin/env bash
# Applies the Yama layer to a fresh checkout of the port source (run from the repo root).
#   bash yama/apply_yama.sh release     (clean build: MainActivity.REPORT = false)
#   bash yama/apply_yama.sh report      (diagnostic build: REPORT stays true)
# What it does, every step verified (it stops if a text it expects is not there):
#   1. adds ru/isaac/port/PatchCfg.java (Yama's loader of port_patches.bin, recovered from his dex)
#   2. calls it in MainActivity right after read("isaac.arm64"), like Yama's dex does
#   3. gives the build the identity of the APK on the phone: com.tboi.modded1, its file-provider
#      authority and the app name "TBOI Mod: Rebirth" (the same changes Yama's pack makes)
set -euo pipefail
VARIANT="${1:-release}"
HERE="$(cd "$(dirname "$0")" && pwd)"
J=port/app/src/main/java/ru/isaac/port
cp "$HERE/PatchCfg.java" "$J/PatchCfg.java"

python3 - "$VARIANT" <<'PY'
import sys, re
variant = sys.argv[1]
def edit(path, old, new, count=1):
    s = open(path, encoding='utf-8', newline='').read()
    if s.count(old) != count:
        sys.exit("expected text not found %d time(s) in %s: %r (found %d)" % (count, path, old[:60], s.count(old)))
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new))

M = 'port/app/src/main/java/ru/isaac/port/MainActivity.java'
edit(M, '                byte[] image = read("isaac.arm64");\n',
        '                byte[] image = read("isaac.arm64");\n'
        '                try { PatchCfg.apply(image, portDir(), read("port_patches.bin")); }\n'
        '                catch (Throwable t) { android.util.Log.e("isaac", "patch loader failed", t); }\n')
if variant == 'release':
    edit(M, 'private static final boolean REPORT = true;', 'private static final boolean REPORT = false;')
edit('port/app/build.gradle', 'applicationId "com.tboi.rebirth"', 'applicationId "com.tboi.modded1"')
edit('port/app/src/main/java/ru/isaac/port/PortFilesProvider.java', '"com.tboi.rebirth.files"', '"com.tboi.modded1.files"')
edit('port/app/src/main/AndroidManifest.xml', 'android:authorities="com.tboi.rebirth.files"', 'android:authorities="com.tboi.modded1.files"')
S = 'port/app/src/main/res/values/strings.xml'
edit(S, '>The Binding of Isaac: Rebirth<', '>TBOI Mod: Rebirth<')
edit(S, '>TBOI: Rebirth<', '>TBOI Mod: Rebirth<')
print("yama layer applied (%s)" % variant)
PY
