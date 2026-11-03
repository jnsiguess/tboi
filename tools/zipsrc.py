# -*- coding: utf-8 -*-
"""Build a zip with the port's sources.

We put in what the work cannot go on without, and leave out what gets built
again: no game image, no resource blob, no finished builds — they weigh
gigabytes and are restored from the ipa and from work/app.

Of the resources we take only the menu: that is what we work on, the other two
and a half gigabytes lie in work/app and are not needed in the zip. Backup
copies (*.before-*, *.orig, *REFERENCE*) we take as well — they show what to roll back
and from what.
"""
import io
import os
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = os.path.join(ROOT, 'isaac-port-source.zip')
INSIDE = 'isaac-port'

DOCUMENTS = ['CLAUDE.md', 'ПЕРЕДАЧА.md', 'ОТЧЁТ-ПРОВЕРКИ.md']

# project folders we take whole, and what we throw out of them
PROJECT = 'port'
SKIP_DIRS = {'build', '.cxx', '.gradle', '.idea', 'outputs', 'assets'}
SKIP_EXTS = {'.apk', '.pak', '.arm64', '.so', '.o', '.zip', '.ipa'}


def wanted(path):
    parts = path.replace('\\', '/').split('/')
    if any(p in SKIP_DIRS for p in parts):
        return False
    return os.path.splitext(path)[1].lower() not in SKIP_EXTS


def collect():
    files = []

    for d in DOCUMENTS:
        p = os.path.join(ROOT, d)
        if os.path.exists(p):
            files.append((p, d))

    for base, dirs, names in os.walk(os.path.join(ROOT, PROJECT)):
        dirs[:] = [p for p in dirs if p not in SKIP_DIRS]
        for n in names:
            p = os.path.join(base, n)
            rel = os.path.relpath(p, ROOT)
            if wanted(rel):
                files.append((p, rel))

    for n in sorted(os.listdir(os.path.join(ROOT, 'tools'))):
        if n.endswith('.py'):
            files.append((os.path.join(ROOT, 'tools', n), os.path.join('tools', n)))

    labels = os.path.join(ROOT, 'menu-labels')
    if os.path.isdir(labels):
        for base, _, names in os.walk(labels):
            for n in names:
                p = os.path.join(base, n)
                files.append((p, os.path.relpath(p, ROOT)))

    # menu resources: they are the only ones needed for work on the look
    for base, _, names in os.walk(os.path.join(ROOT, 'work', 'app')):
        if os.path.basename(base).lower() != 'main menu':
            continue
        for n in names:
            p = os.path.join(base, n)
            files.append((p, os.path.relpath(p, ROOT)))

    with zipfile.ZipFile(NAME, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for full, rel in files:
            z.write(full, INSIDE + '/' + rel.replace('\\', '/'))

    size = os.path.getsize(NAME)
    print('files %d, size %.1f MB' % (len(files), size / 1048576.0))
    return files


if __name__ == '__main__':
    collect()
