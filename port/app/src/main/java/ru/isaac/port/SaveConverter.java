package ru.isaac.port;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.util.TreeMap;

/*
 * THE PERSISTENT SAVES, IN AND OUT (the owner's item 9, 23.09.2026).
 *
 * The format, measured in the iOS image and the PC's isaac-ng.exe 1.7.9b alike
 * (the handoff notes, item 9): the header "ISAACNGSAVE09R  ", a link to the run save
 * at 0x10 that nobody checks, then chunks from 0x14 - `type, size, count` and
 * the elements - types 1..10, then the bestiary (11), then a save counter and a
 * CRC of [0x10, len-4) in the last four bytes. The CRC is the save editor's the
 * owner downloaded (Z:\Isaac\converter), table and seed copied from its script.
 *
 * A save is taken exactly as the game would take it (02.10.2026, the same
 * model as saves.c, game_read): the game's own reader 0x6ff7f0, rule for rule
 * (gameRead below). Whatever file it reads, IMPORT SAVES takes - a count with
 * a stray top byte, a count past the end under a sane size field, a file cut
 * short - and only what it refuses (another header, a checksum that does not
 * match) is refused here. The state it reads is written out CLEAN: every
 * chunk at the Repentance count, the right sizes and checksum.
 *
 * One thing on top, as in saves.c: a file in the writer's own layout, read to
 * the byte, is first rebuilt at the Repentance counts with its checksum put
 * right (layoutMend). The game reads a chunk element by element up to its own
 * count and does not skip what is over it: a longer chunk (Repentance+'s 642
 * achievements) would slip its stream and lose everything after it.
 *
 *   in:  Repentance (PC or iOS), Repentance+ online, Afterbirth+ (09R with
 *        shorter chunks), a port save written LZ4-packed before the port wrote
 *        them plain. Rebirth and Afterbirth (06R..08R) only the game itself
 *        converts, from their own file names.
 *   out: the Repentance layout, plain - what the PC reads as well.
 */
final class SaveConverter {
    private SaveConverter() { }

    static final byte[] HEADER = "ISAACNGSAVE09R  ".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    static final int HEADER_OFFSET = 16;
    static final long CHECKSUM_SEED = 4275878518L;

    /* chunks 1..10: element size, and Repentance's count */
    private static final int[] ELEM = {1, 4, 4, 1, 1, 1, 1, 4, 4, 1};
    private static final int[] COUNT = {638, 496, 14, 733, 7, 104, 46, 27, 2, 80};
    private static final int ONLINE_ACHIEVEMENTS = 642;

