package ru.isaac.port;

import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/*
 * ADDING A USER MOD (the owner, 25.09.2026): a zip in the port's own format,
 * checked and unpacked into <config>/mods/<folder>/ (usermods.c reads it from
 * there at the next start).
 *
 * What the port reads (the one table of a mod's top, TOP below):
 *
 *   mod.json                    {"id", "name", "version", "author", "description",
 *                               "settings"}; name and description may be
 *                               {language: text} (plan 30.09, 4.10.1)
 *   metadata.xml                a PC mod's card: with no mod.json it gives the
 *                               name, version and description, and its
 *                               `directory`, made to the id's rules, the id; a
 *                               mod.json is written from it (plan 30.09, M6)
 *   preview.png                 optional
 *   resources/...               the PC mod's own layout
 *   resources-dlc3/...          Repentance's layer
 *   resources-dlc3.<lang>/...   a language's own files
 *   content/entities2.xml       new enemy variants, merged by usermods.c
 *   patch/<table>.xml           changes to a game table (plan 30.09, M3)
 *   options/<key>/<value>/...   a setting's layer: the mod's own layout again
 *                               (resources*, content, patch), read when the
 *                               setting has that value (mod-settings scheme
 *                               02.10.2026); every <key> and <value> must be
 *                               declared in mod.json's "settings"
 *
 * The zip may hold these at its top or inside one folder (a mod folder zipped
 * as it is). THREE CLASSES (plan 30.09, M6; the owner's decision 23):
 *   - what the port reads, as above;
 *   - DEAD WEIGHT: everything else - a script (.lua: the iOS engine runs no
 *     Lua), the rest of content/, readmes, sources, kinds of file the game
 *     does not read. It goes into the mod's folder as it is and the result
 *     window lists it as not used by the port; a .lua adds the line that the
 *     script does not run. Only system junk is not unpacked: __MACOSX/,
 *     .DS_Store, Thumbs.db, .git/ (and the port's own marks .adding/.remove,
 *     which would take the mod down at the next start). The port's folder has
 *     its .nomedia, so none of it reaches a gallery;
 *   - REFUSAL, of the whole mod, only for what cannot be a mod or would break:
 *     no mod.json and no metadata.xml to make one from, a mod.json that does
 *     not read as JSON, no name, a bad id, a path out of the mod's folder, an
 *     options/ folder mod.json does not declare (the variant would never come
 *     on), more than a gigabyte, nothing the port reads at all.
 *
 * ONE JUDGE OF mod.json (02.10.2026): json.c, the reader the game uses at
 * start, through modcheck_jni.c. LENIENT since hotfix 1.71 (the owner,
 * 05.10.2026: "leniently - settings and the rest are an optional part"): a
 * slip of a hand - a trailing comma, a comment, single quotes, a bare word, a
 * repeated key, text after the closing brace - is read as 1.666's org.json
 * read it, and only noted; a setting json.c finds broken is switched off in
 * the game with its note, and the mod is taken. Only a mod.json that cannot be
 * read even so (not an object, larger than 64 KB) refuses the mod - 1.666
 * refused those too. org.json then reads the fields from the strict text json.c
 * made of it (strictModJson), so both sides read the same; this side matches
 * the options/ folders to the settings, which json.c cannot see.
 *
 * A mod that is taken goes first in the list, switched on (putInPlace: every
 * step with its reason). The list itself, mods.txt, is written by usermods.c
 * alone - this side asks it (listPut).
 *
 * THE FOLDER IS THE GAME'S TO NAME (the owner, 02.10.2026). A mod's folder and
 * its line in mods.txt are a number the installer issues, "<NNN>-<id>"; the
 * author's id is only a label and a key - two different mods with the same id
 * (test, mod, a copy of the sample) used to replace each other silently. An
 * UPDATE is the same author's id, name and author: then the player is asked,
 * before anything is unpacked, whether to update it or to put the new one
 * beside it; anything else is a new mod beside the others, with no question.
 * Mods installed before keep their folders. Since hotfix 1.73 an update, too,
 * gets a new number: the game is not restarted after ADD MOD any more, and it
 * reads the old copy's folder until it is (installAs, usermods.c list_put).
 *
 * NO RESTART AFTER ADD MOD, AND SEVERAL ARCHIVES AT ONCE (hotfix 1.73): see
 * A BATCH below.
 *
 * THE WORK IS SHOWN AS IT GOES (plan 02.10, B1): READ, CHECK, UNPACK, PLACE,
 * LIST, with counts, at most ten reports a second (Pacer). The core reads from
 * a Source, not a Uri, so the whole installer runs on the desk without Android.
 */
final class UserMods {
    private UserMods() { }

    static final class Result {
        boolean ok;
        boolean updated;
        boolean adapted;        /* a PC mod adapted by the converter at install (ModPort) */
        String folder;          /* the folder the mod went into */
        String text;
        String name;            /* the mod's name as the MODS page shows it, once the installer knows it */
        String label;           /* the archive's own name, for a batch's summary (installAll) */
    }

    /* the steps of an add, in their order; the two ADAPT steps only for a mod not in the port's format (ModPort) */
    static final int READ = 1, ADAPT_READ = 2, ADAPT = 3, CHECK = 4, UNPACK = 5, PLACE = 6, LIST = 7;

    /*
     * THE CONVERTER AT INSTALL (the owner, 04.10.2026): a mod with no mod.json - a PC mod with its
     * metadata.xml, or bare resources/ - is adapted once, here, by tools/modport.py's rules (ModPort.java,
     * the same rules line for line: tools/umdesk/test_converters.py), and the result is installed. Mods
     * in the port's format are taken as they are. The game reference is the APK's
     * assets/modport_gameref.bin (MainActivity names it here; the desk names a file); without it the
     * mod is taken unadapted, as before.
     */
    interface RefSource { InputStream open() throws IOException; }
    static volatile RefSource gameRef;
    static final String ADAPTED_REPORT = "modport-report.txt";      /* the converter's report, in the mod's folder */
    static final String GAMEREF_ASSET = "modport_gameref.bin";       /* tools/build.py: tools/modport_ref.py --out assets/... */

    /*
     * where the work is: done of total - bytes of the archive for READ (total -1
     * when the provider does not say), files after it - and the unpacked bytes
     * beside the files (0 of 0 where they mean nothing)
     */
    interface Progress { void at(int step, long done, long total, long bytes, long bytesTotal, String what); }

    /* the same mod is installed already: true to update it, false to put the new one beside it */
    interface Asker { boolean update(String question); }

    /* the zip, opened as often as the installer reads it */
    interface Source {
        InputStream open() throws IOException;
        long size();
    }

    private static final String[] KINDS = {
        "png", "anm2", "xml", "wav", "ogg", "fnt", "stb", "sta", "ogv", "srt"
    };
    private static final long MAX_BYTES = 1024L * 1024L * 1024L;   /* a gigabyte unpacked is plenty */
    static final String ADDING = ".adding";                        /* usermods.c ADDING_MARK: not a mod yet */

    /*
     * The port's own windows speak the phone's language: Russian on a Russian
     * phone, English everywhere else (the owner, 25.09.2026). The game never
     * mentions the modding guide: it is a thing outside the game (the owner,
     * 28.09.2026).
     */
    static String t(String ru, String en) {
        return "ru".equals(Locale.getDefault().getLanguage()) ? ru : en;
    }

    /* the add window's own words, in all seven of the port's languages (02.10.2026) */
    static String tr(String en, String ru, String de, String es, String ja, String ko, String zh) {
        switch (Locale.getDefault().getLanguage()) {
            case "ru": return ru;
            case "de": return de;
            case "es": return es;
            case "ja": return ja;
            case "ko": return ko;
            case "zh": return zh;
            default: return en;
        }
    }

    static String stepName(int step, String what) {
        switch (step) {
            case READ: return tr("Reading the archive", "Читаю архив", "Archiv wird gelesen", "Leyendo el archivo",
                                 "アーカイブを読み込み中", "압축 파일 읽는 중", "正在读取压缩包");
            case ADAPT_READ: return tr("Adapting the PC mod for the port: reading", "Адаптирую ПК-мод для порта: читаю",
                                       "PC-Mod wird für den Port angepasst: lesen", "Adaptando el mod de PC al port: leyendo",
                                       "PC用MODを移植版向けに変換中：読み込み", "PC 모드를 포트용으로 변환 중: 읽기", "正在将PC模组转换为移植版：读取");
            case ADAPT: return tr("Adapting the PC mod for the port: writing", "Адаптирую ПК-мод для порта: пишу",
                                  "PC-Mod wird für den Port angepasst: schreiben", "Adaptando el mod de PC al port: escribiendo",
                                  "PC用MODを移植版向けに変換中：書き込み", "PC 모드를 포트용으로 변환 중: 쓰기", "正在将PC模组转换为移植版：写入");
            case CHECK: return tr("Checking the mod", "Проверяю мод", "Mod wird geprüft", "Comprobando el mod",
                                  "MODを確認中", "모드 확인 중", "正在检查模组");
            case UNPACK: return tr("Unpacking", "Распаковываю", "Wird entpackt", "Descomprimiendo",
                                   "展開中", "압축 해제 중", "正在解压");
            case PLACE: {
                String s = tr("Putting it into the mods folder", "Кладу в папку модов", "Wird in den Mod-Ordner gelegt",
                              "Colocándolo en la carpeta de mods", "MODフォルダーへ配置中", "모드 폴더에 넣는 중", "正在放入模组文件夹");
                if ("move-file".equals(what) || "copy".equals(what))
                    s += tr(" (file by file)", " (по файлам)", " (Datei für Datei)", " (archivo por archivo)",
                            "（ファイルごと）", " (파일별로)", "（逐个文件）");
                return s;
            }
            case LIST: return tr("Writing the mod list", "Записываю список модов", "Modliste wird geschrieben",
                                 "Escribiendo la lista de mods", "MODリストを書き込み中", "모드 목록 기록 중", "正在写入模组列表");
            default: return "";
        }
    }

    /* "1234 of 5333 files, 87 of 333 MB" in the phone's language; bytes alone for READ */
    static String numbers(int step, long done, long total, long bytes, long bytesTotal) {
        long mb = 1 << 20;
        if (step == READ) {
            if (total < 0) return (done / mb) + tr(" MB", " МБ", " MB", " MB", " MB", " MB", " MB");
            return String.format(Locale.ROOT, tr("%d of %d MB", "%d из %d МБ", "%d von %d MB", "%d de %d MB",
                    "%d / %d MB", "%d / %d MB", "%d / %d MB"), done / mb, (total + mb - 1) / mb);
        }
        if (total <= 0) return "";
        String files = String.format(Locale.ROOT, tr("%d of %d files", "%d из %d файлов", "%d von %d Dateien",
                "%d de %d archivos", "%d / %d ファイル", "%d / %d 파일", "%d / %d 个文件"), done, total);
        if (bytesTotal <= 0) return files;
        return files + String.format(Locale.ROOT, tr(", %d of %d MB", ", %d из %d МБ", ", %d von %d MB", ", %d de %d MB",
                "、%d / %d MB", ", %d / %d MB", "，%d / %d MB"), bytes / mb, (bytesTotal + mb - 1) / mb);
    }

    static String windowTitle() {
        return tr("Adding a mod", "Добавляю мод", "Mod wird hinzugefügt", "Añadiendo un mod",
                  "MODを追加中", "모드 추가 중", "正在添加模组");
    }

    static String keepOpenHint() {
        return tr("Do not minimise the game while the mod is being added.", "Не сворачивайте игру, пока мод ставится.",
                  "Bitte das Spiel nicht minimieren, solange der Mod hinzugefügt wird.",
                  "No minimices el juego mientras se añade el mod.", "MODの追加中はゲームを最小化しないでください。",
                  "모드를 추가하는 동안 게임을 최소화하지 마세요.", "添加模组时请勿将游戏切到后台。");
    }

