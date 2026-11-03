package ru.isaac.port;

import android.view.KeyEvent;
import android.view.MotionEvent;

/*
 * TWO JOY-CONS, ONE PAD (the owner's choice, 06.10.2026: "option A").
 *
 * A left and a right Joy-Con that are both connected are joined into ONE
 * pad - one slot, one GCController for the game - the way SDL joins them
 * by default and the way iOS presents a pair the player has joined. A half
 * alone stays a pad of its own, as before; when one half of a pair goes,
 * the other keeps the slot alone.
 *
 * Android gives the halves to us as two devices, and how depends on the
 * phone's kernel:
 *   - hid-nintendo (Linux 5.16+): each half its own evdev device with the
 *     standard codes - left: BTN_DPAD_UP/DOWN/LEFT/RIGHT (0x220..0x223, no
 *     Android key code: SDL3's scancode_to_SDL reads them by scan code),
 *     BTN_TL (L), BTN_TL2 (ZL), BTN_SELECT (-), BTN_THUMBL, BTN_Z (capture),
 *     the stick on ABS_X/ABS_Y; right: BTN_SOUTH/EAST/NORTH/WEST (B/A/X/Y by
 *     position), BTN_TR (R), BTN_TR2 (ZR), BTN_START (+), BTN_THUMBR, BTN_MODE
 *     (home), the stick on ABS_RX/ABS_RY. SL/SR come as the other side's
 *     shoulder codes.
 *   - no hid-nintendo: the Joy-Con's own simple HID report (the
 *     "Nintendo_Switch_Reverse_Engineering" notes, report 0x3F): buttons 1..16
 *     (BTN_JOYSTICK.., Android BUTTON_1..16) - byte 1 by side (left: Down,
 *     Right, Left, Up, SL, SR; right: A, X, B, Y, SL, SR), byte 2 shared (-,
 *     +, left stick, right stick, home, capture, L/R, ZL/ZR) - and the stick
 *     as an 8-way hat, given for the half held SIDEWAYS.
 * Which of the two a half is, is told by its stick: hid-nintendo gives a
 * centred pair (X/Y, RX/RY or Z/RZ), the simple report only a hat. A key is
 * read by its SCAN CODE first (the kernel's code, whatever key layout file
 * the phone has), then by its key code. A simple-report button lands on
 * BTN_JOYSTICK+i (a "joystick" HID application, Android BUTTON_1..16) or on
 * BTN_GAMEPAD+i (a "gamepad" one, Android BUTTON_A, B, C, X, Y, Z, L1...):
 * for a hat half both are read as the report's button number.
 *
 * The pair, as SDL's combined Joy-Cons (positional, like every pad of ours):
 * left stick from the left half, right stick from the right; the d-pad from
 * the left half's four buttons; L/ZL and the left stick press from the left,
 * R/ZR and the right stick press from the right; - is Back (Options), + is
 * Start (Menu); the face buttons by position (B bottom = our A, A right =
 * our B, Y left = our X, X top = our Y). Home, capture and SL/SR have no
 * place in our pad model (SDL: guide, misc1, paddles) and are not used.
 * Each half contributes only its own side, so an SL/SR that a kernel files
 * as the other side's shoulder does not leak into the pair.
 *
 * Pure: the desk (tools/inputdesk) feeds it what Android would.
 */
final class JoyCons {
    static final int NONE = 0, LEFT = 1, RIGHT = 2;

    /* a half's organs, as bits */
    static final int UP = 1, DOWN = 1 << 1, DLEFT = 1 << 2, DRIGHT = 1 << 3, SHOULDER = 1 << 4, ZTRIG = 1 << 5,
            MINUS = 1 << 6, PLUS = 1 << 7, STICK = 1 << 8, HOME = 1 << 9, CAPTURE = 1 << 10, SL = 1 << 11,
            SR = 1 << 12, FACE_S = 1 << 13, FACE_E = 1 << 14, FACE_W = 1 << 15, FACE_N = 1 << 16;

    private static final int VENDOR = 0x057e, PRODUCT_L = 0x2006, PRODUCT_R = 0x2007;

