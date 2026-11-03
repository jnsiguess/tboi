#!/bin/bash
# the desk stand for portpatch.c: the real file, compiled for Windows (LLVM clang + the MSVC runtime)
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
"$CL" -O1 -g -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=1 -DPORTPATCH_DESK -I"$SRC" -c "$SRC/portpatch.c" -o portpatch.o
"$CL" -O1 -g -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS -c desk.c -o desk.o
"$CL" -g desk.o portpatch.o -o portdesk.exe
echo "built portdesk.exe"
