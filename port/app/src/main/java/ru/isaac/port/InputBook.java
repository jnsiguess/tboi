package ru.isaac.port;

import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;

/*
 * ONE BOOK OF THE INPUT DEVICES (batch 2, U4, 01.10.2026).
 *
 * Who is who used to be decided three times over, and each time from a
 * different hint: isGamepad() by the source of every single event, padDevice()
 * by the device, and the keyboard's branch by what was left. A keyboard that
 * also claims a gamepad source got a pad slot, a TV remote's arrows became a
 * pad's cross, and a mouse click and a right click were the same finger.
 *
 * Now a device is judged ONCE, by what it is - at startup and when the system
 * says it came, changed or went - and every event asks the book by its device
 * id. A miss (the events of a new device arrive before onInputDeviceAdded)
 * asks InputDevice.getDevice(id) at once. The order of the decision:
 *
 *   1. VIRTUAL   isVirtual(): not a device for the game; text entry only
 *   2. SCREEN    the touch screen
 *   3. POINTER   a mouse, a touchpad (unless it is a pad's own - see pointer())
 *   4. PAD       joystick or gamepad class AND a pad's organs (a stick axis
 *                under SOURCE_JOYSTICK, two gamepad buttons, or a pad SDL
 *                knows) - see WHAT MAKES A PAD below
 *   5. KEYBOARD  an alphabetic keyboard without a pad's organs, even when it
 *                claims a gamepad source
 *   6. SYSTEM    everything else (the body's keys, a headset): the system's
 *   7. HYBRID    a pad that Android also calls a typing keyboard (the
 *                handhelds' built-in pads, a keyboard with a stick): its pad
 *                keys to the pad when it has gamepad buttons, the other keys
 *                to the keyboard
 *
 * The book also keeps the self-description the report carries: a passport of
 * every device as it is seen (and a line when it goes), and counters by device
 * once a minute and when the app is left.
 */
final class InputBook {
    static final int NONE = 0, VIRTUAL = 1, SCREEN = 2, POINTER = 3, PAD = 4, KEYBOARD = 5, SYSTEM = 6, HYBRID = 7;

    private static final String[] NAMES = { "none", "virtual", "screen", "pointer", "pad", "keyboard", "system", "hybrid" };

    static String name(int kind) {
        return kind >= 0 && kind < NAMES.length ? NAMES[kind] : "?";
    }

    /* the report, or nothing in a build without one */
    interface Say { void say(String line); }

    private static boolean full(int sources, int source) {
        return (sources & source) == source;
    }

    private static boolean padClass(int sources) {
        return full(sources, InputDevice.SOURCE_GAMEPAD) || full(sources, InputDevice.SOURCE_JOYSTICK);
    }

