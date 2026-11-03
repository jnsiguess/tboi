package ru.isaac.port;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/*
 * DESK RUN OF THE INSTALLER (plan 02.10, B1 and item 7): UserMods.install on a zip from disk, no Android.
 *
 *   InstallDesk <zip> <work dir>
 *
 * Checks: the steps come READ -> CHECK -> UNPACK -> PLACE -> LIST; within a step done never falls and
 * ends at total; no more than ten reports in any second (a step's edges aside); UNPACK's total is the
 * zip's file count; a second add of the same mod asks, "beside" makes a new numbered folder, "update"
 * goes into the first; a different mod with the same id is new without a question; a broken zip
 * stops before PLACE with a reason; nothing is left marked .adding.
 *
 * Plan 30.09, M6 and 4.10.4 (threeClasses): the three classes - a script and dead weight placed as they
 * are and listed, system junk and the port's marks not unpacked, patch/ and options/<key>/<value>/ read,
 * an undeclared or mistyped options/ folder refused; a PC mod with metadata.xml only taken with a
 * mod.json made for it; the one judge of mod.json (json.c through modcheck.dll) refusing what org.json
 * forgives; a name in languages keying the mod by its en spelling.
 */
public final class InstallDesk {
    static int failures;

    static void check(boolean ok, String what) {
        System.out.println((ok ? "  ok    " : "  FAIL  ") + what);
        if (!ok) failures++;
    }

    static final class Call { int step; long done, total, bytes, bytesTotal; long at; String what; }

    static UserMods.Source file(File f) {
        return new UserMods.Source() {
            @Override public java.io.InputStream open() throws java.io.IOException {
                return new java.io.BufferedInputStream(new java.io.FileInputStream(f), 1 << 16);
            }
            @Override public long size() { return f.length(); }
        };
    }

    static UserMods.Result add(File zip, File cfg, List<Call> calls, Boolean answer, List<String> asked) {
        return UserMods.install(file(zip), cfg, (step, done, total, bytes, bytesTotal, what) -> {
            Call c = new Call();
            c.step = step; c.done = done; c.total = total; c.bytes = bytes; c.bytesTotal = bytesTotal; c.what = what;
            c.at = System.nanoTime();
            calls.add(c);
        }, answer == null ? null : q -> { asked.add(q); return answer; });
    }

    static void steps(List<Call> calls, int files, boolean placed) {
        int last = 0;
        boolean order = true, monotonic = true, ends = true;
        long prevDone = -1;
        int maxPerSecond = 0;
        for (int i = 0; i < calls.size(); i++) {
            Call c = calls.get(i);
            if (c.step < last) order = false;
            if (c.step != last) { prevDone = -1; last = c.step; }
            if (c.done < prevDone) monotonic = false;
            prevDone = c.done;
            boolean endOfStep = i + 1 == calls.size() || calls.get(i + 1).step != c.step;
            if (endOfStep && c.total >= 0 && c.done != c.total) ends = false;
            int n = 0;
            for (int j = i; j < calls.size() && calls.get(j).at - c.at < 1_000_000_000L; j++) {
                Call d = calls.get(j);
                boolean edge = (j == 0 || calls.get(j - 1).step != d.step) || (d.total >= 0 && d.done >= d.total);
                if (!edge) n++;
            }
            maxPerSecond = Math.max(maxPerSecond, n);
        }
        StringBuilder seen = new StringBuilder();
        int s = 0;
        for (Call c : calls) if (c.step != s) { s = c.step; seen.append(s).append(' '); }
        check(order, "the steps in order: " + seen.toString().trim() + " (READ, [ADAPT_READ, ADAPT,] CHECK, UNPACK, PLACE, LIST)");
        String want = placed ? UserMods.READ + " " + UserMods.CHECK + " " + UserMods.UNPACK + " " + UserMods.PLACE + " " + UserMods.LIST
                             : UserMods.READ + " " + UserMods.CHECK;
        String got = seen.toString().trim();
        String adapting = UserMods.READ + " " + UserMods.ADAPT_READ + " " + UserMods.ADAPT;
        if (got.startsWith(adapting + " ")) got = UserMods.READ + got.substring(adapting.length());     /* an adapted PC mod */
        check(got.equals(want), "every step there" + (placed ? "" : " up to CHECK") + ": " + seen.toString().trim());
        check(monotonic, "done never falls within a step");
        check(ends, "every step ends at done == total");
        check(maxPerSecond <= 10, "at most ten reports a second: " + maxPerSecond);
        if (files >= 0) for (Call c : calls) if (c.step == UserMods.UNPACK) { check(c.total == files, "UNPACK counts " + c.total + " files of " + files); break; }
        long unpackMb = 0;
        for (Call c : calls) if (c.step == UserMods.UNPACK) unpackMb = c.bytesTotal >> 20;
        if (placed) System.out.println("        unpacked total " + unpackMb + " MB; a sample line: "
                + UserMods.stepName(UserMods.UNPACK, null) + " - " + UserMods.numbers(UserMods.UNPACK, files / 2, files, unpackMb << 19, unpackMb << 20));
    }