    static String resultTitle(Result r) {
        if (r == null || !r.ok)
            return tr("Mod not added", "Мод не добавлен", "Mod nicht hinzugefügt", "Mod no añadido",
                      "MODは追加されませんでした", "모드를 추가하지 못했습니다", "模组未添加");
        if (r.adapted)
            return tr("Adapted automatically - may work unstably", "Адаптирован автоматически — может работать нестабильно",
                      "Automatisch angepasst - kann instabil laufen", "Adaptado automáticamente: puede funcionar de forma inestable",
                      "自動で変換しました（動作が不安定な場合があります）", "자동으로 변환됨 - 불안정하게 작동할 수 있음", "已自动转换——可能运行不稳定");
        if (r.updated)
            return tr("Mod updated", "Мод обновлён", "Mod aktualisiert", "Mod actualizado",
                      "MODを更新しました", "모드를 업데이트했습니다", "模组已更新");
        return tr("Mod added", "Мод добавлен", "Mod hinzugefügt", "Mod añadido",
                  "MODを追加しました", "모드를 추가했습니다", "模组已添加");
    }

    static String updateWord() {
        return tr("Update", "Обновить", "Aktualisieren", "Actualizar", "更新", "업데이트", "更新");
    }

    static String besideWord() {
        return tr("Beside it", "Поставить рядом", "Daneben", "Al lado", "並べて入れる", "옆에 설치", "并排安装");
    }

    /* ------------------------------------------------ a batch's words (hotfix 1.73), all seven of the port's languages */

    /* the window's title while mod k (from 1) of n goes in */
    static String batchTitle(int k, int n) {
        return String.format(Locale.ROOT, tr("Adding mods: %d of %d", "Добавляю моды: %d из %d", "Mods werden hinzugefügt: %d von %d",
                "Añadiendo mods: %d de %d", "MODを追加中：%d / %d", "모드 추가 중: %d / %d", "正在添加模组：%d / %d"), k, n);
    }

    static String keepOpenHintMany() {
        return tr("Do not minimise the game while the mods are being added.", "Не сворачивайте игру, пока моды ставятся.",
                  "Bitte das Spiel nicht minimieren, solange die Mods hinzugefügt werden.",
                  "No minimices el juego mientras se añaden los mods.", "MODの追加中はゲームを最小化しないでください。",
                  "모드를 추가하는 동안 게임을 최소화하지 마세요.", "添加模组时请勿将游戏切到后台。");
    }

    /* the summary's title: how many of the batch went in (added or updated) */
    static String summaryTitle(int ok, int n) {
        return String.format(Locale.ROOT, tr("Mods added: %d of %d", "Модов добавлено: %d из %d", "Mods hinzugefügt: %d von %d",
                "Mods añadidos: %d de %d", "追加したMOD：%d / %d", "추가된 모드: %d / %d", "已添加模组：%d / %d"), ok, n);
    }

    /* "Name (file.zip) - added" for the summary; a mod refused names its archive alone when it never got as far as a name */
    static String statusLine(Result r) {
        String who = r.name != null && !r.name.isEmpty() ? r.name + (r.label != null ? " (" + r.label + ")" : "")
                   : r.label != null ? r.label : "?";
        String what;
        if (r.ok) {
            what = r.updated ? tr("updated", "обновлён", "aktualisiert", "actualizado", "更新しました", "업데이트됨", "已更新")
                             : tr("added", "добавлен", "hinzugefügt", "añadido", "追加しました", "추가됨", "已添加");
            if (r.adapted)
                what += tr(" (adapted from a PC mod)", " (адаптирован из мода для ПК)", " (aus einem PC-Mod angepasst)",
                           " (adaptado de un mod de PC)", "（PC用MODから変換）", " (PC 모드에서 변환)", "（由PC模组转换）");
        } else {
            what = tr("not added", "не добавлен", "nicht hinzugefügt", "no añadido", "追加できませんでした", "추가하지 못함", "未添加");
            String why = r.text == null ? "" : r.text.trim();
            int nl = why.indexOf('\n');
            if (nl > 0) why = why.substring(0, nl).trim();
            if (!why.isEmpty()) what += ": " + why;
        }
        return who + " - " + what;
    }

    /* the end of every window that put a mod in: nothing works before the restart, and where the restart is */
    /* the add window's own restart button (hotfix 1.74): the MODS page's row, word for word */
    static String restartWord() {
        return tr("RESTART TO APPLY", "ПЕРЕЗАПУСТИТЬ ДЛЯ ПРИМЕНЕНИЯ", "NEU STARTEN ZUM ANWENDEN", "REINICIAR PARA APLICAR",
                  "さいきどうしててきよう", "재시작해서 적용", "重启以生效");
    }

    static String restartHint() {
        return tr("Nothing changes in the game until it restarts. When you have added everything, tap RESTART TO APPLY "
                        + "at the bottom of the MODS list.",
                  "До перезапуска в игре ничего не изменится. Когда добавите всё, нажмите ПЕРЕЗАПУСТИТЬ ДЛЯ ПРИМЕНЕНИЯ "
                        + "внизу списка МОДЫ.",
                  "Bis zum Neustart ändert sich im Spiel nichts. Wenn alles hinzugefügt ist: unten in der MODS-Liste "
                        + "NEU STARTEN ZUM ANWENDEN antippen.",
                  "Nada cambia en el juego hasta reiniciarlo. Cuando lo hayas añadido todo, toca REINICIAR PARA APLICAR "
                        + "al final de la lista MODS.",
                  "再起動するまでゲームには反映されません。すべて追加したら、MODリストの一番下の「さいきどうしててきよう」を"
                        + "タップしてください。",
                  "재시작하기 전에는 게임에 아무것도 바뀌지 않습니다. 모두 추가했으면 모드 목록 맨 아래의 '재시작해서 적용'을 "
                        + "탭하세요.",
                  "重启之前游戏里不会有任何变化。全部添加完后，点击模组列表最下方的“重启以生效”。");
    }

    /* the update question's tick, shown while more mods of the batch are to come */
    static String sameForAllWord() {
        return tr("Same answer for the other mods in this batch", "Тот же ответ для остальных модов",
                  "Gleiche Antwort für die übrigen Mods", "La misma respuesta para los demás mods",
                  "残りのMODにも同じ答えを使う", "나머지 모드에도 같은 답 적용", "其余模组使用相同的选择");
    }

    /* the room left on the storage of a folder: the phone's own answer; the desk puts in a full phone (hotfix 1.73) */
    static volatile java.util.function.ToLongFunction<File> freeSpace = File::getUsableSpace;

    /* the room a mod takes unpacked: its bytes, a 4 KB block's slack a file, and 16 MB to spare */
    static long roomFor(long bytes, int files) {
        return bytes + 4096L * Math.max(0, files) + (16L << 20);
    }

    static String noRoom(long need, long free) {
        return String.format(Locale.ROOT, tr("Not enough room on the phone: the mod needs about %d MB, %d MB are free.",
                "Не хватает места на телефоне: мод займёт около %d МБ, свободно %d МБ.",
                "Nicht genug Speicher auf dem Telefon: Der Mod braucht etwa %d MB, frei sind %d MB.",
                "No hay espacio suficiente en el teléfono: el mod necesita unos %d MB y hay %d MB libres.",
                "端末の空き容量が足りません：MODに約%d MB必要ですが、空きは%d MBです。",
                "휴대폰 저장 공간이 부족합니다: 모드에 약 %d MB가 필요한데 %d MB만 비어 있습니다.",
                "手机空间不足：该模组需要约%d MB，可用%d MB。"), (need + (1 << 20) - 1) >> 20, free >> 20);
    }

    /* the owner's question (02.10.2026): "Mod X vA is already installed. Update to vB or install it alongside as a separate one?" */
    static String question(String name, String was, String now) {
        String a = was.isEmpty() ? "" : " v" + was, b = now.isEmpty() ? "" : " v" + now;
        return tr("Mod “" + name + "”" + a + " is installed already. Update it" + (b.isEmpty() ? "" : " to" + b)
                        + ", or put this one beside it as a separate mod?",
                  "Мод «" + name + "»" + a + " уже стоит. Обновить" + (b.isEmpty() ? "" : " до" + b)
                        + " или поставить рядом как отдельный?",
                  "Mod „" + name + "“" + a + " ist bereits installiert. " + (b.isEmpty() ? "Aktualisieren" : "Auf" + b + " aktualisieren")
                        + " oder als eigenen Mod daneben installieren?",
                  "El mod «" + name + "»" + a + " ya está instalado. ¿Actualizarlo" + (b.isEmpty() ? "" : " a" + b)
                        + " o instalarlo al lado como uno aparte?",
                  "MOD「" + name + "」" + a + " はすでに入っています。" + (b.isEmpty() ? "" : b.trim() + " に") + "更新しますか、それとも別のMODとして並べて入れますか？",
                  "모드 \"" + name + "\"" + a + "이(가) 이미 설치되어 있습니다. " + (b.isEmpty() ? "" : b.trim() + "(으)로 ")
                        + "업데이트할까요, 아니면 별도의 모드로 옆에 설치할까요?",
                  "模组“" + name + "”" + a + " 已经安装。" + (b.isEmpty() ? "更新" : "更新到" + b) + "，还是作为单独的模组并排安装？");
    }

