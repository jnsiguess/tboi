package ru.isaac.port;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/*
 * THE ONE OWNER OF THE VIBRATORS (batch 3, P9-A/B/V, 01.10.2026).
 *
 * The game says "hold the motor at S for T more seconds" - a pad's eccentric
 * motor's language. Handed to Android as it was, every change of that state
 * went out as a new flat createOneShot: a buzzer on the phone's resonant
 * motor, each call cutting the one before, the frame thread making up to two
 * binder calls a frame (the owner, 30.09: "loose"). The owner's decisions 3
 * and 12: modern haptics, meaning instead of a stream of flat buzzes - and
 * there is no iOS example, the image never vibrated.
 *
 * So the layer (rumble.c) reads WHO asked from the return address and sends
 * only the meaning: a new event of a source (shake, hurt, curse), a pause, a
 * stop, one resume. Here it becomes an effect:
 *
 *   CLASSES   the source and the game's numbers pick a class, and every
 *             number of its feel is in the one table below: TAP (a heartbeat,
 *             a short shake), IMPACT (a stomp, an explosion), QUAKE (a long
 *             shake), HURT (damage to the player), CURSE (the white fire,
 *             probably), RESUME (the rest of a shake after the game's pause).
 *             The strengths step by x1.4 or more, so neighbours tell apart.
 *
 *   CHANNELS  one per physical motor: the phone, and each pad that has one.
 *             WHO FEELS IT (routeOf; the owner, 03.10.2026): the touch
 *             screen shakes the phone, a pad with a motor shakes itself. A
 *             device with no motor of its own (a pad without one, a pad the
 *             book does not know, the keyboard) falls back to the phone when
 *             its player is ALONE in the run, as since 24.09 ("some vibration
 *             rather than none"), and shakes nothing in CO-OP - there the
 *             phone is in the hands of the player on the screen, and his
 *             partner's hits used to land on it. Alone or co-op is the
 *             game's own count of own players at the event (rumble.c). The
 *             pad is the one the game named - the player who was hit - not
 *             the one that spoke last.
 *
 *   MIXER     per channel, the active events and their envelope L(t):
 *               - copies of one event (ShakeScreen walks every player: J&E,
 *                 T.Forgotten, two pads on one motor) collapse;
 *               - a new hit plays only 60 ms after the last one, and only if
 *                 the class's PEAK (not the game's S - ShakeScreen's is always
 *                 0.5) is at least 0.6 of the envelope now; weaker or sooner,
 *                 it lengthens the tail instead;
 *               - what already plays covering the new event in strength and
 *                 end sends nothing; a new effect only for a hit or a felt
 *                 lengthening;
 *               - one vibrate() per channel in 40 ms at most; what comes
 *                 inside the window waits for its end and merges.
 *             Nothing cuts an effect short but four things: the game's
 *             pause, its stop, RUMBLE 0, and the app going to the background
 *             (and events are dropped until it is back).
 *
 *   TIERS     chosen once at start from what the phone can do, or forced by
 *             `rumbletier` in port-touch.ini to try a cheaper one:
 *               1 primitives  THUD / CLICK / QUICK_RISE hits, LOW_TICK texture
 *               2 clicks      the same drawings in CLICK and TICK
 *               3 waveform    amplitude steps; a 20 on / 15 off texture breaks
 *                             the resonant buzz
 *               4 onoff       pulses, strength in their width (cheap motors)
 *               5 none
 *             Pads: amplitude steps of 25 ms or more (Bluetooth), the same
 *             drawing on every motor - their order is written to the report,
 *             never assumed.
 *
 *   ATTRIBUTES  API 33+ USAGE_MEDIA (games and animations); 26..32 an
 *             AudioAttributes USAGE_GAME. The system's "media" vibration
 *             strength then applies.
 *
 * Its own thread: the game thread only queues. The report build writes the
 * capabilities, a mixer self-test on synthetic streams from the image's real
 * numbers (explosions, the It Lives stream at 16/33/67 ms, a hit inside a
 * quake, three copies in a frame, a pause and its resume, RUMBLE 0 midway),
 * a ring of the last events and a summary of who shook; the clean build has
 * none of it and keeps the mixer.
 */
final class Haptics {
    interface Say { void say(String line); }

    /* ---------------------------------------------- the layer's words (rumble.c) */
    static final int SRC_SHAKE = 1, SRC_HURT = 2, SRC_CURSE = 3, SRC_RESUME = 4, SRC_PAUSE = 5, SRC_STOP = 6;
    static final int SRC_PROBE = 7;                 /* ours: the RUMBLE slider's sample */
    static final int TARGET_SCREEN = -1, TARGET_KEYBOARD = -2;
    static final int CTL_LEVEL = 1, CTL_TIER = 2, CTL_SAMPLE = 4;     /* 3 was the Tweaks row's probe */

    /* ------------------------------------------------- who feels a device's shake */
    static final int TO_NONE = 0, TO_PHONE = 1, TO_PAD = 2;

    /*
     * The one rule (the owner, 03.10.2026): the touch screen shakes the
     * phone, a pad with a motor shakes itself; a device with no motor of its
     * own - a pad without one, a pad the book does not know, the keyboard -
     * falls back to the phone when its player is alone in the run and shakes
     * nothing in co-op (two own players or more, by the game's rule: J&E,
     * T.Forgotten and his Soul, Strawman's Keeper are one with their owner).
     * padTier is the pad channel's tier, TIER_NONE when it has no motor.
     */
    static int routeOf(int target, int androidId, int padTier, boolean coop) {
        if (target == TARGET_SCREEN) return TO_PHONE;
        if (target >= 0 && androidId >= 0 && padTier != TIER_NONE) return TO_PAD;
        return coop ? TO_NONE : TO_PHONE;
    }

    static String whyNone(int target, int androidId) {
        if (target == TARGET_KEYBOARD) return "co-op, the keyboard has no motor";
        if (target >= 0 && androidId < 0) return "co-op, a pad the book does not know";
        if (target >= 0) return "co-op, the pad has no motor";
        return "co-op, an unknown device";
    }

    /* the layer's count of own players at the event: 2 and more is co-op */
    static boolean coop(int owners) { return owners >= 2; }
    private static final String[] SOURCE = { "-", "shake", "hurt", "curse", "resume", "pause", "stop", "probe" };

    /* ------------------------------------------------------------------- tiers */
    static final int TIER_AUTO = 0, TIER_PRIMITIVES = 1, TIER_CLICKS = 2, TIER_WAVEFORM = 3, TIER_ONOFF = 4,
            TIER_NONE = 5;
    static final String[] TIER_NAME = { "auto", "primitives", "clicks", "waveform", "onoff", "none" };

    /* the pieces an effect is drawn with */
    static final int P_CLICK = 0, P_THUD = 1, P_RISE = 2, P_TICK = 3;
    private static final String[] PIECE = { "CLICK", "THUD", "QUICK_RISE", "LOW_TICK" };

    /* ----------------------------------------------------------------- classes */
    static final int TAP = 0, IMPACT = 1, QUAKE = 2, HURT = 3, CURSE = 4, RESUME = 5;

