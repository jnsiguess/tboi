package ru.isaac.port;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/*
 * THE CONVERTER INSIDE THE INSTALLER (the owner, 04.10.2026): a mod not in the port's format - a PC mod
 * with its metadata.xml, or bare resources/, no mod.json - is adapted once, at ADD MOD, by the rules of
 * tools/modport.py, and installed as the result. ONE SOURCE OF TRUTH: modport.py is the reference, the
 * web page's modport.js and this file follow it step for step, report line for report line;
 * tools/umdesk/test_converters.py runs all three on the 25 workshop mods and Reflashed and compares the
 * reports, the file lists, the pictures pixel for pixel and the xml byte for byte (Python and Java;
 * the page by its tree).
 *
 * The game reference is the page's gameref.bin (tools/modport_ref.py), built from the very tree the pack
 * is made of and carried in the APK as assets/modport_gameref.bin (tools/build.py makes it next to the
 * pack) - the player exports nothing.
 *
 * A PHONE'S MEMORY: the mod is read twice as a stream (Reflashed: 5387 files, 346 MB) - the first time
 * every file's name, size and CRC, the text files (anm2, xml, fnt: 36 MB for Reflashed) and the head of
 * every picture and room; the decisions are made on those; the second time the result is written to a
 * zip beside the mods, the pictures copied as they are or baked a few rows at a time (ModPortPng).
 */
final class ModPort {
    private ModPort() { }

    /* ------------------------------------------------------------------ the rules' tables (modport.py) */

    static final Set<String> KEEP_EXT = set("png", "anm2", "xml", "wav", "ogg", "fnt", "stb", "sta", "ogv", "srt");
    /* the anm2 the port's own code reads by layer and frame number, each from the file the game loads - a mod's own
     * copy included - stepping aside where that copy has not what it needs: a mod's own copy is the mod's, its sheets
     * cut by its own frames (hotfix 1.71, 05.10.2026). Only the mod's sheets the GAME's frames cut - the mod brings no
     * copy of the anm2 for resources/ - are held to the game's size (usermods.c guard_by_number_sheets; their pictures
     * and twins are the reference's "bynumber", modport.py by_number_sheets) */
    static final Set<String> BY_NUMBER_FILES = set("gfx/ui/completion_widget.anm2", "gfx/ui/scoremenu.anm2",
            "gfx/ui/hudstats2.anm2", "gfx/ui/buttons.anm2", "gfx/ui/bosshp_icons/statuseffect_icon.anm2");
    static final String BY_NUMBER_KEPT = ": the mod's own - the port's code reads this file by layer and frame number from the copy the game "
            + "loads, the mod's included, and steps aside where it has not what it needs; the sheets it cuts are "
            + "the mod's layout, whatever their size";
    static final String WIDGET_KEPT = "; Completion Mark Reminder (TWEAKS) leaves a mod's widget alone unless the mod also brings "
            + "gfx/port_completion_widget.png and port_completion_widget_pause.png cut for it";
    /* the menus the port lays its additions over by name: a mod's own copy is always kept (05.10.2026) - the port's
     * check at start uses it when every addition fits it, the game's file with them otherwise (guard_port_patched) */
    static final Set<String> PORT_PATCHED = set("gfx/ui/main menu/gamemenu.anm2", "gfx/ui/main menu/optionsmenu.anm2",
            "gfx/ui/pausescreen.anm2", "gfx/ui/touch/ios_buttons.anm2");
    /* the menus whose sheets the port's check holds to the game's size under the game's frames (usermods.c
     * guard_menu_sheets; their pictures are the reference's "bymenu"); ios_buttons is the Rebirth set's */
    static final Set<String> MENU_SIZED = set("gfx/ui/main menu/gamemenu.anm2", "gfx/ui/main menu/optionsmenu.anm2",
            "gfx/ui/pausescreen.anm2");
    static final String MENU_KEPT = ": the mod's own - the port lays its rows, straps and buttons over it by name, and they all fit it "
            + "(the port's check at start, run here)";
    static final String MENU_REFUSED = "%s: menu file not used: %s - the port's check at start would use the game's file with the port's menu "
            + "instead, so it is left out here";
    static final String ORPHAN_REFUSED = "%s: a picture of the mod's %s, which the port would not use (see that file): %dx%d is neither the "
            + "game's %dx%d nor a whole multiple of it, so the game's frames cannot cut it - the game's picture is used, so it is left out here";
    static final int ANM_TRACKS_MAX = 64, ANM_FRAMES_MAX = 5000, KFIT_K_MAX = 16;
    static final Set<String> ALL_SETS = set("resources", "resources-dlc3");
    static final String ROOM_REFUSED = "%s: room file format %s is not the game's %s - the game's loader reads no room of such a file, "
            + "and a room the game then asks for there is read through a null pointer, a crash: the game's stays";
    static final String TABLE_COMPLETED = "%s: an older table - %d records of the game's it lacks added at its end; the mod's own records "
            + "are kept whole, as the port's own check at start does";
    static final int MAX_TEX = 4096;
    static final String[] IOS_ONLY_ANM2 = {"resources/gfx/ui/extratouchwidgets.anm2"};
    static final Set<String> TEXT_ATTRS = set("name", "description", "text", "hud", "birthright");
    static final Map<String, String> OTHER_GAME_SETS = new HashMap<>();
    static final Map<String, String> CONTENT_WHY = new HashMap<>();
    static final Map<String, Spec> TEXT_XML = new HashMap<>();
    static {
        OTHER_GAME_SETS.put("resources-dlc4", "Repentance+'s folder (the port is Repentance)");
        OTHER_GAME_SETS.put("resources-repentogon", "REPENTOGON's folder (the port has no REPENTOGON)");
        CONTENT_WHY.put("items.xml", "new items would be written past the save's fixed tables");
        CONTENT_WHY.put("pocketitems.xml", "new cards, runes and pills would be written past the save's fixed tables");
        CONTENT_WHY.put("players.xml", "new characters would be written past the save's fixed tables");
        CONTENT_WHY.put("challenges.xml", "new challenges would be written past the save's fixed tables");
        CONTENT_WHY.put("itempools.xml", "pools for new items, which do not come");
        CONTENT_WHY.put("costumes2.xml", "new costumes are put on by a script or by new items, neither of which come");
        CONTENT_WHY.put("music.xml", "new music is played by a script, which does not run");
        CONTENT_WHY.put("sounds.xml", "new sounds are played by a script, which does not run");
        TEXT_XML.put("items.xml", new Spec("passive|active|familiar|trinket|null", "id", "name", "description"));
        TEXT_XML.put("pocketitems.xml", new Spec("card|pilleffect|rune|pill", "id", "name", "description", "hud"));
        TEXT_XML.put("entities2.xml", new Spec("entity", null, "name"));
        TEXT_XML.put("minibosses.xml", new Spec("miniboss", "id", "name"));
        TEXT_XML.put("stages.xml", new Spec("stage", "id", "name"));
        TEXT_XML.put("challenges.xml", new Spec("challenge", "id", "name"));
        TEXT_XML.put("curses.xml", new Spec("curse", "id", "name"));
        TEXT_XML.put("babies.xml", new Spec("baby", "id", "name"));
        TEXT_XML.put("achievements.xml", new Spec("achievement", "id", "text"));
    }

    static final class Spec {
        final Set<String> tags;
        final String idattr;
        final String[] attrs;
        Spec(String tags, String idattr, String... attrs) {
            this.tags = new HashSet<>(Arrays.asList(tags.split("\\|")));
            this.idattr = idattr;
            this.attrs = attrs;
        }
    }

    private static Set<String> set(String... s) { return new HashSet<>(Arrays.asList(s)); }

    /* ------------------------------------------------------------------ the game reference (gameref.bin) */

    static final class Ref {
        final Map<String, Map<String, Object>> anm2 = new HashMap<>();
        final Map<String, int[]> png = new HashMap<>();
        final Map<String, String> xml = new HashMap<>();
        final Set<String> xmls = new HashSet<>();
        final Map<String, String> stb = new HashMap<>();
        final Set<String> cut = new HashSet<>(), portcut = new HashSet<>(), portcode = new HashSet<>();
        final Map<String, String> iosonly = new HashMap<>();
        final Map<String, long[]> portown = new HashMap<>();
        final Map<String, String> tables = new HashMap<>();
        final Map<String, List<String>> bynumber = new HashMap<>();
        final Map<String, List<String>> bymenu = new HashMap<>();
        final List<Map<String, Object>> portpatch = new ArrayList<>();

        @SuppressWarnings("unchecked")
        static Ref load(InputStream packed) throws IOException {
            java.util.zip.InflaterInputStream in = new java.util.zip.InflaterInputStream(packed);
            ByteArrayOutputStream b = new ByteArrayOutputStream();
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) b.write(buf, 0, n);
            Map<String, Object> j = (Map<String, Object>) new Json(new String(b.toByteArray(), StandardCharsets.UTF_8)).value();
            Ref r = new Ref();
            for (Map.Entry<String, Object> e : ((Map<String, Object>) j.get("anm2")).entrySet())
                r.anm2.put(e.getKey(), (Map<String, Object>) e.getValue());
            for (Map.Entry<String, Object> e : ((Map<String, Object>) j.get("png")).entrySet()) {
                List<Object> s = (List<Object>) e.getValue();
                r.png.put(e.getKey(), new int[] {((Number) s.get(0)).intValue(), ((Number) s.get(1)).intValue()});
            }
            for (Map.Entry<String, Object> e : ((Map<String, Object>) j.get("xml")).entrySet()) r.xml.put(e.getKey(), (String) e.getValue());
            for (Object o : (List<Object>) j.get("xmls")) r.xmls.add((String) o);
            for (Map.Entry<String, Object> e : ((Map<String, Object>) j.get("stb")).entrySet()) r.stb.put(e.getKey(), (String) e.getValue());
            for (Object o : (List<Object>) j.get("cut")) r.cut.add((String) o);
            for (Object o : (List<Object>) j.get("portcut")) r.portcut.add((String) o);
            for (Object o : (List<Object>) j.get("portcode")) r.portcode.add((String) o);
            Object io = j.get("iosonly");
            if (io != null) for (Map.Entry<String, Object> e : ((Map<String, Object>) io).entrySet()) r.iosonly.put(e.getKey(), (String) e.getValue());
            Object po = j.get("portown");
            if (po != null) for (Map.Entry<String, Object> e : ((Map<String, Object>) po).entrySet()) {
                List<Object> s = (List<Object>) e.getValue();
                r.portown.put(e.getKey(), new long[] {((Number) s.get(0)).longValue(), ((Number) s.get(1)).longValue()});
            }
            Object tb = j.get("tables");
            if (tb != null) for (Map.Entry<String, Object> e : ((Map<String, Object>) tb).entrySet()) r.tables.put(e.getKey(), (String) e.getValue());
            Object bn = j.get("bynumber");
            if (bn != null) for (Map.Entry<String, Object> e : ((Map<String, Object>) bn).entrySet()) {
                List<String> s = new ArrayList<>();
                for (Object o : (List<Object>) e.getValue()) s.add((String) o);
                r.bynumber.put(e.getKey(), s);
            }
            Object pp = j.get("portpatch");
            if (pp != null) for (Object o : (List<Object>) pp) r.portpatch.add((Map<String, Object>) o);
            Object bm = j.get("bymenu");
            if (bm != null) for (Map.Entry<String, Object> e : ((Map<String, Object>) bm).entrySet()) {
                List<String> s = new ArrayList<>();
                for (Object o : (List<Object>) e.getValue()) s.add((String) o);
                r.bymenu.put(e.getKey(), s);
            }
            return r;
        }

