package ru.isaac.port;

import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/*
 * A PAD'S LAYOUT, THE WAY SDL KNOWS IT (the owner's decision, 02.10.2026).
 *
 * Android names a pad's controls by key codes and axis ids, and pads disagree
 * about which physical control sends which: a Nintendo-style pad sends
 * BUTTON_B from its bottom button, one pad puts the left trigger on GAS,
 * another on RX. The community database of pad layouts that SDL reads
 * (SDL_GameControllerDB, zlib) settles that per pad. To use it, a pad is
 * named EXACTLY as SDL names it on Android, or every mapping shifts:
 *
 *   - the identity: SDL's GUID - bus 0x0005, the vendor and the product,
 *     the 16-bit button mask from hasKeys() over SDL's 36 keys and the axis
 *     mask from the number of axes (+0x8000 when Z and RX/RY are both there)
 *     (SDLControllerManager.java getButtonMask/getAxisMask, android/
 *     SDL_sysjoystick.c Android_AddJoystick, SDL_joystick.c
 *     SDL_CreateJoystickGUID);
 *   - the buttons: SDL's fixed key code -> index table (keycode_to_SDL), a
 *     hat turned into the d-pad buttons 11..14 (Android_OnHat);
 *   - the axes: the device's joystick-class motion ranges, sorted by SDL's
 *     RangeComparator (GAS and BRAKE swapped, Z between RY and RZ), each axis
 *     once, the hats aside; a value v is (v - min) / (max - min) * 2 - 1, as
 *     a 16-bit number;
 *   - the lookup: exact GUID, then without the button mask (SDL_gamepad.c
 *     SDL_PrivateGetGamepadMappingForGUID), the load order and the
 *     replacement of a GUID in place as SDL's; a binding is evaluated as
 *     SDL_GetGamepadAxis/SDL_GetGamepadButton do (half axes, ~, triggers
 *     0..1, the first non-zero binding of an axis, any binding of a button).
 * Evidence, with files and lines: the research notes of 02.10.2026, the SDL
 * pad database.
 *
 * ONE DEPARTURE FROM SDL (the owner, 02.10.2026, the fewest problems for
 * players): SDL's own built-in lines win. SDL lets the community file
 * replace them; here a community line is used only for a vendor:product
 * SDL has no line for at all (tools/paddb.py drops the rest, find() keeps
 * to it too).
 *
 * THE ORDER (the owner): 1) the pad's own database entry; 2) Android's
 * standard layout (developer docs, "Handle controller actions": the keys by
 * name, X/Y the left stick, Z/RZ the right, LTRIGGER/RTRIGGER or BRAKE/GAS
 * the triggers, HAT the d-pad) - when the device declares nothing that
 * standard leaves open; 3) the resting-value guess of batch 2 (MainActivity,
 * PadAxis) - only for what is left.
 *
 * Pure apart from describe(): a desk harness (tools/paddesk) feeds it
 * synthetic devices against android.jar.
 */
final class PadDb {
    /* our pad model: the bits and the axes of pad.c, in pad.c's order - MainActivity takes them from here */
    static final int BTN_A = 0, BTN_B = 1, BTN_X = 2, BTN_Y = 3, BTN_L1 = 4, BTN_R1 = 5, BTN_MENU = 6,
            BTN_OPTIONS = 7, BTN_L3 = 8, BTN_R3 = 9, BTN_UP = 10, BTN_DOWN = 11, BTN_LEFT = 12, BTN_RIGHT = 13;
    static final int AX_LEFT_X = 0, AX_LEFT_Y = 1, AX_RIGHT_X = 2, AX_RIGHT_Y = 3,
            AX_HAT_X = 4, AX_HAT_Y = 5, AX_L2 = 6, AX_R2 = 7, AX_COUNT = 8;

    static final int SOURCE_DB = 1, SOURCE_STANDARD = 2, SOURCE_GUESSED = 3;

    /* ---------- SDL's numbering ---------- */

    static final int MAX_BUTTONS = 36;                /* ANDROID_MAX_NBUTTONS */
    private static final int SDL_DPAD_UP = 11, SDL_DPAD_DOWN = 12, SDL_DPAD_LEFT = 13, SDL_DPAD_RIGHT = 14;

