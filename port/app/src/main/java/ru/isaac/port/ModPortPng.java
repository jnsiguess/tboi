package ru.isaac.port;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.Inflater;
import java.util.zip.InflaterInputStream;

/*
 * PICTURES FOR THE CONVERTER INSIDE THE INSTALLER (ModPort, 04.10.2026), row by row: a phone has
 * no room for an 8192 x 4096 sheet as one array (128 MB), so a picture is read a row at a time from
 * its own compressed bytes, scaled a block of rows at a time, and written a row at a time.
 *
 * Reading gives RGBA 8-bit as PIL's Image.open(...).convert('RGBA') does (tools/modport.py works on
 * that): every colour type and bit depth of PNG, Adam7 (that one is held whole - the passes are spread
 * over the picture), the palette's tRNS as alpha, a grey or RGB tRNS as the colour key PIL applies
 * (compared, as PIL does, with the unpacked value), 16-bit samples by their high byte.
 * Scaling is THE BOX RULE (modport.py box_down, modport.js boxDown): a picture whose every k x k
 * block is one colour gives that colour back; otherwise each block is the premultiplied mean,
 * integer-exact, round half up. Upscaling (a variant made the size of its base) repeats pixels.
 * Writing: RGBA 8-bit, no filter, zlib in IDAT chunks of at most 256 KB - the bytes differ from
 * PIL's, the pixels are the same (the desk test compares pixels).
 */
final class ModPortPng {
    private ModPortPng() { }

    /* a picture as rows: width, height, then height calls of row() in order */
    interface Rows {
        int width();
        int height();
        void row(byte[] rgba) throws IOException;     /* width * 4 bytes */
    }

    /* the same rows again from the start (a box makes two passes: is every block one colour?) */
    interface Source {
        Rows open() throws IOException;
    }

    static int[] size(byte[] d) {
        if (d == null || d.length < 24 || d[12] != 'I' || d[13] != 'H' || d[14] != 'D' || d[15] != 'R') return null;
        return new int[] {be32(d, 16), be32(d, 20)};
    }

    static int be32(byte[] d, int o) {
        return ((d[o] & 0xff) << 24) | ((d[o + 1] & 0xff) << 16) | ((d[o + 2] & 0xff) << 8) | (d[o + 3] & 0xff);
    }

    /* ------------------------------------------------------------------ reading */

    static Source decoder(final byte[] png) throws IOException {
        final Header h = new Header(png);
        return new Source() {
            @Override public Rows open() throws IOException {
                return h.interlace == 1 ? whole(h) : new Plain(h);
            }
        };
    }

    static final class Header {
        final byte[] png;
        int w, h, depth, color, interlace;
        byte[] plte;
        byte[] trns;
        final List<int[]> idat = new ArrayList<>();     /* offset, length */

        Header(byte[] png) throws IOException {
            this.png = png;
            if (png.length < 8 || (png[0] & 0xff) != 0x89 || png[1] != 'P' || png[2] != 'N' || png[3] != 'G')
                throw new IOException("not a png");
            int p = 8;
            boolean ihdr = false;
            while (p + 8 <= png.length) {
                int n = be32(png, p);
                String t = new String(png, p + 4, 4, java.nio.charset.StandardCharsets.ISO_8859_1);
                int at = p + 8;
                if (n < 0 || at + n > png.length) throw new IOException("a broken png chunk");
                switch (t) {
                    case "IHDR":
                        w = be32(png, at); h = be32(png, at + 4);
                        depth = png[at + 8] & 0xff; color = png[at + 9] & 0xff; interlace = png[at + 12] & 0xff;
                        ihdr = true;
                        break;
                    case "PLTE": plte = java.util.Arrays.copyOfRange(png, at, at + n); break;
                    case "tRNS": trns = java.util.Arrays.copyOfRange(png, at, at + n); break;
                    case "IDAT": idat.add(new int[] {at, n}); break;
                    default: break;
                }
                p = at + n + 4;
                if (t.equals("IEND")) break;
            }
            if (!ihdr || w <= 0 || h <= 0) throw new IOException("a png without its header");
        }

        int channels() {
            switch (color) {
                case 0: case 3: return 1;
                case 2: return 3;
                case 4: return 2;
                case 6: return 4;
                default: return 0;
            }
        }
        int bitsPerPixel() { return channels() * depth; }
        int stride(int width) { return (width * bitsPerPixel() + 7) / 8; }

