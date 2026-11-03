package ru.isaac.port;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

/*
 * DESK RUN OF THE CONVERTER INSIDE THE INSTALLER (ModPort.java, 04.10.2026): the very code the phone
 * runs at ADD MOD, on a mod zip from disk, with the game reference the APK carries.
 *
 *   ConvertDesk <gameref.bin> <mod zip> <out zip>
 *
 * Writes <out zip> (nothing for a mod with nothing for the port) and <out zip>.report.txt, as
 * tools/modport.py does, and prints the time and the heap the run took (tools/umdesk/test_converters.py).
 */
public final class ConvertDesk {
    public static void main(String[] a) throws Exception {
        long t0 = System.nanoTime();
        ModPort.Ref ref;
        try (InputStream in = new FileInputStream(a[0])) { ref = ModPort.Ref.load(in); }
        long t1 = System.nanoTime();
        final File zip = new File(a[1]);
        UserMods.Source src = new UserMods.Source() {
            @Override public InputStream open() throws java.io.IOException {
                return new java.io.BufferedInputStream(new FileInputStream(zip), 1 << 16);
            }
            @Override public long size() { return zip.length(); }
        };
        File out = new File(a[2]);
        out.delete();
        final long[] peak = {0};
        ModPort.Outcome o = ModPort.convert(src, ref, out, (phase, done, total, what) -> {
            Runtime r = Runtime.getRuntime();
            peak[0] = Math.max(peak[0], r.totalMemory() - r.freeMemory());
        });
        try (FileOutputStream f = new FileOutputStream(a[2] + ".report.txt")) {
            f.write(o.report.getBytes(StandardCharsets.UTF_8));
        }
        long t2 = System.nanoTime();
        System.out.println("java: " + (o.empty ? "no zip (nothing for the port)" : o.files + " files") + ", id " + o.id
                + "; reference " + (t1 - t0) / 1_000_000 + " ms, convert " + (t2 - t1) / 1_000_000 + " ms, heap seen up to "
                + (peak[0] >> 20) + " MB of " + (Runtime.getRuntime().maxMemory() >> 20) + " MB");
    }
}