    /* keycode_to_SDL (SDL3 android/SDL_sysjoystick.c), then scancode_to_SDL for the left Joy-Con's d-pad */
    static int sdlButton(int keycode, int scancode) {
        switch (keycode) {
            case KeyEvent.KEYCODE_BUTTON_A: return 0;
            case KeyEvent.KEYCODE_BUTTON_B: return 1;
            case KeyEvent.KEYCODE_BUTTON_X: return 2;
            case KeyEvent.KEYCODE_BUTTON_Y: return 3;
            case KeyEvent.KEYCODE_BUTTON_L1: return 9;
            case KeyEvent.KEYCODE_BUTTON_R1: return 10;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 7;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 8;
            case KeyEvent.KEYCODE_MENU:
            case KeyEvent.KEYCODE_BUTTON_START: return 6;
            case KeyEvent.KEYCODE_BACK:
            case KeyEvent.KEYCODE_BUTTON_SELECT: return 4;
            case KeyEvent.KEYCODE_BUTTON_MODE: return 5;
            case KeyEvent.KEYCODE_BUTTON_L2:
            case KeyEvent.KEYCODE_MEDIA_RECORD: return 15;
            case KeyEvent.KEYCODE_BUTTON_R2: return 16;
            case KeyEvent.KEYCODE_BUTTON_C: return 17;
            case KeyEvent.KEYCODE_BUTTON_Z: return 18;
            case KeyEvent.KEYCODE_DPAD_UP: return SDL_DPAD_UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return SDL_DPAD_DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return SDL_DPAD_LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return SDL_DPAD_RIGHT;
            case KeyEvent.KEYCODE_DPAD_CENTER: return 0;
            default:
                if (keycode >= KeyEvent.KEYCODE_BUTTON_1 && keycode <= KeyEvent.KEYCODE_BUTTON_16)
                    return 20 + (keycode - KeyEvent.KEYCODE_BUTTON_1);
        }
        switch (scancode) {                          /* linux BTN_DPAD_UP.. (0x220..0x223) */
            case 0x220: return SDL_DPAD_UP;
            case 0x221: return SDL_DPAD_DOWN;
            case 0x222: return SDL_DPAD_LEFT;
            case 0x223: return SDL_DPAD_RIGHT;
            default: return -1;
        }
    }

    /* getButtonMask: the keys SDL asks hasKeys() about, and the mask bit each sets */
    static final int[] SDL_KEYS = {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_BACK, KeyEvent.KEYCODE_MENU, KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_BUTTON_START,
        KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR, KeyEvent.KEYCODE_BUTTON_L1,
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_DPAD_CENTER,
        KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_BUTTON_C, KeyEvent.KEYCODE_BUTTON_Z,
        KeyEvent.KEYCODE_BUTTON_1, KeyEvent.KEYCODE_BUTTON_2, KeyEvent.KEYCODE_BUTTON_3, KeyEvent.KEYCODE_BUTTON_4,
        KeyEvent.KEYCODE_BUTTON_5, KeyEvent.KEYCODE_BUTTON_6, KeyEvent.KEYCODE_BUTTON_7, KeyEvent.KEYCODE_BUTTON_8,
        KeyEvent.KEYCODE_BUTTON_9, KeyEvent.KEYCODE_BUTTON_10, KeyEvent.KEYCODE_BUTTON_11, KeyEvent.KEYCODE_BUTTON_12,
        KeyEvent.KEYCODE_BUTTON_13, KeyEvent.KEYCODE_BUTTON_14, KeyEvent.KEYCODE_BUTTON_15, KeyEvent.KEYCODE_BUTTON_16,
    };
    private static final int[] SDL_MASKS = {
        1, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 6, 1 << 5, 1 << 6, 1 << 7, 1 << 8, 1 << 9, 1 << 10,
        1 << 11, 1 << 12, 1 << 13, 1 << 14, 1 << 4, 1,
        1 << 15, 1 << 16, 1 << 17, 1 << 18,
        1 << 20, 1 << 21, 1 << 22, 1 << 23, 1 << 24, 1 << 25, 1 << 26, 1 << 27, 1 << 28, 1 << 29, 1 << 30, 1 << 31,
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    };
    private static final int KEY_L2 = 18, KEY_R2 = 19;   /* their places in SDL_KEYS */

    /* RangeComparator: GAS and BRAKE swapped, Z sorted between RY and RZ */
    static int sortKey(int axis) {
        if (axis == MotionEvent.AXIS_GAS) axis = MotionEvent.AXIS_BRAKE;
        else if (axis == MotionEvent.AXIS_BRAKE) axis = MotionEvent.AXIS_GAS;
        if (axis == MotionEvent.AXIS_Z) axis = MotionEvent.AXIS_RZ - 1;
        else if (axis > MotionEvent.AXIS_Z && axis < MotionEvent.AXIS_RZ) axis--;
        return axis;
    }

    /* SDL_crc16 (stdlib/SDL_crc16.c): CRC-16/ARC */
    static int crc16(byte[] data) {
        int crc = 0;
        for (byte b : data) {
            int r = (crc ^ b) & 0xff, c = 0;
            for (int i = 0; i < 8; i++) {
                c = (((c ^ r) & 1) != 0 ? 0xA001 : 0) ^ (c >>> 1);
                r >>>= 1;
            }
            crc = c ^ (crc >>> 8);
        }
        return crc & 0xffff;
    }

    /* ---------- the device, as SDL sees it ---------- */

    static final class Device {
        final String name, desc;              /* desc: the descriptor, or the name when there is none (SDL) */
        final int vendor, product, sdk;
        final int[] axes;                     /* Android axis ids in SDL's order: SDL's a0, a1, ... */
        final float[] min, max, flat;         /* flat as a share of the half range, 0..0.9 */
        final boolean hats;                   /* HAT_X and HAT_Y: the d-pad as buttons 11..14 */
        final int buttonMask, axisMask, nbuttons;
        final boolean keyL2, keyR2;
        final byte[] guid = new byte[16];     /* SDL's GUID, its CRC word nought (as SDL compares it) */
        final int crc;                        /* SDL's CRC word: crc16 of desc */

