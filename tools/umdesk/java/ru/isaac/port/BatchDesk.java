package ru.isaac.port;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/*
 * DESK RUN OF A BATCH OF MODS (hotfix 1.73: several archives at once, no restart after ADD MOD): the real
 * UserMods.installAll on zips from disk, no Android.
 *
 *   BatchDesk <work dir>        (tools/umdesk/run_batch.sh)
 *
 *   1. the picker's answer (UserMods.picked): one Uri as the data, several as a clip, a clip of one, repeats, nothing;
 *   2. a batch of six: a mod, a broken zip, a PC mod (adapted), a second mod, the first again (the question, answered
 *      "update, the same for the rest"), the PC mod again (updated with no question): every mod has its result, a
 *      failure stops nothing, the updates go into new folders and mark the copies they replace, nothing half-done;
 *   3. a full phone mid-batch: a mod that fits, one that does not (refused before anything is written, with the
 *      numbers), a PC mod that does not (the adapting road's own word), one that fits again;
 *   4. an error nobody foresaw in one mod's archive: that mod ends, the next goes in;
 *   5. the batch's words in all seven languages, and no word of a restart of its own in a mod's text.
 */
public final class BatchDesk {
    static int failures;

    static void check(boolean ok, String what) {
        System.out.println((ok ? "  ok    " : "  FAIL  ") + what);
        if (!ok) failures++;
    }

    static String one(String s) { return s == null ? "-" : s.replace("\n", " | "); }