    static int filesIn(File zip) throws java.io.IOException {
        int n = 0;
        try (java.util.zip.ZipFile z = new java.util.zip.ZipFile(zip)) {
            for (java.util.Enumeration<? extends java.util.zip.ZipEntry> e = z.entries(); e.hasMoreElements(); )
                if (!e.nextElement().isDirectory()) n++;
        }
        return n;
    }

    static boolean anyAdding(File dir) {
        File[] k = dir.listFiles();
        if (k == null) return false;
        for (File f : k) {
            if (f.getName().equals(UserMods.ADDING)) return true;
            if (f.isDirectory() && anyAdding(f)) return true;
        }
        return false;
    }

    public static void main(String[] a) throws Exception {
        /* the judge of mod.json: json.c through modcheck_jni.c, built for the desk by run_java.sh */
        String jni = System.getProperty("umdesk.jni");
        if (jni != null) System.load(jni);
        System.out.println("\n0. the folder's word from the mod's name (the owner, 02.10: \"001-Reflashed\")");
        String[][] names = {
            {"!!TBOI: Reflashed", "reflashrp", "Reflashed"},          /* the PC's sort marks and the TBOI tag go (05.10.2026) */
            {"!!~External item descriptions", "x", "External-item-descriptions"},
            {"TBOI - Better Items", "x", "Better-Items"},
            {"TBOI", "x", "TBOI"},
            {"TBOIRebirth Plus", "x", "TBOIRebirth-Plus"},
            {"Reflashed", "x", "Reflashed"},
            {"Русский перевод v1.2", "x", "Русский-перевод-v1.2"},
            {"日本語 モッド", "x", "日本語-モッド"},
            {"Ñandú / Größe: \"neu\"", "x", "Ñandú-Größe-neu"},
            {"!!! ???", "fallback-id", "fallback-id"},
            {".hidden", "x", "hidden"},
            {"Мод с очень длинным названием про всё на свете", "x", null},
        };
        for (String[] n : names) {
            String w = UserMods.folderWord(n[0], n[1]);
            int bytes = w.getBytes("UTF-8").length;
            check((n[2] == null || w.equals(n[2])) && bytes <= 40 && !w.startsWith(".") && !w.contains(" ") && !w.contains("/"),
                  "\"" + n[0] + "\" -> \"" + w + "\" (" + bytes + " bytes)");
        }

        File zip = new File(a[0]), work = new File(a[1]);
        UserMods.deleteTree(work);
        File cfg = new File(work, "cfg");
        cfg.mkdirs();
        int files = filesIn(zip);

        System.out.println("\n1. a first add of " + zip.getName() + " (" + files + " files, " + (zip.length() >> 20) + " MB)");
        List<Call> calls = new ArrayList<>();
        List<String> asked = new ArrayList<>();
        long t0 = System.nanoTime();
        UserMods.Result r = add(zip, cfg, calls, Boolean.FALSE, asked);
        System.out.println("        " + (System.nanoTime() - t0) / 1_000_000 + " ms, " + calls.size() + " reports; result: " + r.text.replace("\n", " | "));
        check(r.ok && r.folder != null && r.folder.matches("\\d{3}-.+"), "taken, into a numbered folder: " + r.folder);
        /* Reflashed ("!!TBOI: Reflashed") by its exact name; any other zip (05.10.2026: Cooler Completion Marks) by the
         * shape - numbered, a letter or digit first, no PC sort marks left */
        boolean reflashedZip = zip.getName().toLowerCase(java.util.Locale.ROOT).contains("reflashed");
        check(reflashedZip ? r.folder.equals("001-Reflashed")
                           : r.folder.matches("001-[A-Za-z0-9][^!~]*"), "named by the mod's name, the PC's sort marks gone: " + r.folder);
        check(asked.isEmpty(), "no question for a new mod");
        steps(calls, files, true);
        File mods = new File(cfg, "mods");
        check(!anyAdding(mods), "nothing left marked .adding");
        String first = r.folder;

        System.out.println("\n2. the same mod again, the player says \"beside it\"");
        calls.clear(); asked.clear();
        r = add(zip, cfg, calls, Boolean.FALSE, asked);
        check(asked.size() == 1, "asked: " + (asked.isEmpty() ? "-" : asked.get(0)));
        check(r.ok && !r.updated && !first.equals(r.folder), "a new folder beside it: " + r.folder);
        steps(calls, files, true);

        /*
         * hotfix 1.73: no restart after ADD MOD, so the running game still reads the replaced copy - an update
         * never writes into its folder: a new numbered folder, the old one left whole and marked .remove
         */
        System.out.println("\n3. and again, the player says \"update\" (hotfix 1.73: into a new folder, the old one marked)");
        calls.clear(); asked.clear();
        r = add(zip, cfg, calls, Boolean.TRUE, asked);
        check(asked.size() == 1, "asked again");
        check(r.ok && r.updated && !first.equals(r.folder) && r.folder.matches("\\d{3}-.+"),
              "into a new numbered folder: " + r.folder + ", updated=" + r.updated);
        check(new File(mods, first + "/.remove").exists(), "the old folder marked .remove once the new one stood");
        check(new File(mods, first + "/mod.json").isFile(), "the old copy left whole (the running game reads it until the restart)");
        String[] olds = mods.list((d, n) -> n.startsWith(".old-"));
        check(olds != null && olds.length == 0, "nothing moved aside");
        check(!r.text.contains("restart"), "the window's text says nothing of a restart of its own: " + one(r.text));
        steps(calls, files, true);
        check(!anyAdding(mods), "nothing left marked .adding");

        /*
         * 02.10.2026, the example mod's desk: a third add right after two went to <folder>-<time> (step=aside,
         * FileAlreadyExistsException) - the second had left .old-<folder>-<time> of the same second behind.
         * Three more updates as fast as they go, and a folder that already holds the names of the next
         * seconds too: each goes into the first folder, every old copy has a name of its own in the
         * shape usermods.c reads (.old-<id>-yyyyMMdd-HHmmss), all marked.
         */
        System.out.println("\n3a. updates in a row, faster than a second each (02.10.2026: the third went beside)");
        java.nio.file.Path modsPath = mods.toPath();
        long now = System.currentTimeMillis();
        java.text.SimpleDateFormat fmt = new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.ROOT);
        String taken0 = fmt.format(new java.util.Date(now)), taken1 = fmt.format(new java.util.Date(now + 1000));
        /* the names made up here must not be the real old copy of step 3: a small mod is updated within the same
         * second (05.10.2026, Cooler), and deleting the made-up one below took that copy along */
        File fake0 = new File(mods, ".old-" + first + "-" + taken0), fake1 = new File(mods, first + "-" + taken1);
        boolean made0 = !fake0.exists() && fake0.mkdirs(), made1 = !fake1.exists() && fake1.mkdirs();
        String free = UserMods.freeStamp(modsPath, first, now);
        check(!free.equals(taken0) && !free.equals(taken1) && free.matches("\\d{8}-\\d{6}"),
              "the time steps past names already there: " + taken0 + ", " + taken1 + " -> " + free);
        if (made0) UserMods.deleteTree(fake0);
        if (made1) UserMods.deleteTree(fake1);
        /* hotfix 1.73: each update a folder of its own, each replaced copy marked; none goes aside or beside */
        boolean allNew = true, noAside = true, oldMarked = true;
        java.util.Set<String> seen = new java.util.HashSet<>();
        seen.add(first);
        for (int i = 0; i < 3; i++) {
            calls.clear(); asked.clear();
            String[] before = mods.list((d, n) -> !n.startsWith(".") && new File(d, n).isDirectory() && !new File(d, n + "/.remove").exists());
            r = add(zip, cfg, calls, Boolean.TRUE, asked);
            if (!(r.ok && r.updated && seen.add(r.folder))) allNew = false;
            if (r.text != null && r.text.contains("could not be freed")) noAside = false;
            /* exactly one folder of before is marked now: the one the update replaced */
            int marked = 0;
            if (before != null) for (String b : before) if (new File(mods, b + "/.remove").exists()) marked++;
            if (marked != 1) oldMarked = false;
        }
        check(allNew, "each into a new folder: " + seen);
        check(noAside, "none went beside");
        check(oldMarked, "each update marked exactly the copy it replaced");
        olds = mods.list((d, n) -> n.startsWith(".old-"));
        check(olds != null && olds.length == 0, "nothing moved aside: " + (olds == null ? "-" : String.join(", ", olds)));
        String[] beside = mods.list((d, n) -> n.matches(java.util.regex.Pattern.quote(first) + "-\\d{8}-\\d{6}"));
        check(beside != null && beside.length == 0, "no <id>-<time> folder");
        check(!anyAdding(mods), "nothing left marked .adding");