        Device(String name, String desc, int vendor, int product, int sdk,
               int[] rAxis, int[] rSource, float[] rMin, float[] rMax, float[] rFlat, boolean[] hasKeys) {
            this.name = name == null ? "" : name;
            this.desc = desc == null || desc.isEmpty() ? this.name : desc;
            this.vendor = vendor & 0xffff;
            this.product = product & 0xffff;
            this.sdk = sdk;
            /* SDLJoystickHandler.deviceAdded: sorted, the joystick class only, each axis once, the hats aside */
            int n = rAxis.length;
            Integer[] order = new Integer[n];
            for (int i = 0; i < n; i++) order[i] = i;
            java.util.Arrays.sort(order, (a, b) -> sortKey(rAxis[a]) - sortKey(rAxis[b]));   /* stable, as Collections.sort */
            java.util.ArrayList<Integer> keep = new java.util.ArrayList<>();
            java.util.HashSet<Integer> seen = new java.util.HashSet<>();
            int hatAxes = 0;
            for (int k = 0; k < n; k++) {
                int i = order[k];
                if ((rSource[i] & InputDevice.SOURCE_CLASS_JOYSTICK) == 0 || !seen.add(rAxis[i])) continue;
                if (rAxis[i] == MotionEvent.AXIS_HAT_X || rAxis[i] == MotionEvent.AXIS_HAT_Y) hatAxes++;
                else keep.add(i);
            }
            int m = keep.size();
            axes = new int[m];
            min = new float[m];
            max = new float[m];
            flat = new float[m];
            boolean haveZ = false, pastZ = false;
            for (int k = 0; k < m; k++) {
                int i = keep.get(k);
                axes[k] = rAxis[i];
                min[k] = rMin[i];
                max[k] = rMax[i];
                float half = (rMax[i] - rMin[i]) * 0.5f;
                flat[k] = half > 0f ? Math.max(0f, Math.min(0.9f, rFlat[i] / half)) : 0f;
                if (axes[k] == MotionEvent.AXIS_Z) haveZ = true;
                else if (axes[k] > MotionEvent.AXIS_Z && axes[k] < MotionEvent.AXIS_RZ) pastZ = true;
            }
            hats = hatAxes / 2 > 0;
            int am = 0;
            if (m >= 2) am |= 0x0003;
            if (m >= 4) am |= 0x000c;
            if (m >= 6) am |= 0x0030;
            if (haveZ && pastZ) am |= 0x8000;
            axisMask = am;
            int bm = 0;
            for (int i = 0; i < SDL_KEYS.length; i++) if (hasKeys != null && i < hasKeys.length && hasKeys[i]) bm |= SDL_MASKS[i];
            if (hats) bm |= (1 << SDL_DPAD_UP) | (1 << SDL_DPAD_DOWN) | (1 << SDL_DPAD_LEFT) | (1 << SDL_DPAD_RIGHT);
            buttonMask = bm;
            int nb = 0;
            if (bm == 0xFFFFFFFF) nb = MAX_BUTTONS;
            else for (int i = 0; i < 32; i++) if ((bm & (1 << i)) != 0) nb = i + 1;
            nbuttons = nb;
            keyL2 = hasKeys != null && hasKeys.length > KEY_L2 && hasKeys[KEY_L2];
            keyR2 = hasKeys != null && hasKeys.length > KEY_R2 && hasKeys[KEY_R2];
            /* SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_BLUETOOTH, vendor, product, 0, NULL, desc, 0, 0) */
            byte[] d = this.desc.getBytes(java.nio.charset.StandardCharsets.UTF_8);
            crc = crc16(d);
            put16(guid, 0, 0x0005);
            if (this.vendor != 0) {
                put16(guid, 4, this.vendor);
                put16(guid, 8, this.product);
            } else {
                System.arraycopy(d, 0, guid, 4, Math.min(d.length, 11));   /* strlcpy into 12 bytes */
            }
            put16(guid, 12, bm);                    /* guid16[6]: the button mask (SDL's "version") */
            put16(guid, 14, am);                    /* guid16[7]: the axis mask */
        }

        int index(int androidAxis) {
            for (int i = 0; i < axes.length; i++) if (axes[i] == androidAxis) return i;
            return -1;
        }

        String guidHex() { return hex(guid); }
    }

    private static void put16(byte[] g, int at, int v) {
        g[at] = (byte) v;
        g[at + 1] = (byte) (v >> 8);
    }

