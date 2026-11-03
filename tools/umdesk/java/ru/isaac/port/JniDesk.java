package ru.isaac.port;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/*
 * ONE GAME PROCESS ON THE DESK (hotfix 1.75: "the restart button after adding mods disappeared COMPLETELY"): the real
 * UserMods.java over the real usermods.c (tools/umdesk/jnidesk.c, real threads), in the order the phone makes the
 * calls - the start (setConfigDir), the MODS page asking every frame, ADD MOD's batch on a thread of its own, the add
 * window's RESTART TO APPLY - and, for a game Android ended behind the file picker, the picked archives reaching a
 * new process before its start.
 *
 *   JniDesk <work dir> <case> <mods listed> <mods added> [the start's delay in ms]
 *
 *   case: new        - a new mod each (the owner's road: MODS -> ADD MOD -> pick -> OK)
 *         update-on  - each an update of a listed mod that is on (the question answered "update")
 *         update-off - the same of a mod that is off
 *         beside     - the same mod again, answered "beside it"
 *         again      - the same archives added a second time in this run, "update"
 *         death      - new mods, picked into a process whose start comes `delay` ms after the batch begins
 *
 * One line comes out, for test_restart175.py: what the add window and the page show, and what a new start would load.
 */
public final class JniDesk {
    static native void start(String cfg, String inner);
    static native boolean changed();
    static native int count();
    static native boolean pending(int i);
    static native String id(int i);
    static native boolean on(int i);
    static native String owners();
    static native void diskNow();
    static native int diskBusy();

    static File zipOf(File dir, String file, Object... pairs) throws java.io.IOException {
        dir.mkdirs();
        File f = new File(dir, file);
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(f))) {
            for (int i = 0; i < pairs.length; i += 2) {
                z.putNextEntry(new java.util.zip.ZipEntry((String) pairs[i]));
                z.write(((String) pairs[i + 1]).getBytes("UTF-8"));
                z.closeEntry();
            }
        }
        return f;
    }

    static void put(File f, String text) throws java.io.IOException {
        f.getParentFile().mkdirs();
        java.nio.file.Files.write(f.toPath(), text.getBytes("UTF-8"));
    }

    static String card(String id, String name, String version) {
        return "{\"id\": \"" + id + "\", \"name\": \"" + name + "\", \"version\": \"" + version + "\", \"author\": \"desk\"}";
    }

    static UserMods.Item item(File f) {
        return new UserMods.Item(f.getName(), new UserMods.Source() {
            @Override public java.io.InputStream open() throws java.io.IOException {
                return new java.io.BufferedInputStream(new java.io.FileInputStream(f), 1 << 16);
            }
            @Override public long size() { return f.length(); }
        });
    }

    static void deleteTree(File f) {
        File[] k = f.listFiles();
        if (k != null) for (File c : k) deleteTree(c);
        f.delete();
    }

    /* the page: a frame every 4 ms asks whether RESTART TO APPLY stands (tweaks.c restart_row) */
    static volatile boolean playing = true;
    static volatile boolean lastAsk;
    static volatile int asks;

    public static void main(String[] a) throws Exception {
        System.load(System.getProperty("umdesk.jni"));
        File work = new File(a[0]);
        String kind = a[1];
        int listed = Integer.parseInt(a[2]), added = Integer.parseInt(a[3]);
        long delay = a.length > 4 ? Long.parseLong(a[4]) : 0;
        deleteTree(work);
        File cfg = new File(work, "cfg"), mods = new File(cfg, "mods"), stage = new File(work, "stage");
        mods.mkdirs();
        boolean listedOn = !kind.equals("update-off");
        StringBuilder list = new StringBuilder("# the player's mods, the strongest first: on|off <folder>\n");
        for (int i = 0; i < listed; i++) {
            String folder = String.format("%03d-Mod-%d", i + 1, i);
            put(new File(mods, folder + "/mod.json"), card("m" + i, "Mod " + i, "1"));
            put(new File(mods, folder + "/resources/sfx/m" + i + ".wav"), "RIFF....WAVEfmt m" + i);
            list.append(listedOn || i >= added ? "on " : "off ").append(folder).append('\n');
        }
        put(new File(mods, "mods.txt"), list.toString());
        List<UserMods.Item> items = new ArrayList<>();
        boolean sameIds = kind.startsWith("update") || kind.equals("beside");
        for (int k = 0; k < added; k++) {
            String id = sameIds ? "m" + k : "n" + k;
            String name = sameIds ? "Mod " + k : "New " + k;
            items.add(item(zipOf(stage, id + ".zip", "mod.json", card(id, name, "2"),
                                 "resources/sfx/" + id + "-2.wav", "RIFF....WAVEfmt " + id)));
        }
        UserMods.BatchAsker asker = (q, more) -> kind.equals("beside") ? 0 : UserMods.ANSWER_UPDATE;
        File inner = new File(work, "inner");
        inner.mkdirs();

        Thread page = new Thread(() -> {
            while (playing) {
                if (asks > 0 || !kind.equals("death")) { lastAsk = changed(); asks++; }
                try { Thread.sleep(4); } catch (InterruptedException e) { return; }
            }
        }, "frame");

        List<UserMods.Result> results;
        if (kind.equals("death")) {
            /* a new process: the picker's answer (onActivityResult) before the surface - and setConfigDir - are there */
            final List<UserMods.Result>[] out = new List[1];
            Thread add = new Thread(() -> out[0] = UserMods.installAll(items, cfg, null, asker, null), "add mod");
            add.start();
            Thread.sleep(delay);
            start(cfg.getAbsolutePath(), inner.getAbsolutePath());
            asks = 1;
            page.start();
            add.join();
            results = out[0];
        } else {
            start(cfg.getAbsolutePath(), inner.getAbsolutePath());
            page.start();
            Thread.sleep(50);
            results = UserMods.installAll(items, cfg, null, asker, null);
            if (kind.equals("again")) results = UserMods.installAll(items, cfg, null, asker, null);
        }
        int ok = 0;
        for (UserMods.Result r : results) if (r.ok) ok++;
        /* the add window (MainActivity.addMods): its RESTART TO APPLY */
        boolean button = ok > 0 && UserMods.restartIsOwed();
        boolean pageNow = changed();
        /* the page a moment later: the disk looked at (every 2 s on the phone) */
        diskNow();
        Thread.sleep(300);
        while (diskBusy() != 0) Thread.sleep(10);
        boolean pageLater = changed();
        playing = false;
        page.join();
        int pale = 0;
        StringBuilder ids = new StringBuilder();
        for (int i = 0; i < count(); i++) {
            if (pending(i)) pale++;
            if (on(i)) ids.append(ids.length() > 0 ? "," : "").append(id(i));
        }
        String why = "";
        for (UserMods.Result r : results) if (!r.ok) why += " | refused: " + (r.text == null ? "?" : r.text.replace('\n', ' '));
        System.out.println("RESULT case=" + kind + " listed=" + listed + " added=" + ok + "/" + added
                + " button=" + (button ? 1 : 0) + " row_now=" + (pageNow ? 1 : 0) + " row_later=" + (pageLater ? 1 : 0)
                + " frame_row=" + (lastAsk ? 1 : 0) + " pale=" + pale + " count=" + count()
                + " index=[" + owners() + "] on=[" + ids + "]" + why);
    }
}