    static final class Cls {
        final String name;
        final float peak, peakLong;     /* the hit's strength; IMPACT grows to peakLong at 15 frames */
        final int[] pieces;             /* P_*, in order */
        final int[] at;                 /* ms after the start, -1: right after the piece before */
        final float[] share;            /* each piece's share of the peak */
        final float tailFrom;           /* the tail starts at this share of the peak */
        final float tailPerGame;        /* tail = clamp(tailPerGame * the game's length, min, max) */
        final int tailMin, tailMax;     /* ms */
        final float flat;               /* the share of the tail held level before it fades */
        final int period;               /* ms between the texture's ticks */

        Cls(String name, float peak, float peakLong, int[] pieces, int[] at, float[] share, float tailFrom,
            float tailPerGame, int tailMin, int tailMax, float flat, int period) {
            this.name = name; this.peak = peak; this.peakLong = peakLong; this.pieces = pieces; this.at = at;
            this.share = share; this.tailFrom = tailFrom; this.tailPerGame = tailPerGame; this.tailMin = tailMin;
            this.tailMax = tailMax; this.flat = flat; this.period = period;
        }
    }

    /*
     * EVERY NUMBER OF THE FEEL. If the owner says it is still loose, this table
     * is what is tuned - nothing else needs to change.
     *
     *   TAP     ▌           one THUD 0.45                          ShakeScreen <= 4 frames: a heartbeat
     *   IMPACT  █▆▄▂        THUD 0.63..0.80 by length + 2-4 ticks   ShakeScreen 5..15: a stomp, a blast
     *   QUAKE   █▅▅▅▅▅▄▃▂▁  THUD 0.80 + ticks the game's length,    ShakeScreen >= 16: a long shake
     *                       fading in its last 40 %
     *   HURT    ▎█▅▃▁       CLICK 1.0*S, THUD 20 ms later,          damage; the game holds 0.4..2 s, we
     *                       tail clamp(0.4*T, 150, 450) ms          strike and fade (decision 12)
     *   CURSE   ▁▃▅█▅▄▃▂▁   QUICK_RISE, THUD 1.0, tail 400 ms       0x509e00, the white fire (probably)
     *   RESUME  ▅▄▃▂▁       the tail alone, what the pause left     the first frame after the game's pause
     */
    static final Cls[] CLASSES = {
        //       name      peak   long   pieces                       at                 share                  from  perGame min  max  flat period
        new Cls("TAP",    .45f,  .45f,  new int[] { P_THUD },          new int[] { 0 },     new float[] { 1f },      0f,  0f,    0,   0,  0f,  40),
        new Cls("IMPACT", .63f,  .80f,  new int[] { P_THUD },          new int[] { 0 },     new float[] { 1f },    .55f, .6f,   80, 160,  0f,  40),
        new Cls("QUAKE",  .80f,  .80f,  new int[] { P_THUD },          new int[] { 0 },     new float[] { 1f },    .50f, 1f,   250, 900, .6f,  45),
        new Cls("HURT",  1.00f, 1.00f,  new int[] { P_CLICK, P_THUD }, new int[] { 0, 20 }, new float[] { 1f, .8f }, .55f, .4f,  150, 450,  0f,  35),
        new Cls("CURSE", 1.00f, 1.00f,  new int[] { P_RISE, P_THUD },  new int[] { 0, -1 }, new float[] { .7f, 1f }, .60f, 0f,   400, 400,  0f,  40),
        new Cls("RESUME", .40f,  .40f,  new int[] { },                 new int[] { },       new float[] { },        1f,  1f,     0, 450,  0f,  45),
    };
    static final int SHAKE_TAP_MAX = 4, SHAKE_IMPACT_MAX = 15;     /* frames at 60: TAP, IMPACT, then QUAKE */
    static final float HURT_S_MIN = .6f, HURT_S_MAX = 1f;          /* the game's own clamp of its S */
    static final float LEVEL_DEFAULT = 7f;                          /* RUMBLE 7 plays the table as written */

    /* the mixer */
    static final int MIN_SEND = 40;             /* ms between two vibrate() on a channel */
    static final int MIN_HIT = 60;              /* ms between two hits */
    static final float HIT_SHARE = .6f;         /* a hit needs its peak >= this share of L(now) */
    static final int DUP_LENGTH = 20;           /* ms: the same event, lengths this close ... */
    static final int DUP_WINDOW = 40;           /* ... arriving this close */
    static final int EXTEND = 60;               /* ms: a lengthening worth a new effect */
    static final float COVER = .05f;            /* strength: what is playing covers the new event */
    static final int HIT_GAP = 8;               /* ms between a hit and its tail */
    static final float FLOOR = .05f;            /* a tick weaker than this is left out */
    static final int CAP = 1000;                /* ms: no effect is longer */
    static final int STEP = 10;                 /* ms: how finely a sent tail is remembered */

    /* the drawings of the tiers without primitives */
    static final int WAVE_ON = 20, WAVE_OFF = 15, PAD_STEP = 25;
    static final int[] WAVE_PIECE = { 12, 18, 60, 0 };             /* CLICK, THUD, QUICK_RISE as ms at strength */
    static final int ONOFF_HIT_MIN = 25, ONOFF_HIT_MAX = 40, ONOFF_PERIOD = 80;
    static final int ONOFF_PULSE_MIN = 8, ONOFF_PULSE_MAX = 22;
    static final float ONOFF_FLOOR = .15f;
    static final int[] DEFAULT_DURATION = { 12, 30, 60, 10 };      /* when the phone does not say */

    /* ================================================================ the pure part */

    /* an event on a channel: its hit, then its tail */
    static final class Ev {
        int cls, source, target, androidId, site, from;
        float s, t, peak;
        long arrival, start, tailStart, end;
        boolean hit, played;

        float tail(long at) {
            if (at < tailStart || at >= end || end <= tailStart) return 0f;
            Cls k = CLASSES[cls];
            float a0 = k.tailFrom * peak;
            float p = (float) (at - tailStart) / (float) (end - tailStart);
            return p < k.flat ? a0 : a0 * (1f - (p - k.flat) / (1f - k.flat));
        }

        float level(long at) {
            if (hit && at >= start && at < tailStart) return peak;
            return tail(at);
        }
    }

    static Ev make(int cls, float s, float t) {
        Ev e = new Ev();
        e.cls = cls;
        e.s = s;
        e.t = t;
        Cls k = CLASSES[cls];
        float peak = k.peak;
        if (cls == IMPACT) {
            float f = Math.round(t * 60f);
            float x = Math.max(0f, Math.min(1f, (f - (SHAKE_TAP_MAX + 1)) / (SHAKE_IMPACT_MAX - SHAKE_TAP_MAX - 1)));
            peak = k.peak + (k.peakLong - k.peak) * x;
        } else if (cls == HURT) {
            peak = k.peak * Math.max(HURT_S_MIN, Math.min(HURT_S_MAX, s));
        } else if (cls == RESUME) {
            peak = k.peak * Math.max(.5f, Math.min(1f, s));
        }
        e.peak = peak;
        return e;
    }

    static int tailLength(Ev e) {
        Cls k = CLASSES[e.cls];
        return Math.max(k.tailMin, Math.min(k.tailMax, Math.round(k.tailPerGame * e.t * 1000f)));
    }

