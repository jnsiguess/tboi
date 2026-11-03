#!/bin/bash
# the desk stand: the real usermods.c for Windows (LLVM clang + the MSVC runtime)
#   build.sh [out.exe] [diag]: desk.exe with ISAAC_DIAG=1 by default; "build.sh desk0.exe 0" is the clean build
set -e
cd "$(dirname "$0")"
CL="/c/Program Files/LLVM/bin/clang.exe"
MSVC="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC"
MSVC="$MSVC/$(ls "$MSVC" | tail -1)"
SDK="/c/Program Files (x86)/Windows Kits/10"
SDKV=$(ls "$SDK/Lib" | tail -1)
export LIB="$(cygpath -w "$MSVC/lib/x64");$(cygpath -w "$SDK/Lib/$SDKV/ucrt/x64");$(cygpath -w "$SDK/Lib/$SDKV/um/x64")"
export INCLUDE="$(cygpath -w "$MSVC/include");$(cygpath -w "$SDK/Include/$SDKV/ucrt");$(cygpath -w "$SDK/Include/$SDKV/um");$(cygpath -w "$SDK/Include/$SDKV/shared")"
OUT=${1:-desk.exe}
ROOT_M="$(cygpath -m "$(cd ../.. && pwd)")"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
NDK_INC="$SDK_DIR/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include"     # fake/jni.h and fake/zlib.h take the NDK's own
DIAG=${2:-1}                    # 0: the clean build's usermods.c (no report, no NOTE lines)
"$CL" -O1 -g -c win.c -o win.o
"$CL" -O1 -g -Wno-everything -DISAAC_DIAG=$DIAG -DISAAC_ROOT="\"$ROOT_M/\"" -Ifake -idirafter "$NDK_INC" -c desk.c -o "${OUT%.exe}.o"
# content.c (the data tables, plan 4.9 M3): its own unit, as in the app
"$CL" -O1 -g -Wno-everything -DISAAC_DIAG=$DIAG -Ifake -idirafter "$NDK_INC" -include fake/compat.h -I../../port/app/src/main/cpp       -c ../../port/app/src/main/cpp/content.c -o "${OUT%.exe}-content.o"
# json.c (mod.json, plan 4.10): its own unit, as in the app
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=$DIAG -Ifake -idirafter "$NDK_INC" -I../../port/app/src/main/cpp -c ../../port/app/src/main/cpp/json.c -o "${OUT%.exe}-json.o"
# portpatch.c's patch engine (M8; the all-or-nothing rule for a mod's menu .anm2, 02.10.2026): its desk build
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=$DIAG -DPORTPATCH_DESK -I../../port/app/src/main/cpp -c ../../port/app/src/main/cpp/portpatch.c -o "${OUT%.exe}-portpatch.o"
"$CL" -g "${OUT%.exe}.o" "${OUT%.exe}-content.o" "${OUT%.exe}-json.o" "${OUT%.exe}-portpatch.o" win.o -o "$OUT"
echo "built $OUT"