    static File zipOf(File dir, String file, Object... pairs) throws java.io.IOException {
        dir.mkdirs();
        File f = new File(dir, file);
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(f))) {
            for (int i = 0; i < pairs.length; i += 2) {
                z.putNextEntry(new java.util.zip.ZipEntry((String) pairs[i]));
                Object v = pairs[i + 1];
                z.write(v instanceof byte[] ? (byte[]) v : ((String) v).getBytes("UTF-8"));
                z.closeEntry();
            }
        }
        return f;
    }

    static UserMods.Source file(File f) {
        return new UserMods.Source() {
            @Override public java.io.InputStream open() throws java.io.IOException {
                return new java.io.BufferedInputStream(new java.io.FileInputStream(f), 1 << 16);
            }
            @Override public long size() { return f.length(); }
        };
    }

    static UserMods.Item item(File f) { return new UserMods.Item(f.getName(), file(f)); }

    static boolean anyMark(File dir, String mark) {
        File[] k = dir.listFiles();
        if (k == null) return false;
        for (File f : k) {
            if (f.getName().equals(mark)) return true;
            if (f.isDirectory() && anyMark(f, mark)) return true;
        }
        return false;
    }

    static long du(File f) {
        if (f.isFile()) return f.length();
        long n = 0;
        File[] k = f.listFiles();
        if (k != null) for (File c : k) n += du(c);
        return n;
    }

    static byte[] png() throws java.io.IOException {
        java.awt.image.BufferedImage img = new java.awt.image.BufferedImage(64, 64, java.awt.image.BufferedImage.TYPE_INT_ARGB);
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) img.setRGB(x, y, 0xff000000 | (x * 4) << 16 | (y * 4) << 8 | 77);
        java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
        javax.imageio.ImageIO.write(img, "png", b);
        return b.toByteArray();
    }

    public static void main(String[] a) throws Exception {
        String jni = System.getProperty("umdesk.jni");
        if (jni != null) System.load(jni);
        String refPath = System.getProperty("umdesk.gameref");
        if (refPath != null) UserMods.gameRef = () -> new java.io.FileInputStream(refPath);
        File work = new File(a[0]);
        UserMods.deleteTree(work);

        System.out.println("\n1. the picker's answer (ACTION_OPEN_DOCUMENT, EXTRA_ALLOW_MULTIPLE)");
        check(UserMods.picked("one.zip", 0, i -> null).equals(Arrays.asList("one.zip")), "one file as the intent's data alone");
        String[] clip = {"a.zip", "b.zip", null, "a.zip", "c.zip"};
        check(UserMods.picked("data.zip", clip.length, i -> clip[i]).equals(Arrays.asList("a.zip", "b.zip", "c.zip")),
              "several as a clip: in its order, the null and the repeat left out, the data not added on top");
        check(UserMods.picked("a.zip", 1, i -> "a.zip").equals(Arrays.asList("a.zip")), "a clip of one with the same data: once");
        check(UserMods.picked(null, 0, i -> null).isEmpty(), "nothing picked: nothing");
        check(UserMods.picked(null, 2, i -> new String[] {null, null}[i]).isEmpty(), "a clip of nulls: nothing");

        File zips = new File(work, "zips");
        File green = zipOf(zips, "green.zip", "mod.json", "{\"id\": \"green\", \"name\": \"Green Isaac\", \"version\": \"1\"}",
                "resources/gfx/characters/costumes/character_001_isaac.png", png());
        File red = zipOf(zips, "red.zip", "mod.json", "{\"id\": \"red\", \"name\": \"Red Isaac\", \"version\": \"1\"}",
                "resources/gfx/characters/costumes/character_001_isaac.png", png());
        File broken = new File(zips, "broken.zip");
        java.nio.file.Files.write(broken.toPath(), "this is no zip at all".getBytes("UTF-8"));
        File pc = zipOf(zips, "pc.zip", "metadata.xml", "<metadata><name>PC Thing</name><directory>pcthing</directory><version>2</version></metadata>",
                "resources/gfx/ui/thing.png", png());
        File green2 = zipOf(zips, "green-v2.zip", "mod.json", "{\"id\": \"green\", \"name\": \"Green Isaac\", \"version\": \"2\"}",
                "resources/gfx/characters/costumes/character_001_isaac.png", png());

        System.out.println("\n2. a batch of six: a failure in the middle, the PC mod adapted, two updates by one answer");
        File cfg = new File(work, "cfg");
        cfg.mkdirs();
        File mods = new File(cfg, "mods");
        List<String> asked = new ArrayList<>(), moved = new ArrayList<>();
        List<Boolean> askedMore = new ArrayList<>();
        List<UserMods.Item> items = Arrays.asList(item(green), item(broken), item(pc), item(red), item(green2), item(pc));
        List<UserMods.Result> rs = UserMods.installAll(items, cfg, null, (q, more) -> {
            asked.add(q);
            askedMore.add(more);
            return UserMods.ANSWER_UPDATE | UserMods.ANSWER_ALL;
        }, (k, n, label) -> moved.add(k + "/" + n + " " + label));
        check(moved.equals(Arrays.asList("0/6 green.zip", "1/6 broken.zip", "2/6 pc.zip", "3/6 red.zip", "4/6 green-v2.zip", "5/6 pc.zip")),
              "the window is told of each mod in turn: " + moved);
        check(rs.size() == 6, "a result for every archive: " + rs.size());
        for (UserMods.Result r : rs) System.out.println("        " + UserMods.statusLine(r));
        check(rs.get(0).ok && !rs.get(0).updated, "1 green: added into " + rs.get(0).folder);
        check(!rs.get(1).ok && rs.get(1).text.contains("not a zip"), "2 broken: refused with its reason - " + one(rs.get(1).text));
        boolean adapting = refPath != null;
        check(rs.get(2).ok && rs.get(2).adapted == adapting, "3 the PC mod: taken" + (adapting ? ", adapted" : " (no reference: unadapted)")
              + " into " + rs.get(2).folder);
        check(rs.get(3).ok && !rs.get(3).updated, "4 red: added after the failure into " + rs.get(3).folder);
        check(rs.get(4).ok && rs.get(4).updated && !rs.get(4).folder.equals(rs.get(0).folder),
              "5 green again: asked, updated into a new folder " + rs.get(4).folder);
        check(rs.get(5).ok && rs.get(5).updated && !rs.get(5).folder.equals(rs.get(2).folder),
              "6 the PC mod again: updated into " + rs.get(5).folder + " with no question (the same for the rest)");
        check(asked.size() == 1 && askedMore.equals(Arrays.asList(true)), "one question, while more were to come: " + asked);
        check(new File(mods, rs.get(0).folder + "/.remove").exists() && new File(mods, rs.get(2).folder + "/.remove").exists(),
              "the replaced copies marked .remove, left whole for this run");
        check(!new File(mods, rs.get(4).folder + "/.remove").exists() && !new File(mods, rs.get(3).folder + "/.remove").exists(),
              "the new ones stand unmarked");
        String[] left = mods.list((d, n) -> n.startsWith(".incoming") || n.startsWith(".old-"));
        check(!anyMark(mods, UserMods.ADDING) && left != null && left.length == 0, "nothing half-done: no .adding, no .incoming, no .old-");
        check(rs.get(0).name.equals("Green Isaac") && rs.get(1).name == null && "broken.zip".equals(rs.get(1).label),
              "names for the summary: the mod's, else the archive's");
        boolean noRestart = true;
        for (UserMods.Result r : rs) if (r.text != null && (r.text.contains("restart") || r.text.contains("перезапуст"))) noRestart = false;
        check(noRestart, "no mod's text speaks of a restart of its own");
        check(UserMods.summaryTitle(5, 6).equals("Mods added: 5 of 6"), "the summary's title: " + UserMods.summaryTitle(5, 6));

        System.out.println("\n2a. \"beside it\" without the tick: asked for each");
        asked.clear();
        rs = UserMods.installAll(Arrays.asList(item(red), item(red)), cfg, null, (q, more) -> { asked.add(q + " more=" + more); return 0; }, null);
        check(asked.size() == 2 && asked.get(0).endsWith("more=true") && asked.get(1).endsWith("more=false"),
              "two questions, the tick offered only while more are to come");
        check(rs.get(0).ok && !rs.get(0).updated && rs.get(1).ok && !rs.get(1).updated && !rs.get(0).folder.equals(rs.get(1).folder),
              "both beside, each a folder: " + rs.get(0).folder + ", " + rs.get(1).folder);

        System.out.println("\n3. a full phone mid-batch: the room is said before anything is written");
        File cfg3 = new File(work, "cfg3");
        File mods3 = new File(cfg3, "mods");
        cfg3.mkdirs();
        final long budget = 40L << 20;
        UserMods.freeSpace = f -> Math.max(1, budget - du(mods3));
        File big = zipOf(zips, "big.zip", "mod.json", "{\"id\": \"big\", \"name\": \"Big\"}", "resources/sfx/big.wav", new byte[30 << 20]);
        File pcBig = zipOf(zips, "pcbig.zip", "metadata.xml", "<metadata><name>PC Big</name><directory>pcbig</directory></metadata>",
                "resources/gfx/ui/thing.png", png());
        rs = UserMods.installAll(Arrays.asList(item(green), item(big), item(pcBig), item(red)), cfg3, null, null, null);
        for (UserMods.Result r : rs) System.out.println("        " + UserMods.statusLine(r));
        check(rs.get(0).ok, "a small mod fits");
        check(!rs.get(1).ok && rs.get(1).text.contains("Not enough room") && rs.get(1).text.contains("MB"),
              "the big one refused with the numbers: " + one(rs.get(1).text));
        String[] bigLeft = mods3.list((d, n) -> n.contains("Big") || n.startsWith(".incoming"));
        check(bigLeft != null && bigLeft.length == 0, "nothing of it written");
        check(!rs.get(2).ok && (refPath == null || rs.get(2).text.contains("Not enough room")),
              "the PC mod: the adapting road's own check (2 x zip + 64 MB) - " + one(rs.get(2).text));
        check(rs.get(3).ok, "the next small one goes in all the same: " + rs.get(3).folder);
        UserMods.freeSpace = File::getUsableSpace;

        System.out.println("\n4. an error nobody foresaw, in one archive: that mod ends, the next goes in");
        File cfg4 = new File(work, "cfg4");
        cfg4.mkdirs();
        UserMods.Source bad = new UserMods.Source() {
            @Override public java.io.InputStream open() { throw new AssertionError("the provider died"); }
            @Override public long size() { return 10; }
        };
        rs = UserMods.installAll(Arrays.asList(new UserMods.Item("dead.zip", bad), item(green)), cfg4, null, null, null);
        check(!rs.get(0).ok && rs.get(0).text.contains("the provider died"), "the dead one said: " + one(rs.get(0).text));
        check(rs.get(1).ok, "the next taken: " + rs.get(1).folder);

        System.out.println("\n5. the batch's words in the seven languages");
        Locale was = Locale.getDefault();
        String[] langs = {"en", "ru", "de", "es", "ja", "ko", "zh"};
        UserMods.Result ok = new UserMods.Result();
        ok.ok = true; ok.name = "Green Isaac"; ok.label = "green.zip";
        UserMods.Result up = new UserMods.Result();
        up.ok = true; up.updated = true; up.adapted = true; up.name = "PC Thing"; up.label = "pc.zip";
        UserMods.Result no = new UserMods.Result();
        no.label = "broken.zip"; no.text = "This is not a zip archive, or it is damaged: x\nmore";
        java.util.Set<String> hints = new java.util.HashSet<>(), titles = new java.util.HashSet<>(), ticks = new java.util.HashSet<>();
        for (String l : langs) {
            Locale.setDefault(new Locale(l));
            String[] w = {UserMods.batchTitle(2, 5), UserMods.summaryTitle(4, 5), UserMods.statusLine(ok), UserMods.statusLine(up),
                          UserMods.statusLine(no), UserMods.restartHint(), UserMods.sameForAllWord(), UserMods.noRoom(47L << 20, 12L << 20),
                          UserMods.keepOpenHintMany(), UserMods.restartWord()};
            boolean full = true;
            for (String s : w) if (s == null || s.isEmpty()) full = false;
            boolean nums = w[0].contains("2") && w[0].contains("5") && w[1].contains("4") && w[7].contains("47") && w[7].contains("12");
            boolean cut = !w[4].contains("more");
            check(full && nums && cut, l + ": " + String.join(" / ", w));
            /* hotfix 1.74: the window's own RESTART TO APPLY is the row's word, and the hint names it */
            check(w[5].contains(w[9]), l + ": the window's restart button (" + w[9] + ") is the word the hint names");
            hints.add(w[5]); titles.add(w[0]); ticks.add(w[6]);
        }
        check(hints.size() == 7 && titles.size() == 7 && ticks.size() == 7, "each language its own words");
        Locale.setDefault(was);

        System.out.println("\n" + failures + " failures");
        System.exit(failures == 0 ? 0 : 1);
    }
}