    /*
     * WHAT MAKES A PAD (06.10.2026, after the handhelds' complaint on 1.71).
     *
     * The rule is built only from what Android itself states about the
     * device, never from a number of ours:
     *
     *   1. a pad's SOURCE: the device claims SOURCE_GAMEPAD or SOURCE_JOYSTICK;
     *   2. and a pad's ORGANS, at least one of:
     *      - a STICK AXIS that Android itself files under SOURCE_JOYSTICK
     *        (the MotionRange's own source): X, Y, Z, RZ, RX, RY or the hat.
     *        Android centres every one of them to -1..1 (JoystickInputMapper,
     *        isCenteredAxis). A pointer's axes (a mouse, a touchpad, a
     *        keyboard's own pointer) come under SOURCE_MOUSE or
     *        SOURCE_TOUCHPAD and are never counted, whatever their name;
     *      - GAMEPAD BUTTONS the device has (hasKeys): two or more of A, B, X,
     *        Y, L1, R1, L2, R2 and the stick presses - or four numbered ones
     *        (BUTTON_1..16, a generic HID pad) on a device that is not a
     *        typing keyboard (1.74, see Facts.padButtons). One stray key is not a pad;
     *      - a vendor:product SDL's own gamepad database has a line for.
     *   3. A TYPING KEYBOARD (KEYBOARD_TYPE_ALPHABETIC) neither makes a pad nor
     *      unmakes one. Android calls a device alphabetic when its driver
     *      merely declares a Q key, and the built-in pads of handhelds
     *      (Retroid, AYN, Anbernic...) do - which is why their owners have to
     *      switch on "show the on-screen keyboard while a physical one is
     *      active". Such a device with a pad's organs is a HYBRID: its pad keys
     *      (the gamepad buttons, the d-pad, Back/Menu, Start/Select - SDL's
     *      list, PadDb.sdlButton) go to its pad when it has gamepad buttons,
     *      the rest (letters, digits, Enter...) to the keyboard.
     *
     * The 1.7 rule (batch 2, U4, and the 05.10 "stick" test) asked the
     * keyboard type FIRST: a typing pad without an X axis of its own became a
     * KEYBOARD (every button sent to the game's keyboard device, A/B/X/Y lost),
     * and one with an X axis a HYBRID whose d-pad, Back and Menu - not
     * "gamepad buttons" to KeyEvent.isGamepadButton - went to the keyboard as
     * arrows (the game's keyboard shoots with them) and Escape. 1.666 had
     * sent every key of such a device to its pad (by the event's GAMEPAD
     * source) - so the handhelds worked there and broke with 1.7.
     *
     * The owner's "BT5.2 Keyboard" (04e8:7021, report 03.10.2026, sources
     * 0x01002313): it claims a joystick source, but its only joystick-class
     * axis is GENERIC_1 and its X/Y are POINTER axes under SOURCE_MOUSE,
     * 0..3119 x 0..1439 - the owner's screen, 3120 x 1440: an absolute
     * pointer its HID descriptor declares (the owner: it has no touchpad),
     * not a stick. Of the gamepad buttons at most one bit of its SDL
     * mask (bit 0 is A or DPAD_CENTER, which a keyboard has). No stick axis,
     * fewer than two gamepad buttons: not a pad - a pointer whose keys are a
     * keyboard's, as the 05.10 fix wanted. On iOS a keyboard is a GCKeyboard
     * and never a controller.
     */
    static final int[] PAD_BUTTONS = {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2,
        KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR,
    };
    private static final String[] PAD_BUTTON_NAMES = { "A", "B", "X", "Y", "L1", "R1", "L2", "R2", "L3", "R3" };
    static final int[] STICK_AXES = {
        MotionEvent.AXIS_X, MotionEvent.AXIS_Y, MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
        MotionEvent.AXIS_RX, MotionEvent.AXIS_RY, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y,
    };
    private static final int NAMED_NEEDED = 2, NUMBERED_NEEDED = 4;

    /* what Android says about one device, in plain values: the whole input of the rule (the desk builds it itself) */
    static final class Facts {
        final String name;
        final int vendor, product, sources, keyboardType;
        final boolean virtual;
        final int[] axis, axisSource;         /* every motion range: its axis and its own source */
        final float[] min, max;
        final boolean[] named;                /* hasKeys over PAD_BUTTONS */
        final int numbered;                   /* how many of BUTTON_1..16 hasKeys answers */

        Facts(String name, int vendor, int product, int sources, int keyboardType, boolean virtual,
              int[] axis, int[] axisSource, float[] min, float[] max, boolean[] named, int numbered) {
            this.name = name == null ? "" : name;
            this.vendor = vendor & 0xffff;
            this.product = product & 0xffff;
            this.sources = sources;
            this.keyboardType = keyboardType;
            this.virtual = virtual;
            this.axis = axis;
            this.axisSource = axisSource;
            this.min = min;
            this.max = max;
            this.named = named;
            this.numbered = numbered;
        }

        static Facts of(InputDevice dev) {
            java.util.List<InputDevice.MotionRange> rs = dev.getMotionRanges();
            int n = rs.size();
            int[] axis = new int[n], src = new int[n];
            float[] lo = new float[n], hi = new float[n];
            for (int i = 0; i < n; i++) {
                InputDevice.MotionRange r = rs.get(i);
                axis[i] = r.getAxis();
                src[i] = r.getSource();
                lo[i] = r.getMin();
                hi[i] = r.getMax();
            }
            boolean[] named;
            int numbered = 0;
            try {
                named = dev.hasKeys(PAD_BUTTONS);
                int[] nums = new int[16];
                for (int i = 0; i < 16; i++) nums[i] = KeyEvent.KEYCODE_BUTTON_1 + i;
                for (boolean b : dev.hasKeys(nums)) if (b) numbered++;
            } catch (RuntimeException e) {
                named = new boolean[PAD_BUTTONS.length];
            }
            return new Facts(dev.getName(), dev.getVendorId(), dev.getProductId(), dev.getSources(),
                    dev.getKeyboardType(), dev.isVirtual(), axis, src, lo, hi, named, numbered);
        }

