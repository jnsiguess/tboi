package ru.isaac.port;

import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/*
 * The desk of the device rule (InputBook.judge, the real file) against
 * android.jar: devices described the way Android describes them - sources,
 * keyboard type, every motion range with its own source, hasKeys - run
 * through three rules, key by key:
 *
 *   1.666  MainActivity.isGamepad / padDevice, read back from the 1.666 dex
 *          (dexdump of reports/2026-10-05/hotfix171/tboi-1.666.apk): an event
 *          is a pad's by its own source (GAMEPAD or JOYSTICK), a gamepad
 *          button, or a DPAD source on a non-alphabetic device;
 *   1.71   InputBook.kindOf as it was (InputBook.java.before-padrule, the same
 *          code in the 1.7 and the 1.71 dex): padClass AND (an X axis of the
 *          joystick class with min < 0, or BUTTON_A); a typing keyboard is a
 *          HYBRID only with that X axis, else a KEYBOARD; a hybrid's key is
 *          the pad's only when KeyEvent.isGamepadButton;
 *   now    InputBook.judge + InputBook.hybridKey.
 *
 * A key's event source is Android's KeyboardInputMapper's: KEYBOARD, + DPAD
 * when the device has d-pad keys, + GAMEPAD when it has gamepad keys. The
 * pad's layout and its origin are PadDb.choose on the real assets/paddb.txt.
 *
 * The devices are MODELS (no handheld has been measured here): the shapes
 * Android gives a built-in pad, and the owner's measured "BT5.2 Keyboard"
 * (reports/2026-10-03/isaac-start.txt:1038) as it is.
 *   bash tools/inputdesk/run.sh
 */
