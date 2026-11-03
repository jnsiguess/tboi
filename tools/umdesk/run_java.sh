#!/bin/bash
# the installer on the desk: UserMods.java (the real file) + a desk org.json, against android.jar for its types,
# and the real judge of mod.json - modcheck_jni.c + json.c built as a Windows DLL (LLVM clang + MSVC, as
# tools/jsondesk) that UserMods.checkModJson binds to by its exported name, as on the phone
#   bash tools/umdesk/run_java.sh <mod zip> [work dir]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
JDK="/c/Program Files/Android/Android Studio/jbr/bin"
AJ="$SDK_DIR/platforms/android-35/android.jar"
NDKJNI="$SDK_DIR/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include/jni.h"
SRC="$ROOT/port/app/src/main/cpp"
OUT="${TEMP:-/tmp}/umdesk-java"
rm -rf "$OUT"; mkdir -p "$OUT/jni"

# the judge: jni.h alone from the NDK (its sysroot would bring Android's libc headers along)
CL="/c/Program Files/LLVM/bin/clang.exe"
MSVC="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC"
MSVC="$MSVC/$(ls "$MSVC" | tail -1)"
SDK="/c/Program Files (x86)/Windows Kits/10"
SDKV=$(ls "$SDK/Lib" | tail -1)
export LIB="$(cygpath -w "$MSVC/lib/x64");$(cygpath -w "$SDK/Lib/$SDKV/ucrt/x64");$(cygpath -w "$SDK/Lib/$SDKV/um/x64")"
export INCLUDE="$(cygpath -w "$MSVC/include");$(cygpath -w "$SDK/Include/$SDKV/ucrt");$(cygpath -w "$SDK/Include/$SDKV/um");$(cygpath -w "$SDK/Include/$SDKV/shared")"
cp "$NDKJNI" "$OUT/jni/jni.h"
"$CL" -O1 -Wall -Wextra -Wno-unknown-attributes -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=0 -I"$OUT/jni" -I"$SRC" -shared \
    "$SRC/modcheck_jni.c" "$SRC/json.c" -o "$OUT/modcheck.dll" -Wl,/EXPORT:Java_ru_isaac_port_UserMods_checkModJson -Wl,/EXPORT:Java_ru_isaac_port_UserMods_strictModJson

"$JDK/javac.exe" -nowarn -encoding UTF-8 -d "$OUT" -cp "$AJ" \
    "$HERE/java/org/json/JSONObject.java" "$HERE/java/org/json/JSONArray.java" "$HERE/java/org/json/JSONException.java" \
    "$HERE/java/ru/isaac/port/InstallDesk.java" "$ROOT/port/app/src/main/java/ru/isaac/port/UserMods.java" \
    "$ROOT/port/app/src/main/java/ru/isaac/port/ModPort.java" "$ROOT/port/app/src/main/java/ru/isaac/port/ModPortXml.java" \
    "$ROOT/port/app/src/main/java/ru/isaac/port/ModPortPng.java"
# the converter at install (case 17): the reference the APK carries; DESK_ADAPT_BIG=<a PC mod zip> times a big one
GAMEREF="$ROOT/port/app/src/main/assets/modport_gameref.bin"
[ -f "$GAMEREF" ] || GAMEREF="$ROOT/other stuff/usermods/modport-web/src/gameref.bin"
"$JDK/java.exe" ${DESK_XMX:+-Xmx$DESK_XMX} -Duser.language=${DESK_LANG:-en} -Dstdout.encoding=UTF-8 -Dumdesk.jni="$(cygpath -w "$OUT/modcheck.dll")" \
    -Dumdesk.gameref="$(cygpath -w "$GAMEREF")" ${DESK_ADAPT_BIG:+-Dumdesk.adaptbig="$(cygpath -w "$DESK_ADAPT_BIG")"} \
    -cp "$(cygpath -w "$OUT");$AJ" ru.isaac.port.InstallDesk "$1" "${2:-${TEMP:-/tmp}/umdesk-install}"