        System.out.println("\n4. another mod with the same id (another name): new, no question");
        File other = new File(work, "other.zip");
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(other))) {
            /* a PC mod's zip has no mod.json (05.10.2026, Cooler): the id is the one the converter gave the installed mod */
            byte[] card = hasEntry(zip, "mod.json") ? readEntry(zip, "mod.json")
                        : java.nio.file.Files.readAllBytes(new File(new File(mods, first), "mod.json").toPath());
            String id = new org.json.JSONObject(new String(card, "UTF-8")).optString("id", "");
            z.putNextEntry(new java.util.zip.ZipEntry("mod.json"));
            z.write(("{\"id\": \"" + id + "\", \"name\": \"Somebody else's\"}").getBytes("UTF-8"));
            z.putNextEntry(new java.util.zip.ZipEntry("resources/gfx/x.png"));
            z.write(new byte[] {1, 2, 3});
        }
        calls.clear(); asked.clear();
        r = add(other, cfg, calls, Boolean.TRUE, asked);
        check(asked.isEmpty() && r.ok && !r.updated && r.folder.matches("\\d{3}-.+") && !r.folder.equals(first), "new beside: " + r.folder);

        System.out.println("\n5. a broken zip");
        File broken = new File(work, "broken.zip");
        /* its first MB - or its first half, for a zip smaller than that (05.10.2026, Cooler: 0.3 MB stood whole) */
        byte[] head = new byte[(int) Math.min(1 << 20, zip.length() / 2)];
        try (java.io.InputStream in = new java.io.FileInputStream(zip)) { int n = in.read(head); head = java.util.Arrays.copyOf(head, n); }
        java.nio.file.Files.write(broken.toPath(), head);
        calls.clear(); asked.clear();
        r = add(broken, cfg, calls, Boolean.TRUE, asked);
        check(!r.ok && r.text != null && !r.text.isEmpty(), "refused with a reason: " + r.text.replace("\n", " | "));
        boolean place = false;
        for (Call c : calls) if (c.step >= UserMods.UNPACK) place = true;
        check(!place, "no UNPACK, no PLACE");

        System.out.println("\n6. a zip cut short exactly between two files (the streaming reader sees no error)");
        byte[] whole = java.nio.file.Files.readAllBytes(other.toPath());
        int second = -1;
        for (int i = 4; i + 4 <= whole.length; i++)
            if (whole[i] == 'P' && whole[i + 1] == 'K' && whole[i + 2] == 3 && whole[i + 3] == 4) { second = i; break; }
        File cut = new File(work, "cut.zip");
        java.nio.file.Files.write(cut.toPath(), java.util.Arrays.copyOf(whole, second));
        calls.clear(); asked.clear();
        r = add(cut, cfg, calls, Boolean.TRUE, asked);
        check(second > 0 && !r.ok && r.text.contains("cut short"), "refused: " + r.text.replace("\n", " | "));

        threeClasses(new File(work, "m6"));
        adapting(new File(work, "adapt"));

        System.out.println("\n" + failures + " failures");
        System.exit(failures == 0 ? 0 : 1);
    }

    /* ------------------------------------------------ plan 30.09, M6 and 4.10.4: three classes, PC cards, settings */

    /* a zip of (name, text) pairs, in that order */
    static File zipOf(File dir, String file, String... pairs) throws java.io.IOException {
        dir.mkdirs();
        File f = new File(dir, file);
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(f))) {
            for (int i = 0; i < pairs.length; i += 2) {
                z.putNextEntry(new java.util.zip.ZipEntry(pairs[i]));
                z.write(pairs[i + 1].getBytes("UTF-8"));
                z.closeEntry();
            }
        }
        return f;
    }

    static String one(String s) { return s == null ? "-" : s.replace("\n", " | "); }

    static boolean has(File mods, String folder, String path) { return folder != null && new File(new File(mods, folder), path).isFile(); }

    static String text(File f) throws java.io.IOException { return new String(java.nio.file.Files.readAllBytes(f.toPath()), "UTF-8"); }

    static final String SETTINGS = "{\"id\": \"better-isaac\", \"name\": \"Better Isaac\", \"version\": \"1.2\", \"author\": \"Someone\","
        + " \"description\": {\"en\": \"Cleaner sprites.\", \"ru\": \"Чище рисовка.\"},"
        + " \"settings\": ["
        + "  {\"key\": \"blood-tears\", \"kind\": \"switch\", \"label\": {\"en\": \"Blood tears\"}, \"default\": false},"
        + "  {\"key\": \"style\", \"kind\": \"choice\", \"label\": \"Isaac style\", \"values\": ["
        + "     {\"value\": \"classic\", \"label\": \"Classic\"}, {\"value\": \"clean\", \"label\": \"Clean\"}, {\"value\": \"retro\", \"label\": \"Retro\"}],"
        + "   \"default\": \"clean\"},"
        + "  {\"key\": \"outline\", \"kind\": \"number\", \"label\": \"Outline width\", \"min\": 0, \"max\": 3, \"step\": 1, \"default\": 1}"
        + " ]}";

    static void threeClasses(File work) throws Exception {
        System.out.println("\n7a. one judge of mod.json: json.c is loaded");
        check(UserMods.judge("{}".getBytes("UTF-8")) != null, "UserMods.checkModJson answers (modcheck.dll)");
        UserMods.deleteTree(work);
        File cfg = new File(work, "cfg");
        cfg.mkdirs();
        File mods = new File(cfg, "mods");
        List<Call> calls = new ArrayList<>();
        List<String> asked = new ArrayList<>();
        UserMods.Result r;

        System.out.println("\n7. the id's rules for a PC mod's directory (plan 30.09, M6)");
        String[][] ids = {
            {"Better Bolder_123", "better-bolder_123"}, {"Café Mod!", "cafe-mod"}, {"__x", "x"}, {"Мод", ""},
            {"reflashed", "reflashed"}, {"A".repeat(50), "a".repeat(40)}, {"x---y--", "x-y"},
        };
        for (String[] i : ids) {
            String w = UserMods.idWord(i[0]);
            check(w.equals(i[1]) && (w.isEmpty() || w.matches("[a-z0-9][a-z0-9_-]{0,39}")), "\"" + i[0] + "\" -> \"" + w + "\"");
        }

        System.out.println("\n8. a mod with a script and dead weight: taken, the dead weight placed as it is, the lines said");
        File z = zipOf(work, "lua.zip",
            "mod.json", "{\"id\": \"lua-mod\", \"name\": \"Lua Mod\"}",
            "resources/gfx/a.png", "png",
            "main.lua", "print(1)",
            "scripts/one.lua", "x", "scripts/two.lua", "y",
            "README.md", "readme",
            "resources/gfx/notes.txt", "notes",
            "content/items.xml", "<items/>",
            "content/entities2.xml", "<entities/>");
        calls.clear(); asked.clear();
        r = add(z, cfg, calls, Boolean.FALSE, asked);
        System.out.println("        " + one(r.text));
        check(r.ok, "taken");
        check(r.ok && r.text.contains("added: 2 files"), "two files the port reads (a.png, content/entities2.xml)");
        check(r.ok && r.text.contains("The mod's script does not run"), "the line: the script does not run");
        check(r.ok && r.text.contains("Not used by the port") && r.text.contains("• scripts/ (2 files)") && r.text.contains("• main.lua")
                && r.text.contains("• README.md") && r.text.contains("• resources/gfx/notes.txt") && r.text.contains("• content/items.xml"),
              "the dead weight listed, a folder outside as one line");
        check(has(mods, r.folder, "main.lua") && has(mods, r.folder, "scripts/two.lua") && has(mods, r.folder, "README.md")
                && has(mods, r.folder, "content/items.xml") && has(mods, r.folder, "resources/gfx/notes.txt") && has(mods, r.folder, "resources/gfx/a.png"),
              "everything placed in " + r.folder + " as it is");
        steps(calls, 9, true);

        System.out.println("\n9. system junk (a Mac zip, a git checkout): not unpacked, nor the port's marks");
        z = zipOf(work, "junk.zip",
            "MyMod/mod.json", "{\"id\": \"junk-mod\", \"name\": \"Junk Mod\"}",
            "MyMod/resources/gfx/a.png", "png",
            "__MACOSX/MyMod/._mod.json", "x",
            "__MACOSX/MyMod/resources/gfx/._a.png", "x",
            "MyMod/.DS_Store", "x", "MyMod/resources/.DS_Store", "x",
            "MyMod/resources/gfx/Thumbs.db", "x",
            "MyMod/.git/config", "x", "MyMod/.git/objects/ab/cdef", "x",
            "MyMod/.remove", "", "MyMod/.adding", "");
        calls.clear(); asked.clear();
        r = add(z, cfg, calls, Boolean.FALSE, asked);
        System.out.println("        " + one(r.text));
        check(r.ok && !r.text.contains("Not used by the port"), "taken, nothing listed as dead weight");
        File dir = r.folder == null ? null : new File(mods, r.folder);
        check(dir != null && !new File(dir, ".DS_Store").exists() && !new File(dir, ".git").exists() && !new File(dir, "__MACOSX").exists()
                && !new File(dir, "resources/.DS_Store").exists() && !new File(dir, "resources/gfx/Thumbs.db").exists()
                && !new File(dir, ".remove").exists() && !new File(dir, ".adding").exists() && has(mods, r.folder, "resources/gfx/a.png"),
              "no junk and no marks in " + r.folder);
        steps(calls, 2, true);

        System.out.println("\n10. settings: patch/ and options/<key>/<value>/ taken (the scheme 02.10.2026)");
        z = zipOf(work, "settings.zip",
            "mod.json", SETTINGS,
            "preview.png", "png",
            "resources/gfx/main.png", "png",
            "options/blood-tears/on/resources/gfx/tears.png", "png",
            "options/style/retro/resources/gfx/isaac.png", "png",
            "options/style/Classic/resources-dlc3/gfx/isaac.png", "png",
            "options/outline/0/resources/gfx/o.png", "png",
            "options/outline/2/resources/gfx/o.png", "png",
            "options/outline/2/content/entities2.xml", "<entities/>",
            "options/outline/2/readme.txt", "x",
            "options/notes.txt", "x",
            "patch/items.xml", "<items/>",
            "patch/stringtable.sta", "x",
            "patch/sub/players.xml", "<players/>");
        calls.clear(); asked.clear();
        r = add(z, cfg, calls, Boolean.FALSE, asked);
        System.out.println("        " + one(r.text));
        check(r.ok && r.text.contains("added: 9 files"), "taken, 9 files read (1 main, 6 in layers, 2 in patch/)");
        check(r.ok && r.text.contains("• options/notes.txt") && r.text.contains("• options/outline/2/readme.txt") && r.text.contains("• patch/sub/players.xml")
                && !r.text.contains("script"), "a file in no layer, a text in a layer and a nested patch are dead weight; no script line");
        check(has(mods, r.folder, "options/style/Classic/resources-dlc3/gfx/isaac.png") && has(mods, r.folder, "patch/items.xml")
                && has(mods, r.folder, "options/outline/2/readme.txt"), "the layers and patch/ in place");
        steps(calls, 14, true);

        System.out.println("\n11. an options/ folder mod.json does not declare: taken, the folder said (hotfix 1.71: never refused)");
        z = zipOf(work, "typo.zip",
            "mod.json", SETTINGS,
            "resources/gfx/main.png", "png",
            "options/outline/1/resources/gfx/o.png", "png",
            "options/blod-tears/on/resources/gfx/tears.png", "png");
        calls.clear(); asked.clear();
        r = add(z, cfg, calls, Boolean.FALSE, asked);
        System.out.println("        " + one(r.text));
        check(r.ok && r.text.contains("options/blod-tears/") && r.text.contains("not declared"), "taken, options/blod-tears/ said: not declared");

        System.out.println("\n12. values a setting does not have: taken, each said with its reason");
        z = zipOf(work, "values.zip",
            "mod.json", SETTINGS,
            "resources/gfx/main.png", "png",
            "options/blood-tears/yes/resources/gfx/t.png", "png",
            "options/style/modern/resources/gfx/i.png", "png",
            "options/outline/two/resources/gfx/o.png", "png");
        calls.clear(); asked.clear();
        r = add(z, cfg, calls, Boolean.FALSE, asked);
        System.out.println("        " + one(r.text));
        check(r.ok && r.text.contains("options/blood-tears/yes/") && r.text.contains("only on and off")
                && r.text.contains("options/style/modern/") && r.text.contains("classic, clean, retro")
                && r.text.contains("options/outline/two/") && r.text.contains("whole-number"), "taken, three reasons said");

        System.out.println("\n13. a number with no folder at all: taken, said (it changes nothing)");
        z = zipOf(work, "nofolder.zip",
            "mod.json", "{\"id\": \"n\", \"name\": \"N\", \"settings\": [{\"key\": \"size\", \"kind\": \"number\", \"label\": \"Size\", \"min\": 0, \"max\": 3, \"step\": 1, \"default\": 0}]}",
            "resources/gfx/main.png", "png");
        r = add(z, cfg, new ArrayList<>(), Boolean.FALSE, asked);
        System.out.println("        " + one(r.text));
        check(r.ok && r.text.contains("size") && r.text.contains("would change nothing"), "taken, said");

        System.out.println("\n14. a PC mod with metadata.xml and no mod.json: taken, its card made, its script said");
        String meta = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<metadata>\n"
            + "  <name>Better &amp; Bolder</name>\n  <directory>Better Bolder_123</directory>\n  <id>2489635144</id>\n"
            + "  <description><![CDATA[Bolder <b>tears</b>]]> & more</description>\n  <version>1.5</version>\n"
            + "  <visibility>Public</visibility>\n  <tag id=\"Graphics\"/>\n</metadata>\n";
        z = zipOf(work, "pc.zip",
            "Better Bolder_123/metadata.xml", meta,
            "Better Bolder_123/main.lua", "print(1)",
            "Better Bolder_123/resources/gfx/a.png", "png");
        calls.clear(); asked.clear();
        r = add(z, cfg, calls, Boolean.FALSE, asked);
        System.out.println("        " + one(r.text));
        check(r.ok && r.folder.matches("\\d{3}-Better-Bolder"), "taken into " + r.folder);
        check(r.ok && r.text.contains("metadata.xml") && r.text.contains("\"better-bolder_123\"") && r.text.contains("script does not run"),
              "the lines: the card from metadata.xml, the id, the script");
        String card = has(mods, r.folder, "mod.json") ? text(new File(new File(mods, r.folder), "mod.json")) : "";
        org.json.JSONObject j = card.isEmpty() ? null : new org.json.JSONObject(card);
        check(j != null && j.optString("id", "").equals("better-bolder_123") && j.optString("name", "").equals("Better & Bolder")
                && j.optString("version", "").equals("1.5") && j.optString("description", "").equals("Bolder <b>tears</b> & more"),
              "mod.json written: " + card.replace("\n", " "));
        check(has(mods, r.folder, "metadata.xml") && has(mods, r.folder, "main.lua"), "metadata.xml and main.lua in place");
        check("".equals(UserMods.judge(card.getBytes("UTF-8"))), "json.c finds nothing to say about the card made");
        String pcFolder = r.folder;
        calls.clear(); asked.clear();
        r = add(z, cfg, calls, Boolean.TRUE, asked);
        check(asked.size() == 1 && r.ok && r.updated && !pcFolder.equals(r.folder) && new File(mods, pcFolder + "/.remove").exists(),
              "again: asked, updated into " + r.folder + ", " + pcFolder + " marked to go (hotfix 1.73)");

        System.out.println("\n15. not a mod at all, and the hard refusals that stay");
        r = add(zipOf(work, "none.zip", "main.lua", "x", "readme.txt", "y"), cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(!r.ok && r.text.contains("neither mod.json nor metadata.xml"), "no card: " + one(r.text));
        r = add(zipOf(work, "escape.zip", "mod.json", "{\"id\": \"e\", \"name\": \"E\"}", "resources/gfx/a.png", "p",
                "resources/../../evil.png", "x"), cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(!r.ok && r.text.contains("not allowed") && !new File(work, "evil.png").exists() && !new File(cfg, "evil.png").exists(),
              "a path out of the folder: " + one(r.text));
        /* hotfix 1.71: a mod.json cut short is read leniently (the object closed for it) - taken */
        r = add(zipOf(work, "syntax.zip", "mod.json", "{\"id\": \"s\", \"name\": \"S\",", "resources/gfx/a.png", "p"),
                cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(r.ok, "mod.json cut short: read leniently, taken: " + one(r.text));
        r = add(zipOf(work, "notobject.zip", "mod.json", "[1, 2]", "resources/gfx/a.png", "p"),
                cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(!r.ok && r.text.contains("broken [root_not_object]"), "mod.json that is no object at all (1.666 refused it too): " + one(r.text));
        r = add(zipOf(work, "deadonly.zip", "mod.json", "{\"id\": \"d\", \"name\": \"D\"}", "README.md", "x", "main.lua", "x"),
                cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(!r.ok && r.text.contains("no file the port reads"), "dead weight only: " + one(r.text));
        r = add(zipOf(work, "badid.zip", "mod.json", "{\"id\": \"Bad Id!\", \"name\": \"B\"}", "resources/gfx/a.png", "p"),
                cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(!r.ok && r.text.contains("bad id"), "a bad id: " + one(r.text));
        r = add(zipOf(work, "pcbad.zip", "metadata.xml", "<metadata><name>Мод</name></metadata>", "resources/gfx/a.png", "p"),
                cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(!r.ok && r.text.contains("will not do"), "a PC card with nothing to make an id from: " + one(r.text));
        r = add(zipOf(work, "two.zip", "A/mod.json", "{\"id\": \"a\", \"name\": \"A\"}", "A/resources/gfx/a.png", "p",
                "B/resources/gfx/b.png", "p"), cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(!r.ok && r.text.contains("outside the mod's folder"), "a file outside the mod's folder: " + one(r.text));

        System.out.println("\n15a. the slips of a hand and a broken setting: taken (hotfix 1.71, the owner: leniently)");
        String[][] strict = {
            {"{\"id\": \"t\", \"name\": \"T\",}", "T", "trailing commas"},
            {"{\"id\": \"t\", \"name\": \"T\" /* hi */}", "T", "comments"},
            {"{\"id\": \"t\", 'name': \"T\"}", "T", "single"},
            {"{\"id\": \"t\", \"name\": \"T\", \"name\": \"U\"}", "U", "a key twice: the last"},
            {"{id: \"t\", name: \"T\"} trailing text", "T", "bare words, text after"},
            {"{\"id\": \"t\", \"name\": \"T\", \"settings\": [{\"key\": \"k\", \"kind\": \"switch\", \"label\": \"This label is far too long to fit\", \"default\": true}]}",
             "T", "a broken setting (label too long): off in the game, the mod taken"},
            {"{\"id\": \"t\", \"name\": \"T\", \"settings\": [{\"key\": \"k\", \"kind\": \"number\", \"label\": \"K\", \"min\": 0, \"max\": 4, \"step\": 2, \"default\": 1}]}",
             "T", "a broken setting (default off the grid): off in the game, the mod taken"},
        };
        for (String[] c : strict) {
            asked.clear();
            r = add(zipOf(work, "strict.zip", "mod.json", c[0], "resources/gfx/a.png", "p", "options/k/2/resources/gfx/a.png", "p"),
                    cfg, new ArrayList<>(), Boolean.TRUE, asked);
            check(r.ok && r.text.contains("\"" + c[1] + "\""), c[2] + ": taken - " + one(r.text));
        }
        r = add(zipOf(work, "note.zip", "mod.json", "{\"id\": \"note-only\", \"name\": \"Note Only\", \"homepage\": \"x\"}",
                "resources/gfx/a.png", "p"), cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(r.ok, "a note only (an unknown field) does not refuse: " + one(r.text));

        System.out.println("\n16. a name in languages (plan 30.09, 4.10.1): the folder from en, whatever the phone says");
        z = zipOf(work, "lang.zip",
            "mod.json", "{\"name\": {\"ru\": \"Мой мод\", \"en\": \"My Mod\"}, \"version\": \"1\"}",
            "resources/gfx/a.png", "p");
        r = add(z, cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(r.ok && r.folder.matches("\\d{3}-My-Mod"), "taken into " + r.folder + ": " + one(r.text));
        asked.clear();
        r = add(z, cfg, new ArrayList<>(), Boolean.FALSE, asked);
        check(asked.size() == 1, "the same mod is found again (id from the en name): " + (asked.isEmpty() ? "-" : asked.get(0)));
        check(!anyAdding(mods), "nothing left marked .adding");
    }

    /*
     * 17. THE CONVERTER AT INSTALL (04.10.2026): a mod with no mod.json is adapted by ModPort and the
     * result installed; one in the port's format is not touched. -Dumdesk.gameref names the reference
     * (the APK's assets/modport_gameref.bin); -Dumdesk.adaptbig a big PC mod zip to time (Reflashed).
     */
    static void adapting(File work) throws Exception {
        String refPath = System.getProperty("umdesk.gameref");
        System.out.println("\n17. the converter at install (ModPort): a PC mod adapted, a mod of the port's format untouched");
        if (refPath == null) { System.out.println("        (no -Dumdesk.gameref: skipped)"); return; }
        UserMods.deleteTree(work);
        File cfg = new File(work, "cfg");
        cfg.mkdirs();
        File mods = new File(cfg, "mods");
        UserMods.gameRef = () -> new java.io.FileInputStream(refPath);
        try {
            java.awt.image.BufferedImage img = new java.awt.image.BufferedImage(512, 512, java.awt.image.BufferedImage.TYPE_INT_ARGB);
            for (int y = 0; y < 512; y++) for (int x = 0; x < 512; x++) img.setRGB(x, y, ((x * 7 + y) % 255) << 24 | (x % 251) << 16 | (y % 241) << 8 | 77);
            java.io.ByteArrayOutputStream png = new java.io.ByteArrayOutputStream();
            javax.imageio.ImageIO.write(img, "png", png);
            File z = new File(work, "pc.zip");
            try (java.util.zip.ZipOutputStream zo = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(z))) {
                zo.putNextEntry(new java.util.zip.ZipEntry("metadata.xml"));
                zo.write("<metadata><name>PC Sketch</name><directory>pcsketch</directory><version>3</version><description>Sketch & co</description></metadata>".getBytes("UTF-8"));
                zo.putNextEntry(new java.util.zip.ZipEntry("main.lua"));
                zo.write("-- nothing".getBytes("UTF-8"));
                zo.putNextEntry(new java.util.zip.ZipEntry("resources/gfx/ui/main menu/sketch.png"));
                zo.write(png.toByteArray());
                zo.closeEntry();
            }
            List<Call> calls = new ArrayList<>();
            List<String> asked = new ArrayList<>();
            UserMods.Result r = add(z, cfg, calls, Boolean.FALSE, asked);
            check(r.ok && r.adapted, "a PC mod (metadata.xml, a script, a sheet twice the game's) adapted and taken: " + r.folder + " - " + one(r.text));
            StringBuilder seen = new StringBuilder();
            int s = 0;
            for (Call c : calls) if (c.step != s) { s = c.step; seen.append(s).append(' '); }
            check(seen.toString().trim().equals(UserMods.READ + " " + UserMods.ADAPT_READ + " " + UserMods.ADAPT + " " + UserMods.CHECK + " "
                    + UserMods.UNPACK + " " + UserMods.PLACE + " " + UserMods.LIST), "the steps: " + seen + "(READ, ADAPT_READ, ADAPT, CHECK, UNPACK, PLACE, LIST)");
            check(UserMods.resultTitle(r).toLowerCase(java.util.Locale.ROOT).contains("adapted"), "the window's title: " + UserMods.resultTitle(r));
            File folder = new File(mods, r.folder == null ? "-" : r.folder);
            File rep = new File(folder, UserMods.ADAPTED_REPORT);
            String report = rep.exists() ? new String(java.nio.file.Files.readAllBytes(rep.toPath()), "UTF-8") : "";
            check(report.contains("baked down to 256x256"), "the converter's report beside the mod: " + rep.getName() + " (" + report.length() + " bytes)");
            File sheet = new File(folder, "resources/gfx/ui/main menu/sketch.png");
            java.awt.image.BufferedImage back = sheet.exists() ? javax.imageio.ImageIO.read(sheet) : null;
            check(back != null && back.getWidth() == 256 && back.getHeight() == 256, "the sheet baked down to the game's 256x256");
            File cardFile = new File(folder, "mod.json");
            String card = cardFile.exists() ? new String(java.nio.file.Files.readAllBytes(cardFile.toPath()), "UTF-8") : "";
            check(card.contains("\"en\": \"Adapted automatically") && card.contains("\"ru\": \"") && card.contains("Sketch & co"),
                  "mod.json: the page's description says it is adapted, the card's own after it");
            check(!new File(folder, "main.lua").exists() && cardFile.exists(), "the result is the port's format (mod.json, resources/; the script not carried)");

            File script = zipOf(work, "script.zip", "metadata.xml", "<metadata><name>Only Lua</name><directory>onlylua</directory></metadata>", "main.lua", "x");
            calls.clear();
            r = add(script, cfg, calls, Boolean.FALSE, asked);
            check(!r.ok && r.text.contains("script mod"), "a script-only PC mod refused, saying why: " + one(r.text));

            File ours = zipOf(work, "ours.zip", "mod.json", "{\"id\": \"ours\", \"name\": \"Ours\"}", "resources/gfx/a.png", "p");
            calls.clear();
            r = add(ours, cfg, calls, Boolean.FALSE, asked);
            boolean adaptStep = false;
            for (Call c : calls) if (c.step == UserMods.ADAPT || c.step == UserMods.ADAPT_READ) adaptStep = true;
            check(r.ok && !r.adapted && !adaptStep, "a mod in the port's format: not adapted, taken as it is");
            String[] left = mods.list((d, n) -> n.startsWith(".incoming-adapt"));
            check(!anyAdding(mods) && (left == null || left.length == 0), "no converter zip left behind");

            String big = System.getProperty("umdesk.adaptbig");
            if (big != null) {
                calls.clear();
                long t0 = System.nanoTime();
                r = add(new File(big), cfg, calls, Boolean.FALSE, asked);
                Runtime rt = Runtime.getRuntime();
                check(r.ok && r.adapted, "a big PC mod adapted at install: " + new File(big).getName() + " in " + (System.nanoTime() - t0) / 1_000_000
                        + " ms, heap " + (rt.maxMemory() >> 20) + " MB: " + one(r.text));
                steps(calls, -1, true);
            }
        } finally {
            UserMods.gameRef = null;
        }
    }

    static boolean hasEntry(File zip, String name) throws java.io.IOException {
        try (java.util.zip.ZipFile z = new java.util.zip.ZipFile(zip)) { return z.getEntry(name) != null; }
    }

    static byte[] readEntry(File zip, String name) throws java.io.IOException {
        try (java.util.zip.ZipFile z = new java.util.zip.ZipFile(zip)) {
            java.util.zip.ZipEntry e = z.getEntry(name);
            try (java.io.InputStream in = z.getInputStream(e)) { return in.readAllBytes(); }
        }
    }
}