final class InputDesk {
    private static int checks, fails;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            fails++;
            System.out.println("  FAIL " + what);
        }
    }

    private static final int KBD = InputDevice.SOURCE_KEYBOARD, DPAD = InputDevice.SOURCE_DPAD,
            GAMEPAD = InputDevice.SOURCE_GAMEPAD, JOY = InputDevice.SOURCE_JOYSTICK, MOUSE = InputDevice.SOURCE_MOUSE,
            TOUCHPAD = InputDevice.SOURCE_TOUCHPAD;
    private static final int ALPHA = InputDevice.KEYBOARD_TYPE_ALPHABETIC, NONALPHA = InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC,
            NOKBD = InputDevice.KEYBOARD_TYPE_NONE;
    private static final int X = MotionEvent.AXIS_X, Y = MotionEvent.AXIS_Y, Z = MotionEvent.AXIS_Z, RZ = MotionEvent.AXIS_RZ,
            RX = MotionEvent.AXIS_RX, RY = MotionEvent.AXIS_RY, HX = MotionEvent.AXIS_HAT_X, HY = MotionEvent.AXIS_HAT_Y,
            LT = MotionEvent.AXIS_LTRIGGER, RT = MotionEvent.AXIS_RTRIGGER, BRAKE = MotionEvent.AXIS_BRAKE,
            GAS = MotionEvent.AXIS_GAS, GEN1 = MotionEvent.AXIS_GENERIC_1;

    /* the keys tried on every device: [keycode, scancode, label] */
    private static final int K_A = KeyEvent.KEYCODE_BUTTON_A, K_B = KeyEvent.KEYCODE_BUTTON_B, K_X = KeyEvent.KEYCODE_BUTTON_X,
            K_Y = KeyEvent.KEYCODE_BUTTON_Y, K_L1 = KeyEvent.KEYCODE_BUTTON_L1, K_R1 = KeyEvent.KEYCODE_BUTTON_R1,
            K_L2 = KeyEvent.KEYCODE_BUTTON_L2, K_R2 = KeyEvent.KEYCODE_BUTTON_R2, K_TL = KeyEvent.KEYCODE_BUTTON_THUMBL,
            K_TR = KeyEvent.KEYCODE_BUTTON_THUMBR, K_START = KeyEvent.KEYCODE_BUTTON_START,
            K_SELECT = KeyEvent.KEYCODE_BUTTON_SELECT, K_BACK = KeyEvent.KEYCODE_BACK, K_MENU = KeyEvent.KEYCODE_MENU,
            K_UP = KeyEvent.KEYCODE_DPAD_UP, K_DOWN = KeyEvent.KEYCODE_DPAD_DOWN, K_LEFT = KeyEvent.KEYCODE_DPAD_LEFT,
            K_RIGHT = KeyEvent.KEYCODE_DPAD_RIGHT, K_Q = KeyEvent.KEYCODE_Q, K_W = KeyEvent.KEYCODE_W,
            K_ENTER = KeyEvent.KEYCODE_ENTER, K_ESC = KeyEvent.KEYCODE_ESCAPE, K_B1 = KeyEvent.KEYCODE_BUTTON_1;

    /* KeyEvent.isGamepadButton (android.jar's is a stub on the desk): BUTTON_A..MODE, BUTTON_1..16 */
    private static boolean isGamepadButton(int k) {
        return (k >= KeyEvent.KEYCODE_BUTTON_A && k <= KeyEvent.KEYCODE_BUTTON_MODE)
            || (k >= KeyEvent.KEYCODE_BUTTON_1 && k <= KeyEvent.KEYCODE_BUTTON_16);
    }

    private static boolean isDpadKey(int k) {
        return k == K_UP || k == K_DOWN || k == K_LEFT || k == K_RIGHT || k == KeyEvent.KEYCODE_DPAD_CENTER;
    }

    private static boolean full(int s, int src) {
        return (s & src) == src;
    }

    /* a model device */
    static final class M {
        final String title, name;
        final int vendor, product, sources, kbd;
        final int[] axis, axisSrc;
        final float[] min, max;
        final int[] keys;                  /* what hasKeys answers true for */
        final boolean external;

        M(String title, String name, int vendor, int product, int sources, int kbd, boolean external,
          float[][] ranges, int... keys) {
            this.title = title;
            this.name = name;
            this.vendor = vendor;
            this.product = product;
            this.sources = sources;
            this.kbd = kbd;
            this.external = external;
            int n = ranges.length;
            axis = new int[n];
            axisSrc = new int[n];
            min = new float[n];
            max = new float[n];
            for (int i = 0; i < n; i++) {
                axis[i] = (int) ranges[i][0];
                axisSrc[i] = (int) ranges[i][1];
                min[i] = ranges[i][2];
                max[i] = ranges[i][3];
            }
            this.keys = keys;
        }

        boolean has(int k) {
            for (int x : keys) if (x == k) return true;
            return false;
        }

        InputBook.Facts facts() {
            boolean[] named = new boolean[InputBook.PAD_BUTTONS.length];
            for (int i = 0; i < named.length; i++) named[i] = has(InputBook.PAD_BUTTONS[i]);
            int numbered = 0;
            for (int i = 0; i < 16; i++) if (has(KeyEvent.KEYCODE_BUTTON_1 + i)) numbered++;
            return new InputBook.Facts(name, vendor, product, sources, kbd, false, axis, axisSrc, min, max, named, numbered);
        }

        PadDb.Device sdl() {
            boolean[] h = new boolean[PadDb.SDL_KEYS.length];
            for (int i = 0; i < h.length; i++) h[i] = has(PadDb.SDL_KEYS[i]);
            return new PadDb.Device(name, name, vendor, product, 33, axis, axisSrc, min, max, new float[axis.length], h);
        }

        /* KeyboardInputMapper's source for this device's keys */
        int keySource() {
            int s = KBD;
            for (int k : keys) if (isDpadKey(k)) s |= DPAD;
            for (int k : keys) if (isGamepadButton(k)) s |= GAMEPAD;
            return s;
        }
    }

    private static float[] r(int axis, int src, float min, float max) {
        return new float[] { axis, src, min, max };
    }

    private static float[][] ranges(float[]... rs) {
        return rs;
    }

    /* ---------- 1.666 (dex): isGamepad(event) ---------- */
    private static boolean pad1666(M d, int keycode) {
        int src = d.keySource();
        if (full(src, GAMEPAD) || full(src, JOY)) return true;
        if (isGamepadButton(keycode)) return true;
        return full(src, DPAD) && d.kbd != ALPHA;
    }

    /* ---------- 1.71 (InputBook.java.before-padrule) ---------- */
    private static final int NONE = 0, POINTER = 3, PAD = 4, KEYBOARD = 5, SYSTEM = 6, HYBRID = 7;

    private static boolean stick171(M d) {
        for (int i = 0; i < d.axis.length; i++)
            if (d.axis[i] == X && (d.axisSrc[i] & InputDevice.SOURCE_CLASS_JOYSTICK) != 0 && d.min[i] < 0f) return true;
        return false;
    }

    private static int kind171(M d) {
        int src = d.sources;
        boolean padClass = full(src, GAMEPAD) || full(src, JOY);
        boolean pad = padClass && (stick171(d) || d.has(K_A));
        boolean pointer = full(src, MOUSE) || full(src, TOUCHPAD);
        if (pointer && !pad) return POINTER;
        boolean alpha = d.kbd == ALPHA;
        boolean stick = stick171(d);
        if (pad && !alpha) return PAD;
        if (pad && alpha && stick) return HYBRID;
        if (alpha) return KEYBOARD;
        return SYSTEM;
    }

    private static int key171(M d, int keycode) {
        int kind = kind171(d);
        if (kind == HYBRID) return isGamepadButton(keycode) ? PAD : KEYBOARD;
        if ((kind == POINTER || kind == SYSTEM) && d.kbd == ALPHA) return KEYBOARD;
        return kind;
    }

    /* ---------- now ---------- */
    private static int keyNow(M d, int keycode, int scancode) {
        InputBook.Verdict v = InputBook.judge(d.facts());
        if (v.kind == InputBook.HYBRID) return InputBook.hybridKey(v.padKeys, keycode, scancode);
        if ((v.kind == InputBook.POINTER || v.kind == InputBook.SYSTEM) && d.kbd == ALPHA) return KEYBOARD;
        return v.kind;
    }

    private static String where(int kind) {
        switch (kind) {
            case PAD: return "pad";
            case KEYBOARD: return "KBD";
            case SYSTEM: return "sys";
            case POINTER: return "ptr";
            default: return InputBook.name(kind);
        }
    }

    private static String keyName(int k) {
        String s = KeyEvent.class.getSimpleName();
        switch (k) {
            case K_A: return "A"; case K_B: return "B"; case K_X: return "X"; case K_Y: return "Y";
            case K_L1: return "L1"; case K_R1: return "R1"; case K_L2: return "L2"; case K_R2: return "R2";
            case K_TL: return "L3"; case K_TR: return "R3"; case K_START: return "START"; case K_SELECT: return "SELECT";
            case K_BACK: return "BACK"; case K_MENU: return "MENU"; case K_UP: return "UP"; case K_DOWN: return "DOWN";
            case K_LEFT: return "LEFT"; case K_RIGHT: return "RIGHT"; case K_Q: return "Q"; case K_W: return "W";
            case K_ENTER: return "ENTER"; case K_ESC: return "ESC";
            default: return k >= K_B1 && k < K_B1 + 16 ? "BTN" + (k - K_B1 + 1) : s + k;
        }
    }

    /*
     * One device through the three rules. want: what every PAD-meant key must
     * do now (true: reach the pad), and the kind the device must get.
     */
    private static void run(M d, int wantKind, boolean padKeysToPad, int[] typing) {
        InputBook.Facts f = d.facts();
        InputBook.Verdict v = InputBook.judge(f);
        System.out.println();
        System.out.println("== " + d.title);
        System.out.println(String.format(java.util.Locale.ROOT, "   \"%s\" %04x:%04x sources 0x%08x [%s] kbd %s ext %s",
                d.name, d.vendor, d.product, d.sources, InputBook.sourceNames(d.sources),
                d.kbd == ALPHA ? "alpha" : d.kbd == NONALPHA ? "non-alpha" : "none", d.external ? "yes" : "no"));
        System.out.println("   1.71: " + InputBook.name(kind171(d)).toUpperCase() + "   now: "
                + InputBook.name(v.kind).toUpperCase() + (v.kind == InputBook.HYBRID ? (v.padKeys ? " (pad keys to the pad)" : " (keys to the keyboard)") : "")
                + "  - " + v.why);
        if (v.kind == InputBook.PAD || v.kind == InputBook.HYBRID) {
            PadDb.Pick p = PadDb.shared().choose(d.sdl());
            System.out.println("   layout: " + p.line);
        }
        StringBuilder a = new StringBuilder("   key      1.666  1.71   now\n");
        for (int k : d.keys) {
            int scan = 0;
            if (d.name.startsWith("Joy-Con (L)") && isDpadKey(k)) scan = 0x220 + (k - K_UP);
            a.append(String.format(java.util.Locale.ROOT, "   %-8s %-6s %-6s %s\n", keyName(k),
                    pad1666(d, k) ? "pad" : "other", where(key171(d, k)), where(keyNow(d, k, scan))));
            if (padKeysToPad) check(keyNow(d, k, scan) == PAD, d.title + ": " + keyName(k) + " must reach the pad, goes "
                    + where(keyNow(d, k, scan)));
        }
        for (int k : typing) {
            a.append(String.format(java.util.Locale.ROOT, "   %-8s %-6s %-6s %s   (typing key)\n", keyName(k),
                    pad1666(d, k) ? "pad" : "other", where(key171(d, k)), where(keyNow(d, k, 0))));
            check(keyNow(d, k, 0) == KEYBOARD, d.title + ": typing key " + keyName(k) + " must stay the keyboard's");
        }
        System.out.print(a);
        check(v.kind == wantKind, d.title + ": kind " + InputBook.name(v.kind) + ", want " + InputBook.name(wantKind));
    }

    private static final int[] FACE = { K_A, K_B, K_X, K_Y, K_L1, K_R1, K_TL, K_TR, K_START, K_SELECT };
    private static final int[] DPADK = { K_UP, K_DOWN, K_LEFT, K_RIGHT };

    private static int[] cat(int[]... parts) {
        int n = 0;
        for (int[] p : parts) n += p.length;
        int[] out = new int[n];
        int at = 0;
        for (int[] p : parts) for (int x : p) out[at++] = x;
        return out;
    }

    private static final int[] NOTHING = {};

    /* ---------- two Joy-Cons, one pad (JoyCons, the real file) ---------- */

    private static final float[] JO = new float[8];

    private static void eqf(float have, float want, String what) {
        check(Math.abs(have - want) < 0.01f, what + " = " + have + ", want " + want);
    }

    private static boolean bit(int bits, int b) {
        return (bits & (1 << b)) != 0;
    }

    /* press one key on a half, read the pair, let it go; the pair's bits */
    private static int press(JoyCons.Half h, JoyCons.Half l, JoyCons.Half r, int keycode, int scancode) {
        h.key(keycode, scancode, true);
        int bits = JoyCons.merge(l, r, JO);
        h.key(keycode, scancode, false);
        return bits;
    }

    private static void joyCons() {
        System.out.println();
        System.out.println("== Joy-Cons: the halves and the pair");
        check(JoyCons.side(0x057e, 0x2006, "x") == JoyCons.LEFT, "057e:2006 is the left half");
        check(JoyCons.side(0x057e, 0x2007, "x") == JoyCons.RIGHT, "057e:2007 is the right half");
        check(JoyCons.side(0, 0, "Joy-Con (L)") == JoyCons.LEFT, "by name, left");
        check(JoyCons.side(0, 0, "Nintendo Switch Right Joy-Con (R)") == JoyCons.RIGHT, "by name, right");
        check(JoyCons.side(0x057e, 0x2009, "Pro Controller") == JoyCons.NONE, "a Switch Pro is no half");

        float[] m1 = { -1f, -1f }, p1 = { 1f, 1f }, f0 = { 0f, 0f };
        /* hid-nintendo: the left stick on X/Y, the right on RX/RY (+-32767, flat 500) */
        float[] mm = { -32767f, -32767f }, pp = { 32767f, 32767f }, fl = { 500f, 500f };
        JoyCons.Half l = new JoyCons.Half(JoyCons.LEFT, new int[] { X, Y }, mm, pp, fl);
        JoyCons.Half r = new JoyCons.Half(JoyCons.RIGHT, new int[] { RX, RY }, mm, pp, fl);
        System.out.println("   hid-nintendo: L stick " + l.stickName() + ", R stick " + r.stickName());
        check(!l.hatStick() && !r.hatStick(), "hid-nintendo halves have sticks");
        int none = 0;     /* the left d-pad has no Android key code: SDL3 reads it by scan code */
        check(bit(press(l, l, r, none, 0x220), PadDb.BTN_UP), "L BTN_DPAD_UP -> up");
        check(bit(press(l, l, r, none, 0x223), PadDb.BTN_RIGHT), "L BTN_DPAD_RIGHT -> right");
        check(bit(press(l, l, r, K_L1, 0x136), PadDb.BTN_L1), "L BTN_TL -> L1");
        check(bit(press(l, l, r, K_SELECT, 0x13a), PadDb.BTN_OPTIONS), "L minus -> Options (Back)");
        check(bit(press(l, l, r, K_TL, 0x13d), PadDb.BTN_L3), "L stick press -> L3");
        l.key(K_L2, 0x138, true);
        JoyCons.merge(l, r, JO);
        eqf(JO[PadDb.AX_L2], 1f, "L ZL -> L2");
        l.key(K_L2, 0x138, false);
        check(press(l, l, r, K_R1, 0x137) == 0, "L SL/SR filed as BTN_TR: no R1 in the pair");
        check(bit(press(r, l, r, K_A, 0x130), PadDb.BTN_A), "R BTN_SOUTH (B, bottom) -> our A");
        check(bit(press(r, l, r, K_B, 0x131), PadDb.BTN_B), "R BTN_EAST (A, right) -> our B");
        check(bit(press(r, l, r, K_Y, 0x134), PadDb.BTN_X), "R BTN_WEST (Y, left) -> our X");
        check(bit(press(r, l, r, K_X, 0x133), PadDb.BTN_Y), "R BTN_NORTH (X, top) -> our Y");
        check(bit(press(r, l, r, K_R1, 0x137), PadDb.BTN_R1), "R BTN_TR -> R1");
        check(bit(press(r, l, r, K_START, 0x13b), PadDb.BTN_MENU), "R plus -> Menu (Start)");
        check(bit(press(r, l, r, K_TR, 0x13e), PadDb.BTN_R3), "R stick press -> R3");
        check(press(r, l, r, KeyEvent.KEYCODE_BUTTON_MODE, 0x13c) == 0, "home: no place in our model");
        check(press(r, l, r, K_L1, 0x136) == 0, "R SL/SR filed as BTN_TL: no L1 in the pair");
        r.key(K_R2, 0x139, true);
        JoyCons.merge(l, r, JO);
        eqf(JO[PadDb.AX_R2], 1f, "R ZR -> R2");
        r.key(K_R2, 0x139, false);
        /* by key code alone (a driver without the standard scan codes) */
        check(bit(press(l, l, r, K_L1, 0), PadDb.BTN_L1), "L by key code: L1");
        check(bit(press(l, l, r, K_UP, 0), PadDb.BTN_UP), "L by key code: up");
        check(bit(press(r, l, r, K_A, 0), PadDb.BTN_A), "R by key code: BUTTON_A (south) -> our A");
        /* the sticks: left from L, right from R, up positive, the flat cut */
        l.motion(23170f, -23170f, 0f, 0f);
        r.motion(-32767f, 0f, 0f, 0f);
        JoyCons.merge(l, r, JO);
        check(JO[PadDb.AX_LEFT_X] > 0.69f && JO[PadDb.AX_LEFT_Y] > 0.69f, "L stick right+up: left X "
                + JO[PadDb.AX_LEFT_X] + " Y " + JO[PadDb.AX_LEFT_Y] + " (Android up is -)");
        eqf(JO[PadDb.AX_RIGHT_X], -1f, "R stick left: right X");
        eqf(JO[PadDb.AX_RIGHT_Y], 0f, "R stick: right Y");
        l.motion(300f, -200f, 0f, 0f);
        JoyCons.merge(l, r, JO);
        eqf(JO[PadDb.AX_LEFT_X], 0f, "L stick inside its flat: nought");
        l.release();
        r.release();
        check(JoyCons.merge(l, r, JO) == 0 && JO[PadDb.AX_RIGHT_X] == 0f, "released: nothing held");

        /* no hid-nintendo: the simple HID report - BUTTON_1..16, the stick a hat (sideways) */
        JoyCons.Half hl = new JoyCons.Half(JoyCons.LEFT, new int[] { HX, HY }, m1, p1, f0);
        JoyCons.Half hr = new JoyCons.Half(JoyCons.RIGHT, new int[] { HX, HY }, m1, p1, f0);
        System.out.println("   simple HID: L stick " + hl.stickName() + ", R stick " + hr.stickName());
        check(hl.hatStick() && hr.hatStick(), "simple-report halves: hat sticks");
        int b1 = K_B1;
        check(bit(press(hl, hl, hr, b1 + 3, 0x123), PadDb.BTN_UP), "L button 4 (Up) -> up");
        check(bit(press(hl, hl, hr, b1, 0x120), PadDb.BTN_DOWN), "L button 1 (Down) -> down");
        check(bit(press(hl, hl, hr, b1 + 8, 0x128), PadDb.BTN_OPTIONS), "L button 9 (minus) -> Options");
        check(bit(press(hl, hl, hr, b1 + 14, 0x12e), PadDb.BTN_L1), "L button 15 (L) -> L1");
        check(press(hl, hl, hr, b1 + 4, 0x124) == 0, "L button 5 (SL): no place");
        check(bit(press(hr, hl, hr, b1, 0x120), PadDb.BTN_B), "R button 1 (A, right) -> our B");
        check(bit(press(hr, hl, hr, b1 + 2, 0x122), PadDb.BTN_A), "R button 3 (B, bottom) -> our A");
        check(bit(press(hr, hl, hr, b1 + 1, 0x121), PadDb.BTN_Y), "R button 2 (X, top) -> our Y");
        check(bit(press(hr, hl, hr, b1 + 9, 0x129), PadDb.BTN_MENU), "R button 10 (plus) -> Menu");
        hr.key(b1 + 15, 0x12f, true);
        JoyCons.merge(hl, hr, JO);
        eqf(JO[PadDb.AX_R2], 1f, "R button 16 (ZR) -> R2");
        hr.key(b1 + 15, 0x12f, false);
        /* the same report through a "gamepad" HID application: BTN_GAMEPAD+i, Android BUTTON_A.. */
        check(bit(press(hl, hl, hr, K_X, 0x133), PadDb.BTN_UP), "L gamepad-app button 4 (BTN_X scan) -> up");
        check(bit(press(hl, hl, hr, K_X, 0), PadDb.BTN_UP), "L gamepad-app button 4 (BUTTON_X code) -> up");
        /* the hat, sideways: the left half's "up" (towards its rail) is upright right */
        hl.motion(0f, 0f, 0f, -1f);
        hr.motion(0f, 0f, 0f, -1f);
        JoyCons.merge(hl, hr, JO);
        eqf(JO[PadDb.AX_LEFT_X], 1f, "L hat sideways up -> left stick right");
        eqf(JO[PadDb.AX_LEFT_Y], 0f, "L hat sideways up -> left Y nought");
        eqf(JO[PadDb.AX_RIGHT_X], -1f, "R hat sideways up -> right stick left");
        hl.motion(0f, 0f, 1f, 0f);
        JoyCons.merge(hl, hr, JO);
        eqf(JO[PadDb.AX_LEFT_Y], -1f, "L hat sideways right -> left stick down");
        hl.motion(0f, 0f, 1f, -1f);
        JoyCons.merge(hl, hr, JO);
        eqf((float) Math.hypot(JO[PadDb.AX_LEFT_X], JO[PadDb.AX_LEFT_Y]), 1f, "L hat diagonal: length 1");
    }

    public static void main(String[] args) throws Exception {
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(args[0]), java.nio.charset.StandardCharsets.UTF_8))) {
            PadDb.install(PadDb.parse(in, 33));
        }
        System.out.println("paddb.txt: " + PadDb.shared().size() + " entries on API 33");

        float[] jx = r(X, JOY, -1, 1), jy = r(Y, JOY, -1, 1), jz = r(Z, JOY, -1, 1), jrz = r(RZ, JOY, -1, 1),
                jrx = r(RX, JOY, -1, 1), jry = r(RY, JOY, -1, 1), jhx = r(HX, JOY, -1, 1), jhy = r(HY, JOY, -1, 1),
                jlt = r(LT, JOY, 0, 1), jrt = r(RT, JOY, 0, 1), jbr = r(BRAKE, JOY, 0, 1), jgas = r(GAS, JOY, 0, 1);
        int padSrc = KBD | DPAD | GAMEPAD | JOY;

        /* 1. a handheld's built-in pad that Android calls a typing keyboard (its driver declares a Q key):
         *    sticks X/Y + Z/RZ, analog triggers, the d-pad as keys, Select as BACK */
        run(new M("handheld, built-in pad, ALPHABETIC, sticks, d-pad keys", "Handheld Gamepad", 0x2020, 0x3001,
                padSrc, ALPHA, false, ranges(jx, jy, jz, jrz, jbr, jgas),
                cat(FACE, DPADK, new int[] { K_BACK, K_L2, K_R2 })), InputBook.HYBRID, true, new int[] { K_Q, K_ENTER });

        /* 2. the same but non-alphabetic (an internal USB pad: G Cloud, Razer Edge): must not change */
        run(new M("handheld, built-in pad, non-alphabetic, hat d-pad", "Handheld XInput", 0x046d, 0xc1a0,
                KBD | GAMEPAD | JOY, NONALPHA, false, ranges(jx, jy, jz, jrz, jlt, jrt, jbr, jgas, jhx, jhy),
                FACE), InputBook.PAD, true, NOTHING);

        /* 3. a handheld without sticks (a d-pad handheld), alphabetic */
        run(new M("handheld without sticks, ALPHABETIC", "Handheld Buttons", 0x0001, 0x0001,
                KBD | DPAD | GAMEPAD, ALPHA, false, ranges(),
                cat(new int[] { K_A, K_B, K_X, K_Y, K_L1, K_R1, K_L2, K_R2, K_START, K_SELECT }, DPADK)),
                InputBook.HYBRID, true, NOTHING);

        /* 4. a handheld whose built-in controls are two devices: the buttons (alphabetic, no axes)... */
        run(new M("split handheld: the buttons device, ALPHABETIC", "gpio-keys-gamepad", 0, 0,
                KBD | DPAD | GAMEPAD, ALPHA, false, ranges(),
                cat(FACE, DPADK, new int[] { K_L2, K_R2 })), InputBook.HYBRID, true, NOTHING);
        /*    ...and the sticks (no keys at all) */
        run(new M("split handheld: the sticks device", "adc-joystick", 0, 0,
                JOY, NOKBD, false, ranges(jx, jy, jrx, jry), NOTHING), InputBook.PAD, true, NOTHING);

        /* 5. the owner's BT keyboard, as measured (04e8:7021, 03.10.2026; no touchpad - the owner): the
         *    joystick class has GENERIC_1 only, X/Y are pointer axes under MOUSE (0..3119 x 0..1439, his
         *    screen); SDL mask 00107851 - bit 0 is
         *    BUTTON_A or DPAD_CENTER (taken at its worst: A), bit 20 BUTTON_1 */
        run(new M("owner's BT5.2 Keyboard (measured)", "BT5.2 Keyboard", 0x04e8, 0x7021,
                0x01002313, ALPHA, true, ranges(r(X, MOUSE, 0, 3119), r(Y, MOUSE, 0, 1439), r(GEN1, JOY, -1, 1)),
                K_A, K_B1, K_BACK, K_MENU, K_UP, K_DOWN, K_LEFT, K_RIGHT), InputBook.POINTER, false,
                new int[] { K_Q, K_W, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ESC, K_ENTER, K_BACK, K_MENU });

        /* 6. a plain BT keyboard */
        run(new M("plain keyboard", "Keyboard K380", 0x046d, 0xb342, KBD | DPAD, ALPHA, true, ranges(),
                K_UP, K_DOWN), InputBook.KEYBOARD, false, new int[] { K_Q, K_UP, K_ESC });

        /* 7. a gaming keyboard with a joystick interface of its own (X/Y, no gamepad buttons) merged in:
         *    its arrows must stay the keyboard's (the game's keyboard shoots with them) */
        run(new M("keyboard with a joystick X/Y, no gamepad buttons", "Gaming Keyboard", 0x1532, 0x0203,
                KBD | DPAD | JOY, ALPHA, true, ranges(jx, jy), K_UP, K_DOWN), InputBook.HYBRID, false,
                new int[] { K_Q, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ESC });

        /* 8. the owner's 8BitDo Ultimate (2dc8:3012) */
        run(new M("owner's 8BitDo Ultimate", "8BitDo Ultimate", 0x2dc8, 0x3012, KBD | GAMEPAD | JOY, NONALPHA, true,
                ranges(jx, jy, jz, jrz, jbr, jgas, jhx, jhy), cat(FACE, new int[] { K_L2, K_R2 })),
                InputBook.PAD, true, NOTHING);

        /* 9. Joy-Cons with hid-nintendo: two devices; L has the stick X/Y, L/ZL/minus/L3 and the d-pad by
         *    scan codes BTN_DPAD_*, R has A/B/X/Y, R/ZR/plus/R3 and its stick on RX/RY */
        run(new M("Joy-Con (L), hid-nintendo", "Joy-Con (L)", 0x057e, 0x2006, padSrc, NONALPHA, true,
                ranges(jx, jy), cat(new int[] { K_L1, K_L2, K_TL, K_SELECT }, DPADK)), InputBook.PAD, true, NOTHING);
        run(new M("Joy-Con (R), hid-nintendo", "Joy-Con (R)", 0x057e, 0x2007, KBD | GAMEPAD | JOY, NONALPHA, true,
                ranges(jrx, jry), K_A, K_B, K_X, K_Y, K_R1, K_R2, K_TR, K_START), InputBook.PAD, true, NOTHING);

        /* 10. a Joy-Con with no hid-nintendo (generic HID): numbered buttons and the stick as a hat */
        run(new M("Joy-Con (L), generic HID", "Joy-Con (L) HID", 0x057e, 0x2006, KBD | GAMEPAD | JOY, NONALPHA, true,
                ranges(jhx, jhy), K_B1, K_B1 + 1, K_B1 + 2, K_B1 + 3, K_B1 + 4, K_B1 + 8, K_B1 + 10),
                InputBook.PAD, true, NOTHING);

        /* 11. a TV remote: a d-pad and no pad organs - the system's (unchanged, batch 2) */
        run(new M("TV remote", "IR remote", 0, 0, KBD | DPAD, NONALPHA, false, ranges(), K_UP, K_DOWN, K_BACK),
                InputBook.SYSTEM, false, NOTHING);

        /* 12. a mouse */
        run(new M("mouse", "Mouse", 0x046d, 0xc077, MOUSE, NOKBD, true, ranges(r(X, MOUSE, 0, 2400), r(Y, MOUSE, 0, 1080)),
                NOTHING), InputBook.POINTER, false, NOTHING);

        /* 13. a pad SDL knows, presenting no keys (an Xbox 360 pad whose hasKeys answers nothing) */
        run(new M("Xbox 360 pad, hasKeys silent", "Xbox 360 Controller", 0x045e, 0x028e, GAMEPAD | JOY, NOKBD, true,
                ranges(jx, jy, jz, jrz, jlt, jrt, jhx, jhy), NOTHING), InputBook.PAD, true, NOTHING);

        /*
         * 14-17. TYPING KEYBOARDS WITH GENERIC HID BUTTONS (1.74, the research notes of 11.10.2026, keyboard).
         * The owner's BT5.2 (model 5) already shows the shape: a joystick source, hasKeys
         * answering BUTTON_1 (and A or DPAD_CENTER) - HID "Button" usages outside a mouse
         * collection, which the kernel files as BTN_MISC / BTN_JOYSTICK (256.., 288..) and
         * Generic.kl names BUTTON_1..16; beside an absolute axis Android takes them for a
         * JOYSTICK (EventHub: gamepad-range buttons + a joystick axis) - the owner's joystick
         * source. A keyboard of the same kind with four such buttons passed the 1.72 rule
         * as a pad: a HYBRID with a pad slot, its arrows the phantom pad's d-pad. Under
         * 1.71 it was a keyboard. Every key must stay the keyboard's.
         */
        int[] typingKeys = { K_Q, K_W, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ESC, K_ENTER, K_BACK, K_MENU };
        run(new M("BT keyboard like the owner's, 4 generic HID buttons", "BT5.0 Keyboard", 0x04e8, 0x7021,
                0x01002313, ALPHA, true, ranges(r(X, MOUSE, 0, 2400), r(Y, MOUSE, 0, 1080), r(GEN1, JOY, -1, 1)),
                K_B1, K_B1 + 1, K_B1 + 2, K_B1 + 3, K_BACK, K_MENU, K_UP, K_DOWN, K_LEFT, K_RIGHT), InputBook.POINTER, false,
                typingKeys);
        run(new M("BT keyboard, 8 generic HID buttons, no pointer", "Bluetooth Keyboard", 0x05ac, 0x022c,
                KBD | DPAD | JOY, ALPHA, true, ranges(r(GEN1, JOY, -1, 1)),
                K_B1, K_B1 + 1, K_B1 + 2, K_B1 + 3, K_B1 + 4, K_B1 + 5, K_B1 + 6, K_B1 + 7, K_UP, K_DOWN, K_LEFT, K_RIGHT),
                InputBook.KEYBOARD, false, typingKeys);
        run(new M("BT keyboard, BUTTON_A and 4 generic HID buttons, joystick GEN1", "Wireless Keyboard", 0x248a, 0x8266,
                KBD | DPAD | GAMEPAD | JOY, ALPHA, true, ranges(r(GEN1, JOY, -1, 1)),
                K_A, K_B1, K_B1 + 1, K_B1 + 2, K_B1 + 3, K_UP, K_DOWN, K_LEFT, K_RIGHT),
                InputBook.KEYBOARD, false, typingKeys);
        /* the same buttons on a device that is NOT a typing keyboard stay a pad: a generic HID pad */
        run(new M("generic HID pad, numbered buttons, non-alphabetic", "USB Gamepad", 0x0079, 0x0011,
                KBD | GAMEPAD | JOY, NONALPHA, true, ranges(r(GEN1, JOY, -1, 1)),
                K_B1, K_B1 + 1, K_B1 + 2, K_B1 + 3, K_B1 + 4, K_B1 + 5), InputBook.PAD, true, NOTHING);

        joyCons();

        System.out.println();
        System.out.println(checks + " checks, " + fails + " failed");
        if (fails > 0) System.exit(1);
    }
}