    /* the editor's own table (CRC_TABLE in its script) - not the standard CRC-32 one */
    private static final int[] CRC = {
        0x00000000, 0x09073096, 0x120E612C, 0x1B0951BA, 0xFF6DC419, 0xF66AF48F,
        0xED63A535, 0xE46495A3, 0xFEDB8832, 0xF7DCB8A4, 0xECD5E91E, 0xE5D2D988,
        0x01B64C2B, 0x08B17CBD, 0x13B82D07, 0x1ABF1D91, 0xFDB71064, 0xF4B020F2,
        0xEFB97148, 0xE6BE41DE, 0x02DAD47D, 0x0BDDE4EB, 0x10D4B551, 0x19D385C7,
        0x036C9856, 0x0A6BA8C0, 0x1162F97A, 0x1865C9EC, 0xFC015C4F, 0xF5066CD9,
        0xEE0F3D63, 0xE7080DF5, 0xFB6E20C8, 0xF269105E, 0xE96041E4, 0xE0677172,
        0x0403E4D1, 0x0D04D447, 0x160D85FD, 0x1F0AB56B, 0x05B5A8FA, 0x0CB2986C,
        0x17BBC9D6, 0x1EBCF940, 0xFAD86CE3, 0xF3DF5C75, 0xE8D60DCF, 0xE1D13D59,
        0x06D930AC, 0x0FDE003A, 0x14D75180, 0x1DD06116, 0xF9B4F4B5, 0xF0B3C423,
        0xEBBA9599, 0xE2BDA50F, 0xF802B89E, 0xF1058808, 0xEA0CD9B2, 0xE30BE924,
        0x076F7C87, 0x0E684C11, 0x15611DAB, 0x1C662D3D, 0xF6DC4190, 0xFFDB7106,
        0xE4D220BC, 0xEDD5102A, 0x09B18589, 0x00B6B51F, 0x1BBFE4A5, 0x12B8D433,
        0x0807C9A2, 0x0100F934, 0x1A09A88E, 0x130E9818, 0xF76A0DBB, 0xFE6D3D2D,
        0xE5646C97, 0xEC635C01, 0x0B6B51F4, 0x026C6162, 0x196530D8, 0x1062004E,
        0xF40695ED, 0xFD01A57B, 0xE608F4C1, 0xEF0FC457, 0xF5B0D9C6, 0xFCB7E950,
        0xE7BEB8EA, 0xEEB9887C, 0x0ADD1DDF, 0x03DA2D49, 0x18D37CF3, 0x11D44C65,
        0x0DB26158, 0x04B551CE, 0x1FBC0074, 0x16BB30E2, 0xF2DFA541, 0xFBD895D7,
        0xE0D1C46D, 0xE9D6F4FB, 0xF369E96A, 0xFA6ED9FC, 0xE1678846, 0xE860B8D0,
        0x0C042D73, 0x05031DE5, 0x1E0A4C5F, 0x170D7CC9, 0xF005713C, 0xF90241AA,
        0xE20B1010, 0xEB0C2086, 0x0F68B525, 0x066F85B3, 0x1D66D409, 0x1461E49F,
        0x0EDEF90E, 0x07D9C998, 0x1CD09822, 0x15D7A8B4, 0xF1B33D17, 0xF8B40D81,
        0xE3BD5C3B, 0xEABA6CAD, 0xEDB88320, 0xE4BFB3B6, 0xFFB6E20C, 0xF6B1D29A,
        0x12D54739, 0x1BD277AF, 0x00DB2615, 0x09DC1683, 0x13630B12, 0x1A643B84,
        0x016D6A3E, 0x086A5AA8, 0xEC0ECF0B, 0xE509FF9D, 0xFE00AE27, 0xF7079EB1,
        0x100F9344, 0x1908A3D2, 0x0201F268, 0x0B06C2FE, 0xEF62575D, 0xE66567CB,
        0xFD6C3671, 0xF46B06E7, 0xEED41B76, 0xE7D32BE0, 0xFCDA7A5A, 0xF5DD4ACC,
        0x11B9DF6F, 0x18BEEFF9, 0x03B7BE43, 0x0AB08ED5, 0x16D6A3E8, 0x1FD1937E,
        0x04D8C2C4, 0x0DDFF252, 0xE9BB67F1, 0xE0BC5767, 0xFBB506DD, 0xF2B2364B,
        0xE80D2BDA, 0xE10A1B4C, 0xFA034AF6, 0xF3047A60, 0x1760EFC3, 0x1E67DF55,
        0x056E8EEF, 0x0C69BE79, 0xEB61B38C, 0xE266831A, 0xF96FD2A0, 0xF068E236,
        0x140C7795, 0x1D0B4703, 0x060216B9, 0x0F05262F, 0x15BA3BBE, 0x1CBD0B28,
        0x07B45A92, 0x0EB36A04, 0xEAD7FFA7, 0xE3D0CF31, 0xF8D99E8B, 0xF1DEAE1D,
        0x1B64C2B0, 0x1263F226, 0x096AA39C, 0x006D930A, 0xE40906A9, 0xED0E363F,
        0xF6076785, 0xFF005713, 0xE5BF4A82, 0xECB87A14, 0xF7B12BAE, 0xFEB61B38,
        0x1AD28E9B, 0x13D5BE0D, 0x08DCEFB7, 0x01DBDF21, 0xE6D3D2D4, 0xEFD4E242,
        0xF4DDB3F8, 0xFDDA836E, 0x19BE16CD, 0x10B9265B, 0x0BB077E1, 0x02B74777,
        0x18085AE6, 0x110F6A70, 0x0A063BCA, 0x03010B5C, 0xE7659EFF, 0xEE62AE69,
        0xF56BFFD3, 0xFC6CCF45, 0xE00AE278, 0xE90DD2EE, 0xF2048354, 0xFB03B3C2,
        0x1F672661, 0x166016F7, 0x0D69474D, 0x046E77DB, 0x1ED16A4A, 0x17D65ADC,
        0x0CDF0B66, 0x05D83BF0, 0xE1BCAE53, 0xE8BB9EC5, 0xF3B2CF7F, 0xFAB5FFE9,
        0x1DBDF21C, 0x14BAC28A, 0x0FB39330, 0x06B4A3A6, 0xE2D03605, 0xEBD70693,
        0xF0DE5729, 0xF9D967BF, 0xE3667A2E, 0xEA614AB8, 0xF1681B02, 0xF86F2B94,
        0x1C0BBE37, 0x150C8EA1, 0x0E05DF1B, 0x0702EF8D,
    };

