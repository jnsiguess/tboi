/*
 * RUMBLE, given back (the owner's list, item 18, 23.09.2026), and made into
 * haptics (batch 3, P9-A/B/V, 01.10.2026).
 *
 * The game never lost its rumble - only the motor. Everything that asks for a
 * shake is still in the image and still gated by the setting RumbleEnabled,
 * the settings byte +0x55f12. Every ask goes to 0x8b70a4(input, device index,
 * seconds, strength), which finds the device and goes to 0x8a8aac: the two
 * numbers are kept at +0x5f8/+0x5fc, the clock at +0x600, and the device's own
 * virtual +0x128 is asked to set its motors. Once a frame 0x8a7ce0 takes the
 * time gone off +0x5f8 and asks +0x128 again with what is left, then with
 * zeros when it runs out; the game's pause (0x8a8afc) asks it for zeros and
 * keeps the rest for later.
 *
 * So the game does not say "play a hit", it says "hold the motor at S for T
 * more seconds" - the language of a pad's eccentric motor. Handed to Android
 * as it was, a flat one-shot of every state, that became a buzzer on the
 * phone's resonant motor, every call cutting the one before (the owner, 30.09:
 * "loose", decisions 3 and 12: not the PC's dull buzzing - modern haptics).
 * There was no iOS example to follow: the image has no UIImpactFeedback-
 * Generator, no CHHapticEngine, no AudioServices call. The root is the
 * translation at the platforms' border, and it is made here and in
 * Haptics.java:
 *
 *   WHO ASKED is told by the return address. The motor is reached by tail
 *   calls only (0x8b70a4 `b 0x8a8aac`, 0x8a8aac `br x1`), so the link register
 *   in motor() still points into the real caller. All five virtual calls
 *   through +0x128 in the image are known; four are these:
 *
 *     0x8a7d20, 0x8a7d68   the frame's continuation - NOT a new event
 *     0x8a8b40             the game's pause (zeros)
 *     0x8b706c             "stop them all" (zeros)
 *     0x8b7144             a new event for every device (index -1)
 *
 *   and the direct callers of 0x8b70a4 name the source:
 *
 *     0x891354             ShakeScreen, strength always 0.5, length frames/60
 *     0x50e800             damage to a player: clamp(n*0.2, 0.4, 2) s at
 *                          clamp(n*0.6, 0.6, 1.0)
 *     0x509e00             probably the white fire (entity 33.4, the Lost
 *                          curse given beside it): 0.6 s at 1.2
 *     0xad4e0 0x1af3e4 0x2f936c   the same damage in the AB/AB+ copies
 *     0x131bcc 0x405464 0x1674b0  the same ShakeScreen in the copies (the
 *                          AB+ tail call 0x167e68 has a foreign return
 *                          address and takes the fallback)
 *
 *   An address not in the tables takes the fallback: strength exactly 0.5 is
 *   a shake, anything else a hurt. Zeros from anywhere but the frame are a
 *   STOP; zeros from the frame are a natural end and ignored - our effect has
 *   its own shape and length.
 *
 *   WHICH DEVICE. A pad's device (DeviceGamepad) keeps the GCController it was
 *   made from at +0x608 (devices.c); pad.c's shim_pad_slot_of compares the
 *   pointer with its slots and never follows it - a stale object gives -1 and
 *   the shake is dropped, a dead device does not shake. The slot's Android
 *   device is the book's (devices.c, told by Java's padIdentity). The touch
 *   screen (DeviceiOS) is told as TARGET_SCREEN, the keyboard (DeviceKeyboard)
 *   as TARGET_KEYBOARD.
 *
 *   WHO FEELS IT (the owner, 03.10.2026). A pad with a motor shakes itself,
 *   the touch screen shakes the phone. A device with no motor of its own - a
 *   pad without one, a pad the book does not know, the keyboard (always an
 *   external one: keyToGame feeds it from physical keys only) - depends on
 *   the run: ALONE it falls back to the phone, as since 24.09 ("some
 *   vibration rather than none"); in CO-OP (two own players or more) it
 *   shakes nothing, for the phone is then in the hands of the player on the
 *   screen - his partner's hits used to land on it. Own players are counted
 *   by the game's own rule (coop.c own_player, 0x674160: J&E, T.Forgotten
 *   and his Soul are one) at the moment of the event, on the game thread,
 *   and the count goes to Java with it. Every Repentance ask is per player:
 *   the ten direct callers of 0x8b70a4 all pass the player's own device
 *   index (+0x19ec) - the "every device" index -1 is used only by the AB/AB+
 *   copies (0x1674b0, 0x167e6c). The rule itself is applied in one place,
 *   Haptics.routeOf, where the motors are known.
 *
 *   JAVA gets the meaning only - new events, a pause, a stop, one RESUME after
 *   a pause - through shim_haptic_java below, and Haptics.java owns every
 *   vibrator: the mixer, the classes, the capability tiers.
 *
 * The devices, by the image's own type names: KAGE::Input::DeviceGamepad
 * 0xa91a10 (one per GCController, 0x8ca698), DeviceiOS 0xa92d88 (the touch
 * screen, the game's device 0x10dead) and DeviceKeyboard 0xa94958 (the
 * keyboard device - batch 2, U4). All three carry the base 0x8a8b64, a bare
 * `ret`; on the PC the pad's driver overrides it. The port overrides all three
 * with motor() below. No call site is touched.
 *
 * THE SETTING. RUMBLE is a slider 0..10 (7 by default) - since hotfix 1.75 in
 * the game's own Options, in the row that was FILTER (loader.c
 * rumble_menu_item; sticks.c keeps its byte), before that on the Tweaks page:
 * port-touch.ini keeps `rumble` = 0/1 as it always had (old files still read
 * right) and `rumblepower` = 1..10 beside it; the slider shows 0 when rumble
 * is off, the power otherwise. The game's byte follows `rumble` - it is the
 * byte of two owners, and sticks.c alone writes it (shim_sticks_click_byte).
 * `rumbletier` = auto | primitives | clicks | waveform | onoff forces a
 * cheaper tier, so a cheap motor's feel can be tried on the owner's phone.
 */
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>
#include "diag.h"

