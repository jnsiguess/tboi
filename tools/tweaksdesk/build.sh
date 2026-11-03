#!/bin/bash
# the desk stand of the MODS page: the real tweaks.c over the real usermods.c + content.c + json.c (umdesk's harness,
# its main renamed), for Windows (LLVM clang + the MSVC runtime)
#   build.sh -> tweaksdesk.exe
set -e
cd "$(dirname "$0")"
CL="/c/Program Files/LLVM/bin/clang.exe"
MSVC="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC"
MSVC="$MSVC/$(ls "$MSVC" | tail -1)"
SDK="/c/Program Files (x86)/Windows Kits/10"
SDKV=$(ls "$SDK/Lib" | tail -1)
export LIB="$(cygpath -w "$MSVC/lib/x64");$(cygpath -w "$SDK/Lib/$SDKV/ucrt/x64");$(cygpath -w "$SDK/Lib/$SDKV/um/x64")"
export INCLUDE="$(cygpath -w "$MSVC/include");$(cygpath -w "$SDK/Include/$SDKV/ucrt");$(cygpath -w "$SDK/Include/$SDKV/um");$(cygpath -w "$SDK/Include/$SDKV/shared")"
SRC=../../port/app/src/main/cpp
ROOT_M="$(cygpath -m "$(cd ../.. && pwd)")"
SDK_DIR="$(cygpath -m "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}}")"
NDK_INC="$SDK_DIR/ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include"     # fake/jni.h and fake/zlib.h take the NDK's own
UM=../umdesk
"$CL" -O1 -g -c mem.c -o mem.o
"$CL" -O1 -g -c $UM/win.c -o win.o
"$CL" -O1 -g -Wno-everything -DISAAC_DIAG=1 -DISAAC_ROOT="\"$ROOT_M/\"" -I$UM/fake -idirafter "$NDK_INC" -Dmain=umdesk_main -Dshim_image_base=umdesk_image_base -c $UM/desk.c -o um.o
"$CL" -O1 -g -Wno-everything -DISAAC_DIAG=1 -I$UM/fake -idirafter "$NDK_INC" -include $UM/fake/compat.h -I$SRC -c $SRC/content.c -o content.o
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=1 -I$UM/fake -idirafter "$NDK_INC" -I$SRC -c $SRC/json.c -o json.o
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=1 -DISAAC_ROOT="\"$ROOT_M/\"" -I$UM/fake -idirafter "$NDK_INC" -I$SRC -c desk.c -o desk.o
"$CL" -O1 -g -Wno-everything -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=1 -DPORTPATCH_DESK -I$SRC -c $SRC/portpatch.c -o portpatch.o
"$CL" -g desk.o um.o content.o json.o portpatch.o win.o mem.o -o tweaksdesk.exe
echo "built tweaksdesk.exe"