    /* the editor's calculateChecksum: seeded with ~CHECKSUM, over [from, from + len) */
    static int checksum(byte[] d, int from, int len) {
        int s = (int) ~CHECKSUM_SEED;
        for (int i = from; i < from + len; i++) s = CRC[(s ^ d[i]) & 0xff] ^ (s >>> 8);
        return ~s;
    }

    private static int u32(byte[] d, int at) {
        return (d[at] & 0xff) | (d[at + 1] & 0xff) << 8 | (d[at + 2] & 0xff) << 16 | (d[at + 3] & 0xff) << 24;
    }

    private static boolean headed(byte[] d) {
        if (d == null || d.length < 16) return false;
        for (int i = 0; i < 16; i++) if (d[i] != HEADER[i]) return false;
        return true;
    }

    /* an LZ4 block, as the port wrote saves before it wrote them plain (saves.c: lz4_unblock) */
    private static byte[] lz4(byte[] in) {
        byte[] o = new byte[2 << 20];
        int ip = 0, op = 0;
        try {
            while (ip < in.length) {
                int token = in[ip++] & 0xff;
                int lit = token >>> 4;
                if (lit == 15) { int b; do { b = in[ip++] & 0xff; lit += b; } while (b == 255); }
                System.arraycopy(in, ip, o, op, lit);
                ip += lit;
                op += lit;
                if (ip >= in.length) break;
                int off = (in[ip] & 0xff) | (in[ip + 1] & 0xff) << 8;
                ip += 2;
                if (off == 0 || off > op) return null;
                int len = token & 15;
                if (len == 15) { int b; do { b = in[ip++] & 0xff; len += b; } while (b == 255); }
                len += 4;
                for (int k = 0; k < len; k++) { o[op] = o[op - off]; op++; }
            }
        } catch (RuntimeException e) {
            return null;
        }
        return java.util.Arrays.copyOf(o, op);
    }

    /* a save, understood: what the game's reader leaves of it */
    private static final class Save {
        final byte[][] chunk = new byte[10][];      /* at the Repentance counts, as the reader stores them */
        final int[] count = new int[10];
        final TreeMap<Integer, TreeMap<Integer, Integer>> bestiary = new TreeMap<>();
        int counter;
        boolean online;                             /* Repentance+ online: 642 achievements in the writer's layout */
        String note;                                /* for the report: read by the game's own rules, or the hang */
    }

    /* where the reader puts chunks 1..10 in its block (saves.c: CHUNKS) */
    private static final int[] AT = {0x28, 0x2a8, 0xd48, 0xa68, 0xd80, 0xd87, 0xdef, 0xe20, 0xe8c, 0xe94};
    private static final int BLOCK_COUNTER = 0xf4c, BLOCK_BYTES = 0xf58;
    private static final int SAVE_CAP = 2 << 20;    /* the game's read buffer */
    /* bestiary turns past the end of the file that hold the game for seconds and more (saves.c: HANG_TURNS) */
    private static final long HANG_TURNS = 1L << 24;