#if ISAAC_DIAG
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "isaac", __VA_ARGS__)
#else
#define LOG(...) do { } while (0)
#endif

#define VT_GAMEPAD      0xa91a10        /* KAGE::Input::DeviceGamepad */
#define VT_IOS          0xa92d88        /* KAGE::Input::DeviceiOS, the touch screen */
#define VT_KEYBOARD     0xa94958        /* KAGE::Input::DeviceKeyboard, the keyboard device */
#define VT_MOTOR        0x128
#define BASE_MOTOR      0x8a8b64        /* ret */
#define DEVICE_CONTROLLER 0x608         /* a pad device's GCController (devices.c) */

/* who called, by the return address (checked word by word with tools/dis.py) */
#define SITE_FRAME_A    0x8a7d20        /* blr at 0x8a7d1c: the frame's continuation */
#define SITE_FRAME_B    0x8a7d68        /* blr at 0x8a7d64: the same */
#define SITE_PAUSE      0x8a8b40        /* blr at 0x8a8b3c: the game's pause, zeros */
#define SITE_CURSE      0x509e00        /* bl 0x8b70a4 at 0x509dfc: 0.6 s at 1.2 */

static const unsigned long HURT_SITES[] = { 0x50e800, 0xad4e0, 0x1af3e4, 0x2f936c };
static const unsigned long SHAKE_SITES[] = { 0x891354, 0x131bcc, 0x405464, 0x1674b0 };

/* what Java is told - Haptics.java has the same numbers */
#define SRC_SHAKE       1
#define SRC_HURT        2
#define SRC_CURSE       3
#define SRC_RESUME      4
#define SRC_PAUSE       5
#define SRC_STOP        6
#define TARGET_SCREEN   (-1)
#define TARGET_KEYBOARD (-2)
#define CTL_LEVEL       1               /* the slider, 0..10 */
#define CTL_TIER        2               /* 0 auto .. 4 onoff */
#define CTL_SAMPLE      4               /* one IMPACT at the new level; bit 1: the pads, bit 2: co-op */

