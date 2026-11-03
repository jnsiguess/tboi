# -*- coding: utf-8 -*-
"""Apple's "crushed" PNG back into an ordinary one.

Xcode rewrites every PNG in a bundle into a private variant, and a normal
decoder refuses it with "broken data stream". Three things are changed and all
three are undone here:

    a CgBI chunk before IHDR   - the mark of the variant, dropped
    IDAT without a zlib head  - inflated raw (wbits -15)
    BGRA, alpha premultiplied - swapped back and divided out

Only what the icons need is handled: bit depth 8, colour type 6 (RGBA), no
interlace. Anything else is refused out loud rather than quietly mangled.

    py -3 tools/uncrush.py FILE [FILE ...]
"""
import struct
import sys
import zlib


def chunks(d):
    at = 8
    while at < len(d):
        (length,) = struct.unpack_from('>I', d, at)
        kind = d[at + 4:at + 8]
        body = d[at + 8:at + 8 + length]
        yield kind, body
        at += 12 + length


def unfilter(raw, w, h, bpp):
    """The five PNG scanline filters, undone in place."""
    stride = w * bpp
    out = bytearray(h * stride)
    at = 0
    for y in range(h):
        f = raw[at]
        at += 1
        line = bytearray(raw[at:at + stride])
        at += stride
        up = out[(y - 1) * stride:y * stride] if y else bytes(stride)
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = up[x]
            c = up[x - bpp] if x >= bpp else 0
            if f == 0:
                v = line[x]
            elif f == 1:
                v = line[x] + a
            elif f == 2:
                v = line[x] + b
            elif f == 3:
                v = line[x] + ((a + b) >> 1)
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                v = line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))
            else:
                raise SystemExit('unknown filter %d on row %d' % (f, y))
            line[x] = v & 0xFF
        out[y * stride:(y + 1) * stride] = line
    return out


def convert(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit('%s: not a PNG' % path)
    crushed = False
    idat = bytearray()
    w = h = None
    for kind, body in chunks(d):
        if kind == b'CgBI':
            crushed = True
        elif kind == b'IHDR':
            w, h, depth, colour, comp, filt, interlace = struct.unpack('>IIBBBBB', body)
            if depth != 8 or colour != 6 or interlace:
                raise SystemExit('%s: depth %d, colour %d, interlace %d - not handled'
                                 % (path, depth, colour, interlace))
        elif kind == b'IDAT':
            idat += body
        elif kind == b'IEND':
            break
    if not crushed:
        return None
    raw = zlib.decompressobj(-15).decompress(bytes(idat))
    px = unfilter(raw, w, h, 4)
    for i in range(0, len(px), 4):
        b, g, r, a = px[i], px[i + 1], px[i + 2], px[i + 3]
        if a:
            r = min(255, r * 255 // a)
            g = min(255, g * 255 // a)
            b = min(255, b * 255 // a)
        px[i], px[i + 1], px[i + 2] = r, g, b
    body = bytearray()
    stride = w * 4
    for y in range(h):
        body.append(0)
        body += px[y * stride:(y + 1) * stride]

    def chunk(kind, data):
        return (struct.pack('>I', len(data)) + kind + data
                + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF))

    out = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(bytes(body), 9))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(out)
    return w, h


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for p in sys.argv[1:]:
        size = convert(p)
        print('%s: %s' % (p, 'already plain' if size is None else '%dx%d uncrushed' % size))