    /*
     * The file walked as its writer laid it out (saves.c: layout_clean): every
     * chunk 1..10 where the counts before it put it, the bestiary's lists end
     * exactly eight bytes before the end of the file.
     */
    private static boolean layoutClean(byte[] d) {
        int n = d.length;
        if (n < 0x20 || !headed(d)) return false;
        long in = 0x14;
        for (int t = 0; t < 10; t++) {
            if (n - in < 12 || u32(d, (int) in) != t + 1) return false;
            long bytes = (u32(d, (int) in + 8) & 0xffffffffL) * ELEM[t];
            if (bytes > n - in - 12) return false;
            in += 12 + bytes;
        }
        if (n - in < 12 || u32(d, (int) in) != 11) return false;
        long lists = u32(d, (int) in + 8) & 0xffffffffL;
        in += 12;
        for (long k = 0; k < lists; k++) {
            if (n - in < 8) return false;
            long sub = u32(d, (int) in + 4) & 0xffffffffL;
            if (sub % 4 != 0 || sub * 2 > n - in - 8) return false;
            in += 8 + sub * 2;
        }
        return n - in == 8;
    }

    private static void put32(byte[] d, int at, int v) {
        for (int k = 0; k < 4; k++) d[at + k] = (byte) (v >>> (8 * k));
    }

    /* a file layoutClean passed, at the Repentance counts and with its checksum right (saves.c: layout_mend) */
    private static byte[] layoutMend(byte[] d) {
        byte[] o = new byte[d.length];
        System.arraycopy(d, 0, o, 0, 0x14);
        int in = 0x14, out = 0x14;
        for (int t = 0; t < 10; t++) {
            long count = u32(d, in + 8) & 0xffffffffL;
            int keep = (int) Math.min(count, COUNT[t]);
            put32(o, out, t + 1);
            put32(o, out + 4, t == 0 ? keep : keep * 4);
            put32(o, out + 8, keep);
            System.arraycopy(d, in + 12, o, out + 12, keep * ELEM[t]);
            in += 12 + (int) (count * ELEM[t]);
            out += 12 + keep * ELEM[t];
        }
        System.arraycopy(d, in, o, out, d.length - in);
        out += d.length - in;
        o = java.util.Arrays.copyOf(o, out);
        put32(o, out - 4, checksum(o, HEADER_OFFSET, out - HEADER_OFFSET - 4));
        return o;
    }

    /*
     * THE GAME'S READER (0x6ff7f0), rule for rule as saves.c's game_read: the
     * header and checksum, then chunks as they come by their type - for a
     * non-zero size the count, then min(count, Repentance's) elements and no
     * more than the size field allows, NOTHING skipped; a read past the end
     * takes nothing and leaves the variable as it was; the loop ends at the end
     * of the file or at a type outside 0..10; then the save counter. The
     * bestiary's list count and lengths are bounded by nothing: past the end
     * every turn puts the same pair again, so the rest is counted, not run.
     */
    private static final class Reader {
        final byte[] d;
        final int n;
        int at;
        final byte[] blk = new byte[BLOCK_BYTES];
        final TreeMap<Integer, TreeMap<Integer, Integer>> maps = new TreeMap<>();
        final byte[] v58 = new byte[4], v54 = new byte[4], v50 = new byte[4], v4c = new byte[4], v48 = new byte[4];
        long pastEnd;
        int oddType, oddCount, oddRead, stop;

        Reader(byte[] d, int n) { this.d = d; this.n = n; }

        /* the game's stream: what is left, at most k bytes; the rest of `to` stays as it was */
        void take(byte[] to, int off, int k) {
            int got = Math.min(k, n - at);
            System.arraycopy(d, at, to, off, got);
            at += got;
        }

        void pair(int id, byte[] key, byte[] value) {
            maps.computeIfAbsent(id, x -> new TreeMap<>()).put(u32(key, 0), u32(value, 0));
        }

