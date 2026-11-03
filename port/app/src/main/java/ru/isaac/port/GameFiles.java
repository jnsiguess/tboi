package ru.isaac.port;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/*
 * THE GAME'S OWN FILES, OUT (the owner, 25.09.2026): what a player needs to
 * make a mod - the game's resources, zipped in the PC mod's layout, so a
 * file taken from here, edited and put back at the same path in a mod
 * replaces the game's.
 *
 *   repentance-resources/data/...       -> resources/...
 *   repentance-resources/dlc3.<lang>/...-> resources-dlc3.<lang>/...
 *
 * WHAT GOES IN IS WHAT THE GAME READS (plan 30.09, M12), from one registry:
 *   - the pack (resources.pak, tools/pack.py): the game's files as the IPA
 *     has them since M8, the latest edition's copy of each path;
 *   - the port's additions laid over it at start (assets/port_patch/, its
 *     index.txt written by tools/port_patch.py, laid by portpatch.c): the
 *     port's own files at the game's paths ("file" - the straps' sheets, the
 *     language copies of the title's iOS sheet), the port's versions of a few
 *     game files ("over" - what the game shows; a mod's copy still wins), the
 *     .anm2 the port patches by name ("patch" - the GAME's file goes out: the
 *     port lays its additions over a mod's copy too), the port's versions the
 *     pack itself still carries ("inpak");
 *   - the port's own assets, entered into the game's table by bring_in
 *     (eid.c) - its fonts, the built-in mods' pictures, the Tweaks and
 *     Spawner pages, the credits - each at its one canonical path, asked of
 *     the running port (pack.c portFiles): nothing hand-listed that can go
 *     stale. A player's mod replaces them by that path (usermods.c).
 * The zip carries isaac-export.txt, the manifest: each file, its kind, its
 * size and where it came from.
 *
 * THE GAME'S WHOLE TREE, NOT REPENTANCE'S SET ALONE (the owner, 29.09.2026):
 * the Repentance engine mounts all four editions, one over the other
 * (0x886434..0x886468), and reads a file from the latest that has it - so
 * the older editions' files the game reads go out too, under resources/.
 *
 * The kinds the player picks are FileKinds' (one table: names, extensions,
 * ticked or not, stored or deflated).
 */
final class GameFiles {
    private GameFiles() { }

    static final String PAK = "resources.pak";
    static final String EDITION = "repentance-resources/";
    static final String INDEX = "port_patch/index.txt";
    static final String MANIFEST = "isaac-export.txt";

    /* the dialog's lines (MainActivity) */
    static final String[] KIND_NAMES = FileKinds.names();

    static final class Sizes {
        long[] bytes = new long[FileKinds.count()];
        int[] files = new int[FileKinds.count()];
    }

    interface Progress { void at(int done, int total, long bytes); }

    static int kindOf(String path) { return FileKinds.of(path); }

    /* the port's own assets brought into the game's table this run: {asset, path, ...} (pack.c) */
    static native String[] portFiles();

    /* ------------------------------------------------ where the export reads from */

    /* the APK on the phone, folders on the desk (tools/umdesk/java ExportDesk) */
    abstract static class Store implements java.io.Closeable {
        abstract java.nio.channels.FileChannel pak() throws IOException;
        abstract long pakBase();
        abstract InputStream open(String asset) throws IOException;
        /* the asset's length, or -1 when it cannot be had without reading it */
        abstract long length(String asset);
        /* the registry of the port's own assets, or null when the port's layer cannot answer */
        abstract String[] portFiles();
        abstract File filesDir();
        String version() { return "?"; }
    }

    private static final class ApkStore extends Store {
        final Context ctx;
        AssetFileDescriptor fd;
        java.io.FileInputStream stream;
        java.nio.channels.FileChannel channel;

        ApkStore(Context ctx) { this.ctx = ctx; }