unsigned long shim_image_base(void);
void shim_sticks_click_byte(int value);                              /* sticks.c */
int shim_pad_slot_of(const void *controller);                        /* pad.c */
int shim_device_slot_android(int slot);                              /* devices.c */
int shim_report_active(void);
void report_line(const char *kind, const char *text);
int shim_eid_readable(const void *p, unsigned long n);
int shim_eid_players(unsigned long *out, int max);                   /* eid.c */
int shim_coop_own_player(unsigned long p);                           /* coop.c: the game's rule 0x674160 */

static int g_on = 1;                    /* `rumble`: the game's byte follows it */
static int g_power = 7;                 /* `rumblepower`, 1..10 */
static int g_tier;                      /* `rumbletier`, 0 auto */

static const char *const TIER_NAME[] = { "auto", "primitives", "clicks", "waveform", "onoff" };
#define TIERS 5

/* ------------------------------------------------------- alone or co-op */
/*
 * Own players in the run now, by the game's rule (coop.c own_player): 0 in
 * the menus, 1 alone (J&E and T.Forgotten count as one), 2 and more co-op.
 * On the game thread only - motor() and between frames; the last count is
 * kept for the slider's sample, which comes from the options list.
 */
#define OWN_MAX 8
static volatile int g_owners;

static int own_players(void) {
    unsigned long p[OWN_MAX];
    int n = shim_eid_players(p, OWN_MAX), owners = 0;
    for (int i = 0; i < n; i++) owners += shim_coop_own_player(p[i]) ? 1 : 0;
    g_owners = owners;
    return owners;
}

/* ------------------------------------------------------------ the bridge */
/*
 * Haptics.java binds itself (Haptics.bind, on the UI thread before the game
 * thread exists); the layer then calls it from the game thread, which is a
 * Java thread, or from the UI thread while the settings are read. A thread
 * that is not attached is attached for the call.
 */
static JavaVM *g_vm;
static jobject g_haptics;
static jmethodID g_event;               /* Haptics.fromGame(target, android, source, strength, seconds, site, from, owners) */
static jmethodID g_control;             /* Haptics.fromLayer(what, value) */