        void bestiary() {
            maps.clear();
            take(v58, 0, 4);
            long lists = u32(v58, 0) & 0xffffffffL;
            for (long j = 0; j < lists; j++) {
                if (at >= n) {
                    int id = u32(v54, 0), size = u32(v50, 0);
                    boolean fills = Integer.compareUnsigned(id - 1, 4) < 0 && size >= 4;
                    pastEnd += (lists - j) * (1 + (fills ? (size >>> 2) : 0));
                    if (fills) pair(id, v4c, v48);
                    return;
                }
                take(v54, 0, 4);
                int id = u32(v54, 0);
                if (Integer.compareUnsigned(id - 1, 4) >= 0) continue;
                take(v50, 0, 4);
                int size = u32(v50, 0);
                if (size < 4) continue;
                int entries = size >>> 2;
                for (int e = 0; e < entries; e++) {
                    if (at >= n) {
                        pastEnd += entries - e;
                        pair(id, v4c, v48);
                        break;
                    }
                    take(v4c, 0, 4);
                    take(v48, 0, 4);
                    pair(id, v4c, v48);
                }
            }
        }

        void run() {
            at = 0x10;
            byte[] type = new byte[4], size = new byte[4], count = new byte[4];
            take(blk, 0xf48, 4);
            while (at < n) {
                take(type, 0, 4);                   /* not cleared before: a short read keeps the old type */
                java.util.Arrays.fill(size, (byte) 0);
                take(size, 0, 4);
                int t = u32(type, 0);
                long sz = u32(size, 0) & 0xffffffffL;
                if (sz != 0 && Integer.compareUnsigned(t - 1, 10) < 0) {
                    int want = COUNT[t - 1], at0 = AT[t - 1];
                    java.util.Arrays.fill(count, (byte) 0);
                    take(count, 0, 4);
                    int c = u32(count, 0);
                    long m = Integer.compareUnsigned(c, want) < 0 ? (c & 0xffffffffL) : want;
                    long k = 0;
                    if (m != 0) {
                        if (ELEM[t - 1] == 1) {
                            do {
                                take(v58, 0, 1);
                                blk[at0 + (int) k] = (byte) (v58[0] > 0 ? 1 : 0);
                                k++;
                            } while (k < m && k < sz);
                        } else {
                            do {
                                take(v58, 0, 4);
                                System.arraycopy(v58, 0, blk, at0 + 4 * (int) k, 4);
                                k++;
                            } while (k < m && 4 * k < sz);
                        }
                    }
                    if (c != want && oddType == 0) { oddType = t; oddCount = c; oddRead = (int) k; }
                } else if (sz != 0 && t == 11) {
                    bestiary();
                }
                if (at >= n || t < 0 || t >= 11) break;
            }
            stop = at;
            take(blk, BLOCK_COUNTER, 4);
        }
    }

