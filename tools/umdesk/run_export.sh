#!/bin/bash
# the export of the game's files on the desk (plan 30.09, M12): GameFiles.java + FileKinds.java (the real files)
# over a pack made from a slice of work/app, the real assets folder and the bring_in registry
#   bash tools/umdesk/run_export.sh [work dir]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
JDK="/c/Program Files/Android/Android Studio/jbr/bin"
AJ="$SDK_DIR/platforms/android-35/android.jar"
SRC="$ROOT/port/app/src/main/java/ru/isaac/port"
WORK="${1:-${TEMP:-/tmp}/umdesk-export}"
OUT="${TEMP:-/tmp}/umdesk-export-classes"
rm -rf "$OUT" "$WORK"; mkdir -p "$OUT" "$WORK"
py -3 "$HERE/export_inputs.py" "$WORK"
"$JDK/javac.exe" -nowarn -encoding UTF-8 -d "$OUT" -cp "$AJ" \
    "$HERE/java/org/json/JSONObject.java" "$HERE/java/org/json/JSONArray.java" "$HERE/java/org/json/JSONException.java" \
    "$HERE/java/ru/isaac/port/ExportDesk.java" "$SRC/GameFiles.java" "$SRC/FileKinds.java" "$SRC/UserMods.java" "$SRC"/ModPort*.java
"$JDK/java.exe" -Duser.language=en -Dstdout.encoding=UTF-8 -Dumdesk.root="$(cygpath -w "$ROOT")" -cp "$(cygpath -w "$OUT");$AJ" ru.isaac.port.ExportDesk \
    "$(cygpath -w "$WORK/slice.pak")" "$(cygpath -w "$ROOT/port/app/src/main/assets")" "$(cygpath -w "$WORK/registry.txt")" "$(cygpath -w "$WORK")"
