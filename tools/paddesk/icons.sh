#!/bin/bash
# the pad-picture families on the desk: PadIcons.java (the real file, with JoyCons.java and PadDb.java) against
# android.jar - vendor ids, Joy-Con ids, names, the unknown, the keyboard - and the families'
# order against cpp/guide_layout.h
#   bash tools/paddesk/icons.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
JDK="/c/Program Files/Android/Android Studio/jbr/bin"
AJ="$SDK_DIR/platforms/android-35/android.jar"
OUT="${TEMP:-/tmp}/paddesk-icons"
SRC="$ROOT/port/app/src/main/java/ru/isaac/port"
rm -rf "$OUT"; mkdir -p "$OUT"
"$JDK/javac.exe" -Xlint:all -Xlint:-options -encoding UTF-8 -d "$OUT" -cp "$AJ" \
    "$HERE/PadIconsDesk.java" "$SRC/PadIcons.java" "$SRC/JoyCons.java" "$SRC/PadDb.java"
"$JDK/java.exe" -Dstdout.encoding=UTF-8 -cp "$(cygpath -w "$OUT");$AJ" ru.isaac.port.PadIconsDesk \
    "$(cygpath -w "$ROOT/port/app/src/main/cpp/guide_layout.h")"