    private static Save parse(byte[] d) throws IOException {
        if (!headed(d)) {
            byte[] un = d != null && d.length > 16 ? lz4(d) : null;
            if (un == null || !headed(un)) {
                if (d != null && d.length > 16
                        && new String(d, 0, 11, java.nio.charset.StandardCharsets.US_ASCII).equals("ISAACNGSAVE"))
                    throw new IOException(UserMods.t("сейв Rebirth/Afterbirth - его переносит только сама игра", "a Rebirth/Afterbirth save - only the game itself carries it over"));
                throw new IOException(UserMods.t("не сохранение Isaac", "not an Isaac save"));
            }
            d = un;
        }
        if (d.length > SAVE_CAP) d = java.util.Arrays.copyOf(d, SAVE_CAP);   /* the game's buffer takes no more */
        Save s = new Save();
        boolean layout = layoutClean(d);
        if (layout) {
            s.online = (u32(d, 0x14 + 8) & 0xffffffffL) == ONLINE_ACHIEVEMENTS;
            d = layoutMend(d);
        }
        if (d.length < 0x14) throw new IOException(UserMods.t("файл обрезан", "the file is cut short"));
        if (u32(d, d.length - 4) != checksum(d, HEADER_OFFSET, d.length - HEADER_OFFSET - 4))
            throw new IOException(UserMods.t("контрольная сумма не сходится - игра такой сейв обнулила бы",
                    "the checksum does not match - the game would reset this save"));
        Reader r = new Reader(d, d.length);
        r.run();
        for (int t = 0; t < 10; t++) {
            s.chunk[t] = java.util.Arrays.copyOfRange(r.blk, AT[t], AT[t] + COUNT[t] * ELEM[t]);
            s.count[t] = COUNT[t];
        }
        s.bestiary.putAll(r.maps);
        /*
         * The save counter as the reader left it - except with an empty bestiary, which is
         * written with size 0 (the game's own writer does so) and makes the reader take the
         * list count, 4, for the counter: then the word written here is never read, and the
         * writer's own is kept, so a fresh save of the game comes out byte for byte.
         */
        boolean noPairs = true;
        for (TreeMap<Integer, Integer> l : r.maps.values()) noPairs &= l.isEmpty();
        s.counter = layout && noPairs ? u32(d, d.length - 8) : u32(r.blk, BLOCK_COUNTER);
        if (r.pastEnd > HANG_TURNS)
            s.note = "the bestiary would hold the game " + r.pastEnd + " turns past the end of the file"
                    + " - taken as the game reads it up to there";
        else if (!layout)
            s.note = "read by the game's own rules, not the writer's layout ("
                    + (r.oddType != 0 ? "chunk " + r.oddType + " count 0x" + String.format(java.util.Locale.ROOT, "%08x", r.oddCount)
                    + ", " + r.oddRead + " read; " : "")
                    + "the chunks end at " + r.stop + " of " + d.length + " bytes)";
        return s;
    }

    /* the counters that hold a character's completion marks (the editor's getChecklistUnlocks) */
    private static java.util.Set<Integer> markIndices() {
        java.util.Set<Integer> out = new java.util.HashSet<>();
        for (int c = 0; c < 34; c++) {
            if (c == 14) {
                int e = 812;
                for (int t = 0; t < 12; t++) {
                    out.add((e + 4 * t) / 4);
                    if (t == 8) e += 4;
                    if (t == 9) e += 892;
                    if (t == 10) e += 132;
                }
            } else if (c > 14) {
                int b = 796;
                for (int m = 0; m < 12; m++) {
                    out.add((b + 4 * c + 76 * m) / 4);
                    if (m == 8) b += 76;
                    if (m == 9 || m == 10) b += 60;
                }
            } else {
                int b = 108;
                for (int m = 0; m < 12; m++) {
                    out.add((b + 4 * c + 56 * m) / 4);
                    if (m == 5) b += 20;
                    if (m == 8) b += 60;
                    if (m == 9) b += 944;
                    if (m == 10) b += 80;
                }
            }
        }
        return out;
    }

    private static void put(ByteArrayOutputStream o, int v) {
        for (int k = 0; k < 4; k++) o.write((v >>> (8 * k)) & 0xff);
    }