        InputStream data() {
            final List<int[]> parts = idat;
            InputStream raw = new InputStream() {
                int i = 0, off = 0;
                @Override public int read() throws IOException {
                    byte[] one = new byte[1];
                    int n = read(one, 0, 1);
                    return n < 0 ? -1 : one[0] & 0xff;
                }
                @Override public int read(byte[] b, int o, int n) {
                    while (i < parts.size() && off >= parts.get(i)[1]) { i++; off = 0; }
                    if (i >= parts.size()) return -1;
                    int take = Math.min(n, parts.get(i)[1] - off);
                    System.arraycopy(png, parts.get(i)[0] + off, b, o, take);
                    off += take;
                    return take;
                }
            };
            return new InflaterInputStream(raw, new Inflater(), 1 << 16);
        }

        /* a filtered row (its filter byte first) unfiltered in place against the row above */
        void unfilter(byte[] cur, byte[] prev, int n) throws IOException {
            int f = cur[0] & 0xff, bpp = Math.max(1, bitsPerPixel() / 8);
            for (int i = 1; i <= n; i++) {
                int a = i - bpp >= 1 ? cur[i - bpp] & 0xff : 0, b = prev[i] & 0xff, c = i - bpp >= 1 ? prev[i - bpp] & 0xff : 0;
                int x = cur[i] & 0xff, v;
                switch (f) {
                    case 0: v = x; break;
                    case 1: v = x + a; break;
                    case 2: v = x + b; break;
                    case 3: v = x + ((a + b) >> 1); break;
                    case 4: {
                        int pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
                        v = x + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
                        break;
                    }
                    default: throw new IOException("a png row with filter " + f);
                }
                cur[i] = (byte) v;
            }
        }

        /* one unfiltered row (from byte 1) as RGBA, as PIL's convert('RGBA') gives it */
        void rgba(byte[] raw, int width, byte[] out) {
            int d = depth;
            for (int x = 0; x < width; x++) {
                int r, g, b, a = 255;
                switch (color) {
                    case 0: {
                        int v = sample(raw, x, d);
                        int l = scaleGrey(v, d);
                        r = g = b = l;
                        if (trns != null && trns.length >= 2 && l == (((trns[0] & 0xff) << 8) | (trns[1] & 0xff))) a = 0;
                        break;
                    }
                    case 2: {
                        int o = 1 + x * 3 * (d / 8);
                        r = raw[o] & 0xff; g = raw[o + d / 8] & 0xff; b = raw[o + 2 * (d / 8)] & 0xff;
                        if (trns != null && trns.length >= 6 && r == (((trns[0] & 0xff) << 8) | (trns[1] & 0xff))
                                && g == (((trns[2] & 0xff) << 8) | (trns[3] & 0xff)) && b == (((trns[4] & 0xff) << 8) | (trns[5] & 0xff)))
                            a = 0;
                        break;
                    }
                    case 3: {
                        int i = sample(raw, x, d);
                        r = g = b = 0;
                        if (plte != null && i * 3 + 2 < plte.length) { r = plte[i * 3] & 0xff; g = plte[i * 3 + 1] & 0xff; b = plte[i * 3 + 2] & 0xff; }
                        if (trns != null && i < trns.length) a = trns[i] & 0xff;
                        break;
                    }
                    case 4: {
                        int o = 1 + x * 2 * (d / 8);
                        r = g = b = raw[o] & 0xff;
                        a = raw[o + d / 8] & 0xff;
                        break;
                    }
                    default: {
                        int o = 1 + x * 4 * (d / 8);
                        r = raw[o] & 0xff; g = raw[o + d / 8] & 0xff; b = raw[o + 2 * (d / 8)] & 0xff; a = raw[o + 3 * (d / 8)] & 0xff;
                        break;
                    }
                }
                out[x * 4] = (byte) r; out[x * 4 + 1] = (byte) g; out[x * 4 + 2] = (byte) b; out[x * 4 + 3] = (byte) a;
            }
        }

        /* a grey or palette sample of `d` bits (16: its high byte) */
        static int sample(byte[] raw, int x, int d) {
            if (d == 8) return raw[1 + x] & 0xff;
            if (d == 16) return raw[1 + x * 2] & 0xff;
            int per = 8 / d, by = raw[1 + x / per] & 0xff, shift = 8 - d * (x % per + 1);
            return (by >> shift) & ((1 << d) - 1);
        }