    /* which half a device is: by vendor:product, else by its name (a kernel that hides the ids) */
    static int side(int vendor, int product, String name) {
        if ((vendor & 0xffff) == VENDOR && (product & 0xffff) == PRODUCT_L) return LEFT;
        if ((vendor & 0xffff) == VENDOR && (product & 0xffff) == PRODUCT_R) return RIGHT;
        String n = name == null ? "" : name.toLowerCase(java.util.Locale.ROOT);
        if (n.contains("joy-con (l)") || n.contains("joycon (l)") || n.contains("joy-con l")) return LEFT;
        if (n.contains("joy-con (r)") || n.contains("joycon (r)") || n.contains("joy-con r")) return RIGHT;
        return NONE;
    }

    static String sideName(int side) {
        return side == LEFT ? "L" : side == RIGHT ? "R" : "-";
    }

    /* the simple HID report's buttons 1..16 (bit i of the report = BUTTON_(i+1)) */
    private static final int[] HID_LEFT = { DOWN, DRIGHT, DLEFT, UP, SL, SR, 0, 0,
            MINUS, PLUS, STICK, 0, HOME, CAPTURE, SHOULDER, ZTRIG };
    private static final int[] HID_RIGHT = { FACE_E, FACE_N, FACE_S, FACE_W, SL, SR, 0, 0,
            MINUS, PLUS, 0, STICK, HOME, CAPTURE, SHOULDER, ZTRIG };

    /* hid-nintendo's codes (linux/input-event-codes.h) */
    private static final int BTN_JOYSTICK = 0x120, BTN_SOUTH = 0x130, BTN_EAST = 0x131, BTN_NORTH = 0x133,
            BTN_WEST = 0x134, BTN_Z = 0x135, BTN_TL = 0x136, BTN_TR = 0x137, BTN_TL2 = 0x138, BTN_TR2 = 0x139,
            BTN_SELECT = 0x13a, BTN_START = 0x13b, BTN_MODE = 0x13c, BTN_THUMBL = 0x13d, BTN_THUMBR = 0x13e,
            BTN_DPAD_UP = 0x220, BTN_DPAD_DOWN = 0x221, BTN_DPAD_LEFT = 0x222, BTN_DPAD_RIGHT = 0x223;

    /* BTN_GAMEPAD+i as Android's Generic.kl names them: the simple report's button i+1 */
    private static final int[] GAMEPAD_KEYS = {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_C, KeyEvent.KEYCODE_BUTTON_X,
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_Z, KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1,
        KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_BUTTON_SELECT,
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_BUTTON_THUMBL,
        KeyEvent.KEYCODE_BUTTON_THUMBR,
    };

    /* one key of a simple-report half (its stick a hat) as an organ */
    static int simpleOrgan(int side, int keycode, int scancode) {
        int[] hid = side == LEFT ? HID_LEFT : HID_RIGHT;
        if (scancode >= BTN_JOYSTICK && scancode < BTN_JOYSTICK + 16) return hid[scancode - BTN_JOYSTICK];
        if (scancode >= BTN_SOUTH && scancode < BTN_SOUTH + 15) return hid[scancode - BTN_SOUTH];
        if (keycode >= KeyEvent.KEYCODE_BUTTON_1 && keycode <= KeyEvent.KEYCODE_BUTTON_16)
            return hid[keycode - KeyEvent.KEYCODE_BUTTON_1];
        for (int i = 0; i < GAMEPAD_KEYS.length; i++) if (GAMEPAD_KEYS[i] == keycode) return hid[i];
        return 0;
    }

