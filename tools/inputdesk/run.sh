#!/bin/bash
# the device rule on the desk: InputBook.java and PadDb.java (the real files) against android.jar,
# fed model devices (sources, keyboard type, motion ranges with their sources, hasKeys) and the
# real assets/paddb.txt; each key routed by the 1.666 rule, the 1.71 rule and the current one
#   bash tools/inputdesk/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
JDK="/c/Program Files/Android/Android Studio/jbr/bin"
AJ="$SDK_DIR/platforms/android-35/android.jar"
OUT="${TEMP:-/tmp}/inputdesk"
SRC="$ROOT/port/app/src/main/java/ru/isaac/port"
rm -rf "$OUT"; mkdir -p "$OUT"
"$JDK/javac.exe" -Xlint:all -Xlint:-options -encoding UTF-8 -d "$OUT" -cp "$AJ" \
    "$HERE/InputDesk.java" "$SRC/InputBook.java" "$SRC/PadDb.java" "$SRC/JoyCons.java"
"$JDK/java.exe" -Dstdout.encoding=UTF-8 -cp "$(cygpath -w "$OUT");$AJ" ru.isaac.port.InputDesk \
    "$(cygpath -w "$ROOT/port/app/src/main/assets/paddb.txt")"