        /* the game's text of an xml (the TEXT_XML ones, then the other whole tables), null when not held */
        String xmlText(String path) {
            String t = xml.get(path);
            return t != null ? t : tables.get(path);
        }
    }

    /* a JSON reader, enough for the reference (objects in their order, arrays, strings, numbers) */
    static final class Json {
        final String s;
        int p;
        Json(String s) { this.s = s; }
        Object value() throws IOException {
            ws();
            if (p >= s.length()) throw new IOException("json ends early");
            char c = s.charAt(p);
            if (c == '{') {
                p++;
                Map<String, Object> m = new LinkedHashMap<>();
                ws();
                if (s.charAt(p) == '}') { p++; return m; }
                for (;;) {
                    ws();
                    String k = string();
                    ws();
                    p++;            /* : */
                    m.put(k, value());
                    ws();
                    char d = s.charAt(p++);
                    if (d == '}') return m;
                }
            }
            if (c == '[') {
                p++;
                List<Object> l = new ArrayList<>();
                ws();
                if (s.charAt(p) == ']') { p++; return l; }
                for (;;) {
                    l.add(value());
                    ws();
                    char d = s.charAt(p++);
                    if (d == ']') return l;
                }
            }
            if (c == '"') return string();
            if (s.startsWith("true", p)) { p += 4; return Boolean.TRUE; }
            if (s.startsWith("false", p)) { p += 5; return Boolean.FALSE; }
            if (s.startsWith("null", p)) { p += 4; return null; }
            int a = p;
            while (p < s.length() && "+-0123456789.eE".indexOf(s.charAt(p)) >= 0) p++;
            String num = s.substring(a, p);
            if (num.indexOf('.') >= 0 || num.indexOf('e') >= 0 || num.indexOf('E') >= 0) return Double.valueOf(num);
            return Long.valueOf(num);
        }
        void ws() { while (p < s.length() && s.charAt(p) <= ' ') p++; }
        String string() throws IOException {
            if (s.charAt(p) != '"') throw new IOException("json: a string expected");
            p++;
            StringBuilder b = new StringBuilder();
            for (;;) {
                char c = s.charAt(p++);
                if (c == '"') return b.toString();
                if (c != '\\') { b.append(c); continue; }
                char e = s.charAt(p++);
                switch (e) {
                    case 'n': b.append('\n'); break;
                    case 't': b.append('\t'); break;
                    case 'r': b.append('\r'); break;
                    case 'b': b.append('\b'); break;
                    case 'f': b.append('\f'); break;
                    case 'u': b.append((char) Integer.parseInt(s.substring(p, p + 4), 16)); p += 4; break;
                    default: b.append(e);
                }
            }
        }
    }

    /* ------------------------------------------------------------------ the report (modport.py Report) */

    static final class Report {
        final List<String> lines = new ArrayList<>();
        final Map<String, Integer> counts = new HashMap<>();
        void add(String kind, String text) {
            Integer c = counts.get(kind);
            counts.put(kind, c == null ? 1 : c + 1);
            lines.add(String.format(Locale.ROOT, "%-8s %s", kind, text));
        }
        String text() {
            StringBuilder b = new StringBuilder("modport report\n\n");
            List<String> kinds = new ArrayList<>(counts.keySet());
            Collections.sort(kinds);
            for (String k : kinds) b.append(String.format(Locale.ROOT, "%6d  %s\n", counts.get(k), k));
            b.append('\n');
            for (String l : lines) b.append(l).append('\n');
            return b.toString();
        }
    }

    /* ------------------------------------------------------------------ helpers as modport.py's */

    static String norm(String p) {
        String s = p.replace('\\', '/');
        int a = 0, b = s.length();
        while (a < b && s.charAt(a) == '/') a++;
        while (b > a && s.charAt(b - 1) == '/') b--;
        return s.substring(a, b).toLowerCase(Locale.ROOT);
    }

    /* is picture p (a mod path, any set) cut only by anm2 of BY_NUMBER_FILES the mod brings its own copy of
     * (own: those anm2, for resources/), or their twin? as modport.py by_number_free */
    static boolean byNumberFree(String p, Ref ref, Set<String> own) {
        String me = norm(p);
        String inner = me.indexOf('/') >= 0 ? me.substring(me.indexOf('/') + 1) : me;
        int owners = 0, owned = 0;
        for (Map.Entry<String, List<String>> e : ref.bynumber.entrySet()) {
            if (!e.getValue().contains(inner)) continue;
            owners++;
            if (own.contains(e.getKey())) owned++;
        }
        return owners > 0 && owned == owners;
    }

    /* the anm2 of sheets ({anm2: [pictures]}) that cut picture p (a mod path, any set) with the GAME's frames here
     * (the mod brings no copy of its own: own), and the ones the mod's own copy cuts: {game's, own} (modport.py held_by) */
    static List<List<String>> heldBy(String p, Map<String, List<String>> sheets, Set<String> own) {
        String me = norm(p);
        String inner = me.indexOf('/') >= 0 ? me.substring(me.indexOf('/') + 1) : me;
        List<String> keys = new ArrayList<>(sheets.keySet());
        Collections.sort(keys);
        List<String> g = new ArrayList<>(), o = new ArrayList<>();
        for (String a : keys) {
            if (!sheets.get(a).contains(inner)) continue;
            (own.contains(a) ? o : g).add(a);
        }
        List<List<String>> out = new ArrayList<>();
        out.add(g);
        out.add(o);
        return out;
    }

    /* ------------------------------------------------------------------ the port's check at start, run here (round 3) */
    /* modport.py: what the port's check would refuse is left out with its reason, what it would use is kept */

    /* portpatch.c num_of */
    static int numOf(String v) {
        if (v == null || v.isEmpty()) return -1;
        int i = 0;
        boolean neg = false;
        if (v.charAt(0) == '+' || v.charAt(0) == '-') { neg = v.charAt(0) == '-'; i = 1; }
        if (i >= v.length()) return -1;
        long n = 0;
        for (; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c < '0' || c > '9') return -1;
            n = n * 10 + (c - '0');
            if (n > 100000000L) return -1;
        }
        return (int) (neg ? -n : n);
    }

    static List<ModPortXml.El> elKids(ModPortXml.El e, String tag) {
        List<ModPortXml.El> out = new ArrayList<>();
        if (e == null) return out;
        for (ModPortXml.El k : e.kids) if (tag == null || k.tag.equals(tag)) out.add(k);
        return out;
    }

    /* usermods.c anm_limits: past the engine loader's limits - its reason, or null */
    static String anmLimits(ModPortXml.El root) {
        ModPortXml.El anims = root.find("Animations");
        if (anims == null) return null;
        for (ModPortXml.El a : elKids(anims, "Animation")) {
            String nm = a.get("Name", "");
            for (int nl = 0; nl < 2; nl++) {
                ModPortXml.El box = a.find(nl == 1 ? "NullAnimations" : "LayerAnimations");
                int n = elKids(box, nl == 1 ? "NullAnimation" : "LayerAnimation").size();
                if (n > ANM_TRACKS_MAX)
                    return "past the engine loader's limits: animation \"" + nm + "\" has " + n + " " + (nl == 1 ? "null" : "layer") + " tracks (" + ANM_TRACKS_MAX + " at most)";
                for (ModPortXml.El t : elKids(box, null)) {
                    int f = elKids(t, "Frame").size();
                    if (f >= ANM_FRAMES_MAX)
                        return "past the engine loader's limits: animation \"" + nm + "\" has a track of " + f + " frames (" + (ANM_FRAMES_MAX - 1) + " at most)";
                }
            }
            int rf = elKids(a.find("RootAnimation"), "Frame").size();
            if (rf >= ANM_FRAMES_MAX)
                return "past the engine loader's limits: animation \"" + nm + "\" has " + rf + " root frames (" + (ANM_FRAMES_MAX - 1) + " at most)";
        }
        return null;
    }

    /* what the patch and its all-or-nothing check read of an .anm2 (modport.py anm_model) */
    static final class Model {
        final List<String[]> sheets = new ArrayList<>(), layers = new ArrayList<>(), nulls = new ArrayList<>();
        final List<Anim> anims = new ArrayList<>();
        Model copy() {
            Model m = new Model();
            for (String[] x : sheets) m.sheets.add(x.clone());
            for (String[] x : layers) m.layers.add(x.clone());
            for (String[] x : nulls) m.nulls.add(x.clone());
            for (Anim a : anims) { Anim b = new Anim(a.name); b.lids.addAll(a.lids); b.nids.addAll(a.nids); m.anims.add(b); }
            return m;
        }
    }

    static final class Anim {
        final String name;
        final List<String> lids = new ArrayList<>(), nids = new ArrayList<>();
        Anim(String name) { this.name = name; }
    }

    static Model anmModel(ModPortXml.El root) {
        ModPortXml.El c = root.find("Content"), an = root.find("Animations");
        if (c == null || an == null) return null;
        Model m = new Model();
        for (ModPortXml.El e : elKids(c.find("Spritesheets"), "Spritesheet")) m.sheets.add(new String[] {e.get("Id"), e.get("Path")});
        for (ModPortXml.El e : elKids(c.find("Layers"), "Layer")) m.layers.add(new String[] {e.get("Id"), e.get("Name"), e.get("SpritesheetId")});
        for (ModPortXml.El e : elKids(c.find("Nulls"), "Null")) m.nulls.add(new String[] {e.get("Id"), e.get("Name")});
        for (ModPortXml.El a : elKids(an, "Animation")) {
            Anim x = new Anim(a.get("Name"));
            for (ModPortXml.El l : elKids(a.find("LayerAnimations"), "LayerAnimation")) x.lids.add(l.get("LayerId"));
            for (ModPortXml.El l : elKids(a.find("NullAnimations"), "NullAnimation")) x.nids.add(l.get("NullId"));
            m.anims.add(x);
        }
        return m;
    }

    static String[] firstBy(List<String[]> lst, int i, String want, boolean fold) {
        if (want == null) return null;
        for (String[] e : lst) {
            String v = e[i];
            if (v == null) continue;
            if (fold ? (v.length() == want.length() && v.toLowerCase(Locale.ROOT).equals(want.toLowerCase(Locale.ROOT))) : v.equals(want)) return e;
        }
        return null;
    }

    static Anim animBy(List<Anim> lst, String want) {
        if (want == null) return null;
        for (Anim a : lst) if (want.equals(a.name)) return a;
        return null;
    }

    static int maxId(List<String[]> lst) {
        int m = -1;
        for (String[] e : lst) m = Math.max(m, numOf(e[0]));
        return m;
    }

    static String str(Object o) { return o == null ? null : (String) o; }
    static String av(String s) { return s == null ? "" : s; }

    /* portpatch.c pp_apply on a model (modport.py sim_patch): the first line it would say, or null; m is changed */
    @SuppressWarnings("unchecked")
    static String simPatch(Model m, List<Object> ops) {
        String first = null;
        for (Object o : ops) {
            List<Object> op = (List<Object>) o;
            String kind = (String) op.get(0);
            String said = null;
            if (kind.equals("sheet")) {
                String path = str(op.get(1));
                if (path == null || firstBy(m.sheets, 1, path, true) != null) continue;
                m.sheets.add(new String[] {String.valueOf(maxId(m.sheets) + 1), path});
            } else if (kind.equals("layer")) {
                String name = str(op.get(1)), sp = str(op.get(2)), of = str(op.get(3));
                String[] sh = sp != null && !sp.isEmpty() ? firstBy(m.sheets, 1, sp, true) : null;
                if (sh == null && of != null && !of.isEmpty()) {
                    String[] ol = firstBy(m.layers, 1, of, false);
                    if (ol != null) for (String[] x : m.sheets) if (ol[2] != null && x[0] != null && x[0].equals(ol[2])) sh = x;
                }
                if (sh == null) said = "layer " + av(name) + ": no sheet " + av(sp) + " - refused";
                else if (name != null) {
                    String[] ly = firstBy(m.layers, 1, name, false);
                    if (ly == null) m.layers.add(new String[] {String.valueOf(maxId(m.layers) + 1), name, sh[0]});
                    else ly[2] = sh[0];
                }
            } else if (kind.equals("null")) {
                String name = str(op.get(1)), bf = str(op.get(2));
                if (name == null || firstBy(m.nulls, 1, name, false) != null) continue;
                String[] before = bf != null && !bf.isEmpty() ? firstBy(m.nulls, 1, bf, false) : null;
                if (bf != null && !bf.isEmpty() && before == null) said = "null " + name + ": no null " + bf + " to go before - appended";
                if (before != null) {
                    int nid = numOf(before[0]);
                    int at = 0;
                    while (m.nulls.get(at) != before) at++;
                    for (String[] x : m.nulls) { int v = numOf(x[0]); if (v >= nid) x[0] = String.valueOf(v + 1); }
                    for (Anim a : m.anims)
                        for (int i = 0; i < a.nids.size(); i++) if (numOf(a.nids.get(i)) >= nid) a.nids.set(i, String.valueOf(numOf(a.nids.get(i)) + 1));
                    m.nulls.add(at, new String[] {String.valueOf(nid), name});
                } else m.nulls.add(new String[] {String.valueOf(maxId(m.nulls) + 1), name});
            } else if (kind.equals("anim")) {
                String name = str(op.get(1));
                if (name == null) continue;
                Anim a = animBy(m.anims, name);
                if (a == null) { a = new Anim(name); m.anims.add(a); }
                for (Object po : (List<Object>) op.get(2)) {
                    List<Object> part = (List<Object>) po;
                    String pk = (String) part.get(0);
                    if (pk.equals("keep")) continue;
                    String line = null;
                    if (pk.equals("other")) line = name + ": unknown part <" + part.get(1) + "> - skipped";
                    else {
                        boolean isLayer = pk.equals("layer");
                        List<String[]> lst = isLayer ? m.layers : m.nulls;
                        List<String> box = isLayer ? a.lids : a.nids;
                        String target = str(part.get(1)), after = str(part.get(2));
                        String[] r = firstBy(lst, 1, target, false);
                        if (r == null) line = name + ": " + pk + " " + av(target) + " is not in the file - refused";
                        else if (!box.contains(r[0])) {
                            if (after != null && !after.isEmpty()) {
                                String[] af = firstBy(lst, 1, after, false);
                                if (!(af != null && box.contains(af[0]))) line = name + ": " + pk + " " + target + " - " + after + " is not drawn here, appended";
                            }
                            box.add(r[0]);
                        }
                    }
                    if (line != null && first == null) first = line;
                }
            } else said = "unknown operation <" + op.get(1) + "> - skipped";
            if (said != null && first == null) first = said;
        }
        return first;
    }

    /* portpatch.c pp_complete (modport.py patch_complete): why not, or null */
    @SuppressWarnings("unchecked")
    static String patchComplete(Model m, Map<String, Object> t) {
        List<Object> adds = (List<Object>) t.get("adds");
        Map<String, Object> g = (Map<String, Object>) t.get("game");
        for (Object o : (List<Object>) g.get("layers")) {
            List<Object> x = (List<Object>) o;
            String name = str(x.get(0));
            int gid = ((Number) x.get(1)).intValue();
            String[] y = firstBy(m.layers, 1, name, false);
            if (y == null) return "the port's menu needs layer " + av(name);
            if (!adds.contains(name) && numOf(y[0]) != gid) return "layer " + name + " is number " + numOf(y[0]) + " here, the game's code draws it as " + gid;
        }
        for (Object o : (List<Object>) g.get("nulls")) {
            List<Object> x = (List<Object>) o;
            String name = str(x.get(0));
            int gid = ((Number) x.get(1)).intValue();
            String[] y = firstBy(m.nulls, 1, name, false);
            if (y == null) return "the port's menu needs null " + av(name);
            if (numOf(y[0]) != gid) return "null " + name + " is number " + numOf(y[0]) + " here, the menu's touch zones go by " + gid;
        }
        for (Object o : (List<Object>) g.get("anims"))
            if (animBy(m.anims, str(o)) == null) return "the port's menu needs animation " + av(str(o));
        return null;
    }

    @SuppressWarnings("unchecked")
    static String menuFit(ModPortXml.El root, Map<String, Object> t) {
        Model model = anmModel(root);
        if (model == null) return "it does not read as an .anm2";
        Model m = model.copy();
        String said = simPatch(m, (List<Object>) t.get("ops"));
        if (said != null) return "the port's menu does not fit it: " + said;
        return patchComplete(m, t);
    }

    /* the mod's copies that answer a patched target, in THE ORDER (modport.py target_candidates) */
    static List<String> targetCandidates(Map<String, Object> t, Map<String, String> have) {
        String rel = (String) t.get("rel"), set = (String) t.get("set");
        String all = have.get("resources-dlc3/" + rel);
        if (all == null) all = have.get("resources/" + rel);
        List<String> c = new ArrayList<>();
        if (set.indexOf('.') >= 0) {
            String lang = set.substring(set.indexOf('.') + 1);
            c.add(have.get("resources-dlc3." + lang + "/" + rel));
            c.add(all);
            c.add(have.get("resources." + lang + "/" + rel));
        } else c.add(all);
        List<String> out = new ArrayList<>();
        for (String x : c) if (x != null) out.add(x);
        return out;
    }

    /* usermods.c sheet_wanted_by_own (modport.py): another used .anm2 of the mod in the sheet's folder names it */
    static boolean sheetWantedByOwn(String rel, Map<String, byte[]> texts) {
        int cut = rel.lastIndexOf('/');
        String d = cut >= 0 ? rel.substring(0, cut + 1) : "", lf = rel.substring(cut + 1);
        String want = ("path=\"" + lf + "\"").toLowerCase(Locale.ROOT);
        for (Map.Entry<String, byte[]> e : texts.entrySet()) {
            String low = e.getKey().toLowerCase(Locale.ROOT);
            String inner = low.indexOf('/') >= 0 ? low.substring(low.indexOf('/') + 1) : "";
            if (!inner.startsWith(d) || inner.substring(d.length()).indexOf('/') >= 0) continue;
            if (new String(e.getValue(), StandardCharsets.UTF_8).toLowerCase(Locale.ROOT).contains(want)) return true;
        }
        return false;
    }

    static String leafs(List<String> list) {
        List<String> out = new ArrayList<>();
        for (String x : list) out.add(x.substring(x.lastIndexOf('/') + 1));
        return String.join(", ", out);
    }

    static String dirname(String p) {
        int i = p.lastIndexOf('/');
        return i < 0 ? "" : p.substring(0, i);
    }

    static String basename(String p) {
        int i = p.lastIndexOf('/');
        return i < 0 ? p : p.substring(i + 1);
    }

    /* os.path.normpath(os.path.join(base, rel)), lower case, '/' (modport.js joinNorm) */
    static String joinNorm(String base, String rel) {
        String all = (base.isEmpty() ? "" : base + "/") + rel.replace('\\', '/');
        List<String> out = new ArrayList<>();
        for (String s : all.split("/", -1)) {
            if (s.isEmpty() || s.equals(".")) continue;
            if (s.equals("..")) { if (!out.isEmpty()) out.remove(out.size() - 1); continue; }
            out.add(s);
        }
        return String.join("/", out).toLowerCase(Locale.ROOT);
    }

    static String ext(String low) {
        int dot = low.lastIndexOf('.');
        return dot < 0 ? "" : low.substring(dot + 1);
    }

    private static final Pattern VARIANT = Pattern.compile("^(.*)_[a-z0-9]+\\.png$", Pattern.CASE_INSENSITIVE);

    static String variantBase(String p) {
        Matcher m = VARIANT.matcher(p);
        return m.matches() ? m.group(1) + ".png" : null;
    }

    /* UserMods.idWord, the installer's id rule */
    static String idWord(String s) { return UserMods.idWord(s == null ? "" : s); }

    /* Python's str.strip() on the whitespace a card carries */
    static String pyStrip(String s) {
        int a = 0, b = s.length();
        while (a < b && isPySpace(s.charAt(a))) a++;
        while (b > a && isPySpace(s.charAt(b - 1))) b--;
        return s.substring(a, b);
    }
    static boolean isPySpace(char c) {
        return Character.isWhitespace(c) || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a)
                || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000 || (c >= 0x1c && c <= 0x1f);
    }

    /* Python's repr of a float (the shortest digits that read back; fixed between 1e-4 and 1e16) */
    static String pyFloat(double d) {
        if (d == Math.rint(d) && Math.abs(d) < 1e16) {
            long l = (long) d;
            if (l == 0 && 1 / d < 0) return "-0.0";
            return l + ".0";
        }
        java.math.BigDecimal exact = new java.math.BigDecimal(d);
        java.math.BigDecimal best = null;
        for (int prec = 1; prec <= 17; prec++) {
            java.math.BigDecimal r = exact.round(new java.math.MathContext(prec, java.math.RoundingMode.HALF_EVEN));
            if (r.doubleValue() == d) { best = r; break; }
        }
        if (best == null) best = exact;
        best = best.stripTrailingZeros();
        int exp = best.precision() - best.scale() - 1;
        if (exp >= -4 && exp < 16) {
            String plain = best.toPlainString();
            return plain.indexOf('.') < 0 ? plain + ".0" : plain;
        }
        String digits = best.unscaledValue().abs().toString();
        String mant = digits.length() > 1 ? digits.charAt(0) + "." + digits.substring(1) : digits;
        return (best.signum() < 0 ? "-" : "") + mant + "e" + (exp < 0 ? "-" : "+") + (Math.abs(exp) < 10 ? "0" : "") + Math.abs(exp);
    }

    /* str(int(v)) if v == int(v) else str(v) */
    static String pyNum(double v) {
        return v == Math.rint(v) && Math.abs(v) < 9e18 ? Long.toString((long) v) : pyFloat(v);
    }

    /* Python's round(): half to even */
    static long pyRound(double x) { return (long) Math.rint(x); }

    static double fl(String s, double def) {
        if (s == null) return def;
        try { return Double.parseDouble(s.trim()); } catch (NumberFormatException e) { return def; }
    }

    /* Python's float() of an attribute (which raises on junk: the converter then stops, as modport.py) */
    static double fl(String s) { return Double.parseDouble(s.trim()); }

    static int intOf(String s, int def) {
        if (s == null) return def;
        try { return Integer.parseInt(s.trim()); } catch (NumberFormatException e) { return def; }
    }

    /* Python's repr of bytes */
    static String pyBytes(byte[] b) {
        boolean sq = false, dq = false;
        for (byte x : b) { if (x == '\'') sq = true; if (x == '"') dq = true; }
        char q = sq && !dq ? '"' : '\'';
        StringBuilder s = new StringBuilder("b").append(q);
        for (byte x : b) {
            int c = x & 0xff;
            if (c == q || c == '\\') s.append('\\').append((char) c);
            else if (c == '\t') s.append("\\t");
            else if (c == '\n') s.append("\\n");
            else if (c == '\r') s.append("\\r");
            else if (c < 0x20 || c >= 0x7f) s.append(String.format(Locale.ROOT, "\\x%02x", c));
            else s.append((char) c);
        }
        return s.append(q).toString();
    }

    static String hex(byte[] b, int n) {
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < Math.min(n, b.length); i++) s.append(String.format(Locale.ROOT, "%02x", b[i] & 0xff));
        return s.toString();
    }

    /* the json.dumps(..., ensure_ascii=False) of a string */
    static String pyJsonString(String v) {
        StringBuilder b = new StringBuilder("\"");
        for (int i = 0; i < v.length(); i++) {
            char c = v.charAt(i);
            switch (c) {
                case '"': b.append("\\\""); break;
                case '\\': b.append("\\\\"); break;
                case '\n': b.append("\\n"); break;
                case '\r': b.append("\\r"); break;
                case '\t': b.append("\\t"); break;
                case '\b': b.append("\\b"); break;
                case '\f': b.append("\\f"); break;
                default:
                    if (c < 0x20) b.append(String.format(Locale.ROOT, "\\u%04x", (int) c));
                    else b.append(c);
            }
        }
        return b.append('"').toString();
    }

    /* ------------------------------------------------------------------ anm2 (modport.py's anm2 helpers) */

    /* the pictures an anm2 cuts: sheet id -> path, in the file's order */
    static LinkedHashMap<Integer, String> sheetsOf(ModPortXml.El root, String rel) {
        LinkedHashMap<Integer, String> out = new LinkedHashMap<>();
        String base = dirname(rel);
        for (ModPortXml.El s : root.findall("Content/Spritesheets/Spritesheet")) {
            String p = s.get("Path", "").replace('\\', '/');
            out.put(Integer.parseInt(s.get("Id").trim()), joinNorm(base, p));
        }
        return out;
    }

    static Set<Integer> idsOf(ModPortXml.El root, String rel, String sheet) {
        Set<Integer> ids = new HashSet<>();
        for (Map.Entry<Integer, String> e : sheetsOf(root, rel).entrySet()) if (e.getValue().equals(sheet)) ids.add(e.getKey());
        return ids;
    }

    /* layer id -> its sheet id, the last layer of an id winning (modport.py scale_anm2_for walks them all) */
    static Map<String, Integer> layerSheets(ModPortXml.El root) {
        Map<String, Integer> m = new HashMap<>();
        for (ModPortXml.El l : root.findall("Content/Layers/Layer")) m.put(l.get("Id"), intOf(l.get("SpritesheetId"), -1));
        return m;
    }

    /* modport.py repair_anm2: the mod's anm2 made complete against the game's shape */
    @SuppressWarnings("unchecked")
    static boolean repair(ModPortXml.El root, Map<String, Object> shape, String rel, Report rep) {
        ModPortXml.El content = child(root, "Content");
        Map<Integer, ModPortXml.El> ml = byId(root, "Layers/Layer"), mn = byId(root, "Nulls/Null"), me = byId(root, "Events/Event"),
                ms = byId(root, "Spritesheets/Spritesheet");
        LinkedHashMap<String, ModPortXml.El> ma = anims(root);
        List<ModPortXml.El> animsBefore = new ArrayList<>(ma.values());
        boolean changed = false;
        for (Object o : (List<Object>) shape.get("l")) {
            List<Object> ln = (List<Object>) o;
            int lid = ((Number) ln.get(0)).intValue();
            if (ml.containsKey(lid)) continue;
            int minSheet = 0;
            if (!ms.isEmpty()) minSheet = Collections.min(ms.keySet());
            child(content, "Layers").sub("Layer", "Name", (String) ln.get(1), "Id", String.valueOf(lid), "SpritesheetId", String.valueOf(minSheet));
            for (ModPortXml.El a : animsBefore) child(a, "LayerAnimations").sub("LayerAnimation", "LayerId", String.valueOf(lid), "Visible", "false");
            rep.add("repaired", rel + ": layer " + lid + " \"" + ln.get(1) + "\" added (empty)");
            changed = true;
        }
        for (Object o : (List<Object>) shape.get("n")) {
            List<Object> nn = (List<Object>) o;
            int nid = ((Number) nn.get(0)).intValue();
            if (mn.containsKey(nid)) continue;
            child(content, "Nulls").sub("Null", "Name", (String) nn.get(1), "Id", String.valueOf(nid), "ShowRect", "false");
            for (ModPortXml.El a : animsBefore) child(a, "NullAnimations").sub("NullAnimation", "NullId", String.valueOf(nid), "Visible", "false");
            rep.add("repaired", rel + ": null " + nid + " \"" + nn.get(1) + "\" added (empty)");
            changed = true;
        }
        for (Object o : (List<Object>) shape.get("e")) {
            List<Object> en = (List<Object>) o;
            int eid = ((Number) en.get(0)).intValue();
            if (me.containsKey(eid)) continue;
            child(content, "Events").sub("Event", "Name", (String) en.get(1), "Id", String.valueOf(eid));
            rep.add("repaired", rel + ": event " + eid + " \"" + en.get(1) + "\" added");
            changed = true;
        }
        ModPortXml.El animsEl = root.find("Animations");
        if (animsEl != null && !ma.isEmpty()) {
            ModPortXml.El source = ma.get(animsEl.get("DefaultAnimation"));
            if (source == null) source = ma.values().iterator().next();
            for (Object o : (List<Object>) shape.get("a")) {
                String name = (String) o;
                if (ma.containsKey(name)) continue;
                ModPortXml.El clone = source.deepCopy();
                clone.set("Name", name);
                animsEl.kids.add(clone);
                rep.add("repaired", rel + ": animation \"" + name + "\" was missing, cloned from \"" + source.get("Name") + "\"");
                changed = true;
            }
        }
        return changed;
    }

    static ModPortXml.El child(ModPortXml.El parent, String tag) {
        ModPortXml.El e = parent.find(tag);
        return e != null ? e : parent.sub(tag);
    }

    static Map<Integer, ModPortXml.El> byId(ModPortXml.El root, String path) {
        Map<Integer, ModPortXml.El> m = new LinkedHashMap<>();
        ModPortXml.El c = root.find("Content");
        if (c == null) return m;
        for (ModPortXml.El e : c.findall(path)) m.put(Integer.parseInt(e.get("Id").trim()), e);
        return m;
    }

    static LinkedHashMap<String, ModPortXml.El> anims(ModPortXml.El root) {
        LinkedHashMap<String, ModPortXml.El> m = new LinkedHashMap<>();
        for (ModPortXml.El a : root.findall("Animations/Animation")) m.put(a.get("Name"), a);
        return m;
    }

    /* modport.py scale_anm2_for: frames on sheets scaled down by k */
    static void scaleFor(ModPortXml.El root, Set<Integer> ids, Report rep, String rel, int k) {
        for (ModPortXml.El a : root.findall("Animations/Animation"))
            for (ModPortXml.El la : a.findall("LayerAnimations/LayerAnimation")) {
                String lid = la.get("LayerId");
                ModPortXml.El layer = null;
                for (ModPortXml.El l : root.findall("Content/Layers/Layer")) if (lid != null && lid.equals(l.get("Id"))) layer = l;
                if (layer == null || !ids.contains(intOf(layer.get("SpritesheetId"), -1))) continue;
                for (ModPortXml.El f : la.findall("Frame")) {
                    for (String key : new String[] {"XCrop", "YCrop", "Width", "Height", "XPivot", "YPivot"})
                        if (f.get(key) != null) f.set(key, Long.toString(pyRound(fl(f.get(key)) / (double) k)));
                    for (String key : new String[] {"XScale", "YScale"})
                        if (f.get(key) != null) f.set(key, pyFloat(fl(f.get(key)) * (double) k));
                }
            }
        rep.add("rescaled", rel + ": frames on sheets scaled down by " + k + " rescaled");
    }

    /* modport.py fit_anm2_up: crops and pivots times k, scale over k */
    static void fitUp(ModPortXml.El root, Set<Integer> ids, int k) {
        Map<String, Integer> ls = layerSheets(root);
        for (ModPortXml.El a : root.findall("Animations/Animation"))
            for (ModPortXml.El la : a.findall("LayerAnimations/LayerAnimation")) {
                Integer sid = ls.get(la.get("LayerId"));
                if (sid == null || !ids.contains(sid)) continue;
                for (ModPortXml.El f : la.findall("Frame")) {
                    for (String key : new String[] {"XCrop", "YCrop", "Width", "Height", "XPivot", "YPivot"})
                        if (f.get(key) != null) f.set(key, pyNum(fl(f.get(key)) * k));
                    for (String key : new String[] {"XScale", "YScale"})
                        f.set(key, pyNum(fl(f.get(key, "100")) / k));
                }
            }
    }

    /* modport.py game_scale_frames */
    static Boolean gameScaleFrames(ModPortXml.El root, String rel, String sheet, int[] gsize, int k) {
        Set<Integer> ids = idsOf(root, rel, sheet);
        if (ids.isEmpty()) return null;
        Map<String, Integer> ls = layerSheets(root);
        List<Double> scales = new ArrayList<>();
        for (ModPortXml.El a : root.findall("Animations/Animation"))
            for (ModPortXml.El la : a.findall("LayerAnimations/LayerAnimation")) {
                Integer sid = ls.get(la.get("LayerId"));
                if (sid == null || !ids.contains(sid)) continue;
                for (ModPortXml.El f : la.findall("Frame")) {
                    double w = fl(f.get("Width", "0")), h = fl(f.get("Height", "0"));
                    if (w <= 0 || h <= 0) continue;
                    if (fl(f.get("XCrop", "0")) + w > gsize[0] + 8 || fl(f.get("YCrop", "0")) + h > gsize[1] + 8) return false;
                    scales.add(Math.sqrt(Math.abs(fl(f.get("XScale", "100")) * fl(f.get("YScale", "100")))));
                }
            }
        if (scales.isEmpty()) return false;
        Collections.sort(scales);
        return scales.get(scales.size() / 2) > 100.0 / Math.sqrt(k);
    }

    static int wholeK(int[] size, int[] gsize) {
        if (size == null || gsize == null || size[0] % gsize[0] != 0 || size[1] % gsize[1] != 0) return 0;
        int k = size[0] / gsize[0];
        return k >= 2 && size[1] / gsize[1] == k ? k : 0;
    }

    static List<String> fntPages(byte[] d) {
        List<String> pages = new ArrayList<>();
        if (d.length >= 3 && d[0] == 'B' && d[1] == 'M' && d[2] == 'F') {
            int p = 4;
            while (p + 5 <= d.length) {
                int kind = d[p] & 0xff;
                long size = (d[p + 1] & 0xffL) | ((d[p + 2] & 0xffL) << 8) | ((d[p + 3] & 0xffL) << 16) | ((d[p + 4] & 0xffL) << 24);
                int end = (int) Math.min(d.length, p + 5 + size);
                if (kind == 3) {
                    pages.clear();
                    for (String x : new String(d, p + 5, end - p - 5, StandardCharsets.ISO_8859_1).split("\0")) if (!x.isEmpty()) pages.add(x);
                }
                p += 5 + (int) size;
            }
            return pages;
        }
        Matcher m = Pattern.compile("page\\s+id=\\d+\\s+file=\"([^\"]+)\"").matcher(new String(d, StandardCharsets.ISO_8859_1));
        while (m.find()) pages.add(m.group(1));
        return pages;
    }

    /* modport.py parse_xml: strict, then the game's way with a report line; null when neither */
    static final class Parsed {
        ModPortXml.El root;
        byte[] data;
    }

    static Parsed parseXml(byte[] data, String rel, Report rep) {
        Parsed out = new Parsed();
        try {
            out.root = ModPortXml.parse(data);
            out.data = data;
            return out;
        } catch (ModPortXml.Bad ignored) { /* the game's way below */ }
        ModPortXml.Fixed f = ModPortXml.lenient(data);
        try {
            out.root = ModPortXml.parse(f.data);
        } catch (ModPortXml.Bad e) {
            return null;
        }
        out.data = f.data;
        rep.add("repaired", rel + ": " + (f.fixes.isEmpty() ? "parse quirks" : String.join(", ", f.fixes))
                + " - the game's parser forgives these, a strict one does not; written escaped");
        return out;
    }

    /* ------------------------------------------------------------------ tables */

    static int countEntries(ModPortXml.El r, Spec spec) {
        int n = 0;
        for (ModPortXml.El e : r.iter()) if (spec.tags.contains(e.tag)) n++;
        return n;
    }

    /* strtol(v, NULL, 10) as the port's check reads a number (modport.py c_long) */
    static long cLong(String v, long def) {
        if (v == null) return def;
        int i = 0, n = v.length();
        while (i < n && " \t\n\u000b\f\r".indexOf(v.charAt(i)) >= 0) i++;
        int start = i;
        if (i < n && (v.charAt(i) == '+' || v.charAt(i) == '-')) i++;
        int digits = i;
        while (i < n && v.charAt(i) >= '0' && v.charAt(i) <= '9') i++;
        if (i == digits) return 0;
        try { return Long.parseLong(v.substring(start, i)); } catch (NumberFormatException e) { return 0; }
    }

    /* a record's key in a whole table, as usermods.c rec_key (modport.py rec_key) */
    static String recKey(ModPortXml.El e) {
        String cls = e.tag.equals("active") || e.tag.equals("passive") || e.tag.equals("familiar") ? "item" : e.tag;
        if (cls.equals("entity"))
            return "entity[" + cLong(e.get("id"), -1) + "." + cLong(e.get("variant"), 0) + "." + cLong(e.get("subtype"), 0) + "]";
        for (String a : new String[] {"id", "Id", "ID", "name", "Name"})
            if (e.get(a) != null) return cls + "[" + e.get(a) + "]";
        return "";
    }

    /* the game's records the mod's table lacks, appended to its root (usermods.c guard_table; modport.py
     * complete_table): the number added */
    static int completeTable(ModPortXml.El m, ModPortXml.El g) {
        if (!m.tag.equals(g.tag)) return 0;
        Set<String> have = new HashSet<>();
        for (ModPortXml.El e : m.kids) have.add(recKey(e));
        List<ModPortXml.El> add = new ArrayList<>();
        for (ModPortXml.El e : g.kids) {
            String k = recKey(e);
            if (!k.isEmpty() && !have.contains(k)) add.add(e);
        }
        for (ModPortXml.El e : add) m.kids.add(e.deepCopy());
        return add.size();
    }

    static void skewReport(Report rep, String n, byte[] data, String game) {
        ModPortXml.El m, g;
        try {
            m = ModPortXml.parse(ModPortXml.lenient(data).data);
            g = ModPortXml.parse(ModPortXml.lenient(game.getBytes(StandardCharsets.UTF_8)).data);
        } catch (ModPortXml.Bad e) {
            return;
        }
        LinkedHashMap<String, ModPortXml.El> gk = keyed(g), mk = keyed(m);
        List<String[]> rows = new ArrayList<>();
        List<List<String>> whats = new ArrayList<>();
        for (Map.Entry<String, ModPortXml.El> en : gk.entrySet()) {
            ModPortXml.El ge = en.getValue(), me = mk.get(en.getKey());
            List<String> what = new ArrayList<>();
            if (me == null) { what.add("gone"); rows.add(new String[] {ge.tag, ge.get("id")}); whats.add(what); continue; }
            for (int i = 0; i < ge.keys.size(); i++) {
                String a = ge.keys.get(i), v = ge.vals.get(i);
                if (TEXT_ATTRS.contains(a)) continue;
                if (me.get(a) == null) what.add(a + " " + v + " -> -");
                else if (!me.get(a).equals(v)) what.add(a + " " + v + " -> " + me.get(a));
            }
            for (int i = 0; i < me.keys.size(); i++) {
                String a = me.keys.get(i);
                if (!TEXT_ATTRS.contains(a) && ge.get(a) == null) what.add(a + " - -> " + me.vals.get(i));
            }
            if (ge.kids.size() != me.kids.size()) what.add(ge.kids.size() + " child entries -> " + me.kids.size());
            if (!what.isEmpty()) { rows.add(new String[] {ge.tag, ge.get("id")}); whats.add(what); }
        }
        for (Map.Entry<String, ModPortXml.El> en : mk.entrySet()) {
            if (gk.containsKey(en.getKey())) continue;
            List<String> what = new ArrayList<>();
            what.add("new");
            rows.add(new String[] {en.getValue().tag, en.getValue().get("id")});
            whats.add(what);
        }
        if (rows.isEmpty()) return;
        StringBuilder sample = new StringBuilder();
        for (int i = 0; i < Math.min(4, rows.size()); i++) {
            if (i > 0) sample.append("; ");
            sample.append(rows.get(i)[0]).append(' ').append(rows.get(i)[1]).append(": ")
                  .append(String.join(", ", whats.get(i).subList(0, Math.min(3, whats.get(i).size()))));
        }
        rep.add("warning", n + ": " + rows.size() + " entries differ from the game's beyond their text (" + sample
                + (rows.size() > 4 ? "; ..." : "") + ") - the whole table replaces the game's: if it was copied from another version of the game, that version's values come too");
    }

    static LinkedHashMap<String, ModPortXml.El> keyed(ModPortXml.El r) {
        LinkedHashMap<String, ModPortXml.El> out = new LinkedHashMap<>();
        for (ModPortXml.El e : r.iter()) {
            String id = e.get("id");
            if (id == null) continue;
            String k = e.tag + "\u0001" + id;
            if (!out.containsKey(k)) out.put(k, e);
        }
        return out;
    }

    /* ------------------------------------------------------------------ the mod's files */

    /* one file of the result */
    static final class Item {
        String raw;                 /* the source zip's entry it comes from, or null */
        byte[] data;                /* its bytes when the converter made them (anm2, xml...) */
        int[] size;                 /* a picture's size as the mod ships it */
        long length = -1, crc = -1; /* the entry's, from the first reading */
        final List<int[]> ops = new ArrayList<>();      /* {0, m} nearest up; {1, k, w, h} box down */
        Item copyOf;                /* the same bytes as another (the iOS copy of a picture) */

        int[] finalSize() {
            if (data != null) return ModPortPng.size(data);
            if (copyOf != null) return copyOf.finalSize();
            int[] s = size;
            for (int[] op : ops) s = op[0] == 0 ? new int[] {s[0] * op[1], s[1] * op[1]} : new int[] {op[2], op[3]};
            return s;
        }
        boolean transformed() { return !ops.isEmpty(); }
    }

    /* phase 0: the first reading, done = entries read of `expected`; phase 1: the writing, files of the result */
    interface Progress { void at(int phase, long done, long total, String what); }

    static final class Outcome {
        boolean empty;
        int files;
        String report = "";
        String id = "mod", name = "", version = "", author = "", description = "";
        String manifestJson = "";
        String counts = "";         /* the report's head on one line: "rescaled 33, repaired 67, ..." */
        boolean script;             /* the mod carries a script (.lua) */
    }

    /* a stream counting what was read (the READ-like part of adapting) */
    static final class Counting extends java.io.FilterInputStream {
        long n;
        Counting(InputStream in) { super(in); }
        @Override public int read() throws IOException { int c = super.read(); if (c >= 0) n++; return c; }
        @Override public int read(byte[] b, int o, int l) throws IOException { int k = super.read(b, o, l); if (k > 0) n += k; return k; }
    }

    static byte[] readAll(InputStream in, long hint) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream(hint > 0 && hint < (1 << 28) ? (int) hint : 1 << 14);
        byte[] buf = new byte[1 << 16];
        for (int n; (n = in.read(buf)) > 0; ) b.write(buf, 0, n);
        return b.toByteArray();
    }

    static boolean resourceTop(String low) {
        String top = low.indexOf('/') >= 0 ? low.substring(0, low.indexOf('/')) : low;
        return top.equals("resources") || top.startsWith("resources-dlc3") || top.startsWith("resources.");
    }

    /*
     * modport.py main(), step for step. `out` gets the zip the installer then takes (mod.json first); when
     * nothing in the mod is for the port the outcome is `empty` and no zip is written.
     */
    @SuppressWarnings("unchecked")
    static Outcome convert(UserMods.Source src, Ref ref, File out, Progress progress) throws IOException {
        return convert(src, ref, out, progress, -1, null);
    }

    /*
     * `expected`: the zip's entries, when the caller has counted them (the installer's READ) - for the
     * progress of the first reading. `label`: the installer's {language: line} put before the mod's
     * description in the mod.json written (the adapted mod's page says so); null writes modport.py's card.
     */
    static Outcome convert(UserMods.Source src, Ref ref, File out, Progress progress, int expected,
                           Map<String, String> label) throws IOException {
        long total = src.size();
        int seen = 0;
        /* 1. the first reading: names, sizes, CRCs, the text files and the heads of pictures and rooms */
        List<String> names = new ArrayList<>();                         /* every entry, directories too */
        Map<String, byte[]> body = new HashMap<>();                     /* raw name -> bytes held */
        Map<String, long[]> sums = new HashMap<>();                     /* raw name -> length, crc */
        try (Counting raw = new Counting(src.open());
             ZipInputStream z = new ZipInputStream(new BufferedInputStream(raw, 1 << 16))) {
            byte[] buf = new byte[1 << 16];
            for (ZipEntry e; (e = z.getNextEntry()) != null; ) {
                String n = e.getName().replace('\\', '/');
                names.add(n);
                if (e.isDirectory()) continue;
                String low = n.toLowerCase(Locale.ROOT);
                boolean whole = low.endsWith(".anm2") || low.endsWith(".xml") || low.endsWith(".fnt") || low.endsWith(".json");
                int head = low.endsWith(".png") ? 33 : low.endsWith(".stb") ? 8 : 0;
                CRC32 crc = new CRC32();
                ByteArrayOutputStream keep = whole || head > 0 ? new ByteArrayOutputStream(whole ? 1 << 14 : head) : null;
                long len = 0;
                for (int k; (k = z.read(buf)) > 0; ) {
                    crc.update(buf, 0, k);
                    if (keep != null) {
                        if (whole) keep.write(buf, 0, k);
                        else if (keep.size() < head) keep.write(buf, 0, (int) Math.min(k, head - keep.size()));
                    }
                    len += k;
                }
                seen++;
                if (progress != null) progress.at(0, seen, expected, n);
                if (keep != null) body.put(n, keep.toByteArray());
                sums.put(n, new long[] {len, crc.getValue()});
            }
        }
        /* the folder everything sits in, if one (modport.py load_mod) */
        String prefix = "";
        Set<String> tops = new java.util.TreeSet<>();
        boolean cardAtRoot = false;
        for (String n : names) {
            if (n.indexOf('/') >= 0) tops.add(n.substring(0, n.indexOf('/')));
            if (n.equals("mod.json") || n.equals("metadata.xml")) cardAtRoot = true;
        }
        if (tops.size() == 1 && !cardAtRoot) prefix = tops.iterator().next() + "/";
        LinkedHashMap<String, String> files = new LinkedHashMap<>();    /* the mod's path -> raw name */
        for (String n : names) {
            if (n.endsWith("/") || !n.startsWith(prefix)) continue;
            String p = n.substring(prefix.length());
            if (!files.containsKey(p)) files.put(p, n);
            else files.put(p, n);
        }
        Report rep = new Report();
        Map<String, String> meta = new HashMap<>();
        if (files.containsKey("metadata.xml")) {
            byte[] d = body.get(files.get("metadata.xml"));
            ModPortXml.El x = null;
            try { x = ModPortXml.parse(d); } catch (ModPortXml.Bad e) {
                try { x = ModPortXml.parse(ModPortXml.lenient(d).data); } catch (ModPortXml.Bad e2) { rep.add("note", "metadata.xml does not parse"); }
            }
            if (x != null) for (String k : new String[] {"name", "directory", "id", "version", "description"}) {
                ModPortXml.El e = x.find(k);
                if (e != null && e.text != null && !e.text.isEmpty()) meta.put(k, pyStrip(e.text));
            }
        }
        if (files.containsKey("mod.json")) {
            try {
                Object j = new Json(new String(body.get(files.get("mod.json")), StandardCharsets.UTF_8)).value();
                if (j instanceof Map) for (Map.Entry<String, Object> e : ((Map<String, Object>) j).entrySet())
                    if (e.getValue() instanceof String) meta.put(e.getKey(), (String) e.getValue());
                Object id = j instanceof Map ? ((Map<String, Object>) j).get("id") : null;
                if (id != null) meta.put("_json_id", String.valueOf(id));
            } catch (RuntimeException | IOException e) {
                rep.add("note", "mod.json does not parse");
            }
        }
        boolean lua = false;
        for (String n : files.keySet()) if (n.toLowerCase(Locale.ROOT).endsWith("main.lua")) lua = true;
        if (lua) rep.add("dropped", "main.lua: the port runs no scripts; whatever the script did is not carried");
        /* what lies outside resources (and its sets) and content/ */
        Map<String, List<String>> outside = new HashMap<>();
        for (String n : files.keySet()) {
            String low = n.toLowerCase(Locale.ROOT);
            String top = low.indexOf('/') >= 0 ? low.substring(0, low.indexOf('/')) : "";
            if (top.equals("resources") || top.startsWith("resources-dlc3") || top.startsWith("resources.") || top.equals("content")) continue;
            if (low.equals("mod.json") || low.equals("metadata.xml") || low.equals("main.lua")) continue;
            List<String> l = outside.get(top);
            if (l == null) outside.put(top, l = new ArrayList<>());
            l.add(n);
        }
        List<String> outTops = new ArrayList<>(outside.keySet());
        Collections.sort(outTops);
        for (String top : outTops) {
            List<String> ns = outside.get(top);
            if (OTHER_GAME_SETS.containsKey(top)) {
                rep.add("dropped", top + "/ (" + ns.size() + " files): " + OTHER_GAME_SETS.get(top) + " - not taken");
            } else if (!top.isEmpty()) {
                boolean allLua = true;
                for (String x : ns) if (!x.toLowerCase(Locale.ROOT).endsWith(".lua")) allLua = false;
                rep.add("dropped", top + "/ (" + ns.size() + " files): not read by the port" + (allLua ? " (scripts)" : ""));
            } else {
                List<String> sorted = new ArrayList<>(ns);
                Collections.sort(sorted);
                rep.add("dropped", ns.size() + " files beside the card (" + String.join(", ", sorted.subList(0, Math.min(4, sorted.size())))
                        + (ns.size() > 4 ? ", ..." : "") + "): not read by the port");
            }
        }
        /* content/ */
        LinkedHashMap<String, byte[]> contentKept = new LinkedHashMap<>();
        Set<Integer> gameTypes = new HashSet<>();
        String gent = ref.xml.get("resources/entities2.xml");
        if (gent != null) {
            try {
                for (ModPortXml.El e : ModPortXml.parse(gent).iter())
                    if (e.tag.equals("entity") && isDigits(stripMinus(e.get("id", "")))) gameTypes.add(toInt(e.get("id")));
            } catch (ModPortXml.Bad ignored) { /* the game's own: never */ }
        }
        List<String> sortedFiles = new ArrayList<>(files.keySet());
        Collections.sort(sortedFiles);
        for (String n : sortedFiles) {
            String low = n.toLowerCase(Locale.ROOT);
            if (!low.startsWith("content/") || low.endsWith("/")) continue;
            if (!low.equals("content/entities2.xml")) {
                String why = CONTENT_WHY.get(basename(low));
                rep.add("dropped", n + ": the port takes only content/entities2.xml; " + (why != null ? why : "the port merges no other content"));
                continue;
            }
            Parsed px = parseXml(body.get(files.get(n)), n, rep);
            if (px == null) { rep.add("dropped", n + ": does not parse"); continue; }
            int ents = 0;
            java.util.TreeSet<Integer> newTypes = new java.util.TreeSet<>();
            for (ModPortXml.El e : px.root.iter()) {
                if (!e.tag.equals("entity")) continue;
                ents++;
                String id = e.get("id", "");
                if (isDigits(stripMinus(id)) && !gameTypes.contains(toInt(id))) newTypes.add(toInt(id));
            }
            rep.add("kept", n + ": " + ents + " entities for the port to merge into the game's");
            if (!newTypes.isEmpty() && !gameTypes.isEmpty()) {
                List<String> t = new ArrayList<>();
                for (Integer i : newTypes) { if (t.size() >= 12) break; t.add(String.valueOf(i)); }
                rep.add("warning", n + ": types " + String.join(", ", t) + " are not the game's (a script's creatures) - the port leaves them out");
            }
            contentKept.put("content/entities2.xml", px.data);
        }
        /* the resource files */
        LinkedHashMap<String, Item> taken = new LinkedHashMap<>();
        List<String> refusedAnm2 = new ArrayList<>();     /* the mod's .anm2 the port would not use: their sheets judged below */
        LinkedHashMap<String, byte[]> anm2Data = new LinkedHashMap<>();     /* the anm2 as they stand now */
        Map<String, List<String>> sheetsOfMod = new HashMap<>();
        for (String n : sortedFiles) {
            String low = n.toLowerCase(Locale.ROOT);
            if (!resourceTop(low)) continue;
            String ext = low.indexOf('.') >= 0 ? low.substring(low.lastIndexOf('.') + 1) : "";   /* as modport.py: on the whole path */
            if (!KEEP_EXT.contains(ext)) { rep.add("dropped", n + ": not a format the port loads"); continue; }
            String inner = low.indexOf('/') >= 0 ? low.substring(low.indexOf('/') + 1) : "";
            String rawName = files.get(n);
            byte[] data = body.get(rawName);
            Item it = new Item();
            it.raw = rawName;
            long[] sm = sums.get(rawName);
            it.length = sm[0];
            it.crc = sm[1];
            if (ext.equals("png")) it.size = ModPortPng.size(data);
            if (ext.equals("anm2") || ext.equals("xml") || ext.equals("fnt")) it.data = data;
            taken.put(n, it);
            if (ext.equals("anm2")) {
                Parsed px = parseXml(data, n, rep);
                if (px == null) { rep.add("dropped", n + ": the anm2 does not parse"); taken.remove(n); refusedAnm2.add(n); continue; }
                it.data = px.data;
                anm2Data.put(n, px.data);
                if (BY_NUMBER_FILES.contains(inner))
                    rep.add("kept", n + BY_NUMBER_KEPT + (inner.equals("gfx/ui/completion_widget.anm2") ? WIDGET_KEPT : ""));
                for (String sheet : sheetsOf(px.root, n).values()) addTo(sheetsOfMod, sheet, n);
            } else if (ext.equals("fnt")) {
                for (String page : fntPages(data)) addTo(sheetsOfMod, joinNorm(dirname(n), page), n);
            }
        }
        /* anm2 against the game's */
        List<String> unplayed = new ArrayList<>();
        Map<String, ModPortXml.El> roots = new HashMap<>();             /* parsed on demand, dropped after use */
        for (String n : new ArrayList<>(anm2Data.keySet())) {
            Map<String, Object> shape = ref.anm2.get(norm(n));
            if (shape == null) { rep.add("kept", n + ": new to the game (a mod's own animation)"); continue; }
            ModPortXml.El root = root(anm2Data, n);
            Set<String> ga = new HashSet<>();
            for (Object a : (List<Object>) shape.get("a")) ga.add((String) a);
            List<String> xa = new ArrayList<>();
            for (String a : anims(root).keySet()) if (!ga.contains(a)) xa.add(a);
            if (!xa.isEmpty())
                unplayed.add(basename(n) + " (" + String.join(", ", xa.subList(0, Math.min(3, xa.size()))) + (xa.size() > 3 ? ", ..." : "") + ")");
            if (repair(root, shape, n, rep)) {
                byte[] b = ModPortXml.tostring(root);
                anm2Data.put(n, b);
                taken.get(n).data = b;
            }
        }
        if (!unplayed.isEmpty())
            rep.add("note", unplayed.size() + " anm2 have animations the game's files have not - never played here (made for another version of the game, or for a script): "
                    + String.join("; ", unplayed.subList(0, Math.min(6, unplayed.size()))) + (unplayed.size() > 6 ? "; ..." : ""));
        /* the port's check at start, run here (usermods.c guard_file, guard_port_patched): an .anm2 past the engine
         * loader's limits, and a menu file the port's rows do not fit whole, are not used by the port - left out */
        {
            List<String> anmNames = new ArrayList<>(anm2Data.keySet());
            Collections.sort(anmNames);
            for (String n : anmNames) {
                String why = anmLimits(root(anm2Data, n));
                if (why != null) {
                    rep.add("dropped", n + ": " + why + " - the port would not use it (the game's file stands in): left out");
                    taken.remove(n); anm2Data.remove(n); refusedAnm2.add(n);
                }
            }
            Map<String, String> have = new HashMap<>();
            for (String n : anm2Data.keySet()) have.put(n.toLowerCase(Locale.ROOT), n);
            Map<String, String> refusedWhy = new HashMap<>();
            for (Map<String, Object> t : ref.portpatch)
                for (String n : targetCandidates(t, have)) {
                    if (refusedWhy.containsKey(n)) continue;
                    String why = menuFit(root(anm2Data, n), t);
                    if (why == null) break;
                    refusedWhy.put(n, why);
                }
            anmNames = new ArrayList<>(anm2Data.keySet());
            Collections.sort(anmNames);
            for (String n : anmNames) {
                String low = n.toLowerCase(Locale.ROOT);
                if (!PORT_PATCHED.contains(low.indexOf('/') >= 0 ? low.substring(low.indexOf('/') + 1) : low)) continue;
                if (refusedWhy.containsKey(n)) {
                    rep.add("dropped", String.format(Locale.ROOT, MENU_REFUSED, n, refusedWhy.get(n)));
                    taken.remove(n); anm2Data.remove(n); refusedAnm2.add(n);
                } else rep.add("kept", n + MENU_KEPT);
            }
            for (String sheet : new ArrayList<>(sheetsOfMod.keySet())) {
                List<String> keep = new ArrayList<>();
                for (String a : sheetsOfMod.get(sheet)) if (taken.containsKey(a)) keep.add(a);
                if (keep.isEmpty()) sheetsOfMod.remove(sheet); else sheetsOfMod.put(sheet, keep);
            }
        }
        /* pictures: size against the game's, and the GL limit */
        /* the files read by number the mod brings its own copy of for all sets: the sheets they cut are its layout */
        Set<String> ownByNumber = new HashSet<>();
        for (String a : anm2Data.keySet()) {
            String al = a.toLowerCase(Locale.ROOT);
            int cut = al.indexOf('/');
            if (cut > 0 && ALL_SETS.contains(al.substring(0, cut)) && BY_NUMBER_FILES.contains(al.substring(cut + 1)))
                ownByNumber.add(al.substring(cut + 1));
        }
        /* the menus the mod brings its own copy of for all sets (always kept): the port's check decides on them */
        Set<String> ownMenu = new HashSet<>();
        for (String a : anm2Data.keySet()) {
            String al = a.toLowerCase(Locale.ROOT);
            int cut = al.indexOf('/');
            if (cut > 0 && ALL_SETS.contains(al.substring(0, cut)) && MENU_SIZED.contains(al.substring(cut + 1)))
                ownMenu.add(al.substring(cut + 1));
        }
        LinkedHashMap<String, Integer> shrunk = new LinkedHashMap<>();
        Map<String, int[]> modSizes = new HashMap<>();
        for (Map.Entry<String, Item> e : taken.entrySet())
            if (e.getKey().toLowerCase(Locale.ROOT).endsWith(".png") && e.getValue().size != null) modSizes.put(norm(e.getKey()), e.getValue().size);
        /* the sheets of an .anm2 the port would not use are cut by the game's .anm2 in its place (usermods.c
         * guard_orphan_sheets; modport.py): the game's size or a whole k (2..16) are fine, any other size is left out
         * unless another .anm2 of the mod that is used names it */
        Map<String, String> orphans = new HashMap<>();
        {
            Map<String, byte[]> keptAnm2 = new LinkedHashMap<>();
            for (Map.Entry<String, Item> e : taken.entrySet())
                if (e.getKey().toLowerCase(Locale.ROOT).endsWith(".anm2")) keptAnm2.put(e.getKey(), e.getValue().data);
            List<String> takenSorted = new ArrayList<>(taken.keySet());
            Collections.sort(takenSorted);
            for (String a : refusedAnm2) {
                Map<String, Object> shape = ref.anm2.get(norm(a));
                if (shape == null || shape.get("s") == null) continue;
                for (Object spo : (List<Object>) shape.get("s")) {
                    String rel = joinNorm(dirname(a), (String) spo);
                    String rl = rel.indexOf('/') >= 0 ? rel.substring(rel.indexOf('/') + 1) : rel;
                    for (String pn : takenSorted) {
                        String np = norm(pn), ip = np.indexOf('/') >= 0 ? np.substring(np.indexOf('/') + 1) : np;
                        if (!pn.toLowerCase(Locale.ROOT).endsWith(".png") || !ip.equals(rl) || orphans.containsKey(np)) continue;
                        int[] ms = taken.get(pn).size, gs = ref.png.get(np);
                        if (ms == null || gs == null || (ms[0] == gs[0] && ms[1] == gs[1])) continue;
                        int k = gs[0] != 0 && gs[1] != 0 && ms[0] % gs[0] == 0 && ms[1] % gs[1] == 0 ? ms[0] / gs[0] : 0;
                        if (k >= 2 && k <= KFIT_K_MAX && ms[1] / gs[1] == k) continue;
                        if (sheetWantedByOwn(rl, keptAnm2)) continue;
                        orphans.put(np, String.format(Locale.ROOT, ORPHAN_REFUSED, pn, basename(a), ms[0], ms[1], gs[0], gs[1]));
                    }
                }
            }
        }
        for (String n : new ArrayList<>(taken.keySet())) {
            if (!n.toLowerCase(Locale.ROOT).endsWith(".png")) continue;
            Item it = taken.get(n);
            if (orphans.containsKey(norm(n))) { rep.add("dropped", orphans.get(norm(n))); taken.remove(n); continue; }
            if (it.size == null) { rep.add("dropped", n + ": not a readable png"); taken.remove(n); continue; }
            int[] size = it.size;
            String base = variantBase(n);
            String baseNorm = base != null ? norm(base) : null;
            int[] gsize = ref.png.get(norm(n));
            if (gsize != null) {
                int[] gbase = baseNorm != null ? ref.png.get(baseNorm) : null;
                boolean twin = baseNorm != null && !ref.cut.contains(norm(n)) && gbase != null && gbase[0] == gsize[0] && gbase[1] == gsize[1];
                /* a picture of a file read by number the mod brings its own copy of (and its twin) is cut by the
                 * mod's frames: not the port's layout (hotfix 1.71) */
                boolean portLike = (ref.portcut.contains(norm(n)) && !byNumberFree(n, ref, ownByNumber))
                        || (twin && ref.portcut.contains(baseNorm) && !byNumberFree(baseNorm, ref, ownByNumber));
                boolean cutLike = ref.cut.contains(norm(n)) || (twin && ref.cut.contains(baseNorm));
                boolean differs = gsize[0] != size[0] || gsize[1] != size[1];
                boolean odd = false;     /* kept at a size the port's layout does not fit */
                if (differs && portLike) {
                    int k = size[0] % gsize[0] == 0 && size[1] % gsize[1] == 0 && size[0] / gsize[0] == size[1] / gsize[1] ? size[0] / gsize[0] : 0;
                    if (k > 1 && ref.portcode.contains(norm(n)) && folderScaled(n, modSizes, ref, k)) {
                        rep.add("warning", String.format(Locale.ROOT, "%s: %dx%d, the game's %dx%d times %d like the rest of its folder; kept for the game, but the port's own drawing of this picture (a touch button or a built-in mod) will show it %d times too big", n, size[0], size[1], gsize[0], gsize[1], k, k));
                    } else if (k > 1) {
                        it.ops.add(new int[] {1, k, gsize[0], gsize[1]});
                        shrunk.put(norm(n), k);
                        rep.add("rescaled", String.format(Locale.ROOT, "%s: %dx%d is the game's %dx%d times %d; the port draws it at the game's layout - baked down to %dx%d (area mean of each %dx%d block)", n, size[0], size[1], gsize[0], gsize[1], k, gsize[0], gsize[1], k, k));
                    } else {
                        List<List<String>> hn = heldBy(n, ref.bynumber, ownByNumber), hm = heldBy(n, ref.bymenu, ownMenu);
                        if (!hn.get(0).isEmpty() || !hm.get(0).isEmpty()) {
                            /* the game's frames cut it here; the port's check serves it as it is (05.10.2026), so it is kept, said louder */
                            List<String> held = new ArrayList<>(hn.get(0));
                            held.addAll(hm.get(0));
                            rep.add("warning", String.format(Locale.ROOT, "%s: %dx%d is not a multiple of the game's %dx%d, and the game's frames cut it here (the mod brings no %s of its own) - kept, as the game takes it; those frames will not fit it: redraw it at the game's size, or bring the anm2 cut for it", n, size[0], size[1], gsize[0], gsize[1], leafs(held)));
                            odd = true;
                        } else if (!hm.get(1).isEmpty()) {
                            rep.add("kept", String.format(Locale.ROOT, "%s: %dx%d, cut by the mod's own %s - the mod's layout (if the port's check gives that file up, the game's picture stands in for this one)", n, size[0], size[1], leafs(hm.get(1))));
                            odd = true;
                        } else {
                            rep.add("warning", String.format(Locale.ROOT, "%s: %dx%d is not a multiple of the game's %dx%d - kept, as the game takes it; the port draws it at the game's layout, so its crops will not fit it: redraw it at the game's size", n, size[0], size[1], gsize[0], gsize[1]));
                            odd = true;
                        }
                    }
                    if (!odd) continue;
                }
                int[] baseMod = baseNorm != null ? modSizes.get(baseNorm) : null;
                if (odd) {
                    /* judged above */
                } else if (twin && !sheetsOfMod.containsKey(norm(n)) && sheetsOfMod.containsKey(baseNorm) && baseMod != null
                        && (baseMod[0] != size[0] || baseMod[1] != size[1])) {
                    int bw = baseMod[0], bh = baseMod[1];
                    boolean up = bw % size[0] == 0 && bh % size[1] == 0 && bw / size[0] == bh / size[1];
                    boolean down = size[0] % bw == 0 && size[1] % bh == 0 && size[0] / bw == size[1] / bh;
                    if (up || down) {
                        if (up) it.ops.add(new int[] {0, bw / size[0]});
                        else it.ops.add(new int[] {1, size[0] / bw, bw, bh});
                        rep.add("rescaled", String.format(Locale.ROOT, "%s: %dx%d made %dx%d, the size of the mod's %s - the game swaps this sheet in by code under the mod's anm2 for it", n, size[0], size[1], bw, bh, basename(base)));
                        size = new int[] {bw, bh};
                    } else {
                        rep.add("warning", String.format(Locale.ROOT, "%s: %dx%d while the mod's %s is %dx%d - the game swaps this sheet in by code under that anm2, and the crops will not fit", n, size[0], size[1], basename(base), bw, bh));
                    }
                } else if (differs && !sheetsOfMod.containsKey(norm(n))
                        /* a twin at the size of its base the mod's own anm2 cuts is that anm2's layout too: nothing to say */
                        && !(twin && sheetsOfMod.containsKey(baseNorm) && baseMod != null && baseMod[0] == size[0] && baseMod[1] == size[1])) {
                    /* a shorter one is kept, as the game and 1.666 take it (the owner, 05.10.2026: never stricter than the port) */
                    if (((size[0] == gsize[0] && size[1] < gsize[1]) || (size[1] == gsize[1] && size[0] < gsize[0])) && cutLike)
                        rep.add("warning", String.format(Locale.ROOT, "%s: %dx%d is a shorter sheet than the game's %dx%d (an older game's picture?) - kept, as the game takes it; where the game's crops reach past it the picture shows wrong: redo it from the export", n, size[0], size[1], gsize[0], gsize[1]));
                    else
                        rep.add("note", String.format(Locale.ROOT, "%s: %dx%d where the game has %dx%d (no anm2 or fnt of the mod cuts it)", n, size[0], size[1], gsize[0], gsize[1]));
                }
            }
            if (Math.max(size[0], size[1]) > MAX_TEX) {
                it.ops.add(new int[] {1, 2, Math.max(1, size[0] / 2), Math.max(1, size[1] / 2)});
                shrunk.put(norm(n), 2);
                rep.add("rescaled", String.format(Locale.ROOT, "%s: %dx%d is over %d, halved", n, size[0], size[1], MAX_TEX));
            }
        }
        Map<String, String> tk = new HashMap<>();
        for (String t : taken.keySet()) tk.put(norm(t), t);
        for (Map.Entry<String, Integer> sh : shrunk.entrySet()) {
            String sheet = sh.getKey();
            int k = sh.getValue();
            int[] gsize = ref.png.get(sheet);
            List<String> cutters = sheetsOfMod.get(sheet);
            if (cutters == null) continue;
            for (String anm : cutters) {
                if (!anm2Data.containsKey(anm) || !taken.containsKey(anm)) continue;
                ModPortXml.El root = root(anm2Data, anm);
                Item pic = tk.get(sheet) != null ? taken.get(tk.get(sheet)) : null;
                int[] fs = pic != null ? pic.finalSize() : null;
                if (gsize != null && fs != null && gsize[0] == fs[0] && gsize[1] == fs[1] && Boolean.TRUE.equals(gameScaleFrames(root, anm, sheet, gsize, k))) {
                    rep.add("kept", anm + ": cuts " + basename(sheet) + " with the game's crops - fits the picture baked down to the game's size as it is");
                    continue;
                }
                scaleFor(root, idsOf(root, anm, sheet), rep, anm, k);
                byte[] b = ModPortXml.tostring(root);
                anm2Data.put(anm, b);
                taken.get(anm).data = b;
            }
        }
        /* R0: a mod anm2 that cuts a picture the mod ships k times the game's size with the game's crops */
        for (String anm : new ArrayList<>(anm2Data.keySet())) {
            if (!taken.containsKey(anm)) continue;
            ModPortXml.El root = root(anm2Data, anm);
            List<String> fitted = new ArrayList<>();
            List<Map.Entry<Integer, String>> sheets = new ArrayList<>(sheetsOf(root, anm).entrySet());
            sheets.sort((a, b) -> a.getKey() != b.getKey().intValue() ? Integer.compare(a.getKey(), b.getKey()) : a.getValue().compareTo(b.getValue()));
            for (Map.Entry<Integer, String> e : sheets) {
                String sheet = e.getValue();
                String keyName = tk.get(sheet);
                int[] gsize = ref.png.get(sheet);
                int k = keyName != null && taken.containsKey(keyName) ? wholeK(taken.get(keyName).finalSize(), gsize) : 0;
                if (k > 0 && !shrunk.containsKey(norm(sheet)) && Boolean.TRUE.equals(gameScaleFrames(root, anm, sheet, gsize, k))) {
                    fitUp(root, idsOf(root, anm, sheet), k);
                    fitted.add(basename(sheet) + " x" + k);
                }
            }
            if (!fitted.isEmpty()) {
                byte[] b = ModPortXml.tostring(root);
                anm2Data.put(anm, b);
                taken.get(anm).data = b;
                rep.add("repaired", anm + ": its frames cut " + String.join(", ", fitted) + " with the game's crops - the mod redrew the picture bigger and not this file (the PC shows the top-left part too); fitted: crops times k, scale over k");
            }
        }
        /* R1: the iOS-only widgets of the game, fitted to the mod's pictures */
        for (String rel : IOS_ONLY_ANM2) {
            if (tk.containsKey(rel)) continue;
            String g = ref.iosonly.get(rel);
            if (g == null) continue;
            ModPortXml.El groot;
            try { groot = ModPortXml.parse(g); } catch (ModPortXml.Bad e) { continue; }
            List<String> fitted = new ArrayList<>();
            List<Map.Entry<Integer, String>> sheets = new ArrayList<>(sheetsOf(groot, rel).entrySet());
            sheets.sort((a, b) -> a.getKey() != b.getKey().intValue() ? Integer.compare(a.getKey(), b.getKey()) : a.getValue().compareTo(b.getValue()));
            for (Map.Entry<Integer, String> e : sheets) {
                String sheet = e.getValue();
                String keyName = tk.get(sheet);
                int k = keyName != null && taken.containsKey(keyName) ? wholeK(taken.get(keyName).finalSize(), ref.png.get(sheet)) : 0;
                if (k > 0) {
                    fitUp(groot, idsOf(groot, rel, sheet), k);
                    fitted.add(basename(sheet) + " x" + k);
                }
            }
            if (!fitted.isEmpty()) {
                Item it = new Item();
                it.data = ModPortXml.tostring(groot);
                taken.put(rel, it);
                rep.add("repaired", rel + ": the iOS game's own widget (no PC mod brings it) cuts " + String.join(", ", fitted) + " of the mod - the game's file put in, its frames fitted to the mod's picture");
            }
        }
        /* the iOS game's own copy of a sheet by code (titlemenu_ios.png) */
        for (String n : new ArrayList<>(taken.keySet())) {
            if (!n.toLowerCase(Locale.ROOT).endsWith(".png")) continue;
            String ios = n.substring(0, n.length() - 4) + "_ios.png";
            int[] gi = ref.png.get(norm(ios)), gb = ref.png.get(norm(n));
            boolean have = false;
            for (String t : taken.keySet()) if (norm(t).equals(norm(ios))) have = true;
            if (gi != null && !ref.cut.contains(norm(ios)) && gb != null && gi[0] == gb[0] && gi[1] == gb[1] && !have) {
                Item it = new Item();
                it.copyOf = taken.get(n);
                taken.put(ios, it);
                rep.add("repaired", ios + ": the game asks for this iOS copy of " + basename(n) + " by code; the mod's picture put under that name too");
            }
        }
        /* xml */
        for (String n : new ArrayList<>(taken.keySet())) {
            String low = n.toLowerCase(Locale.ROOT);
            if (!low.endsWith(".xml")) continue;
            Item it = taken.get(n);
            String nn = norm(n);
            if (!ref.xmls.contains(nn)) { rep.add("kept", n + ": new to the game"); continue; }
            Spec spec = TEXT_XML.get(basename(low));
            String game = ref.xmlText(nn);
            if (spec == null) {
                rep.add("kept", n + ": replaces the game's whole");
                if (game != null) skewReport(rep, n, it.data, game);
                continue;
            }
            Parsed px = parseXml(it.data, n, rep);
            if (px == null) { rep.add("dropped", n + ": does not parse"); taken.remove(n); continue; }
            it.data = px.data;
            ModPortXml.El groot;
            try { groot = ModPortXml.parse(game); } catch (ModPortXml.Bad e) { continue; }
            int mc = countEntries(px.root, spec), gc = countEntries(groot, spec);
            /* completed as the port's check completes a table: the mod's records whole, the game's it lacks added */
            int added = completeTable(px.root, groot);
            if (added > 0) {
                it.data = ModPortXml.tostring(px.root);
                rep.add("merged", String.format(Locale.ROOT, TABLE_COMPLETED, n, added));
            }
            if (mc > gc) {
                rep.add("warning", n + ": " + mc + " entries where the game has " + gc + " - new content, kept whole");
                skewReport(rep, n, it.data, game);
            } else if (added == 0) {
                rep.add("kept", mc == gc ? n + ": " + mc + " entries, as the game"
                        : n + ": " + mc + " entries where the game has " + gc + ", none of the game's missing by its key - kept whole");
            }
        }
        /* rooms */
        for (String n : new ArrayList<>(taken.keySet())) {
            if (!n.toLowerCase(Locale.ROOT).endsWith(".stb")) continue;
            String g = ref.stb.get(norm(n));
            if (g == null) { rep.add("kept", n + ": new room file"); continue; }
            byte[] head = body.get(taken.get(n).raw);
            if (head == null) head = new byte[0];
            /* the magic only: the room count after it is the mod's own (05.10.2026) */
            String gmagic = g.substring(0, Math.min(8, g.length()));
            if (!hex(head, 4).equals(gmagic)) {
                rep.add("dropped", String.format(Locale.ROOT, ROOM_REFUSED, n, pyBytes(Arrays.copyOf(head, Math.min(4, head.length))), pyBytes(unhex(gmagic))));
                taken.remove(n);
            }
        }
        /* the port's built-ins' own files */
        List<String> same = new ArrayList<>(), other = new ArrayList<>();
        List<String> sortedTaken = new ArrayList<>(taken.keySet());
        Collections.sort(sortedTaken);
        for (String n : sortedTaken) {
            long[] mine = ref.portown.get(norm(n));
            if (mine == null) continue;
            Item it = taken.get(n);
            long len, crc;
            if (it.data != null) { CRC32 c = new CRC32(); c.update(it.data); len = it.data.length; crc = c.getValue(); }
            else if (it.copyOf == null && !it.transformed()) { len = it.length; crc = it.crc; }
            else { len = -1; crc = -1; }
            (len == mine[0] && crc == mine[1] ? same : other).add(n);
        }
        if (!same.isEmpty()) {
            List<String> leaves = new ArrayList<>();
            for (int i = 0; i < Math.min(3, same.size()); i++) leaves.add(basename(same.get(i)));
            rep.add("note", same.size() + " files are the port's built-in's own, byte for byte (" + String.join(", ", leaves) + (same.size() > 3 ? ", ..." : "") + ") - the mod's copies change nothing");
        }
        for (String n : other)
            rep.add("warning", n + ": replaces the port's built-in's own file (a different one) - the built-in will draw the mod's; the PC version of a mod the port has built in is not needed");
        /* the manifest */
        Outcome o = new Outcome();
        o.script = anyLua(files.keySet());
        {
            List<String> kinds = new ArrayList<>(rep.counts.keySet());
            Collections.sort(kinds);
            StringBuilder c = new StringBuilder();
            for (String k : kinds) c.append(c.length() > 0 ? ", " : "").append(k).append(' ').append(rep.counts.get(k));
            o.counts = c.toString();
        }
        String mid = "";
        for (String cand : new String[] {meta.get("_json_id"), meta.get("directory"), meta.get("name"), meta.get("id")}) {
            mid = idWord(cand == null ? "" : cand);
            if (!mid.isEmpty()) break;
        }
        if (mid.isEmpty()) mid = "mod";
        o.id = mid;
        if (taken.isEmpty() && contentKept.isEmpty()) {
            rep.add("refused", "nothing here for the port: " + (anyLua(files.keySet()) ? "a script mod; the port runs no scripts" : "no resources/ the port reads")
                    + " - no zip written, the installer would refuse it (\"has no file the port reads\")");
            o.empty = true;
            o.report = rep.text();
            o.name = meta.containsKey("name") ? meta.get("name") : mid;
            return o;
        }
        o.name = meta.containsKey("name") ? meta.get("name") : mid;
        o.version = meta.containsKey("version") ? meta.get("version") : "1.0";
        o.author = meta.containsKey("author") ? meta.get("author") : "Unknown";
        o.description = meta.containsKey("description") ? meta.get("description") : "";
        o.manifestJson = "{\n  \"id\": " + pyJsonString(o.id) + ",\n  \"name\": " + pyJsonString(o.name) + ",\n  \"version\": " + pyJsonString(o.version)
                + ",\n  \"author\": " + pyJsonString(o.author) + ",\n  \"description\": " + pyJsonString(o.description) + "\n}";
        o.report = rep.text();
        o.files = taken.size();
        String written = o.manifestJson;
        if (label != null && !label.isEmpty()) {
            StringBuilder d = new StringBuilder("{");
            boolean first = true;
            for (Map.Entry<String, String> e : label.entrySet()) {
                d.append(first ? "\n    " : ",\n    ").append(pyJsonString(e.getKey())).append(": ")
                 .append(pyJsonString(e.getValue() + (o.description.isEmpty() ? "" : "\n\n" + o.description)));
                first = false;
            }
            d.append("\n  }");
            written = o.manifestJson.replace("\"description\": " + pyJsonString(o.description), "\"description\": " + d);
        }
        /* 2. the second reading: the result written */
        write(src, files, taken, contentKept, written, out, progress);
        return o;
    }

    static boolean anyLua(Iterable<String> names) {
        for (String n : names) if (n.toLowerCase(Locale.ROOT).endsWith(".lua")) return true;
        return false;
    }

    static byte[] unhex(String h) {
        byte[] b = new byte[h.length() / 2];
        for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(h.substring(i * 2, i * 2 + 2), 16);
        return b;
    }

    static boolean isDigits(String s) {
        if (s.isEmpty()) return false;
        for (int i = 0; i < s.length(); i++) if (!Character.isDigit(s.charAt(i))) return false;
        return true;
    }

    static int toInt(String s) {
        try { return Integer.parseInt(s); } catch (NumberFormatException e) { return Integer.MIN_VALUE; }
    }

    static String stripMinus(String s) {
        int i = 0;
        while (i < s.length() && s.charAt(i) == '-') i++;
        return s.substring(i);
    }

    static void addTo(Map<String, List<String>> m, String k, String v) {
        List<String> l = m.get(k);
        if (l == null) m.put(k, l = new ArrayList<>());
        l.add(v);
    }

    static ModPortXml.El root(Map<String, byte[]> anm2Data, String n) {
        try {
            return ModPortXml.parse(anm2Data.get(n));
        } catch (ModPortXml.Bad e) {
            throw new IllegalStateException(n + " parsed once and not again: " + e.getMessage());
        }
    }

    static boolean folderScaled(String n, Map<String, int[]> modSizes, Ref ref, int k) {
        String me = norm(n), folder = me.substring(0, me.lastIndexOf('/') + 1);
        int atK = 0, at1 = 0;
        for (Map.Entry<String, int[]> e : modSizes.entrySet()) {
            String o = e.getKey();
            if (!o.startsWith(folder) || o.substring(folder.length()).indexOf('/') >= 0 || o.equals(me)) continue;
            int[] g = ref.png.get(o), s = e.getValue();
            if (g == null) continue;
            if (s[0] == g[0] * k && s[1] == g[1] * k) atK++;
            else if (s[0] == g[0] && s[1] == g[1]) at1++;
        }
        return atK >= 3 && atK > at1;
    }

    /* ------------------------------------------------------------------ the second reading: the zip written */

    static final Set<String> STORED = set("png", "ogg", "ogv", "wav");

    static void write(UserMods.Source src, LinkedHashMap<String, String> files, LinkedHashMap<String, Item> taken,
                      LinkedHashMap<String, byte[]> contentKept, String manifest, File out, Progress progress) throws IOException {
        Map<String, List<String>> byRaw = new HashMap<>();            /* raw entry -> the result's names it makes */
        Map<Item, List<String>> aliases = new HashMap<>();
        for (Map.Entry<String, Item> e : taken.entrySet()) {
            Item it = e.getValue();
            if (it.copyOf != null) {
                List<String> l = aliases.get(it.copyOf);
                if (l == null) aliases.put(it.copyOf, l = new ArrayList<>());
                l.add(e.getKey());
            }
        }
        for (Map.Entry<String, Item> e : taken.entrySet()) {
            Item it = e.getValue();
            if (it.data == null && it.copyOf == null && it.raw != null) addTo(byRaw, it.raw, e.getKey());
        }
        long done = 0, all = taken.size() + contentKept.size() + 1;
        try (ZipOutputStream z = new ZipOutputStream(new java.io.BufferedOutputStream(new FileOutputStream(out), 1 << 16))) {
            put(z, "mod.json", manifest.getBytes(StandardCharsets.UTF_8), false);
            done++;
            for (Map.Entry<String, byte[]> e : contentKept.entrySet()) { put(z, e.getKey(), e.getValue(), false); done++; }
            for (Map.Entry<String, Item> e : taken.entrySet()) {
                Item it = e.getValue();
                if (it.data == null) continue;
                put(z, e.getKey(), it.data, STORED.contains(ext(e.getKey().toLowerCase(Locale.ROOT))));
                List<String> al = aliases.get(it);
                if (al != null) for (String a : al) put(z, a, it.data, true);
                done++;
                if (progress != null) progress.at(1, done, all, e.getKey());
            }
            if (!byRaw.isEmpty()) {
                try (ZipInputStream zin = new ZipInputStream(new BufferedInputStream(src.open(), 1 << 16))) {
                    for (ZipEntry e; (e = zin.getNextEntry()) != null; ) {
                        String rawName = e.getName().replace('\\', '/');
                        List<String> made = byRaw.remove(rawName);
                        if (made == null) continue;
                        for (String n : made) {
                            Item it = taken.get(n);
                            List<String> al = aliases.get(it);
                            boolean stored = STORED.contains(ext(n.toLowerCase(Locale.ROOT)));
                            if (it.transformed()) {
                                byte[] png = readAll(zin, it.length);
                                ModPortPng.Source s = ModPortPng.decoder(png);
                                for (int[] op : it.ops)
                                    s = op[0] == 0 ? ModPortPng.nearestUp(s, op[1]) : ModPortPng.boxDown(s, op[1], op[2], op[3]);
                                png = null;
                                byte[] made2 = ModPortPng.bytes(s);
                                put(z, n, made2, stored);
                                if (al != null) for (String a : al) put(z, a, made2, true);
                            } else if (al != null || made.size() > 1) {
                                byte[] b = readAll(zin, it.length);
                                put(z, n, b, stored);
                                if (al != null) for (String a : al) put(z, a, b, true);
                            } else {
                                ZipEntry o = new ZipEntry(n);
                                if (stored) {
                                    o.setMethod(ZipEntry.STORED);
                                    o.setSize(it.length);
                                    o.setCompressedSize(it.length);
                                    o.setCrc(it.crc);
                                }
                                z.putNextEntry(o);
                                byte[] buf = new byte[1 << 16];
                                for (int k; (k = zin.read(buf)) > 0; ) z.write(buf, 0, k);
                                z.closeEntry();
                            }
                            done++;
                            if (progress != null) progress.at(1, done, all, n);
                            break;      /* one entry makes one file (the same raw name twice: the later one's, as modport.py) */
                        }
                        if (byRaw.isEmpty()) break;
                    }
                }
            }
            if (!byRaw.isEmpty()) throw new IOException("the mod changed while it was read: " + byRaw.keySet().iterator().next());
        }
        if (progress != null) progress.at(1, all, all, null);       /* the step ends at done == total */
    }

    static void put(ZipOutputStream z, String name, byte[] data, boolean stored) throws IOException {
        ZipEntry e = new ZipEntry(name);
        if (stored) {
            CRC32 c = new CRC32();
            c.update(data);
            e.setMethod(ZipEntry.STORED);
            e.setSize(data.length);
            e.setCompressedSize(data.length);
            e.setCrc(c.getValue());
        }
        z.putNextEntry(e);
        z.write(data);
        z.closeEntry();
    }
}
