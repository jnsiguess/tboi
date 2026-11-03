package ru.isaac.port;

import java.util.Locale;

/*
 * THE KINDS OF FILE THE EXPORT OFFERS, ONE TABLE (plan 30.09, M12). The
 * dialog's lines, which extensions each takes, whether it is ticked when the
 * dialog opens, and how its files go into the zip - all here, so the scan,
 * the export, its self-check and the dialog cannot disagree (the old table
 * was spread over GameFiles.KIND_NAMES, KIND_EXTS and a test of extensions in
 * dump()).
 *
 * Shaders (Repentance's own, repentance-resources/data/shaders) are a kind of
 * their own and OFF by default: the port draws with its own versions of some
 * of them (hq4x.fs), a mod rarely wants them and a broken one blacks the
 * screen out - they are there for whoever ticks them.
 */
final class FileKinds {
    private FileKinds() { }

    static final class Kind {
        final String ru, en;
        final String[] exts;
        final boolean on;               /* ticked when the dialog opens */
        final String[] stored;          /* compressed already: into the zip as they are */
        Kind(String ru, String en, String[] exts, boolean on, String[] stored) {
            this.ru = ru; this.en = en; this.exts = exts; this.on = on; this.stored = stored;
        }
    }

    static final Kind[] ALL = {
        new Kind("Графика (png, anm2)", "Graphics (png, anm2)", new String[] {"png", "anm2"}, true, new String[] {"png"}),
        new Kind("Звуки (wav)", "Sounds (wav)", new String[] {"wav"}, true, new String[0]),
        new Kind("Музыка (ogg)", "Music (ogg)", new String[] {"ogg"}, true, new String[] {"ogg"}),
        new Kind("Данные, тексты и шрифты (xml, stb, sta, fnt)", "Data, texts and fonts (xml, stb, sta, fnt)",
                 new String[] {"xml", "stb", "sta", "fnt"}, true, new String[0]),
        new Kind("Видео (ogv, srt)", "Video (ogv, srt)", new String[] {"ogv", "srt"}, true, new String[] {"ogv"}),
        new Kind("Шейдеры (fs, vs) — для знающих", "Shaders (fs, vs) - for those who know them",
                 new String[] {"fs", "vs"}, false, new String[0]),
    };

    static int count() { return ALL.length; }

    static String ext(String path) {
        int dot = path.lastIndexOf('.'), slash = path.lastIndexOf('/');
        return dot > slash ? path.substring(dot + 1).toLowerCase(Locale.ROOT) : "";
    }

    /* the kind of a file by its extension, or -1: a file the export does not offer */
    static int of(String path) {
        String e = ext(path);
        for (int k = 0; k < ALL.length; k++)
            for (String x : ALL[k].exts) if (x.equals(e)) return k;
        return -1;
    }

    static String name(int k) { return UserMods.t(ALL[k].ru, ALL[k].en); }

    static String[] names() {
        String[] n = new String[ALL.length];
        for (int k = 0; k < n.length; k++) n[k] = name(k);
        return n;
    }

    static boolean on(int k) { return ALL[k].on; }

    /* is it stored (compressed already) rather than deflated */
    static boolean stored(String path) {
        int k = of(path);
        if (k < 0) return false;
        String e = ext(path);
        for (String x : ALL[k].stored) if (x.equals(e)) return true;
        return false;
    }
}
