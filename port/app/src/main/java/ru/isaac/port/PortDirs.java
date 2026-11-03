package ru.isaac.port;

import android.content.Context;

import java.io.File;

/*
 * THE PORT'S FOLDER, ONE PER PROCESS (P3, 30.09.2026): Android/media/<app>.
 *
 * It holds everything a person may touch by hand - port-touch.ini, mods/,
 * shared/, and since 30.09 the saves with the game's options.ini (saves/). It
 * used to be worked out in two places (MainActivity.portDir and the documents
 * provider), each of which quietly fell back to Android/data/<app>/files when
 * getExternalMediaDirs() gave null for a moment at start - and then the config
 * was not there, the defaults were written, and the next launch found the
 * real folder again: settings "reset every other time" (the skeptic's
 * candidate, P1.8). Now there is one answer, worked out once:
 *
 *   - getExternalMediaDirs()[0], the media folder Android gives the app;
 *   - failing that, the same path worked out from getExternalFilesDir -
 *     /Android/data/<app>/files becomes /Android/media/<app>;
 *   - failing that, NO folder, and the reason is shown on the screen. The game
 *     does not start in a folder that is not its own.
 *
 * `.nomedia` lies in its root (P5): the media scanner leaves mods/, shared/ and
 * saves/ alone, so a mod's pictures never reach the gallery.
 */
final class PortDirs {
    private PortDirs() { }

    private static File port;
    private static String how = "";
    private static String problem;

    static synchronized File port(Context c) {
        if (port != null) return port;
        File dir = null;
        try {
            File[] media = c.getExternalMediaDirs();
            if (media != null && media.length > 0 && media[0] != null) {
                dir = media[0];
                how = "media";
            }
        } catch (Throwable ignored) { }
        if (dir == null) {
            File files = c.getExternalFilesDir(null);
            String tail = "/Android/data/" + c.getPackageName() + "/files";
            if (files != null && files.getAbsolutePath().endsWith(tail)) {
                String p = files.getAbsolutePath();
                dir = new File(p.substring(0, p.length() - tail.length()) + "/Android/media/" + c.getPackageName());
                how = "media, worked out from the files folder";
            }
        }
        if (dir == null) {
            problem = UserMods.t("Android не дал игре папку в Android/media (общее хранилище недоступно).",
                    "Android gave the game no folder in Android/media (the shared storage is not available).");
            return null;
        }
        dir.mkdirs();
        if (!dir.isDirectory()) {
            problem = UserMods.t("Не удалось создать папку игры: ", "The game's folder could not be created: ") + dir;
            return null;
        }
        problem = null;
        port = dir;
        return dir;
    }

    /* how the folder was found, for the report */
    static synchronized String how() { return how; }

    /* why there is no folder, or null */
    static synchronized String problem() { return problem; }

    /* the saves: the game's /Documents, with Repentance/ inside */
    static File saves(Context c) {
        File p = port(c);
        return p != null ? new File(p, "saves") : null;
    }

    /*
     * `.nomedia` in the root: "present", "created" or why not. A fresh one asks
     * the scanner to look at the folder once, so pictures it had already taken
     * in are marked hidden. The rows are never deleted from MediaStore: a row
     * deleted there takes its file with it.
     */
    static String nomedia(Context c) {
        File p = port(c);
        if (p == null) return "no folder";
        File mark = new File(p, ".nomedia");
        if (mark.exists()) return "present";
        try {
            if (!mark.createNewFile() && !mark.exists()) return "could NOT be created";
        } catch (java.io.IOException e) {
            return "could NOT be created: " + e.getMessage();
        }
        try {
            android.media.MediaScannerConnection.scanFile(c, new String[] { p.getAbsolutePath() }, null, null);
        } catch (Throwable ignored) { }
        return "created";
    }

    /* how many pictures of the folder MediaStore still lists (report builds); -1 when it will not say */
    static int picturesListed(Context c) {
        if (android.os.Build.VERSION.SDK_INT < 29) return -1;
        try (android.database.Cursor cur = c.getContentResolver().query(
                android.provider.MediaStore.Images.Media.EXTERNAL_CONTENT_URI,
                new String[] { android.provider.MediaStore.MediaColumns._ID },
                android.provider.MediaStore.MediaColumns.RELATIVE_PATH + " LIKE ?",
                new String[] { "Android/media/" + c.getPackageName() + "/%" }, null)) {
            return cur != null ? cur.getCount() : -1;
        } catch (Throwable t) {
            return -1;
        }
    }
}
