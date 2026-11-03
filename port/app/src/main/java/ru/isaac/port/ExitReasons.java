package ru.isaac.port;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/*
 * WHY THE LAST RUN ENDED, AS THE SYSTEM SAW IT - in every build (02.10.2026).
 *
 * A game killed for its memory leaves no crash, no report line and no mark of
 * ours: there is no signal to catch. The system does keep the reason, for the
 * last few deaths of each package - ActivityManager.getHistoricalProcessExitReasons
 * (Android 11+, our own package needs no permission): LOW_MEMORY, CRASH_NATIVE
 * with the signal, ANR, SIGNALED..., PSS/RSS at the moment of death, and for a
 * native crash on Android 12+ the tombstone itself (getTraceInputStream, a
 * protobuf), for an ANR the thread dump (text).
 *
 * Every start, on a thread of its own, the exits newer than the newest one
 * already written go to Android/media/<app>/last-exit.txt (the previous file
 * becomes last-exit.old.txt first). Nothing new - nothing is touched, so a
 * crash stays readable however many clean starts follow it. The newest written
 * time is kept inside the app (files/last-exit.stamp), and only after the file
 * is safely down - a write that failed is tried again next start. All writes go
 * through MediaFs, the one safe writer. The report build also gets a summary.
 *
 * The tombstone is decoded here into text (signal, fault address, abort
 * message, the crashing thread's registers and frames, the mappings those
 * addresses fall into, the last log lines - "image: ... landed at" among them),
 * because a protobuf in a folder people open by hand is no use to anybody. If
 * the decoding finds no signal at all, the readable strings of the stream are
 * written instead, so nothing the system kept is lost to a schema mismatch.
 * The whole file stays under 64 KB.
 */
final class ExitReasons {
    private ExitReasons() { }

    interface Say { void say(String line); }

    static final String FILE = "last-exit.txt";
    static final String OLD = "last-exit.old.txt";
    private static final String STAMP = "last-exit.stamp";
    private static final int MAX_EXITS = 16;
    private static final int FILE_CAP = 64 * 1024;
    private static final int TRACE_READ_CAP = 2 << 20;      /* a tombstone with maps and logs is a few hundred KB */

    private static final AtomicBoolean DONE = new AtomicBoolean();

    /* once per process, off the main thread */
    static void record(Context context, Say say) {
        if (!DONE.compareAndSet(false, true)) return;
        final Context app = context.getApplicationContext();
        new Thread(() -> {
            try {
                run(app, say);
            } catch (Throwable t) {
                if (say != null) say.say("exit reasons: failed - " + t);
            }
        }, "exit reasons").start();
    }

    private static void run(Context c, Say say) throws Exception {
        if (android.os.Build.VERSION.SDK_INT < 30) {
            if (say != null) say.say("exit reasons: Android " + android.os.Build.VERSION.SDK_INT
                    + " keeps none (API 30+)");
            return;
        }
        collect(c, say);
    }

    @android.annotation.TargetApi(30)
    private static void collect(Context c, Say say) throws Exception {
        ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) {
            if (say != null) say.say("exit reasons: no ActivityManager");
            return;
        }
        List<ApplicationExitInfo> all = am.getHistoricalProcessExitReasons(c.getPackageName(), 0, MAX_EXITS);
        File stampFile = new File(c.getFilesDir(), STAMP);
        long stamp = readStamp(stampFile);
        List<ApplicationExitInfo> fresh = new ArrayList<>();
        long newest = stamp;
        for (ApplicationExitInfo e : all) {
            if (e.getTimestamp() > stamp) fresh.add(e);
            if (e.getTimestamp() > newest) newest = e.getTimestamp();
        }
        if (say != null) {
            say.say("exit reasons: the system keeps " + all.size() + ", new since the last start "
                    + fresh.size());
            for (int i = 0; i < all.size() && i < 3; i++) {
                ApplicationExitInfo e = all.get(i);
                say.say("exit reasons: " + (e.getTimestamp() > stamp ? "NEW " : "") + brief(e));
            }
        }
        if (fresh.isEmpty()) return;