static JNIEnv *env_now(int *attached) {
    JNIEnv *env = NULL;
    *attached = 0;
    if (!g_vm) return NULL;
    if ((*g_vm)->GetEnv(g_vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return NULL;
        *attached = 1;
    }
    return env;
}

static void control(int what, int value) {
    if (!g_haptics || !g_control) return;
    int attached;
    JNIEnv *env = env_now(&attached);
    if (!env) return;
    (*env)->CallVoidMethod(env, g_haptics, g_control, (jint) what, (jint) value);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

/* a shake's meaning, to the one owner of the vibrators */
void shim_haptic_java(int target, int android, int source, float strength, float seconds,
                      unsigned long site, unsigned long from, int owners) {
    if (!g_haptics || !g_event) {
        DIAG({ static int said; if (!said && shim_report_active()) { said = 1;
            report_line("haptics", "bridge missing: Haptics.fromGame was not bound - no vibration"); } });
        return;
    }
    int attached;
    JNIEnv *env = env_now(&attached);
    if (!env) return;
    (*env)->CallVoidMethod(env, g_haptics, g_event, (jint) target, (jint) android, (jint) source,
                           (jfloat) strength, (jfloat) seconds, (jint) site, (jint) from, (jint) owners);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

JNIEXPORT void JNICALL
Java_ru_isaac_port_Haptics_bind(JNIEnv *env, jobject thiz) {
    if (g_haptics) return;
    (*env)->GetJavaVM(env, &g_vm);
    jclass cls = (*env)->GetObjectClass(env, thiz);
    g_event = (*env)->GetMethodID(env, cls, "fromGame", "(IIIFFIII)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); g_event = 0; }
    g_control = (*env)->GetMethodID(env, cls, "fromLayer", "(II)V");
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); g_control = 0; }
    g_haptics = (*env)->NewGlobalRef(env, thiz);
    LOG("haptics: bound%s%s", g_event ? "" : " - fromGame MISSING", g_control ? "" : " - fromLayer MISSING");
    /* what the settings already say (they may have been read before the bind) */
    control(CTL_TIER, g_tier);
    control(CTL_LEVEL, g_on ? g_power : 0);
}

/* ----------------------------------------------------------- the setting */
int shim_rumble_get(void) { return g_on; }

void shim_rumble_set(int on) {
    g_on = on != 0;
    shim_sticks_click_byte(g_on);
    control(CTL_LEVEL, g_on ? g_power : 0);         /* 0 cancels every channel at once */
}

int shim_rumble_power_get(void) { return g_power; }

void shim_rumble_power_set(int n) {
    g_power = n < 1 ? 1 : (n > 10 ? 10 : n);
    if (g_on) control(CTL_LEVEL, g_power);
}

/* the slider: 0 when off, else the power */
int shim_rumble_level(void) { return g_on ? g_power : 0; }

/* the slider moved (the Options row, sticks.c): 0 turns it off and keeps the power; else one IMPACT at the new level */
void shim_rumble_level_set(int n, int by_pad) {
    n = n < 0 ? 0 : (n > 10 ? 10 : n);
    if (n > 0) g_power = n;
    shim_rumble_set(n > 0);
    if (n > 0) control(CTL_SAMPLE, (by_pad ? 1 : 0) | (g_owners >= 2 ? 2 : 0));
}

const char *shim_rumble_tier_get(void) { return TIER_NAME[g_tier]; }

void shim_rumble_tier_set(const char *name) {
    int t = 0;
    while (name && (*name == ' ' || *name == '\t')) name++;
    size_t n = name ? strcspn(name, " \t\r\n") : 0;
    for (int i = 0; i < TIERS; i++)
        if (n == strlen(TIER_NAME[i]) && !strncmp(name, TIER_NAME[i], n)) t = i;
    g_tier = t;
    control(CTL_TIER, g_tier);
}

/* --------------------------------------------------------------- the motor */
enum { ASK_NOTHING, ASK_NEW, ASK_PAUSE, ASK_STOP, ASK_RESUME };

static int in(const unsigned long *list, int n, unsigned long site) {
    for (int i = 0; i < n; i++) if (list[i] == site) return 1;
    return 0;
}

/*
 * A device the game paused (its motor asked for zeros at 0x8a8b40): its first
 * continuation after the pause is one RESUME. Keyed by the device object; a
 * handful of devices at most.
 */
#define PAUSED_MAX 8
static const void *g_paused[PAUSED_MAX];

static int paused_take(const void *device) {
    for (int i = 0; i < PAUSED_MAX; i++)
        if (g_paused[i] == device) { g_paused[i] = 0; return 1; }
    return 0;
}

static void paused_put(const void *device) {
    for (int i = 0; i < PAUSED_MAX; i++) if (g_paused[i] == device) return;
    for (int i = 0; i < PAUSED_MAX; i++) if (!g_paused[i]) { g_paused[i] = device; return; }
    g_paused[0] = device;
}

/* what a call means, from who made it and what it carries; *source for a new event */
static int meaning(unsigned long site, float left, float strength, int was_paused, int *source) {
    int zeros = left <= 0.0f || strength <= 0.0f;
    if (site == SITE_FRAME_A || site == SITE_FRAME_B) {
        if (!was_paused || zeros) return ASK_NOTHING;   /* the continuation; its end is ours to shape */
        *source = SRC_RESUME;
        return ASK_RESUME;
    }
    if (site == SITE_PAUSE) return ASK_PAUSE;
    if (zeros) return ASK_STOP;
    if (in(HURT_SITES, sizeof(HURT_SITES) / sizeof(HURT_SITES[0]), site)) *source = SRC_HURT;
    else if (site == SITE_CURSE) *source = SRC_CURSE;
    else if (in(SHAKE_SITES, sizeof(SHAKE_SITES) / sizeof(SHAKE_SITES[0]), site)) *source = SRC_SHAKE;
    else *source = strength == 0.5f ? SRC_SHAKE : SRC_HURT;     /* 0x8b7144 and anything unknown */
    return ASK_NEW;
}

/* the device's own +0x128: (device, seconds left, strength); zeros stop it */
static void motor(void *device, float left, float strength) {
    unsigned long lr = (unsigned long) __builtin_return_address(0);
    unsigned long b = shim_image_base();
    if (!b || !device) return;
    unsigned long site = lr - b;
    unsigned long vt = *(const unsigned long *) device - b;

    int source = 0;
    int was_paused = (site == SITE_FRAME_A || site == SITE_FRAME_B) ? paused_take(device) : 0;
    int ask = meaning(site, left, strength, was_paused, &source);
    if (ask == ASK_NOTHING) return;
    if (ask == ASK_PAUSE) { paused_put(device); source = SRC_PAUSE; }
    else if (ask == ASK_STOP) source = SRC_STOP;
    else if (ask == ASK_NEW) paused_take(device);            /* a fresh shake ends the paused one */
    if (!g_on && ask != ASK_PAUSE && ask != ASK_STOP) return;

    int target, android = -1;
    if (vt == VT_GAMEPAD) {
        target = shim_pad_slot_of(*(void *const *) ((const char *) device + DEVICE_CONTROLLER));
        if (target < 0) return;                             /* a stale pad: nothing to shake */
        android = shim_device_slot_android(target);
    } else {
        target = vt == VT_KEYBOARD ? TARGET_KEYBOARD : TARGET_SCREEN;
    }

    unsigned long from = 0;
#if ISAAC_DIAG
    /*
     * Who called ShakeScreen, for the report's "who shook": its frame record
     * is still the frame pointer we were entered with (0x8b70a4 and 0x8a8aac
     * restore theirs before the tail calls), and its saved link register is
     * the place in the game. A ShakeScreen reached by a tail call itself
     * (0x70485c, 0x7b5edc) names the caller one level up.
     */
    if (source == SRC_SHAKE && in(SHAKE_SITES, sizeof(SHAKE_SITES) / sizeof(SHAKE_SITES[0]), site)) {
        const unsigned long *ours = (const unsigned long *) __builtin_frame_address(0);
        unsigned long game_fp = ours ? ours[0] : 0;
        if (game_fp && shim_eid_readable((const void *) game_fp, 16)) {
            unsigned long ret = ((const unsigned long *) game_fp)[1];
            if (ret > b && ret - b < 0x1000000) from = ret - b;
        }
    }
    {
        static int said;
        if (said < 16 && shim_report_active()) {
            char line[160];
            static const char *const WHAT[] = { "-", "shake", "hurt", "curse", "resume", "pause", "stop" };
            said++;
            snprintf(line, sizeof(line), "%s device, site 0x%lx from 0x%lx: %s, %.2f s at %.2f -> target %d id %d",
                     vt == VT_GAMEPAD ? "pad" : vt == VT_IOS ? "screen" : "keyboard", site, from,
                     WHAT[source], (double) left, (double) strength, target, android);
            report_line("rumble", line);
        }
    }
#endif
    shim_haptic_java(target, android, source, strength, left, site, from, own_players());
}

/* at load, before protect(): the three vtables get the motor, if they still hold the base */
void shim_rumble_install(unsigned char *base) {
    static const unsigned long VT[3] = { VT_GAMEPAD, VT_IOS, VT_KEYBOARD };
    static const char *const NAME[3] = { "pad", "screen", "keyboard" };
    (void) NAME;                                    /* the log's alone */
    for (int i = 0; i < 3; i++) {
        unsigned long *slot = (unsigned long *) (base + VT[i] + VT_MOTOR);
        if (*slot != (unsigned long) base + BASE_MOTOR && *slot != (unsigned long) motor) {
            LOG("rumble: the %s motor slot holds %lx, not the base - left alone",
                NAME[i], *slot - (unsigned long) base);
            continue;
        }
        *slot = (unsigned long) motor;
        LOG("rumble: the %s motor slot is ours", NAME[i]);
    }
}

/* once a frame: the game's byte agrees with the setting, whatever flipped it; the count for the slider */
void shim_rumble_between_frames(void) {
    shim_sticks_click_byte(g_on);
    own_players();
}