    /* the class of what the game sent */
    static int classOf(int source, float t) {
        switch (source) {
            case SRC_HURT: return HURT;
            case SRC_CURSE: return CURSE;
            case SRC_RESUME: return RESUME;
            default: {
                int frames = Math.round(t * 60f);
                return frames <= SHAKE_TAP_MAX ? TAP : frames <= SHAKE_IMPACT_MAX ? IMPACT : QUAKE;
            }
        }
    }

    /* what a channel is given to play: primitives, or a waveform (amps null: on/off) */
    static final class Plan {
        int tier;
        int[] pieces; float[] scales; int[] delays;
        long[] timings; int[] amps;
        int length, allowed;
        boolean hit;
        String what;
    }

    /* a tier's drawing */
    static final class Render {
        final int tier;
        final boolean pad;
        final int[] duration;

        Render(int tier, boolean pad, int[] duration) {
            this.tier = tier;
            this.pad = pad;
            this.duration = duration != null ? duration : DEFAULT_DURATION;
        }

        int dur(int piece) {
            switch (tier) {
                case TIER_PRIMITIVES: return duration[piece];
                case TIER_CLICKS: return piece == P_TICK ? duration[P_TICK] : duration[P_CLICK];
                case TIER_ONOFF: return piece == P_CLICK ? ONOFF_HIT_MIN : ONOFF_HIT_MAX;
                default: return WAVE_PIECE[piece];
            }
        }

        /* the hit's pieces: start times in ms, and where the hit ends */
        int[] hitTimes(Cls k) {
            int[] out = new int[k.pieces.length + 1];
            int end = 0;
            for (int i = 0; i < k.pieces.length; i++) {
                int at = k.at[i] < 0 ? end : k.at[i];
                out[i] = at;
                end = Math.max(end, at + dur(k.pieces[i]));
            }
            out[k.pieces.length] = end;
            return out;
        }

        int hitLength(Cls k) { return k.pieces.length == 0 ? 0 : hitTimes(k)[k.pieces.length]; }

        /* the allowance of a class: its hit, the gap and its longest tail */
        int allowance(Cls k) { return hitLength(k) + HIT_GAP + k.tailMax + MIN_SEND; }

        Plan build(List<Ev> active, Ev hit, long now, float gain) {
            if (tier == TIER_NONE) return null;
            long end = now;
            int allowed = 0;
            for (Ev e : active) {
                end = Math.max(end, e.end);
                allowed = Math.max(allowed, allowance(CLASSES[e.cls]));
            }
            if (end - now > CAP) end = now + CAP;
            int length = (int) (end - now);
            /* the hit's pieces, then the tail's samples: (piece, strength, at) */
            ArrayList<float[]> marks = new ArrayList<>();
            int tailFrom = 0;
            if (hit != null) {
                Cls k = CLASSES[hit.cls];
                int[] times = hitTimes(k);
                for (int i = 0; i < k.pieces.length; i++)
                    marks.add(new float[] { k.pieces[i], Math.min(1f, hit.peak * k.share[i] * gain), times[i] });
                tailFrom = times[k.pieces.length] + HIT_GAP;
            }
            /* the tail: the loudest active tail at each tick, at its own period */
            ArrayList<float[]> ticks = new ArrayList<>();
            for (long t = now + tailFrom; t < end; ) {
                float a = 0f;
                int period = 40;
                for (Ev e : active) {
                    float v = e.tail(t);
                    if (v > a) { a = v; period = CLASSES[e.cls].period; }
                }
                ticks.add(new float[] { P_TICK, Math.min(1f, a * gain), t - now, period });
                t += tier == TIER_ONOFF ? ONOFF_PERIOD : (tier == TIER_WAVEFORM ? (pad ? PAD_STEP : WAVE_ON + WAVE_OFF) : period);
            }
            boolean any = !marks.isEmpty();
            for (float[] k : ticks) any |= k[1] >= FLOOR;
            if (!any) return null;
            Plan p;
            switch (tier) {
                case TIER_PRIMITIVES: case TIER_CLICKS: p = composed(marks, ticks); break;
                case TIER_WAVEFORM: p = waved(marks, ticks, length); break;
                default: p = pulsed(marks, ticks, length); break;
            }
            p.tier = tier;
            p.hit = hit != null;
            p.allowed = allowed;
            return p;
        }

        private Plan composed(List<float[]> marks, List<float[]> ticks) {
            ArrayList<float[]> all = new ArrayList<>(marks);
            for (float[] k : ticks) if (k[1] >= FLOOR) all.add(k);
            int n = all.size();
            Plan p = new Plan();
            p.pieces = new int[n];
            p.scales = new float[n];
            p.delays = new int[n];
            int cursor = 0, textures = 0;
            StringBuilder w = new StringBuilder();
            for (int i = 0; i < n; i++) {
                float[] m = all.get(i);
                int piece = (int) m[0];
                int at = (int) m[2];
                p.pieces[i] = piece;
                p.scales[i] = Math.max(0f, Math.min(1f, m[1]));
                p.delays[i] = Math.max(0, at - cursor);
                cursor += p.delays[i] + dur(piece);
                if (piece == P_TICK) textures++;
                else if (telling) w.append(w.length() > 0 ? " + " : "").append(name(piece)).append(String.format(java.util.Locale.ROOT, " %.2f", p.scales[i]));
            }
            if (telling && textures > 0) w.append(w.length() > 0 ? " + " : "").append(name(P_TICK)).append(" x").append(textures);
            p.length = cursor;
            p.what = telling ? w.toString() : "";
            return p;
        }

        private String name(int piece) {
            if (tier == TIER_CLICKS) return piece == P_TICK ? "TICK" : "CLICK";
            return PIECE[piece];
        }

        private static int amp(float a) {
            int v = Math.round(Math.max(0f, Math.min(1f, a)) * 255f);
            return v < 1 ? 0 : v;
        }

        /* amplitude steps: from zero, the hit, a gap, the tail, back to zero */
        private Plan waved(List<float[]> marks, List<float[]> ticks, int length) {
            ArrayList<long[]> seg = new ArrayList<>();
            seg.add(new long[] { 0, 0 });
            int cursor = 0;
            for (float[] m : marks) {
                int piece = (int) m[0], at = (int) m[2];
                if (at > cursor) { seg.add(new long[] { at - cursor, 0 }); cursor = at; }
                if (piece == P_RISE) {
                    int third = WAVE_PIECE[P_RISE] / 3;
                    seg.add(new long[] { third, amp(m[1] * .45f) });
                    seg.add(new long[] { third, amp(m[1] * .75f) });
                    seg.add(new long[] { WAVE_PIECE[P_RISE] - 2 * third, amp(m[1]) });
                } else {
                    seg.add(new long[] { WAVE_PIECE[piece], amp(m[1]) });
                }
                cursor += WAVE_PIECE[piece];
            }
            for (float[] k : ticks) {
                int at = (int) k[2];
                if (at > cursor) { seg.add(new long[] { at - cursor, 0 }); cursor = at; }
                int on = pad ? PAD_STEP : WAVE_ON;
                on = Math.max(1, Math.min(on, length - cursor));
                seg.add(new long[] { on, amp(k[1]) });
                cursor += on;
            }
            seg.add(new long[] { STEP, 0 });
            cursor += STEP;
            Plan p = new Plan();
            p.timings = new long[seg.size()];
            p.amps = new int[seg.size()];
            for (int i = 0; i < seg.size(); i++) { p.timings[i] = seg.get(i)[0]; p.amps[i] = (int) seg.get(i)[1]; }
            p.length = cursor;
            p.what = telling ? "wave " + seg.size() + " steps, " + cursor + " ms" : "";
            return p;
        }

