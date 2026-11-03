#!/bin/bash
# THE RESTART AFTER ADD MOD, the phone's order of calls (hotfix 1.75): the real UserMods.java over the real usermods.c
# (jnidesk.c, real threads: rt/), as a game process - JniDesk.java plays the start, the page and the add window.
#   bash tools/umdesk/run_jni.sh [JniDesk args...]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
NDK_INC="$SDK_DIR/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include"     # fake/jni.h and fake/zlib.h take the NDK's own
JDK="/c/Program Files/Android/Android Studio/jbr/bin"
AJ="$SDK_DIR/platforms/android-35/android.jar"
SRC="$ROOT/port/app/src/main/cpp"
OUT="${DESK_OUT:-${TEMP:-/tmp}/umdesk-jni}"
mkdir -p "$OUT"
CL="/c/Program Files/LLVM/bin/clang.exe"
MSVC="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC"
MSVC="$MSVC/$(ls "$MSVC" | tail -1)"
SDK="/c/Program Files (x86)/Windows Kits/10"
SDKV=$(ls "$SDK/Lib" | tail -1)
export LIB="$(cygpath -w "$MSVC/lib/x64");$(cygpath -w "$SDK/Lib/$SDKV/ucrt/x64");$(cygpath -w "$SDK/Lib/$SDKV/um/x64")"
export INCLUDE="$(cygpath -w "$MSVC/include");$(cygpath -w "$SDK/Include/$SDKV/ucrt");$(cygpath -w "$SDK/Include/$SDKV/um");$(cygpath -w "$SDK/Include/$SDKV/shared")"
DIAG=${DESK_DIAG:-0}            # the clean build's usermods.c by default: what the players run
EXPORTS=""
for f in listPut gameReady installing restartOwed innerDir nativeReport reportActive checkModJson strictModJson; do
    EXPORTS="$EXPORTS -Wl,/EXPORT:Java_ru_isaac_port_UserMods_$f"
done
for f in start changed count pending id on owners diskNow diskBusy; do
    EXPORTS="$EXPORTS -Wl,/EXPORT:Java_ru_isaac_port_JniDesk_$f"
done
cd "$HERE"
"$CL" -O1 -g -c win.c -o "$OUT/win.o"
"$CL" -O1 -g -c rt/rt.c -o "$OUT/rt.o"
UM=()                            # DESK_USERMODS_C=<an earlier usermods.c>: the same run over it (test_restart175.py)
[ -n "$DESK_USERMODS_C" ] && UM=(-DUMDESK_USERMODS="\"$DESK_USERMODS_C\"")
"$CL" -O1 -g -Wno-everything -DISAAC_DIAG=$DIAG -DISAAC_ROOT="\"$(cygpath -m "$ROOT")/\"" -Irt -Ifake -idirafter "$NDK_INC" "${UM[@]}" -c jnidesk.c -o "$OUT/jnidesk.o"
"$CL" -O1 -g -Wno-everything -DISAAC_DIAG=$DIAG -Irt -Ifake -idirafter "$NDK_INC" -include fake/compat.h -I"$SRC" -c "$SRC/content.c" -o "$OUT/content.o"
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=$DIAG -Irt -Ifake -idirafter "$NDK_INC" -I"$SRC" -c "$SRC/json.c" -o "$OUT/json.o"
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=$DIAG -DPORTPATCH_DESK -I"$SRC" -c "$SRC/portpatch.c" -o "$OUT/portpatch.o"
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=$DIAG -Irt -Ifake -idirafter "$NDK_INC" -I"$SRC" -c "$SRC/modcheck_jni.c" -o "$OUT/modcheck.o"
"$CL" -g -shared "$OUT/jnidesk.o" "$OUT/content.o" "$OUT/json.o" "$OUT/portpatch.o" "$OUT/modcheck.o" "$OUT/win.o" "$OUT/rt.o" \
    -o "$OUT/umjni.dll" $EXPORTS
"$JDK/javac.exe" -nowarn -encoding UTF-8 -d "$OUT" -cp "$AJ" \
    "$HERE/java/org/json/JSONObject.java" "$HERE/java/org/json/JSONArray.java" "$HERE/java/org/json/JSONException.java" \
    "$HERE/java/ru/isaac/port/JniDesk.java" "${DESK_USERMODS_JAVA:-$ROOT/port/app/src/main/java/ru/isaac/port/UserMods.java}" \
    "$ROOT/port/app/src/main/java/ru/isaac/port/ModPort.java" "$ROOT/port/app/src/main/java/ru/isaac/port/ModPortXml.java" \
    "$ROOT/port/app/src/main/java/ru/isaac/port/ModPortPng.java"
[ -n "$DESK_BUILD_ONLY" ] && exit 0
"$JDK/java.exe" -Dstdout.encoding=UTF-8 -Dumdesk.jni="$(cygpath -w "$OUT/umjni.dll")" \
    -cp "$(cygpath -w "$OUT");$AJ" ru.isaac.port.JniDesk "$@"