        /* PIL unpacks grey of 1, 2, 4 bits stretched to 0..255 */
        int scaleGrey(int v, int d) {
            switch (d) {
                case 1: return v * 255;
                case 2: return v * 85;
                case 4: return v * 17;
                default: return v;
            }
        }
    }

    private static final class Plain implements Rows {
        final Header h;
        final InputStream in;
        byte[] cur, prev;
        final int n;
        Plain(Header h) {
            this.h = h;
            this.in = h.data();
            n = h.stride(h.w);
            cur = new byte[n + 1];
            prev = new byte[n + 1];
        }
        @Override public int width() { return h.w; }
        @Override public int height() { return h.h; }
        @Override public void row(byte[] out) throws IOException {
            byte[] t = prev; prev = cur; cur = t;
            int got = 0;
            while (got < n + 1) {
                int k = in.read(cur, got, n + 1 - got);
                if (k < 0) throw new IOException("the png ends early");
                got += k;
            }
            h.unfilter(cur, prev, n);
            h.rgba(cur, h.w, out);
        }
    }

    /* Adam7: the passes are spread over the picture, so it is held whole (RGBA) */
    private static Rows whole(final Header h) throws IOException {
        final byte[] img = new byte[h.w * h.h * 4];
        int[][] passes = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
        InputStream in = h.data();
        for (int[] ps : passes) {
            int pw = (h.w - ps[0] + ps[2] - 1) / ps[2], ph = (h.h - ps[1] + ps[3] - 1) / ps[3];
            if (pw <= 0 || ph <= 0) continue;
            int n = h.stride(pw);
            byte[] cur = new byte[n + 1], prev = new byte[n + 1], px = new byte[pw * 4];
            for (int y = 0; y < ph; y++) {
                int got = 0;
                while (got < n + 1) {
                    int k = in.read(cur, got, n + 1 - got);
                    if (k < 0) throw new IOException("the png ends early");
                    got += k;
                }
                h.unfilter(cur, prev, n);
                h.rgba(cur, pw, px);
                int yy = ps[1] + y * ps[3];
                for (int x = 0; x < pw; x++) System.arraycopy(px, x * 4, img, (yy * h.w + ps[0] + x * ps[2]) * 4, 4);
                byte[] t = prev; prev = cur; cur = t;
            }
        }
        return new Rows() {
            int y = 0;
            @Override public int width() { return h.w; }
            @Override public int height() { return h.h; }
            @Override public void row(byte[] out) { System.arraycopy(img, (y++) * h.w * 4, out, 0, h.w * 4); }
        };
    }

    /* ------------------------------------------------------------------ scaling */

    /* each pixel repeated m times across and down (PIL's NEAREST at a whole factor up) */
    static Source nearestUp(final Source src, final int m) {
        return new Source() {
            @Override public Rows open() throws IOException {
                final Rows r = src.open();
                return new Rows() {
                    final byte[] in = new byte[r.width() * 4];
                    int left = 0;
                    @Override public int width() { return r.width() * m; }
                    @Override public int height() { return r.height() * m; }
                    @Override public void row(byte[] out) throws IOException {
                        if (left == 0) { r.row(in); left = m; }
                        left--;
                        for (int x = 0; x < r.width(); x++)
                            for (int i = 0; i < m; i++) System.arraycopy(in, x * 4, out, (x * m + i) * 4, 4);
                    }
                };
            }
        };
    }