        /* on/off only: an off first, then pulses whose width is the strength, an off last */
        private Plan pulsed(List<float[]> marks, List<float[]> ticks, int length) {
            ArrayList<Long> t = new ArrayList<>();
            int cursor = 0, pulses = 0;
            boolean on = false;                     /* the state the next entry describes: t[0] is off */
            ArrayList<int[]> ons = new ArrayList<>();
            for (float[] m : marks) {
                int at = (int) m[2];
                int width = Math.round(ONOFF_HIT_MIN + (ONOFF_HIT_MAX - ONOFF_HIT_MIN) * m[1]);
                ons.add(new int[] { at, width });
            }
            for (float[] k : ticks) {
                if (k[1] < ONOFF_FLOOR) continue;
                int width = Math.round(ONOFF_PULSE_MIN + (ONOFF_PULSE_MAX - ONOFF_PULSE_MIN) * k[1]);
                ons.add(new int[] { (int) k[2], width });
            }
            for (int[] o : ons) {
                int at = Math.max(o[0], cursor);
                t.add((long) (at - cursor));        /* off */
                t.add((long) o[1]);                 /* on */
                cursor = at + o[1];
                pulses++;
            }
            t.add((long) STEP);                     /* off, last */
            cursor += STEP;
            Plan p = new Plan();
            p.timings = new long[t.size()];
            for (int i = 0; i < t.size(); i++) p.timings[i] = t.get(i);
            p.length = cursor;
            p.what = telling ? "onoff " + pulses + " pulses, " + cursor + " ms" : "";
            return p;
        }
    }

    /* one channel: a physical motor */
    static final class Channel {
        final String name;
        Render render;
        final ArrayList<Ev> active = new ArrayList<>();
        long lastSend = -100000, lastHit = -100000;
        long sentAt, sentEnd;
        float[] sent = new float[0];
        boolean pending;
        Ev pendingHit;
        Runnable flush;
        /* the device behind it (the real part) */
        int androidId = -1;
        Object motor;                   /* a Vibrator, or a pad's VibratorManager / Vibrator */
        int[] motorIds = new int[0];

        Channel(String name, Render render) { this.name = name; this.render = render; }
    }

    interface Out {
        void play(Channel c, Plan p, long now);
        void cancel(Channel c, long now);
        void wake(Channel c, long at);
    }

    /* the rules; pure, driven by the clock it is given */
    static final class Mixer {
        final Out out;
        float gain = 1f;

        Mixer(Out out) { this.out = out; }

        private static void prune(Channel c, long now) {
            for (int i = c.active.size() - 1; i >= 0; i--)
                if (c.active.get(i).end <= now && c.active.get(i) != c.pendingHit) c.active.remove(i);
        }

        private static float level(Channel c, long at) {
            float a = 0f;
            for (Ev e : c.active) a = Math.max(a, e.level(at));
            return a;
        }

        private static float sentLevel(Channel c, long at) {
            int i = (int) ((at - c.sentAt) / STEP);
            return i >= 0 && i < c.sent.length ? c.sent[i] : 0f;
        }

        private static boolean covered(Channel c, Ev e, long now) {
            if (c.sentEnd <= now || e.end > c.sentEnd + EXTEND) return false;
            for (long t = Math.max(now, e.tailStart); t < e.end; t += STEP)
                if (e.tail(t) > sentLevel(c, t) + COVER) return false;
            return true;
        }

        /* a new event on a channel: what the mixer did with it */
        String event(Channel c, Ev e, long now) {
            prune(c, now);
            for (Ev a : c.active)
                if (a.cls == e.cls && Math.abs(a.s - e.s) < .01f && Math.abs(a.t - e.t) * 1000f <= DUP_LENGTH
                        && now - a.arrival <= DUP_WINDOW) return "dup";
            Cls k = CLASSES[e.cls];
            int hitLen = c.render.hitLength(k);
            e.arrival = now;
            e.start = now;
            e.hit = hitLen > 0 && now - c.lastHit >= MIN_HIT && e.peak >= HIT_SHARE * level(c, now);
            e.tailStart = e.hit ? now + hitLen + HIT_GAP : now;
            e.end = e.tailStart + tailLength(e);
            if (!e.hit && e.end <= now) return "absorbed";      /* a hit too soon, and no tail of its own */
            if (!e.hit && covered(c, e, now)) { c.active.add(e); return "covered"; }
            c.active.add(e);
            if (e.hit && (c.pendingHit == null || e.peak > c.pendingHit.peak)) c.pendingHit = e;
            if (now - c.lastSend >= MIN_SEND && !c.pending) {
                send(c, now);
                return e.hit ? "new" : "extend";
            }
            if (!c.pending) {
                c.pending = true;
                out.wake(c, c.lastSend + MIN_SEND);
            }
            return "coalesce";
        }

        /* the window is over: what waited goes out as one effect */
        void flush(Channel c, long now) {
            if (!c.pending) return;
            prune(c, now);
            send(c, now);
        }

        private void send(Channel c, long now) {
            Ev h = c.pendingHit;
            c.pendingHit = null;
            c.pending = false;
            if (h != null && now - c.lastHit < MIN_HIT) h = null;
            if (h != null && h.start != now) {
                long d = now - h.start;
                h.start += d; h.tailStart += d; h.end += d;
            }
            /* a hit that lost to a stronger one keeps its tail, from its own start */
            for (Ev e : c.active)
                if (e.hit && !e.played && e != h) {
                    long len = e.end - e.tailStart;
                    e.hit = false;
                    e.tailStart = e.start;
                    e.end = e.start + len;
                }
            prune(c, now);
            if (h != null) h.played = true;
            Plan p = c.render.build(c.active, h, now, gain);
            if (p == null) return;
            out.play(c, p, now);
            c.lastSend = now;
            if (h != null) c.lastHit = now;
            c.sentAt = now;
            c.sentEnd = now + p.length;
            c.sent = new float[p.length / STEP + 1];
            for (int i = 0; i < c.sent.length; i++) {
                float a = 0f;
                for (Ev e : c.active) a = Math.max(a, e.tail(now + (long) i * STEP));
                c.sent[i] = a;
            }
        }

        /* the game's pause, its stop, RUMBLE 0, the app leaving: the channel falls silent */
        void cancel(Channel c, long now) {
            c.active.clear();
            c.pending = false;
            c.pendingHit = null;
            c.sentEnd = now;
            c.sent = new float[0];
            out.cancel(c, now);
        }
    }