    /* the Repentance layout, plain, checksummed */
    private static byte[] build(Save in) {
        boolean online = in.online;
        java.util.Set<Integer> marks = markIndices();
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        o.write(HEADER, 0, 16);
        put(o, 0);                                  /* the link to a run save: none */
        for (int t = 1; t <= 10; t++) {
            int n = COUNT[t - 1], e = ELEM[t - 1];
            byte[] data = new byte[n * e];
            byte[] src = in.chunk[t - 1];
            int have = Math.min(in.count[t - 1], n);
            /*
             * Repentance+ online: only what the editor carries over - the
             * achievements, the collectibles, the challenges, the stats and the
             * marks. Its other chunks are laid out otherwise and stay at zero.
             */
            boolean take = !online || t == 1 || t == 2 || t == 4 || t == 7;
            if (take && src != null) System.arraycopy(src, 0, data, 0, have * e);
            if (e == 1) for (int k = 0; k < data.length; k++) data[k] = (byte) (data[k] > 0 ? 1 : 0);
            if (t == 2) {
                for (int k = 0; k < n; k++) {
                    int v = (data[4 * k] & 0xff) | (data[4 * k + 1] & 0xff) << 8
                          | (data[4 * k + 2] & 0xff) << 16 | (data[4 * k + 3] & 0xff) << 24;
                    if (marks.contains(k)) v = (v & 3) == 3 ? 2 : (v & 3);   /* the online bits dropped */
                    else if (online && k > 26) v = 0;                        /* only the stats agree there */
                    for (int b = 0; b < 4; b++) data[4 * k + b] = (byte) (v >>> (8 * b));
                }
            }
            put(o, t);
            put(o, t == 1 ? n : n * 4);
            put(o, n);
            o.write(data, 0, data.length);
        }
        int total = 0;
        for (TreeMap<Integer, Integer> l : in.bestiary.values()) total += l.size();
        put(o, 11);
        put(o, 4 * total);
        /*
         * The four lists always, empty ones too: that is how the game writes a
         * save with an empty bestiary (11, 0, then 4 and four empty lists - a
         * fresh rep_persistentgamedata, measured 02.10.2026). Without them the
         * port's own check (saves.c) found the layout unreadable and the next
         * start put the backup over the import.
         */
        put(o, 4);
        for (int id : new int[] {4, 2, 3, 1}) {
            TreeMap<Integer, Integer> l = in.bestiary.containsKey(id) ? in.bestiary.get(id) : new TreeMap<>();
            put(o, id);
            put(o, 4 * l.size());
            for (java.util.Map.Entry<Integer, Integer> en : l.entrySet()) {
                put(o, en.getKey());
                put(o, en.getValue());
            }
        }
        put(o, in.counter);
        put(o, 0);                                  /* the checksum, below */
        byte[] d = o.toByteArray();
        int c = checksum(d, HEADER_OFFSET, d.length - HEADER_OFFSET - 4);
        for (int k = 0; k < 4; k++) d[d.length - 4 + k] = (byte) (c >>> (8 * k));
        return d;
    }

    /* any save the port can take, as the port's game writes it */
    static byte[] toPort(byte[] d) throws IOException {
        Save s = parse(d);
        if (s.note != null) UserMods.report("import", s.note);
        return build(s);
    }

    /* the port's save, as the PC's game takes it: the same layout, plain */
    static byte[] toPc(byte[] d) throws IOException { return build(parse(d)); }

    /* where the game keeps them under the port's saves folder, and their names */
    static File saveFolder(File savesRoot) {
        if (savesRoot == null) return null;
        File rep = new File(savesRoot, "Repentance");
        rep.mkdirs();
        return rep;
    }

    static String fileName(int slot) { return "rep_persistentgamedata" + slot + ".dat"; }

    static String runName(int slot) { return "rep_gamestate" + slot + ".dat"; }

    /* the PC's own name in its local folder (Documents\My Games\Binding of Isaac Repentance) */
    static String pcFileName(int slot) { return "persistentgamedata" + slot + ".dat"; }

    /*
     * A save in a few words, for the import's slot chooser (P7): how many of
     * Repentance's 637 achievements it holds. Achievement 0 does not exist; a
     * Repentance+ online save's extra ones are past the end and not counted.
     */
    static final int ACHIEVEMENTS = COUNT[0] - 1;

    static int achievements(byte[] d) throws IOException {
        Save s = parse(d);
        byte[] a = s.chunk[0];
        int got = 0;
        for (int i = 1; a != null && i < a.length && i <= ACHIEVEMENTS; i++) if (a[i] > 0) got++;
        return got;
    }

    /* the slot a picked file names: persistentgamedataN, whatever comes before or after it */
    static int slotOf(String name) {
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("persistentgamedata([1-3])")
                .matcher(name == null ? "" : name.toLowerCase());
        return m.find() ? m.group(1).charAt(0) - '0' : 0;
    }
}