    /*
     * THE BOX RULE down by k to ow x oh (the source's first ow*k columns and oh*k rows: halving a
     * side of odd length drops its last row or column). Two passes: is every block one colour? then the rows.
     */
    static Source boxDown(final Source src, final int k, final int ow, final int oh) {
        return new Source() {
            Boolean uniform;
            @Override public Rows open() throws IOException {
                if (uniform == null) uniform = allUniform(src, k, ow, oh);
                final Rows r = src.open();
                final boolean u = uniform;
                return new Rows() {
                    final byte[][] block = new byte[k][r.width() * 4];
                    int y = 0;
                    @Override public int width() { return ow; }
                    @Override public int height() { return oh; }
                    @Override public void row(byte[] out) throws IOException {
                        for (int i = 0; i < k; i++) r.row(block[i]);
                        y++;
                        int n = k * k;
                        for (int x = 0; x < ow; x++) {
                            int o = x * 4;
                            if (u) { System.arraycopy(block[0], x * k * 4, out, o, 4); continue; }
                            long rs = 0, gs = 0, bs = 0, as = 0;
                            for (int yy = 0; yy < k; yy++) {
                                byte[] row = block[yy];
                                for (int xx = 0; xx < k; xx++) {
                                    int i = (x * k + xx) * 4, al = row[i + 3] & 0xff;
                                    rs += (row[i] & 0xff) * al; gs += (row[i + 1] & 0xff) * al; bs += (row[i + 2] & 0xff) * al; as += al;
                                }
                            }
                            out[o + 3] = (byte) ((2 * as + n) / (2 * n));
                            if (as > 0) {
                                out[o] = (byte) ((2 * rs + as) / (2 * as));
                                out[o + 1] = (byte) ((2 * gs + as) / (2 * as));
                                out[o + 2] = (byte) ((2 * bs + as) / (2 * as));
                            } else {
                                out[o] = out[o + 1] = out[o + 2] = 0;
                            }
                        }
                    }
                };
            }
        };
    }

    private static boolean allUniform(Source src, int k, int ow, int oh) throws IOException {
        Rows r = src.open();
        byte[][] block = new byte[k][r.width() * 4];
        for (int y = 0; y < oh; y++) {
            for (int i = 0; i < k; i++) r.row(block[i]);
            for (int x = 0; x < ow; x++) {
                int o = x * k * 4;
                for (int yy = 0; yy < k; yy++)
                    for (int xx = 0; xx < k; xx++) {
                        int i = (x * k + xx) * 4;
                        byte[] row = block[yy];
                        if (row[i] != block[0][o] || row[i + 1] != block[0][o + 1] || row[i + 2] != block[0][o + 2] || row[i + 3] != block[0][o + 3])
                            return false;
                    }
            }
        }
        return true;
    }

    /* ------------------------------------------------------------------ writing */

    static void write(Source src, OutputStream out) throws IOException {
        Rows r = src.open();
        int w = r.width(), h = r.height();
        out.write(new byte[] {(byte) 0x89, 'P', 'N', 'G', 13, 10, 26, 10});
        byte[] ihdr = new byte[13];
        put32(ihdr, 0, w); put32(ihdr, 4, h);
        ihdr[8] = 8; ihdr[9] = 6;
        chunk(out, "IHDR", ihdr, 13);
        Deflater d = new Deflater(Deflater.DEFAULT_COMPRESSION);
        ByteArrayOutputStream pending = new ByteArrayOutputStream();
        byte[] row = new byte[w * 4 + 1], buf = new byte[1 << 16];
        byte[] px = new byte[w * 4];
        for (int y = 0; y < h; y++) {
            r.row(px);
            row[0] = 0;
            System.arraycopy(px, 0, row, 1, px.length);
            d.setInput(row);
            while (!d.needsInput()) {
                int n = d.deflate(buf);
                pending.write(buf, 0, n);
            }
            if (pending.size() >= 256 * 1024) { chunk(out, "IDAT", pending.toByteArray(), pending.size()); pending.reset(); }
        }
        d.finish();
        while (!d.finished()) {
            int n = d.deflate(buf);
            pending.write(buf, 0, n);
        }
        d.end();
        if (pending.size() > 0) chunk(out, "IDAT", pending.toByteArray(), pending.size());
        chunk(out, "IEND", new byte[0], 0);
    }

    static byte[] bytes(Source src) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        write(src, b);
        return b.toByteArray();
    }

    private static void put32(byte[] b, int o, int v) {
        b[o] = (byte) (v >>> 24); b[o + 1] = (byte) (v >>> 16); b[o + 2] = (byte) (v >>> 8); b[o + 3] = (byte) v;
    }

    private static void chunk(OutputStream out, String type, byte[] data, int n) throws IOException {
        byte[] head = new byte[8];
        put32(head, 0, n);
        byte[] t = type.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1);
        System.arraycopy(t, 0, head, 4, 4);
        out.write(head);
        out.write(data, 0, n);
        CRC32 c = new CRC32();
        c.update(t);
        c.update(data, 0, n);
        byte[] crc = new byte[4];
        put32(crc, 0, (int) c.getValue());
        out.write(crc);
    }
}