    static String hex(byte[] g) {
        StringBuilder s = new StringBuilder(32);
        for (byte b : g) s.append(Character.forDigit((b >> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
        return s.toString();
    }

    /* the device as SDL's Java side describes it (Android only: the desk builds Device itself) */
    static Device describe(InputDevice dev) {
        java.util.List<InputDevice.MotionRange> rs = dev.getMotionRanges();
        int n = rs.size();
        int[] axis = new int[n], src = new int[n];
        float[] lo = new float[n], hi = new float[n], fl = new float[n];
        for (int i = 0; i < n; i++) {
            InputDevice.MotionRange r = rs.get(i);
            axis[i] = r.getAxis();
            src[i] = r.getSource();
            lo[i] = r.getMin();
            hi[i] = r.getMax();
            fl[i] = r.getFlat();
        }
        boolean[] has;
        try {
            has = dev.hasKeys(SDL_KEYS);
        } catch (RuntimeException e) {
            has = new boolean[SDL_KEYS.length];
        }
        return new Device(dev.getName(), dev.getDescriptor(), dev.getVendorId(), dev.getProductId(),
                android.os.Build.VERSION.SDK_INT, axis, src, lo, hi, fl, has);
    }

    /* ---------- the database ---------- */

    static final class Entry {
        final byte[] guid = new byte[16];
        final String name, mapping;          /* mapping: everything after the name */
        final int crc;                       /* the crc: field, -1 when there is none */
        final boolean sdl;                   /* SDL's own built-in line (@sdl), else the community file's (@db) */

        Entry(byte[] g, String name, String mapping, int crc, boolean sdl) {
            System.arraycopy(g, 0, guid, 0, 16);
            this.name = name;
            this.mapping = mapping;
            this.crc = crc;
            this.sdl = sdl;
        }
    }

    private final java.util.ArrayList<Entry> entries = new java.util.ArrayList<>();
    /* the vendor:product pairs SDL has a built-in line for: the community file never speaks for them */
    private final java.util.HashSet<Integer> sdlPads = new java.util.HashSet<>();
    int lines, skippedSdk, skippedKnown;

    static final PadDb EMPTY = new PadDb();
    private static volatile PadDb shared = EMPTY;

    static PadDb shared() { return shared; }

    /* the database every judge reads: load() on the phone, the desk with its own parse */
    static void install(PadDb db) { shared = db == null ? EMPTY : db; }

    /* assets/paddb.txt (tools/paddb.py), once at startup; a missing asset leaves the database empty */
    static void load(android.content.res.AssetManager am, int sdk) {
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                am.open("paddb.txt"), java.nio.charset.StandardCharsets.UTF_8))) {
            install(parse(in, sdk));
        } catch (java.io.IOException e) {
            shared = EMPTY;
        }
    }

    private static int field(String line, String key) {
        int at = line.indexOf(key);
        if (at < 0) return Integer.MIN_VALUE;
        int i = at + key.length(), j = i;
        while (j < line.length() && line.charAt(j) != ',') j++;
        return parseIntSafe(line.substring(i, j), key.equals("crc:") ? 16 : 10);   /* SDL_strtol(,16) / SDL_atoi */
    }

