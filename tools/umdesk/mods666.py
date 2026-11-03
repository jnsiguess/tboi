"""The reader of mods.txt in the build of 1.666 (tboi.apk of 01.10.2026), copied from its code:
libisaacport.so shim_usermods_setup 0xd2de4..0xd3050 (llvm-objdump, 02.10.2026).

  fopen(<mods>/mods.txt, "r"); fgets(line, 160); sscanf(line, "%7s %63s", state, id) == 2
  -> stat(<mods>/<id>) is a directory -> appended, on = (state == "on"); no check for twice
  then every folder of <mods> not starting with '.' and not listed yet -> appended, on
  (up to 64 in all; and then the list is written back unconditionally - not modelled here)

    py -3 mods666.py <mods folder>   -> the list as 1.666 would take it
"""
import os
import sys


def scan_s(s, i, width):
    """scanf %<width>s from s[i]: (token or None, next i)"""
    while i < len(s) and s[i] in ' \t\n\r\v\f':
        i += 1
    j = i
    while j < len(s) and j - i < width and s[j] not in ' \t\n\r\v\f':
        j += 1
    return (s[i:j] if j > i else None), j


def read_666(mods):
    out = []
    try:
        data = open(os.path.join(mods, 'mods.txt'), 'rb').read().decode('utf-8', 'replace')
    except OSError:
        data = None
    if data is not None:
        # fgets(160): a longer line comes in pieces of 159
        lines = []
        for raw in data.splitlines(keepends=True):
            while len(raw) > 159:
                lines.append(raw[:159])
                raw = raw[159:]
            lines.append(raw)
        for line in lines:
            if len(out) >= 64:
                break
            state, i = scan_s(line, 0, 7)
            if state is None:
                continue
            ident, i = scan_s(line, i, 63)
            if ident is None:
                continue
            if os.path.isdir(os.path.join(mods, ident)):
                out.append((ident, state == 'on'))
    for name in os.listdir(mods):
        if len(out) >= 64:
            break
        if name.startswith('.') or not os.path.isdir(os.path.join(mods, name)):
            continue
        if any(ident == name for ident, _ in out):
            continue
        out.append((name, True))
    return out


if __name__ == '__main__':
    for ident, on in read_666(sys.argv[1]):
        print('on ' if on else 'off', ident)
