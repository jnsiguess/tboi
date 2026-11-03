# -*- coding: utf-8 -*-
"""The mod's luck formulas, turned into C.

EID shows what a luck-based chance actually is for the run you are in: "33%
chance at 4 luck". The chances live in features/eid_data.lua as one Lua
function per item -

    EID.LuckFormulas["5.100.55"] = function(luck)
        return (100 / math.max(1, (2 - math.floor(luck*0.5)))) end

- fifty-odd of them, all plain arithmetic over `luck` with max, min and floor.
This reads them and writes eid_luck.h: one switch, one line each.

    py -3 tools/eid_luck.py [mod dir] [eid_luck.h]

The luck itself is a float at Player + 0x1950. It was found in the game rather
than guessed: fifty places load that field, take the floor and build a
one-in-N chance out of it, which is the same shape as the formulas here.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.join(os.path.dirname(HERE), 'eid-mod')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'port', 'app', 'src', 'main', 'cpp', 'eid_luck.h')

VARIANTS = {'100': 1, '350': 2, '300': 3, '70': 4}


def split_args(text):
    """The top-level arguments of a call, as text."""
    parts, depth, at = [], 0, 0
    for i, c in enumerate(text):
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        elif c == ',' and depth == 0:
            parts.append(text[at:i])
            at = i + 1
    parts.append(text[at:])
    return [p.strip() for p in parts]


def to_c(expr):
    """One Lua expression over `luck`, as C.

    Only what the mod actually writes: numbers, luck, + - * /, brackets, and
    math.max / math.min / math.floor. A one-argument math.min is Lua's way of
    saying "leave it alone", and it comes out as the argument itself.
    """
    expr = expr.strip()
    while True:
        call = re.search(r'math\.(max|min|floor|ceil)\(', expr)
        if not call:
            break
        start = call.end() - 1
        depth = 0
        for i in range(start, len(expr)):
            if expr[i] == '(':
                depth += 1
            elif expr[i] == ')':
                depth -= 1
                if depth == 0:
                    end = i
                    break
        else:
            raise SystemExit('unbalanced brackets: ' + expr)
        inner = expr[start + 1:end]
        args = [to_c(a) for a in split_args(inner)]
        name = call.group(1)
        if name in ('floor', 'ceil'):
            made = '%sf(%s)' % (name, args[0])
        elif len(args) == 1:
            made = '(%s)' % args[0]                 # math.min(x) is just x
        else:
            made = args[0]
            for more in args[1:]:
                made = '%sf(%s, %s)' % ('fmax' if name == 'max' else 'fmin', made, more)
        expr = expr[:call.start()] + made + expr[end + 1:]
    return expr


def formulas(mod_dir):
    """[(kind, id, C expression, the mod's own comment)] in the file's order."""
    text = io.open(os.path.join(mod_dir, 'features', 'eid_data.lua'), encoding='utf-8').read()
    found = []
    for line in text.split('\n'):
        m = re.match(r'\s*EID\.LuckFormulas\["([\d.]+)"\]\s*=\s*function\(luck\)\s*return\s+(.*?)\s+end'
                     r'\s*(?:--\s*(.*))?$', line)
        if not m:
            continue
        parts = m.group(1).split('.')
        if len(parts) != 3 or parts[0] != '5' or parts[1] not in VARIANTS:
            continue
        found.append((VARIANTS[parts[1]], int(parts[2]), to_c(m.group(2)),
                      (m.group(3) or '').strip()))
    return found


def main(mod_dir, out):
    rows = formulas(mod_dir)
    # the mod redefines a few of them further down the file; the last one wins,
    # exactly as it would when Lua runs the file top to bottom
    last = {}
    for row in rows:
        last[(row[0], row[1])] = row
    rows = [last[key] for key in sorted(last)]
    text = io.StringIO()
    text.write('/*\n * What a luck-based chance is worth at this much luck.\n *\n')
    text.write(' * Written by tools/eid_luck.py out of the mod\'s own LuckFormulas\n')
    text.write(' * (features/eid_data.lua) - do not edit here. The chance is a percentage,\n')
    text.write(' * clamped to 0..100 by the caller, exactly as the mod clamps it.\n */\n')
    text.write('#include <math.h>\n\n')
    text.write('static int eid_luck_chance(unsigned kind, unsigned id, float luck, float *out) {\n')
    text.write('    switch (kind * 10000u + id) {\n')
    for kind, ident, expr, note in rows:
        text.write('        case %du: *out = (float) (%s); return 1;%s\n'
                   % (kind * 10000 + ident, expr, ('  /* %s */' % note) if note else ''))
    text.write('        default: return 0;\n')
    text.write('    }\n}\n')
    io.open(out, 'w', encoding='utf-8', newline='\n').write(text.getvalue())
    print('%s: %d formulas' % (out, len(rows)))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else MOD,
         sys.argv[2] if len(sys.argv) > 2 else OUT)