    /* ------------------------------------------------------------- self-test */
    /*
     * The mixer against synthetic streams from the image's real numbers, the
     * vibrator replaced by a record. Answers "ok ..." or "FAIL <what>".
     */
    static String selfTest() {
        final class Rec implements Out {
            final ArrayList<Object[]> plays = new ArrayList<>();       /* channel, time, plan */
            final ArrayList<Object[]> cancels = new ArrayList<>();
            final HashMap<Channel, Long> wakes = new HashMap<>();
            public void play(Channel c, Plan p, long now) { plays.add(new Object[] { c, now, p }); }
            public void cancel(Channel c, long now) { cancels.add(new Object[] { c, now }); wakes.remove(c); }
            public void wake(Channel c, long at) { wakes.put(c, at); }
        }
        final Render[] renders = {
            new Render(TIER_PRIMITIVES, false, DEFAULT_DURATION), new Render(TIER_WAVEFORM, false, null),
            new Render(TIER_WAVEFORM, true, null), new Render(TIER_ONOFF, false, null),
            new Render(TIER_CLICKS, false, DEFAULT_DURATION),
        };
        StringBuilder fail = new StringBuilder();
        int cases = 0, sends = 0;
        /* the scenarios: {time ms, class or -source, S, T} */
        ArrayList<float[][]> scenarios = new ArrayList<>();
        ArrayList<String> names = new ArrayList<>();
        {
            float[][] s = new float[20][];
            for (int i = 0; i < 20; i++) s[i] = new float[] { i * 50, -SRC_SHAKE, .5f, 10 / 60f };
            scenarios.add(s); names.add("20 explosions in a second");
        }
        for (int step : new int[] { 16, 33, 67 }) {
            int n = 2000 / step;
            float[][] s = new float[n][];
            for (int i = 0; i < n; i++) s[i] = new float[] { i * step, -SRC_SHAKE, .5f, 4 / 60f };
            scenarios.add(s); names.add("It Lives every " + step + " ms");
        }
        scenarios.add(new float[][] { { 0, -SRC_SHAKE, .5f, 40 / 60f }, { 200, -SRC_SHAKE, .5f, 10 / 60f },
                                      { 400, -SRC_HURT, 1f, 1.2f } });
        names.add("a hit inside a quake");
        scenarios.add(new float[][] { { 100, -SRC_SHAKE, .5f, 8 / 60f }, { 100, -SRC_SHAKE, .5f, 8 / 60f },
                                      { 100, -SRC_SHAKE, .5f, 8 / 60f } });
        names.add("three copies in one frame");
        scenarios.add(new float[][] { { 0, -SRC_HURT, 1f, 1.5f }, { 100, -SRC_PAUSE, 0f, 0f },
                                      { 1100, -SRC_RESUME, 1f, .3f } });
        names.add("a pause and its resume");
        scenarios.add(new float[][] { { 0, -SRC_SHAKE, .5f, 40 / 60f }, { 200, 99, 0f, 0f },
                                      { 300, -SRC_SHAKE, .5f, 10 / 60f } });
        names.add("RUMBLE 0 midway");
        for (Render r : renders) {
            for (int si = 0; si < scenarios.size(); si++) {
                cases++;
                float[][] sc = scenarios.get(si);
                String what = names.get(si) + " (" + TIER_NAME[r.tier] + (r.pad ? " pad" : "") + ")";
                Rec rec = new Rec();
                Mixer m = new Mixer(rec);
                Channel c = new Channel("test", r);
                boolean off = false;
                int k = 0;
                long now = 0;
                while (k < sc.length || rec.wakes.containsKey(c)) {
                    long nextEvent = k < sc.length ? (long) sc[k][0] : Long.MAX_VALUE;
                    Long w = rec.wakes.get(c);
                    if (w != null && w <= nextEvent) {
                        now = w;
                        rec.wakes.remove(c);
                        m.flush(c, now);
                        continue;
                    }
                    now = nextEvent;
                    float[] ev = sc[k++];
                    int code = (int) ev[1];
                    if (code == 99) { off = true; m.cancel(c, now); continue; }
                    int source = -code;
                    if (source == SRC_PAUSE || source == SRC_STOP) { m.cancel(c, now); continue; }
                    if (off) continue;
                    Ev e = make(classOf(source, ev[3]), ev[2], ev[3]);
                    e.source = source;
                    m.event(c, e, now);
                }
                sends += rec.plays.size();
                long lastT = -100000, lastHitT = -100000;
                for (Object[] pl : rec.plays) {
                    long t = (Long) pl[1];
                    Plan p = (Plan) pl[2];
                    if (t - lastT < MIN_SEND) fail.append(what).append(": two sends ").append(t - lastT).append(" ms apart; ");
                    lastT = t;
                    if (p.hit) {
                        if (t - lastHitT < MIN_HIT) fail.append(what).append(": two hits ").append(t - lastHitT).append(" ms apart; ");
                        lastHitT = t;
                    }
                    if (p.length > p.allowed || p.length > CAP + STEP + ONOFF_HIT_MAX)
                        fail.append(what).append(": an effect of ").append(p.length).append(" ms, allowed ").append(p.allowed).append("; ");
                    if (p.amps != null && (p.amps[0] != 0 || p.amps[p.amps.length - 1] != 0))
                        fail.append(what).append(": a wave that does not start and end at zero; ");
                    if (p.amps == null && p.timings != null && p.timings.length % 2 == 0)
                        fail.append(what).append(": pulses that do not end off; ");
                    if (p.scales != null)
                        for (float s : p.scales)
                            if (!(s >= 0f && s <= 1f)) fail.append(what).append(": a scale of ").append(s).append("; ");
                }
                if (names.get(si).startsWith("three copies") && rec.plays.size() != 1)
                    fail.append(what).append(": ").append(rec.plays.size()).append(" effects for one event; ");
                if (names.get(si).startsWith("a pause")) {
                    if (rec.cancels.isEmpty()) fail.append(what).append(": the pause did not cancel; ");
                    boolean resumed = false;
                    for (Object[] pl : rec.plays) resumed |= (Long) pl[1] >= 1100;
                    if (!resumed) fail.append(what).append(": the resume played nothing; ");
                }
                if (names.get(si).startsWith("RUMBLE 0")) {
                    if (rec.cancels.isEmpty()) fail.append(what).append(": RUMBLE 0 did not cancel; ");
                    for (Object[] pl : rec.plays)
                        if ((Long) pl[1] >= 200) fail.append(what).append(": played after RUMBLE 0; ");
                }
            }
        }
        /* who feels it: {target, android id, pad tier, own players, the answer} */
        final int[][] routes = {
            { TARGET_SCREEN, -1, TIER_NONE, 1, TO_PHONE },          /* alone on the screen: the phone */
            { 0, 12, TIER_NONE, 1, TO_PHONE },                      /* alone on a motorless pad: the phone, as before */
            { 0, -1, TIER_NONE, 1, TO_PHONE },                      /* alone on a pad the book does not know */
            { TARGET_KEYBOARD, -1, TIER_NONE, 1, TO_PHONE },        /* alone on the keyboard: the phone */
            { 0, 12, TIER_WAVEFORM, 1, TO_PAD },                    /* alone on a pad with a motor: the pad */
            { 0, 12, TIER_NONE, 0, TO_PHONE },                      /* no players (menus): alone */
            { TARGET_SCREEN, -1, TIER_NONE, 2, TO_PHONE },          /* co-op, the screen: the phone */
            { 1, 12, TIER_NONE, 2, TO_NONE },                       /* co-op, a motorless pad: nothing */
            { 1, -1, TIER_WAVEFORM, 2, TO_NONE },                   /* co-op, a pad the book does not know */
            { TARGET_KEYBOARD, -1, TIER_NONE, 2, TO_NONE },         /* co-op, the keyboard: nothing */
            { 1, 12, TIER_WAVEFORM, 2, TO_PAD },                    /* co-op, a pad with a motor: itself */
            { 2, 13, TIER_ONOFF, 4, TO_PAD },
            { 3, 14, TIER_NONE, 4, TO_NONE },
        };
        for (int[] r : routes) {
            cases++;
            int got = routeOf(r[0], r[1], r[2], coop(r[3]));
            if (got != r[4])
                fail.append("route target ").append(r[0]).append(" id ").append(r[1]).append(" tier ")
                    .append(TIER_NAME[r[2]]).append(" own players ").append(r[3]).append(": ").append(got)
                    .append(", not ").append(r[4]).append("; ");
        }
        if (fail.length() > 0) return "FAIL " + fail.toString().trim();
        return "ok (" + cases + " cases, " + sends + " effects; sends >= " + MIN_SEND + " ms, hits >= " + MIN_HIT
                + " ms apart, copies collapse, waves start and end at zero, pause and RUMBLE 0 cancel;"
                + " alone a motorless device falls back to the phone, in co-op it shakes nothing)";
    }

