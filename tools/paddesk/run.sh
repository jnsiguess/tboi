#!/bin/bash
# the pad layouts on the desk: PadDb.java (the real file) against android.jar, fed synthetic devices
# (real pads' vendor/product/axes/keys) and the real assets/paddb.txt
#   bash tools/paddesk/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
JDK="/c/Program Files/Android/Android Studio/jbr/bin"
AJ="$SDK_DIR/platforms/android-35/android.jar"
OUT="${TEMP:-/tmp}/paddesk"
rm -rf "$OUT"; mkdir -p "$OUT"
"$JDK/javac.exe" -Xlint:all -Xlint:-options -encoding UTF-8 -d "$OUT" -cp "$AJ" \
    "$HERE/PadDesk.java" "$ROOT/port/app/src/main/java/ru/isaac/port/PadDb.java"
"$JDK/java.exe" -Dstdout.encoding=UTF-8 -cp "$(cygpath -w "$OUT");$AJ" ru.isaac.port.PadDesk \
    "$(cygpath -w "$ROOT/port/app/src/main/assets/paddb.txt")"
