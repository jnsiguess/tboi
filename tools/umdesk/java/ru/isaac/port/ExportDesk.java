package ru.isaac.port;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/*
 * DESK RUN OF THE EXPORT OF THE GAME'S FILES (plan 30.09, M12): GameFiles.java and FileKinds.java as they are,
 * over a pack made from work/app (vanilla since M8) and the real assets folder (port_patch/, the port's assets),
 * with the port's registry played by the bring_in pairs the C code makes (passed in a file).
 *
 *   ExportDesk <pak> <assets dir> <registry file> <work dir>
 *
 * Checks: the kinds (shaders a kind of their own, off by default); every file once; the manifest last, a line for
 * every file; the port's "over" sheets go out as the port's versions, the patched .anm2 as the game's, the port's
 * own files at the game's paths and its assets at their canonical paths; stored/deflated by kind; shaders left
 * out when not ticked; the self-check over a packuse.txt with every class of line; no registry - a note.
 */
public final class ExportDesk {
    /* the game's own files, under the project root run_export.sh passes */
    static final String GAME_DATA = System.getProperty("umdesk.root", "../..") + "/work/app/repentance-resources/data/";
    static int failures;

    static void check(boolean ok, String what) {
        System.out.println((ok ? "  ok    " : "  FAIL  ") + what);
        if (!ok) failures++;
    }

    static final class DeskStore extends GameFiles.Store {
        final File pakFile, assets, files;
        final String[] registry;
        java.io.RandomAccessFile raf;

        DeskStore(File pak, File assets, String[] registry, File files) {
            this.pakFile = pak; this.assets = assets; this.registry = registry; this.files = files;
        }
        @Override java.nio.channels.FileChannel pak() throws IOException {
            if (raf == null) raf = new java.io.RandomAccessFile(pakFile, "r");
            return raf.getChannel();
        }
        @Override long pakBase() { return 0; }
        @Override InputStream open(String asset) throws IOException { return new java.io.FileInputStream(new File(assets, asset)); }
        @Override long length(String asset) {
            File f = new File(assets, asset);
            /* as on the phone: an asset aapt compresses (anything but png/ogg...) has no length to ask */
            return f.isFile() && asset.endsWith(".png") ? f.length() : -1;
        }
        @Override String[] portFiles() { return registry; }
        @Override File filesDir() { return files; }
        @Override String version() { return "desk"; }
        @Override public void close() throws IOException { if (raf != null) raf.close(); raf = null; }
    }

    static byte[] bytes(File f) throws IOException { return java.nio.file.Files.readAllBytes(f.toPath()); }

    static byte[] entry(ZipFile z, String name) throws IOException {
        ZipEntry e = z.getEntry(name);
        if (e == null) return null;
        try (InputStream in = z.getInputStream(e)) { return in.readAllBytes(); }
    }