        /* the stick axes Android files under SOURCE_JOYSTICK, centred, by name */
        String stickAxes() {
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < axis.length; i++) {
                boolean stickAxis = false;
                for (int a : STICK_AXES) if (a == axis[i]) stickAxis = true;
                if (stickAxis && full(axisSource[i], InputDevice.SOURCE_JOYSTICK) && min[i] < 0f && max[i] > 0f)
                    s.append(s.length() > 0 ? "," : "").append(axisName(axis[i]));
            }
            return s.toString();
        }

        int namedCount() {
            int k = 0;
            if (named != null) for (boolean b : named) if (b) k++;
            return k;
        }

        String namedList() {
            StringBuilder s = new StringBuilder();
            for (int i = 0; named != null && i < named.length && i < PAD_BUTTON_NAMES.length; i++)
                if (named[i]) s.append(s.length() > 0 ? "," : "").append(PAD_BUTTON_NAMES[i]);
            return s.length() > 0 ? s.toString() : "-";
        }

        /*
         * The device's keys are a pad's: it has gamepad buttons. NUMBERED ones
         * (BUTTON_1..16) count only on a device that is not a typing keyboard
         * (1.74, the research notes of 11.10.2026, keyboard): they are HID "Button" usages
         * with no gamepad meaning - the kernel files the ones outside a mouse
         * collection as BTN_MISC / BTN_JOYSTICK, Generic.kl names them
         * BUTTON_1..16, and Android takes such buttons beside any absolute axis
         * for a JOYSTICK. A generic HID pad (a Joy-Con without hid-nintendo) has
         * nothing else; a Bluetooth keyboard's descriptor carries them beside its
         * keys (the owner's BT5.2: BUTTON_1 and a GENERIC_1 axis, and so a
         * joystick source). Four of them made such a keyboard a hybrid with a pad
         * slot and its arrows the phantom pad's d-pad. The handhelds' typing pads
         * that the hybrid is for have NAMED buttons (A, B, X, Y...), a stick or
         * an SDL line - all of which still count.
         */
        boolean padButtons() {
            boolean typing = keyboardType == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
            return namedCount() >= NAMED_NEEDED || (!typing && numbered >= NUMBERED_NEEDED);
        }
    }

    /* the verdict: the kind, whether its keys are a pad's (a hybrid's split), and why - the report's words */
    static final class Verdict {
        final int kind;
        final boolean padKeys;
        final String why;

        Verdict(int kind, boolean padKeys, String why) {
            this.kind = kind;
            this.padKeys = padKeys;
            this.why = why;
        }
    }

    /*
     * A pointer: a mouse, a touchpad, a relative mouse. A pad that carries a
     * touchpad in the same device (some Bluetooth pads do) stays a pad: its
     * sticks and buttons matter more than a click.
     */
    private static boolean pointer(int sources) {
        return full(sources, InputDevice.SOURCE_MOUSE) || full(sources, InputDevice.SOURCE_TOUCHPAD)
            || full(sources, InputDevice.SOURCE_MOUSE_RELATIVE);
    }

    /* the rule itself: pure, any thread, no Android call (the desk runs it) */
    static Verdict judge(Facts f) {
        if (f.virtual) return new Verdict(VIRTUAL, false, "virtual");
        int src = f.sources;
        if (full(src, InputDevice.SOURCE_TOUCHSCREEN)) return new Verdict(SCREEN, false, "touch screen");
        String sticks = f.stickAxes();
        boolean buttons = f.padButtons();
        boolean sdl = PadDb.shared().sdlKnows(f.vendor, f.product);
        boolean organs = sticks.length() > 0 || buttons || sdl;
        boolean pad = padClass(src) && organs;
        boolean alpha = f.keyboardType == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
        String why = "pad source " + (padClass(src) ? "yes" : "no")
                + ", stick axes " + (sticks.length() > 0 ? sticks : "-")
                + ", pad buttons " + f.namedList()
                + (f.numbered > 0 ? " +" + f.numbered + " numbered" + (alpha ? " (a typing keyboard's: not counted)" : "") : "")
                + (sdl ? ", SDL knows its vendor:product" : "")
                + ", keyboard " + (alpha ? "alpha" : f.keyboardType == InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC ? "non-alpha" : "none");
        if (pointer(src) && !pad) return new Verdict(POINTER, false, why);
        if (pad && alpha) return new Verdict(HYBRID, buttons || sdl, why);
        if (pad) return new Verdict(PAD, true, why);
        if (alpha) return new Verdict(KEYBOARD, false, why);
        return new Verdict(SYSTEM, false, why);
    }

    static int kindOf(InputDevice dev) {
        if (dev == null) return NONE;
        return judge(Facts.of(dev)).kind;
    }

    /*
     * A hybrid's key, split: a pad key (SDL's list - the gamepad buttons, the
     * d-pad, Back/Menu, Start/Select, by key code or by the left Joy-Con's
     * scan code) goes to its pad when the device has gamepad buttons; any
     * other key, and every key of a typing keyboard that only has a stick,
     * to the keyboard.
     */
    static int hybridKey(boolean padKeys, int keycode, int scancode) {
        return padKeys && PadDb.sdlButton(keycode, scancode) >= 0 ? PAD : KEYBOARD;
    }

    private static String axisName(int a) {
        switch (a) {
            case MotionEvent.AXIS_X: return "X";
            case MotionEvent.AXIS_Y: return "Y";
            case MotionEvent.AXIS_Z: return "Z";
            case MotionEvent.AXIS_RZ: return "RZ";
            case MotionEvent.AXIS_RX: return "RX";
            case MotionEvent.AXIS_RY: return "RY";
            case MotionEvent.AXIS_HAT_X: return "HX";
            case MotionEvent.AXIS_HAT_Y: return "HY";
            case MotionEvent.AXIS_LTRIGGER: return "LT";
            case MotionEvent.AXIS_RTRIGGER: return "RT";
            case MotionEvent.AXIS_BRAKE: return "BRAKE";
            case MotionEvent.AXIS_GAS: return "GAS";
            case MotionEvent.AXIS_GENERIC_1: return "GEN1";
            case MotionEvent.AXIS_WHEEL: return "WHEEL";
            case MotionEvent.AXIS_PRESSURE: return "PRESSURE";
            case MotionEvent.AXIS_SIZE: return "SIZE";
            default: return "a" + a;
        }
    }

    /* a source word as names: the report's */
    static String sourceNames(int s) {
        int[] v = { InputDevice.SOURCE_KEYBOARD, InputDevice.SOURCE_DPAD, InputDevice.SOURCE_GAMEPAD,
                InputDevice.SOURCE_TOUCHSCREEN, InputDevice.SOURCE_MOUSE, InputDevice.SOURCE_STYLUS,
                InputDevice.SOURCE_TRACKBALL, InputDevice.SOURCE_MOUSE_RELATIVE, InputDevice.SOURCE_TOUCHPAD,
                InputDevice.SOURCE_TOUCH_NAVIGATION, InputDevice.SOURCE_ROTARY_ENCODER, InputDevice.SOURCE_JOYSTICK,
                InputDevice.SOURCE_HDMI, 0x04000000 /* SOURCE_SENSOR, API 31 */ };
        String[] n = { "keyboard", "dpad", "gamepad", "touchscreen", "mouse", "stylus", "trackball", "mouse-rel",
                "touchpad", "touchnav", "rotary", "joystick", "hdmi", "sensor" };
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < v.length; i++)
            if (full(s, v[i])) out.append(out.length() > 0 ? "+" : "").append(n[i]);
        return out.length() > 0 ? out.toString() : "none";
    }

    /* a range's own source, short: what decides whether an axis is a stick's */
    private static String rangeSource(int s) {
        if (full(s, InputDevice.SOURCE_JOYSTICK)) return "joy";
        if (full(s, InputDevice.SOURCE_TOUCHPAD)) return "touchpad";
        if (full(s, InputDevice.SOURCE_TOUCHSCREEN)) return "screen";
        if (full(s, InputDevice.SOURCE_MOUSE)) return "mouse";
        if (full(s, InputDevice.SOURCE_MOUSE_RELATIVE)) return "mouse-rel";
        return String.format(java.util.Locale.ROOT, "0x%x", s);
    }

    /* ---------- the book ---------- */

    private static final class Entry {
        int kind;
        boolean padKeys;                    /* a hybrid's pad keys go to its pad (Verdict.padKeys) */
        String name = "";
        boolean external = true;
        /* counters since the last time they were told */
        int keys, repeats, fallback, mismatched, axes, hat, fingers, mice, styli, palms, mouseBack, pointerButtons;
    }

    private final android.util.SparseArray<Entry> book = new android.util.SparseArray<>();
    private final Say say;
    private long countersTold;

    InputBook(Say say) {
        this.say = say;
    }

    private void tell(String line) {
        if (say != null) say.say(line);
    }

    /*
     * IDS ANDROID DOES NOT KNOW (hotfix 1.72): events injected by key-mapping
     * apps carry id 0 or -1, and getDevice answered null on every one of them -
     * a call into the input service per event. Such an id is asked about again
     * at most once a second; a device that really arrives is learnt at once
     * through added/changed, which forget the mark.
     */
    private final android.util.SparseLongArray unknownUntil = new android.util.SparseLongArray();
    private static final Entry UNKNOWN = new Entry();      /* kind NONE: what learn answers for a null device */

    private Entry entry(int id) {
        Entry e = book.get(id);
        if (e != null) return e;
        long now = android.os.SystemClock.uptimeMillis();
        if (now < unknownUntil.get(id, 0L)) { UNKNOWN.kind = NONE; return UNKNOWN; }
        InputDevice dev = InputDevice.getDevice(id);
        if (dev == null) {
            unknownUntil.put(id, now + 1000);
            Entry none = new Entry();
            none.kind = NONE;
            return none;
        }
        unknownUntil.delete(id);
        return learn(id, dev);
    }

    private Entry learn(int id, InputDevice dev) {
        Entry e = new Entry();
        if (dev == null) { e.kind = NONE; return e; }   /* not known to Android (yet): judged again next time */
        Facts f = Facts.of(dev);
        Verdict v = judge(f);
        e.kind = v.kind;
        e.padKeys = v.padKeys;
        e.name = dev.getName();
        if (android.os.Build.VERSION.SDK_INT >= 29) e.external = dev.isExternal();
        book.put(id, e);
        if (say != null) tell(passport(id, dev, f, v));
        return e;
    }

    /* the kind of a device by its id */
    int kindOfId(int id) {
        return entry(id).kind;
    }

    /* is the device built in (a gaming phone's own "pad"); false when Android cannot say */
    boolean builtIn(int id) {
        return !entry(id).external;
    }

    /*
     * The kind an EVENT belongs to: its device's, except that a hybrid splits
     * its keys - pad buttons to the pad, the rest to the keyboard - and a key
     * of a pointer or of a screen that types is the keyboard's.
     */
    int kindOfEvent(InputEvent ev) {
        int kind = kindOfId(ev.getDeviceId());
        if (ev instanceof KeyEvent) {
            int code = ((KeyEvent) ev).getKeyCode();
            if (kind == HYBRID) return hybridKey(entry(ev.getDeviceId()).padKeys, code, ((KeyEvent) ev).getScanCode());
            if (kind == POINTER || kind == SCREEN || kind == SYSTEM) {
                InputDevice dev = ev.getDevice();
                if (dev != null && dev.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC) return KEYBOARD;
            }
            if (kind == NONE) {
                int src = ev.getSource();
                if (padClass(src)) return PAD;
                return (src & InputDevice.SOURCE_KEYBOARD) != 0 ? KEYBOARD : SYSTEM;
            }
            return kind;
        }
        if (kind == HYBRID) return padClass(ev.getSource()) ? PAD : KEYBOARD;
        if (kind == NONE) {
            int src = ev.getSource();
            if (full(src, InputDevice.SOURCE_TOUCHSCREEN)) return SCREEN;
            if (padClass(src)) return PAD;
            if (pointer(src)) return POINTER;
        }
        return kind;
    }

    boolean padEvent(InputEvent ev) {
        return kindOfEvent(ev) == PAD;
    }

    /*
     * A key, counted; the answer says it must be DROPPED: a FLAG_FALLBACK key
     * of a pad or a keyboard is the system's own translation of a key one of
     * ours already had (Generic.kcm: the pad's B as BACK, a stick as the
     * arrows) - the device's real key was handled or refused already, the
     * copy is never the game's (U5).
     */
    boolean keyEvent(KeyEvent ev) {
        Entry e = entry(ev.getDeviceId());
        int kind = kindOfEvent(ev);
        boolean down = ev.getAction() == KeyEvent.ACTION_DOWN;
        boolean fallback = (ev.getFlags() & KeyEvent.FLAG_FALLBACK) != 0;
        if (fallback) e.fallback++;
        else if (down && ev.getRepeatCount() > 0) e.repeats++;
        else if (down) e.keys++;
        if (kind == PAD && !padClass(ev.getSource()) && !KeyEvent.isGamepadButton(ev.getKeyCode())) e.mismatched++;
        if (ev.getKeyCode() == KeyEvent.KEYCODE_BACK && (ev.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE)
            e.mouseBack++;
        countersMaybe();
        return fallback && (kind == PAD || kind == KEYBOARD || e.kind == HYBRID);
    }

    /* a pad's motion, counted */
    void padMotion(MotionEvent ev) {
        Entry e = entry(ev.getDeviceId());
        e.axes++;
        if (ev.getAxisValue(MotionEvent.AXIS_HAT_X) != 0f || ev.getAxisValue(MotionEvent.AXIS_HAT_Y) != 0f) e.hat++;
        countersMaybe();
    }

    /* a pointer's button, taken from the system (U6), counted */
    void pointerButton(MotionEvent ev) {
        entry(ev.getDeviceId()).pointerButtons++;
        countersMaybe();
    }

    /* a touch going down, counted by its tool; a palm, counted on its own */
    void touchDown(MotionEvent ev, int index) {
        Entry e = entry(ev.getDeviceId());
        switch (ev.getToolType(index)) {
            case MotionEvent.TOOL_TYPE_MOUSE: e.mice++; break;
            case MotionEvent.TOOL_TYPE_STYLUS:
            case MotionEvent.TOOL_TYPE_ERASER: e.styli++; break;
            default: e.fingers++; break;
        }
        countersMaybe();
    }

    void palm(MotionEvent ev) {
        entry(ev.getDeviceId()).palms++;
        countersMaybe();
    }

    /* ---------- the line-up ---------- */

    /* everything there is now: at startup and on the way back into the app */
    void scanAll() {
        for (int id : InputDevice.getDeviceIds()) if (book.get(id) == null) learn(id, InputDevice.getDevice(id));
    }

    void added(int id) {
        unknownUntil.delete(id);
        if (book.get(id) == null) learn(id, InputDevice.getDevice(id));
    }

    /* judged again; a new passport only when the kind has changed (some phones call this often) */
    void changed(int id) {
        unknownUntil.delete(id);
        Entry was = book.get(id);
        InputDevice dev = InputDevice.getDevice(id);
        if (was != null && dev != null) {
            Verdict v = judge(Facts.of(dev));
            if (v.kind == was.kind && v.padKeys == was.padKeys) return;
        }
        book.remove(id);
        Entry now = learn(id, dev);
        if (was != null) {
            now.keys = was.keys; now.repeats = was.repeats; now.fallback = was.fallback;
            now.mismatched = was.mismatched; now.axes = was.axes; now.hat = was.hat;
            now.fingers = was.fingers; now.mice = was.mice; now.styli = was.styli; now.palms = was.palms;
            now.mouseBack = was.mouseBack; now.pointerButtons = was.pointerButtons;
        }
    }

    void removed(int id, String slot) {
        Entry e = book.get(id);
        if (e == null) return;
        tellCounters(id, e);
        book.remove(id);
        tell("input: - id " + id + " (" + name(e.kind) + (slot != null ? ", " + slot : "") + ")");
    }

    /* the ids the book holds now: the line-up watcher walks them for the ones that went */
    int[] ids() {
        int[] out = new int[book.size()];
        for (int i = 0; i < out.length; i++) out[i] = book.keyAt(i);
        return out;
    }

    /* ---------- the report ---------- */

    /*
     * THE PASSPORT (report builds): everything the rule read, so a player's
     * report shows the cause - the name, vendor:product, the sources by name,
     * EVERY motion range with its own source (a pointer's X is "mouse" or
     * "touchpad", a stick's "joy"), the gamepad buttons it declares, the kind
     * with the rule's reasons, a hybrid's split, and for a pad its layout and
     * where that comes from (SDL's database, Android's standard, the guess).
     */
    static String passport(int id, InputDevice dev, Facts f, Verdict v) {
        int kind = v.kind;
        StringBuilder s = new StringBuilder(400);
        String desc = dev.getDescriptor();
        s.append(String.format(java.util.Locale.ROOT, "input: + id %d \"%s\" %04x:%04x desc %s sources 0x%08x kbd %s",
                id, dev.getName(), dev.getVendorId(), dev.getProductId(),
                desc == null ? "-" : desc.substring(0, Math.min(8, desc.length())), dev.getSources(),
                dev.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC ? "alpha"
                    : dev.getKeyboardType() == InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC ? "non-alpha" : "none"));
        s.append(" ext ").append(android.os.Build.VERSION.SDK_INT >= 29 ? (dev.isExternal() ? "yes" : "no") : "?");
        s.append(" virt ").append(dev.isVirtual() ? "yes" : "no");
        s.append(" ctl# ").append(dev.getControllerNumber());
        s.append(" [").append(sourceNames(dev.getSources())).append(']');
        s.append(" ranges");
        if (f.axis.length == 0) s.append(" -");
        for (int i = 0; i < f.axis.length; i++) {
            InputDevice.MotionRange r = dev.getMotionRange(f.axis[i], f.axisSource[i]);
            s.append(String.format(java.util.Locale.ROOT, " %s/%s(%.2f..%.2f flat %.3f)", axisName(f.axis[i]),
                    rangeSource(f.axisSource[i]), f.min[i], f.max[i], r != null ? r.getFlat() : 0f));
        }
        String motors;
        try {
            motors = android.os.Build.VERSION.SDK_INT >= 31
                ? String.valueOf(dev.getVibratorManager().getVibratorIds().length)
                : (dev.getVibrator() != null && dev.getVibrator().hasVibrator() ? "1" : "0");
        } catch (RuntimeException e) {
            motors = "?";
        }
        s.append(" motors ").append(motors);
        s.append(" -> ").append(name(kind).toUpperCase(java.util.Locale.ROOT)).append(" (").append(v.why).append(')');
        if (kind == HYBRID)
            s.append(v.padKeys ? " split: pad keys (buttons, d-pad, Back/Menu) to the pad, the rest to the keyboard"
                               : " split: every key to the keyboard, the stick to the pad");
        int joy = JoyCons.side(f.vendor, f.product, f.name);
        if (joy != JoyCons.NONE)
            s.append(" | Joy-Con ").append(JoyCons.sideName(joy))
             .append(" half: joined with the other half into one pad while both are here");
        /* a pad's layout and where it comes from (PadDb): "db <name>", "android standard" or "guessed: why" */
        if (kind == PAD || kind == HYBRID) {
            try {
                PadDb.Device pd = PadDb.describe(dev);
                s.append(" layout ").append(PadDb.shared().choose(pd).line)
                 .append(String.format(java.util.Locale.ROOT, " (SDL: %d buttons, mask %08x/%04x, axes",
                         pd.nbuttons, pd.buttonMask, pd.axisMask));
                for (int i = 0; i < pd.axes.length; i++)
                    s.append(" a").append(i).append('=').append(MotionEvent.axisToString(pd.axes[i]).replace("AXIS_", ""));
                s.append(pd.hats ? ", hat)" : ")");
            } catch (RuntimeException e) {
                s.append(" layout ? (").append(e).append(')');
            }
        }
        return s.toString();
    }

    private void tellCounters(int id, Entry e) {
        if (say == null) return;
        if (e.keys + e.repeats + e.fallback + e.mismatched + e.axes + e.hat + e.fingers + e.mice + e.styli
                + e.palms + e.mouseBack + e.pointerButtons == 0) return;
        tell(String.format(java.util.Locale.ROOT,
                "input: counters id %d %s keys %d repeats %d fallback %d mismatched %d axes %d hat %d"
                + " touches finger %d mouse %d stylus %d palm %d pointer-buttons %d back-from-mouse %d",
                id, name(e.kind), e.keys, e.repeats, e.fallback, e.mismatched, e.axes, e.hat,
                e.fingers, e.mice, e.styli, e.palms, e.pointerButtons, e.mouseBack));
        e.keys = e.repeats = e.fallback = e.mismatched = e.axes = e.hat = 0;
        e.fingers = e.mice = e.styli = e.palms = e.mouseBack = e.pointerButtons = 0;
    }

    /* the counters of every device: once a minute from the events, and when the app is left */
    void tellCounters() {
        countersTold = android.os.SystemClock.uptimeMillis();
        for (int i = 0; i < book.size(); i++) tellCounters(book.keyAt(i), book.valueAt(i));
    }

    private void countersMaybe() {
        if (say == null) return;
        long now = android.os.SystemClock.uptimeMillis();
        if (countersTold == 0) countersTold = now;
        else if (now - countersTold >= 60000) tellCounters();
    }
}
