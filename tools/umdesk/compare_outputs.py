# -*- coding: utf-8 -*-
"""Two converter outputs (zips) compared the way test_converters.py needs it.

    py -3 tools/umdesk/compare_outputs.py <a.zip> <b.zip> [--tree]

Same file list (names exactly); then per file:
  .png       the decoded RGBA pixels (PIL, convert('RGBA')) - encoders differ, pixels must not;
  .anm2/.xml the bytes, or with --tree the element tree (tag, attributes in order, text, tail with the
             whitespace of ET) - the page's XMLSerializer writes other bytes for the same tree;
  mod.json   the JSON value;
  the rest   the bytes.
Prints every difference; returns the count.
"""
import io
import json
import sys
import zipfile
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image


def tree(b):
    r = ET.fromstring(b)
    out = []

    def walk(e):
        out.append((e.tag, tuple(e.attrib.items()), (e.text or '').strip(), (e.tail or '').strip()))
        for c in e:
            walk(c)
        out.append('/' + e.tag)
    walk(r)
    return out


def pixels(b):
    return np.array(Image.open(io.BytesIO(b)).convert('RGBA'))


def compare(a, b, by_tree=False, quiet=False):
    za, zb = zipfile.ZipFile(a), zipfile.ZipFile(b)
    na, nb = set(za.namelist()), set(zb.namelist())
    diffs = []
    for n in sorted(na - nb):
        diffs.append('only in %s: %s' % (a, n))
    for n in sorted(nb - na):
        diffs.append('only in %s: %s' % (b, n))
    for n in sorted(na & nb):
        x, y = za.read(n), zb.read(n)
        if x == y:
            continue
        low = n.lower()
        if low == 'mod.json':
            if json.loads(x) != json.loads(y):
                diffs.append('%s: the manifests differ' % n)
        elif low.endswith('.png'):
            try:
                px, py = pixels(x), pixels(y)
            except Exception as e:
                diffs.append('%s: does not decode (%s)' % (n, e))
                continue
            if px.shape != py.shape:
                diffs.append('%s: %s against %s' % (n, px.shape, py.shape))
            elif (px != py).any():
                d = np.abs(px.astype(int) - py.astype(int)).max(axis=2)
                diffs.append('%s: %d pixels differ, by up to %d' % (n, int((d > 0).sum()), int(d.max())))
        elif by_tree and low.endswith(('.anm2', '.xml')):
            try:
                if tree(x) != tree(y):
                    diffs.append('%s: the trees differ' % n)
            except ET.ParseError as e:
                diffs.append('%s: %s' % (n, e))
        else:
            diffs.append('%s: the bytes differ (%d against %d)' % (n, len(x), len(y)))
    if not quiet:
        for d in diffs[:40]:
            print('   ' + d)
        if len(diffs) > 40:
            print('   ... and %d more' % (len(diffs) - 40))
    return diffs


if __name__ == '__main__':
    d = compare(sys.argv[1], sys.argv[2], '--tree' in sys.argv)
    print('%d differences' % len(d))
    sys.exit(1 if d else 0)