    /* one key of a hid-nintendo half as an organ, 0 when it is none of them */
    static int organ(int side, int keycode, int scancode) {
        boolean l = side == LEFT;
        int[] hid = l ? HID_LEFT : HID_RIGHT;
        switch (scancode) {
            case BTN_DPAD_UP: return UP;
            case BTN_DPAD_DOWN: return DOWN;
            case BTN_DPAD_LEFT: return DLEFT;
            case BTN_DPAD_RIGHT: return DRIGHT;
            case BTN_SOUTH: return FACE_S;
            case BTN_EAST: return FACE_E;
            case BTN_NORTH: return FACE_N;
            case BTN_WEST: return FACE_W;
            case BTN_Z: return CAPTURE;
            case BTN_TL: return l ? SHOULDER : SL;
            case BTN_TL2: return l ? ZTRIG : SR;
            case BTN_TR: return l ? SR : SHOULDER;
            case BTN_TR2: return l ? SL : ZTRIG;
            case BTN_SELECT: return MINUS;
            case BTN_START: return PLUS;
            case BTN_MODE: return HOME;
            case BTN_THUMBL:
            case BTN_THUMBR: return STICK;
            default:
                if (scancode >= BTN_JOYSTICK && scancode < BTN_JOYSTICK + 16) return hid[scancode - BTN_JOYSTICK];
        }
        if (keycode >= KeyEvent.KEYCODE_BUTTON_1 && keycode <= KeyEvent.KEYCODE_BUTTON_16)
            return hid[keycode - KeyEvent.KEYCODE_BUTTON_1];
        switch (keycode) {
            case KeyEvent.KEYCODE_DPAD_UP: return UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return DLEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return DRIGHT;
            case KeyEvent.KEYCODE_BUTTON_A: return FACE_S;          /* Generic.kl: BTN_SOUTH */
            case KeyEvent.KEYCODE_BUTTON_B: return FACE_E;
            case KeyEvent.KEYCODE_BUTTON_X: return FACE_N;          /* BTN_NORTH is BTN_X */
            case KeyEvent.KEYCODE_BUTTON_Y: return FACE_W;
            case KeyEvent.KEYCODE_BUTTON_L1: return l ? SHOULDER : SL;
            case KeyEvent.KEYCODE_BUTTON_L2: return l ? ZTRIG : SR;
            case KeyEvent.KEYCODE_BUTTON_R1: return l ? SR : SHOULDER;
            case KeyEvent.KEYCODE_BUTTON_R2: return l ? SL : ZTRIG;
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_BACK: return MINUS;
            case KeyEvent.KEYCODE_BUTTON_START:
            case KeyEvent.KEYCODE_MENU: return PLUS;
            case KeyEvent.KEYCODE_BUTTON_MODE:
            case KeyEvent.KEYCODE_HOME: return HOME;
            case KeyEvent.KEYCODE_BUTTON_Z:
            case KeyEvent.KEYCODE_MEDIA_RECORD: return CAPTURE;
            case KeyEvent.KEYCODE_BUTTON_THUMBL:
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return STICK;
            default: return 0;
        }
    }

    /* one half: its organs held and its stick, upright, up positive */
    static final class Half {
        final int side;
        final int axisX, axisY;              /* the stick's pair, -1 when it is the hat */
        final float minX, maxX, minY, maxY, flat;
        int organs;
        float x, y;

        /*
         * axes/min/max/flat: the device's joystick-class motion ranges. The
         * stick is the first centred pair of X/Y, RX/RY, Z/RZ; with none,
         * the hat (the simple HID report).
         */
        Half(int side, int[] axes, float[] min, float[] max, float[] flat) {
            this.side = side;
            int[][] pairs = { { MotionEvent.AXIS_X, MotionEvent.AXIS_Y }, { MotionEvent.AXIS_RX, MotionEvent.AXIS_RY },
                    { MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ } };
            int ax = -1, ay = -1;
            float x0 = -1f, x1 = 1f, y0 = -1f, y1 = 1f, fl = 0f;
            for (int[] p : pairs) {
                int i = find(axes, p[0]), j = find(axes, p[1]);
                if (i < 0 || j < 0 || !(min[i] < 0f && max[i] > 0f && min[j] < 0f && max[j] > 0f)) continue;
                ax = p[0];
                ay = p[1];
                x0 = min[i]; x1 = max[i]; y0 = min[j]; y1 = max[j];
                float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
                fl = Math.max(hx > 0f ? flat[i] / hx : 0f, hy > 0f ? flat[j] / hy : 0f);
                break;
            }
            axisX = ax;
            axisY = ay;
            minX = x0; maxX = x1; minY = y0; maxY = y1;
            this.flat = Math.max(0f, Math.min(0.9f, fl));
        }

        private static int find(int[] a, int v) {
            for (int i = 0; i < a.length; i++) if (a[i] == v) return i;
            return -1;
        }

        boolean hatStick() {
            return axisX < 0;
        }

        /* a key: true when it is one of the half's organs */
        boolean key(int keycode, int scancode, boolean down) {
            int o = hatStick() ? simpleOrgan(side, keycode, scancode) : organ(side, keycode, scancode);
            if (o == 0) return false;
            if (down) organs |= o; else organs &= ~o;
            return true;
        }