    /* ================================================================ the real part */

    private final android.content.Context context;
    private final Say say;
    private android.os.Handler handler;
    private Mixer mixer;
    private Channel phone;
    private final HashMap<Integer, Channel> pads = new HashMap<>();   /* by Android device id */
    private int[] phoneDurations = DEFAULT_DURATION;
    private int phoneBest = TIER_NONE, forced = TIER_AUTO;
    private volatile boolean paused;
    private volatile int level = (int) LEVEL_DEFAULT;
    private int droppedPaused;

    /* the report: the last events, and who shook */
    private static final int RING = 400;
    private final String[] ring = new String[RING];
    private int ringAt, ringNew;
    private final HashMap<Long, int[]> whoShook = new HashMap<>();
    private final HashMap<Long, String> whoShookClass = new HashMap<>();

    /*
     * The report is on (a Say was given): only then are the drawings' and the
     * events' descriptions written out - in the build we play on they were
     * built and thrown away, String.format and all, on every shake (hotfix 1.72).
     */
    static volatile boolean telling;

    Haptics(android.content.Context context, Say say) {
        this.context = context.getApplicationContext();
        this.say = say;
        telling = say != null;
    }

    private native void bind();

    private void tell(String line) { if (say != null) say.say("haptics: " + line); }

    private static long now() { return android.os.SystemClock.uptimeMillis(); }

    /* the thread, the phone's abilities, the bridge; on the UI thread, before the game thread exists */
    void start() {
        android.os.HandlerThread thread = new android.os.HandlerThread("haptics");
        thread.start();
        handler = new android.os.Handler(thread.getLooper());
        mixer = new Mixer(new Out() {
            public void play(Channel c, Plan p, long now) { vibrate(c, p); }
            public void cancel(Channel c, long now) { silence(c); if (c.flush != null) handler.removeCallbacks(c.flush); }
            public void wake(Channel c, long at) {
                if (c.flush == null) {
                    final Channel ch = c;
                    c.flush = () -> mixer.flush(ch, now());
                }
                handler.removeCallbacks(c.flush);
                handler.postAtTime(c.flush, at);
            }
        });
        mixer.gain = level / LEVEL_DEFAULT;
        handler.post(this::detect);
        if (say != null) handler.post(() -> tell("selftest: " + selfTest()));
        try {
            bind();
        } catch (Throwable t) {
            tell("bridge missing: " + t);
        }
    }

    /* ---- from the layer (any thread): queued, never done here ---- */

    /* a shake's meaning (rumble.c shim_haptic_java) */
    public void fromGame(int target, int androidId, int source, float strength, float seconds, int site, int from,
                         int owners) {
        if (paused) { droppedPaused++; return; }
        android.os.Handler h = handler;
        if (h != null) h.post(() -> onGame(target, androidId, source, strength, seconds, site, from, owners));
    }

    /* the setting (rumble.c): the level, the tier, the sample of a slider step */
    public void fromLayer(int what, int value) {
        if (what == CTL_LEVEL) level = Math.max(0, Math.min(10, value));
        android.os.Handler h = handler;
        if (h != null) h.post(() -> onLayer(what, value));
    }

    /* the app leaves (onPause) and comes back (onResume) */
    void pause(boolean on) {
        paused = on;
        android.os.Handler h = handler;
        if (h == null) return;
        if (on) h.post(() -> { stopAll(); dump(); });
    }

    /* ---- on the haptics thread ---- */

    private void stopAll() {
        long now = now();
        if (phone != null) mixer.cancel(phone, now);
        for (Channel c : pads.values()) mixer.cancel(c, now);
    }

    private void onLayer(int what, int value) {
        switch (what) {
            case CTL_LEVEL:
                mixer.gain = value / LEVEL_DEFAULT;
                if (value <= 0) stopAll();
                note(null, "level " + value + (value <= 0 ? ": every channel cancelled" : ""));
                break;
            case CTL_TIER:
                forced = value >= TIER_AUTO && value <= TIER_ONOFF ? value : TIER_AUTO;
                choosePhoneTier(true);
                break;
            case CTL_SAMPLE:
                if (level > 0 && !paused) probe(value & 1, (value & 2) != 0, IMPACT, .5f, 10 / 60f);
                break;
        }
    }

    /*
     * The sample of a slider step: to the phone (0), or to the pads
     * with a motor (1). When no pad has one, by routeOf's rule: the phone if
     * the run is not co-op (bit 2 of the layer's value), else nothing.
     */
    private void probe(int toPads, boolean coop, int cls, float s, float t) {
        ArrayList<Channel> to = new ArrayList<>();
        if (toPads == 1) {
            for (int id : android.view.InputDevice.getDeviceIds()) {
                android.view.InputDevice d = android.view.InputDevice.getDevice(id);
                if (d == null) continue;
                int kind = InputBook.kindOf(d);
                if (kind != InputBook.PAD && kind != InputBook.HYBRID) continue;
                Channel c = padChannel(-1, id);
                if (c.render.tier != TIER_NONE) to.add(c);
            }
        }
        if (to.isEmpty() && phone != null && (toPads == 0 || !coop)) to.add(phone);
        long now = now();
        if (to.isEmpty()) note(null, "probe " + CLASSES[cls].name + ": co-op and no pad with a motor - nothing plays");
        for (Channel c : to) {
            Ev e = make(cls, s, t);
            e.source = SRC_PROBE;
            e.target = toPads == 1 ? 0 : TARGET_SCREEN;
            e.androidId = c.androidId;
            String d = mixer.event(c, e, now);
            note(e, c.name + " " + CLASSES[cls].name + " -> " + d);
        }
    }