    /* a mod back into the port's format, as <outDir>/<id>.zip: its folder's files at the zip's top,
       the deletion mark left out (EXPORT on the mod's page, MainActivity.exportMod) */
    static File pack(File outDir, File configDir, String id) throws IOException {
        File dir = new File(modsDir(configDir), id);
        if (!new File(dir, "mod.json").isFile()) throw new IOException("no mod.json");
        File out = outDir;
        out.mkdirs();
        File zip = new File(out, id + ".zip"), tmp = new File(out, id + ".zip.tmp");
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(new FileOutputStream(tmp))) {
            addTree(z, dir, "");
        }
        zip.delete();
        if (!tmp.renameTo(zip)) throw new IOException("could not save " + zip.getName());
        return zip;
    }

    private static void addTree(java.util.zip.ZipOutputStream z, File dir, String prefix) throws IOException {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        byte[] buf = new byte[1 << 16];
        for (File k : kids) {
            if (k.getName().startsWith(".")) continue;          /* .remove and the like */
            if (k.isDirectory()) { addTree(z, k, prefix + k.getName() + "/"); continue; }
            z.putNextEntry(new ZipEntry(prefix + k.getName()));
            try (InputStream in = new java.io.FileInputStream(k)) {
                int n;
                while ((n = in.read(buf)) > 0) z.write(buf, 0, n);
            }
            z.closeEntry();
        }
    }

    static File modsDir(File configDir) {
        File d = new File(configDir, "mods");
        d.mkdirs();
        return d;
    }

    /* where a zip entry belongs: "" for the mod's top, or the part below the one folder it came in */
    private static String inner(String name, String prefix) {
        name = name.replace('\\', '/');
        while (name.startsWith("/")) name = name.substring(1);
        if (!prefix.isEmpty()) {
            if (!name.startsWith(prefix)) return null;
            name = name.substring(prefix.length());
        }
        return name;
    }

    private static boolean readableKind(String name) {
        int dot = name.lastIndexOf('.');
        if (dot < 0) return false;
        String ext = name.substring(dot + 1).toLowerCase(Locale.ROOT);
        for (String k : KINDS) if (k.equals(ext)) return true;
        return false;
    }

    /* ------------------------------------------------ what may stand at a mod's top */

    /* how an entry of TOP is read */
    private static final int CARD = 1, RESOURCES = 2, CONTENT = 3, PATCH = 4, OPTIONS = 5;

    /*
     * THE ONE TABLE of a mod's top (plan 30.09, M6 and 4.10.4): the entry, how
     * what is under it is read, and whether it belongs to the mod's root only
     * (a setting's layer, options/<key>/<value>/, is the mod's layout again
     * without its card and without options/ of its own). A name ending in
     * ".*" is that word, a dot and anything (resources.<lang>). Case is not
     * minded: Android's shared storage is not either.
     */
    private static final Object[][] TOP = {
        /* entry               how         root only */
        {"mod.json",           CARD,       true},
        {"metadata.xml",       CARD,       true},
        {"preview.png",        CARD,       true},
        {"resources",          RESOURCES,  false},
        {"resources.*",        RESOURCES,  false},
        {"resources-dlc3",     RESOURCES,  false},
        {"resources-dlc3.*",   RESOURCES,  false},
        {"content",            CONTENT,    false},
        {"patch",              PATCH,      false},
        {"options",            OPTIONS,    true},
    };

    /* how an entry at the top (of the mod, or of a layer) is read; 0 for dead weight */
    private static int topKind(String path, boolean layer) {
        int slash = path.indexOf('/');
        String top = (slash < 0 ? path : path.substring(0, slash)).toLowerCase(Locale.ROOT);
        for (Object[] row : TOP) {
            String entry = (String) row[0];
            int how = (Integer) row[1];
            if (layer && (Boolean) row[2]) continue;
            boolean match = entry.endsWith(".*")
                ? top.startsWith(entry.substring(0, entry.length() - 1)) && top.length() > entry.length() - 1
                : top.equals(entry);
            if (!match) continue;
            /* a card is a file at the top; the rest are folders */
            if ((how == CARD) != (slash < 0)) return 0;
            return how;
        }
        return 0;
    }

    /* is this file, below the top (of the mod or of a layer), one the port reads? (options/ aside) */
    private static boolean readable(String path, boolean layer) {
        String low = path.toLowerCase(Locale.ROOT);
        switch (topKind(path, layer)) {
            case CARD: return true;
            case RESOURCES: return readableKind(low);
            /* only the new enemy variants are merged from content/ (usermods.c content_entities) */
            case CONTENT: return low.equals("content/entities2.xml");
            /* a game table's changes: one file per table, at patch/'s top (plan 30.09, M3) */
            case PATCH: return low.matches("patch/[^/]+\\.xml") || low.equals("patch/stringtable.sta");
            default: return false;
        }
    }

    /*
     * System junk, never unpacked (decision 23): a Mac's __MACOSX/ and
     * .DS_Store, Windows' Thumbs.db, a git repository's .git/ - anywhere in
     * the zip.
     */
    static boolean junk(String zipPath) {
        String[] parts = zipPath.split("/");
        for (int i = 0; i < parts.length; i++) {
            String s = parts[i];
            if (i + 1 < parts.length) { if (s.equals("__MACOSX") || s.equals(".git")) return true; }
            else if (s.equals(".DS_Store") || s.equalsIgnoreCase("Thumbs.db")) return true;
        }
        return false;
    }

    /* a path that leaves the folder it is unpacked into (a leading '/' is gone already, inner()) */
    private static boolean escapes(String path) {
        for (String s : path.split("/")) if (s.equals("..")) return true;
        return false;
    }

    /* ------------------------------------------------ mod.json's words and settings */

    /* the port's sets by the phone's language (the schema 02.10.2026: en ru de es jp kr zh) */
    private static String phoneSet() {
        String l = Locale.getDefault().getLanguage();
        switch (l) {
            case "ja": return "jp";
            case "ko": return "kr";
            default: return l;
        }
    }

    /*
     * A field that is a string, or {language: text} (plan 30.09, 4.10.1): the
     * phone's language, then en, then the first there is. `phone` false asks
     * for the one spelling that does not change with the phone - en or the
     * first - for what keys the mod (the update's question, the folder's word).
     */
    static String word(org.json.JSONObject j, String key, boolean phone) {
        if (j == null) return "";
        Object o = j.opt(key);
        if (o instanceof org.json.JSONObject) {
            org.json.JSONObject by = (org.json.JSONObject) o;
            List<String> order = new ArrayList<>();
            if (phone) { order.add(phoneSet()); order.add(Locale.getDefault().getLanguage()); }
            order.add("en");
            for (String k : order) {
                Object v = by.opt(k);
                if (v instanceof String && !((String) v).trim().isEmpty()) return ((String) v).trim();
            }
            for (java.util.Iterator<String> it = by.keys(); it.hasNext(); ) {
                Object v = by.opt(it.next());
                if (v instanceof String && !((String) v).trim().isEmpty()) return ((String) v).trim();
            }
            return "";
        }
        if (o == null || o == org.json.JSONObject.NULL || o instanceof org.json.JSONArray) return "";
        return o.toString().trim();
    }

    /* one setting as mod.json declares it - what its folders need, no more (the rest is usermods.c's) */
    static final class Setting {
        String key, kind;
        final java.util.Set<String> values = new java.util.LinkedHashSet<>();
        boolean any;                            /* options/<key>/ holds a folder at all */
    }

    /* mod.json's "settings", by key; a setting with no key has no folder to own and is usermods.c's to judge */
    static java.util.Map<String, Setting> settings(org.json.JSONObject j) {
        java.util.Map<String, Setting> m = new java.util.LinkedHashMap<>();
        org.json.JSONArray a = j == null ? null : j.optJSONArray("settings");
        if (a == null) return m;
        for (int i = 0; i < a.length(); i++) {
            org.json.JSONObject s = a.optJSONObject(i);
            if (s == null) continue;
            Setting d = new Setting();
            d.key = s.optString("key", "").trim();
            d.kind = s.optString("kind", "").trim();
            if (d.key.isEmpty() || m.containsKey(d.key)) continue;
            org.json.JSONArray vals = s.optJSONArray("values");
            if (vals != null) for (int k = 0; k < vals.length(); k++) {
                org.json.JSONObject v = vals.optJSONObject(k);
                String val = v != null ? v.optString("value", "") : vals.opt(k) instanceof String ? (String) vals.opt(k) : "";
                if (!val.trim().isEmpty()) d.values.add(val.trim());
            }
            m.put(d.key, d);
        }
        return m;
    }

    /*
     * A file under options/ (the scheme 02.10.2026): options/<key>/<value>/ is
     * a layer when mod.json declares <key> and <value> is one of its values -
     * on/off for a switch, a "value" of a choice, a whole number for a
     * number. Then the file is read by the layer's own table. A file that is
     * in no layer (options/x, options/<key>/x) is dead weight. A layer mod.json
     * does not declare is dead weight too, with the reason said after the
     * install (hotfix 1.71, the owner: settings are an optional part - a mod is
     * never refused for one; it used to refuse the mod). Case is not minded, as
     * the storage does not mind it.
     */
    private static boolean layer(String in, java.util.Map<String, Setting> declared, java.util.Set<String> refused) {
        String[] parts = in.split("/", 4);
        if (parts.length < 4) return false;
        String key = parts[1], value = parts[2];
        Setting s = null;
        for (Setting d : declared.values()) if (d.key.equalsIgnoreCase(key)) { s = d; break; }
        if (s == null) {
            refused.add("options/" + key + "/" + t(" — настройка «" + key + "» не объявлена в mod.json (\"settings\")",
                                                   " - setting \"" + key + "\" is not declared in mod.json (\"settings\")"));
            return false;
        }
        s.any = true;
        String why = null;
        switch (s.kind) {
            case "switch":
                if (!value.equalsIgnoreCase("on") && !value.equalsIgnoreCase("off"))
                    why = t("у переключателя «" + s.key + "» папки только on и off", "switch \"" + s.key + "\" has only on and off folders");
                break;
            case "choice": {
                boolean known = false;
                for (String v : s.values) if (v.equalsIgnoreCase(value)) known = true;
                if (!known)
                    why = t("у выбора «" + s.key + "» нет значения «" + value + "» (есть: " + String.join(", ", s.values) + ")",
                            "choice \"" + s.key + "\" has no value \"" + value + "\" (it has: " + String.join(", ", s.values) + ")");
                break;
            }
            case "number":
                if (!value.matches("-?\\d{1,9}"))
                    why = t("у числа «" + s.key + "» имя папки — целое число", "number \"" + s.key + "\" takes whole-number folders only");
                break;
            default:
                refused.add("options/" + key + "/" + t(" — вид настройки «" + s.kind + "» неизвестен: switch, choice или number",
                                                       " - the setting's kind \"" + s.kind + "\" is unknown: switch, choice or number"));
                return false;
        }
        if (why != null) { refused.add("options/" + key + "/" + value + "/" + t(" — ", " - ") + why); return false; }
        return readable(parts[3], true);
    }

    private static String noBom(String s) {
        return s.startsWith("\uFEFF") ? s.substring(1) : s;
    }

    /* ------------------------------------------------ a PC mod's metadata.xml */

    /* the PC mod's card: name, directory, version, description, and its workshop id */
    static final class Metadata { String name = "", directory = "", version = "", description = "", workshop = ""; }

    static Metadata metadata(String xml) {
        String x = xml.replaceAll("(?s)<!--.*?-->", "");
        Metadata m = new Metadata();
        m.name = xmlText(x, "name");
        m.directory = xmlText(x, "directory");
        m.version = xmlText(x, "version");
        m.description = xmlText(x, "description");
        m.workshop = xmlText(x, "id");
        return m;
    }

    /* <tag>text</tag> under the card, its CDATA opened and its entities read; PC cards are often not
       well-formed XML (a bare & in a description), so this reads them leniently */
    private static String xmlText(String xml, String tag) {
        java.util.regex.Matcher t = java.util.regex.Pattern.compile("<" + tag + "(?:\\s[^>]*)?>(.*?)</" + tag + "\\s*>",
                java.util.regex.Pattern.DOTALL | java.util.regex.Pattern.CASE_INSENSITIVE).matcher(xml);
        if (!t.find()) return "";
        String v = t.group(1);
        StringBuilder b = new StringBuilder();
        java.util.regex.Matcher c = java.util.regex.Pattern.compile("(?s)<!\\[CDATA\\[(.*?)\\]\\]>").matcher(v);
        int at = 0;
        while (c.find()) {
            b.append(entities(v.substring(at, c.start()))).append(c.group(1));
            at = c.end();
        }
        b.append(entities(v.substring(at)));
        return b.toString().trim();
    }

    private static String entities(String s) {
        java.util.regex.Matcher e = java.util.regex.Pattern.compile("&(#x[0-9a-fA-F]+|#[0-9]+|amp|lt|gt|quot|apos);").matcher(s);
        StringBuilder b = new StringBuilder();
        int at = 0;
        while (e.find()) {
            b.append(s, at, e.start());
            String n = e.group(1);
            try {
                if (n.startsWith("#x")) b.appendCodePoint(Integer.parseInt(n.substring(2), 16));
                else if (n.startsWith("#")) b.appendCodePoint(Integer.parseInt(n.substring(1)));
                else b.append(n.equals("amp") ? "&" : n.equals("lt") ? "<" : n.equals("gt") ? ">" : n.equals("quot") ? "\"" : "'");
            } catch (IllegalArgumentException bad) { b.append(e.group()); }
            at = e.end();
        }
        return b.append(s.substring(at)).toString();
    }

    /*
     * A word made to the id's rules (plan 30.09, M6): lowercase latin letters,
     * digits, '-' and '_', starting with a letter or digit, at most 40.
     * Accents come off ("Café" -> "cafe"), anything else - spaces, other
     * scripts, punctuation - becomes one '-'. "" when nothing is left.
     */
    static String idWord(String s) {
        String n = java.text.Normalizer.normalize(s, java.text.Normalizer.Form.NFD).replaceAll("\\p{M}+", "");
        n = n.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9_-]+", "-").replaceAll("-{2,}", "-").replaceAll("^[^a-z0-9]+", "");
        if (n.length() > 40) n = n.substring(0, 40);
        return n.replaceAll("[-_]+$", "");
    }

    /* mod.json made from a PC card: plain UTF-8 strings, \" \\ \n escaped, read alike by json_field and json.c;
       only the schema's fields and none empty, so json.c has nothing to note */
    static String cardJson(String id, String name, String version, String description) {
        StringBuilder b = new StringBuilder("{\n  \"name\": ").append(jsonString(name)).append(",\n  \"id\": ").append(jsonString(id));
        if (!version.isEmpty()) b.append(",\n  \"version\": ").append(jsonString(version));
        if (!description.isEmpty()) b.append(",\n  \"description\": ").append(jsonString(description));
        return b.append("\n}\n").toString();
    }

    private static String jsonString(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c == '\n') b.append("\\n");
            else if (c == '\t') b.append(' ');
            else if (c >= 0x20) b.append(c);
        }
        return b.append('"').toString();
    }

    /* the dead weight, for the result window: a folder outside what the port reads as one line with its
       count, a file among what it reads by its own path, at most eight lines */
    static List<String> deadLines(List<String> dead) {
        java.util.Map<String, List<String>> groups = new java.util.LinkedHashMap<>();
        for (String d : dead) {
            int slash = d.indexOf('/');
            String key;
            if (slash < 0) key = d;
            else if (topKind(d, false) != 0) key = d.substring(0, d.lastIndexOf('/') + 1);
            else key = d.substring(0, slash + 1);
            groups.computeIfAbsent(key, k -> new ArrayList<>()).add(d);
        }
        List<String> out = new ArrayList<>();
        int shown = 0;
        for (java.util.Map.Entry<String, List<String>> g : groups.entrySet()) {
            if (out.size() >= 8) {
                int rest = dead.size() - shown;
                out.add(t("… и ещё файлов: " + rest, "… and " + rest + " more files"));
                break;
            }
            int n = g.getValue().size();
            shown += n;
            out.add(n == 1 ? g.getValue().get(0) : g.getKey() + t(" (файлов: " + n + ")", " (" + n + " files)"));
        }
        return out;
    }

    /* ------------------------------------------------ the reading, counted */

    /*
     * The bytes the zip reader has taken from the provider so far, and the last
     * 64 KB of them: a zip ends with its central directory's end record (in the
     * last 22 bytes plus a comment of up to 64 KB). The streaming reader never
     * looks at it, so a zip cut short exactly between two files would read as a
     * whole, smaller mod; without that record the archive is refused.
     */
    static final class Counted extends FilterInputStream {
        long n;
        private final byte[] tail = new byte[65535 + 22];
        Counted(InputStream in) { super(in); }
        private void keep(byte[] b, int off, int k) {
            int len = tail.length;
            if (k > len) { off += k - len; n += k - len; k = len; }
            int pos = (int) (n % len), a = Math.min(k, len - pos);
            System.arraycopy(b, off, tail, pos, a);
            System.arraycopy(b, off + a, tail, 0, k - a);
            n += k;
        }
        @Override public int read() throws IOException {
            int b = super.read();
            if (b >= 0) keep(new byte[] {(byte) b}, 0, 1);
            return b;
        }
        @Override public int read(byte[] b, int off, int len) throws IOException {
            int k = super.read(b, off, len);
            if (k > 0) keep(b, off, k);
            return k;
        }
        @Override public long skip(long k) throws IOException {
            byte[] buf = new byte[8192];
            long done = 0;
            while (done < k) {
                int r = read(buf, 0, (int) Math.min(buf.length, k - done));
                if (r <= 0) break;
                done += r;
            }
            return done;
        }
        /* read to the end; is the end-of-central-directory record there? */
        boolean wholeZip() throws IOException {
            byte[] buf = new byte[1 << 16];
            while (read(buf, 0, buf.length) > 0) { /* to the end */ }
            int have = (int) Math.min(n, tail.length);
            for (int back = 22; back <= have; back++) {
                long at = n - back;
                if (tail[(int) (at % tail.length)] == 'P' && tail[(int) ((at + 1) % tail.length)] == 'K'
                        && tail[(int) ((at + 2) % tail.length)] == 5 && tail[(int) ((at + 3) % tail.length)] == 6) return true;
            }
            return false;
        }
    }

    /* at most ten reports a second; a step's first and last always get through */
    static final class Pacer implements Progress {
        private final Progress to;
        private long last;
        private int step = -1;
        Pacer(Progress to) { this.to = to; }
        long bytes, bytesTotal;                 /* the unpacked bytes, kept here by the installer */
        @Override public void at(int s, long done, long total, long b, long bt, String what) {
            long now = System.nanoTime();
            boolean edge = s != step || (total >= 0 && done >= total);
            if (!edge && now - last < 100_000_000L) return;
            step = s;
            last = now;
            if (to != null) to.at(s, done, total, b, bt, what);
        }
        void at(int s, long done, long total, String what) { at(s, done, total, bytes, bytesTotal, what); }
    }

    /* the archive behind a Uri, for the phone */
    static Result add(Context ctx, Uri uri, File configDir, Progress p, Asker asker) {
        return install(uriSource(ctx, uri), configDir, p, asker);
    }

    /* ------------------------------------------------ A BATCH (hotfix 1.73) */
    /*
     * SEVERAL MODS AT ONCE, AND NO RESTART AFTER THEM (the players' request, the owner approved it 08.10.2026).
     * The picker gives one archive or many; they go in one after another, each the whole way the one archive
     * always went - READ, [ADAPT,] CHECK, its room, the update question, UNPACK into .incoming-<folder>, PLACE,
     * LIST - so each keeps every guarantee of the single add: nothing half-unpacked is ever a mod, an update
     * never writes into the folder the running game reads. A mod that fails is said and the next one goes in;
     * even an error nobody foresaw (an exception, the memory) ends that mod only. The game is not restarted:
     * the MODS page shows the new mods at once, pale and with RESTART TO APPLY under them, and the player
     * restarts once, when he is done.
     */
    static final class Item {
        final String label;     /* the archive's own name, as the picker shows it */
        final Source src;
        Item(String label, Source src) { this.label = label; this.src = src; }
    }

    /* where the batch is: mod k (from 0) of n, and its archive's name */
    interface Batch { void mod(int k, int n, String label); }

    /* the update question in a batch: ANSWER_UPDATE or not, and ANSWER_ALL - the same for the batch's other mods */
    interface BatchAsker { int ask(String question, boolean more); }
    static final int ANSWER_UPDATE = 1, ANSWER_ALL = 2;

    static List<Result> installAll(List<Item> items, File configDir, Progress progress, BatchAsker asker, Batch batch) {
        waitForGame(20_000);                    /* THE START (hotfix 1.74) */
        markInstalling(true);
        try {
            return installEach(items, configDir, progress, asker, batch);
        } finally {
            markInstalling(false);
        }
    }

    private static List<Result> installEach(List<Item> items, File configDir, Progress progress, BatchAsker asker, Batch batch) {
        List<Result> out = new ArrayList<>();
        final Boolean[] forAll = {null};
        int n = items.size();
        for (int k = 0; k < n; k++) {
            Item it = items.get(k);
            if (batch != null) batch.mod(k, n, it.label);
            final boolean more = k + 1 < n;
            Asker one = asker == null ? null : question -> {
                if (forAll[0] != null) return forAll[0];
                int a = asker.ask(question, more);
                boolean up = (a & ANSWER_UPDATE) != 0;
                if ((a & ANSWER_ALL) != 0) forAll[0] = up;
                return up;
            };
            Result r;
            try {
                r = install(it.src, configDir, progress, one);
            } catch (Throwable ex) {            /* nothing foreseen, still one mod's end only */
                r = new Result();
                r.text = t("Установка прервалась: ", "The add stopped: ") + reasonOf(ex);
                report("usermods", "add of " + it.label + " stopped: " + reasonOf(ex));
            }
            if (r == null) { r = new Result(); r.text = "?"; }
            r.label = it.label;
            out.add(r);
            report("usermods", "batch " + (k + 1) + "/" + n + " " + it.label + ": " + (r.ok ? (r.updated ? "updated" : "added")
                    + " into " + r.folder : "refused"));
        }
        return out;
    }

    /*
     * THE PICKER'S ANSWER (ACTION_OPEN_DOCUMENT with EXTRA_ALLOW_MULTIPLE): several files come as ClipData,
     * one as ClipData or as the intent's data alone, depending on the picker. Every item of the clip, in its
     * order, then the data when the clip had none; nulls and repeats left out. Generic, so the desk reads it
     * without Android: MainActivity hands in data.getData() and the clip's items.
     */
    static <T> List<T> picked(T single, int count, java.util.function.IntFunction<T> item) {
        List<T> out = new ArrayList<>();
        for (int i = 0; i < count; i++) {
            T u = item.apply(i);
            if (u != null && !out.contains(u)) out.add(u);
        }
        if (out.isEmpty() && single != null) out.add(single);
        return out;
    }

    /* the name the picker shows for a Uri, else its last path segment */
    static String labelOf(Context ctx, Uri uri) {
        try (android.database.Cursor c = ctx.getContentResolver().query(uri,
                new String[] {android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && !c.isNull(0)) {
                String n = c.getString(0);
                if (n != null && !n.trim().isEmpty()) return n.trim();
            }
        } catch (RuntimeException ignored) { }
        String last = uri.getLastPathSegment();
        if (last == null) return "?";
        int slash = last.lastIndexOf('/');
        return slash >= 0 ? last.substring(slash + 1) : last;
    }

    /* the archive behind a Uri, opened as often as the installer reads it */
    static Source uriSource(Context ctx, Uri uri) {
        return new Source() {
            @Override public InputStream open() throws IOException {
                InputStream in = ctx.getContentResolver().openInputStream(uri);
                if (in == null) throw new IOException("the provider gave no stream");
                return in;
            }
            @Override public long size() {
                try (android.database.Cursor c = ctx.getContentResolver().query(uri,
                        new String[] {android.provider.OpenableColumns.SIZE}, null, null, null)) {
                    if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getLong(0);
                } catch (RuntimeException ignored) { }
                try (android.content.res.AssetFileDescriptor fd = ctx.getContentResolver().openAssetFileDescriptor(uri, "r")) {
                    if (fd != null && fd.getLength() >= 0) return fd.getLength();
                } catch (IOException | RuntimeException ignored) { }
                return -1;
            }
        };
    }

    /* a mod installed already: its folder and its card */
    static final class Installed {
        String folder, id, name, author, version;
    }

    private static String idOf(String id, String name) {
        id = id.trim().toLowerCase(Locale.ROOT);
        if (id.isEmpty()) id = name.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]+", "-").replaceAll("^-|-$", "");
        return id;
    }

    /* the mods' folders in the order mods.txt names them (either word order), the rest after */
    static List<String> listOrder(File mods) {
        List<String> order = new ArrayList<>();
        try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(new File(mods, "mods.txt")), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("#")) continue;
                String[] w = line.trim().split("\\s+");
                if (w.length < 2) continue;
                boolean a = w[0].equals("on") || w[0].equals("off");
                String folder = a ? w[1] : w[0];
                if (!order.contains(folder)) order.add(folder);
            }
        } catch (IOException ignored) { /* no list: the folders' own order */ }
        File[] kids = mods.listFiles();
        if (kids != null) {
            java.util.Arrays.sort(kids);
            for (File k : kids) if (k.isDirectory() && !k.getName().startsWith(".") && !order.contains(k.getName())) order.add(k.getName());
        }
        return order;
    }

    /* an installed mod with the same author's id, name and author: the one an update is for, or null */
    static Installed findSame(File mods, String id, String name, String author) {
        for (String folder : listOrder(mods)) {
            File dir = new File(mods, folder);
            if (!dir.isDirectory() || new File(dir, ".remove").exists() || new File(dir, ADDING).exists()) continue;
            File card = new File(dir, "mod.json");
            if (!card.isFile()) continue;
            try (InputStream in = new java.io.FileInputStream(card)) {
                org.json.JSONObject j = new org.json.JSONObject(new String(readAll(in, 64 * 1024), "UTF-8"));
                Installed m = new Installed();
                m.folder = folder;
                m.name = word(j, "name", false);
                m.id = idOf(j.optString("id", ""), m.name);
                m.author = j.optString("author", "").trim();
                m.version = j.optString("version", "").trim();
                if (m.id.equals(id) && m.name.equals(name) && m.author.equals(author)) return m;
            } catch (IOException | org.json.JSONException | RuntimeException ignored) { /* a card that does not read is no match */ }
        }
        return null;
    }

    /*
     * The mod's NAME as a folder's word (the owner, 02.10.2026: "001-Reflashed"): letters
     * and digits of any script are kept as they are - Cyrillic, CJK, accents (the phone's
     * storage, the media store and the file managers all take UTF-8 names; NFC, so one
     * name is one spelling) - and so are '_' and '.'; everything else (spaces, ':', '/',
     * '!', quotes, emoji) becomes one '-'. No dot or dash at the ends (a leading dot would
     * hide the folder - usermods.c skips those). At most 40 bytes of UTF-8, cut between
     * characters, so "<number>-<word>-<yyyyMMdd>-<HHmmss>" (a copy put beside) stays under
     * the 63 bytes the list's reader takes - the 1.666 build's too ("%63s"). A name with
     * nothing left (all punctuation) gives the author's id.
     */
    /*
     * THE NAME WITHOUT THE PC'S SORT MARKS (the owner, 05.10.2026), as usermods.c
     * display_name shows it on the MODS page: a PC modder puts his mod where he wants it
     * in the PC's list by the first characters of its name ("!!TBOI: Reflashed",
     * "!!~External item descriptions"); the port has an order of its own, so the leading
     * marks go, and then a leading "TBOI" tag when a real name follows it. For the folder's
     * word of a new install only: mod.json keeps the name as the mod has it, and folders made
     * before stay as they are.
     */
    static final String SORT_MARKS = " !~_.-+=#*@$%^&|:;,`\t";

    static String displayName(String name) {
        int p = 0;
        while (p < name.length() && SORT_MARKS.indexOf(name.charAt(p)) >= 0) p++;
        if (name.regionMatches(true, p, "TBOI", 0, 4) && p + 4 < name.length() && ":- _".indexOf(name.charAt(p + 4)) >= 0) {
            int q = p + 4;
            while (q < name.length() && SORT_MARKS.indexOf(name.charAt(q)) >= 0) q++;
            if (q < name.length()) p = q;                      /* a name follows the tag */
        }
        if (p >= name.length()) return name;                   /* marks alone: left as they are */
        String s = name.substring(p);
        int e = s.length();
        while (e > 0 && s.charAt(e - 1) == ' ') e--;
        return s.substring(0, e);
    }

    static String folderWord(String name, String id) {
        String n = java.text.Normalizer.normalize(displayName(name), java.text.Normalizer.Form.NFC);
        StringBuilder b = new StringBuilder();
        n.codePoints().forEach(cp -> {
            /* past the BMP, JNI's modified UTF-8 is not the file system's UTF-8: such a letter is a '-' too */
            if ((Character.isLetterOrDigit(cp) && cp < 0x10000) || cp == '_' || cp == '.') b.appendCodePoint(cp);
            else if (b.length() > 0 && b.charAt(b.length() - 1) != '-') b.append('-');
        });
        String w = b.toString().replaceAll("^[-.]+|[-.]+$", "");
        StringBuilder cut = new StringBuilder();
        int bytes = 0;
        for (int i = 0; i < w.length(); ) {
            int cp = w.codePointAt(i);
            int len = new String(Character.toChars(cp)).getBytes(java.nio.charset.StandardCharsets.UTF_8).length;
            if (bytes + len > 40) break;
            cut.appendCodePoint(cp);
            bytes += len;
            i += Character.charCount(cp);
        }
        w = cut.toString().replaceAll("[-.]+$", "");
        return w.isEmpty() ? id : w;
    }

    /* the next number for a mod's folder: one past every number the folder holds, live or in passing */
    static String issue(File mods, String name, String id) {
        String word = folderWord(name, id);
        int max = 0;
        String[] names = mods.list();
        if (names != null) for (String n : names) {
            String s = n;
            if (s.startsWith(".incoming-")) s = s.substring(10);
            else if (s.startsWith(".old-")) s = s.substring(5);
            int k = 0;
            while (k < s.length() && Character.isDigit(s.charAt(k))) k++;
            if (k == 0 || k > 6 || k >= s.length() || s.charAt(k) != '-') continue;
            max = Math.max(max, Integer.parseInt(s.substring(0, k)));
        }
        String folder;
        do {
            max++;
            folder = String.format(Locale.ROOT, "%03d-%s", max, word);
        } while (new File(mods, folder).exists() || new File(mods, ".incoming-" + folder).exists());
        return folder;
    }

    /* ------------------------------------------------ the installer */

    static Result install(Source src, File configDir, Progress progress, Asker asker) {
        return installAs(src, configDir, progress, asker, false);
    }

    /* the label the adapted mod's page shows first (usermods.c reads en, ru, de, es; jp kr zh read en) */
    static java.util.Map<String, String> adaptedLabel() {
        java.util.Map<String, String> m = new java.util.LinkedHashMap<>();
        m.put("en", "Adapted automatically from a PC mod - may work unstably.");
        m.put("ru", "Адаптирован автоматически из мода для ПК - может работать нестабильно.");
        m.put("de", "Automatisch aus einem PC-Mod angepasst - kann instabil laufen.");
        m.put("es", "Adaptado automáticamente de un mod de PC: puede funcionar de forma inestable.");
        return m;
    }

    /* does the zip need the converter: no mod.json, and a PC card or resources/ (at the top or one folder down) */
    static boolean wantsAdapting(List<String> names, String manifest, String pcCard) {
        if (manifest != null) return false;
        if (pcCard != null) return true;
        for (String n : names) {
            String low = n.toLowerCase(Locale.ROOT);
            if (low.startsWith("resources") || low.matches("[^/]+/resources.*")) return true;
        }
        return false;
    }

    /* the adapting road: the converter writes the mod in the port's format beside the mods, and that is installed */
    private static Result adapt(Source src, File configDir, final Pacer p, Progress progress, Asker asker, int entries) {
        Result r = new Result();
        RefSource rs = gameRef;
        if (rs == null) return null;                            /* no reference: the mod is taken as before */
        File mods = modsDir(configDir);
        File tmp = new File(mods, ".incoming-adapt-" + System.currentTimeMillis() + ".zip");   /* .incoming-*: swept at start */
        /* adapting holds the converted zip and the unpacked mod at once - about twice the mod - so too little
           room is said before anything is written, not halfway (the owner, 05.10.2026) */
        File probe = mods.isDirectory() ? mods : configDir;
        long free = freeSpace.applyAsLong(probe), need = 2 * src.size() + (64L << 20);
        if (free > 0 && free < need) {
            report("usermods", "adapt: " + (free >> 20) + " MB free, " + (need >> 20) + " MB needed - refused");
            r.text = t("Не хватает места на телефоне: для адаптации нужно около " + (need >> 20) + " МБ, свободно " + (free >> 20) + " МБ.",
                       "Not enough room on the phone: adapting needs about " + (need >> 20) + " MB, " + (free >> 20) + " MB are free.");
            return r;
        }
        ModPort.Outcome o;
        long t0 = System.nanoTime();
        try {
            ModPort.Ref ref;
            try (InputStream in = new java.io.BufferedInputStream(rs.open(), 1 << 16)) { ref = ModPort.Ref.load(in); }
            if (!mods.isDirectory() && !mods.mkdirs()) throw new IOException("could not make " + mods);
            p.at(ADAPT_READ, 0, entries, null);
            o = ModPort.convert(src, ref, tmp, (phase, done, total, what) -> {
                if (phase == 0) p.at(ADAPT_READ, Math.min(done, entries), entries, what);
                else p.at(ADAPT, done, total, what);
            }, entries, adaptedLabel());
        } catch (IOException | RuntimeException | OutOfMemoryError ex) {
            tmp.delete();
            report("usermods", "adapt failed: " + reasonOf(ex));
            r.text = t("Мод для ПК не удалось адаптировать: ", "The PC mod could not be adapted: ") + reasonOf(ex);
            return r;
        }
        report("usermods", "adapt: " + o.files + " files, " + (System.nanoTime() - t0) / 1_000_000 + " ms" + (o.empty ? ", nothing for the port" : ""));
        if (o.name != null && !o.name.isEmpty()) r.name = displayName(o.name);
        if (o.empty) {
            tmp.delete();
            r.text = t("В моде «" + o.name + "» нет ничего, что читает порт: ", "Mod \"" + o.name + "\" has nothing the port reads: ")
                    + (o.script ? t("это мод-скрипт, а скриптов порт не запускает, — ставить нечего.",
                                    "it is a script mod, and the port runs no scripts - there is nothing to install.")
                                : t("ни resources/, ни content/entities2.xml.", "neither resources/ nor content/entities2.xml."));
            return r;
        }
        final File made = tmp;
        Source converted = new Source() {
            @Override public InputStream open() throws IOException { return new java.io.BufferedInputStream(new java.io.FileInputStream(made), 1 << 16); }
            @Override public long size() { return made.length(); }
        };
        /* the converted mod's own reading is part of adapting: not shown as a second READ */
        Progress rest = (step, done, total, bytes, bytesTotal, what) -> {
            if (step != READ && progress != null) progress.at(step, done, total, bytes, bytesTotal, what);
        };
        Result in = installAs(converted, configDir, rest, asker, true);
        tmp.delete();
        if (!in.ok) {
            in.text = t("Мод для ПК адаптирован, но не установлен:\n", "The PC mod was adapted but not installed:\n") + in.text;
            return in;
        }
        in.adapted = true;
        String where = in.folder + "/" + ADAPTED_REPORT;
        try (java.io.OutputStream f = new java.io.FileOutputStream(new File(new File(mods, in.folder), ADAPTED_REPORT))) {
            f.write(o.report.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (IOException | RuntimeException ex) {
            where = null;
            report("usermods", "adapt: the report could not be saved: " + reasonOf(ex));
        }
        StringBuilder b = new StringBuilder(t("Это мод для ПК: порт адаптировал его сам и может работать с ним нестабильно.",
                "This is a PC mod: the port adapted it by itself, and it may work unstably."));
        if (!o.counts.isEmpty()) b.append(t(" Сделано: ", " Done: ")).append(o.counts).append('.');
        if (where != null) b.append(t(" Отчёт по файлам: mods/", " The report, file by file: mods/")).append(where).append('.');
        in.text = b + "\n\n" + in.text;
        return in;
    }

    private static Result installAs(Source src, File configDir, Progress progress, Asker asker, boolean adapted) {
        Result r = new Result();
        Pacer p = new Pacer(progress);
        File mods = modsDir(configDir);
        List<String> names = new ArrayList<>();
        /* the cards: mod.json, else a PC mod's metadata.xml - at the top or one folder down, the shallower first */
        String manifest = null, manifestAt = null, pcCard = null, pcCardAt = null;
        byte[] manifestBytes = null;
        long t0 = System.nanoTime();
        long zipBytes = src.size(), read = 0;
        java.util.Map<String, Long> sizes = new java.util.HashMap<>();
        /* first reading: what is inside, how big unpacked, and the cards */
        p.at(READ, 0, zipBytes, null);
        try (Counted raw = new Counted(src.open());
             ZipInputStream zip = new ZipInputStream(raw)) {
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            while ((e = zip.getNextEntry()) != null) {
                String n = e.getName().replace('\\', '/');
                if (e.isDirectory()) continue;
                names.add(n);
                String low = n.toLowerCase(Locale.ROOT);
                long size = 0;
                int depth = low.split("/").length;
                if (!junk(n) && depth <= 2) {
                    boolean json = low.equals("mod.json") || low.endsWith("/mod.json");
                    boolean pc = low.equals("metadata.xml") || low.endsWith("/metadata.xml");
                    if ((json && (manifestAt == null || depth < manifestAt.split("/").length))
                            || (pc && (pcCardAt == null || depth < pcCardAt.split("/").length))) {
                        byte[] m = readAll(zip, 64 * 1024);
                        if (json) { manifest = new String(m, "UTF-8"); manifestBytes = m; manifestAt = n; }
                        else { pcCard = new String(m, "UTF-8"); pcCardAt = n; }
                        size = m.length;
                    }
                }
                int k;
                while ((k = zip.read(buf)) > 0) {
                    size += k;
                    if (zipBytes < 0 || raw.n < zipBytes) p.at(READ, raw.n, zipBytes, n);
                }
                sizes.put(n, size);
                if (zipBytes < 0 || raw.n < zipBytes) p.at(READ, raw.n, zipBytes, n);
            }
            if (!raw.wholeZip()) throw new IOException(t("архив обрезан — в конце нет оглавления zip", "the archive is cut short - the zip's directory at its end is missing"));
            read = raw.n;
        } catch (IOException | RuntimeException ex) {
            r.text = t("Это не zip-архив или он повреждён: ", "This is not a zip archive, or it is damaged: ") + ex.getMessage();
            report("usermods", "add refused at READ: " + reasonOf(ex));
            return r;
        }
        p.at(READ, zipBytes < 0 ? read : zipBytes, zipBytes < 0 ? read : zipBytes, null);
        long tRead = System.nanoTime();
        if (!adapted && wantsAdapting(names, manifest, pcCard)) {
            Result a = adapt(src, configDir, p, progress, asker, names.size());
            if (a != null) return a;
        }
        p.at(CHECK, 0, 1, null);
        /* the card: mod.json, else one made from a PC mod's metadata.xml (plan 30.09, M6) */
        if (manifest == null && pcCard == null) {
            boolean script = false;
            for (String n : names) if (!junk(n) && n.toLowerCase(Locale.ROOT).endsWith(".lua")) script = true;
            r.text = script
                ? t("В архиве скрипт, но нет ни mod.json, ни metadata.xml — это не мод. Порт скрипты не запускает; ресурсный мод для ПК берётся вместе со своим metadata.xml.",
                    "The archive holds a script but neither mod.json nor metadata.xml, so it is not a mod. The port runs no scripts; a PC resource mod is taken with its metadata.xml.")
                : t("В архиве нет ни mod.json, ни metadata.xml — это не мод.",
                    "The archive has neither mod.json nor metadata.xml, so it is not a mod.");
            return r;
        }
        /* the shallower card is the mod's; on a tie mod.json wins */
        boolean fromPc = manifest == null
            || (pcCard != null && pcCardAt.split("/").length < manifestAt.split("/").length);
        String cardAt = fromPc ? pcCardAt : manifestAt;
        String prefix = cardAt.contains("/") ? cardAt.substring(0, cardAt.lastIndexOf('/') + 1) : "";
        /* name: in the phone's language, for the windows; keyName: the one spelling that keys the mod */
        String id, name, keyName, author, version, madeCard = null;
        java.util.Map<String, Setting> declared;
        if (fromPc) {
            Metadata md = metadata(noBom(pcCard));
            id = idWord(md.directory);
            if (id.isEmpty()) id = idWord(md.name);
            if (id.isEmpty()) id = idWord(md.workshop);
            if (id.isEmpty()) {
                r.text = t("metadata.xml мода для ПК не годится: ни в directory, ни в name нет латинских букв или цифр, из которых сделать id.",
                           "The PC mod's metadata.xml will not do: neither its directory nor its name has latin letters or digits to make an id from.");
                return r;
            }
            name = keyName = !md.name.isEmpty() ? md.name : !md.directory.isEmpty() ? md.directory : id;
            author = "";
            version = md.version;
            madeCard = cardJson(id, name, version, md.description);
            declared = settings(null);
        } else {
            /* one judge (02.10.2026): json.c, the reader the game itself uses, says first; org.json only reads the fields after */
            String verdict = judge(manifestBytes);
            if (verdict != null && !verdict.isEmpty()) {
                StringBuilder faults = new StringBuilder();
                /* hotfix 1.71: a slip is a note and a broken setting is switched off in the game - neither refuses */
                for (String line : verdict.split("\n"))
                    if (line.startsWith("broken ")) faults.append("\n• ").append(line);
                report("usermods", "add: mod.json " + verdict.trim().replace("\n", "; "));
                if (faults.length() > 0) {
                    r.text = t("mod.json не годится (причины по-английски, как в check.txt):", "mod.json will not do:") + faults;
                    return r;
                }
            }
            try {
                String strictText = strict(manifestBytes);
                org.json.JSONObject j = new org.json.JSONObject(strictText != null ? strictText : noBom(manifest));
                name = word(j, "name", true);
                keyName = word(j, "name", false);
                id = idOf(j.optString("id", ""), keyName);
                author = word(j, "author", false);
                version = j.optString("version", "").trim();
                declared = settings(j);
            } catch (org.json.JSONException ex) {
                r.text = t("mod.json не читается: ", "mod.json cannot be read: ") + ex.getMessage();
                return r;
            }
            if (name.isEmpty()) { r.text = t("В mod.json нет названия (\"name\").", "mod.json has no name (\"name\")."); return r; }
            if (!id.matches("[a-z0-9][a-z0-9_-]{0,39}")) {
                r.text = t("В mod.json плохой id: латиница, цифры, - и _, до 40 знаков.", "mod.json has a bad id: lowercase latin letters, digits, - and _, up to 40 characters.");
                return r;
            }
        }
        /* the windows name it as the MODS page does, without the PC's sort marks (the card keeps it as it is) */
        name = displayName(name);
        r.name = name;
        /* the check: each file read by the port, dead weight, or a reason to refuse the mod (the three classes) */
        java.util.Set<String> refused = new java.util.LinkedHashSet<>();
        java.util.Set<String> optionNotes = new java.util.LinkedHashSet<>();   /* settings' folders that change nothing */
        java.util.Set<String> keep = new java.util.HashSet<>();        /* the zip's files to unpack */
        List<String> dead = new ArrayList<>();
        int files = 0, toUnpack = 0, skipped = 0;
        boolean script = false;
        long unpackBytes = 0;
        for (String n : names) {
            if (junk(n)) { skipped++; continue; }
            String in = inner(n, prefix);
            if (in == null) { refused.add(n + t(" — вне папки мода", " - outside the mod's folder")); continue; }
            if (in.isEmpty()) continue;
            if (escapes(in)) { refused.add(n + t(" — недопустимый путь", " - a path that is not allowed")); continue; }
            /* the port's own marks: one would take the mod down at the next start */
            if (in.equalsIgnoreCase(ADDING) || in.equalsIgnoreCase(".remove")) { skipped++; continue; }
            toUnpack++;
            keep.add(n);
            Long sz = sizes.get(n);
            unpackBytes += sz != null ? sz : 0;
            if (in.toLowerCase(Locale.ROOT).endsWith(".lua")) script = true;
            int top = topKind(in, false);
            boolean reads = top == OPTIONS ? layer(in, declared, optionNotes) : readable(in, false);
            if (!reads) dead.add(in);
            else if (top != CARD) files++;
        }
        /* a number with no folder of its own would change nothing (the scheme 02.10.2026, part 2): said, not refused */
        for (Setting s : declared.values())
            if ("number".equals(s.kind) && !s.any)
                optionNotes.add(t("настройка «" + s.key + "» (число): нет ни одной папки options/" + s.key + "/<число>/ — она ничего бы не меняла",
                              "setting \"" + s.key + "\" (a number): there is no options/" + s.key + "/<number>/ folder, so it would change nothing"));
        if (!refused.isEmpty()) {
            StringBuilder b = new StringBuilder(t("Мод «" + name + "» не принят:\n", "Mod \"" + name + "\" refused:\n"));
            int shown = 0;
            for (String s : refused) { if (shown++ >= 8) { b.append(t("… и ещё ", "… and ")).append(refused.size() - 8).append(t("", " more")); break; } b.append("• ").append(s).append('\n'); }
            r.text = b.toString().trim();
            return r;
        }
        if (files == 0) {
            r.text = t("В моде «" + name + "» нет ни одного файла, который читает порт (resources/, content/entities2.xml, patch/, options/).",
                       "Mod \"" + name + "\" has no file the port reads (resources/, content/entities2.xml, patch/, options/).");
            /* a PC script mod (Stats+, Planetarium Chance, the range fixes - 04.10.2026): say why, not only what */
            if (script)
                r.text += t(" Это мод-скрипт: всё, что он делает, делает его Lua, а скриптов порт не запускает, — ставить нечего.",
                            " It is a script mod: all it does is done by its Lua, and the port runs no scripts - there is nothing to install.");
            return r;
        }
        if (unpackBytes > MAX_BYTES) { r.text = t("Мод больше гигабайта в распакованном виде.", "The mod is more than a gigabyte unpacked."); return r; }
        /*
         * THE ROOM, BEFORE ANYTHING IS WRITTEN (hotfix 1.73: a batch of mods fills a phone mod by mod, and a mod
         * cut short by a full storage was found only halfway through its unpacking): the unpacked bytes, a block's
         * slack a file, and 16 MB to spare. A mod that does not fit is refused with the numbers; the batch goes
         * on with the next one, which may be smaller. The adapting road has said its own word already (adapt).
         */
        long freeNow = freeSpace.applyAsLong(mods), needNow = roomFor(unpackBytes, toUnpack);
        if (freeNow > 0 && freeNow < needNow) {
            report("usermods", "add " + id + ": " + (freeNow >> 20) + " MB free, " + (needNow >> 20) + " MB needed - refused");
            r.text = noRoom(needNow, freeNow);
            return r;
        }
        p.at(CHECK, 1, 1, null);
        /* the folder: the same mod's own when the player updates it, else a new number */
        Installed same = findSame(mods, id, keyName, author);
        boolean update = false;
        if (same != null) update = asker != null && asker.update(question(name, same.version, version));
        /*
         * AN UPDATE GOES INTO A NEW FOLDER (hotfix 1.73). With no restart after ADD MOD the running game keeps
         * reading the copy it started with - this run's index and the guard's word point into its folder, the
         * pairs and the caches are keyed by its name - so the installer never writes into it: the new version
         * gets a folder of its own, takes the old one's place in the list with its on/off and its settings
         * (usermods.c list_put), and the old folder is marked to go at the next start.
         */
        String target = issue(mods, keyName, id), retire = update ? same.folder : null;
        r.folder = target;
        long tCheck = System.nanoTime();
        /* second reading: unpack into a fresh folder, marked unfinished, then put it in place */
        File incoming = new File(mods, ".incoming-" + target);
        deleteTree(incoming);
        long total = 0;
        int done = 0;
        List<String> notTaken = new ArrayList<>();
        p.bytes = 0;
        p.bytesTotal = unpackBytes;
        p.at(UNPACK, 0, toUnpack, null);
        try {
            if (!incoming.mkdirs() && !incoming.isDirectory()) throw new IOException("could not make " + incoming.getName());
            if (!new File(incoming, ADDING).createNewFile() && !new File(incoming, ADDING).exists())
                throw new IOException("could not mark " + incoming.getName());
        } catch (IOException | RuntimeException ex) {
            r.text = t("Не удалось распаковать мод: ", "Could not unpack the mod: ") + ex.getMessage();
            return r;
        }
        try (InputStream raw = src.open();
             ZipInputStream zip = new ZipInputStream(raw)) {
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            String base = incoming.getCanonicalPath() + File.separator;
            while ((e = zip.getNextEntry()) != null) {
                if (e.isDirectory()) continue;
                String n = e.getName().replace('\\', '/');
                if (!keep.contains(n)) continue;            /* junk and the port's marks */
                String in = inner(n, prefix);
                if (in == null || in.isEmpty()) continue;
                File out = new File(incoming, in);
                if (!out.getCanonicalPath().startsWith(base)) continue;
                out.getParentFile().mkdirs();
                OutputStream opened;
                try {
                    opened = new FileOutputStream(out);
                } catch (IOException ex) {
                    /* dead weight the storage will not take (a name it does not allow) is left out, not the mod */
                    if (!dead.remove(in)) throw ex;
                    notTaken.add(in + " — " + reasonOf(ex));
                    done++;
                    continue;
                }
                try (OutputStream o = opened) {
                    int k;
                    while ((k = zip.read(buf)) > 0) {
                        total += k;
                        if (total > MAX_BYTES) throw new IOException("more than a gigabyte");
                        o.write(buf, 0, k);
                    }
                }
                done++;
                p.bytes = total;
                p.at(UNPACK, Math.min(done, toUnpack), toUnpack, in);
            }
        } catch (IOException | RuntimeException ex) {
            deleteTree(incoming);
            r.text = t("Не удалось распаковать мод: ", "Could not unpack the mod: ") + ex.getMessage();
            report("usermods", "add " + id + " refused at UNPACK: " + reasonOf(ex));
            return r;
        }
        /* a PC mod's card, made from its metadata.xml: what usermods.c, the update's question and EXPORT read */
        if (madeCard != null) {
            try (OutputStream o = new FileOutputStream(new File(incoming, "mod.json"))) {
                o.write(madeCard.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            } catch (IOException | RuntimeException ex) {
                deleteTree(incoming);
                r.text = t("Не удалось распаковать мод: ", "Could not unpack the mod: ") + "mod.json: " + ex.getMessage();
                report("usermods", "add " + id + " refused at UNPACK: mod.json " + reasonOf(ex));
                return r;
            }
        }
        p.at(UNPACK, toUnpack, toUnpack, null);
        p.bytes = p.bytesTotal = 0;
        long tUnpack = System.nanoTime();
        Result placed = putInPlace(r, mods, incoming, target, update, retire, name, files, toUnpack, p);
        long tEnd = System.nanoTime();
        report("usermods", "add " + id + " as " + target + " (" + (update ? "update of " + retire : same != null ? "new, beside the same mod in " + same.folder : "new")
                + "): read " + read / (1 << 20) + " MB in " + (tRead - t0) / 1_000_000 + " ms, " + names.size() + " files; check "
                + (tCheck - tRead) / 1_000_000 + " ms; unpack " + done + " files " + total / (1 << 20) + " MB in "
                + (tUnpack - tCheck) / 1_000_000 + " ms; place and list " + (tEnd - tUnpack) / 1_000_000 + " ms");
        report("usermods", "add " + id + ": " + files + " read, " + dead.size() + " dead weight" + (script ? " (a script)" : "")
                + ", " + skipped + " junk not unpacked" + (notTaken.isEmpty() ? "" : ", " + notTaken.size() + " not taken by the storage")
                + (fromPc ? ", card made from metadata.xml" : "") + (declared.isEmpty() ? "" : ", settings " + declared.keySet()));
        if (placed.ok) placed.text += afterword(dead, notTaken, script, fromPc, id);
        if (placed.ok && !optionNotes.isEmpty()) {
            StringBuilder b = new StringBuilder(t("\n\nНастройки, которые ничего не меняют (мод работает без них):",
                                                  "\n\nSettings that change nothing (the mod works without them):"));
            int shown = 0;
            for (String l : optionNotes) {
                if (shown++ >= 8) { b.append(t("\n… и ещё ", "\n… and ")).append(optionNotes.size() - 8).append(t("", " more")); break; }
                b.append("\n• ").append(l);
            }
            placed.text += b;
        }
        return placed;
    }

    /*
     * The result window's lines past the folder (plan 30.09, M6): a PC mod's
     * card, the script that does not run, and the dead weight - what lies in
     * the mod's folder unused by the port, so the player knows the mod is not
     * all there.
     */
    static String afterword(List<String> dead, List<String> notTaken, boolean script, boolean fromPc, String id) {
        StringBuilder b = new StringBuilder();
        if (fromPc)
            b.append(t("\n\nМод для ПК: название, версия и описание взяты из metadata.xml, id — «" + id + "».",
                       "\n\nA PC mod: its name, version and description are taken from metadata.xml; its id is \"" + id + "\"."));
        if (script)
            b.append(t("\n\nСкрипт мода не работает: порт скрипты не запускает, поэтому части того, что делает мод, может не быть.",
                       "\n\nThe mod's script does not run: the port runs no scripts, so part of what the mod does may be missing."));
        if (!dead.isEmpty()) {
            b.append(t("\n\nНе используется портом (лежит в папке мода как есть):", "\n\nNot used by the port (left in the mod's folder as it is):"));
            for (String l : deadLines(dead)) b.append("\n• ").append(l);
        }
        if (!notTaken.isEmpty()) {
            b.append(t("\n\nНе распаковано — хранилище не приняло имя:", "\n\nNot unpacked - the storage did not take the name:"));
            int shown = 0;
            for (String l : notTaken) {
                if (shown++ >= 8) { b.append(t("\n… и ещё ", "\n… and ")).append(notTaken.size() - 8).append(t("", " more")); break; }
                b.append("\n• ").append(l);
            }
        }
        return b.toString();
    }

    /* ------------------------------------------------ putting the mod in place (plan 30.09, M7; 02.10, B3) */
    /*
     * WHY THIS IS NOT ONE RENAME ANY MORE. The mods folder is in Android/media,
     * where a rename goes through the system's media store, and on 28.09 it
     * refused: stale index rows on the target path of an empty folder. The
     * delete before it was never checked either, and the player saw "could not
     * put the mod into the mods folder" and nothing else. Now every step is
     * java.nio.file, whose failure names itself (EPERM, ENOTEMPTY, EIO), and
     * has a way around:
     *   1. the old copy of <folder> goes aside, .old-<folder>-<time>;
     *   2. .incoming-<folder> -> <folder>: a rename; failing that, file by
     *      file; failing that, a copy and a delete, file by file. The copy is
     *      marked .adding until it is whole (usermods.c removes an unfinished
     *      one at the start);
     *   3. if <folder> cannot be freed or filled, the mod goes to
     *      <folder>-<time>, and the list names that folder; the old one is
     *      switched off;
     *   4. only now - the new copy whole in its place - the old copies are
     *      marked .remove, to go at the next start (02.10, B3: marked at once,
     *      a kill between the two steps lost the mod entirely; now usermods.c
     *      puts an unmarked old copy back when <folder> is not there);
     *   5. the list is written by the one writer of mods.txt (usermods.c).
     * Honestly: the ways around are ways around. The real guard is that a
     * refusal says the step, the path and the reason, on the screen and in the
     * report - the case of 28.09 is not reproduced by any self-test, since it
     * came from files made past the media store.
     */
    static final class StepFailed extends IOException {
        final String step, path, reason;
        StepFailed(String step, java.nio.file.Path path, Throwable why) {
            super(step + " " + path + ": " + reasonOf(why));
            this.step = step;
            this.path = String.valueOf(path);
            this.reason = reasonOf(why);
        }
        String line() { return "step=" + step + " path=" + path + " reason=" + reason; }
    }

    static String reasonOf(Throwable e) {
        if (e instanceof java.nio.file.FileSystemException) {
            String why = ((java.nio.file.FileSystemException) e).getReason();
            return e.getClass().getSimpleName() + (why != null ? " (" + why + ")" : "");
        }
        return e.getClass().getSimpleName() + (e.getMessage() != null ? " (" + e.getMessage() + ")" : "");
    }

    /* the PLACE step's count of files moved, for the window */
    private static final class Moving {
        final Pacer p;
        final long total;
        long done;
        String how = "rename";
        Moving(Pacer p, long total) { this.p = p; this.total = total; }
        void one() { done++; p.at(PLACE, Math.min(done, total), total, how); }
    }

    /*
     * THE TIME IN THE SET-ASIDE NAMES IS UNIQUE (02.10.2026). Three adds of one
     * mod within a second: the second left .old-<id>-<time> behind (marked, it
     * goes at the next start), and the third's aside step met that very name -
     * FileAlreadyExistsException, and the mod went beside as <id>-<time>. The
     * name keeps its shape (usermods.c old_copy_of reads the last 16 characters
     * as -yyyyMMdd-HHmmss, and the siblings' pattern is <id>-\d{8}-\d{6}), so
     * the time itself moves on, a second at a time, until neither
     * .old-<id>-<time> nor <id>-<time> is in the folder.
     */
    static String freeStamp(java.nio.file.Path mods, String id, long now) {
        java.text.SimpleDateFormat f = new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT);
        for (int k = 0; ; k++) {
            String s = f.format(new java.util.Date(now + k * 1000L));
            if (k >= 100000) return s;      /* not reached: a hundred thousand copies of one mod */
            if (!java.nio.file.Files.exists(mods.resolve(".old-" + id + "-" + s), java.nio.file.LinkOption.NOFOLLOW_LINKS)
                && !java.nio.file.Files.exists(mods.resolve(id + "-" + s), java.nio.file.LinkOption.NOFOLLOW_LINKS))
                return s;
        }
    }

    private static Result putInPlace(Result r, File modsFolder, File incomingFolder, String id, boolean update, String retire,
                                     String name, int files, int toMove, Pacer p) {
        java.nio.file.Path mods = modsFolder.toPath(), incoming = incomingFolder.toPath();
        String stamp = freeStamp(mods, id, System.currentTimeMillis());
        List<String> off = new ArrayList<>();
        List<String> notes = new ArrayList<>();
        List<java.nio.file.Path> toMark = new ArrayList<>();       /* marked .remove only once the new copy stands */
        Moving moving = new Moving(p, toMove);
        p.at(PLACE, 0, toMove, null);
        String target = id;
        java.nio.file.Path place = mods.resolve(id), aside = null;
        boolean replaced = java.nio.file.Files.exists(place, java.nio.file.LinkOption.NOFOLLOW_LINKS);
        /* 1. the old copy aside - not marked yet */
        if (replaced) {
            aside = mods.resolve(".old-" + id + "-" + stamp);
            try {
                java.nio.file.Files.move(place, aside);
                toMark.add(aside);
            } catch (IOException | RuntimeException ex) {
                StepFailed f = new StepFailed("aside", place, ex);
                report("usermods", "add " + id + " failed " + f.line() + " - the new copy goes beside it");
                notes.add(f.line());
                aside = null;
                toMark.add(place);
                off.add(id);
                target = id + "-" + stamp;
            }
        }
        /* <id> now holds nothing of the old copy: whatever lands there is this add's own */
        boolean placeOurs = !replaced || aside != null;
        /* an update's old folder (hotfix 1.73): left whole for this run, off, and gone at the next start */
        if (retire != null && !retire.equals(id) && new File(modsFolder, retire).isDirectory()) {
            toMark.add(mods.resolve(retire));
            off.add(retire);
        }
        /* the same mod's earlier copies beside <id>: off, and gone at the next start */
        File[] kids = modsFolder.listFiles();
        if (kids != null) for (File k : kids) {
            String n = k.getName();
            if (k.isDirectory() && !n.equals(target) && n.matches(java.util.regex.Pattern.quote(id) + "-\\d{8}-\\d{6}")) {
                toMark.add(k.toPath());
                off.add(n);
            }
        }
        /* 2. into place, and 3. beside it when <id> will not take it */
        List<StepFailed> failed = new ArrayList<>();
        String how = moveInto(incoming, mods.resolve(target), failed, moving);
        if (how == null && target.equals(id)) {
            target = id + "-" + stamp;
            java.nio.file.Path dest = mods.resolve(target);
            /* what a half-done move put into <id> goes along first; what will not go stays marked .adding */
            if (java.nio.file.Files.isDirectory(place, java.nio.file.LinkOption.NOFOLLOW_LINKS))
                moveInto(place, dest, failed, moving);
            if (java.nio.file.Files.exists(place, java.nio.file.LinkOption.NOFOLLOW_LINKS)) off.add(id);
            how = moveInto(incoming, dest, failed, moving);
        }
        for (StepFailed f : failed) report("usermods", "add " + id + " failed " + f.line());
        /* the new copy is whole: it is a mod from now on */
        if (how != null) {
            try {
                java.nio.file.Files.deleteIfExists(mods.resolve(target).resolve(ADDING));
            } catch (IOException | RuntimeException ex) {
                failed.add(new StepFailed("ready", mods.resolve(target), ex));
                how = null;
            }
        }
        if (how == null) {
            /* nothing took it: what this add put down goes, the old copy back where it was; nothing was marked */
            try { deleteTree(mods.resolve(id + "-" + stamp)); } catch (StepFailed f) { report("usermods", "add " + id + ": " + f.line()); }
            if (placeOurs) try { deleteTree(place); } catch (StepFailed f) { report("usermods", "add " + id + ": " + f.line()); }
            if (aside != null) {
                try {
                    java.nio.file.Files.move(aside, place);
                } catch (IOException | RuntimeException ex) {
                    /* left unmarked: usermods.c puts it back at the next start (B3) */
                    report("usermods", "add " + id + ": the old copy stays aside - " + new StepFailed("back", aside, ex).line());
                }
            }
            try { deleteTree(incoming); } catch (StepFailed f) { report("usermods", "add " + id + ": " + f.line()); }
            StringBuilder b = new StringBuilder(t("Не удалось положить мод «" + name + "» в папку модов.\n",
                    "Could not put mod \"" + name + "\" into the mods folder.\n"));
            for (StepFailed f : failed)
                b.append("• ").append(t("шаг ", "step ")).append(f.step).append(": ").append(f.path)
                 .append(" — ").append(f.reason).append('\n');
            r.text = b.toString().trim();
            return r;
        }
        p.at(PLACE, toMove, toMove, how);
        /* 4. the old copies marked, now that the new one stands */
        for (java.nio.file.Path m : toMark) mark(m, notes);
        /* 5. the list, by its one writer */
        p.at(LIST, 0, 1, null);
        String listError;
        try {
            /* an update keeps the replaced copy's place in the order and its on/off; a new mod goes first, on */
            listError = listPut(modsFolder.getAbsolutePath(), target, off.toArray(new String[0]),
                                update ? (retire != null ? retire : id) : null);
        } catch (Throwable ex) {                /* the layer is not there: the folder alone is enough, it joins on */
            listError = reasonOf(ex);
        }
        p.at(LIST, 1, 1, null);
        if (listError != null && listError.startsWith(LIST_FULL)) {
            /* a new mod past the most the list holds (hotfix 1.74): no start takes its folder (read_list stops at
               MODS_MAX) and no restart would apply it - so it is not "added": its folder goes at the next start */
            mark(mods.resolve(target), notes);
            report("usermods", "add " + id + " into " + target + ": " + listError + " - refused, the folder marked");
            String most = listError.replaceAll("\\D+", "");
            r.text = t("Мод «" + name + "» не добавлен: список модов полон (" + most + ") — больше игра не берёт. "
                            + "Удалите ненужный мод на его странице и добавьте этот снова.",
                       "Mod \"" + name + "\" was not added: the list already holds " + most + " mods, the most the game takes. "
                            + "Delete a mod you do not need on its page, then add this one again.");
            return r;
        }
        report("usermods", "add " + id + " into " + target + " by " + how
                + (off.isEmpty() ? "" : ", off: " + off) + (listError != null ? ", mods.txt NOT written: " + listError : ""));
        r.ok = true;
        r.updated = update && (replaced || retire != null);
        r.folder = target;
        StringBuilder b = new StringBuilder(update
            ? t("Мод «" + name + "» обновлён: " + files + " файлов. Его место в списке и вкл/выкл остались прежними.",
                "Mod \"" + name + "\" updated: " + files + " files. It keeps its place in the list and its ON/OFF.")
            : t("Мод «" + name + "» добавлен: " + files + " файлов. Он включён и стоит первым в списке.",
                "Mod \"" + name + "\" added: " + files + " files. It is on and first in the list."));
        b.append(t("\n\nПапка мода: " + target, "\n\nThe mod's folder: " + target));
        if (!target.equals(id))
            b.append(t("\n\nПапку «" + id + "» освободить не удалось — мод лёг в «" + target + "», старая копия выключена и уйдёт при запуске.",
                       "\n\nThe folder \"" + id + "\" could not be freed - the mod went to \"" + target + "\"; the old copy is off and goes at the next start."));
        if (listError != null)
            b.append(t("\n\nСписок модов (mods.txt) не записан: " + listError + ". Мод всё равно будет включён.",
                       "\n\nThe mod list (mods.txt) was not written: " + listError + ". The mod will be on all the same."));
        if (!failed.isEmpty() || !notes.isEmpty()) {
            b.append(t("\n\nПо пути:", "\n\nOn the way:"));
            for (StepFailed f : failed) b.append("\n• ").append(f.step).append(": ").append(f.path).append(" — ").append(f.reason);
            for (String n : notes) b.append("\n• ").append(n);
        }
        r.text = b.toString();
        return r;
    }

    /*
     * A folder into target: a rename when the target is free, else file by
     * file, else a copy of what is left; into a target that exists the files
     * are merged. A target filled file by file is marked .adding first, so a
     * copy cut short is never taken for a mod. The way it went, or null.
     */
    private static String moveInto(java.nio.file.Path from, java.nio.file.Path to, List<StepFailed> failed, Moving moving) {
        if (!java.nio.file.Files.exists(to, java.nio.file.LinkOption.NOFOLLOW_LINKS)) {
            try {
                java.nio.file.Files.move(from, to);
                moving.done = moving.total;
                moving.p.at(PLACE, moving.total, moving.total, "rename");
                return "rename";
            } catch (IOException | RuntimeException ex) {
                failed.add(new StepFailed("rename", to, ex));
            }
        }
        try {
            java.nio.file.Files.createDirectories(to);
            java.nio.file.Path mark = to.resolve(ADDING);
            if (!java.nio.file.Files.exists(mark)) java.nio.file.Files.createFile(mark);
        } catch (IOException | RuntimeException ex) {
            failed.add(new StepFailed("mark", to, ex));
            return null;
        }
        moving.how = "move-file";
        try {
            moveTree(from, to, false, moving);
            deleteTree(from);
            return "move-file";
        } catch (StepFailed f) {
            failed.add(f);
        }
        moving.how = "copy";
        try {
            moveTree(from, to, true, moving);       /* what the move left, copied */
            deleteTree(from);
            return "copy";
        } catch (StepFailed f) {
            failed.add(f);
        }
        return null;
    }

    private static void moveTree(java.nio.file.Path from, java.nio.file.Path to, boolean copy, Moving moving) throws StepFailed {
        String step = copy ? "copy" : "move-file";
        try {
            java.nio.file.Files.createDirectories(to);
        } catch (IOException | RuntimeException ex) {
            throw new StepFailed(step, to, ex);
        }
        List<java.nio.file.Path> kids = new ArrayList<>();
        try (java.nio.file.DirectoryStream<java.nio.file.Path> d = java.nio.file.Files.newDirectoryStream(from)) {
            for (java.nio.file.Path k : d) kids.add(k);
        } catch (IOException | RuntimeException ex) {
            throw new StepFailed(step, from, ex);
        }
        for (java.nio.file.Path k : kids) {
            java.nio.file.Path there = to.resolve(k.getFileName().toString());
            if (java.nio.file.Files.isDirectory(k, java.nio.file.LinkOption.NOFOLLOW_LINKS)) { moveTree(k, there, copy, moving); continue; }
            try {
                if (copy) {
                    java.nio.file.Files.copy(k, there, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                    java.nio.file.Files.delete(k);
                } else {
                    java.nio.file.Files.move(k, there, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
                }
            } catch (IOException | RuntimeException ex) {
                throw new StepFailed(step, there, ex);
            }
            if (!k.getFileName().toString().equals(ADDING)) moving.one();
        }
    }

    /* a folder to go at the next start (usermods.c, REMOVE_MARK) */
    private static void mark(java.nio.file.Path dir, List<String> notes) {
        try {
            java.nio.file.Path m = dir.resolve(".remove");
            if (!java.nio.file.Files.exists(m)) java.nio.file.Files.createFile(m);
        } catch (IOException | RuntimeException ex) {
            StepFailed f = new StepFailed("mark", dir, ex);
            notes.add(f.line());
            report("usermods", "add: " + f.line());
        }
    }

    /* ------------------------------------------------ the layer's side (usermods.c) */

    /* mods.txt: `first` on and at the top - or, for an update, at the place and with the on/off of the copy
       in `keepFrom` - and the folders in `off` switched off; null, or why it was not written */
    static native String listPut(String modsDir, String first, String[] off, String keepFrom);
    /* listPut's answer when the list holds all the mods it can (usermods.c MODS_MAX) and the mod is a new one */
    static final String LIST_FULL = "the list is full";
    /* the start has built its index (usermods.c gameReady); ADD MOD is at work (installing) - THE START, below */
    private static native boolean gameReady();
    private static native void installing(boolean on);
    /* RESTART TO APPLY is owed (usermods.c restartOwed: what a start would index is not this run's) */
    private static native boolean restartOwed();

    /* the add window's RESTART TO APPLY: when the MODS page's row would stand; with no layer, after any mod put in */
    static boolean restartIsOwed() {
        try { return restartOwed(); } catch (Throwable ignored) { return true; }
    }

    /*
     * THE START (hotfix 1.74). Android may end the game while its file picker is open (a phone short of
     * memory); the picked archives then come to a new process, before its surface and setConfigDir. The
     * batch waits for the start's index - so what it puts in is this run's change, pale and with RESTART TO
     * APPLY, never taken half into the index - and says it is at work, so the start, should it come all the
     * same, leaves its .incoming- and .adding folders alone. Up to `ms`; with no layer (the desk) at once.
     */
    static long waitForGame(long ms) {
        long t0 = System.nanoTime(), end = t0 + ms * 1_000_000L;
        try {
            while (!gameReady()) {
                if (System.nanoTime() >= end) {
                    report("usermods", "add: the start's index is not there after " + ms + " ms - going on");
                    break;
                }
                Thread.sleep(50);
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Throwable ignored) {
            return 0;                       /* the layer is not loaded */
        }
        long waited = (System.nanoTime() - t0) / 1_000_000L;
        if (waited >= 50) report("usermods", "add: waited " + waited + " ms for the start's index");
        return waited;
    }

    static void markInstalling(boolean on) {
        try { installing(on); } catch (Throwable ignored) { /* the layer is not loaded */ }
    }

    /* the app's own folder: the copy of mods.txt inside the app goes to <files>/state */
    static native void innerDir(String filesDir);
    private static native void nativeReport(String kind, String text);
    /* json.c's reading of a mod.json (modcheck_jni.c): its issues as UTF-8 lines "level [code]: text", empty for none */
    private static native byte[] checkModJson(byte[] text);
    /* json.c's lenient reading of a mod.json as strict JSON text (modcheck_jni.c), null when it cannot be read */
    private static native byte[] strictModJson(byte[] text);

    /* the mod.json as json.c reads it, in strict JSON for org.json; null when there is no judge or no reading */
    static String strict(byte[] text) {
        try {
            byte[] b = strictModJson(text);
            return b == null ? null : new String(b, java.nio.charset.StandardCharsets.UTF_8);
        } catch (Throwable ignored) {
            return null;
        }
    }

    /* the issues json.c finds in a mod.json, "" for none; null when there is no judge (the layer is not loaded) */
    static String judge(byte[] text) {
        try {
            byte[] b = checkModJson(text);
            return b == null ? null : new String(b, java.nio.charset.StandardCharsets.UTF_8);
        } catch (Throwable ignored) {
            return null;
        }
    }
    static native boolean reportActive();

    /* a line into the tester's report, when one is open; never a failure of its own */
    static void report(String kind, String text) {
        try { nativeReport(kind, text); } catch (Throwable ignored) { /* the layer is not loaded */ }
    }

    static boolean reporting() {
        try { return reportActive(); } catch (Throwable ignored) { return false; }
    }

    /* named before setConfigDir (MainActivity); never a failure of its own */
    static void setInnerDir(File filesDir) {
        try { if (filesDir != null) innerDir(filesDir.getAbsolutePath()); } catch (Throwable ignored) { /* the layer is not loaded */ }
    }

    private static byte[] readAll(InputStream in, int limit) throws IOException {
        java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int k;
        while ((k = in.read(buf)) > 0) {
            b.write(buf, 0, k);
            if (b.size() > limit) break;
        }
        return b.toByteArray();
    }

    /* a folder and everything in it; the first thing that would not go names itself */
    static void deleteTree(java.nio.file.Path p) throws StepFailed {
        if (!java.nio.file.Files.exists(p, java.nio.file.LinkOption.NOFOLLOW_LINKS)) return;
        if (java.nio.file.Files.isDirectory(p, java.nio.file.LinkOption.NOFOLLOW_LINKS)) {
            List<java.nio.file.Path> kids = new ArrayList<>();
            try (java.nio.file.DirectoryStream<java.nio.file.Path> d = java.nio.file.Files.newDirectoryStream(p)) {
                for (java.nio.file.Path k : d) kids.add(k);
            } catch (IOException | RuntimeException ex) {
                throw new StepFailed("delete", p, ex);
            }
            for (java.nio.file.Path k : kids) deleteTree(k);
        }
        try {
            java.nio.file.Files.deleteIfExists(p);
        } catch (IOException | RuntimeException ex) {
            throw new StepFailed("delete", p, ex);
        }
    }

    static void deleteTree(File f) {
        if (f == null) return;
        try { deleteTree(f.toPath()); } catch (StepFailed e) { report("usermods", "delete failed " + e.line()); }
    }
}