        File port = PortDirs.port(c);
        if (port == null) {
            if (say != null) say.say("exit reasons: no port folder, " + FILE + " not written");
            return;
        }
        String text = describe(c, fresh, all.size(), stamp);
        byte[] data = text.getBytes(StandardCharsets.UTF_8);
        File file = new File(port, FILE);
        if (file.isFile()) {
            try {
                MediaFs.write(new File(port, OLD), MediaFs.read(file));
            } catch (Throwable t) {
                if (say != null) say.say("exit reasons: the previous " + FILE + " was not kept - " + t);
            }
        }
        MediaFs.write(file, data);
        MediaFs.write(stampFile, Long.toString(newest).getBytes(StandardCharsets.UTF_8));
        if (say != null) say.say("exit reasons: " + FILE + " written, " + data.length + " bytes, "
                + fresh.size() + " exits");
    }

    private static long readStamp(File f) {
        try {
            return Long.parseLong(new String(MediaFs.read(f), StandardCharsets.UTF_8).trim());
        } catch (Throwable t) {
            return 0;
        }
    }

    private static String when(long ms) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS Z", Locale.US).format(new Date(ms));
    }

    static String reasonName(int r) {
        switch (r) {
            case 0: return "UNKNOWN";
            case 1: return "EXIT_SELF";
            case 2: return "SIGNALED";
            case 3: return "LOW_MEMORY";
            case 4: return "CRASH";
            case 5: return "CRASH_NATIVE";
            case 6: return "ANR";
            case 7: return "INITIALIZATION_FAILURE";
            case 8: return "PERMISSION_CHANGE";
            case 9: return "EXCESSIVE_RESOURCE_USAGE";
            case 10: return "USER_REQUESTED";
            case 11: return "USER_STOPPED";
            case 12: return "DEPENDENCY_DIED";
            case 13: return "OTHER";
            case 14: return "FREEZER";
            case 15: return "PACKAGE_STATE_CHANGE";
            case 16: return "PACKAGE_UPDATED";
            default: return "reason " + r;
        }
    }

    private static String signalName(int s) {
        switch (s) {
            case 4: return "SIGILL";
            case 5: return "SIGTRAP";
            case 6: return "SIGABRT";
            case 7: return "SIGBUS";
            case 8: return "SIGFPE";
            case 9: return "SIGKILL";
            case 11: return "SIGSEGV";
            case 15: return "SIGTERM";
            default: return "signal " + s;
        }
    }

    /* what the status number means for this reason */
    private static String status(int reason, int status) {
        if (reason == 2 || reason == 5) return signalName(status) + " (" + status + ")";
        return "status " + status;
    }

    @android.annotation.TargetApi(30)
    private static String brief(ApplicationExitInfo e) {
        return when(e.getTimestamp()) + " " + e.getProcessName() + " pid " + e.getPid() + ": "
                + reasonName(e.getReason()) + ", " + status(e.getReason(), e.getStatus())
                + ", pss " + (e.getPss() >> 10) + " MB, rss " + (e.getRss() >> 10) + " MB"
                + (e.getDescription() != null ? ", \"" + e.getDescription() + "\"" : "");
    }

    @android.annotation.TargetApi(30)
    private static String describe(Context c, List<ApplicationExitInfo> fresh, int kept, long stamp) {
        StringBuilder b = new StringBuilder();
        b.append("Why the game's last runs ended, as Android recorded it\n");
        b.append("written ").append(when(System.currentTimeMillis())).append(", Android ")
                .append(android.os.Build.VERSION.RELEASE).append(" (API ").append(android.os.Build.VERSION.SDK_INT)
                .append("), ").append(android.os.Build.MANUFACTURER).append(' ').append(android.os.Build.MODEL)
                .append('\n');
        b.append("the system keeps ").append(kept).append(" exits; new since ")
                .append(stamp == 0 ? "ever" : when(stamp)).append(": ").append(fresh.size()).append("\n\n");
        for (ApplicationExitInfo e : fresh) {
            b.append("== ").append(brief(e)).append('\n');
            b.append("   importance ").append(e.getImportance()).append(", pss ").append(e.getPss())
                    .append(" KB, rss ").append(e.getRss()).append(" KB\n");
            byte[] summary = e.getProcessStateSummary();
            if (summary != null && summary.length > 0)
                b.append("   state summary: ").append(new String(summary, StandardCharsets.UTF_8)).append('\n');
            b.append("   ").append(e).append('\n');
        }
        /* the traces, newest first, while there is room */
        for (ApplicationExitInfo e : fresh) {
            int room = FILE_CAP - b.length() - 256;
            if (room < 2048) break;
            String trace = trace(e, room);
            if (trace == null) continue;
            b.append("\n== trace of ").append(when(e.getTimestamp())).append(" pid ").append(e.getPid())
                    .append(" (").append(reasonName(e.getReason())).append(")\n");
            b.append(trace);
            if (trace.length() > 0 && trace.charAt(trace.length() - 1) != '\n') b.append('\n');
        }
        if (b.length() > FILE_CAP) {
            b.setLength(FILE_CAP - 40);
            b.append("\n... cut at 64 KB\n");
        }
        return b.toString();
    }

    @android.annotation.TargetApi(30)
    private static String trace(ApplicationExitInfo e, int room) {
        byte[] raw;
        try (InputStream in = e.getTraceInputStream()) {
            if (in == null) return null;
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            for (int n; out.size() < TRACE_READ_CAP && (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
            raw = out.toByteArray();
        } catch (Throwable t) {
            return "(the trace could not be read: " + t + ")\n";
        }
        if (raw.length == 0) return null;
        String text;
        if (e.getReason() == 5 && !looksLikeText(raw)) {
            text = Tombstone.decode(raw, room);
        } else {
            text = new String(raw, 0, Math.min(raw.length, room), StandardCharsets.UTF_8);
        }
        return text.length() > room ? text.substring(0, room) + "\n... cut\n" : text;
    }

    private static boolean looksLikeText(byte[] d) {
        int n = Math.min(d.length, 512), bad = 0;
        for (int i = 0; i < n; i++) {
            int v = d[i] & 0xff;
            if (v < 9 || (v > 13 && v < 32)) bad++;
        }
        return bad * 20 < n;
    }

    /*
     * The tombstone protobuf (system/core/debuggerd/proto/tombstone.proto), read
     * by field number with a reader of the wire format and nothing else:
     * Tombstone 2 fingerprint, 4 time, 5 pid, 6 tid, 9 command line, 10 signal
     * (1 number, 2 name, 3 code, 4 code name, 8 has fault address, 9 fault
     * address), 14 abort message, 15 cause (1 text), 16 threads (map: 1 tid,
     * 2 thread), 17 mappings, 18 log buffers (1 name, 2 lines). Thread: 1 id,
     * 2 name, 3 registers (1 name, 2 value), 4 frames (1 rel_pc, 2 pc, 4
     * function, 5 offset in it, 6 file). Mapping: 1 begin, 2 end, 3 offset,
     * 4 r, 5 w, 6 x, 7 name, 9 load bias. Log line: 1 time, 3 tid, 4 priority,
     * 5 tag, 6 text.
     */
    static final class Tombstone {
        private Tombstone() { }

        /* one field: number, wire type, the value or the bytes' place */
        static final class F {
            int num, type;
            long v;
            int off, len;
        }

        static List<F> fields(byte[] d, int off, int len) {
            List<F> out = new ArrayList<>();
            int p = off, end = off + len;
            try {
                while (p < end) {
                    long key = 0;
                    int shift = 0;
                    for (;;) {
                        int b = d[p++] & 0xff;
                        key |= (long) (b & 0x7f) << shift;
                        if (b < 0x80) break;
                        shift += 7;
                    }
                    F f = new F();
                    f.num = (int) (key >>> 3);
                    f.type = (int) (key & 7);
                    if (f.type == 0) {
                        long v = 0;
                        shift = 0;
                        for (;;) {
                            int b = d[p++] & 0xff;
                            v |= (long) (b & 0x7f) << shift;
                            if (b < 0x80) break;
                            shift += 7;
                        }
                        f.v = v;
                    } else if (f.type == 1) {
                        long v = 0;
                        for (int i = 0; i < 8; i++) v |= (long) (d[p + i] & 0xff) << (8 * i);
                        p += 8;
                        f.v = v;
                    } else if (f.type == 5) {
                        long v = 0;
                        for (int i = 0; i < 4; i++) v |= (long) (d[p + i] & 0xff) << (8 * i);
                        p += 4;
                        f.v = v;
                    } else if (f.type == 2) {
                        long n = 0;
                        shift = 0;
                        for (;;) {
                            int b = d[p++] & 0xff;
                            n |= (long) (b & 0x7f) << shift;
                            if (b < 0x80) break;
                            shift += 7;
                        }
                        if (n < 0 || p + n > end) break;    /* cut short: keep what came whole */
                        f.off = p;
                        f.len = (int) n;
                        p += (int) n;
                    } else {
                        break;                                /* groups are not used by this schema */
                    }
                    out.add(f);
                }
            } catch (ArrayIndexOutOfBoundsException cut) {
                /* the stream ended inside a field: what came before stands */
            }
            return out;
        }

        static String str(byte[] d, F f) {
            return f.type == 2 ? new String(d, f.off, f.len, StandardCharsets.UTF_8) : "";
        }

        static List<F> sub(byte[] d, F f) {
            return f.type == 2 ? fields(d, f.off, f.len) : new ArrayList<>();
        }

        static F first(List<F> fs, int num) {
            for (F f : fs) if (f.num == num) return f;
            return null;
        }

        static long num(List<F> fs, int num) {
            F f = first(fs, num);
            return f != null && f.type != 2 ? f.v : 0;
        }

        static String text(byte[] d, List<F> fs, int num) {
            F f = first(fs, num);
            return f != null ? str(d, f) : "";
        }

        static String hex(long v) { return "0x" + Long.toHexString(v); }

        static String decode(byte[] d, int room) {
            StringBuilder b = new StringBuilder();
            List<F> top = fields(d, 0, d.length);
            F sig = first(top, 10);
            if (sig == null) return strings(d, room);
            List<F> s = sub(d, sig);
            b.append("tombstone ").append(text(d, top, 4)).append(", pid ").append(num(top, 5))
                    .append(", tid ").append(num(top, 6)).append('\n');
            StringBuilder cmd = new StringBuilder();
            for (F f : top) if (f.num == 9) cmd.append(str(d, f)).append(' ');
            if (cmd.length() > 0) b.append("command: ").append(cmd.toString().trim()).append('\n');
            b.append("signal ").append(num(s, 1)).append(" (").append(text(d, s, 2)).append("), code ")
                    .append(num(s, 3)).append(" (").append(text(d, s, 4)).append(')');
            if (num(s, 8) != 0) b.append(", fault address ").append(hex(num(s, 9)));
            b.append('\n');
            String abort = text(d, top, 14);
            if (!abort.isEmpty()) b.append("abort: ").append(abort).append('\n');
            for (F f : top) if (f.num == 15) b.append("cause: ").append(text(d, sub(d, f), 1)).append('\n');

            /* the mappings, for placing addresses */
            List<long[]> maps = new ArrayList<>();
            List<String> mapNames = new ArrayList<>();
            for (F f : top) {
                if (f.num != 17) continue;
                List<F> m = sub(d, f);
                maps.add(new long[] { num(m, 1), num(m, 2), num(m, 3), num(m, 4), num(m, 5), num(m, 6), num(m, 9) });
                mapNames.add(text(d, m, 7));
            }

            long crashTid = num(top, 6);
            java.util.Set<Long> placed = new java.util.LinkedHashSet<>();   /* each address once */
            StringBuilder others = new StringBuilder();
            int otherCount = 0;
            for (F f : top) {
                if (f.num != 16) continue;
                List<F> entry = sub(d, f);
                F th = first(entry, 2);
                if (th == null) continue;
                List<F> t = sub(d, th);
                long id = num(t, 1);
                String name = text(d, t, 2);
                List<F> frames = new ArrayList<>();
                for (F x : t) if (x.num == 4) frames.add(x);
                if (id == crashTid) {
                    b.append("\ncrashing thread ").append(id).append(" \"").append(name).append("\"\n");
                    int k = 0;
                    StringBuilder regs = new StringBuilder();
                    for (F x : t) {
                        if (x.num != 3) continue;
                        List<F> r = sub(d, x);
                        String rn = text(d, r, 1);
                        long rv = num(r, 2);
                        regs.append(String.format(Locale.US, "  %-4s %016x", rn, rv));
                        if (++k % 4 == 0) regs.append('\n');
                        if (rn.equals("pc") || rn.equals("lr") || rn.equals("x30")) placed.add(rv);
                    }
                    if (regs.length() > 0) b.append(regs).append(k % 4 != 0 ? "\n" : "");
                    for (F x : t) if (x.num == 7) b.append("  note: ").append(str(d, x)).append('\n');
                    int i = 0;
                    for (F x : frames) {
                        List<F> fr = sub(d, x);
                        long pc = num(fr, 2);
                        if (i < 4) placed.add(pc);
                        b.append(String.format(Locale.US, "  #%02d pc %016x rel %08x  %s", i, pc, num(fr, 1),
                                text(d, fr, 6)));
                        String fn = text(d, fr, 4);
                        if (!fn.isEmpty()) b.append(" (").append(fn).append('+').append(num(fr, 5)).append(')');
                        b.append('\n');
                        if (++i >= 64) break;
                    }
                } else if (otherCount < 48) {
                    otherCount++;
                    others.append("  ").append(id).append(" \"").append(name).append('"');
                    int i = 0;
                    for (F x : frames) {
                        List<F> fr = sub(d, x);
                        others.append(i == 0 ? ": " : " < ");
                        String fn = text(d, fr, 4);
                        String file = text(d, fr, 6);
                        int slash = file.lastIndexOf('/');
                        others.append(fn.isEmpty() ? (slash >= 0 ? file.substring(slash + 1) : file)
                                + "+" + Long.toHexString(num(fr, 1)) : fn);
                        if (++i >= 3) break;
                    }
                    others.append('\n');
                }
            }
            if (num(s, 8) != 0) placed.add(num(s, 9));
            if (!placed.isEmpty() && !maps.isEmpty()) {
                b.append("\nwhere those addresses lie (pc, lr, the top frames, the fault address):\n");
                for (Long a : placed) {
                    int at = -1;
                    for (int i = 0; i < maps.size(); i++) {
                        long[] m = maps.get(i);
                        if (Long.compareUnsigned(a, m[0]) >= 0 && Long.compareUnsigned(a, m[1]) < 0) { at = i; break; }
                    }
                    if (at < 0) {
                        b.append(String.format(Locale.US, "  %016x  in no mapping\n", a));
                        continue;
                    }
                    long[] m = maps.get(at);
                    b.append(String.format(Locale.US, "  %016x  = %016x + %x in %016x-%016x %s%s%s off %x  %s\n",
                            a, m[0], a - m[0], m[0], m[1], m[3] != 0 ? "r" : "-", m[4] != 0 ? "w" : "-",
                            m[5] != 0 ? "x" : "-", m[2], mapNames.get(at)));
                }
            }
            if (others.length() > 0) b.append("\nother threads:\n").append(others);

            /* the last log lines the tombstone carries: the image's base is written there */
            List<String> lines = new ArrayList<>();
            for (F f : top) {
                if (f.num != 18) continue;
                List<F> buf = sub(d, f);
                String bufName = text(d, buf, 1);
                for (F x : buf) {
                    if (x.num != 2) continue;
                    List<F> l = sub(d, x);
                    lines.add(text(d, l, 1) + " " + bufName + " " + num(l, 3) + " "
                            + "VDIWEF".charAt((int) Math.max(0, Math.min(5, num(l, 4) - 2))) + " "
                            + text(d, l, 5) + ": " + text(d, l, 6));
                }
            }
            if (!lines.isEmpty()) {
                b.append("\nlog (image base lines first, then the last lines):\n");
                int from = Math.max(0, lines.size() - 120);
                for (int i = 0; i < from; i++)
                    if (lines.get(i).contains("landed at")) b.append("  ").append(lines.get(i)).append('\n');
                for (int i = from; i < lines.size(); i++) b.append("  ").append(lines.get(i)).append('\n');
            }
            b.append("\nmappings: ").append(maps.size()).append(", tombstone ").append(d.length).append(" bytes\n");
            return b.length() > room ? b.substring(0, room) : b.toString();
        }

        /* nothing decoded: every readable string of the stream, so nothing is lost */
        static String strings(byte[] d, int room) {
            StringBuilder b = new StringBuilder("(not decoded as a tombstone; its readable strings follow)\n");
            int start = -1;
            for (int i = 0; i <= d.length && b.length() < room; i++) {
                int v = i < d.length ? d[i] & 0xff : 0;
                boolean ok = v >= 32 && v < 127;
                if (ok && start < 0) start = i;
                if (!ok && start >= 0) {
                    if (i - start >= 6) b.append(new String(d, start, i - start, StandardCharsets.US_ASCII)).append('\n');
                    start = -1;
                }
            }
            return b.length() > room ? b.substring(0, room) : b.toString();
        }
    }
}
