package ru.isaac.port;

import android.app.Activity;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

/**
 * The port's reconnaissance: hand the game image to the loader and give it a
 * real surface to draw on.
 *
 * The work runs on a thread of its own: the game holds that thread itself, as
 * it would hold the event loop on iOS, and the layout thread must not be hung
 * on it.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {

    /*
     * The library is loaded here, under interception, rather than in a static
     * block.
     *
     * The reason: two complaints of "closes right on the tap on the icon" from
     * other people's phones and not a line of log. A failure in a static block
     * kills the app before the first line of our own code — there is nothing
     * left to report the cause with and nobody to report it to. Under
     * interception we write it to a file next to the saves and show it on the
     * screen.
     */
    private static boolean layerLoaded;

    static final String REPORT_NAME = "isaac-start.txt";
    /*
     * The report of the launch before this one. A crash's lines are written into
     * the report of the run that crashed, and the next launch used to empty that
     * file first thing - so a crash the tester restarted after was gone before
     * anyone could read it (the owner's eighth round, 24.09.2026: the gamepad
     * crash left nothing). Now the old report is copied here before the new one
     * starts.
     */
    static final String REPORT_PREVIOUS = "isaac-previous.txt";

    /**
     * Whether this build writes a startup report at all.
     *
     * The report is what made the crash on other people's phones findable: the
     * stages, the self-test, the registers and the walk back through the frames
     * all go into Downloads/isaac-start.txt. None of that belongs in a build
     * meant for playing — it leaves a file in Downloads on every launch and
     * costs about a second of startup.
     *
     * It is one flag rather than a build type on purpose. The build that found
     * this bug was a RELEASE build with the report on, because that is what a
     * tester can install; tying the report to the debug build would have made
     * that impossible. Set it back to true to make another such build.
     */
    private static final boolean REPORT = true;

    /* how far the test frame sits from the window's corner; zero at full screen */
    private float frameOffX, frameOffY;

    /* an exact frame in pixels, from `aspect = WxH`; zero when a shape was asked for */
    private int frameW, frameH;

    /*
     * The aspect asked for in port-touch.ini, or zero for the whole screen.
     * Read here rather than passed from the layer: the frame has to be sized
     * before the surface exists, and the layer is not up yet.
     */
    private double testAspect() {
        try {
            java.io.File dir = portDir();
            if (dir == null) return 0.0;
            java.io.File ini = new java.io.File(dir, "port-touch.ini");
            if (!ini.exists()) return 0.0;
            java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(ini));
            String line;
            double got = 0.0;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.startsWith("#") || !line.toLowerCase().startsWith("aspect")) continue;
                int eq = line.indexOf('=');
                if (eq < 0) continue;
                String v = line.substring(eq + 1).trim();
                int colon = v.indexOf(':');
                int by = v.toLowerCase().indexOf('x');
                try {
                    if (by > 0) {
                        /*
                         * W x H asks for a frame in PIXELS rather than a shape,
                         * which is a different question and sometimes the only
                         * useful one. A ratio reproduces another phone's number
                         * of UNITS but not its number of pixels per unit: on a
                         * 3120 x 1440 screen `2460:1080` is 615 units at five
                         * and a bit pixels each, while `2460x1080` is 615 units
                         * at exactly four - the same canvas the phone that
                         * reported the fault actually has.
                         */
                        frameW = Integer.parseInt(v.substring(0, by).trim());
                        frameH = Integer.parseInt(v.substring(by + 1).trim());
                        if (frameW < 320 || frameH < 240) frameW = frameH = 0;
                        got = 0.0;
                    } else {
                        got = (colon > 0)
                                ? Double.parseDouble(v.substring(0, colon).trim())
                                  / Double.parseDouble(v.substring(colon + 1).trim())
                                : Double.parseDouble(v);
                    }
                } catch (Throwable ignored) { got = 0.0; frameW = frameH = 0; }
            }
            r.close();
            return (got > 0.5 && got < 5.0) ? got : 0.0;
        } catch (Throwable t) {
            return 0.0;
        }
    }

    /*
     * ONE PROCESS, ONE TRUTH (05.10.2026). Android may build a new MainActivity
     * in a process that is still alive - a language or font-size change, the
     * developer option "Don't keep activities", the system reclaiming a
     * window in the background. The game thread, the layer and everything it
     * was told live on; only the window is new. So whatever describes the
     * process rather than the window - the report, whether the game is paused,
     * whether its sound is held, the vibrators' owner - is kept in statics: a
     * new window reads it and carries on, instead of starting from "playing,
     * not paused" while the layer still sits in the old window's pause - which
     * was a game frozen in its pause and silent for good.
     */
    private static android.content.Context appContext;                  /* the application's, never a window's */
    private static int windows;                                         /* MainActivity instances this process has had */
    private static android.os.ParcelFileDescriptor reportFile;   /* held open for the native side */
    private static android.net.Uri reportUri;                    /* what Java reopens to append */
    private static java.io.File reportBackingFile;               /* the pre-API-29 fallback */

    /**
     * Open the startup report in Downloads and keep it open for the whole run.
     *
     * Downloads, and not a folder of ours, because of a measurement: the old
     * file came out as `-rw-rw---- u0_a312 media_rw` — no read bit for anybody
     * else — and a MediaStore query for its name answered "No result found"
     * while the neighbouring saves were indexed. So it was invisible twice
     * over, which is why two testers were told to send a file they could not
     * see. A row we create in Downloads is indexed by the act of creating it,
     * needs no permission, and shows up in the phone's own Files app.
     *
     * The descriptor stays open and goes down to loader.c as well, so the
     * stages from here and the crash line from the signal handler land in the
     * same visible file as they happen. That is what makes one launch enough:
     * nothing has to be copied anywhere afterwards, and nothing overwrites what
     * an earlier launch recorded.
     *
     * Below API 29 there is no Downloads collection, so the report falls back to
     * a plain file next to the saves — the phones in this test are all newer.
     */
    private void openReport() {
        if (!REPORT || reportFile != null || reportUri != null) return;   /* a new window: the run's report goes on */
        /*
         * Two attempts, each caught on its own. If MediaStore throws — and a
         * phone whose storage misbehaves is exactly the phone we are chasing —
         * the fallback below must still run, so its failure cannot take the
         * fallback down with it.
         */
        try {
            if (android.os.Build.VERSION.SDK_INT >= 29) {
                android.content.ContentResolver cr = getContentResolver();
                android.net.Uri collection = android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI;
                android.net.Uri item = null;
                /*
                 * An existing row of ours is reused and truncated rather than
                 * deleted and remade: deleting first leaves a window with no
                 * file at all, and the system would otherwise append "(1)" to a
                 * second file of the same name.
                 */
                try (android.database.Cursor c = cr.query(collection,
                        new String[]{ android.provider.MediaStore.MediaColumns._ID },
                        android.provider.MediaStore.MediaColumns.DISPLAY_NAME + "=?",
                        new String[]{ REPORT_NAME }, null)) {
                    if (c != null && c.moveToFirst())
                        item = android.content.ContentUris.withAppendedId(collection, c.getLong(0));
                }
                if (item == null) {
                    android.content.ContentValues v = new android.content.ContentValues();
                    v.put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, REPORT_NAME);
                    v.put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                    item = cr.insert(collection, v);
                }
                if (item != null) {
                    keepPrevious(cr, collection, item);
                    /* empty it once, so last launch's report is not appended to */
                    try (java.io.OutputStream os = cr.openOutputStream(item, "wt")) {
                        if (os != null) os.flush();
                    }
                    reportUri = item;
                    /*
                     * Append mode for the descriptor the native side keeps. Every
                     * write then goes to the end of the file whatever the offset
                     * is, so Java reopening the same file between native writes
                     * cannot make the two overwrite each other.
                     */
                    reportFile = cr.openFileDescriptor(item, "wa");
                }
            }
        } catch (Throwable t) {
            android.util.Log.e("isaac", "Downloads did not take the report", t);
            reportUri = null;
        }
        try {
            if (reportFile == null) {
                /* beside the config; getExternalMediaDirs() would create a media folder just by being asked */
                java.io.File dir = getExternalFilesDir(null);
                if (dir != null) {
                    dir.mkdirs();
                    reportBackingFile = new java.io.File(dir, REPORT_NAME);
                    java.io.File previous = new java.io.File(dir, REPORT_PREVIOUS);
                    previous.delete();
                    if (!reportBackingFile.renameTo(previous)) reportBackingFile.delete();
                    reportFile = android.os.ParcelFileDescriptor.open(reportBackingFile,
                            android.os.ParcelFileDescriptor.MODE_WRITE_ONLY
                                    | android.os.ParcelFileDescriptor.MODE_CREATE
                                    | android.os.ParcelFileDescriptor.MODE_APPEND);
                }
            }
        } catch (Throwable t) {
            android.util.Log.e("isaac", "the report did not open", t);
        }
    }

    /* the last launch's report, copied aside before this launch empties it */
    private void keepPrevious(android.content.ContentResolver cr, android.net.Uri collection,
                              android.net.Uri item) {
        try {
            android.net.Uri target = null;
            try (android.database.Cursor c = cr.query(collection,
                    new String[]{ android.provider.MediaStore.MediaColumns._ID },
                    android.provider.MediaStore.MediaColumns.DISPLAY_NAME + "=?",
                    new String[]{ REPORT_PREVIOUS }, null)) {
                if (c != null && c.moveToFirst())
                    target = android.content.ContentUris.withAppendedId(collection, c.getLong(0));
            }
            if (target == null) {
                android.content.ContentValues v = new android.content.ContentValues();
                v.put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, REPORT_PREVIOUS);
                v.put(android.provider.MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                target = cr.insert(collection, v);
            }
            if (target == null) return;
            try (java.io.InputStream in = cr.openInputStream(item);
                 java.io.OutputStream out = cr.openOutputStream(target, "wt")) {
                if (in == null || out == null) return;
                byte[] buf = new byte[65536];
                for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
            }
        } catch (Throwable t) {
            android.util.Log.e("isaac", "the previous report was not kept", t);
        }
    }

    /**
     * One line into the report.
     *
     * The file is opened for the line and closed again, and that is deliberate.
     * MediaStore fills in a row's size when the descriptor is closed: with one
     * held open for the whole run the size column stays NULL, and a file manager
     * showing a nought-byte file is exactly the kind of thing that makes a
     * tester say the report is not there. Closing after every line keeps the
     * size honest, and appending means it never fights the descriptor the native
     * side is holding.
     */
    /*
     * A PANEL MODE SIXTY DIVIDES (21.10.2026, the renderer audit). The game's
     * speed is its number of frames, sixty a second. On a 90 Hz panel sixty
     * pictures land on vsyncs one and two apart by turns - every step of the
     * game on screen 11.1 ms, then 22.2 - and no timing of ours can make that
     * even: the Tecno Spark Go 2024, the Moto G50 and the Galaxy A15 judder so
     * at every RENDER SCALE, in every version (desk: 5.9-6.1 ms of judder a
     * picture on 90 Hz, 0.6 on 60). So the window asks for a display mode of
     * the same size whose rate sixty divides - 60, 120, 180, 240 - when the one
     * in force is not such, the fastest of them. This is a mode, not a frame
     * rate: the setFrameRate(60, FIXED_SOURCE) of old let the platform throttle
     * the app's vsync to a divisor and locked a 90 Hz tester at 30 (gl.c); a
     * mode only changes the panel's own rate.
     */
    private void sixtyMode() {
        if (android.os.Build.VERSION.SDK_INT < 23) return;
        try {
            android.view.Display d = getWindowManager().getDefaultDisplay();
            android.view.Display.Mode now = d.getMode();
            if (sixtyDivides(now.getRefreshRate())) return;
            android.view.Display.Mode best = null;
            for (android.view.Display.Mode m : d.getSupportedModes()) {
                if (m.getPhysicalWidth() != now.getPhysicalWidth()
                        || m.getPhysicalHeight() != now.getPhysicalHeight()) continue;
                if (!sixtyDivides(m.getRefreshRate())) continue;
                if (best == null || m.getRefreshRate() > best.getRefreshRate()) best = m;
            }
            if (REPORT) say("display: the mode in force is " + now.getRefreshRate() + " Hz"
                    + (best == null ? " - no mode sixty divides, left as it is"
                                    : ", asked for " + best.getRefreshRate() + " Hz (mode " + best.getModeId() + ")"));
            if (best == null) return;
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            getWindow().setAttributes(lp);
        } catch (Throwable t) {
            if (REPORT) say("display: the mode was not asked for - " + t);
        }
    }

    private static boolean sixtyDivides(float hz) {
        int k = Math.round(hz / 60f);
        return k >= 1 && Math.abs(hz - 60f * k) <= 0.5f * k;
    }

    private static void say(String line) {
        byte[] bytes;
        try {
            bytes = (line + "\n").getBytes("UTF-8");
        } catch (Throwable t) {
            return;
        }
        try {
            if (reportUri != null) {
                try (java.io.OutputStream os = appContext.getContentResolver().openOutputStream(reportUri, "wa")) {
                    if (os != null) os.write(bytes);
                }
            } else if (reportBackingFile != null) {
                try (java.io.OutputStream os = new java.io.FileOutputStream(reportBackingFile, true)) {
                    os.write(bytes);
                }
            }
        } catch (Throwable ignore) { }
    }

    /** A mark for a stage that was passed: it shows where exactly things broke off. */
    private static void stage(String text) {
        say("stage: " + text);
    }

    private static void writeFailure(String kind, Throwable t) {
        say(kind + ": " + t);
        for (StackTraceElement el : t.getStackTrace()) say("    " + el);
        Throwable cause = t.getCause();
        if (cause != null) say("  cause: " + cause);
    }

    /** Show the reason on the screen itself: no log can be pulled off another phone. */
    private void showFailure(String text) {
        android.widget.TextView t = new android.widget.TextView(this);
        t.setText("The game did not start.\n\n" + text
                + "\n\nThe details are in Downloads/" + REPORT_NAME);
        t.setTextColor(0xFFFFFFFF);
        t.setBackgroundColor(0xFF000000);
        t.setPadding(40, 80, 40, 40);
        t.setTextSize(14);
        setContentView(t);
    }

    private native void setSurface(Surface surface);
    private native void touch(int id, int phase, float x, float y);
    private native void soundPause(boolean on);
    private native void setConfigDir(String path);
    private native void setAssets(android.content.res.AssetManager assets, String root);
    private native void padState(int slot, boolean connected, int buttons, float[] axes);
    private native void padIdentity(int slot, int androidId, String descriptor);   /* devices.c */
    private native void padFamily(int slot, int family, String iosName);          /* pad.c: PadIcons */
    private native void touchPalm(int id, float x, float y);                        /* devices.c: phase 4, a palm */
    private native void setSavesDir(String path);
    private native void appPause();
    private native void appResume();
    private native void imeInit();
    private native void setReport(int fd);
    private native void imeText(String text);
    private native void imeBackspace();
    private native void configChanged();
    /* the saves' own backup and the writes before a restart (saves.c) */
    private native void setBackupDir(String path);
    private native int requestOptionsSave();
    private native int optionsSaveState(int token);
    private native void savesFreeze();
    private native void savesKeep(String path, String tag);
    private native void savesBackup(String path);
    /* the game's saves queued for the writer thread (safefile.c): on the disk within ms; seal for an import */
    private native boolean savesFlush(int ms, boolean seal);
    private native String probe(byte[] image, String home, int w, int h, float density, String bundle);

    // the game image is mapped once per process, not once per window
    private static Thread worker;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        appContext = getApplicationContext();
        if (windows++ > 0) {
            /* the same process, a new window: nothing about the run starts again */
            if (REPORT) say("window: a new MainActivity in the same process (#" + windows + "): game "
                    + (playing ? "running" : "paused") + ", sound " + (soundHeld ? "held" : "free")
                    + (inCall ? ", a call is on" : "") + ", haptics " + (haptics != null ? "kept" : "none")
                    + ", pads kept " + pads.size() + " (slots " + slotsText() + ")");
            startGame();
            return;
        }

        openReport();
        say("Isaac port startup report");
        say("device: " + android.os.Build.MANUFACTURER + " "
                + android.os.Build.MODEL + ", Android "
                + android.os.Build.VERSION.RELEASE
                + " (API " + android.os.Build.VERSION.SDK_INT + ")");
        /*
         * The chip and the build. Two phones fail where this one does not, and
         * these are the coarse differences worth having written down before
         * anything finer is blamed.
         */
        say("build: " + android.os.Build.HARDWARE + ", " + android.os.Build.DEVICE
                + ", abis " + java.util.Arrays.toString(android.os.Build.SUPPORTED_ABIS)
                + ", fingerprint " + android.os.Build.FINGERPRINT);
        /*
         * The memory is written at once: if the system does not like the game's
         * appetite it will kill the app silently — there will be neither a
         * message nor a crash report, and from outside that is
         * indistinguishable from a break of our own. Then only the stages and
         * these numbers will be left in the file.
         */
        try {
            android.app.ActivityManager am =
                    (android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE);
            if (am != null) {
                android.app.ActivityManager.MemoryInfo mi =
                        new android.app.ActivityManager.MemoryInfo();
                am.getMemoryInfo(mi);
                say("memory: total " + (mi.totalMem >> 20) + " MB, free "
                        + (mi.availMem >> 20) + " MB, threshold " + (mi.threshold >> 20)
                        + " MB, low=" + mi.lowMemory
                        + ", heap limit " + am.getMemoryClass() + " MB");
            }
            say("room in phone storage: free "
                    + (getFilesDir().getUsableSpace() >> 20) + " MB");
        } catch (Throwable t) {
            android.util.Log.e("isaac", "the phone details did not reach the report", t);
        }
        if (REPORT)
            android.widget.Toast.makeText(this, "Report: Downloads/" + REPORT_NAME,
                    android.widget.Toast.LENGTH_LONG).show();
        Thread.setDefaultUncaughtExceptionHandler((thread, t) -> {
            writeFailure("BROKE OFF on thread " + thread.getName(), t);
            /* the saves the game has already made are on the writer's queue: a moment for them first */
            if (layerLoaded) try { savesFlush(1000, false); } catch (Throwable ignored) { }
            android.os.Process.killProcess(android.os.Process.myPid());
        });

        startGame();
    }

    /** Everything the report setup is not: load the layer and put up the window. */
    private void startGame() {
        if (!layerLoaded) {
            try {
                System.loadLibrary("isaacport");
                layerLoaded = true;
            } catch (Throwable t) {
                writeFailure("THE LAYER DID NOT LOAD", t);
                showFailure("The port library did not load:\n" + t);
                return;
            }
        }
        setReport(reportFile != null ? reportFile.getFd() : -1);
        stage("layer loaded");
        /*
         * The one owner of the vibrators (Haptics, batch 3): bound before the
         * settings are read and before the game thread exists, so the layer's
         * first word about RUMBLE already has somewhere to go.
         */
        if (haptics == null) {
            haptics = new Haptics(this, REPORT ? MainActivity::say : null);
            haptics.start();
        }
        /*
         * The pads' layouts (PadDb, assets/paddb.txt - SDL's gamepad database,
         * zlib): read once, before the first passport names a pad's layout.
         */
        if (PadDb.shared() == PadDb.EMPTY) {
            PadDb.load(getAssets(), android.os.Build.VERSION.SDK_INT);
            if (REPORT) say("paddb: " + PadDb.shared().size() + " layouts of " + PadDb.shared().lines
                    + " lines (" + PadDb.shared().skippedSdk + " not for API " + android.os.Build.VERSION.SDK_INT + ")");
        }
        /*
         * THE PORT'S FOLDER OR NOTHING (P3): the saves, options.ini and the
         * config all live there now, and a game started anywhere else would
         * spread the player's progress over two places. So with no folder the
         * reason is on the screen instead.
         */
        if (PortDirs.port(this) == null) {
            say("config: NO port folder - " + PortDirs.problem());
            showFailure(PortDirs.problem());
            return;
        }
        /* `.nomedia` before anything is copied or unpacked into the folder (P5) */
        nomediaState = PortDirs.nomedia(this);
        ExitReasons.record(this, REPORT ? MainActivity::say : null);   /* why the last run ended: last-exit.txt, any build */

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        sixtyMode();
        /*
         * A display cutout must not trim the frame: the game keeps its own
         * margins. The field itself came with Android 9; before it no screen
         * had a cutout, so there is nothing to ask for and the field is not
         * there to be written.
         */
        if (android.os.Build.VERSION.SDK_INT >= 28) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        SurfaceView view = new SurfaceView(this);
        view.getHolder().addCallback(this);
        /* an invisible input field sits over the frame — the seeds go through it */
        /*
         * The root sees every key before the IME does (K1, K9): the spawner's
         * card takes a keyboard's keys there, and the probe counts what the
         * IME lets through. It holds the focus, so the keys pass it.
         */
        android.widget.FrameLayout root = new android.widget.FrameLayout(this) {
            @Override public boolean dispatchKeyEventPreIme(KeyEvent e) {
                return keyBeforeIme(e) || super.dispatchKeyEventPreIme(e);
            }
        };
        root.setFocusable(true);
        root.setFocusableInTouchMode(true);
        /* focused, but never drawn as focused: Android 8's highlight would tint the frame */
        if (android.os.Build.VERSION.SDK_INT >= 26) root.setDefaultFocusHighlightEnabled(false);
        /*
         * A frame of a chosen shape, for trying the layout at other aspects.
         *
         * The canvas widens to 270 * the window's ratio, so everything the port
         * lays out - the menus, the sticks, the button row - depends on that
         * ratio and on nothing else. To try another one, the FRAME is made that
         * shape rather than the phone: the largest rectangle of the wanted
         * ratio that fits, centred, with the rest of the screen left black. The
         * game then genuinely runs at that ratio, with square units, and the
         * picture is not stretched to fill the phone the way a display-size
         * override would stretch it.
         *
         * `aspect` in port-touch.ini, as W:H or a plain number - 16:9, 21:9,
         * 16:10 - and anything else, or nothing, leaves the whole screen. As
         * WxH it is a frame in pixels instead, clamped to the screen, which is
         * how another phone's window is reproduced exactly rather than only in
         * shape.
         */
        android.widget.FrameLayout.LayoutParams lp =
                new android.widget.FrameLayout.LayoutParams(
                        android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                        android.view.ViewGroup.LayoutParams.MATCH_PARENT);
        double want = testAspect();          /* fills frameW/frameH for a pixel frame */
        if (want > 0.0 || frameW > 0) {
            DisplayMetrics dm0 = getResources().getDisplayMetrics();
            double sw = Math.max(dm0.widthPixels, dm0.heightPixels);
            double sh = Math.min(dm0.widthPixels, dm0.heightPixels);
            int w = (int) sw, h = (int) sh;
            if (frameW > 0) {
                w = Math.min(frameW, (int) sw);
                h = Math.min(frameH, (int) sh);
            } else if (sw / sh > want) {
                w = (int) Math.round(sh * want);
            } else {
                h = (int) Math.round(sw / want);
            }
            lp = new android.widget.FrameLayout.LayoutParams(w, h);
            lp.gravity = android.view.Gravity.CENTER;
            /*
             * A finger is reported in the WINDOW's coordinates, and the frame no
             * longer fills the window: without this the whole layout answers
             * taps shifted by half the difference, which looks exactly like the
             * layout being wrong. Zero when the frame is the whole screen, so
             * the ordinary build is untouched.
             */
            frameOffX = (float) ((sw - w) / 2.0);
            frameOffY = (float) ((sh - h) / 2.0);
            stage("frame forced to " + w + " x " + h
                    + ", touches moved by " + frameOffX + "," + frameOffY);
        }
        root.setBackgroundColor(0xFF000000);
        root.addView(view, lp);
        field = makeField();
        root.addView(field, new android.widget.FrameLayout.LayoutParams(1, 1));
        setContentView(root);
        root.requestFocus();
        imeInit();
        if (REPORT) keysScanTable(KeyMap.HID_OF_SCAN);     /* the table against the game's own, at start (K9) */
        hideBars();
        stage("window created");
    }

    /*
     * Whether the game is being played right now, as opposed to merely alive.
     *
     * On iOS the system warns the app the moment anything covers it, and the
     * game saves the run and stops on that warning. On Android the only warning
     * that ever arrives is onPause, and it arrives only when we really leave.
     *
     * The notification shade is NOT one of those moments here, and that is
     * measured, not assumed: on the owner's phone every standard road stays
     * silent while the shade is open — window focus, onTopResumedActivity-
     * Changed, the insets listener, the root insets polled every 200 ms, and
     * the touch stream (neither a cancel nor the obscured flag). The panel is
     * drawn over us without the application being told anything at all, so
     * there is nothing here to hang a pause on, and we do not guess at one.
     */
    private static boolean playing = true;      /* the process's: see ONE PROCESS, ONE TRUTH */

    /*
     * Set by the import before the process is ended: the game holds the old
     * saves in memory, and the save a pause makes would write them straight
     * back over the files just imported.
     */
    private volatile boolean quitting;

    private void stopPlaying() {
        if (!layerLoaded || !playing || quitting) return;
        playing = false;
        /*
         * The whole iOS ceremony, save included — Android may freeze us at any
         * moment and the run has to be on disk.
         */
        appPause();
        soundHold("the app went to the background");
        watchCalls(false);
        releasePads();
        releaseKeys("the app was left");   /* a key held as the app is left is let go in the game */
        book.tellCounters();            /* the devices' counters, when the app is left (U4) */
    }

    private void startPlaying() {
        if (!layerLoaded || playing) return;
        playing = true;
        inCall = callNow();             /* a call still going on keeps the sound held: no blip of sound first */
        soundHold("the app came back");
        appResume();
        book.scanAll();                 /* every device's passport, once (U4) */
        syncPads();                     /* a pad could have been plugged in while we were away */
    }

    @Override
    /*
     * Going into the background. The game has to be told: on iOS the system
     * warns it, and on that warning it saves the run. We wait for it to be
     * handled — otherwise Android would freeze the process before the file was
     * written, and quitting the game would let a bad room be replayed.
     */
    protected void onPause() {
        super.onPause();
        stopPlaying();
        watchPads(false);               /* the window's listener: off with every pause, quitting or not */
        /* every motor still, and the game's late shakes dropped until we are back (P9-A) */
        if (haptics != null) haptics.pause(true);
    }

    /*
     * No longer seen: the saves the pause made are on the disk by now
     * (shim_app_pause waits for them); whatever the game wrote after that is
     * waited for here, before Android may end the process (hotfix 1.72).
     */
    @Override
    protected void onStop() {
        super.onStop();
        if (layerLoaded) savesFlush(1500, false);
    }

    /*
     * A window going for good (a rebuilt one, or the app closed): its device
     * listeners go with it, so the old window is not kept alive by the input
     * service and does not answer for the new one. The pads stay (statics).
     */
    @Override
    protected void onDestroy() {
        watchPads(false);
        if (keyboardWatch != null) {
            android.hardware.input.InputManager im =
                    (android.hardware.input.InputManager) getSystemService(INPUT_SERVICE);
            if (im != null) im.unregisterInputDeviceListener(keyboardWatch);
            keyboardWatch = null;
        }
        super.onDestroy();
    }

    @Override
    protected void onResume() {
        super.onResume();
        /* back from the file manager: the config read again if it was changed there */
        if (configStamp != 0) {
            java.io.File ini = configFile();
            if (ini != null && ini.lastModified() != configStamp) configChanged();
            configStamp = 0;
        }
        hideBars();
        if (haptics != null) haptics.pause(false);
        startPlaying();
        /*
         * THE DEVICE WATCH FROM THE FIRST SCREEN ON (06.10.2026). It used to
         * be set by startPlaying alone - which returns at once on the first
         * resume, as `playing` starts true - so until the player first left
         * the app no pad that came, went or changed its mode was heard
         * (1.666 to 1.71 alike). watchPads keeps one listener per window and
         * does nothing when it is set already.
         */
        if (layerLoaded) watchPads(true);   /* the first line-up is the game thread's (syncPads after setAssets) */
        watchCalls(true);          /* after the sound is back, so a call in progress silences it */
    }

    /*
     * Mixing with everything else, as the game does on iOS.
     *
     * The image sets AVAudioSessionCategoryAmbient. An ambient session never
     * interrupts anybody - music from another app keeps playing under the game
     * - and the system silences it for one thing only, a phone call. The port
     * used to ask Android for AUDIOFOCUS_GAIN with a game's attributes instead,
     * and that was both complaints from the testers in one: the request stops
     * whatever was playing the moment the game starts, and the listener that
     * came with it muted the game the moment anything else asked for focus.
     * Neither is what the game does.
     *
     * So no focus is asked for at all. The one interruption ambient audio does
     * honour is listened for, and nothing is polled: from Android 12 the audio
     * mode itself reports its changes (AudioManager.OnModeChangedListener, no
     * permission), and below that the call state does (LISTEN_CALL_STATE,
     * which has needed no permission since Android 6 - the number is simply
     * not passed). Where a phone still refuses, a call plays over the game,
     * as it does over any application that holds no focus.
     *
     * ONLY A REAL CALL (the owner, 05.10.2026: "players complained the game
     * goes silent; silence it only for a real call - now even recording a
     * voice message in a messenger can silence it"). MODE_IN_COMMUNICATION is
     * not a call: Discord and Telegram voice chats hold it for as long as they
     * are open, messengers take it to record a voice message, and VoIP calls
     * use it too - none of that silences the game any more, and the system
     * mixes or ducks as it does for any app. A call is the telephony ringing
     * or in progress: MODE_RINGTONE, MODE_IN_CALL and MODE_CALL_SCREENING (the
     * phone's assistant answering a real call), or below Android 12 the call
     * state, which only telephony sets. No audio focus is taken, so another
     * app's focus request never reaches us at all.
     *
     * The game's sound is held for two reasons, the app being away and a
     * call, and soundHold tells the layer only when the sum changes - with the
     * reason in the report.
     */
    private static boolean inCall;
    private static boolean soundHeld;                    /* what the layer was last told: the process's */

    private void soundHold(String why) {
        if (!layerLoaded) return;
        boolean hold = !playing || inCall;
        if (hold == soundHeld) return;
        soundHeld = hold;
        soundPause(hold);
        if (REPORT) say("audio: the game's sound " + (hold ? "held" : "released") + " - " + why
                + (inCall ? " (a call is on)" : ""));
    }

    /* whether a real call is on right now, asked once (the listeners report the changes) */
    private boolean callNow() {
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            android.media.AudioManager am = audio();
            return am != null && busyMode(am.getMode());
        }
        try {
            android.telephony.TelephonyManager tm =
                    (android.telephony.TelephonyManager) getSystemService(TELEPHONY_SERVICE);
            return tm != null && tm.getCallState() != android.telephony.TelephonyManager.CALL_STATE_IDLE;
        } catch (SecurityException e) {
            return false;               /* this phone wants a permission for it: a call plays over the game */
        }
    }
    private Object modeListener;                         /* AudioManager.OnModeChangedListener, Android 12+ */
    private android.telephony.PhoneStateListener callListener;  /* below that */

    private android.media.AudioManager audio() {
        return (android.media.AudioManager) getSystemService(AUDIO_SERVICE);
    }

    private static boolean busyMode(int mode) {
        return mode == android.media.AudioManager.MODE_RINGTONE
            || mode == android.media.AudioManager.MODE_IN_CALL
            || mode == android.media.AudioManager.MODE_CALL_SCREENING;   /* API 30; the branch is 31+ */
    }

    private void callState(boolean busy) {
        if (busy == inCall) return;
        inCall = busy;
        soundHold(busy ? "a phone call began" : "the phone call ended");
    }

    private void watchCalls(boolean on) {
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            android.media.AudioManager am = audio();
            if (am == null) return;
            if (on) {
                if (modeListener == null) {
                    android.media.AudioManager.OnModeChangedListener l =
                            new android.media.AudioManager.OnModeChangedListener() {
                                @Override public void onModeChanged(int mode) { callState(busyMode(mode)); }
                            };
                    am.addOnModeChangedListener(getMainExecutor(), l);
                    modeListener = l;
                }
                callState(busyMode(am.getMode()));       /* where things stand right now, once */
            } else if (modeListener != null) {
                am.removeOnModeChangedListener((android.media.AudioManager.OnModeChangedListener) modeListener);
                modeListener = null;
                inCall = false;
            }
            return;
        }
        android.telephony.TelephonyManager tm =
                (android.telephony.TelephonyManager) getSystemService(TELEPHONY_SERVICE);
        if (tm == null) return;
        try {
            if (on) {
                if (callListener == null) {
                    callListener = new android.telephony.PhoneStateListener() {
                        @Override public void onCallStateChanged(int state, String number) {
                            callState(state != android.telephony.TelephonyManager.CALL_STATE_IDLE);
                        }
                    };
                }
                tm.listen(callListener, android.telephony.PhoneStateListener.LISTEN_CALL_STATE);
            } else if (callListener != null) {
                tm.listen(callListener, android.telephony.PhoneStateListener.LISTEN_NONE);
                inCall = false;
            }
        } catch (SecurityException e) {
            /* this phone wants a permission for it: then a call plays over the game */
        }
    }

    @Override
    public void onWindowFocusChanged(boolean has) {
        super.onWindowFocusChanged(has);
        if (has) hideBars();       // the bars come back by themselves - hide them again
        else if (layerLoaded) {
            releasePads();          /* whatever was held is not held any more */
            /*
             * The keys too (08.10.2026): a key held as the window loses focus
             * - the notification shade, a system dialog - gets no release of its
             * own, and the game kept it down; with R that is a restart nobody
             * holds (reset.c, the keyboard's hand).
             */
            releaseKeys("the window lost focus");
        }
    }

    /*
     * Seed entry. The game asks the layer for a keyboard, the layer asks us.
     *
     * Android shows it only to whoever has input focus, so we keep an invisible
     * field one pixel across: the system writes the letters there, and we
     * compare the string before and after and hand the layer the difference.
     * Catching key presses directly is not possible — on-screen keyboards do
     * not send them, they insert text at once.
     *
     * The field is given focus only for the time of the input: otherwise it
     * would take it away from the game from the very first frame.
     */
    private android.widget.EditText field;
    private String lastText = "";

    private android.widget.EditText makeField() {
        android.widget.EditText f = new android.widget.EditText(this);
        f.setAlpha(0f);                      /* invisible but alive: a hidden view takes no focus */
        f.setFocusable(false);
        f.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        f.setImeOptions(android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | android.view.inputmethod.EditorInfo.IME_FLAG_NO_FULLSCREEN);
        f.addTextChangedListener(new android.text.TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void afterTextChanged(android.text.Editable e) {
                String now = e.toString();
                if (now.length() > lastText.length() && now.startsWith(lastText)) {
                    String added = now.substring(lastText.length());
                    for (int i = 0; i < added.length(); i++)
                        imeText(String.valueOf(added.charAt(i)));
                } else if (now.length() < lastText.length()) {
                    for (int i = now.length(); i < lastText.length(); i++) imeBackspace();
                }
                lastText = now;
            }
        });
        /*
         * Confirming the input. The game recognises Return by comparing the
         * inserted text with a line break: `virtualKeyboardInsertText:` matches
         * it against the string "\n" and on a match goes down its own branch.
         *
         * The field itself never gives such text: it is single-line, and
         * instead of an insertion Android sends an editor action or
         * KEYCODE_ENTER. So we catch both paths and hand the layer exactly a
         * line break.
         */
        f.setOnEditorActionListener((v, action, event) -> {
            imeText("\n");
            return true;
        });
        f.setOnKeyListener((v, code, event) -> {
            if (event.getAction() == android.view.KeyEvent.ACTION_DOWN
                    && (code == android.view.KeyEvent.KEYCODE_ENTER
                        || code == android.view.KeyEvent.KEYCODE_NUMPAD_ENTER)) {
                imeText("\n");
                return true;
            }
            return false;
        });
        return f;
    }

    /*
     * THE SAVES MOVE INTO THE PORT'S FOLDER (P3, the owner, 30.09.2026):
     * Android/media/<app>/saves, beside the config and the mods, where a file
     * manager can reach them. They lived in Android/data/<app>/files/saves for
     * the cloud backup's sake, which is off now (AndroidManifest).
     *
     * On the worker thread, before setSavesDir - before the game reads a byte.
     * File by file through MediaFs, each checked by its size; no folder is
     * renamed inside Android/media (that is what MediaProvider refused on
     * 28.09). `saves/.moving` stands while the copy is under way, so a move cut
     * short is simply done again by the next launch - the source is not touched
     * until every file is across. Then the source folder is renamed, inside
     * Android/data where no MediaProvider stands in the way, to
     * files/saves.moved-to-media: invisible, never moved again, gone with the
     * app. The spawner's run marks (port-spawner-runs.txt) are a file of the
     * saves folder and move with it.
     *
     * Both places holding the game's files (a move from long ago meeting a new
     * one): the sets are never mixed file by file - somebody else's gamestate
     * beside a persistentgamedata breaks. The set with the newer
     * persistentgamedata stays, the other is put aside whole as
     * saves.conflict-<date> on its own side.
     */
    private static boolean gameFile(String n) {
        return n.contains("persistentgamedata") || n.contains("gamestate")
            || n.equals("options.ini") || n.equals("inputconfigs.dat");
    }

    private static boolean hasGameFiles(java.io.File dir) {
        java.io.File[] items = dir.listFiles();
        if (items == null) return false;
        for (java.io.File f : items) {
            if (f.isDirectory() ? hasGameFiles(f) : gameFile(f.getName())) return true;
        }
        return false;
    }

    private static long newestProgress(java.io.File dir) {
        long t = 0;
        java.io.File[] items = dir.listFiles();
        if (items == null) return 0;
        for (java.io.File f : items) {
            if (f.isDirectory()) t = Math.max(t, newestProgress(f));
            else if (f.getName().contains("persistentgamedata")) t = Math.max(t, f.lastModified());
        }
        return t;
    }

    private static String today() {
        return new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.ROOT).format(new java.util.Date());
    }

    private String nomediaState = "not looked at";

    private void moveSavesToMedia(java.io.File saves) {
        java.io.File box = getExternalFilesDir(null);
        java.io.File old = box != null ? new java.io.File(box, "saves") : null;
        java.io.File moving = new java.io.File(saves, ".moving");
        try {
            saves.mkdirs();
            if (old == null || !old.isDirectory()) {
                if (moving.exists()) moving.delete();         /* copied and renamed; only the mark was left */
                return;
            }
            boolean resume = moving.exists();
            if (!resume && hasGameFiles(saves) && hasGameFiles(old)) {
                long here = newestProgress(saves), there = newestProgress(old);
                String tag = "saves.conflict-" + today();
                if (there <= here) {
                    java.io.File aside = new java.io.File(box, tag);
                    boolean ok = old.renameTo(aside);
                    say("saves: CONFLICT: kept the set in " + saves + ", the other set " + (ok ? "put aside in " + aside
                            : "could NOT be put aside, left in " + old));
                    return;
                }
                java.io.File aside = new java.io.File(saves.getParentFile(), tag);
                long[] put = MediaFs.copyTree(saves, aside);
                java.io.File[] items = saves.listFiles();
                if (items != null) for (java.io.File f : items) {
                    if (f.getName().equals(".nomedia")) continue;
                    if (f.isDirectory()) MediaFs.deleteTree(f, true); else f.delete();
                }
                say("saves: CONFLICT: the set in Android/data is newer and comes in; the one that was here ("
                        + put[0] + " files) is put aside in " + aside);
            }
            try (java.io.FileOutputStream mark = new java.io.FileOutputStream(moving)) { mark.getFD().sync(); }
            long[] got = MediaFs.copyTree(old, saves);
            java.io.File done = new java.io.File(box, "saves.moved-to-media");
            if (done.exists()) done = new java.io.File(box, "saves.moved-to-media-" + today());
            boolean renamed = old.renameTo(done);
            moving.delete();
            say("saves: moved " + got[0] + " files, " + got[1] + " bytes from " + old + " into " + saves
                    + (renamed ? "; the source is now " + done : "; the source could NOT be renamed and stays"));
        } catch (Exception e) {
            say("saves: the move into Android/media broke off - " + e + " (it is done again at the next start)");
        }
    }

    /*
     * THE SAVES AS THE LAUNCH FINDS THEM (item 27, report builds): where they
     * are, how the folder was found, the free space, when the app was installed
     * and updated, and every file of the saves with its size and time - before
     * the game has touched any of them. The layer adds its own check of every
     * slot against the backup (saves.c, "self-check").
     */
    private void savesInventory(java.io.File saves) {
        if (!REPORT) return;
        try {
            android.content.pm.PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            java.text.SimpleDateFormat f = new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm", java.util.Locale.ROOT);
            long free = saves != null ? new android.os.StatFs(saves.getAbsolutePath()).getAvailableBytes() : -1;
            say("saves: root " + (saves != null ? saves.getAbsolutePath() : "none") + " (" + PortDirs.how()
                    + "), free " + (free >> 20) + " MB, installed " + f.format(new java.util.Date(pi.firstInstallTime))
                    + ", updated " + f.format(new java.util.Date(pi.lastUpdateTime)));
            if (saves == null) return;
            for (java.io.File dir : new java.io.File[] { saves, new java.io.File(saves, "Repentance") }) {
                java.io.File[] files = dir.listFiles();
                if (files == null || files.length == 0) { say("saves: " + dir.getName() + "/ is empty"); continue; }
                for (java.io.File file : files)
                    if (file.isFile())
                        say("saves:   " + dir.getName() + "/" + file.getName() + " " + file.length() + " bytes, "
                                + f.format(new java.util.Date(file.lastModified())));
            }
        } catch (Exception e) {
            say("saves: inventory failed - " + e);
        }
    }

    /* ------------------------------------------------ the Tweaks page's actions */
    /*
     * Called from the game's thread (tweaks.c -> loader.c): 1 the config file in
     * the system's file manager, 2 import saves, 3 export saves (the owner's
     * items 14 and 9, 23.09.2026). Everything that shows a window is done on the
     * UI thread.
     */
    private static final int REQ_IMPORT = 7101, REQ_EXPORT = 7102, REQ_ADD_MOD = 7103, REQ_DUMP = 7104,
            REQ_MOD_OUT = 7105;
    private volatile java.io.File savesRoot;
    private long configStamp;

    /*
     * THE PORT'S FOLDER: Android/media/<app> (the owner, 28.09.2026), worked out
     * once per process by PortDirs. It holds port-touch.ini, the player's mods,
     * the exported zips and - since 30.09.2026 - the saves with options.ini:
     * everything a person edits by hand. Android/data, where they lived, is
     * closed to file managers since Android 11, the document picker was shut
     * out of it in 13, and the Shizuku workaround fails on many phones.
     * Android/media is shared storage by Google's own words, while the app
     * reads and writes its own folder there with no permission at all.
     *
     * The first launch that finds the new folder without a config moves the
     * config, the mods and the exported zips over from Android/data, so an
     * update loses nobody their settings or mods.
     */
    private boolean portDirMoved;

    private java.io.File portDir() {
        java.io.File dir = PortDirs.port(this);
        if (dir == null) return null;
        if (!portDirMoved) {
            portDirMoved = true;
            migrateConfig(getExternalFilesDir(null), dir);
        }
        return dir;
    }

    /* port-touch.ini, mods/ and shared/ from Android/data/<app>/files into the port's folder, once */
    private void migrateConfig(java.io.File old, java.io.File port) {
        try {
            java.io.File ini = new java.io.File(port, "port-touch.ini");
            if (REPORT) say("config: dir " + port + " (" + PortDirs.how() + "); port-touch.ini "
                    + (ini.exists() ? ini.length() + " bytes, " + new java.util.Date(ini.lastModified()) : "not there"));
            if (old == null || old.equals(port)) return;
            java.io.File oldIni = new java.io.File(old, "port-touch.ini");
            if (ini.exists() || !oldIni.exists()) return;
            long n = 0;
            for (String name : new String[] {"port-touch.ini", "mods", "shared"}) {
                java.io.File from = new java.io.File(old, name), to = new java.io.File(port, name);
                if (!from.exists()) continue;
                if (from.isDirectory()) { n += MediaFs.copyTree(from, to)[0]; MediaFs.deleteTree(from, true); }
                else { MediaFs.copy(from, to); from.delete(); n++; }
            }
            android.util.Log.i("isaac", "config: moved " + n + " files from " + old + " into " + port);
            say("config: moved " + n + " files from " + old.getAbsolutePath() + " into " + port.getAbsolutePath());
        } catch (Exception e) {
            android.util.Log.e("isaac", "config: the move into Android/media failed", e);
            say("config: the move into Android/media failed - " + e);
        }
    }

    private java.io.File configFile() {
        java.io.File dir = portDir();
        return dir != null ? new java.io.File(dir, "port-touch.ini") : null;
    }

    public void portAction(final int what) {
        runOnUiThread(() -> {
            try {
                if (what == 1) openConfig();
                else if (what == 2) pickSaves();
                else if (what == 3) pickExportFolder();
                else if (what == 4) pickMod();
                else if (what == 6) chooseDump();
                else if (what == 7) restartSoon();         /* RESTART TO APPLY: mods.txt is saved already */
            } catch (Throwable t) {
                android.util.Log.e("isaac", "port action " + what, t);
                toast(UserMods.t("Не удалось открыть: ", "Could not open: ") + t.getMessage());
            }
        });
    }

    /* about one of the player's mods (tweaks.c): 5 share it, `text` its id */
    public void portActionText(final int what, final String text) {
        if (what == 5) runOnUiThread(() -> {
            modOut = text;
            startActivityForResult(new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_MOD_OUT);
        });
    }

    private void toast(String text) {
        runOnUiThread(() -> android.widget.Toast.makeText(this, text, android.widget.Toast.LENGTH_LONG).show());
    }

    /*
     * The system's OWN file manager, not a vendor's: the one app that answers
     * the documents UI's intents is found by its package (com.android.documentsui
     * or Google's com.google.android.documentsui) and asked by name, so a
     * vendor's manager that also claims the intent is never offered.
     */
    private void openConfig() {
        java.io.File ini = configFile();
        /* port-touch.ini goes through the save writer's queue (safefile.c): its stamp is the one on the disk */
        if (layerLoaded) savesFlush(1000, false);
        configStamp = ini != null ? ini.lastModified() : 0;
        if (configStamp == 0) configStamp = 1;
        android.net.Uri root = android.provider.DocumentsContract.buildRootUri(
                PortFilesProvider.AUTHORITY, PortFilesProvider.ROOT);
        android.content.Intent view = new android.content.Intent(android.content.Intent.ACTION_VIEW);
        view.setDataAndType(root, android.provider.DocumentsContract.Root.MIME_TYPE_ITEM);
        view.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
                | android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        /*
         * The system's documents UI by name, and the start itself is the
         * question: a package list can be empty for want of visibility while
         * the activity is there all along. The root view first, the documents
         * UI's own browse action after, each on both of its package names.
         */
        android.content.Intent browse = new android.content.Intent("android.provider.action.BROWSE");
        browse.setData(root);
        browse.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
                | android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        String[] systems = {"com.google.android.documentsui", "com.android.documentsui"};
        for (android.content.Intent how : new android.content.Intent[] { view, browse }) {
            for (String pkg : systems) {
                how.setPackage(pkg);
                try {
                    startActivity(how);
                    if (REPORT) say("config: opened by " + pkg + " (" + how.getAction() + ")");
                    return;
                } catch (android.content.ActivityNotFoundException | SecurityException e) {
                    if (REPORT) say("config: " + pkg + " (" + how.getAction() + ") - " + e.getClass().getSimpleName());
                }
            }
        }
        toast(UserMods.t("Системный файловый менеджер не найден", "The system file manager was not found"));
    }

    private void pickSaves() {
        android.content.Intent pick = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        pick.putExtra(android.content.Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(pick, REQ_IMPORT);
    }

    private void pickExportFolder() {
        startActivityForResult(new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_EXPORT);
    }

    @Override
    protected void onActivityResult(int request, int result, android.content.Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null) return;
        if (request == REQ_IMPORT) {
            java.util.List<android.net.Uri> uris = new java.util.ArrayList<>();
            if (data.getClipData() != null)
                for (int i = 0; i < data.getClipData().getItemCount(); i++) uris.add(data.getClipData().getItemAt(i).getUri());
            else if (data.getData() != null) uris.add(data.getData());
            new Thread(() -> importSaves(uris), "import").start();
        } else if (request == REQ_EXPORT && data.getData() != null) {
            final android.net.Uri tree = data.getData();
            new Thread(() -> exportSaves(tree), "export").start();
        } else if (request == REQ_ADD_MOD) {
            /* one archive or several (hotfix 1.73): the clip's items, else the data alone */
            final android.content.ClipData clip = data.getClipData();
            final java.util.List<android.net.Uri> zips = UserMods.picked(data.getData(),
                    clip != null ? clip.getItemCount() : 0, i -> clip.getItemAt(i).getUri());
            if (REPORT) say("usermods: picked " + zips.size() + " archive(s)" + (clip != null ? " as a clip of " + clip.getItemCount() : ""));
            if (zips.isEmpty()) return;
            if (!addInFlight.compareAndSet(false, true)) {          /* one add at a time: its window again */
                if (addWindow != null && !addWindow.isShowing()) addWindow.show();
                return;
            }
            openAddWindow(zips.size());                              /* the window before the work */
            new Thread(() -> addMods(zips), "add mod").start();
        } else if (request == REQ_MOD_OUT && data.getData() != null && modOut != null) {
            final android.net.Uri tree = data.getData();
            final String id = modOut;
            new Thread(() -> exportMod(id, tree), "mod out").start();
        } else if (request == REQ_DUMP && data.getData() != null) {
            final android.net.Uri zip = data.getData();
            final boolean[] kinds = dumpKinds.clone();
            dumpGame(zip, kinds);
        }
    }

    /* ------------------------------------------------ the player's own mods (UserMods, usermods.c) */
    private void pickMod() {
        if (addInFlight.get()) {
            /* an add is running: ADD MOD brings its window back instead of a second add */
            if (REPORT) say("usermods: add asked again while busy");
            if (addWindow != null && !addWindow.isShowing()) addWindow.show();
            return;
        }
        android.content.Intent pick = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
        pick.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        pick.setType("*/*");
        pick.putExtra(android.content.Intent.EXTRA_MIME_TYPES,
                new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        /* several archives at once (hotfix 1.73): a long press in the picker selects more than one */
        pick.putExtra(android.content.Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(pick, REQ_ADD_MOD);
    }

    /*
     * THE ADD WINDOW (plan 02.10, B2). A big mod (Reflashed: 5333 files, 300 MB)
     * took tens of seconds with nothing on the screen, and the window came only
     * at the end; a player pressed ADD MOD again and a second add wrote the same
     * folder. Now a plain Android window opens the moment the file is picked,
     * before the work starts: the step, a bar, the counts. It cannot be closed
     * by a touch beside it or by Back, and it holds the touches and the pad
     * while it is up - the game behind it gets nothing. A second ADD MOD while
     * one runs brings the same window back. The installer's question (the same
     * mod installed already: update it or put the new one beside it) is asked
     * in it, and at the end it turns into the result, with OK - no moment
     * without a window.
     *
     * HOTFIX 1.73: several archives at once, and no restart after them. The
     * window's title counts the batch ("Adding mods: 2 of 5"), a line under it
     * names the archive, and the update question carries a tick - the same
     * answer for the rest of the batch - while more mods are to come. At the end
     * one mod's result reads as it always did; a batch's is a summary, a line a
     * mod, the ones not added in red, then each mod's own text. OK closes it, and
     * the game goes on: the MODS page shows the new mods, RESTART TO APPLY under
     * them, which the player taps when he is done.
     */
    private final java.util.concurrent.atomic.AtomicBoolean addInFlight = new java.util.concurrent.atomic.AtomicBoolean();
    private android.app.AlertDialog addWindow;
    private android.widget.TextView addStep, addNumbers, addHint, addWhich;
    private android.widget.ProgressBar addBar;
    private android.widget.CheckBox addSame;
    private int addCount = 1;

    private void openAddWindow(int count) {
        addCount = Math.max(1, count);
        float dp = getResources().getDisplayMetrics().density;
        android.widget.LinearLayout box = new android.widget.LinearLayout(this);
        box.setOrientation(android.widget.LinearLayout.VERTICAL);
        int pad = (int) (22 * dp);
        box.setPadding(pad, (int) (8 * dp), pad, (int) (4 * dp));
        addWhich = new android.widget.TextView(this);
        addWhich.setTextSize(13);
        addWhich.setPadding(0, 0, 0, (int) (6 * dp));
        addWhich.setVisibility(addCount > 1 ? android.view.View.VISIBLE : android.view.View.GONE);
        addStep = new android.widget.TextView(this);
        addStep.setTextSize(16);
        addStep.setText(UserMods.stepName(UserMods.READ, null));
        addBar = new android.widget.ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        addBar.setIndeterminate(true);
        addBar.setMax(1000);
        addNumbers = new android.widget.TextView(this);
        addNumbers.setTextSize(14);
        addSame = new android.widget.CheckBox(this);
        addSame.setText(UserMods.sameForAllWord());
        addSame.setVisibility(android.view.View.GONE);
        addHint = new android.widget.TextView(this);
        addHint.setTextSize(13);
        addHint.setText(addCount > 1 ? UserMods.keepOpenHintMany() : UserMods.keepOpenHint());
        addHint.setPadding(0, (int) (10 * dp), 0, 0);
        box.addView(addWhich);
        box.addView(addStep);
        box.addView(addBar);
        box.addView(addNumbers);
        box.addView(addSame);
        box.addView(addHint);
        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        scroll.addView(box);
        addWindow = new android.app.AlertDialog.Builder(this)
                .setTitle(addCount > 1 ? UserMods.batchTitle(1, addCount) : UserMods.windowTitle())
                .setView(scroll)
                .setCancelable(false)
                .setPositiveButton("OK", null)
                .setNeutralButton(UserMods.besideWord(), null)
                .create();
        addWindow.setCanceledOnTouchOutside(false);
        addWindow.show();
        addWindow.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setVisibility(android.view.View.GONE);
        addWindow.getButton(android.app.AlertDialog.BUTTON_NEUTRAL).setVisibility(android.view.View.GONE);
        if (REPORT) say("usermods: add dialog: shown for " + addCount + " archive(s)");
    }

    private void addProgress(int step, long done, long total, long bytes, long bytesTotal, String what) {
        runOnUiThread(() -> {
            if (addWindow == null) return;
            addStep.setText(UserMods.stepName(step, what));
            if (total > 0) {
                addBar.setIndeterminate(false);
                addBar.setProgress((int) Math.min(1000, done * 1000 / total));
            } else {
                addBar.setIndeterminate(true);
            }
            addNumbers.setText(UserMods.numbers(step, done, total, bytes, bytesTotal));
        });
    }

    /* the batch moves on to mod k (from 0) of n: the title counts it, the line under it names the archive */
    private void addNext(int k, int n, String label) {
        runOnUiThread(() -> {
            if (addWindow == null) return;
            if (n > 1) {
                addWindow.setTitle(UserMods.batchTitle(k + 1, n));
                addWhich.setText(label);
            }
            addStep.setText(UserMods.stepName(UserMods.READ, null));
            addBar.setIndeterminate(true);
            addNumbers.setText("");
        });
    }

    /* the installer's question, in the window; the add thread waits for the answer (and the tick, in a batch) */
    private int addAsk(String question, boolean more) {
        final java.util.concurrent.CountDownLatch answered = new java.util.concurrent.CountDownLatch(1);
        final int[] answer = {0};
        runOnUiThread(() -> {
            if (addWindow == null) { answered.countDown(); return; }
            addStep.setText(question);
            addBar.setVisibility(android.view.View.GONE);
            addNumbers.setVisibility(android.view.View.GONE);
            addSame.setChecked(false);
            addSame.setVisibility(more ? android.view.View.VISIBLE : android.view.View.GONE);
            final android.widget.Button up = addWindow.getButton(android.app.AlertDialog.BUTTON_POSITIVE);
            final android.widget.Button beside = addWindow.getButton(android.app.AlertDialog.BUTTON_NEUTRAL);
            up.setText(UserMods.updateWord());
            beside.setText(UserMods.besideWord());
            up.setVisibility(android.view.View.VISIBLE);
            beside.setVisibility(android.view.View.VISIBLE);
            android.view.View.OnClickListener chosen = v -> {
                answer[0] = (v == up ? UserMods.ANSWER_UPDATE : 0)
                        | (more && addSame.isChecked() ? UserMods.ANSWER_ALL : 0);
                up.setVisibility(android.view.View.GONE);
                beside.setVisibility(android.view.View.GONE);
                addSame.setVisibility(android.view.View.GONE);
                addBar.setVisibility(android.view.View.VISIBLE);
                addNumbers.setVisibility(android.view.View.VISIBLE);
                up.setOnClickListener(null);
                beside.setOnClickListener(null);
                answered.countDown();
            };
            up.setOnClickListener(chosen);
            beside.setOnClickListener(chosen);
            up.requestFocus();
        });
        try {
            answered.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return 0;
        }
        if (REPORT) say("usermods: add asked \"" + question + "\" - "
                + ((answer[0] & UserMods.ANSWER_UPDATE) != 0 ? "update" : "beside it")
                + ((answer[0] & UserMods.ANSWER_ALL) != 0 ? ", the same for the rest of the batch" : ""));
        return answer[0];
    }

    private void addMods(java.util.List<android.net.Uri> zips) {
        java.io.File ini = configFile();
        java.util.List<UserMods.Result> results = new java.util.ArrayList<>();
        try {
            if (ini != null) {
                java.util.List<UserMods.Item> items = new java.util.ArrayList<>();
                for (android.net.Uri u : zips) items.add(new UserMods.Item(UserMods.labelOf(this, u), UserMods.uriSource(this, u)));
                results = UserMods.installAll(items, ini.getParentFile(), this::addProgress, this::addAsk, this::addNext);
            }
        } catch (Throwable t) {
            android.util.Log.e("isaac", "add mod", t);
            UserMods.Result failed = new UserMods.Result();
            failed.text = UserMods.t("Установка прервалась: ", "The add stopped: ") + t;
            results.add(failed);
        }
        final java.util.List<UserMods.Result> all = results;
        final boolean noConfig = ini == null;
        int added = 0;
        for (UserMods.Result r : all) if (r.ok) added++;
        final int ok = added;
        if (REPORT) for (UserMods.Result r : all)
            say("usermods: add " + (r.ok ? "taken - " : "refused - ") + (r.label != null ? r.label + ": " : "") + r.text);
        runOnUiThread(() -> {
            if (addWindow == null) openAddWindow(all.size());
            final int red = 0xFFC62828;
            android.text.SpannableStringBuilder text = new android.text.SpannableStringBuilder();
            if (noConfig || all.isEmpty()) {
                addWindow.setTitle(UserMods.resultTitle(null));
                appendColoured(text, UserMods.t("Папка настроек недоступна.", "The settings folder is not available."), red);
            } else if (all.size() == 1) {
                /* one mod: its result as it always read, the restart's word after it; refused - in red */
                UserMods.Result r = all.get(0);
                addWindow.setTitle(UserMods.resultTitle(r));
                appendColoured(text, r.text, r.ok ? 0 : red);
                if (r.ok) text.append("\n\n").append(UserMods.restartHint());
            } else {
                /* a batch: a line a mod (the ones not added in red), the restart's word, then each mod's own text */
                addWindow.setTitle(UserMods.summaryTitle(ok, all.size()));
                for (UserMods.Result r : all) appendColoured(text, "• " + UserMods.statusLine(r) + "\n", r.ok ? 0 : red);
                if (ok > 0) text.append("\n").append(UserMods.restartHint()).append("\n");
                for (UserMods.Result r : all) {
                    text.append("\n");
                    String head = r.name != null && !r.name.isEmpty() ? r.name + (r.label != null ? " (" + r.label + ")" : "")
                                : r.label != null ? r.label : "?";
                    int at = text.length();
                    appendColoured(text, head + "\n", r.ok ? 0 : red);
                    text.setSpan(new android.text.style.StyleSpan(android.graphics.Typeface.BOLD), at, text.length(),
                            android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
                    appendColoured(text, (r.text != null ? r.text.trim() : "") + "\n", r.ok ? 0 : red);
                }
            }
            addWhich.setVisibility(android.view.View.GONE);
            addBar.setVisibility(android.view.View.GONE);
            addNumbers.setVisibility(android.view.View.GONE);
            addSame.setVisibility(android.view.View.GONE);
            addHint.setVisibility(android.view.View.GONE);
            addStep.setText(text);
            /* hotfix 1.74: RESTART TO APPLY in the window too, whenever the MODS page's row would stand */
            final android.widget.Button again = addWindow.getButton(android.app.AlertDialog.BUTTON_NEUTRAL);
            final boolean owed = ok > 0 && UserMods.restartIsOwed();
            again.setVisibility(owed ? android.view.View.VISIBLE : android.view.View.GONE);
            if (owed) {
                again.setText(UserMods.restartWord());
                again.setOnClickListener(v -> {
                    if (addWindow != null) addWindow.dismiss();
                    addWindow = null;
                    addInFlight.set(false);
                    if (REPORT) say("usermods: add dialog: RESTART TO APPLY, " + ok + " of " + all.size() + " added");
                    restartSoon();
                });
            }
            android.widget.Button button = addWindow.getButton(android.app.AlertDialog.BUTTON_POSITIVE);
            button.setText("OK");
            button.setVisibility(android.view.View.VISIBLE);
            button.setOnClickListener(v -> {
                if (addWindow != null) addWindow.dismiss();
                addWindow = null;
                addInFlight.set(false);
                /* no restart (hotfix 1.73): the MODS page shows the new mods and RESTART TO APPLY under them */
                if (REPORT) say("usermods: add dialog: closed, " + ok + " of " + all.size() + " added, no restart");
            });
            button.requestFocus();
        });
    }

    /* text into the window, in a colour of its own when `argb` is not 0 */
    private static void appendColoured(android.text.SpannableStringBuilder b, String s, int argb) {
        if (s == null) return;
        int at = b.length();
        b.append(s);
        if (argb != 0 && b.length() > at)
            b.setSpan(new android.text.style.ForegroundColorSpan(argb), at, b.length(), android.text.Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    }

    /*
     * EXPORT on a mod's page (it was SHARE, the system's share sheet; the owner
     * wanted it in a folder, 25.09.2026): the mod zipped in the port's format
     * into a folder the player picks, as <id>.zip.
     */
    private volatile String modOut;

    private void exportMod(String id, android.net.Uri tree) {
        java.io.File ini = configFile();
        if (ini == null) { toast(UserMods.t("Папка настроек недоступна", "The settings folder is not available")); return; }
        java.io.File zip = null;
        try {
            zip = UserMods.pack(getCacheDir(), ini.getParentFile(), id);
            android.net.Uri parent = android.provider.DocumentsContract.buildDocumentUriUsingTree(tree,
                    android.provider.DocumentsContract.getTreeDocumentId(tree));
            android.net.Uri out = android.provider.DocumentsContract.createDocument(getContentResolver(), parent,
                    "application/zip", zip.getName());
            if (out == null) throw new java.io.IOException(UserMods.t("не удалось создать файл", "could not create the file"));
            try (java.io.InputStream in = new java.io.FileInputStream(zip);
                 java.io.OutputStream o = getContentResolver().openOutputStream(out)) {
                if (o == null) throw new java.io.IOException(UserMods.t("не удалось открыть файл", "could not open the file"));
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
            }
            if (REPORT) say("usermods: export " + id + " - " + zip.length() + " bytes");
            toast(UserMods.t("Мод выгружен: ", "Mod exported: ") + zip.getName());
        } catch (java.io.IOException | RuntimeException e) {
            if (REPORT) say("usermods: export " + id + " failed - " + e);
            toast(UserMods.t("Не удалось выгрузить мод: ", "Could not export the mod: ") + e.getMessage());
        } finally {
            if (zip != null) zip.delete();
        }
    }

    /*
     * THE GAME'S FILES OUT (GameFiles): the kinds weighed first, the player
     * ticks the ones wanted, picks where the zip goes, and it is written with
     * the count on the screen. The game goes on behind the window.
     */
    private final boolean[] dumpKinds = new boolean[GameFiles.KIND_NAMES.length];

    private void chooseDump() {
        toast(UserMods.t("Считаю файлы игры…", "Counting the game's files…"));
        new Thread(() -> {
            final GameFiles.Sizes sizes;
            try {
                sizes = GameFiles.scan(this);
            } catch (java.io.IOException e) {
                toast(UserMods.t("Не удалось прочитать файлы игры: ", "Could not read the game's files: ") + e.getMessage());
                return;
            }
            runOnUiThread(() -> {
                String[] items = new String[GameFiles.KIND_NAMES.length];
                for (int k = 0; k < items.length; k++) {
                    items[k] = GameFiles.KIND_NAMES[k] + " — " + sizes.files[k] + UserMods.t(" файлов, ", " files, ")
                            + (sizes.bytes[k] + (1 << 19)) / (1 << 20) + UserMods.t(" МБ", " MB");
                    dumpKinds[k] = FileKinds.on(k);          /* shaders start unticked (FileKinds, M12) */
                }
                new android.app.AlertDialog.Builder(this)
                        .setTitle(UserMods.t("Выгрузить файлы игры (zip, раскладка мода для ПК)", "Export game files (zip, PC mod layout)"))
                        .setMultiChoiceItems(items, dumpKinds.clone(), (d, which, on) -> dumpKinds[which] = on)
                        .setNegativeButton(UserMods.t("Отмена", "Cancel"), null)
                        .setPositiveButton(UserMods.t("Выгрузить", "Export"), (d, w) -> {
                            boolean any = false;
                            for (boolean b : dumpKinds) any |= b;
                            if (!any) { toast(UserMods.t("Ничего не выбрано", "Nothing is ticked")); return; }
                            android.content.Intent save = new android.content.Intent(android.content.Intent.ACTION_CREATE_DOCUMENT);
                            save.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                            save.setType("application/zip");
                            save.putExtra(android.content.Intent.EXTRA_TITLE, "isaac-repentance-files.zip");
                            startActivityForResult(save, REQ_DUMP);
                        })
                        .show();
            });
        }, "dump scan").start();
    }

    private void dumpGame(android.net.Uri zip, boolean[] kinds) {
        final android.app.AlertDialog window = new android.app.AlertDialog.Builder(this)
                .setTitle(UserMods.t("Выгрузка файлов игры", "Exporting game files"))
                .setMessage(UserMods.t("Начинаю…", "Starting…"))
                .setCancelable(false)
                .show();
        new Thread(() -> {
            String text;
            try {
                int n = GameFiles.dump(this, zip, kinds, (done, total, bytes) -> runOnUiThread(() ->
                        window.setMessage(UserMods.t("Выгружено ", "Exported ") + done + UserMods.t(" из ", " of ") + total
                                + UserMods.t(" файлов, ", " files, ") + bytes / (1 << 20) + UserMods.t(" МБ", " MB"))));
                text = UserMods.t("Готово: " + n + " файлов. В архиве папки resources/ и resources-dlc3.<язык>/ — "
                        + "та же раскладка, что у мода: поправленный файл кладётся в мод по тому же пути.",
                        "Done: " + n + " files. The zip holds resources/ and resources-dlc3.<language>/ - "
                        + "the layout of a mod: an edited file goes into a mod at the same path.");
                if (REPORT) say("dump: " + n + " files");
            } catch (java.io.IOException | RuntimeException e) {
                text = UserMods.t("Выгрузка прервалась: ", "The export stopped: ") + e.getMessage();
                if (REPORT) say("dump: failed - " + e);
            }
            final String result = text;
            runOnUiThread(() -> {
                window.dismiss();
                new android.app.AlertDialog.Builder(this).setTitle(UserMods.t("Выгрузка файлов игры", "Exporting game files"))
                        .setMessage(result).setPositiveButton("OK", null).show();
            });
        }, "dump").start();
    }

    private String nameOf(android.net.Uri uri) {
        try (android.database.Cursor c = getContentResolver().query(uri,
                new String[] {android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) { }
        return uri.getLastPathSegment();
    }

    private static byte[] readAll(java.io.InputStream in) throws java.io.IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        return out.toByteArray();
    }

    /*
     * IMPORT SAVES WITH A CHOICE OF SLOT (P7, the owner's complaint of 30.09:
     * "the import always lands in slot 1"). The slot used to come from the
     * file's name alone - persistentgamedata1.dat went to slot 1, always, and
     * there was no way to put it into 2 or 3. Now every picked file gets a
     * window of its own: the three slots with what each holds now (empty, the
     * share of achievements, the date), the slot from the file's name already
     * chosen, and IMPORT or SKIP. Nothing is written until the last window is
     * answered.
     *
     * Then, in this order: the game's own saves are let go (savesFreeze - a
     * save the game makes now would write the old progress over the new
     * files), options.ini is written the game's way (restartSoon), and each
     * chosen slot is written: the file there before is kept beside it as
     * .before-import and in the port's backup (saves.c, keep-before-import),
     * the new one goes in through MediaFs and into the backup as the newest,
     * and the slot's run in progress is wiped - it belongs to the progress just
     * replaced (the owner, 24.09.2026). So are the slot's spawner run marks
     * (port-spawner-runs.txt): they name runs of the progress that is gone.
     * The game starts again afterwards. A run in progress (gamestate) is never
     * taken: it breaks.
     */
    private static final class Picked {
        String name;
        byte[] data;
        int achievements = -1;
        int slot;                                   /* 0 skipped */
    }

    private String slotText(java.io.File dir, int slot) {
        java.io.File f = new java.io.File(dir, SaveConverter.fileName(slot));
        if (!f.isFile() || f.length() == 0) return UserMods.t("пусто", "empty");
        String date = new java.text.SimpleDateFormat("dd.MM.yyyy HH:mm", java.util.Locale.ROOT)
                .format(new java.util.Date(f.lastModified()));
        try {
            int got = SaveConverter.achievements(MediaFs.read(f));
            int pct = Math.round(got * 100f / SaveConverter.ACHIEVEMENTS);
            return pct + UserMods.t("% достижений, ", "% achievements, ") + date;
        } catch (Exception e) {
            return UserMods.t("не читается, ", "unreadable, ") + date;
        }
    }

    private void importSaves(java.util.List<android.net.Uri> uris) {
        if (layerLoaded) savesFlush(3000, false);       /* the slots' windows show what the game last wrote */
        java.io.File dir = SaveConverter.saveFolder(savesRoot);
        if (dir == null) { toast(UserMods.t("Папка сохранений недоступна", "The saves folder is not available")); return; }
        final java.util.List<Picked> picked = new java.util.ArrayList<>();
        final StringBuilder bad = new StringBuilder();
        for (android.net.Uri uri : uris) {
            String name = nameOf(uri);
            if (name == null) name = "";
            if (name.toLowerCase().contains("gamestate")) { bad.append(name).append(UserMods.t(" (забег не принимается)\n", " (a run in progress is not taken)\n")); continue; }
            try (java.io.InputStream in = getContentResolver().openInputStream(uri)) {
                if (in == null) throw new java.io.IOException(UserMods.t("не открылся", "did not open"));
                Picked p = new Picked();
                p.name = name;
                p.data = SaveConverter.toPort(readAll(in));
                try { p.achievements = SaveConverter.achievements(p.data); } catch (Exception ignored) { }
                picked.add(p);
            } catch (Exception e) {
                bad.append(name).append(" (").append(e.getMessage()).append(")\n");
            }
        }
        if (picked.isEmpty()) {
            if (bad.length() > 0) toast(UserMods.t("Не принято:\n", "Not taken:\n") + bad.toString().trim());
            return;
        }
        final String[] slots = new String[4];
        for (int k = 1; k <= 3; k++) slots[k] = slotText(dir, k);
        runOnUiThread(() -> askSlot(picked, 0, dir, slots, bad));
    }

    /* one window per file; the next one when this one is answered */
    private void askSlot(java.util.List<Picked> picked, int i, java.io.File dir, String[] slots, StringBuilder bad) {
        if (i >= picked.size()) { importChosen(picked, dir, bad); return; }
        final Picked p = picked.get(i);
        /* the slot the name gives, or the first one no earlier file of this import took */
        int want = SaveConverter.slotOf(p.name);
        if (want == 0) {
            want = 1;
            for (int k = 1; k <= 3; k++) {
                boolean taken = false;
                for (int j = 0; j < i; j++) taken |= picked.get(j).slot == k;
                if (!taken) { want = k; break; }
            }
        }
        final int[] choice = { want };
        String[] rows = new String[3];
        for (int k = 1; k <= 3; k++) {
            String by = null;
            for (int j = 0; j < i; j++) if (picked.get(j).slot == k) by = picked.get(j).name;
            rows[k - 1] = UserMods.t("Слот ", "Slot ") + k + " — "
                    + (by != null ? UserMods.t("уже выбран для ", "already chosen for ") + by : slots[k]);
        }
        String what = p.achievements >= 0
                ? Math.round(p.achievements * 100f / SaveConverter.ACHIEVEMENTS) + UserMods.t("% достижений", "% achievements")
                : "";
        new android.app.AlertDialog.Builder(this)
                .setTitle(UserMods.t("Куда положить ", "Where to put ") + p.name
                        + (what.isEmpty() ? "" : " (" + what + ")")
                        + (picked.size() > 1 ? "  " + (i + 1) + "/" + picked.size() : ""))
                .setSingleChoiceItems(rows, want - 1, (d, which) -> choice[0] = which + 1)
                .setCancelable(false)
                .setNegativeButton(UserMods.t("Пропустить", "Skip"), (d, w) -> {
                    p.slot = 0;
                    askSlot(picked, i + 1, dir, slots, bad);
                })
                .setPositiveButton(UserMods.t("Импортировать", "Import"), (d, w) -> {
                    /* one file per slot: a slot chosen again goes to the later file */
                    for (int j = 0; j < i; j++) if (picked.get(j).slot == choice[0]) picked.get(j).slot = 0;
                    p.slot = choice[0];
                    askSlot(picked, i + 1, dir, slots, bad);
                })
                .show();
    }

    private void importChosen(java.util.List<Picked> picked, java.io.File dir, StringBuilder bad) {
        final java.util.List<Picked> go = new java.util.ArrayList<>();
        StringBuilder plan = new StringBuilder();
        for (Picked p : picked) if (p.slot > 0) {
            go.add(p);
            plan.append(p.name).append(UserMods.t(" → слот ", " → slot ")).append(p.slot).append('\n');
        }
        if (go.isEmpty()) {
            toast(UserMods.t("Ничего не импортировано", "Nothing was imported")
                    + (bad.length() > 0 ? UserMods.t("\nНе принято:\n", "\nNot taken:\n") + bad.toString().trim() : ""));
            return;
        }
        android.widget.Toast.makeText(this, UserMods.t("Импорт:\n", "Importing:\n") + plan.toString().trim()
                + (bad.length() > 0 ? UserMods.t("\nНе принято:\n", "\nNot taken:\n") + bad.toString().trim() : "")
                + UserMods.t("\nИгра перезапустится.", "\nThe game will restart."), android.widget.Toast.LENGTH_LONG).show();
        if (layerLoaded) savesFreeze();
        restartSoon(() -> {
            for (Picked p : go) writeImported(dir, p);
            dropRunMarks(go);
        });
    }

    private void writeImported(java.io.File dir, Picked p) {
        java.io.File target = new java.io.File(dir, SaveConverter.fileName(p.slot));
        try {
            if (target.isFile() && target.length() > 0) {
                MediaFs.copy(target, new java.io.File(dir, target.getName() + ".before-import"));
                if (layerLoaded) savesKeep(target.getAbsolutePath(), "before-import");
            }
            MediaFs.write(target, p.data);
            if (layerLoaded) savesBackup(target.getAbsolutePath());
            String run = SaveConverter.runName(p.slot);
            for (String leftover : new String[] { run, run + ".tmp", run + ".ready", run + ".before-import" }) {
                java.io.File f = new java.io.File(dir, leftover);
                if (f.exists() && !f.delete()) say("import: " + leftover + " NOT wiped");
            }
            say("import: " + p.name + " -> slot " + p.slot + ", " + p.data.length + " bytes, "
                    + p.achievements + " achievements");
        } catch (Exception e) {
            say("import: " + p.name + " -> slot " + p.slot + " FAILED - " + e);
        }
    }

    /* the imported slots' spawner run marks (spawner.c: "slot seed challenge" a line) go with their runs */
    private void dropRunMarks(java.util.List<Picked> go) {
        if (savesRoot == null) return;
        java.io.File marks = new java.io.File(savesRoot, "port-spawner-runs.txt");
        if (!marks.isFile()) return;
        try {
            String[] lines = new String(MediaFs.read(marks), java.nio.charset.StandardCharsets.UTF_8).split("\n");
            StringBuilder keep = new StringBuilder();
            int dropped = 0;
            for (String line : lines) {
                String t = line.trim();
                boolean drop = false;
                if (!t.isEmpty() && !t.startsWith("#")) {
                    String[] f = t.split("\\s+");
                    for (Picked p : go) drop |= f.length >= 1 && f[0].equals(String.valueOf(p.slot));
                }
                if (drop) dropped++; else if (!line.isEmpty()) keep.append(line).append('\n');
            }
            if (dropped > 0) MediaFs.write(marks, keep.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
            if (dropped > 0) say("import: " + dropped + " spawner run marks of the imported slots dropped");
        } catch (Exception e) {
            say("import: the spawner run marks were not updated - " + e);
        }
    }

    /*
     * OUR OWN RESTART WRITES THE SETTINGS FIRST (P1.5). RESTART TO APPLY and
     * IMPORT end the process outright (OK after ADD MOD did too, until hotfix
     * 1.73: now it only closes the window), and the game writes
     * options.ini only on BACK from its own settings - whatever was changed
     * since lived in memory and was lost. So: `quitting` first (no pause may
     * save the old progress now), then the layer is asked to write options.ini
     * the game's way on its frame thread, and this thread waits - every 20 ms,
     * 1.5 s at most; frames that stand do not hold the restart up, they only
     * leave a line in the report. Then `before` (the import's writes), and the
     * next start is RestartActivity's, from a process of its own (see there).
     */
    private volatile boolean restarting;

    private void restartSoon() { restartSoon(null); }

    private void restartSoon(final Runnable before) {
        if (restarting) return;
        restarting = true;
        quitting = true;
        new Thread(() -> {
            int state = 0;
            if (layerLoaded) {
                int token = requestOptionsSave();
                long end = android.os.SystemClock.uptimeMillis() + 1500;
                while ((state = optionsSaveState(token)) == 0 && android.os.SystemClock.uptimeMillis() < end) {
                    try { Thread.sleep(20); } catch (InterruptedException ignored) { }
                }
            }
            if (REPORT) say(state == 1 ? "options: written the game's way before the restart"
                    : state == 2 ? "options: NOT written before the restart (nothing read yet, or the write failed)"
                    : "options: NOT written before the restart - the frames stood for 1.5 s");
            /*
             * Every save the game made is on the disk before the process ends -
             * and before an import writes its files: then the queue is sealed,
             * so nothing still waiting in it lands over the imported slot.
             */
            if (layerLoaded) {
                boolean flushed = savesFlush(2500, before != null);
                if (REPORT) say("saves: the writer's queue " + (flushed ? "is on the disk" : "was NOT emptied in 2.5 s")
                        + " before the restart");
            }
            if (before != null) {
                try { before.run(); } catch (Throwable t) { writeFailure("the work before the restart", t); }
            }
            runOnUiThread(() -> {
                android.content.Intent again = new android.content.Intent(this, RestartActivity.class);
                again.putExtra(RestartActivity.EXTRA_PID, android.os.Process.myPid());
                again.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(again);
                /*
                 * Ended outright, with no pause: a pause saves, and the game's save is
                 * of the data it read at start - the old saves, over the new files.
                 */
                new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() ->
                    android.os.Process.killProcess(android.os.Process.myPid()), 600);
            });
        }, "restart").start();
    }

    /* the three slots, in the PC's own form, into a folder the owner chose */
    private void exportSaves(android.net.Uri tree) {
        if (layerLoaded) savesFlush(3000, false);       /* the game's last saves, not the ones before them */
        java.io.File dir = SaveConverter.saveFolder(savesRoot);
        StringBuilder done = new StringBuilder();
        try {
            android.net.Uri parent = android.provider.DocumentsContract.buildDocumentUriUsingTree(tree,
                    android.provider.DocumentsContract.getTreeDocumentId(tree));
            for (int slot = 1; slot <= 3 && dir != null; slot++) {
                java.io.File f = new java.io.File(dir, SaveConverter.fileName(slot));
                if (!f.isFile()) continue;
                byte[] pc;
                try (java.io.FileInputStream in = new java.io.FileInputStream(f)) {
                    pc = SaveConverter.toPc(readAll(in));
                }
                android.net.Uri out = android.provider.DocumentsContract.createDocument(getContentResolver(), parent,
                        "application/octet-stream", SaveConverter.pcFileName(slot));
                if (out == null) continue;
                try (java.io.OutputStream o = getContentResolver().openOutputStream(out)) {
                    o.write(pc);
                }
                done.append(SaveConverter.pcFileName(slot)).append('\n');
            }
        } catch (Exception e) {
            toast(UserMods.t("Экспорт прервался: ", "The export stopped: ") + e.getMessage());
            return;
        }
        toast(done.length() > 0 ? UserMods.t("Экспортировано:\n", "Exported:\n") + done.toString().trim() : UserMods.t("Сохранений нет", "There are no saves"));
    }

    public void showKeyboard(final boolean show) {
        runOnUiThread(() -> {
            android.view.inputmethod.InputMethodManager imm =
                    (android.view.inputmethod.InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm == null || field == null) return;
            textEntry = show;
            if (show) {
                lastText = "";
                field.setText("");
                field.setFocusable(true);
                field.setFocusableInTouchMode(true);
                field.requestFocus();
                imm.showSoftInput(field, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT);
            } else {
                imm.hideSoftInputFromWindow(field.getWindowToken(), 0);
                field.clearFocus();
                field.setFocusable(false);
                hideBars();                  /* the keyboard dragged the bars in — restore full screen */
            }
        });
    }

    /**
     * Full screen: there must be no status bar and no navigation bar.
     *
     * Android 11 replaced the flags with a controller. Below that the
     * controller does not exist at all and the call kills the process in
     * onCreate, so the same wish is stated the way those releases understand.
     * The flag set is Android's own for sticky immersive mode, not a guess.
     */
    @SuppressWarnings("deprecation")
    private void hideBars() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
            return;
        }
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    /*
     * Fingers are handed over as they are — in pixels and from the top left
     * corner: that is exactly how the game itself counts them. The phases are
     * Apple's: 0 began, 1 moved, 3 ended, 4 cancelled.
     */
    /*
     * A MOUSE CLICK IS A FINGER, as on iPadOS (U6): the game never asks what
     * touched it. Only the primary button is: a touch with only the secondary
     * or middle button held is taken from the system and given to nobody -
     * a finger of its own it is not, and on DeX and ChromeOS an unhandled right
     * button turns into the system's BACK. The pointers refused, by id.
     */
    private int refusedPointers;

    private boolean pointerRefused(MotionEvent e, int i) {
        if ((e.getSource() & android.view.InputDevice.SOURCE_MOUSE) != android.view.InputDevice.SOURCE_MOUSE
            && book.kindOfId(e.getDeviceId()) != InputBook.POINTER) return false;
        int held = e.getButtonState();
        return held != 0 && (held & MotionEvent.BUTTON_PRIMARY) == 0;
    }

    private static int bitOfPointer(int id) {
        return id >= 0 && id < 32 ? 1 << id : 0;
    }

    /*
     * A PALM IS NOT A RELEASE (U7). From Android 13 a pointer the system has
     * judged to be a palm goes up with FLAG_CANCELED: that is UIKit's
     * touchesCancelled, phase 4, not phase 3 - a palm on a button is not its
     * press. It goes down through touchPalm, which names it in the report.
     */
    private static boolean palmUp(MotionEvent e) {
        return android.os.Build.VERSION.SDK_INT >= 33 && (e.getFlags() & MotionEvent.FLAG_CANCELED) != 0;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (!layerLoaded) return super.onTouchEvent(e);
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int i = e.getActionIndex();
                if (e.getActionMasked() == MotionEvent.ACTION_DOWN) refusedPointers = 0;
                book.touchDown(e, i);
                if (pointerRefused(e, i)) {
                    refusedPointers |= bitOfPointer(e.getPointerId(i));
                    return true;
                }
                touch(e.getPointerId(i), 0, e.getX(i) - frameOffX, e.getY(i) - frameOffY);
                return true;
            }
            case MotionEvent.ACTION_MOVE:
                /*
                 * Only the last sample of a batch — and that is not a
                 * simplification. UIKit itself merges a finger's samples into
                 * one touchesMoved: per frame (the fine samples lie apart, in
                 * coalescedTouches), so the game is built for exactly that
                 * stream. Unrolling Android's batch back out is not allowed:
                 * the engine does not process everything in a row, and
                 * scrolling does not speed up from it, it disappears.
                 */
                for (int i = 0; i < e.getPointerCount(); i++) {
                    if ((refusedPointers & bitOfPointer(e.getPointerId(i))) != 0) continue;
                    touch(e.getPointerId(i), 1, e.getX(i) - frameOffX, e.getY(i) - frameOffY);
                }
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int i = e.getActionIndex();
                int bit = bitOfPointer(e.getPointerId(i));
                if ((refusedPointers & bit) != 0) { refusedPointers &= ~bit; return true; }
                if (palmUp(e)) {
                    book.palm(e);
                    touchPalm(e.getPointerId(i), e.getX(i) - frameOffX, e.getY(i) - frameOffY);
                } else {
                    touch(e.getPointerId(i), 3, e.getX(i) - frameOffX, e.getY(i) - frameOffY);
                }
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    if ((refusedPointers & bitOfPointer(e.getPointerId(i))) != 0) continue;
                    touch(e.getPointerId(i), 4, e.getX(i) - frameOffX, e.getY(i) - frameOffY);
                }
                refusedPointers = 0;
                return true;
        }
        return super.onTouchEvent(e);
    }


    /* ---------- gamepad ---------- */

    /*
     * The game reads the gamepad through GameController, while Android sends
     * buttons and axes in different events. Here they are brought into one
     * picture and go downwards whole: down there a poll every frame is waiting
     * for them, not an event queue.
     *
     * The order of the bits and the axes must match pad.c — PadDb holds them,
     * the only place where they are tied together, so they are only ever
     * changed in pairs.
     */
    private static final int BTN_A = PadDb.BTN_A, BTN_B = PadDb.BTN_B, BTN_X = PadDb.BTN_X, BTN_Y = PadDb.BTN_Y,
            BTN_L1 = PadDb.BTN_L1, BTN_R1 = PadDb.BTN_R1, BTN_MENU = PadDb.BTN_MENU, BTN_OPTIONS = PadDb.BTN_OPTIONS,
            BTN_L3 = PadDb.BTN_L3, BTN_R3 = PadDb.BTN_R3,
            BTN_UP = PadDb.BTN_UP, BTN_DOWN = PadDb.BTN_DOWN, BTN_LEFT = PadDb.BTN_LEFT, BTN_RIGHT = PadDb.BTN_RIGHT;
    private static final int AX_LEFT_X = PadDb.AX_LEFT_X, AX_LEFT_Y = PadDb.AX_LEFT_Y,
            AX_RIGHT_X = PadDb.AX_RIGHT_X, AX_RIGHT_Y = PadDb.AX_RIGHT_Y,
            AX_HAT_X = PadDb.AX_HAT_X, AX_HAT_Y = PadDb.AX_HAT_Y, AX_L2 = PadDb.AX_L2, AX_R2 = PadDb.AX_R2;

    /*
     * ONE CONTROLLER PER DEVICE (the owner, 28.09.2026, for the game's co-op).
     * Every pad has a slot of its own downstairs (pad.c: four pads, slots
     * 0..3) and goes down as its own GCController, so the game can seat a
     * player on each. A fifth pad finds no slot and is ignored. The hardware
     * keyboard is NOT a pad: the game has a keyboard device of its own, with
     * its own bindings in Controls, and the keys go to it through the shell
     * (keyToGame) - a keyboard turned into a fifth GCController took every key
     * from it (the owner, 28.09.2026: "the keyboard stopped working, and
     * Controls shows a second keyboard").
     */
    private static final int PAD_SLOTS = 4;
    /*
     * THE PADS ARE THE PROCESS'S (06.10.2026, with ONE PROCESS, ONE TRUTH):
     * the slots downstairs (pad.c, devices.c) outlive a window, so the books
     * that say which device sits in which slot - slotUsed, pads,
     * slotOfDescriptor and the device book - live as long as they do. A
     * window rebuilt in the same process used to start with all three empty
     * while every slot was still taken below: its first syncPads seated each
     * pad again, from slot 0 up and with no memory of where it sat, so a
     * co-op player could find himself on another player's controller. Only
     * the listeners are a window's, registered and unregistered in pairs
     * (onResume / onPause, onDestroy).
     */
    private static final boolean[] slotUsed = new boolean[PAD_SLOTS];
    private static final float[] NO_AXES = new float[8];

    /*
     * ONE STATE PER DEVICE (the owner's items 10 and 24, 23.09.2026).
     *
     * Every pad used to write straight into one shared state, and two
     * devices - a handheld's own pad and a Bluetooth one, Joy-Cons, a pad that
     * also shows up as a second joystick - overwrote each other on every event:
     * a direction held on one flickered with the other's zeros, each flicker a
     * new press, and a menu list ran away at the event rate. Now each device
     * keeps its own state and goes down into a slot of its own (since
     * 28.09.2026 the slots are separate controllers to the game; they used to
     * be merged into one). A device that goes, cancels its gesture, or is left
     * while the game pauses stops counting at once.
     *
     * The triggers: Android hands them over as axes (LTRIGGER/BRAKE,
     * RTRIGGER/GAS), as the L2/R2 keys (the built-in pad of Anbernic's
     * handhelds and Switch-style pads) or as both; they were read as axes only,
     * and the keys fell through to the system. Now a trigger is the higher of
     * its axis and its key.
     *
     * A PAD AS A CLASS (batch 2, U5). On iOS an axis reaches the game already
     * worked by the system - nought at rest, a trigger 0..1. Android hands the
     * raw axis with the device's own range, so that is restored here from the
     * device's own data, never from a number of ours:
     *   - which of Z/RZ/RX/RY is a stick and which a trigger is decided by the
     *     value at REST: a trigger lies at its minimum, a stick in the middle
     *     (a pad with no trigger axes sends them on Z/RZ resting at -1, and a
     *     DualShock on RX/RY; the old guess by the axes a device declares read
     *     a resting trigger as 0.5 - "held" for ever);
     *   - a trigger is (v - min) / (max - min);
     *   - a stick loses the flat centre the device itself declares (getFlat),
     *     radially: nought inside, (|v| - flat) / (1 - flat) outside. A device
     *     that declares no flat gets nothing cut; SELF-CHECK pad names it if it
     *     rests off centre.
     * A built-in pad (a gaming phone's own) takes a slot only on its first real
     * input, and a pad that comes back takes the slot its descriptor had.
     *
     * THE LAYOUT FIRST (the owner, 02.10.2026; PadDb): a pad is named as SDL
     * names it on Android and its layout taken from SDL's gamepad database;
     * a pad not in it gets Android's standard layout when it declares nothing
     * that layout leaves open. The roles by resting value above are the third
     * and last way - for the pads neither covers, and for a "standard" pad
     * whose Z/RZ turn out to rest at their minimum (triggers, not a stick).
     */
    private static final int ROLE_NONE = 0, ROLE_UNKNOWN = 1, ROLE_STICK = 2, ROLE_TRIGGER = 3;
    private static final String[] ROLE_NAMES = { "-", "?", "stick", "trigger" };

    /* one axis of one device: its range, taken once, and its role */
    private static final class PadAxis {
        final int axis;
        final boolean present;
        final float min, max, flat;          /* flat as a share of the half range, 0..1 */
        int role;
        long centreSince;                    /* a "trigger" seen resting in the middle since */

        PadAxis(android.view.InputDevice dev, int axis) {
            this.axis = axis;
            android.view.InputDevice.MotionRange r = dev != null ? dev.getMotionRange(axis) : null;
            present = r != null && r.getMax() > r.getMin();
            min = present ? r.getMin() : -1f;
            max = present ? r.getMax() : 1f;
            float half = (max - min) * 0.5f;
            flat = present && half > 0f ? Math.max(0f, Math.min(0.9f, r.getFlat() / half)) : 0f;
            role = present ? ROLE_UNKNOWN : ROLE_NONE;
        }

        /* the value as a stick's, -1..1 */
        float centred(float v) {
            return present ? Math.max(-1f, Math.min(1f, 2f * (v - min) / (max - min) - 1f)) : 0f;
        }

        /* the value as a trigger's, 0..1 */
        float travel(float v) {
            return present ? Math.max(0f, Math.min(1f, (v - min) / (max - min))) : 0f;
        }

        /* judged at rest: lying at the minimum is a trigger, in the middle a stick; held is judged later */
        void judge(float v, long now) {
            if (!present) return;
            float t = travel(v), c = Math.abs(centred(v));
            float nearMin = Math.max(0.05f, flat * 0.5f), nearMid = Math.max(0.15f, flat);
            if (role == ROLE_UNKNOWN) {
                if (t <= nearMin) role = ROLE_TRIGGER;
                else if (c <= nearMid) role = ROLE_STICK;
            } else if (role == ROLE_TRIGGER) {
                /* a stick held to its edge at the first look: it comes back to the middle and stays */
                if (c <= nearMid) {
                    if (centreSince == 0) centreSince = now;
                } else {
                    centreSince = 0;
                }
            }
        }
    }

    private static final class PadDev {
        int id;
        int slot = -1;               /* the slot downstairs, -1 when every slot is taken (or not yet) */
        String descriptor = "";
        boolean waiting;             /* a built-in pad: no slot until it is really used */
        boolean woken;               /* it was, and took its slot on that first input */
        int buttons;
        final float[] axes = new float[8];
        final float[] rawAxes = new float[8];     /* the same before the stick's flat is cut */
        boolean hatByAxis, l2Key, r2Key;
        PadAxis x, y, z, rz, rx, ry, lt, rt, brake, gas;
        /* the first seconds with the slot: how the pad rests */
        long restFrom;
        int restEvents;
        boolean restTold;
        final float[] restRaw = new float[8], restOut = new float[8];
        /* the layout (PadDb): the database's or Android's standard one; null - the resting guess */
        PadDb.Layout layout;
        int layoutSource = PadDb.SOURCE_GUESSED;
        float[] layoutRaw;                   /* one event's values of the layout's axes */
        boolean standardChecked;             /* a standard layout's Z/RZ seen resting where a stick rests */
        /* a Joy-Con half (JoyCons): its side, its organs, and the other half it is joined with */
        int joy = JoyCons.NONE;
        JoyCons.Half half;
        PadDev mate;                         /* joined: both halves share one slot, the pair is one pad */
        /* its pad pictures and its name to the game (PadIcons), by its vendor, name and database line */
        int iconFamily = PadIcons.XBOX;
        int product;
    }
    private static final android.util.SparseArray<PadDev> pads = new android.util.SparseArray<>();
    /* where every pad sat, by its descriptor: one that comes back takes its own slot again */
    private static final java.util.HashMap<String, Integer> slotOfDescriptor = new java.util.HashMap<>();

    /* the book of the devices (InputBook): the one classifier, and the report's passports */
    private static final InputBook book = new InputBook(REPORT ? MainActivity::say : null);

    /* the vibrators' one owner: the shakes the game asks for (rumble.c) and the Tweaks probe */
    private static Haptics haptics;     /* one per process: the layer binds the first and only it (rumble.c) */

    private PadDev padOf(android.view.InputEvent e) {
        return padById(e.getDeviceId());
    }

    /*
     * The pad of an Android device id, made the first time. Which device is a
     * pad is the book's to say (InputBook: a keyboard that claims a gamepad
     * source is NOT one - the 19th round, 28.09.2026, the owner's keyboard took
     * pad slot 2). A built-in pad waits for its first real input before it
     * takes a slot: a gaming phone's own "pad" that nobody touches is not a
     * paired pad, and as one it hid the touch controls and, noisy, took player
     * 0 from the screen (U5).
     */
    private PadDev padById(int id) {
        PadDev d = pads.get(id);
        if (d == null) {
            d = new PadDev();
            d.id = id;
            android.view.InputDevice dev = android.view.InputDevice.getDevice(id);
            if (dev != null && dev.getDescriptor() != null) d.descriptor = dev.getDescriptor();
            d.x = new PadAxis(dev, MotionEvent.AXIS_X);
            d.y = new PadAxis(dev, MotionEvent.AXIS_Y);
            d.z = new PadAxis(dev, MotionEvent.AXIS_Z);
            d.rz = new PadAxis(dev, MotionEvent.AXIS_RZ);
            d.rx = new PadAxis(dev, MotionEvent.AXIS_RX);
            d.ry = new PadAxis(dev, MotionEvent.AXIS_RY);
            d.lt = new PadAxis(dev, MotionEvent.AXIS_LTRIGGER);
            d.rt = new PadAxis(dev, MotionEvent.AXIS_RTRIGGER);
            d.brake = new PadAxis(dev, MotionEvent.AXIS_BRAKE);
            d.gas = new PadAxis(dev, MotionEvent.AXIS_GAS);
            /* the sticks and the triggers are what they say they are */
            d.x.role = d.x.present ? ROLE_STICK : ROLE_NONE;
            d.y.role = d.y.present ? ROLE_STICK : ROLE_NONE;
            for (PadAxis t : new PadAxis[] { d.lt, d.rt, d.brake, d.gas }) if (t.present) t.role = ROLE_TRIGGER;
            String dbName = "";
            if (dev != null) {
                try {
                    PadDb.Pick pick = PadDb.shared().choose(PadDb.describe(dev));
                    dbName = PadIcons.dbNameOf(pick.line);
                    d.layout = pick.layout;
                    d.layoutSource = pick.source;
                    if (d.layout != null) d.layoutRaw = new float[d.layout.dev.axes.length];
                } catch (RuntimeException ex) {
                    d.layout = null;             /* a device Android cannot describe: the guess */
                    d.layoutSource = PadDb.SOURCE_GUESSED;
                    if (REPORT) say("pad: device id " + id + " has no layout (" + ex + "): guessed");
                }
            }
            if (dev != null) {
                d.joy = JoyCons.side(dev.getVendorId(), dev.getProductId(), dev.getName());
                if (d.joy != JoyCons.NONE) d.half = joyHalf(d.joy, dev);
                d.product = dev.getProductId();
                d.iconFamily = d.joy != JoyCons.NONE ? PadIcons.SWITCH
                        : PadIcons.family(dev.getVendorId(), dev.getProductId(), dev.getName(), dbName, false);
                if (REPORT) say("pad: device id " + id + " (" + Integer.toHexString(dev.getVendorId()) + ":"
                        + Integer.toHexString(dev.getProductId()) + ") draws as " + PadIcons.NAMES[d.iconFamily]);
            }
            pads.put(id, d);
            d.waiting = book.builtIn(id);
            if (joinJoyCon(d)) return d;         /* the other half is here: the pair is one pad, in its slot */
            if (!d.waiting) takeSlot(d);
            else if (REPORT) say("pad: device id " + id + " is built in: no slot until it is used");
        }
        return d;
    }

    private boolean slotRemembered(int k, String except) {
        for (java.util.Map.Entry<String, Integer> e : slotOfDescriptor.entrySet())
            if (e.getValue() == k && !e.getKey().equals(except)) return true;
        return false;
    }

    /* the slots as the report shows them: which Android device sits in each, "-" for a free one */
    private static String slotsText() {
        StringBuilder t = new StringBuilder();
        for (int k = 0; k < PAD_SLOTS; k++) {
            String who = "-";
            for (int i = 0; i < pads.size(); i++)
                if (pads.valueAt(i).slot == k) who = (who.equals("-") ? "id " : who + "+") + pads.keyAt(i);   /* a Joy-Con pair: both */
            if (k > 0) t.append(", ");
            t.append(k).append(": ").append(who);
        }
        return t.toString();
    }

    /* a slot for the pad: its own from before if free, else a free one nobody else had, else any free one */
    private void takeSlot(PadDev d) {
        d.waiting = false;
        Integer own = slotOfDescriptor.get(d.descriptor);
        boolean back = own != null && !slotUsed[own];
        if (back) d.slot = own;
        for (int pass = 0; pass < 2 && d.slot < 0; pass++) {
            if (pass == 1) {
                /*
                 * EVERY SLOT TAKEN: a pad that went away and came back has a
                 * new Android id, and the listener that frees the old one
                 * runs after the events of the new one (the 19th round: "id 35
                 * takes slot -1", then ids 31-33 left). So the ones that are
                 * gone are dropped here, and the newcomer takes a freed slot.
                 */
                for (int i = pads.size() - 1; i >= 0; i--)
                    if (android.view.InputDevice.getDevice(pads.keyAt(i)) == null) dropPad(pads.keyAt(i));
                if (own != null && !slotUsed[own]) { d.slot = own; back = true; break; }
            }
            for (int k = 0; k < PAD_SLOTS && d.slot < 0; k++)
                if (!slotUsed[k] && !slotRemembered(k, d.descriptor)) d.slot = k;
            for (int k = 0; k < PAD_SLOTS && d.slot < 0; k++)
                if (!slotUsed[k]) d.slot = k;
        }
        if (d.slot >= 0) {
            slotUsed[d.slot] = true;
            if (d.descriptor.length() > 0) slotOfDescriptor.put(d.descriptor, d.slot);
            d.restFrom = android.os.SystemClock.uptimeMillis();
            d.restEvents = 0;
            d.restTold = false;
            java.util.Arrays.fill(d.restRaw, 0f);
            java.util.Arrays.fill(d.restOut, 0f);
            if (layerLoaded) {
                /* before the slot is seen connected: the game names its device the moment it makes it */
                tellFamily(d);
                padState(d.slot, true, 0, NO_AXES);
                padIdentity(d.slot, d.id, d.descriptor.substring(0, Math.min(8, d.descriptor.length())));
            }
        }
        if (REPORT) say("pad: device id " + d.id + " takes slot " + d.slot + (back ? " (its own from before)" : ""));
    }

    /* a Joy-Con half's organs and stick, from the device's joystick-class ranges */
    private static JoyCons.Half joyHalf(int side, android.view.InputDevice dev) {
        java.util.ArrayList<android.view.InputDevice.MotionRange> rs = new java.util.ArrayList<>();
        for (android.view.InputDevice.MotionRange r : dev.getMotionRanges())
            if ((r.getSource() & android.view.InputDevice.SOURCE_CLASS_JOYSTICK) != 0) rs.add(r);
        int n = rs.size();
        int[] axes = new int[n];
        float[] lo = new float[n], hi = new float[n], fl = new float[n];
        for (int i = 0; i < n; i++) {
            axes[i] = rs.get(i).getAxis();
            lo[i] = rs.get(i).getMin();
            hi[i] = rs.get(i).getMax();
            fl[i] = rs.get(i).getFlat();
        }
        return new JoyCons.Half(side, axes, lo, hi, fl);
    }

    /* a slot's pad pictures and the name the game is given for it (PadIcons; a joined pair is one pad) */
    private void tellFamily(PadDev d) {
        if (d.slot < 0) return;
        padFamily(d.slot, d.iconFamily, PadIcons.iosName(d.iconFamily, d.product, d.joy, d.mate != null));
    }

    private static String desc8(PadDev d) {
        return d.descriptor.substring(0, Math.min(8, d.descriptor.length()));
    }

    /*
     * TWO JOY-CONS, ONE PAD (the owner, 06.10.2026; JoyCons). A half that
     * finds the other half seated and alone joins it: the same slot, so the
     * game keeps the one controller it has and both halves now drive it. The
     * slot's identity is the LEFT half's, whichever came first, so the pair
     * is the same "pad:<descriptor>" to the co-op ledger every time.
     */
    private boolean joinJoyCon(PadDev d) {
        if (d.joy == JoyCons.NONE || d.half == null) return false;
        int want = d.joy == JoyCons.LEFT ? JoyCons.RIGHT : JoyCons.LEFT;
        PadDev o = null;
        for (int i = 0; i < pads.size() && o == null; i++) {
            PadDev c = pads.valueAt(i);
            if (c != d && c.joy == want && c.half != null && c.mate == null && c.slot >= 0
                && android.view.InputDevice.getDevice(c.id) != null) o = c;
        }
        if (o == null) return false;
        d.mate = o;
        o.mate = d;
        d.waiting = false;
        d.slot = o.slot;
        for (PadDev h : new PadDev[] { d, o }) {
            h.buttons = 0;
            h.l2Key = h.r2Key = false;
            java.util.Arrays.fill(h.axes, 0f);
            java.util.Arrays.fill(h.rawAxes, 0f);
            if (h.layout != null) h.layout.release();
            h.half.release();
        }
        PadDev l = d.joy == JoyCons.LEFT ? d : o, r = l.mate;
        if (d.descriptor.length() > 0) slotOfDescriptor.put(d.descriptor, d.slot);
        if (layerLoaded) {
            padIdentity(d.slot, l.id, desc8(l));
            tellFamily(l);
        }
        if (REPORT) say("pad: joined Joy-Con pair: L id " + l.id + " (stick " + l.half.stickName() + ") + R id " + r.id
                + " (stick " + r.half.stickName() + ") -> one pad in slot " + d.slot);
        pushJoined(d);
        return true;
    }

    /* a joined pair's state: the two halves merged (JoyCons.merge), into the pair's slot */
    private static final float[] JOINED = new float[8];

    private void pushJoined(PadDev d) {
        PadDev l = d.joy == JoyCons.LEFT ? d : d.mate, r = l.mate;
        int bits = JoyCons.merge(l.half, r.half, JOINED);
        for (PadDev h : new PadDev[] { l, r }) {
            h.buttons = bits;
            h.l2Key = h.r2Key = false;
            System.arraycopy(JOINED, 0, h.axes, 0, 8);
            System.arraycopy(JOINED, 0, h.rawAxes, 0, 8);
        }
        pushPad(l);
    }

    /* a pad that is gone: its slot is told and freed - or, half of a pair, left to the other half */
    private void dropPad(int id) {
        PadDev d = pads.get(id);
        if (d == null) return;
        pads.remove(id);
        if (d.mate != null) {
            PadDev m = d.mate;
            m.mate = null;
            d.mate = null;
            m.buttons = 0;
            m.l2Key = m.r2Key = false;
            java.util.Arrays.fill(m.axes, 0f);
            java.util.Arrays.fill(m.rawAxes, 0f);
            if (m.layout != null) m.layout.release();
            if (m.half != null) m.half.release();
            if (layerLoaded && m.slot >= 0) {
                padIdentity(m.slot, m.id, desc8(m));
                tellFamily(m);
            }
            if (REPORT) say("pad: Joy-Con pair split: id " + id + " left, " + JoyCons.sideName(m.joy) + " id " + m.id
                    + " keeps slot " + m.slot + " alone");
            pushPad(m);
            return;
        }
        if (d.slot >= 0) {
            slotUsed[d.slot] = false;
            if (layerLoaded) {
                padState(d.slot, false, 0, NO_AXES);
                padIdentity(d.slot, -1, "");
            }
        }
        if (REPORT) say("pad: device id " + id + " left slot " + d.slot);
    }

    /* a stick's pair, the device's own flat cut off radially (U5) */
    private static void stick(PadAxis ax, PadAxis ay, float vx, float vy, boolean flipY, float[] out, float[] raw, int at) {
        float x = ax.centred(vx), y = ay.centred(vy);
        if (flipY) y = -y;
        raw[at] = x;
        raw[at + 1] = y;
        float flat = Math.max(ax.flat, ay.flat);
        float r = (float) Math.hypot(x, y);
        if (r <= flat || r <= 0f) {
            out[at] = out[at + 1] = 0f;
        } else {
            float k = (r - flat) / (1f - flat) / r;
            out[at] = x * k;
            out[at + 1] = y * k;
        }
    }

    /* the right stick's pair: Z/RZ if both rest in the middle, else RX/RY if they do */
    private static PadAxis[] rightStick(PadDev d) {
        if (d.z.role == ROLE_STICK && d.rz.role == ROLE_STICK) return new PadAxis[] { d.z, d.rz };
        if (d.rx.role == ROLE_STICK && d.ry.role == ROLE_STICK) return new PadAxis[] { d.rx, d.ry };
        return null;
    }

    /* a trigger: the highest of the axes that lie at their minimum at rest */
    private static float trigger(MotionEvent e, PadAxis... axes) {
        float best = 0f;
        for (PadAxis a : axes)
            if (a.role == ROLE_TRIGGER) best = Math.max(best, a.travel(e.getAxisValue(a.axis)));
        return best;
    }

    /* the pad's axes out of one event, by the roles its resting values gave them */
    private void readAxes(PadDev d, MotionEvent e) {
        long now = android.os.SystemClock.uptimeMillis();
        for (PadAxis a : new PadAxis[] { d.z, d.rz, d.rx, d.ry }) a.judge(e.getAxisValue(a.axis), now);
        /* a "trigger" that has rested in the middle for a second, its partner too: a stick after all */
        PadAxis[][] pairs = { { d.z, d.rz }, { d.rx, d.ry } };
        for (PadAxis[] p : pairs)
            for (int i = 0; i < 2; i++) {
                PadAxis a = p[i], other = p[1 - i];
                if (a.role == ROLE_TRIGGER && a.centreSince != 0 && now - a.centreSince >= 1000
                    && (other.role == ROLE_STICK || (other.centreSince != 0 && now - other.centreSince >= 1000))) {
                    a.role = ROLE_STICK;
                    a.centreSince = 0;
                    if (REPORT) say("pad: slot " + d.slot + " axis " + MotionEvent.axisToString(a.axis)
                            + " rests in the middle: a stick, not a trigger");
                }
            }
        /* On Android Y grows downwards, on GameController upwards: flipped here, once */
        stick(d.x, d.y, e.getAxisValue(MotionEvent.AXIS_X), e.getAxisValue(MotionEvent.AXIS_Y), true,
              d.axes, d.rawAxes, AX_LEFT_X);
        PadAxis[] right = rightStick(d);
        if (right != null) {
            stick(right[0], right[1], e.getAxisValue(right[0].axis), e.getAxisValue(right[1].axis), true,
                  d.axes, d.rawAxes, AX_RIGHT_X);
        } else {
            d.axes[AX_RIGHT_X] = d.axes[AX_RIGHT_Y] = d.rawAxes[AX_RIGHT_X] = d.rawAxes[AX_RIGHT_Y] = 0f;
        }
        d.axes[AX_L2] = d.rawAxes[AX_L2] = trigger(e, d.lt, d.brake, d.z, d.rx);
        d.axes[AX_R2] = d.rawAxes[AX_R2] = trigger(e, d.rt, d.gas, d.rz, d.ry);
    }

    private static final String[] AXIS_LABELS = { "LX", "LY", "RX", "RY", "HX", "HY", "L2", "R2" };
    private static final String[] LAYOUT_NAMES = { "-", "db", "android standard", "guessed" };

    /* a layout's pad, read into our model and sent down */
    private void layoutPush(PadDev d) {
        d.buttons = d.layout.read(d.axes, d.rawAxes);
        pushPad(d);
    }

    /*
     * A standard layout's right stick is Z/RZ. A device whose Z or RZ lies at
     * its minimum at rest sends its triggers there instead (U5): the docs'
     * layout does not fit it, and the resting guess - the last way - takes the
     * pad from this event on. Both seen resting in the middle: the check ends.
     */
    private void standardCheck(PadDev d, MotionEvent e) {
        if (d.layoutSource != PadDb.SOURCE_STANDARD || d.standardChecked) return;
        long now = android.os.SystemClock.uptimeMillis();
        d.z.judge(e.getAxisValue(d.z.axis), now);
        d.rz.judge(e.getAxisValue(d.rz.axis), now);
        if (d.z.role == ROLE_TRIGGER || d.rz.role == ROLE_TRIGGER) {
            if (REPORT) say("pad: slot " + d.slot + " layout android standard -> guessed: "
                    + (d.z.role == ROLE_TRIGGER ? "Z" : "RZ") + " rests at its minimum (a trigger, not a stick)");
            d.layout = null;
            d.layoutRaw = null;
            d.layoutSource = PadDb.SOURCE_GUESSED;
        } else if (d.z.role != ROLE_UNKNOWN && d.rz.role != ROLE_UNKNOWN) {
            d.standardChecked = true;
        }
    }

    /*
     * HOW THE PAD RESTS, once per connection (report builds): the largest
     * value of every axis in the first two seconds with its slot and no button
     * held, before and after the flat is cut, with the roles the axes got. An
     * axis resting at the game's own "held" threshold (0.3, 0x946378) is a pad
     * that takes player 0 from the screen by itself: SELF-CHECK pad says so.
     */
    private void restWatch(PadDev d) {
        if (!REPORT || d.restTold || d.slot < 0) return;
        long now = android.os.SystemClock.uptimeMillis();
        boolean over = now - d.restFrom >= 2000 || d.buttons != 0 || d.l2Key || d.r2Key;
        if (!over) {
            d.restEvents++;
            for (int i = 0; i < 8; i++) {
                d.restRaw[i] = Math.max(d.restRaw[i], Math.abs(d.rawAxes[i]));
                d.restOut[i] = Math.max(d.restOut[i], Math.abs(d.axes[i]));
            }
            return;
        }
        d.restTold = true;
        StringBuilder s = new StringBuilder(200);
        s.append("pad: slot ").append(d.slot).append(" at rest, ").append(d.restEvents).append(" events")
         .append(d.woken ? " (built in, from its first input on)" : "").append(':');
        String worst = null;
        float worstV = 0f;
        for (int i = 0; i < 8; i++) {
            s.append(String.format(java.util.Locale.ROOT, " %s %.2f/%.2f", AXIS_LABELS[i], d.restRaw[i], d.restOut[i]));
            if (d.restOut[i] > worstV) { worstV = d.restOut[i]; worst = AXIS_LABELS[i]; }
        }
        s.append(" | layout ").append(LAYOUT_NAMES[d.layoutSource]);
        s.append(" | roles Z ").append(ROLE_NAMES[d.z.role]).append(" RZ ").append(ROLE_NAMES[d.rz.role])
         .append(" RX ").append(ROLE_NAMES[d.rx.role]).append(" RY ").append(ROLE_NAMES[d.ry.role])
         .append(String.format(java.util.Locale.ROOT, " flat X %.3f Z %.3f RX %.3f", d.x.flat, d.z.flat, d.rx.flat));
        say(s.toString());
        if (worstV >= 0.3f)
            say(String.format(java.util.Locale.ROOT, "SELF-CHECK pad: FAIL resting axis %s=%.2f >= 0.3 on slot %d",
                    worst, worstV, d.slot));
        else
            say("SELF-CHECK pad: OK resting on slot " + d.slot + (d.restEvents == 0 ? " (no motion at rest)" : ""));
    }

    /* real input from a built-in pad: a button, or an axis past the game's own "held" (0.3) */
    private static boolean realInput(PadDev d) {
        if (d.buttons != 0 || d.l2Key || d.r2Key) return true;
        for (float v : d.axes) if (Math.abs(v) >= 0.3f) return true;
        return false;
    }

    /* one device's state, downwards into its slot */
    private void pushPad(PadDev d) {
        if (d.waiting) {
            if (!realInput(d)) return;
            d.woken = true;
            takeSlot(d);                /* the built-in pad is used: now it is a pad */
        }
        if (!layerLoaded || d.slot < 0) return;
        restWatch(d);
        float[] a = d.axes;
        if (d.l2Key || d.r2Key) {
            a = d.axes.clone();
            if (d.l2Key) a[AX_L2] = Math.max(a[AX_L2], 1f);
            if (d.r2Key) a[AX_R2] = Math.max(a[AX_R2], 1f);
        }
        padState(d.slot, true, d.buttons, a);
    }

    /* everything let go: a pause, a lost focus - nothing held may survive it */
    private void releasePads() {
        for (int i = 0; i < pads.size(); i++) {
            PadDev d = pads.valueAt(i);
            d.buttons = 0;
            d.l2Key = d.r2Key = false;
            java.util.Arrays.fill(d.axes, 0f);
            java.util.Arrays.fill(d.rawAxes, 0f);
            if (d.layout != null) d.layout.release();   /* its joystick too: nothing held survives */
            if (d.half != null) d.half.release();       /* a Joy-Con half's organs */
            pushPad(d);
        }
    }

    /*
     * Is the event a pad's? The book says (InputBook.kindOfEvent): the device's
     * kind, a hybrid's pad buttons to the pad. A D-pad source alone does not
     * make a pad (a keyboard's arrows are KEYBOARD | DPAD - the testers,
     * 23.09.2026), and neither does a keyboard's gamepad source.
     */
    private boolean isGamepad(android.view.InputEvent e) {
        return book.padEvent(e);
    }

    /*
     * A key the book drops (a FLAG_FALLBACK copy of a pad's or a keyboard's
     * own key), or a pad's button the port has no bit for: taken from the
     * system either way - own events are always answered true (U5), and an
     * unanswered pad button came back as the system's fallback (B as BACK).
     */
    private int bitlessTold;

    private boolean keyTaken(int code, KeyEvent e) {
        if (book.keyEvent(e)) return true;
        /* a joined Joy-Con half: every key is the pair's (JoyCons), none goes on to the system */
        if (isGamepad(e)) {
            PadDev jd = padOf(e);
            if (jd.mate != null) {
                int act = e.getAction();
                if ((act == KeyEvent.ACTION_DOWN || act == KeyEvent.ACTION_UP)
                    && jd.half.key(code, e.getScanCode(), act == KeyEvent.ACTION_DOWN)) pushJoined(jd);
                return true;
            }
        }
        /*
         * A pad with a layout (PadDb): every key SDL counts as one of the
         * joystick's buttons is the layout's - the pad's own buttons, the
         * d-pad, BACK/MENU, L2/R2 - and what that button means is the
         * layout's to say. A pad's other gamepad button is taken from the
         * system as below; any other key goes on as before.
         */
        if (isGamepad(e) && (e.getAction() == KeyEvent.ACTION_DOWN || e.getAction() == KeyEvent.ACTION_UP)) {
            PadDev d = padOf(e);
            if (d.layout != null) {
                if (d.layout.key(code, e.getScanCode(), e.getAction() == KeyEvent.ACTION_DOWN)) {
                    layoutPush(d);
                    return true;
                }
                if (!KeyEvent.isGamepadButton(code)) return false;
                if (REPORT && e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0 && bitlessTold < 20) {
                    bitlessTold++;
                    say("pad: code " + code + " of device " + e.getDeviceId() + " is no button of SDL's: taken from the system");
                }
                return true;
            }
        }
        if (!isGamepad(e) || bitOf(code) >= 0 || !KeyEvent.isGamepadButton(code)
            || code == KeyEvent.KEYCODE_BUTTON_L2 || code == KeyEvent.KEYCODE_BUTTON_R2) return false;
        if (REPORT && e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0 && bitlessTold < 20) {
            bitlessTold++;
            say("pad: code " + code + " of device " + e.getDeviceId() + " has no bit: taken from the system");
        }
        return true;
    }

    /*
     * Enumerating devices is not for the hot path: axis events come in their
     * hundreds per second, and the system does not poll the list for free. So
     * the line-up is taken only where it can change: at startup, on returning
     * to the app, and when the system says a device came or went. A pad that
     * is plugged in but silent is a slot all the same - the game shows it and
     * puts the touch controls aside, as iOS does with a paired pad (a built-in
     * one excepted, see padById).
     */
    private void syncPads() {
        java.util.Set<Integer> seen = new java.util.HashSet<>();
        for (int id : android.view.InputDevice.getDeviceIds()) {
            int kind = book.kindOfId(id);
            if (kind == InputBook.PAD || kind == InputBook.HYBRID) {
                seen.add(id);
                padById(id);
            }
        }
        for (int i = pads.size() - 1; i >= 0; i--)
            if (!seen.contains(pads.keyAt(i))) dropPad(pads.keyAt(i));
        for (int i = 0; i < pads.size(); i++) pushPad(pads.valueAt(i));
    }

    /*
     * A pad unplugged while running. Android does not report it with an input
     * event — the events simply stop coming, and the last known state stays.
     * So a pad taken away in the middle of a run went on counting as alive, and
     * the static sticks stayed hidden: every frame the game asked for the list
     * of controllers and got a non-empty one.
     *
     * A device listener plays the same role as GCControllerDidDisconnect on
     * iOS: the system itself says the list has changed. Polling the list every
     * frame is not allowed, enumerating devices is not free. The book hears
     * it first: a device is judged once, when it comes or changes.
     */
    private android.hardware.input.InputManager.InputDeviceListener listener;

    /*
     * A DEVICE "CHANGED" IS GATHERED FOR 250 ms (hotfix 1.72). Some firmwares
     * send onInputDeviceChanged in bursts - for every battery tick, every
     * sensor - and each one judged the device again (two hasKeys calls into
     * the input service) and walked the pads (syncPads): up to 2 ms of the UI
     * thread each, the thread the touches come through. Now the ids of a burst
     * are judged once each and the pads walked once, a quarter second after the
     * first; a device that comes or goes is still answered at once, and a
     * pending batch is done on the spot when the listener goes (the pause).
     */
    private final java.util.HashSet<Integer> changedIds = new java.util.HashSet<>();
    private boolean changePosted;
    private final android.os.Handler changeHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable changeRun = () -> {
        changePosted = false;
        if (changedIds.isEmpty()) return;
        Integer[] ids = changedIds.toArray(new Integer[0]);
        changedIds.clear();
        for (int id : ids) book.changed(id);
        syncPads();
    };

    private void watchPads(boolean on) {
        android.hardware.input.InputManager im =
                (android.hardware.input.InputManager) getSystemService(INPUT_SERVICE);
        if (im == null) return;
        if (on) {
            if (listener != null) return;
            listener = new android.hardware.input.InputManager.InputDeviceListener() {
                @Override public void onInputDeviceAdded(int id) { book.added(id); syncPads(); }
                @Override public void onInputDeviceRemoved(int id) {
                    changedIds.remove(id);
                    PadDev d = pads.get(id);
                    book.removed(id, d != null ? "slot " + d.slot : null);
                    syncPads();
                }
                @Override public void onInputDeviceChanged(int id) {
                    changedIds.add(id);
                    if (!changePosted) { changePosted = true; changeHandler.postDelayed(changeRun, 250); }
                }
            };
            im.registerInputDeviceListener(listener, null);   /* null — the main thread */
        } else if (listener != null) {
            im.unregisterInputDeviceListener(listener);
            listener = null;
            if (changePosted) { changeHandler.removeCallbacks(changeRun); changeRun.run(); }
        }
    }

    private static int bitOf(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: return BTN_A;
            case KeyEvent.KEYCODE_BUTTON_B: return BTN_B;
            case KeyEvent.KEYCODE_BUTTON_X: return BTN_X;
            case KeyEvent.KEYCODE_BUTTON_Y: return BTN_Y;
            case KeyEvent.KEYCODE_BUTTON_L1: return BTN_L1;
            case KeyEvent.KEYCODE_BUTTON_R1: return BTN_R1;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return BTN_L3;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return BTN_R3;
            case KeyEvent.KEYCODE_BUTTON_START:
            case KeyEvent.KEYCODE_MENU: return BTN_MENU;
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_BACK: return BTN_OPTIONS;
            case KeyEvent.KEYCODE_DPAD_UP: return BTN_UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return BTN_DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return BTN_LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return BTN_RIGHT;
            default: return -1;
        }
    }

    /* PROBE, report builds: the keys that are not a pad's, where they went */
    private int keysTold;

    private void tellKey(KeyEvent e, boolean down, String went) {
        if (!REPORT || keysTold >= 40 || (down && e.getRepeatCount() > 0)) return;
        keysTold++;
        say(String.format(java.util.Locale.ROOT, "key: code %d %s source 0x%x device %d -> %s",
                e.getKeyCode(), down ? "down" : "up", e.getSource(), e.getDeviceId(), went));
    }

    /*
     * THE HARDWARE KEYBOARD'S ROAD (batch 2, K1/K2/K4/K9, 01.10.2026).
     *
     * Where a key goes is decided here, in dispatchKeyEvent - before any view,
     * before focus navigation - and by the DEVICE (the book, InputBook), not by
     * the source of one event. A keyboard's key goes to the game unless the
     * seed field is being typed into (uikit.c's text entry: the field is the
     * one taking letters then). Everything else - a pad, a virtual keyboard,
     * the phone's own buttons - takes the road it took before: super, and from
     * there onKeyDown/onKeyUp.
     *
     * The spawner's card is a text field too, but the game's own, drawn by us
     * (spawner.c): its letters are taken BEFORE the IME (the root's
     * dispatchKeyEventPreIme), so a keyboard app switched to another language
     * cannot eat or rewrite them on the way, and they go down the same road as
     * every key, with the character the layout made of the key beside its place.
     *
     * A key of ours is always answered true, and its release takes the road its
     * press took: a key refused on the press and taken on the release (or the
     * other way round) is what makes Android's fallbacks - Esc with Alt as HOME -
     * and a key stuck down in the game.
     */
    private native void keyEvent(int code, boolean down, int mods, int ch, boolean repeat);
    private native boolean keysForSpawner();
    private native void keysScanTable(short[] table);

    private volatile boolean textEntry;         /* the seed field has the focus (showKeyboard) */

    /* the road each key's press took, and the HID usage it went as: its release follows */
    private final java.util.HashMap<Long, Integer> keyRoads = new java.util.HashMap<>();
    private static final int ROAD_SYSTEM = -1;

    private static long keyId(KeyEvent e) {
        return ((long) e.getDeviceId() << 32) | ((long) (e.getScanCode() & 0xffff) << 12) | (e.getKeyCode() & 0xfff);
    }

    /*
     * THE PLACE OF A KEY, NOT ITS LABEL (K2). The game reads keys as HID usages,
     * by place, like GLFW on the PC: WASD on AZERTY is the same four physical
     * keys. getKeyCode() follows the layout (the French and German maps remap
     * letters; some boards send Esc as BACK), getScanCode() is the place the
     * kernel saw, and KeyMap (tools/keymap.py, Chromium's usb/evdev pairing)
     * turns it back into the usage. A key with no scan code - a synthetic one,
     * a virtual keyboard's - falls back on its key code. -1: not a key the game
     * reads, the system's.
     */
    private static int hidFor(KeyEvent e) {
        int hid = KeyMap.hidOfScan(e.getScanCode());
        if (hid == 0) hid = hidOf(e.getKeyCode());
        return hid > 0 ? hid : -1;
    }

    /* the character the layout makes of the key, for the spawner's search alone; 0 for none */
    private static int charOf(KeyEvent e) {
        int ch = e.getUnicodeChar(e.getMetaState());
        return (ch & android.view.KeyCharacterMap.COMBINING_ACCENT) != 0 ? 0 : ch;
    }

    /*
     * One of a keyboard's keys, pressed or let go: into the game or left to the
     * system. Answers true when it is the game's.
     */
    private String keyWent = "";                /* the probe's note of the last decision */

    private boolean keyboardKey(KeyEvent e) {
        int action = e.getAction();
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) {
            keyWent = "the system (not a press)";
            return false;                       /* ACTION_MULTIPLE: a keyboard app's text, see the probe */
        }
        boolean down = action == KeyEvent.ACTION_DOWN;
        long id = keyId(e);
        Integer road = keyRoads.get(id);
        int hid;
        if (road != null && (!down || e.getRepeatCount() > 0)) hid = road;
        else hid = hidFor(e);
        if (down) keyRoads.put(id, hid >= 0 ? hid : ROAD_SYSTEM);
        else keyRoads.remove(id);
        if (hid < 0) {
            keyWent = "the system";
            return false;
        }
        boolean repeat = down && e.getRepeatCount() > 0;
        keyEvent(hid, down, modsOf(e), down ? charOf(e) : 0, repeat);
        /* the probe's note: built only for the report - a String.format per key otherwise (hotfix 1.72) */
        if (REPORT) keyWent = String.format(java.util.Locale.ROOT, "the game, HID 0x%02x%s", hid, repeat ? " (repeat)" : "");
        return true;
    }

    /* the seed field is typed into: then a keyboard's letters are the field's (uikit.c) */
    private boolean seedTyping() {
        return textEntry && !(layerLoaded && keysForSpawner());
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (layerLoaded && book.kindOfEvent(e) == InputBook.KEYBOARD && !seedTyping()) {
            /* the book first: a keyboard's FLAG_FALLBACK copy is the system's echo, dropped (U5) */
            if ((e.getFlags() & KeyEvent.FLAG_FALLBACK) != 0) {
                book.keyEvent(e);
                if (REPORT) keyProbe(e, "post-IME", "dropped (fallback)");
                return true;
            }
            if (keyboardKey(e)) {
                book.keyEvent(e);
                if (REPORT) keyProbe(e, "post-IME", keyWent);
                return true;
            }
            if (REPORT) keyProbe(e, "post-IME", keyWent);
            return super.dispatchKeyEvent(e);
        }
        if (REPORT) keyProbe(e, "post-IME", seedTyping() ? "the seed field" : "super (" + InputBook.name(book.kindOfEvent(e)) + ")");
        return super.dispatchKeyEvent(e);
    }

    /*
     * Before the IME (the root view's dispatchKeyEventPreIme): the spawner's
     * card takes a keyboard's keys here, so no keyboard app sees them (K1).
     * Everything else only passes by the probe, which counts what the IME then
     * lets through (K9, K3 is decided by that count).
     */
    private boolean keyBeforeIme(KeyEvent e) {
        boolean ours = layerLoaded && book.kindOfEvent(e) == InputBook.KEYBOARD
                && (e.getFlags() & KeyEvent.FLAG_FALLBACK) == 0;
        if (ours && keysForSpawner() && keyboardKey(e)) {
            book.keyEvent(e);
            if (REPORT) { keyProbe(e, "pre-IME", keyWent + " (the spawner's card)"); keysTakenEarly++; }
            return true;
        }
        if (REPORT && ours) keyProbePre(e);
        return false;
    }

    /* everything held let go: the app is left with a key down, and the game must not keep it */
    private void releaseKeys(String why) {
        for (java.util.Map.Entry<Long, Integer> k : keyRoads.entrySet())
            if (k.getValue() >= 0 && layerLoaded) keyEvent(k.getValue(), false, 0, 0, false);
        keyRoads.clear();
        if (REPORT) keyProbeSummary(why);
    }

    /* ---------- PROBE, report builds (K9): the raw keys, and what the IME does to them ---------- */
    private int keyrawTold, keysPre, keysPost, keysTakenEarly, keysEaten, keysEatenTold, keysSummaries;
    private final java.util.ArrayDeque<long[]> keysPending = new java.util.ArrayDeque<>();   /* {time, code, action} */
    private android.hardware.input.InputManager.InputDeviceListener keyboardWatch;

    private static String hex(int v) { return "0x" + Integer.toHexString(v); }

    /*
     * The keyboard's language and layout as Android 14 keeps them on the
     * device. Not in the public SDK, so asked by name; a phone that refuses
     * says "-".
     */
    private static String deviceString(android.view.InputDevice dev, String method) {
        if (dev == null || android.os.Build.VERSION.SDK_INT < 34) return "-";
        try {
            Object v = android.view.InputDevice.class.getMethod(method).invoke(dev);
            return v != null ? v.toString() : "-";
        } catch (Throwable t) {
            return "-";
        }
    }

    private void keyProbe(KeyEvent e, String stage, String went) {
        if (stage.startsWith("post") && book.kindOfEvent(e) == InputBook.KEYBOARD) {
            keysPost++;
            for (java.util.Iterator<long[]> it = keysPending.iterator(); it.hasNext(); ) {
                long[] p = it.next();
                if (p[0] == e.getEventTime() && p[1] == e.getKeyCode() && p[2] == e.getAction()) { it.remove(); break; }
            }
        }
        if (keyboardWatch == null) watchKeyboards();
        if (keyrawTold >= 300) return;
        keyrawTold++;
        android.view.InputDevice dev = e.getDevice();
        int uc = e.getUnicodeChar(e.getMetaState());
        String lang = deviceString(dev, "getKeyboardLanguageTag"), layout = deviceString(dev, "getKeyboardLayoutType");
        int wflags = getWindow().getAttributes().flags;
        boolean imeTarget = (wflags & (WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM
                | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)) == 0;
        android.view.View focus = getCurrentFocus();
        say(String.format(java.util.Locale.ROOT,
                "keyraw: #%d %s %s code %d scan %d uc %s rep %d flags %s meta %s | dev %d src %s devsrc %s kbd %s virt %s ext %s"
                + " \"%s\" %04x:%04x lang %s layout %s | ime-target %s focus %s text %s | %s -> %s",
                keyrawTold, stage, e.getAction() == KeyEvent.ACTION_DOWN ? "down" : e.getAction() == KeyEvent.ACTION_UP ? "up" : "multiple",
                e.getKeyCode(), e.getScanCode(), uc >= 0x20 && uc < 0x110000 ? "'" + new String(Character.toChars(uc)) + "'" : hex(uc),
                e.getRepeatCount(), hex(e.getFlags()), hex(e.getMetaState()), e.getDeviceId(), hex(e.getSource()),
                dev != null ? hex(dev.getSources()) : "-",
                dev == null ? "-" : dev.getKeyboardType() == android.view.InputDevice.KEYBOARD_TYPE_ALPHABETIC ? "alpha"
                        : dev.getKeyboardType() == android.view.InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC ? "non-alpha" : "none",
                dev != null && dev.isVirtual() ? "yes" : "no",
                dev != null && android.os.Build.VERSION.SDK_INT >= 29 ? (dev.isExternal() ? "yes" : "no") : "?",
                dev != null ? dev.getName() : "-", dev != null ? dev.getVendorId() : 0, dev != null ? dev.getProductId() : 0,
                lang == null || lang.isEmpty() ? "-" : lang, layout == null || layout.isEmpty() ? "-" : layout,
                imeTarget ? "yes" : "no", focus != null ? focus.getClass().getSimpleName() : "none",
                textEntry ? "yes" : "no", InputBook.name(book.kindOfEvent(e)), went));
    }

    /* a keyboard's key seen before the IME: it is owed to dispatchKeyEvent, or the IME ate it */
    private void keyProbePre(KeyEvent e) {
        keysPre++;
        long now = e.getEventTime();
        while (!keysPending.isEmpty() && now - keysPending.peekFirst()[0] > 1000) {
            long[] p = keysPending.pollFirst();
            keysEaten++;
            if (keysEatenTold < 20) {
                keysEatenTold++;
                say("keys: EATEN by the IME - code " + p[1] + " " + (p[2] == KeyEvent.ACTION_DOWN ? "down" : "up")
                        + " never reached dispatchKeyEvent (" + keysEaten + " so far)");
            }
        }
        if (keysPending.size() < 64) keysPending.addLast(new long[] { now, e.getKeyCode(), e.getAction() });
        if (keysPre % 100 == 0) keyProbeSummary("every 100");
    }

    private void keyProbeSummary(String when) {
        if (keysPre + keysPost + keysTakenEarly == 0 || keysSummaries >= 30) return;
        keysSummaries++;
        say(String.format(java.util.Locale.ROOT,
                "keys: pre-IME %d, post-IME %d, taken before the IME %d, eaten by IME %d, still owed %d (%s)",
                keysPre, keysPost, keysTakenEarly, keysEaten, keysPending.size(), when));
    }

    /* a keyboard changing its layout (Android 14+ says it through the device): a line */
    private void watchKeyboards() {
        android.hardware.input.InputManager im =
                (android.hardware.input.InputManager) getSystemService(INPUT_SERVICE);
        if (im == null) return;
        keyboardWatch = new android.hardware.input.InputManager.InputDeviceListener() {
            @Override public void onInputDeviceAdded(int id) { }
            @Override public void onInputDeviceRemoved(int id) { }
            @Override public void onInputDeviceChanged(int id) {
                android.view.InputDevice dev = android.view.InputDevice.getDevice(id);
                if (dev == null || InputBook.kindOf(dev) != InputBook.KEYBOARD || keysSummaries >= 30) return;
                String lang = deviceString(dev, "getKeyboardLanguageTag");
                String layout = deviceString(dev, "getKeyboardLayoutType");
                keysSummaries++;
                say("keys: input device changed: id " + id + " \"" + dev.getName() + "\" lang " + lang + " layout " + layout);
            }
        };
        im.registerInputDeviceListener(keyboardWatch, null);
    }

    /*
     * A key by its Android code - the fallback for a key without a scan code
     * (a virtual keyboard's, a synthetic one). Only usages the game's table
     * 0x945ad4 knows: the rest (the Windows key, the backquote, volume, back)
     * are the system's, as on a phone they are.
     */
    private static int hidOf(int code) {
        if (code >= KeyEvent.KEYCODE_A && code <= KeyEvent.KEYCODE_Z)
            return 4 + (code - KeyEvent.KEYCODE_A);
        if (code >= KeyEvent.KEYCODE_1 && code <= KeyEvent.KEYCODE_9)
            return 30 + (code - KeyEvent.KEYCODE_1);
        if (code >= KeyEvent.KEYCODE_F1 && code <= KeyEvent.KEYCODE_F12)
            return 58 + (code - KeyEvent.KEYCODE_F1);
        if (code >= KeyEvent.KEYCODE_NUMPAD_1 && code <= KeyEvent.KEYCODE_NUMPAD_9)
            return 89 + (code - KeyEvent.KEYCODE_NUMPAD_1);
        switch (code) {
            case KeyEvent.KEYCODE_0: return 39;
            /*
             * Return as the game's own table reads it: 0x945ad4 turns HID 0x9e
             * (Keyboard Return) into GLFW 257, the key MENU CONFIRM is bound to,
             * and throws 0x28 (Return or Enter) away as -1.
             */
            case KeyEvent.KEYCODE_ENTER: return 0x9e;
            case KeyEvent.KEYCODE_NUMPAD_ENTER: return 88;
            case KeyEvent.KEYCODE_ESCAPE: return 41;
            case KeyEvent.KEYCODE_DEL: return 42;
            case KeyEvent.KEYCODE_TAB: return 43;
            case KeyEvent.KEYCODE_SPACE: return 44;
            case KeyEvent.KEYCODE_MINUS: return 45;
            case KeyEvent.KEYCODE_EQUALS: return 46;
            case KeyEvent.KEYCODE_LEFT_BRACKET: return 47;
            case KeyEvent.KEYCODE_RIGHT_BRACKET: return 48;
            case KeyEvent.KEYCODE_BACKSLASH: return 49;
            case KeyEvent.KEYCODE_SEMICOLON: return 51;
            case KeyEvent.KEYCODE_APOSTROPHE: return 52;
            case KeyEvent.KEYCODE_COMMA: return 54;
            case KeyEvent.KEYCODE_PERIOD: return 55;
            case KeyEvent.KEYCODE_SLASH: return 56;
            case KeyEvent.KEYCODE_CAPS_LOCK: return 57;
            case KeyEvent.KEYCODE_SCROLL_LOCK: return 0x47;
            case KeyEvent.KEYCODE_BREAK: return 0x48;           /* Pause */
            case KeyEvent.KEYCODE_INSERT: return 73;
            case KeyEvent.KEYCODE_MOVE_HOME: return 74;
            case KeyEvent.KEYCODE_PAGE_UP: return 75;
            case KeyEvent.KEYCODE_FORWARD_DEL: return 76;
            case KeyEvent.KEYCODE_MOVE_END: return 77;
            case KeyEvent.KEYCODE_PAGE_DOWN: return 78;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 79;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 80;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 81;
            case KeyEvent.KEYCODE_DPAD_UP: return 82;
            case KeyEvent.KEYCODE_NUM_LOCK: return 0x53;
            case KeyEvent.KEYCODE_NUMPAD_DIVIDE: return 0x54;
            case KeyEvent.KEYCODE_NUMPAD_MULTIPLY: return 0x55;
            case KeyEvent.KEYCODE_NUMPAD_SUBTRACT: return 0x56;
            case KeyEvent.KEYCODE_NUMPAD_ADD: return 0x57;
            case KeyEvent.KEYCODE_NUMPAD_0: return 98;
            case KeyEvent.KEYCODE_NUMPAD_DOT: return 0x63;
            case KeyEvent.KEYCODE_NUMPAD_EQUALS: return 0x67;
            case KeyEvent.KEYCODE_CTRL_LEFT: return 224;
            case KeyEvent.KEYCODE_SHIFT_LEFT: return 225;
            case KeyEvent.KEYCODE_ALT_LEFT: return 226;
            case KeyEvent.KEYCODE_CTRL_RIGHT: return 228;
            case KeyEvent.KEYCODE_SHIFT_RIGHT: return 229;
            case KeyEvent.KEYCODE_ALT_RIGHT: return 230;
            default: return -1;
        }
    }

    /* Android's meta bits in the shape UIKey reports them */
    private static int modsOf(KeyEvent e) {
        int meta = e.getMetaState(), out = 0;
        if ((meta & KeyEvent.META_SHIFT_ON) != 0) out |= 1 << 17;
        if ((meta & KeyEvent.META_CTRL_ON) != 0) out |= 1 << 18;
        if ((meta & KeyEvent.META_ALT_ON) != 0) out |= 1 << 19;
        if ((meta & KeyEvent.META_META_ON) != 0) out |= 1 << 20;
        return out;
    }

    /*
     * A key of a device that is neither a keyboard nor a pad reaching
     * onKeyDown/onKeyUp (a virtual keyboard's key with the seed field not in
     * use, a synthetic one): the game's when it is a key the game reads, by the
     * same place-first rule and with its repeats marked (K4).
     */
    private boolean keyToGame(int code, KeyEvent e, boolean down) {
        int hid = hidFor(e);
        if (hid < 0) return false;
        keyEvent(hid, down, modsOf(e), down ? charOf(e) : 0, down && e.getRepeatCount() > 0);
        return true;
    }

    private boolean triggerKey(int code, KeyEvent e, boolean down) {
        if (!isGamepad(e) || (code != KeyEvent.KEYCODE_BUTTON_L2 && code != KeyEvent.KEYCODE_BUTTON_R2))
            return false;
        PadDev d = padOf(e);
        if (code == KeyEvent.KEYCODE_BUTTON_L2) d.l2Key = down; else d.r2Key = down;
        pushPad(d);
        return true;
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        if (keyTaken(code, e)) return true;             /* the book: fallbacks, bitless pad buttons (U5) */
        if (triggerKey(code, e, true)) return true;
        int bit = isGamepad(e) ? bitOf(code) : -1;
        if (bit < 0) {
            if (!isGamepad(e)) {
                if (keyToGame(code, e, true)) { tellKey(e, true, "the shell"); return true; }
                tellKey(e, true, "the system");
            } else {
                tellKey(e, true, "a pad button with no bit");
            }
            return super.onKeyDown(code, e);
        }
        PadDev d = padOf(e);
        d.buttons |= 1 << bit;
        hatFromButtons(d);
        pushPad(d);
        return true;
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent e) {
        if (keyTaken(code, e)) return true;             /* the book: fallbacks, bitless pad buttons (U5) */
        if (triggerKey(code, e, false)) return true;
        int bit = isGamepad(e) ? bitOf(code) : -1;
        if (bit < 0) {
            if (!isGamepad(e) && keyToGame(code, e, false)) return true;
            return super.onKeyUp(code, e);
        }
        PadDev d = padOf(e);
        d.buttons &= ~(1 << bit);
        hatFromButtons(d);
        pushPad(d);
        return true;
    }

    /*
     * Pads report the d-pad differently: some with buttons, some with a HAT
     * axis. We keep both kinds and bring them to one: the bits are the truth,
     * and the axis is computed from them if no HAT arrived.
     */
    private static void hatFromButtons(PadDev d) {
        if (d.hatByAxis) return;
        d.axes[AX_HAT_X] = ((d.buttons >> BTN_RIGHT) & 1) - ((d.buttons >> BTN_LEFT) & 1);
        d.axes[AX_HAT_Y] = ((d.buttons >> BTN_UP) & 1) - ((d.buttons >> BTN_DOWN) & 1);
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        int action = e.getActionMasked();
        /*
         * A POINTER'S BUTTON (U6): taken from the system. On DeX and ChromeOS
         * an unanswered right button becomes the system's BACK; the click
         * itself arrives in onTouchEvent. Hover and scroll are not the game's
         * either - an iOS game has no cursor - and go on to the system as
         * before.
         */
        if ((action == MotionEvent.ACTION_BUTTON_PRESS || action == MotionEvent.ACTION_BUTTON_RELEASE)
            && ((e.getSource() & android.view.InputDevice.SOURCE_MOUSE) == android.view.InputDevice.SOURCE_MOUSE
                || book.kindOfId(e.getDeviceId()) == InputBook.POINTER)) {
            book.pointerButton(e);
            return true;
        }
        if (!isGamepad(e)) return super.onGenericMotionEvent(e);
        book.padMotion(e);
        if (action == MotionEvent.ACTION_CANCEL) {
            /*
             * The system takes the gesture back (focus change, device reset):
             * Android sends a cancel for a stick that was off centre, and
             * nothing else - dropped, the direction stayed held until that
             * very stick moved again.
             */
            PadDev d = pads.get(e.getDeviceId());
            if (d != null && d.mate != null) {
                d.half.x = d.half.y = 0f;        /* a joined half's stick at rest; its buttons are keys */
                pushJoined(d);
            } else if (d != null && d.layout != null) {
                d.layout.rest();                 /* the axes at rest, the hat's d-pad let go */
                layoutPush(d);
            } else if (d != null) {
                java.util.Arrays.fill(d.axes, 0f);
                java.util.Arrays.fill(d.rawAxes, 0f);
                d.buttons &= ~((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT));
                pushPad(d);
            }
            return true;
        }
        /*
         * A pad's own event is always answered true (U5): one left to the
         * system comes back as its fallback keys - arrows made out of the
         * stick, with a repeat of their own.
         */
        if (action != MotionEvent.ACTION_MOVE) return true;
        PadDev d = padOf(e);
        if (d.mate != null) {
            /* a joined Joy-Con half: its stick (or its hat) into the pair */
            JoyCons.Half h = d.half;
            h.motion(h.hatStick() ? 0f : e.getAxisValue(h.axisX), h.hatStick() ? 0f : e.getAxisValue(h.axisY),
                     e.getAxisValue(MotionEvent.AXIS_HAT_X), e.getAxisValue(MotionEvent.AXIS_HAT_Y));
            pushJoined(d);
            return true;
        }
        standardCheck(d, e);
        if (d.layout != null) {
            /* SDL's joystick: its axes in SDL's order, the hat as the d-pad buttons (PadDb) */
            int[] ax = d.layout.dev.axes;
            for (int i = 0; i < ax.length; i++) d.layoutRaw[i] = e.getAxisValue(ax[i]);
            d.layout.motion(d.layoutRaw, e.getAxisValue(MotionEvent.AXIS_HAT_X), e.getAxisValue(MotionEvent.AXIS_HAT_Y));
            layoutPush(d);
            return true;
        }
        readAxes(d, e);
        float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X);
        float hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if (hx != 0 || hy != 0 || d.hatByAxis) {
            d.hatByAxis = true;
            d.axes[AX_HAT_X] = d.rawAxes[AX_HAT_X] = hx;
            d.axes[AX_HAT_Y] = d.rawAxes[AX_HAT_Y] = -hy;
            d.buttons &= ~((1 << BTN_UP) | (1 << BTN_DOWN) | (1 << BTN_LEFT) | (1 << BTN_RIGHT));
            if (hx > 0.5f) d.buttons |= 1 << BTN_RIGHT;
            if (hx < -0.5f) d.buttons |= 1 << BTN_LEFT;
            if (hy < -0.5f) d.buttons |= 1 << BTN_UP;
            if (hy > 0.5f) d.buttons |= 1 << BTN_DOWN;
        }
        pushPad(d);
        return true;
    }

    /*
     * Every one of these calls into the layer, so all of them are gated on the
     * library actually being there.
     *
     * Without that gate a library that fails to load takes the app down twice:
     * onCreate catches the failure and draws the reason, and then onResume calls
     * soundPause on a library that is not loaded, throws UnsatisfiedLinkError
     * and kills the process before anyone can read the screen. From outside that
     * is exactly "tapped the icon and nothing happened" — measured on the
     * device, not guessed.
     */
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (!layerLoaded) return;
        setSurface(holder.getSurface());
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int fmt, int w, int h) {
        if (!layerLoaded) return;
        setSurface(holder.getSurface());
        if (worker != null) return;                 // the image is brought up once
        // the saves' backup and the settings guard live inside the app (saves.c);
        // named before anything of the port's folder is read
        setBackupDir(getFilesDir().getAbsolutePath());
        // the stick settings, the mods and the exports: the port's folder in
        // Android/media, the one a file manager can reach (portDir)
        java.io.File cfg = portDir();
        // the copy of mods.txt inside the app (usermods.c, plan 02.10 A4): named before the list is read
        UserMods.setInnerDir(getFilesDir());
        // the game reference of the converter at ADD MOD (ModPort, 04.10.2026): built by tools/build.py
        // from the tree the pack is made of; a PC mod is adapted with it
        final android.content.res.AssetManager assets = getAssets();
        UserMods.gameRef = () -> assets.open(UserMods.GAMEREF_ASSET);
        if (cfg != null) setConfigDir(cfg.getAbsolutePath());
        DisplayMetrics dm = getResources().getDisplayMetrics();
        final int pw = dm.widthPixels, ph = dm.heightPixels;
        final float density = dm.density;
        /*
         * The game thread gets its stack size named, and generously.
         *
         * The default belongs to the runtime and differs between phones — 4 MB
         * measured here, and nothing promises that elsewhere. The game does not
         * fit a modest one: a single function of its XML loading (0x618758)
         * moves the stack pointer 160 KB down in one instruction, and the XML
         * work nests.
         *
         * That single move is the danger. On iOS the compiler pairs it with a
         * stack probe that walks down page by page, so a stack that is too
         * small faults at once and plainly. Our layer turns the probe into a
         * no-op — right for the main thread, which Android grows on demand, and
         * wrong here: a thread's stack is a fixed mapping with one guard page,
         * and a 160 KB step clears that guard in a single bound. Past it the
         * writes land in whatever is mapped next and quietly damage it.
         *
         * Sixteen megabytes of address space costs nothing until it is touched.
         */
        worker = new Thread(null, () -> {
            /*
             * The frame thread at the display's priority (21.10.2026, the
             * renderer audit): it ran at the default nice 0, below the system's
             * own frame threads, and since 1.75 it sleeps most of every frame -
             * a late wake-up under load costs a whole slot (objc.c).
             */
            try {
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);
            } catch (Throwable t) {
                if (REPORT) say("frame thread: the priority was not raised - " + t);
            }
            try {
                // the home and the resources are one folder in INTERNAL storage:
                // only there is the app the owner and able to create its folders
                java.io.File home = new java.io.File(getFilesDir(), "app");
                home.mkdirs();              // the game writes saves and settings here
                String app = home.getAbsolutePath();
                /*
                 * The resources are not unpacked: they lie in the APK
                 * uncompressed and the loader reads them straight from there.
                 * Unpacking would copy the same two and a half gigabytes onto
                 * the same disk and give nothing in return. The table has to be
                 * built before the game opens its first file.
                 */
                /*
                 * The saves go not into internal storage but into the port's
                 * folder, Android/media/<app>/saves (P3, the owner, 30.09.2026):
                 * there a file manager can see them and they can be replaced by
                 * hand. The game asks for /Documents and gets this folder; its
                 * files are in Repentance/ inside. The cloud backup is off; the
                 * layer keeps its own backup inside the app (setBackupDir above)
                 * and restores a missing, empty or unreadable file from it
                 * before the game reads (saves.c). EXPORT SAVES and IMPORT SAVES
                 * carry saves between phones and the PC.
                 *
                 * `.nomedia` first, before anything is copied in (P5), then the
                 * move from Android/data - once, file by file.
                 */
                if (REPORT) {
                    say("media: .nomedia " + nomediaState);
                    say("media: " + PortDirs.picturesListed(MainActivity.this) + " images of ours in MediaStore");
                }
                java.io.File saves = PortDirs.saves(MainActivity.this);
                if (saves != null) {
                    moveSavesToMedia(saves);
                    savesRoot = saves;
                    setSavesDir(saves.getAbsolutePath());
                }
                savesInventory(saves);
                stage("surface is there, the thread has started");
                /*
                 * The light report build of 26.10.2026 kept a copy of the pack
                 * in the app's files (1.36 GB); it was dropped the same day
                 * (the owner). A phone that ran it still holds the copy, which
                 * nothing reads any more.
                 */
                for (String gone : new String[] {"resources.pak", "resources.pak.id", "resources.pak.tmp"}) {
                    java.io.File f = new java.io.File(getFilesDir(), gone);
                    if (f.exists() && f.delete() && REPORT) say("light build leftover removed: " + gone);
                }
                setAssets(getAssets(), app);
                stage("resources taken from the APK");
                runOnUiThread(new Runnable() { public void run() { syncPads(); } });   // a pad plugged in beforehand
                byte[] image = read("isaac.arm64");
                stage("image read, " + (image.length >> 20) + " MB");
                probe(image, app, pw, ph, density, app);
                stage("the game finished by itself");
            } catch (Throwable t) {
                android.util.Log.e("isaac", "broke off", t);
            }
        }, "isaac", 16L << 20);
        worker.start();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (!layerLoaded) return;
        setSurface(null);
    }

    private byte[] read(String name) throws Exception {
        try (InputStream in = getAssets().open(name)) {
            ByteArrayOutputStream bo = new ByteArrayOutputStream(1 << 24);
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) bo.write(buf, 0, n);
            return bo.toByteArray();
        }
    }
}