    public static void main(String[] a) throws Exception {
        File pak = new File(a[0]), assets = new File(a[1]), regFile = new File(a[2]), work = new File(a[3]);
        work.mkdirs();
        List<String> reg = new ArrayList<>();
        for (String line : java.nio.file.Files.readAllLines(regFile.toPath(), java.nio.charset.StandardCharsets.UTF_8)) {
            String[] f = line.split("\t");
            if (f.length == 2) { reg.add(f[0]); reg.add(f[1]); }
        }
        File files = new File(work, "files");
        files.mkdirs();
        new File(files, "packuse.txt").delete();
        DeskStore s = new DeskStore(pak, assets, reg.toArray(new String[0]), files);

        System.out.println("\n1. the kinds");
        check(FileKinds.count() == 6 && FileKinds.of("x/hq4x.fs") == 5 && FileKinds.of("a.VS") == 5,
              "six kinds, shaders (fs, vs) the sixth");
        boolean othersOn = true;
        for (int k = 0; k < 5; k++) othersOn &= FileKinds.on(k);
        check(!FileKinds.on(5) && othersOn, "shaders off by default, the rest on");
        check(FileKinds.stored("a.png") && FileKinds.stored("m.ogg") && FileKinds.stored("v.ogv") && !FileKinds.stored("a.anm2")
              && !FileKinds.stored("s.wav") && !FileKinds.stored("x.srt"), "stored: png, ogg, ogv; the rest deflated");
        check(GameFiles.KIND_NAMES.length == FileKinds.count(), "the dialog's lines from the one table");

        System.out.println("\n2. the dialog's numbers");
        GameFiles.Sizes sizes = GameFiles.scan(s);
        for (int k = 0; k < FileKinds.count(); k++)
            System.out.println("        " + FileKinds.ALL[k].en + ": " + sizes.files[k] + " files, " + sizes.bytes[k] + " bytes");
        check(sizes.files[0] > 0 && sizes.files[5] > 0, "graphics and shaders counted");

        System.out.println("\n3. everything ticked");
        boolean[] all = new boolean[FileKinds.count()];
        java.util.Arrays.fill(all, true);
        File zipAll = new File(work, "all.zip");
        int n;
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(zipAll)) { n = GameFiles.dump(s, out, all, null); }
        GameFiles.Plan plan = GameFiles.plan(s);
        try (ZipFile z = new ZipFile(zipAll)) {
            List<String> names = new ArrayList<>();
            Map<String, ZipEntry> by = new HashMap<>();
            for (Enumeration<? extends ZipEntry> e = z.entries(); e.hasMoreElements(); ) {
                ZipEntry x = e.nextElement();
                names.add(x.getName());
                by.put(x.getName().toLowerCase(), x);
            }
            check(names.size() == n + 1 && by.size() == names.size(), "every file once, and the manifest: " + n + " + 1");
            check(names.get(names.size() - 1).equals(GameFiles.MANIFEST), "the manifest last");
            String man = new String(entry(z, GameFiles.MANIFEST), "UTF-8");
            int lines = 0;
            for (String l : man.split("\n")) if (!l.startsWith("#") && !l.isEmpty()) lines++;
            check(lines == n, "a manifest line for every file: " + lines);
            String M = "gfx/ui/main menu/";
            File pp = new File(assets, "port_patch");
            check(java.util.Arrays.equals(entry(z, "resources/" + M + "gamemenu.png"),
                      bytes(new File(pp, "repentance-resources/data/gfx/ui/main_menu/gamemenu.png"))),
                  "gamemenu.png: the port's version (what the game shows)");
            check(java.util.Arrays.equals(entry(z, "resources-dlc3.ru/" + M + "gamemenu.png"),
                      bytes(new File(pp, "repentance-resources/dlc3.ru/gfx/ui/main_menu/gamemenu.png"))),
                  "resources-dlc3.ru gamemenu.png: the port's version");
            check(java.util.Arrays.equals(entry(z, "resources/" + M + "gamemenu.anm2"),
                      bytes(new File(GAME_DATA + M + "gamemenu.anm2"))),
                  "gamemenu.anm2: the game's (the port's patch is laid over a mod's copy at start)");
            check(entry(z, "resources/" + M + "port_strap_credits.png") != null && entry(z, "resources-dlc3.ru/" + M + "port_strap_credits.png") != null,
                  "the port's own files at the game's paths: port_strap_credits.png in data and ru");
            check(java.util.Arrays.equals(entry(z, "resources-dlc3.de/" + M + "titlemenu_ios.png"),
                      bytes(new File(pp, "repentance-resources/dlc3.de/gfx/ui/main_menu/titlemenu_ios.png"))),
                  "resources-dlc3.de titlemenu_ios.png: the port's language copy");
            check(java.util.Arrays.equals(entry(z, "resources/" + M + "titlemenu_ios.png"),
                      bytes(new File(GAME_DATA + M + "titlemenu_ios.png"))),
                  "resources titlemenu_ios.png: the game's");
            check(entry(z, "resources/gfx/port_wavebar.png") != null && entry(z, "resources/font/eid_default.fnt") != null,
                  "the port's assets at their canonical paths (port_wavebar.png, eid_default.fnt)");
            check(java.util.Arrays.equals(entry(z, "resources/shaders/hq4x.fs"),
                      bytes(new File(pp, "repentance-resources/data/shaders/hq4x.fs"))), "shaders: hq4x.fs the port's version");
            check(by.get(("resources/" + M + "gamemenu.png").toLowerCase()).getMethod() == ZipEntry.STORED
                  && by.get(("resources/" + M + "gamemenu.anm2").toLowerCase()).getMethod() == ZipEntry.DEFLATED, "png stored, anm2 deflated");
            check(man.contains("resources/" + M + "gamemenu.png\tGraphics (png, anm2)\t") && man.contains("port-version repentance-resources/data/" + M + "gamemenu.png"),
                  "the manifest names where gamemenu.png came from");
            check(man.contains("port-asset port_wavebar.png"), "and the port's assets");
            System.out.println("        manifest head:\n" + man.substring(0, Math.min(man.length(), 1400)).replace("\n", "\n        | "));
        }

