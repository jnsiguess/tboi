#!/usr/bin/env bash
# Mete la guadana de Samael (samael_scythe.c) en el source del port.
# Corre desde la RAIZ del repo (igual que apply_ff.sh / apply_personaje.sh), despues de apply_yama.sh:
#   bash samael/apply_samael.sh
# Se puede correr varias veces: lo que ya esta puesto no se repite.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CPP=port/app/src/main/cpp
[ -d "$CPP" ] || { echo "ERROR: no encuentro $CPP. Corre este script desde la raiz del repo."; exit 1; }
cp "$HERE/samael_scythe.c" "$CPP/samael_scythe.c"
python3 - <<'PY'
import sys
C = 'port/app/src/main/cpp/'

def edit(path, old, new, done):
    s = open(path, encoding='utf-8', newline='').read()
    if done in s:
        print("ya estaba: " + path)
        return
    if s.count(old) != 1:
        sys.exit("ERROR: texto esperado encontrado %d veces en %s: %r" % (s.count(old), path, old[:70]))
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
    print("editado: " + path)

# 1) el archivo nuevo entra al build (misma tecnica que ffchars.c / mychar.c)
edit(C + 'CMakeLists.txt', ' rangefix.c ', ' rangefix.c samael_scythe.c ', 'samael_scythe.c')
# 2) se llama una vez por frame, junto a la llamada de statsplus (mods.c, shim_mods_between_frames)
edit(C + 'mods.c',
     '    shim_statsplus_between_frames();\n',
     '    shim_statsplus_between_frames();\n'
     '    void shim_samael_between_frames(void);\n'
     '    shim_samael_between_frames();\n',
     'shim_samael_between_frames();')
PY
echo "guadana de Samael aplicada"