        @Override java.nio.channels.FileChannel pak() throws IOException {
            if (channel == null) {
                fd = ctx.getAssets().openFd(PAK);
                stream = new java.io.FileInputStream(fd.getFileDescriptor());
                channel = stream.getChannel();
            }
            return channel;
        }
        @Override long pakBase() { return fd != null ? fd.getStartOffset() : 0; }
        @Override InputStream open(String asset) throws IOException { return ctx.getAssets().open(asset); }
        @Override long length(String asset) {
            try (AssetFileDescriptor f = ctx.getAssets().openFd(asset)) { return f.getLength(); }
            catch (IOException e) { return -1; }        /* a compressed asset: its size is not asked */
        }
        @Override String[] portFiles() {
            try { return GameFiles.portFiles(); }
            catch (UnsatisfiedLinkError e) { return null; }
        }
        @Override File filesDir() { return ctx.getFilesDir(); }
        @Override String version() {
            try { return ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0).versionName; }
            catch (Exception e) { return "?"; }
        }
        @Override public void close() throws IOException {
            if (stream != null) stream.close();
            if (fd != null) fd.close();
            channel = null; stream = null; fd = null;
        }
    }

    /* ------------------------------------------------ the pack (ISAACPK2: tools/pack.py, pack.c) */

    /*
     * A file the DLC sets repeat byte for byte is stored once, and every
     * repeat is a -1 length followed by the offset and length of the bytes
     * already written. The table is walked first - names, and where each
     * file's bytes are - and the bytes are read by their offset.
     */
    static final class Entry {
        final String path;
        final long at, length;           /* the bytes, from the start of the pack */
        Entry(String path, long at, long length) { this.path = path; this.at = at; this.length = length; }
    }

    private static void read(Store s, long at, byte[] into, int n) throws IOException {
        java.nio.ByteBuffer b = java.nio.ByteBuffer.wrap(into, 0, n);
        while (b.hasRemaining()) {
            if (s.pak().read(b, s.pakBase() + at + b.position()) < 0) throw new IOException("the pack ends early");
        }
    }

    private static int int32(byte[] b) {
        return ((b[0] & 0xff) << 24) | ((b[1] & 0xff) << 16) | ((b[2] & 0xff) << 8) | (b[3] & 0xff);
    }

    private static long int64(byte[] b, int o) {
        long v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | (b[o + i] & 0xff);
        return v;
    }

    /* the pack's table: every file of it, and where its bytes are */
    static List<Entry> table(Store s) throws IOException {
        List<Entry> out = new ArrayList<>();
        byte[] head = new byte[24];
        s.pak();
        read(s, 0, head, 12);
        String tag = new String(head, 0, 8, "US-ASCII");
        boolean second = tag.equals("ISAACPK2");
        if (!second && !tag.equals("ISAACPK1")) throw new IOException("not the pack format");
        int count = int32(new byte[] {head[8], head[9], head[10], head[11]});
        long p = 12;
        byte[] name = new byte[4096];
        for (int i = 0; i < count; i++) {
            read(s, p, head, 4);
            int n = int32(head);
            if (n <= 0 || n > name.length) throw new IOException("a damaged entry in the pack");
            read(s, p + 4, name, n);
            p += 4 + n;
            read(s, p, head, 8);
            long size = int64(head, 0);
            p += 8;
            String path = new String(name, 0, n, "UTF-8");
            if (second && size == -1) {
                read(s, p, head, 16);
                out.add(new Entry(path, int64(head, 0), int64(head, 8)));
                p += 16;
                continue;
            }
            if (size < 0) throw new IOException("a damaged entry in the pack");
            out.add(new Entry(path, p, size));
            p += size;
        }
        return out;
    }

    /* ------------------------------------------------ paths */

    private static final String[] EDITIONS = {
        "rebirth-resources/", "afterbirth-resources/", "afterbirthplus-resources/", EDITION
    };

    /* the edition a pack path belongs to, 0..3 in mounting order, or -1 */
    static int editionOf(String path) {
        for (int i = 0; i < EDITIONS.length; i++) if (path.startsWith(EDITIONS[i])) return i;
        return -1;
    }

    /* where a file of the pack goes in the zip, or null when the game does not read it */
    static String pcPath(String path) {
        int ed = editionOf(path);
        if (ed < 0) return null;
        String rest = path.substring(EDITIONS[ed].length());
        int slash = rest.indexOf('/');
        if (slash < 0) return null;
        String set = rest.substring(0, slash), inner = rest.substring(slash + 1);
        if (set.equals("data")) return "resources/" + inner;
        if (ed == EDITIONS.length - 1 && set.startsWith("dlc3.")) return "resources-dlc3" + set.substring(4) + "/" + inner;
        return null;
    }

    /* a path the port entered itself (bring_in: "gfx/x", "data/gfx/x", "resources/data/gfx/x"): its zip path */
    static String portPcPath(String path) {
        String p = path;
        if (p.startsWith("resources/")) p = p.substring(10);
        if (p.startsWith("data/")) p = p.substring(5);
        return p.isEmpty() ? null : "resources/" + p;
    }

    static String low(String s) { return s.toLowerCase(Locale.ROOT); }

    /* ------------------------------------------------ the registry */

    /* one file of the zip: where it goes, its kind, where its bytes are, and where it came from */
    static final class Item {
        final String zip;
        final int kind;
        final Entry entry;              /* from the pack, or */
        final String asset;             /* from the assets */
        long length;
        final String origin;
        Item(String zip, int kind, Entry entry, String asset, long length, String origin) {
            this.zip = zip; this.kind = kind; this.entry = entry; this.asset = asset; this.length = length; this.origin = origin;
        }
    }

    static final class Plan {
        final List<Item> items = new ArrayList<>();
        final List<String> notes = new ArrayList<>();
        int portAssets = -1;            /* -1: the port's registry did not answer */
    }

    /* port_patch/index.txt: kind -> {target (lower case) -> asset} */
    static Map<String, Map<String, String>> overlay(Store s, Plan plan) {
        Map<String, Map<String, String>> o = new HashMap<>();
        for (String k : new String[] {"file", "over", "patch", "pixels", "inpak", "variants"}) o.put(k, new HashMap<>());
        try (InputStream in = s.open(INDEX);
             java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(in, "UTF-8"))) {
            for (String line; (line = r.readLine()) != null; ) {
                if (line.isEmpty() || line.startsWith("#")) continue;
                String[] f = line.split("\t");
                if (f.length < 2 || !o.containsKey(f[0])) continue;
                o.get(f[0]).put(low(f[1]), f.length > 2 ? f[2] : "");
            }
        } catch (IOException e) {
            plan.notes.add("port_patch/index.txt could not be read (" + UserMods.reasonOf(e) + "): the port's additions are not in the export");
        }
        return o;
    }

    /* everything the export could carry, every kind */
    static Plan plan(Store s) throws IOException {
        Plan plan = new Plan();
        Map<String, Map<String, String>> o = overlay(s, plan);
        /* the pack: per zip path, the latest edition's copy */
        Map<String, Entry> best = new HashMap<>();
        Map<String, Integer> rank = new HashMap<>();
        for (Entry e : table(s)) {
            String pc = pcPath(e.path);
            if (pc == null) continue;
            String key = low(pc);
            int ed = editionOf(e.path);
            Integer was = rank.get(key);
            if (was == null || ed > was) { best.put(key, e); rank.put(key, ed); }
        }
        Set<String> taken = new HashSet<>();
        for (Entry e : best.values()) {
            int k = FileKinds.of(e.path);
            if (k < 0) continue;
            String lp = low(e.path), pc = pcPath(e.path);
            String over = o.get("over").get(lp);
            Item it;
            if (over != null) {
                long n = s.length(over);
                it = new Item(pc, k, null, over, n, "port-version " + e.path + (o.get("pixels").containsKey(lp)
                        ? " (the port's version of the game's file; its retouch is laid over a mod's copy of the game's size too)"
                        : " (the port's version of the game's file; a mod's copy replaces it)"));
            } else if (o.get("patch").containsKey(lp)) {
                it = new Item(pc, k, e, null, e.length, "game " + e.path
                        + " (the port lays its additions over this file by name at start - over a mod's copy too)");
            } else if (o.get("inpak").containsKey(lp)) {
                it = new Item(pc, k, e, null, e.length, "port-version " + e.path + " (in the pack)");
            } else {
                it = new Item(pc, k, e, null, e.length, "game " + e.path);
            }
            plan.items.add(it);
            taken.add(low(pc));
        }
        /* the port's own files at the game's paths, and "over" files the pack has no copy of */
        for (String kind : new String[] {"file", "over"}) {
            for (Map.Entry<String, String> f : o.get(kind).entrySet()) {
                String pc = pcPath(f.getKey());
                if (pc == null || taken.contains(low(pc))) continue;
                int ed = editionOf(f.getKey());
                Integer was = rank.get(low(pc));
                if (was != null && was > ed) continue;                      /* a later edition's copy is what is read */
                int k = FileKinds.of(f.getKey());
                if (k < 0) continue;
                plan.items.add(new Item(pc, k, null, f.getValue(), s.length(f.getValue()),
                        "port " + f.getKey() + " (the port's own file; a mod's copy replaces it)"));
                taken.add(low(pc));
            }
        }
        /* the port's own assets, by bring_in's registry */
        String[] reg = s.portFiles();
        if (reg == null) {
            plan.notes.add("the port's own assets: the registry did not answer (the port's layer is not loaded) - left out");
        } else {
            plan.portAssets = 0;
            for (int i = 0; i + 1 < reg.length; i += 2) {
                String pc = portPcPath(reg[i + 1]);
                int k = FileKinds.of(reg[i + 1]);
                if (pc == null || k < 0) continue;
                if (taken.contains(low(pc))) {
                    plan.notes.add(pc + ": the port's asset " + reg[i] + " has the path of a game file - the game's goes out");
                    continue;
                }
                plan.items.add(new Item(pc, k, null, reg[i], s.length(reg[i]), "port-asset " + reg[i]
                        + " (the port's own; a mod's resources/" + reg[i + 1] + " replaces it)"));
                taken.add(low(pc));
                plan.portAssets++;
            }
        }
        return plan;
    }

    /* an asset's bytes */
    private static byte[] readAsset(Store s, String asset) throws IOException {
        try (InputStream in = s.open(asset); java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream()) {
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) bytes.write(buf, 0, n);
            return bytes.toByteArray();
        }
    }

    private static long lengthOf(Store s, Item it) throws IOException {
        if (it.length >= 0) return it.length;
        it.length = readAsset(s, it.asset).length;            /* a compressed asset: counted by reading it */
        return it.length;
    }

    /* ------------------------------------------------ the dialog's numbers and the zip */

    static Sizes scan(Context ctx) throws IOException {
        try (Store s = new ApkStore(ctx)) { return scan(s); }
    }

    static Sizes scan(Store s) throws IOException {
        Sizes out = new Sizes();
        for (Item it : plan(s).items) {
            out.bytes[it.kind] += lengthOf(s, it);
            out.files[it.kind]++;
        }
        return out;
    }

    static int dump(Context ctx, Uri target, boolean[] kinds, Progress progress) throws IOException {
        try (Store s = new ApkStore(ctx);
             OutputStream raw = ctx.getContentResolver().openOutputStream(target)) {
            if (raw == null) throw new IOException("could not open the file for writing");
            return dump(s, raw, kinds, progress);
        }
    }

    /*
     * The chosen kinds into a zip. Pictures, music and video are compressed
     * already and go in stored; the rest is deflated at the fastest level.
     * The manifest goes last.
     */
    static int dump(Store s, OutputStream raw, boolean[] kinds, Progress progress) throws IOException {
        Plan plan = plan(s);
        List<Item> wanted = new ArrayList<>();
        for (Item it : plan.items) if (kinds[it.kind] && it.length <= Integer.MAX_VALUE) wanted.add(it);
        java.util.Collections.sort(wanted, (a, b) -> a.zip.compareTo(b.zip));
        int total = wanted.size(), done = 0;
        long bytes = 0;
        StringBuilder man = new StringBuilder();
        try (ZipOutputStream zip = new ZipOutputStream(new java.io.BufferedOutputStream(raw, 1 << 16))) {
            byte[] data = new byte[1 << 20];
            CRC32 crc = new CRC32();
            for (Item it : wanted) {
                int len;
                if (it.entry != null) {
                    len = (int) it.entry.length;
                    if (len > data.length) data = new byte[len];
                    read(s, it.entry.at, data, len);
                } else {
                    byte[] one = readAsset(s, it.asset);
                    len = one.length;
                    if (len > data.length) data = new byte[len];
                    System.arraycopy(one, 0, data, 0, len);
                }
                ZipEntry e = new ZipEntry(it.zip);
                if (FileKinds.stored(it.zip)) {
                    crc.reset();
                    crc.update(data, 0, len);
                    e.setMethod(ZipEntry.STORED);
                    e.setSize(len);
                    e.setCompressedSize(len);
                    e.setCrc(crc.getValue());
                } else {
                    e.setMethod(ZipEntry.DEFLATED);
                    zip.setLevel(1);
                }
                zip.putNextEntry(e);
                zip.write(data, 0, len);
                zip.closeEntry();
                man.append(it.zip).append('\t').append(FileKinds.ALL[it.kind].en).append('\t').append(len)
                   .append('\t').append(it.origin).append('\n');
                done++;
                bytes += len;
                if (progress != null && (done % 200 == 0 || done == total)) progress.at(done, total, bytes);
            }
            byte[] m = manifest(s, kinds, plan, done, man).getBytes("UTF-8");
            ZipEntry e = new ZipEntry(MANIFEST);
            e.setMethod(ZipEntry.DEFLATED);
            zip.setLevel(1);
            zip.putNextEntry(e);
            zip.write(m);
            zip.closeEntry();
            if (progress != null) progress.at(done, done, bytes);
        }
        if (UserMods.reporting()) selfCheck(s, kinds, wanted, done, plan);
        return done;
    }

    /* isaac-export.txt: what went out and where each file came from */
    static String manifest(Store s, boolean[] kinds, Plan plan, int done, CharSequence lines) {
        StringBuilder b = new StringBuilder();
        b.append("# The Binding of Isaac: Repentance (Android port ").append(s.version()).append(") - the game's files, exported\n");
        b.append("# ").append(done).append(" files. Kinds:");
        for (int k = 0; k < FileKinds.count(); k++)
            b.append(' ').append(FileKinds.ALL[k].en).append(kinds[k] ? " [yes]" : " [no]").append(';');
        b.append('\n');
        b.append("# A file put into a mod at the same path replaces the game's (resources/, resources-dlc3.<language>/).\n");
        b.append("# Each line: the path in the zip, its kind, its size in bytes, where it came from:\n");
        b.append("#   game <pack path>          the game's own file (the latest edition that has it)\n");
        b.append("#   port-version <pack path>  the port's version of a game file - what the port shows\n");
        b.append("#   port <pack path>          a file of the port's own at the game's paths\n");
        b.append("#   port-asset <asset>        a file of the port's own (fonts, built-in mods, its pages)\n");
        b.append("# Not files: the stick size variants (ios_buttons_<size>.anm2) are built at start from ios_buttons.anm2.\n");
        for (String n : plan.notes) b.append("# note: ").append(n).append('\n');
        b.append(lines);
        return b.toString();
    }

    /*
     * THE EXPORT CHECKS ITSELF (plan 30.09, M12 p.5), in the report build: is
     * everything the game reads in the zip? The report build keeps an account
     * of every file the game read outside Repentance's own set, and every file
     * of the port's it read (files/packuse.txt, pack.c, every run together);
     * each is looked up here by the path the zip gives it. What the game read
     * and the zip does not carry is named, and why: a kind the player did not
     * tick, a place the PC layout has no folder for (an older edition's
     * data.jp, the engine's own kage-resources), or a file the port builds at
     * start (the stick sizes).
     */
    static void selfCheck(Store s, boolean[] kinds, List<Item> wanted, int done, Plan plan) {
        for (String line : selfCheckLines(s, kinds, wanted, done, plan)) UserMods.report("export", line);
    }

    /* the self-check's lines (the desk reads them, the report build reports them) */
    static List<String> selfCheckLines(Store s, boolean[] kinds, List<Item> wanted, int done, Plan plan) {
        List<String> out = new ArrayList<>();
        Set<String> inZip = new HashSet<>();
        for (Item it : wanted) inZip.add(low(it.zip));
        File use = new File(s.filesDir(), "packuse.txt");
        String assets = "port assets: " + (plan.portAssets < 0 ? "the registry did not answer" : String.valueOf(plan.portAssets));
        if (!use.isFile()) {
            out.add(done + " files; the account of reads (files/packuse.txt) is not there yet: "
                    + "it is written every 30 s of a report build's run");
            out.add(assets);
            return out;
        }
        int read = 0, unticked = 0, built = 0, ported = 0;
        List<String> missing = new ArrayList<>();
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(use), "UTF-8"))) {
            for (String line; (line = in.readLine()) != null; ) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) continue;
                read++;
                String why = null;
                String pc;
                if (low(line).matches(".*/ios_buttons_\\d+\\.anm2")) { built++; continue; }
                if (editionOf(line) >= 0) pc = pcPath(line);
                else if (line.startsWith("kage-resources/")) pc = null;
                else { pc = portPcPath(line); ported++; }
                int k = FileKinds.of(line);
                if (pc == null) why = line.startsWith("kage-resources/") || editionOf(line) < 0
                        ? "not an edition's (the engine's own set)" : "no folder for it in the PC layout";
                else if (k < 0) why = "a kind the export does not offer";
                else if (!kinds[k]) { unticked++; continue; }
                else if (!inZip.contains(low(pc))) why = "not in the zip";
                if (why != null) missing.add(line + " (" + why + ")");
            }
        } catch (IOException ex) {
            out.add("files/packuse.txt could not be read: " + UserMods.reasonOf(ex));
            return out;
        }
        out.add(done + " files; the game read " + read + " files outside repentance-resources or of the "
                + "port's (all report runs; " + ported + " of the port's own assets, " + built + " built at start); "
                + "read by the game but not exported: " + missing.size() + (unticked > 0 ? "; of kinds not ticked: " + unticked : ""));
        int shown = 0;
        for (String m : missing) {
            if (shown++ >= 60) { out.add("... and " + (missing.size() - 60) + " more"); break; }
            out.add("  not exported: " + m);
        }
        out.add(assets);
        for (String n : plan.notes) out.add("note: " + n);
        return out;
    }
}
