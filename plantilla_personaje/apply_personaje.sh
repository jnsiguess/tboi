#!/usr/bin/env bash
# Mete mychar.c en el source del port (corre desde la RAIZ del repo, despues de apply_yama.sh si lo usas).
#   bash plantilla_personaje/apply_personaje.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CPP=port/app/src/main/cpp
cp "$HERE/mychar.c" "$CPP/mychar.c"
python3 - <<'PY'
def edit(path, old, new):
    s = open(path, encoding='utf-8', newline='').read()
    if s.count(old) != 1:
        raise SystemExit("texto esperado encontrado %d veces en %s: %r" % (s.count(old), path, old[:70]))
    open(path, 'w', encoding='utf-8', newline='').write(s.replace(old, new))
C = 'port/app/src/main/cpp/'
edit(C + 'CMakeLists.txt', ' rangefix.c ', ' rangefix.c mychar.c ')
edit(C + 'loader.c',
     '        { void shim_rangefix_install(unsigned char *); shim_rangefix_install(im.base); }\n',
     '        { void shim_rangefix_install(unsigned char *); shim_rangefix_install(im.base); }\n'
     '        { void shim_mychar_install(unsigned char *); shim_mychar_install(im.base); }\n')
print("mychar aplicado")
PY
