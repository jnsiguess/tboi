#!/bin/bash
# pad.c (the real file) on the desk: the drop patch, a held R2, the double tap against the ungated game
#   bash tools/paddesk/surface.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
CL="/c/Program Files/LLVM/bin/clang.exe"
MSVC="/c/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC"
MSVC="$MSVC/$(ls "$MSVC" | tail -1)"
SDK="/c/Program Files (x86)/Windows Kits/10"
SDKV=$(ls "$SDK/Lib" | tail -1)
export LIB="$(cygpath -w "$MSVC/lib/x64");$(cygpath -w "$SDK/Lib/$SDKV/ucrt/x64");$(cygpath -w "$SDK/Lib/$SDKV/um/x64")"
export INCLUDE="$(cygpath -w "$MSVC/include");$(cygpath -w "$SDK/Include/$SDKV/ucrt");$(cygpath -w "$SDK/Include/$SDKV/um");$(cygpath -w "$SDK/Include/$SDKV/shared")"
SRC="$ROOT/port/app/src/main/cpp"
OUT="${TEMP:-/tmp}/paddesk-surface"
mkdir -p "$OUT"
# Windows keeps long at 32 bits and the layer is written for LP64 (a pointer in an unsigned long):
# the desk compiles copies where "unsigned long" reads "unsigned long long" - nothing else is changed.
lp64() { sed -e 's/unsigned long long/U_L_L/g' -e 's/unsigned long/unsigned long long/g' -e 's/U_L_L/unsigned long long/g' "$1" > "$2"; }
lp64 "$SRC/pad.c" "$OUT/pad.c"
lp64 "$HERE/surface.c" "$OUT/surface.c"
for DIAG in 0 1; do
    "$CL" -O1 -g -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS -DISAAC_DIAG=$DIAG \
        -I"$HERE/fake" -include "$HERE/fake/desk_prelude.h" -I"$SRC" -c "$OUT/pad.c" -o "$OUT/pad.o"
    "$CL" -O1 -g -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS -c "$OUT/surface.c" -o "$OUT/surface.o"
    "$CL" -g "$OUT/surface.o" "$OUT/pad.o" -o "$OUT/surface.exe"
    echo "== ISAAC_DIAG=$DIAG"
    "$OUT/surface.exe" "$(cygpath -w "$ROOT/work/isaac.arm64")"
done
