# -*- coding: utf-8 -*-
"""The file index at the end of MAP.md, written from the files themselves.

Each source's first header line - the first line of text in the opening
comment of a .c/.h/.S/.java file, the first line of a Python module's
docstring - goes into a table by folder. Everything above the index's
heading is kept as it is; the index is written anew. Both maps get it: the
Russian MAP.md (section 7) and the English docs-en/MAP.md (section 6, for
the English archive, tools/share_zip.py --english).

    py -3 tools/map_index.py
"""
import ast
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# (map, the index's heading, its words: the heading's title, the intro line, the table's head)
MAPS = [
    (os.path.join(ROOT, 'MAP.md'), re.compile(r'^## (\d+)\. Указатель файлов', re.M),
     ('Указатель файлов', 'Первая строка заголовка каждого исходника, как она написана в коде '
      '(`tools/map_index.py`).', '| Файл | Заголовок |')),
    (os.path.join(ROOT, 'docs-en', 'MAP.md'), re.compile(r'^## (\d+)\. File index', re.M),
     ('File index', 'The first header line of every source, as written in the code '
      '(`tools/map_index.py`).', '| File | Header |')),
]
FOLDERS = [
    ('port/app/src/main/cpp', ('.c', '.h', '.S')),
    ('port/app/src/main/java/ru/isaac/port', ('.java',)),
    ('tools', ('.py',)),
]
REFERENCE = re.compile(r'\.before-|\.orig$|\.broken')


def c_head(text):
    m = re.search(r'/\*\*?(.*?)\*/', text, re.S)
    if not m:
        return ''
    for line in m.group(1).split('\n'):
        line = line.strip().lstrip('*').strip()
        if line:
            return line
    return ''


def py_head(text):
    try:
        doc = ast.get_docstring(ast.parse(text))
    except SyntaxError:
        return ''
    if not doc:
        return ''
    return next((l.strip() for l in doc.split('\n') if l.strip()), '')


def rows(folder, exts):
    base = os.path.join(ROOT, *folder.split('/'))
    out = []
    for dp, dn, fn in os.walk(base):
        dn[:] = sorted(d for d in dn if d not in ('__pycache__', 'build'))
        if folder != 'tools' and dp != base:
            continue
        for f in sorted(fn):
            if not f.endswith(exts) or REFERENCE.search(f):
                continue
            path = os.path.join(dp, f)
            text = open(path, encoding='utf-8', errors='replace').read()
            head = py_head(text) if f.endswith('.py') else c_head(text)
            rel = os.path.relpath(path, base).replace(os.sep, '/')
            out.append((rel, head.replace('|', '\\|')))
    return out


def main():
    found = [(folder, rows(folder, exts)) for folder, exts in FOLDERS]
    for path, heading, (title, intro, head) in MAPS:
        if not os.path.exists(path):
            continue
        text = open(path, encoding='utf-8').read()
        m = heading.search(text)
        lines = [text[:m.start()].rstrip('\n'), '', '## %s. %s' % (m.group(1), title), '', intro, '']
        for folder, got in found:
            lines += ['### `%s/` (%d)' % (folder, len(got)), '', head, '|---|---|']
            lines += ['| `%s` | %s |' % (rel, h or '—') for rel, h in got]
            lines.append('')
        open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
        print('%s: index of %d files' % (os.path.relpath(path, ROOT), sum(len(g) for _, g in found)))


if __name__ == '__main__':
    main()