    private void onGame(int target, int androidId, int source, float strength, float seconds, int site, int from,
                        int owners) {
        if (phone == null) return;
        Channel pad = target >= 0 && androidId >= 0 ? padChannel(target, androidId) : null;
        int to = routeOf(target, androidId, pad != null ? pad.render.tier : TIER_NONE, coop(owners));
        long now = now();
        if (to == TO_NONE) {
            /* co-op and nobody's motor: not even a pause or a stop may touch the phone, it is the other player's */
            if (say != null) ringLine(now, null, target, source, site, from, strength, seconds, "drop: " + whyNone(target, androidId) + " (" + owners + " own players)");
            return;
        }
        Channel c = to == TO_PAD ? pad : phone;
        if (source == SRC_PAUSE || source == SRC_STOP) {
            mixer.cancel(c, now);
            ringLine(now, c, target, source, site, from, strength, seconds, "cancel");
            return;
        }
        if (level <= 0) { ringLine(now, c, target, source, site, from, strength, seconds, "drop: RUMBLE 0"); return; }
        Ev e = make(classOf(source, seconds), strength, seconds);
        e.source = source;
        e.target = target;
        e.androidId = androidId;
        e.site = site;
        e.from = from;
        String d = mixer.event(c, e, now);
        if (source == SRC_RESUME && d.equals("new")) d = "resume";
        if (say != null)
            ringLine(now, c, target, source, site, from, strength, seconds, CLASSES[e.cls].name + " " + d
                    + (c == phone && target != TARGET_SCREEN ? " (alone, " + owners + " own player: the phone stands in)" : ""));
        if (say != null) {
            long key = from != 0 ? from : site;
            int[] n = whoShook.get(key);
            if (n == null) { n = new int[1]; whoShook.put(key, n); whoShookClass.put(key, CLASSES[e.cls].name); }
            n[0]++;
        }
    }

    /* ---- the devices ---- */