        /*
         * One motion event: vx/vy the values of axisX/axisY (ignored for a hat
         * stick), hx/hy the hat's. Android's Y grows downwards, ours upwards.
         * The hat of the simple HID report is the half held sideways, turned
         * here to the half held upright: the left half lies with its rail up
         * (a quarter turn anticlockwise from upright), the right one with its
         * rail up the other way.
         */
        void motion(float vx, float vy, float hx, float hy) {
            float sx, sy;
            if (hatStick()) {
                float ux = Math.round(hx), uy = -Math.round(hy);       /* sideways, up positive */
                if (side == LEFT) { sx = uy; sy = -ux; }               /* sideways up = upright right */
                else { sx = -uy; sy = ux; }                            /* sideways up = upright left */
                float r = (float) Math.hypot(sx, sy);
                if (r > 1f) { sx /= r; sy /= r; }
                x = sx;
                y = sy;
                return;
            }
            sx = 2f * (vx - minX) / (maxX - minX) - 1f;
            sy = -(2f * (vy - minY) / (maxY - minY) - 1f);
            sx = Math.max(-1f, Math.min(1f, sx));
            sy = Math.max(-1f, Math.min(1f, sy));
            float r = (float) Math.hypot(sx, sy);
            if (r <= flat || r <= 0f) {
                x = y = 0f;
            } else {
                float k = (r - flat) / (1f - flat) / r;
                x = sx * k;
                y = sy * k;
            }
        }

        /* everything let go: a pause, a cancel, a split */
        void release() {
            organs = 0;
            x = y = 0f;
        }

        String stickName() {
            if (hatStick()) return "hat (sideways, turned upright)";
            return axisName(axisX) + "/" + axisName(axisY);
        }

        private static String axisName(int a) {
            switch (a) {
                case MotionEvent.AXIS_X: return "X";
                case MotionEvent.AXIS_Y: return "Y";
                case MotionEvent.AXIS_RX: return "RX";
                case MotionEvent.AXIS_RY: return "RY";
                case MotionEvent.AXIS_Z: return "Z";
                case MotionEvent.AXIS_RZ: return "RZ";
                default: return "a" + a;
            }
        }
    }

    private static boolean has(Half h, int o) {
        return h != null && (h.organs & o) != 0;
    }

    /*
     * The pair in our pad model: the bits returned, out[] the 8 axes (PadDb's
     * AX_*). Each half gives only its own side.
     */
    static int merge(Half l, Half r, float[] out) {
        int bits = 0;
        if (has(l, UP)) bits |= 1 << PadDb.BTN_UP;
        if (has(l, DOWN)) bits |= 1 << PadDb.BTN_DOWN;
        if (has(l, DLEFT)) bits |= 1 << PadDb.BTN_LEFT;
        if (has(l, DRIGHT)) bits |= 1 << PadDb.BTN_RIGHT;
        if (has(l, SHOULDER)) bits |= 1 << PadDb.BTN_L1;
        if (has(l, STICK)) bits |= 1 << PadDb.BTN_L3;
        if (has(l, MINUS)) bits |= 1 << PadDb.BTN_OPTIONS;
        if (has(r, FACE_S)) bits |= 1 << PadDb.BTN_A;
        if (has(r, FACE_E)) bits |= 1 << PadDb.BTN_B;
        if (has(r, FACE_W)) bits |= 1 << PadDb.BTN_X;
        if (has(r, FACE_N)) bits |= 1 << PadDb.BTN_Y;
        if (has(r, SHOULDER)) bits |= 1 << PadDb.BTN_R1;
        if (has(r, STICK)) bits |= 1 << PadDb.BTN_R3;
        if (has(r, PLUS)) bits |= 1 << PadDb.BTN_MENU;
        out[PadDb.AX_LEFT_X] = l != null ? l.x : 0f;
        out[PadDb.AX_LEFT_Y] = l != null ? l.y : 0f;
        out[PadDb.AX_RIGHT_X] = r != null ? r.x : 0f;
        out[PadDb.AX_RIGHT_Y] = r != null ? r.y : 0f;
        out[PadDb.AX_HAT_X] = ((bits >> PadDb.BTN_RIGHT) & 1) - ((bits >> PadDb.BTN_LEFT) & 1);
        out[PadDb.AX_HAT_Y] = ((bits >> PadDb.BTN_UP) & 1) - ((bits >> PadDb.BTN_DOWN) & 1);
        out[PadDb.AX_L2] = has(l, ZTRIG) ? 1f : 0f;
        out[PadDb.AX_R2] = has(r, ZTRIG) ? 1f : 0f;
        return bits;
    }
}
