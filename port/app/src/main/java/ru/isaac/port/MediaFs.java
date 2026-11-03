package ru.isaac.port;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Set;

/*
 * THE ONE RULE FOR WRITING A FILE, ON THE JAVA SIDE (P1, 30.09.2026) - the
 * same as shim.c's shim_safe_fopen/shim_safe_fclose, so a file written from
 * either side can be finished by the same start-up recovery:
 *
 *   1. name.tmp gets the whole content and is flushed to the disk (fsync);
 *   2. rename(name.tmp, name);
 *   3. if rename is refused (the media folder answers through MediaProvider,
 *      which did refuse on 28.09), an empty name.ready says "the .tmp is
 *      whole" and the content is written over name in place, flushed;
 *   4. .tmp and .ready go.
 *
 * A file whose rename was refused once is written in place for the rest of the
 * process. A .tmp that could not be written whole is removed and the old file
 * stands; an in-place copy that broke off leaves .tmp and .ready, and the next
 * start (shim_safe_recover_dir) puts the whole copy in place.
 */
final class MediaFs {
    private MediaFs() { }

    private static final Set<String> IN_PLACE = new HashSet<>();

    private static synchronized boolean inPlace(File f) { return IN_PLACE.contains(f.getAbsolutePath()); }

    private static synchronized void markInPlace(File f) { IN_PLACE.add(f.getAbsolutePath()); }

    static void write(File target, byte[] data) throws IOException {
        File tmp = new File(target.getPath() + ".tmp");
        File ready = new File(target.getPath() + ".ready");
        ready.delete();                                  /* a mark from before belongs to no copy now */
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
            out.flush();
            out.getFD().sync();
        } catch (IOException e) {
            tmp.delete();
            throw e;
        }
        if (tmp.length() != data.length) {
            tmp.delete();
            throw new IOException("the copy came out " + tmp.length() + " bytes of " + data.length);
        }
        if (!inPlace(target)) {
            if (tmp.renameTo(target)) return;
            markInPlace(target);
        }
        try (FileOutputStream mark = new FileOutputStream(ready)) {
            mark.getFD().sync();
        } catch (IOException e) {
            tmp.delete();                                /* no mark, no in-place copy: the old file stands */
            throw e;
        }
        /* from here a copy cut short is finished at the next start from the .tmp */
        try (FileOutputStream out = new FileOutputStream(target)) {
            out.write(data);
            out.flush();
            out.getFD().sync();
        }
        if (target.length() != data.length)
            throw new IOException("written in place, " + target.length() + " bytes of " + data.length);
        tmp.delete();
        ready.delete();
    }

    static byte[] read(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            return readAll(in);
        }
    }

    static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
        return out.toByteArray();
    }

    /* a file copied whole, checked by its size, with its time kept */
    static void copy(File from, File to) throws IOException {
        byte[] d = read(from);
        write(to, d);
        if (to.length() != from.length())
            throw new IOException(to.getName() + ": " + to.length() + " bytes where " + from.length() + " belong");
        to.setLastModified(from.lastModified());
    }

    /* a folder copied file by file; answers the files and bytes; the half-written leftovers stay behind */
    static long[] copyTree(File from, File to) throws IOException {
        long[] got = {0, 0};
        File[] items = from.listFiles();
        if (items == null) return got;
        to.mkdirs();
        for (File item : items) {
            String n = item.getName();
            if (n.endsWith(".tmp") || n.endsWith(".ready")) continue;
            File target = new File(to, n);
            if (item.isDirectory()) {
                long[] sub = copyTree(item, target);
                got[0] += sub[0];
                got[1] += sub[1];
                continue;
            }
            copy(item, target);
            got[0]++;
            got[1] += item.length();
        }
        return got;
    }

    static void deleteTree(File dir, boolean self) {
        File[] items = dir.listFiles();
        if (items != null) for (File item : items) {
            if (item.isDirectory()) deleteTree(item, true); else item.delete();
        }
        if (self) dir.delete();
    }
}