    private android.os.Vibrator phoneVibrator() {
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            android.os.VibratorManager vm =
                    (android.os.VibratorManager) context.getSystemService(android.content.Context.VIBRATOR_MANAGER_SERVICE);
            return vm != null ? vm.getDefaultVibrator() : null;
        }
        return (android.os.Vibrator) context.getSystemService(android.content.Context.VIBRATOR_SERVICE);
    }

    /* what the phone can do, once; the report's `caps` line */
    private void detect() {
        android.os.Vibrator v = phoneVibrator();
        StringBuilder caps = new StringBuilder();
        caps.append(android.os.Build.MANUFACTURER).append(' ').append(android.os.Build.MODEL)
            .append(", API ").append(android.os.Build.VERSION.SDK_INT);
        int best = TIER_NONE;
        if (v != null && v.hasVibrator()) {
            boolean amplitude = v.hasAmplitudeControl();
            caps.append(", amplitude ").append(amplitude ? "yes" : "no");
            best = amplitude ? TIER_WAVEFORM : TIER_ONOFF;
            if (android.os.Build.VERSION.SDK_INT >= 30) {
                boolean clicks;
                if (android.os.Build.VERSION.SDK_INT >= 31) {
                    int[] want = { android.os.VibrationEffect.Composition.PRIMITIVE_CLICK,
                                   android.os.VibrationEffect.Composition.PRIMITIVE_THUD,
                                   android.os.VibrationEffect.Composition.PRIMITIVE_QUICK_RISE,
                                   android.os.VibrationEffect.Composition.PRIMITIVE_LOW_TICK,
                                   android.os.VibrationEffect.Composition.PRIMITIVE_TICK };
                    boolean[] has = v.arePrimitivesSupported(want);
                    int[] ms = v.getPrimitiveDurations(want);
                    caps.append(", primitives");
                    String[] names = { "CLICK", "THUD", "QUICK_RISE", "LOW_TICK", "TICK" };
                    boolean all = true;
                    for (int i = 0; i < 4; i++) all &= has[i] && ms[i] > 0;
                    for (int i = 0; i < want.length; i++)
                        caps.append(' ').append(names[i]).append(has[i] ? " " + ms[i] + " ms" : " no");
                    clicks = has[0] && has[4] && ms[0] > 0 && ms[4] > 0;
                    if (all) phoneDurations = new int[] { ms[0], ms[1], ms[2], ms[3] };
                    else if (clicks) phoneDurations = new int[] { ms[0], ms[0], ms[0], ms[4] };
                    if (all) best = TIER_PRIMITIVES;
                    else if (clicks) best = TIER_CLICKS;
                } else {
                    boolean[] has = v.arePrimitivesSupported(android.os.VibrationEffect.Composition.PRIMITIVE_CLICK,
                            android.os.VibrationEffect.Composition.PRIMITIVE_TICK);
                    clicks = has[0] && has[1];
                    caps.append(", primitives CLICK ").append(has[0] ? "yes" : "no").append(" TICK ")
                        .append(has[1] ? "yes" : "no").append(" (durations unknown before API 31)");
                    if (clicks) best = TIER_CLICKS;
                }
            }
            if (android.os.Build.VERSION.SDK_INT >= 34)
                caps.append(String.format(java.util.Locale.ROOT, ", resonant %.1f Hz, Q %.1f", v.getResonantFrequency(), v.getQFactor()));
            caps.append(", envelopes ").append(envelopes(v));
        } else {
            caps.append(", no vibrator");
        }
        caps.append(", media vibration ").append(mediaIntensity());
        caps.append(", attributes ").append(android.os.Build.VERSION.SDK_INT >= 33 ? "USAGE_MEDIA" : "AudioAttributes USAGE_GAME");
        phoneBest = best;
        phone = new Channel("phone", new Render(best, false, phoneDurations));
        phone.motor = v;
        choosePhoneTier(false);
        tell("caps: " + caps + " -> " + TIER_NAME[phone.render.tier]
                + (forced != TIER_AUTO ? " (rumbletier " + TIER_NAME[forced] + ")" : ""));
    }

    private static String envelopes(android.os.Vibrator v) {
        if (android.os.Build.VERSION.SDK_INT < 36) return "n/a";
        try {
            Object r = android.os.Vibrator.class.getMethod("areEnvelopeEffectsSupported").invoke(v);
            return Boolean.TRUE.equals(r) ? "yes" : "no";
        } catch (Throwable t) {
            return "unknown";
        }
    }

    /* the system's "media" vibration strength, where the phone lets it be read */
    private String mediaIntensity() {
        try {
            return String.valueOf(android.provider.Settings.System.getInt(context.getContentResolver(),
                    "media_vibration_intensity"));
        } catch (Throwable t) {
            return "unknown";
        }
    }

    /* `rumbletier`: a cheaper tier on request; one this phone lacks is refused */
    private void choosePhoneTier(boolean say) {
        if (phone == null) return;
        int tier = phoneBest;
        String why = "";
        if (forced != TIER_AUTO && phoneBest != TIER_NONE) {
            if (forced >= phoneBest) tier = forced;
            else why = " (" + TIER_NAME[forced] + " is not available here)";
        }
        long now = now();
        mixer.cancel(phone, now);
        phone.render = new Render(tier, false, phoneDurations);
        if (say) tell("tier: " + TIER_NAME[tier] + why);
    }

    /* a pad's channel, made the first time its Android device is named */
    private Channel padChannel(int slot, int androidId) {
        Channel c = pads.get(androidId);
        if (c != null) return c;
        android.view.InputDevice dev = android.view.InputDevice.getDevice(androidId);
        int tier = TIER_NONE;
        Object motor = null;
        int[] ids = new int[0];
        boolean amplitude = false;
        try {
            if (dev != null && android.os.Build.VERSION.SDK_INT >= 31) {
                android.os.VibratorManager vm = dev.getVibratorManager();
                ids = vm.getVibratorIds();
                if (ids.length > 0) {
                    motor = vm;
                    amplitude = true;
                    for (int id : ids) amplitude &= vm.getVibrator(id).hasAmplitudeControl();
                    tier = amplitude ? TIER_WAVEFORM : TIER_ONOFF;
                }
            } else if (dev != null) {
                android.os.Vibrator v = dev.getVibrator();
                if (v != null && v.hasVibrator()) {
                    motor = v;
                    amplitude = v.hasAmplitudeControl();
                    tier = amplitude ? TIER_WAVEFORM : TIER_ONOFF;
                }
            }
        } catch (RuntimeException e) {
            tier = TIER_NONE;
        }
        if (forced == TIER_ONOFF && tier == TIER_WAVEFORM) tier = TIER_ONOFF;
        c = new Channel("pad id " + androidId, new Render(tier, true, null));
        c.androidId = androidId;
        c.motor = motor;
        c.motorIds = ids;
        pads.put(androidId, c);
        tell("pad: slot " + slot + " id " + androidId + (dev != null ? " " + dev.getName() : " (gone)")
                + ", motors " + java.util.Arrays.toString(ids) + " (in the order Android lists them)"
                + ", amplitude " + (amplitude ? "yes" : "no")
                + " -> " + (tier == TIER_NONE ? "none: the phone when its player is alone, nothing in co-op" : TIER_NAME[tier]));
        return c;
    }

    /* ---- the effect, handed to Android ---- */

    private static int primitive(int piece, int tier) {
        if (tier == TIER_CLICKS)
            return piece == P_TICK ? android.os.VibrationEffect.Composition.PRIMITIVE_TICK
                                   : android.os.VibrationEffect.Composition.PRIMITIVE_CLICK;
        switch (piece) {
            case P_THUD: return android.os.VibrationEffect.Composition.PRIMITIVE_THUD;
            case P_RISE: return android.os.VibrationEffect.Composition.PRIMITIVE_QUICK_RISE;
            case P_TICK: return android.os.VibrationEffect.Composition.PRIMITIVE_LOW_TICK;
            default: return android.os.VibrationEffect.Composition.PRIMITIVE_CLICK;
        }
    }

    private static android.os.VibrationEffect effectOf(Plan p) {
        if (p.pieces != null && android.os.Build.VERSION.SDK_INT >= 30) {
            android.os.VibrationEffect.Composition c = android.os.VibrationEffect.startComposition();
            for (int i = 0; i < p.pieces.length; i++) c.addPrimitive(primitive(p.pieces[i], p.tier), p.scales[i], p.delays[i]);
            return c.compose();
        }
        if (p.amps != null) return android.os.VibrationEffect.createWaveform(p.timings, p.amps, -1);
        return android.os.VibrationEffect.createWaveform(p.timings, -1);
    }

    private void vibrate(Channel c, Plan p) {
        String outcome = p.what;
        try {
            android.os.VibrationEffect fx = effectOf(p);
            if (c == phone) {
                android.os.Vibrator v = (android.os.Vibrator) c.motor;
                if (v == null) return;
                if (android.os.Build.VERSION.SDK_INT >= 33)
                    v.vibrate(fx, android.os.VibrationAttributes.createForUsage(android.os.VibrationAttributes.USAGE_MEDIA));
                else
                    v.vibrate(fx, new android.media.AudioAttributes.Builder()
                            .setUsage(android.media.AudioAttributes.USAGE_GAME).build());
            } else if (android.os.Build.VERSION.SDK_INT >= 31 && c.motor instanceof android.os.VibratorManager) {
                android.os.VibratorManager vm = (android.os.VibratorManager) c.motor;
                android.os.CombinedVibration.ParallelCombination all = android.os.CombinedVibration.startParallel();
                for (int id : c.motorIds) all.addVibrator(id, fx);          /* the same drawing on every motor */
                if (android.os.Build.VERSION.SDK_INT >= 33)
                    vm.vibrate(all.combine(), android.os.VibrationAttributes.createForUsage(android.os.VibrationAttributes.USAGE_MEDIA));
                else
                    vm.vibrate(all.combine());
            } else if (c.motor instanceof android.os.Vibrator) {
                ((android.os.Vibrator) c.motor).vibrate(fx);
            }
        } catch (RuntimeException e) {
            outcome = "REFUSED " + p.what + ": " + e;       /* a device gone mid-call, or a drawing it will not take */
        }
        if (say != null) addRing("  " + c.name + " plays " + outcome);
    }

    private void silence(Channel c) {
        try {
            if (c.motor instanceof android.os.Vibrator) ((android.os.Vibrator) c.motor).cancel();
            else if (android.os.Build.VERSION.SDK_INT >= 31 && c.motor instanceof android.os.VibratorManager)
                ((android.os.VibratorManager) c.motor).cancel();
        } catch (RuntimeException ignored) {
            /* the pad went away */
        }
    }

    /* ---- the report ---- */

    private void addRing(String line) {
        ring[ringAt] = line;
        ringAt = (ringAt + 1) % RING;
        if (ringNew < RING) ringNew++;
    }

    private void ringLine(long now, Channel c, int target, int source, int site, int from, float s, float t, String decision) {
        if (say == null) return;
        String what = source >= 0 && source < SOURCE.length ? SOURCE[source] : "source " + source;
        String device = target >= 0 ? "pad slot " + target : target == TARGET_KEYBOARD ? "keyboard" : "screen";
        addRing(String.format(java.util.Locale.ROOT, "%d %s on %s: %s site 0x%x%s S %.2f T %.3f -> %s", now, device,
                c != null ? c.name : "no motor",
                what, site,
                from != 0 ? String.format(java.util.Locale.ROOT, " from 0x%x", from) : "", s, t, decision));
    }

    private void note(Ev e, String line) {
        if (say == null) return;
        addRing(now() + " " + line);
    }

    /* at onPause: what happened since the last time, and who shook */
    private void dump() {
        if (say == null) return;
        if (ringNew > 0) {
            tell("ring: the last " + ringNew + " events" + (droppedPaused > 0 ? ", " + droppedPaused + " dropped while away" : ""));
            for (int i = ringNew; i > 0; i--) tell("  " + ring[(ringAt - i + RING) % RING]);
            ringNew = 0;
        }
        if (!whoShook.isEmpty()) {
            ArrayList<Map.Entry<Long, int[]>> list = new ArrayList<>(whoShook.entrySet());
            list.sort((a, b) -> b.getValue()[0] - a.getValue()[0]);
            StringBuilder w = new StringBuilder("who shook:");
            int shown = 0;
            for (Map.Entry<Long, int[]> en : list) {
                if (shown++ == 40) { w.append(" ..."); break; }
                w.append(String.format(java.util.Locale.ROOT, " 0x%x x%d %s,", en.getKey(), en.getValue()[0], whoShookClass.get(en.getKey())));
            }
            tell(w.toString());
        }
    }
}
