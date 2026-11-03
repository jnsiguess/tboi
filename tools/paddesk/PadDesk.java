package ru.isaac.port;

import android.view.KeyEvent;
import android.view.MotionEvent;

/*
 * The desk harness of PadDb: synthetic devices with the vendor/product, axes
 * and keys of real pads, the real assets/paddb.txt, and the final mapping in
 * our pad model asserted. Every expectation below is read off the database
 * line the device must land on (quoted in the comment) and SDL's numbering
 * (PadDb's header) - not off PadDb's own output.
 *   bash tools/paddesk/run.sh
 */
final class PadDesk {
    private static int checks, fails;

    private static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            fails++;
            System.out.println("  FAIL " + what);
        }
    }

    private static void eq(float have, float want, String what) {
        check(Math.abs(have - want) < 0.01f, what + " = " + have + ", want " + want);
    }

    private static final int JOY = 0x01000010;                 /* SOURCE_JOYSTICK */
    private static final int X = MotionEvent.AXIS_X, Y = MotionEvent.AXIS_Y, Z = MotionEvent.AXIS_Z,
            RZ = MotionEvent.AXIS_RZ, RX = MotionEvent.AXIS_RX, RY = MotionEvent.AXIS_RY,
            LT = MotionEvent.AXIS_LTRIGGER, RT = MotionEvent.AXIS_RTRIGGER, BRAKE = MotionEvent.AXIS_BRAKE,
            GAS = MotionEvent.AXIS_GAS, HX = MotionEvent.AXIS_HAT_X, HY = MotionEvent.AXIS_HAT_Y;
    private static final int K_A = KeyEvent.KEYCODE_BUTTON_A, K_B = KeyEvent.KEYCODE_BUTTON_B,
            K_X = KeyEvent.KEYCODE_BUTTON_X, K_Y = KeyEvent.KEYCODE_BUTTON_Y, K_L1 = KeyEvent.KEYCODE_BUTTON_L1,
            K_R1 = KeyEvent.KEYCODE_BUTTON_R1, K_L2 = KeyEvent.KEYCODE_BUTTON_L2, K_R2 = KeyEvent.KEYCODE_BUTTON_R2,
            K_START = KeyEvent.KEYCODE_BUTTON_START, K_SELECT = KeyEvent.KEYCODE_BUTTON_SELECT,
            K_MODE = KeyEvent.KEYCODE_BUTTON_MODE, K_TL = KeyEvent.KEYCODE_BUTTON_THUMBL,
            K_TR = KeyEvent.KEYCODE_BUTTON_THUMBR;
    /* the keys of a full modern pad: with a hat, SDL's 16-bit button mask is ffff */
    private static final int[] FULL = { K_A, K_B, K_X, K_Y, K_SELECT, K_MODE, K_START, K_TL, K_TR, K_L1, K_R1, K_L2, K_R2 };

    private static boolean trig(int a) {
        return a == LT || a == RT || a == BRAKE || a == GAS;
    }

    private static PadDb.Device dev(String name, String desc, int vid, int pid, int sdk, int[] axes, int[] keys) {
        int n = axes.length;
        int[] src = new int[n];
        float[] lo = new float[n], hi = new float[n], fl = new float[n];
        for (int i = 0; i < n; i++) {
            src[i] = JOY;
            lo[i] = trig(axes[i]) ? 0f : -1f;
            hi[i] = 1f;
        }
        boolean[] has = new boolean[PadDb.SDL_KEYS.length];
        for (int k : keys)
            for (int j = 0; j < has.length; j++) if (PadDb.SDL_KEYS[j] == k) has[j] = true;
        return new PadDb.Device(name, desc, vid, pid, sdk, axes, src, lo, hi, fl, has);
    }

    /* one motion event: pairs (axis, value); every other axis at its Android rest (0) */
    private static void move(PadDb.Layout l, float... pairs) {
        float[] raw = new float[l.dev.axes.length];
        float hx = 0f, hy = 0f;
        for (int p = 0; p < pairs.length; p += 2) {
            int a = (int) pairs[p];
            if (a == HX) hx = pairs[p + 1];
            else if (a == HY) hy = pairs[p + 1];
            else raw[l.dev.index(a)] = pairs[p + 1];
        }
        l.motion(raw, hx, hy);
    }

    private static final float[] out = new float[8], raw = new float[8];

    private static int read(PadDb.Layout l) {
        return l.read(out, raw);
    }

    private static boolean bit(int bits, int b) {
        return (bits & (1 << b)) != 0;
    }

    private static PadDb.Pick pick(PadDb db, PadDb.Device d, String title) {
        PadDb.Pick p = db.choose(d);
        System.out.println(title + ": " + p.line + (p.layout != null ? "\n    " + p.layout.mapping : ""));
        return p;
    }

    public static void main(String[] args) throws Exception {
        PadDb db;
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(args[0]), java.nio.charset.StandardCharsets.UTF_8))) {
            db = PadDb.parse(in, 33);
        }
        System.out.println("paddb.txt: " + db.lines + " lines, " + db.size() + " entries after SDL's load order on API 33, "
                + db.skippedSdk + " skipped by sdk>=/<=");
        check(db.lines == 58, "asset lines " + db.lines);

        /* SDL_crc16 is CRC-16/ARC: the check value of "123456789" is 0xbb3d */
        check(PadDb.crc16("123456789".getBytes("US-ASCII")) == 0xbb3d, "crc16");

        /* RangeComparator: X Y RX RY Z RZ LT RT BRAKE GAS, hats aside; Z with RX/RY sets 0x8000 */
        PadDb.Device s = dev("sort", "d", 1, 2, 33, new int[] { HY, GAS, RZ, X, RY, LT, Z, Y, RX, BRAKE, RT, HX }, new int[0]);
        check(java.util.Arrays.equals(s.axes, new int[] { X, Y, RX, RY, Z, RZ, LT, RT, BRAKE, GAS }),
                "sort order " + java.util.Arrays.toString(s.axes));
        check(s.axisMask == 0x803f && s.hats, "axis mask " + Integer.toHexString(s.axisMask));

        /* 1. 8BitDo SN30 Pro, Android mode (2dc8:6001):
         *    05000000c82d000001600000ffff3f00 a:b1,b:b0,lefttrigger:a5,righttrigger:a4,x:b3,y:b2 */
        PadDb.Device sn = dev("8BitDo SN30 Pro", "0123456789abcdef", 0x2dc8, 0x6001, 33,
                new int[] { X, Y, Z, RZ, BRAKE, GAS, HX, HY }, FULL);
        check(sn.guidHex().equals("05000000c82d000001600000ffff3f00"), "SN30 Pro guid " + sn.guidHex());
        PadDb.Pick p = pick(db, sn, "SN30 Pro");
        check(p.source == PadDb.SOURCE_DB && p.line.contains("8BitDo SN30 Pro") && p.line.contains("exact"), "SN30 Pro source");
        PadDb.Layout l = p.layout;
        l.key(K_B, 0, true);                                   /* the bottom button sends BUTTON_B */
        int bits = read(l);
        check(bit(bits, PadDb.BTN_A) && !bit(bits, PadDb.BTN_B), "SN30 bottom (BUTTON_B) -> A");
        l.key(K_B, 0, false);
        l.key(K_Y, 0, true);                                   /* the left button sends BUTTON_Y: x:b3 */
        check(bit(read(l), PadDb.BTN_X), "SN30 BUTTON_Y -> X");
        l.key(K_Y, 0, false);
        move(l, GAS, 1f);
        read(l);
        eq(out[PadDb.AX_L2], 1f, "SN30 GAS -> L2");
        eq(out[PadDb.AX_R2], 0f, "SN30 R2 at rest");
        move(l, BRAKE, 0.5f, X, 1f, Y, -1f, HY, -1f);
        bits = read(l);
        eq(out[PadDb.AX_R2], 0.5f, "SN30 BRAKE half -> R2");
        eq(out[PadDb.AX_L2], 0f, "SN30 L2 let go");
        eq(out[PadDb.AX_LEFT_X], 1f, "SN30 X -> left X");
        eq(out[PadDb.AX_LEFT_Y], 1f, "SN30 Android up (-1) -> left Y up (+1)");
        check(bit(bits, PadDb.BTN_UP) && out[PadDb.AX_HAT_Y] == 1f, "SN30 hat up -> d-pad up");
        move(l);
        check(!bit(read(l), PadDb.BTN_UP), "SN30 hat back");

        /* 2. the same pad with no L2 key: the mask differs, the second pass finds it */
        PadDb.Device sn2 = dev("8BitDo SN30 Pro", "x", 0x2dc8, 0x6001, 33, new int[] { X, Y, Z, RZ, BRAKE, GAS, HX, HY },
                new int[] { K_A, K_B, K_X, K_Y, K_SELECT, K_START, K_L1, K_R1 });
        p = pick(db, sn2, "SN30 Pro, other keys");
        check(p.source == PadDb.SOURCE_DB && p.line.contains("button mask aside"), "SN30 second pass");

        /* 3. DualShock 4 v2 (054c:09cc), modern Android key layout:
         *    050000004c050000cc090000ffff3f00 a:b0,lefttrigger:a4,rightx:a2,righty:a3 */
        PadDb.Device ds4 = dev("Wireless Controller", "ds4", 0x054c, 0x09cc, 33, new int[] { X, Y, Z, RZ, LT, RT, HX, HY }, FULL);
        p = pick(db, ds4, "DualShock 4");
        check(p.source == PadDb.SOURCE_DB && p.line.contains("PS4 Controller"), "DS4 source");
        l = p.layout;
        move(l, LT, 0.5f, Z, -1f, RZ, 1f);
        read(l);
        eq(out[PadDb.AX_L2], 0.5f, "DS4 LTRIGGER half -> L2");
        eq(out[PadDb.AX_RIGHT_X], -1f, "DS4 Z -> right X");
        eq(out[PadDb.AX_RIGHT_Y], -1f, "DS4 RZ down -> right Y down");
        l.key(K_A, 0, true);
        l.key(K_START, 0, true);
        l.key(K_SELECT, 0, true);
        bits = read(l);
        check(bit(bits, PadDb.BTN_A) && bit(bits, PadDb.BTN_MENU) && bit(bits, PadDb.BTN_OPTIONS), "DS4 cross/options/share");

        /* 4. DualSense (054c:0ce6) on an older key layout: X Y Z RX RY RZ, no THUMBR in hasKeys (mask feff:
         *    050000004c050000e60c0000fffe3f80 lefttrigger:a2,righttrigger:a3,rightx:a4,righty:a5,leftshoulder:b3 */
        PadDb.Device ds5 = dev("DualSense Wireless Controller", "ds5", 0x054c, 0x0ce6, 33,
                new int[] { X, Y, Z, RX, RY, RZ, HX, HY },
                new int[] { K_A, K_B, K_X, K_Y, K_SELECT, K_MODE, K_START, K_TL, K_L1, K_R1, K_L2, K_R2 });
        check(ds5.guidHex().equals("050000004c050000e60c0000fffe3f80"), "DS5 guid " + ds5.guidHex());
        p = pick(db, ds5, "DualSense");
        l = p.layout;
        check(p.source == PadDb.SOURCE_DB && p.line.contains("exact"), "DS5 source");
        move(l, RX, 1f, Z, 1f);                                /* a2 = RX (the left trigger), a4 = Z */
        read(l);
        eq(out[PadDb.AX_L2], 1f, "DS5 RX -> L2");
        eq(out[PadDb.AX_RIGHT_X], 1f, "DS5 Z -> right X");
        move(l, RX, -1f);
        read(l);
        eq(out[PadDb.AX_L2], 0f, "DS5 RX at -1 -> L2 released");
        l.key(K_Y, 0, true);                                   /* leftshoulder:b3 */
        check(bit(read(l), PadDb.BTN_L1), "DS5 BUTTON_Y -> L1 (as its line says)");

        /* 5. Xbox Series (045e:0b13): 050000005e040000130b0000ffff3f00 lefttrigger:a4,rightx:a2 */
        PadDb.Device xs = dev("Xbox Wireless Controller", "xs", 0x045e, 0x0b13, 33,
                new int[] { X, Y, Z, RZ, LT, RT, BRAKE, GAS, HX, HY }, FULL);
        p = pick(db, xs, "Xbox Series");
        l = p.layout;
        check(p.source == PadDb.SOURCE_DB && p.line.contains("Xbox Series"), "Xbox Series source");
        move(l, LT, 1f, RT, 0.25f, Z, 0.5f);
        read(l);
        eq(out[PadDb.AX_L2], 1f, "Xbox LTRIGGER -> L2");
        eq(out[PadDb.AX_R2], 0.25f, "Xbox RTRIGGER -> R2");
        eq(out[PadDb.AX_RIGHT_X], 0.5f, "Xbox Z -> right X");

        /* 6. Xbox One S (045e:02e0) on a modern key layout: the community line e0020000ffff3f00 (stale:
         *    lefttrigger:a2 = Z here, leftshoulder:b3 = BUTTON_Y) is dropped - SDL's own line wins, with
         *    the button mask aside: 050000005e040000e002000000783f00 lefttrigger:a4,rightx:a2,leftshoulder:b9 */
        PadDb.Device xo = dev("Xbox Wireless Controller", "xo", 0x045e, 0x02e0, 33,
                new int[] { X, Y, Z, RZ, LT, RT, BRAKE, GAS, HX, HY }, FULL);
        p = pick(db, xo, "Xbox One S");
        l = p.layout;
        check(p.source == PadDb.SOURCE_DB && p.line.contains("Xbox One S Controller") && p.line.contains("SDL, button mask aside"),
                "Xbox One S: SDL's line");
        move(l, Z, 1f, LT, 1f);
        l.key(K_L1, 0, true);
        bits = read(l);
        eq(out[PadDb.AX_RIGHT_X], 1f, "Xbox One S: Z -> right X");
        eq(out[PadDb.AX_L2], 1f, "Xbox One S: LTRIGGER -> L2");
        check(bit(bits, PadDb.BTN_L1), "Xbox One S: BUTTON_L1 -> L1");

        /* 7. Switch Pro (057e:2009), API >= 29: SDL's built-in sdk>=:29 line, not the community one:
         *    050000007e05000009200000ffff0f00 leftshoulder:b9,lefttrigger:b15,start:b6 */
        PadDb.Device sw = dev("Pro Controller", "sw", 0x057e, 0x2009, 33, new int[] { X, Y, Z, RZ, HX, HY }, FULL);
        p = pick(db, sw, "Switch Pro, API 33");
        l = p.layout;
        check(p.source == PadDb.SOURCE_DB && p.line.contains("SDL, exact") && l.mapping.contains("sdk>=:29"),
                "Switch Pro API 33: SDL's sdk>=29 line");
        l.key(K_L1, 0, true);
        l.key(K_L2, 0, true);
        l.key(K_START, 0, true);
        bits = read(l);
        check(bit(bits, PadDb.BTN_L1) && bit(bits, PadDb.BTN_MENU), "Switch Pro: L1 -> L1, START -> Menu");
        eq(out[PadDb.AX_L2], 1f, "Switch Pro: ZL (BUTTON_L2) -> L2");
        PadDb db28;
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(args[0]), java.nio.charset.StandardCharsets.UTF_8))) {
            db28 = PadDb.parse(in, 28);
        }
        p = pick(db28, dev("Pro Controller", "sw", 0x057e, 0x2009, 28, new int[] { X, Y, Z, RZ, HX, HY }, FULL),
                "Switch Pro, API 28");
        check(p.source == PadDb.SOURCE_DB && p.layout.mapping.contains("sdk<=:28"), "Switch Pro API 28: SDL's sdk<=28 line");

        /* 8. 8BitDo M30 (2dc8:5006): SDL's line (leftx:a0, no d-pad) over the community one (d-pad on half axes) */
        PadDb.Device m30 = dev("8BitDo M30", "m30", 0x2dc8, 0x5006, 33, new int[] { X, Y, Z, RZ, BRAKE, GAS, HX, HY }, FULL);
        p = pick(db, m30, "8BitDo M30");
        l = p.layout;
        check(p.line.contains("8BitDo M30 Gamepad") && p.line.contains("SDL"), "M30: SDL's line");
        move(l, Y, 1f, X, -1f);
        bits = read(l);
        eq(out[PadDb.AX_LEFT_X], -1f, "M30 X -> left X");
        eq(out[PadDb.AX_LEFT_Y], -1f, "M30 Y down -> left Y down");
        check(!bit(bits, PadDb.BTN_DOWN), "M30: no d-pad from the stick in SDL's line");
        /* the half-axis syntax, on the community line's own elements */
        PadDb.Layout half = new PadDb.Layout(m30, "dpdown:+a1,dpleft:-a0,dpright:+a0,dpup:-a1,", PadDb.MAX_BUTTONS);
        move(half, Y, 1f, X, -1f);
        bits = read(half);
        check(bit(bits, PadDb.BTN_DOWN) && bit(bits, PadDb.BTN_LEFT) && !bit(bits, PadDb.BTN_UP), "half axes -> d-pad");
        move(half, Y, 0.4f);
        check(!bit(read(half), PadDb.BTN_DOWN), "+a1 at 0.4: under SDL's threshold (half of the half axis)");

        /* 8b. a pad only the community file knows: Flydigi Vader 2 (04b4:2411)
         *     05000000b404000011240000dfff3f00 a:b0,lefttrigger:a4,rightx:a2 */
        PadDb.Device fd = dev("Flydigi Vader 2", "fd", 0x04b4, 0x2411, 33, new int[] { X, Y, Z, RZ, LT, RT, HX, HY },
                new int[] { K_A, K_B, K_X, K_Y, K_SELECT, K_START, K_TL, K_TR, K_L1, K_R1, K_L2, K_R2 });
        p = pick(db, fd, "Flydigi Vader 2");
        l = p.layout;
        check(p.source == PadDb.SOURCE_DB && p.line.contains("community, exact"), "Vader 2: the community line");
        move(l, LT, 1f, Z, -1f);
        read(l);
        eq(out[PadDb.AX_L2], 1f, "Vader 2 LTRIGGER -> L2");
        eq(out[PadDb.AX_RIGHT_X], -1f, "Vader 2 Z -> right X");

        /* 8c. SDL knows the pad (XiaoMi 2717:3144: ffff3f80 only), the device's axes fit none of its lines:
         *     the community ffff3f00 line was dropped - Android's standard layout, not the community line */
        p = pick(db, dev("Mi Gamepad", "mi", 0x2717, 0x3144, 33, new int[] { X, Y, Z, RZ, LT, RT, HX, HY }, FULL), "XiaoMi");
        check(p.source == PadDb.SOURCE_STANDARD && p.line.contains("SDL has lines for this pad"), "XiaoMi: standard");

        /* 9. Xbox 360 (045e:028e) with no keys in hasKeys: mask 7800, nbuttons 15 - L2 (b15) is past them */
        PadDb.Device x360 = dev("Xbox 360", "x360", 0x045e, 0x028e, 33, new int[] { X, Y, Z, RZ, LT, RT, HX, HY }, new int[0]);
        check(x360.guidHex().equals("050000005e0400008e02000000783f00") && x360.nbuttons == 15, "360 guid/nbuttons");
        p = pick(db, x360, "Xbox 360, no keys");
        l = p.layout;
        check(l.key(K_A, 0, true) && bit(read(l), PadDb.BTN_A), "360 A");
        check(l.key(K_L2, 0, true), "360 L2 is SDL's button 15 (consumed)");

        /* 10. the owner's 8BitDo Ultimate (2dc8:3012): no Android line -> Android's standard layout */
        PadDb.Device ult = dev("8BitDo Ultimate Wireless", "ult", 0x2dc8, 0x3012, 33,
                new int[] { X, Y, Z, RZ, BRAKE, GAS, HX, HY }, FULL);
        p = pick(db, ult, "8BitDo Ultimate");
        l = p.layout;
        check(p.source == PadDb.SOURCE_STANDARD, "Ultimate: standard");
        l.key(K_A, 0, true);
        move(l, BRAKE, 1f, GAS, 0.5f, Z, 1f, RZ, -1f, HX, 1f);
        bits = read(l);
        check(bit(bits, PadDb.BTN_A) && bit(bits, PadDb.BTN_RIGHT), "Ultimate A, hat right");
        eq(out[PadDb.AX_L2], 1f, "Ultimate BRAKE -> L2");
        eq(out[PadDb.AX_R2], 0.5f, "Ultimate GAS -> R2");
        eq(out[PadDb.AX_RIGHT_X], 1f, "Ultimate Z -> right X");
        eq(out[PadDb.AX_RIGHT_Y], 1f, "Ultimate RZ up -> right Y up");
        l.release();
        bits = read(l);
        check(bits == 0 && out[PadDb.AX_L2] == 0f && out[PadDb.AX_RIGHT_X] == 0f, "Ultimate released");

        /* 11. 8BitDo Pro 2 in D (Android) mode (2dc8:6006): not in the database -> standard */
        p = pick(db, dev("8BitDo Pro 2", "p2", 0x2dc8, 0x6006, 33, new int[] { X, Y, Z, RZ, BRAKE, GAS, HX, HY }, FULL), "Pro 2 D-mode");
        check(p.source == PadDb.SOURCE_STANDARD, "Pro 2 D: standard");

        /* 12. a generic Chinese pad: X Y Z RZ + hat, triggers as keys only -> standard, L2 key -> L2 */
        PadDb.Device cn = dev("USB Gamepad", "cn", 0x0079, 0x0006, 33, new int[] { X, Y, Z, RZ, HX, HY }, FULL);
        p = pick(db, cn, "generic pad");
        l = p.layout;
        check(p.source == PadDb.SOURCE_STANDARD, "generic: standard");
        l.key(K_L2, 0, true);
        move(l, Z, -1f);
        read(l);
        eq(out[PadDb.AX_L2], 1f, "generic L2 key -> L2");
        eq(out[PadDb.AX_RIGHT_X], -1f, "generic Z -> right X");

        /* 13. a DualShock 3 clone (raw HID): X Y Z RX RY RZ -> the guess */
        p = pick(db, dev("PS3 clone", "s3", 0x2563, 0x0575, 33, new int[] { X, Y, Z, RX, RY, RZ, HX, HY }, FULL), "DS3 clone");
        check(p.source == PadDb.SOURCE_GUESSED && p.layout == null, "DS3 clone: guessed");

        /* 14. Z/RZ and nothing else that could be a trigger -> the guess */
        p = pick(db, dev("Mystery", "my", 0x1234, 0x5678, 33, new int[] { X, Y, Z, RZ }, new int[] { K_A, K_B }), "Z/RZ only");
        check(p.source == PadDb.SOURCE_GUESSED, "Z/RZ only: guessed");

        /* 15. no vendor id: SDL puts the descriptor's first 11 characters into the GUID - never in the database */
        PadDb.Device nv = dev("BT pad", "a1b2c3d4e5f60718", 0, 0, 33, new int[] { X, Y, Z, RZ, LT, RT }, FULL);
        check(nv.guidHex().startsWith("050000006131623263336434") && nv.guidHex().endsWith("3f00"), "no-vendor guid " + nv.guidHex());
        p = pick(db, nv, "no vendor");
        check(p.source == PadDb.SOURCE_STANDARD, "no vendor: standard");

        /* 16. the element syntax: ~ inverts, a trigger from a button, a hat binding is dead on Android */
        PadDb.Layout syn = new PadDb.Layout(cn, "leftx:a0~,lefttrigger:b15,dpup:h0.1,-righty:a1,", PadDb.MAX_BUTTONS);
        move(syn, X, 1f, Y, 1f, HY, -1f);
        syn.key(K_L2, 0, true);
        bits = read(syn);
        eq(out[PadDb.AX_LEFT_X], -1f, "a0~ inverts");
        eq(out[PadDb.AX_L2], 1f, "lefttrigger:b15");
        check(!bit(bits, PadDb.BTN_UP), "h0.1 never on Android");
        eq(out[PadDb.AX_RIGHT_Y], 1f, "-righty from a full axis at +1 (SDL: -32768, up)");

        /* 17. the load order: same GUID replaced in place, sdk>=/<= skipped, crc: preferred when it matches */
        PadDb.Device k = dev("k", "kdesc", 0x1111, 0x2222, 30, new int[] { X, Y }, new int[] { K_A });
        String g = k.guidHex();
        String text = g + ",old,a:b1,\n"
                + g + ",sdk-high,a:b2,sdk>=:31,\n"
                + g + ",new,a:b0,\n"
                + g + ",crc-other,a:b3,crc:" + Integer.toHexString((k.crc + 1) & 0xffff) + ",\n";
        PadDb t = PadDb.parse(new java.io.BufferedReader(new java.io.StringReader(text)), 30);
        check(t.size() == 2 && t.skippedSdk == 1, "load: " + t.size() + " entries, " + t.skippedSdk + " skipped");
        check(t.choose(k).line.contains("\"new\""), "load: the later line replaced the earlier, the crc: one skipped");
        PadDb t2 = PadDb.parse(new java.io.BufferedReader(new java.io.StringReader(
                text + g + ",crc-mine,a:b3,crc:" + Integer.toHexString(k.crc) + ",\n")), 30);
        check(t2.choose(k).line.contains("crc-mine"), "load: a crc: that matches wins");


        /* 18. SDL first: a community line never replaces SDL's line, nor speaks for a vendor:product SDL knows */
        PadDb.Device ka = dev("ka", "kadesc", 0x3333, 0x4444, 33, new int[] { X, Y }, new int[] { K_A });
        PadDb.Device kb = dev("kb", "kbdesc", 0x3333, 0x4444, 33, new int[] { X, Y }, new int[] { K_A, K_B });
        PadDb.Device kc = dev("kc", "kcdesc", 0x5555, 0x6666, 33, new int[] { X, Y }, new int[] { K_A });
        String tiers = "@sdl\n" + ka.guidHex() + ",sdl-a,a:b0,\n@db\n"
                + ka.guidHex() + ",db-a,a:b1,\n" + kb.guidHex() + ",db-b,a:b1,\n" + kc.guidHex() + ",db-c,a:b2,\n";
        PadDb t3 = PadDb.parse(new java.io.BufferedReader(new java.io.StringReader(tiers)), 33);
        check(t3.skippedKnown == 1, "tiers: the community line of SDL's GUID is not taken");
        check(t3.choose(ka).line.contains("\"sdl-a\" (SDL, exact"), "tiers: SDL's exact line");
        check(t3.choose(kb).line.contains("\"sdl-a\" (SDL, button mask aside"),
                "tiers: SDL's line without the mask beats the community's exact one");
        check(t3.choose(kc).line.contains("\"db-c\" (community, exact"), "tiers: a pad SDL does not know");

        System.out.println(checks + " checks, " + fails + " failed");
        System.exit(fails == 0 ? 0 : 1);
    }
}