    private static int parseIntSafe(String s, int radix) {
        try {
            return Integer.parseInt(s.trim(), radix);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    /* vendor:product of a GUID in SDL's standard form (SDL_GetJoystickGUIDInfo), 0 when it has none */
    private static int padOf(byte[] g) {
        int bus = (g[0] & 0xff) | (g[1] & 0xff) << 8;
        boolean standard = bus < 0x20 && g[6] == 0 && g[7] == 0 && g[10] == 0 && g[11] == 0;
        int v = (g[4] & 0xff) | (g[5] & 0xff) << 8, p = (g[8] & 0xff) | (g[9] & 0xff) << 8;
        return standard && v != 0 && p != 0 ? v << 16 | p : 0;
    }

    /*
     * The lines replayed as SDL loads them, section by section (@sdl - SDL's
     * built-ins, then @db - the community file; a text without sections is
     * all community): an sdk>=:/sdk<=: line that does not fit the phone is not
     * added; within a section a line whose GUID (and crc:) is there already
     * replaces it in place, any other goes to the end. THE PORT'S ONE
     * DEPARTURE FROM SDL (the owner, 02.10.2026, the fewest problems for
     * players): SDL's own lines win - a community line never replaces one,
     * and is not even consulted for a vendor:product SDL has a line for (find).
     */
    static PadDb parse(java.io.BufferedReader in, int sdk) throws java.io.IOException {
        PadDb db = new PadDb();
        java.util.HashMap<String, Integer> at = new java.util.HashMap<>();
        boolean sdl = false;
        for (String line; (line = in.readLine()) != null; ) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            if (line.equals("@sdl")) { sdl = true; continue; }
            if (line.equals("@db")) { sdl = false; continue; }
            int c1 = line.indexOf(','), c2 = c1 < 0 ? -1 : line.indexOf(',', c1 + 1);
            if (c1 != 32 || c2 < 0) continue;
            byte[] g = new byte[16];
            boolean ok = true;
            for (int i = 0; i < 16 && ok; i++) {
                int hi = Character.digit(line.charAt(2 * i), 16), lo = Character.digit(line.charAt(2 * i + 1), 16);
                if (hi < 0 || lo < 0) ok = false;
                else g[i] = (byte) (hi << 4 | lo);
            }
            if (!ok) continue;
            db.lines++;
            /* a pad SDL has a line for - even one this phone's API skipped - is SDL's */
            if (sdl && padOf(g) != 0) db.sdlPads.add(padOf(g));
            int ge = field(line, "sdk>=:"), le = field(line, "sdk<=:");
            if ((ge != Integer.MIN_VALUE && !(sdk >= ge)) || (le != Integer.MIN_VALUE && !(sdk <= le))) {
                db.skippedSdk++;
                continue;
            }
            int crc = field(line, "crc:");
            int guidCrc = (g[2] & 0xff) | (g[3] & 0xff) << 8;
            if (guidCrc != 0) {                        /* a CRC in the GUID moves into the crc: field */
                crc = guidCrc;
                g[2] = g[3] = 0;
            }
            if (crc == Integer.MIN_VALUE || crc == 0) crc = -1;
            Entry e = new Entry(g, line.substring(c1 + 1, c2), line.substring(c2 + 1), crc, sdl);
            String key = hex(g) + "/" + crc;
            Integer was = at.get(key);
            if (was != null && db.entries.get(was).sdl && !sdl) {
                db.skippedKnown++;                     /* SDL's own line stays */
            } else if (was != null) {
                db.entries.set(was, e);
            } else {
                at.put(key, db.entries.size());
                db.entries.add(e);
            }
        }
        return db;
    }

    int size() { return entries.size(); }

    /* SDL_PrivateMatchGamepadMappingForGUID, over one section */
    private Entry match(Device d, boolean sdl, boolean version, boolean exactCrc) {
        Entry best = null;
        for (Entry e : entries) {
            if (e.sdl != sdl) continue;
            boolean same = true;
            for (int i = 0; i < 16 && same; i++) {
                if (!version && (i == 12 || i == 13)) continue;     /* SDL_SetJoystickGUIDVersion(, 0) on both */
                if (e.guid[i] != d.guid[i]) same = false;           /* the CRC words: nought on both */
            }
            if (!same) continue;
            if (e.crc >= 0) {
                if (e.crc != d.crc) continue;
                return e;
            } else if (d.crc != 0 && exactCrc) {
                continue;
            }
            if (best == null) best = e;
        }
        return best;
    }

    /* SDL_PrivateGetGamepadMappingForGUID over one section: exact, without the CRC, without the button mask */
    private Entry find(Device d, boolean sdl, String[] how) {
        Entry e = match(d, sdl, true, true);
        if (e == null) e = match(d, sdl, true, false);
        if (e != null) { how[0] = "exact"; return e; }
        if (d.vendor != 0 && d.product != 0) {            /* SDL_JoystickGUIDUsesVersion */
            e = match(d, sdl, false, true);
            if (e == null) e = match(d, sdl, false, false);
            if (e != null) { how[0] = "button mask aside"; return e; }
        }
        return null;
    }

    /* SDL knows this vendor:product */
    boolean sdlKnows(Device d) {
        return sdlKnows(d.vendor, d.product);
    }

    /* the same by the bare ids: one of the pad's organs for InputBook's rule */
    boolean sdlKnows(int vendor, int product) {
        vendor &= 0xffff;
        product &= 0xffff;
        return vendor != 0 && product != 0 && sdlPads.contains(vendor << 16 | product);
    }

    /*
     * SDL's own lines first, all the way; the community file only for a pad
     * SDL has no line for at all - not even one for another mask of the same
     * vendor:product (the owner: SDL's line gives players the fewest
     * problems, a community line never overrides it).
     */
    Entry find(Device d, String[] how) {
        Entry e = find(d, true, how);
        if (e != null) { how[0] = "SDL, " + how[0]; return e; }
        if (sdlKnows(d)) return null;
        e = find(d, false, how);
        if (e != null) how[0] = "community, " + how[0];
        return e;
    }

    /* ---------- the choice ---------- */

    /*
     * Android's standard layout fits when the device declares nothing it
     * leaves open. RX/RY are a right stick on some pads and the triggers on
     * others; Z/RZ with no trigger anywhere else may be the triggers too
     * (batch 2, U5: a pad that sends its triggers on Z/RZ resting at -1).
     */
    static String standardRefusal(Device d) {
        boolean x = d.index(MotionEvent.AXIS_X) >= 0, y = d.index(MotionEvent.AXIS_Y) >= 0;
        boolean z = d.index(MotionEvent.AXIS_Z) >= 0, rz = d.index(MotionEvent.AXIS_RZ) >= 0;
        boolean rx = d.index(MotionEvent.AXIS_RX) >= 0, ry = d.index(MotionEvent.AXIS_RY) >= 0;
        boolean trig = d.index(MotionEvent.AXIS_LTRIGGER) >= 0 || d.index(MotionEvent.AXIS_RTRIGGER) >= 0
                || d.index(MotionEvent.AXIS_BRAKE) >= 0 || d.index(MotionEvent.AXIS_GAS) >= 0 || d.keyL2 || d.keyR2;
        if (x != y) return "one of X/Y";
        if (rx || ry) return "RX/RY declared (a stick or the triggers)";
        if (z != rz) return "one of Z/RZ";
        if (z && !trig) return "Z/RZ and no trigger elsewhere (a stick or the triggers)";
        return null;
    }

    private static void bindAxis(StringBuilder m, String out, Device d, int androidAxis) {
        int i = d.index(androidAxis);
        if (i >= 0) m.append(out).append(":a").append(i).append(',');
    }

    /* Android's standard layout in SDL's own terms, over SDL's numbering of this device */
    static String standardMapping(Device d) {
        StringBuilder m = new StringBuilder("a:b0,b:b1,x:b2,y:b3,back:b4,start:b6,leftstick:b7,rightstick:b8,"
                + "leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,");
        bindAxis(m, "leftx", d, MotionEvent.AXIS_X);
        bindAxis(m, "lefty", d, MotionEvent.AXIS_Y);
        bindAxis(m, "rightx", d, MotionEvent.AXIS_Z);
        bindAxis(m, "righty", d, MotionEvent.AXIS_RZ);
        bindAxis(m, "lefttrigger", d, MotionEvent.AXIS_LTRIGGER);
        bindAxis(m, "lefttrigger", d, MotionEvent.AXIS_BRAKE);
        m.append("lefttrigger:b15,");
        bindAxis(m, "righttrigger", d, MotionEvent.AXIS_RTRIGGER);
        bindAxis(m, "righttrigger", d, MotionEvent.AXIS_GAS);
        m.append("righttrigger:b16,");
        return m.toString();
    }

    /* what the device gets: a layout (db or standard), or none - the resting guess - with the reason */
    static final class Pick {
        final Layout layout;
        final int source;
        final String line;                   /* the report's words */

        Pick(Layout layout, int source, String line) {
            this.layout = layout;
            this.source = source;
            this.line = line;
        }
    }

    Pick choose(Device d) {
        String[] how = new String[1];
        Entry e = find(d, how);
        if (e != null)
            return new Pick(new Layout(d, e.mapping, MAX_BUTTONS_SDL), SOURCE_DB,
                    "db \"" + e.name + "\" (" + how[0] + ") guid " + d.guidHex());
        String why = standardRefusal(d);
        String miss = sdlKnows(d) ? " (SDL has lines for this pad, none for its axes)" : " not in the database";
        if (why == null)
            return new Pick(new Layout(d, standardMapping(d), MAX_BUTTONS_ALL), SOURCE_STANDARD,
                    "android standard, guid " + d.guidHex() + miss + " (" + size() + " entries)");
        return new Pick(null, SOURCE_GUESSED, "guessed: " + why + ", guid " + d.guidHex() + miss);
    }

    /* the joystick's button count: SDL's own for a database layout, every button for Android's standard */
    private static final int MAX_BUTTONS_SDL = -1, MAX_BUTTONS_ALL = MAX_BUTTONS;

    /* ---------- a layout at work ---------- */

    private static final int OUT_BUTTON = 0, OUT_AXIS = 1, IN_AXIS = 0, IN_BUTTON = 1, IN_HAT = 2;
    /* SDL's gamepad axes */
    private static final int LEFTX = 0, LEFTY = 1, RIGHTX = 2, RIGHTY = 3, LTRIG = 4, RTRIG = 5;
    private static final String[] AXIS_NAMES = { "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger" };
    /* SDL's gamepad buttons that are in our model, and where they go */
    private static final String[] BUTTON_NAMES = { "a", "b", "x", "y", "back", "start", "leftstick", "rightstick",
            "leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright" };
    private static final int[] BUTTON_BITS = { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_OPTIONS, BTN_MENU, BTN_L3, BTN_R3,
            BTN_L1, BTN_R1, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT };
    private static final int AXIS_MIN = -32768, AXIS_MAX = 32767;

    private static final class Binding {
        int outKind, out, outMin, outMax;
        int inKind, in, inMin, inMax, hatMask;

        boolean same(Binding o) {
            return outKind == o.outKind && out == o.out && outMin == o.outMin && outMax == o.outMax && inKind == o.inKind
                    && in == o.in && inMin == o.inMin && inMax == o.inMax && hatMask == o.hatMask;
        }
    }

    static final class Layout {
        final Device dev;
        final String mapping;
        private final Binding[] binds;
        private final int nbuttons;
        private final short[] axis;                  /* SDL's joystick axes */
        private final boolean[] button = new boolean[MAX_BUTTONS];
        private int dpadState;                       /* Android_OnHat: the hat's d-pad buttons */
        final float leftFlat, rightFlat;

        Layout(Device dev, String mapping, int nbuttons) {
            this.dev = dev;
            this.mapping = mapping;
            this.nbuttons = nbuttons < 0 ? dev.nbuttons : nbuttons;
            axis = new short[dev.axes.length];
            binds = parseBindings(mapping);
            float lf = 0f, rf = 0f;
            for (Binding b : binds) {
                if (b.outKind != OUT_AXIS || b.inKind != IN_AXIS || b.in >= dev.axes.length) continue;
                if (b.out == LEFTX || b.out == LEFTY) lf = Math.max(lf, dev.flat[b.in]);
                if (b.out == RIGHTX || b.out == RIGHTY) rf = Math.max(rf, dev.flat[b.in]);
            }
            leftFlat = lf;
            rightFlat = rf;
            rest();
        }

        /* SDL_PrivateParseGamepadConfigString + SDL_PrivateParseGamepadElement */
        private static Binding[] parseBindings(String mapping) {
            java.util.ArrayList<Binding> out = new java.util.ArrayList<>();
            for (String part : mapping.replace(" ", "").split(",")) {
                int colon = part.indexOf(':');
                if (colon <= 0 || colon == part.length() - 1) continue;
                String game = part.substring(0, colon), joy = part.substring(colon + 1);
                Binding b = new Binding();
                char half = 0;
                if (game.charAt(0) == '+' || game.charAt(0) == '-') { half = game.charAt(0); game = game.substring(1); }
                int ax = -1, bt = -1;
                for (int i = 0; i < AXIS_NAMES.length; i++) if (AXIS_NAMES[i].equalsIgnoreCase(game)) ax = i;
                for (int i = 0; i < BUTTON_NAMES.length; i++) if (BUTTON_NAMES[i].equalsIgnoreCase(game)) bt = i;
                if (ax >= 0) {
                    b.outKind = OUT_AXIS;
                    b.out = ax;
                    if (ax == LTRIG || ax == RTRIG) { b.outMin = 0; b.outMax = AXIS_MAX; }
                    else if (half == '+') { b.outMin = 0; b.outMax = AXIS_MAX; }
                    else if (half == '-') { b.outMin = 0; b.outMax = AXIS_MIN; }
                    else { b.outMin = AXIS_MIN; b.outMax = AXIS_MAX; }
                } else if (bt >= 0) {
                    b.outKind = OUT_BUTTON;
                    b.out = BUTTON_BITS[bt];
                } else {
                    continue;                        /* guide, misc, paddles, touchpad, sdk>=, crc: not ours */
                }
                char halfIn = 0;
                if (joy.charAt(0) == '+' || joy.charAt(0) == '-') { halfIn = joy.charAt(0); joy = joy.substring(1); }
                if (joy.isEmpty()) continue;
                boolean invert = joy.charAt(joy.length() - 1) == '~';
                if (joy.length() >= 2 && joy.charAt(0) == 'a' && Character.isDigit(joy.charAt(1))) {
                    b.inKind = IN_AXIS;
                    b.in = leadingInt(joy, 1);
                    if (halfIn == '+') { b.inMin = 0; b.inMax = AXIS_MAX; }
                    else if (halfIn == '-') { b.inMin = 0; b.inMax = AXIS_MIN; }
                    else { b.inMin = AXIS_MIN; b.inMax = AXIS_MAX; }
                    if (invert) { int t = b.inMin; b.inMin = b.inMax; b.inMax = t; }
                } else if (joy.length() >= 2 && joy.charAt(0) == 'b' && Character.isDigit(joy.charAt(1))) {
                    b.inKind = IN_BUTTON;
                    b.in = leadingInt(joy, 1);
                } else if (joy.length() >= 4 && joy.charAt(0) == 'h' && Character.isDigit(joy.charAt(1))
                        && joy.charAt(2) == '.' && Character.isDigit(joy.charAt(3))) {
                    b.inKind = IN_HAT;
                    b.in = leadingInt(joy, 1);
                    b.hatMask = leadingInt(joy, 3);
                } else {
                    continue;
                }
                boolean dup = false;
                for (Binding o : out) if (o.same(b)) dup = true;
                if (!dup) out.add(b);
            }
            return out.toArray(new Binding[0]);
        }

        private static int leadingInt(String s, int from) {
            int v = 0;
            for (int i = from; i < s.length() && Character.isDigit(s.charAt(i)); i++) v = v * 10 + (s.charAt(i) - '0');
            return v;
        }

        /* ---- the joystick, fed ---- */

        /* a key: true when it is one of SDL's buttons (Android_OnPadDown answers 0 then) */
        boolean key(int keycode, int scancode, boolean down) {
            int b = sdlButton(keycode, scancode);
            if (b < 0) return false;
            if (b < nbuttons) button[b] = down;      /* SDL_SendJoystickButton ignores one past nbuttons */
            return true;
        }

        /* one motion event: raw[i] is the value of dev.axes[i]; the hat's two values */
        void motion(float[] raw, float hatX, float hatY) {
            for (int i = 0; i < axis.length; i++) {
                float range = dev.max[i] - dev.min[i];
                float v = range > 0f ? (raw[i] - dev.min[i]) / range * 2.0f - 1.0f : 0f;
                axis[i] = toShort(32767.0 * v);
            }
            if (dev.hats) hat(Math.round(hatX), Math.round(hatY));
        }

        private static short toShort(double v) {
            if (v >= AXIS_MAX) return (short) AXIS_MAX;
            if (v <= AXIS_MIN) return (short) AXIS_MIN;
            return (short) (int) v;                  /* (Sint16) truncates towards nought */
        }

        /* Android_OnHat */
        private void hat(int x, int y) {
            if (x < -1 || x > 1 || y < -1 || y > 1) return;
            int s = 0;
            if (x < 0) s |= 1 << SDL_DPAD_LEFT;
            else if (x > 0) s |= 1 << SDL_DPAD_RIGHT;
            if (y < 0) s |= 1 << SDL_DPAD_UP;
            else if (y > 0) s |= 1 << SDL_DPAD_DOWN;
            int delta = s ^ dpadState;
            for (int b = SDL_DPAD_UP; b <= SDL_DPAD_RIGHT; b++)
                if ((delta & (1 << b)) != 0 && b < nbuttons) button[b] = (s & (1 << b)) != 0;
            dpadState = s;
        }

        /* every axis at its rest: a trigger axis at its minimum, any other in the middle; the d-pad let go */
        void rest() {
            for (int i = 0; i < axis.length; i++) {
                int a = dev.axes[i];
                boolean trigger = a == MotionEvent.AXIS_LTRIGGER || a == MotionEvent.AXIS_RTRIGGER
                        || a == MotionEvent.AXIS_BRAKE || a == MotionEvent.AXIS_GAS;
                float range = dev.max[i] - dev.min[i];
                float v = trigger ? -1f : range > 0f ? (0f - dev.min[i]) / range * 2.0f - 1.0f : 0f;
                axis[i] = toShort(32767.0 * Math.max(-1f, Math.min(1f, v)));
            }
            hat(0, 0);
            for (int b = SDL_DPAD_UP; b <= SDL_DPAD_RIGHT; b++) button[b] = false;
        }

        /* everything let go: a pause, a lost focus */
        void release() {
            java.util.Arrays.fill(button, false);
            dpadState = 0;
            rest();
        }

        /* ---- the gamepad, read: SDL_GetGamepadButton / SDL_GetGamepadAxis ---- */

        private int joyAxis(int i) {
            return i >= 0 && i < axis.length ? axis[i] : 0;
        }

        private boolean pressed(Binding b) {
            switch (b.inKind) {
                case IN_AXIS: {
                    int value = joyAxis(b.in);
                    int threshold = b.inMin + (b.inMax - b.inMin) / 2;
                    if (b.inMin < b.inMax) return value >= b.inMin && value <= b.inMax && value >= threshold;
                    return value >= b.inMax && value <= b.inMin && value <= threshold;
                }
                case IN_BUTTON:
                    return b.in >= 0 && b.in < nbuttons && button[b.in];
                default:
                    return false;                    /* hats: SDL on Android has none (Android_AddJoystick nhats = 0) */
            }
        }

        private int sdlAxis(int which) {
            for (Binding b : binds) {
                if (b.outKind != OUT_AXIS || b.out != which) continue;
                int value = 0;
                if (b.inKind == IN_AXIS) {
                    if (b.in < 0 || b.in >= axis.length) continue;      /* SDL3: a missing axis is skipped */
                    value = axis[b.in];
                    boolean valid = b.inMin < b.inMax ? value >= b.inMin && value <= b.inMax
                                                      : value >= b.inMax && value <= b.inMin;
                    if (valid) {
                        if (b.inMin != b.outMin || b.inMax != b.outMax) {
                            float n = (float) (value - b.inMin) / (b.inMax - b.inMin);
                            value = b.outMin + (int) (n * (b.outMax - b.outMin));
                        }
                    } else {
                        value = 0;
                    }
                } else if (b.inKind == IN_BUTTON) {
                    if (b.in >= 0 && b.in < nbuttons && button[b.in]) value = b.outMax;
                }
                boolean validOut = b.outMin < b.outMax ? value >= b.outMin && value <= b.outMax
                                                       : value >= b.outMax && value <= b.outMin;
                if (value != 0 && validOut) return value;
            }
            return 0;
        }

        /*
         * The pad in our model: the bits returned, the axes written - sticks
         * -1..1 with up positive (SDL's Y grows downwards, GameController's
         * upwards), the device's own flat cut off radially as for every pad
         * (raw: before the cut), triggers 0..1, the d-pad's axes from its
         * buttons.
         */
        int read(float[] out, float[] raw) {
            int bits = 0;
            for (Binding b : binds) if (b.outKind == OUT_BUTTON && pressed(b)) bits |= 1 << b.out;
            stick(sdlAxis(LEFTX), sdlAxis(LEFTY), leftFlat, out, raw, AX_LEFT_X);
            stick(sdlAxis(RIGHTX), sdlAxis(RIGHTY), rightFlat, out, raw, AX_RIGHT_X);
            out[AX_L2] = raw[AX_L2] = clamp01(sdlAxis(LTRIG) / 32767f);
            out[AX_R2] = raw[AX_R2] = clamp01(sdlAxis(RTRIG) / 32767f);
            out[AX_HAT_X] = raw[AX_HAT_X] = ((bits >> BTN_RIGHT) & 1) - ((bits >> BTN_LEFT) & 1);
            out[AX_HAT_Y] = raw[AX_HAT_Y] = ((bits >> BTN_UP) & 1) - ((bits >> BTN_DOWN) & 1);
            return bits;
        }

        private static float clamp01(float v) { return Math.max(0f, Math.min(1f, v)); }

        private static void stick(int sx, int sy, float flat, float[] out, float[] raw, int at) {
            float x = Math.max(-1f, Math.min(1f, sx / 32767f));
            float y = Math.max(-1f, Math.min(1f, -sy / 32767f));
            raw[at] = x;
            raw[at + 1] = y;
            cut(x, y, flat, out, at);
        }
    }

    /* a stick's flat, cut off radially (U5): nought inside, (r - flat) / (1 - flat) outside */
    static void cut(float x, float y, float flat, float[] out, int at) {
        float r = (float) Math.hypot(x, y);
        if (r <= flat || r <= 0f) {
            out[at] = out[at + 1] = 0f;
        } else {
            float k = (r - flat) / (1f - flat) / r;
            out[at] = x * k;
            out[at + 1] = y * k;
        }
    }
}