        System.out.println("\n4. shaders not ticked (the default)");
        boolean[] def = new boolean[FileKinds.count()];
        for (int k = 0; k < def.length; k++) def[k] = FileKinds.on(k);
        File zipDef = new File(work, "default.zip");
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(zipDef)) { GameFiles.dump(s, out, def, null); }
        try (ZipFile z = new ZipFile(zipDef)) {
            boolean shaders = false;
            for (Enumeration<? extends ZipEntry> e = z.entries(); e.hasMoreElements(); ) {
                String nm = e.nextElement().getName();
                if (nm.endsWith(".fs") || nm.endsWith(".vs")) shaders = true;
            }
            check(!shaders, "no shader in the zip");
        }

        System.out.println("\n5. the self-check over files/packuse.txt");
        java.nio.file.Files.write(new File(files, "packuse.txt").toPath(), String.join("\n",
            "# test",
            "rebirth-resources/data/gfx/ui/touch/ios_buttons.png",               // an older edition's, exported
            "rebirth-resources/data/gfx/ui/touch/ios_buttons_120.anm2",          // built at start
            "rebirth-resources/data.jp/gfx/ui/touch/ios_buttons.png",            // no PC folder
            "resources/data/gfx/port_wavebar.png",                               // the port's asset, one of its shapes
            "font/eid_default.fnt",                                              // and another
            "repentance-resources/dlc3.ru/gfx/ui/main menu/port_strap_credits.png", // the port's own at a game path
            "kage-resources/shaders/x.fs",                                       // the engine's own
            "gfx/never_brought.png"                                              // a port path not in the zip
        ).getBytes("UTF-8"));
        boolean[] gfxOnly = new boolean[FileKinds.count()];
        gfxOnly[0] = true;
        List<GameFiles.Item> wanted = new ArrayList<>();
        for (GameFiles.Item it : plan.items) if (gfxOnly[it.kind]) wanted.add(it);
        List<String> lines = GameFiles.selfCheckLines(s, gfxOnly, wanted, wanted.size(), plan);
        for (String l : lines) System.out.println("        " + l);
        String text = String.join("\n", lines);
        check(text.contains("read by the game but not exported: 3") && text.contains("data.jp") && text.contains("kage-resources")
              && text.contains("gfx/never_brought.png (not in the zip)"), "three missing, each with its reason");
        check(text.contains("of kinds not ticked: 1") && text.contains("1 built at start") && text.contains("3 of the port's own assets"),
              "an unticked kind, the stick sizes built at start, the port's own counted");

        System.out.println("\n6. the registry does not answer");
        DeskStore none = new DeskStore(pak, assets, null, files);
        GameFiles.Plan p2 = GameFiles.plan(none);
        check(p2.portAssets == -1 && !p2.notes.isEmpty() && p2.notes.get(p2.notes.size() - 1).contains("registry did not answer"),
              "a note, and no port asset: " + p2.notes);
        s.close();
        none.close();
        System.out.println("\n" + failures + " failures");
        System.exit(failures == 0 ? 0 : 1);
    }
}
