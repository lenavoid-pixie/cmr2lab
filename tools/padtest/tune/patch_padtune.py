#!/usr/bin/env python3
"""patch_padtune.py -- put the tuning seam and the two tiers into the input layer.

One edit, applied to port/platform/deck_dinput.cpp. Idempotent: refuses to apply
twice. Everything it writes is in the file it names, and the anchors are exact
strings so a drift in the source is an error, not a silent no-op.

  python3 patch_padtune.py           apply
  python3 patch_padtune.py --check   say whether it is applied, change nothing
"""
import sys, hashlib

SRC = "/home/deck/lena/.lena_cmr2/port/platform/deck_dinput.cpp"
PRE_MD5 = "8adf8f0000ca563e069bc9366bc5e006"   # the file INPUTGEN published

MARK = "THE TUNING SEAM"

ANCHOR = "static int g_axis_saturation[PAD_AXES]= { 10000,10000,10000,10000,10000,10000,10000,10000 };\n"

BLOCK = r'''
/* ========================== THE TUNING SEAM -- the two tiers =============
 *
 * WHY THIS IS THE INPUT LAYER'S BUSINESS AND NOT A UI TASK.
 * The car does not shape its own axes. StageObjects.cpp:12707-12718 reads the
 * steering axis and does this, with nothing in between:
 *
 *     raw  = ((int *)pDev)[axisSteer * 5 + 0x11f];
 *     half = raw <= -1 ? -raw : raw;
 *     ... FixMulShift32(half, 0x3f0000)      -> flag0x1d0[0] / flag0x1d0[1]
 *
 * and the throttle and brake path a few lines below is the same shape: linear
 * in `raw`, with no deadzone, no curve, no saturation and no threshold of its
 * own. So every deadzone, every response curve and every saturation the car
 * will ever feel has to be applied HERE, on the way out of this file, or it
 * does not exist anywhere in the program. That is why this is not a feature
 * that can be bolted on later: there is no other seam to bolt it to.
 *
 * The one shaping the game does do is a deadzone of its own -- 0xc8, 2 % of
 * full scale, set on every axis it finds at Input.cpp:966 through
 * DIPROP_DEADZONE. DirectInput's contract is that the DEVICE applies it. The
 * device is this file. So the game's deadzone arrives here as a number and we
 * are the ones who honour it, in axis_scaled() below.
 *
 * THE WHOLE SEAM IS TWO FUNCTIONS.
 *   axis_scaled(idx, trigger)  every analogue value the game ever sees, one
 *                              call per axis. Deadzone, curve, saturation,
 *                              range, in that order.
 *   deck_pedal_axis()          the game's ONE combined pedal axis: literally
 *                              axis_scaled(L2) - axis_scaled(R2). The
 *                              per-trigger shaping below therefore reaches the
 *                              pedal for free instead of needing a second
 *                              code path that would then drift from this one.
 * Nothing else in this file writes a value the game reads back as an axis.
 * That is a checkable claim, not a comment: the DIJOYSTATE fill, the trace and
 * the pedal synthesis are the only consumers, and they all go through these.
 *
 * THE TWO TIERS, AND THE CONSTRAINT THE OWNER NAMED HIMSELF.
 * "I don't want to make it too confusing" -- so depth is available and never
 * compulsory, and that is enforced rather than promised:
 *
 *   EZ        what happens when nobody tuned anything, because it has to be
 *             what HAPPENS and not a mode somebody has to find. In EZ this
 *             file reads no per-axis knob at all -- the read sits behind the
 *             tier test, not behind a "did they set one" test -- so a player
 *             who never opens a tuning screen cannot be affected by one
 *             existing. A tuning screen cannot get between anyone and the
 *             start line if the code never looks at what it would write.
 *   ADVANCED  deadzone, response curve and saturation per axis; the two
 *             triggers separately; the combined pedal inherits both. Off
 *             unless asked for, by DECK_PAD_TIER=advanced.
 *
 * WHAT EZ ACTUALLY IS, HONESTLY.
 * pad_tune_ez() returns the game's own numbers: the deadzone the game itself
 * asks for, saturation at 10000, and a linear curve. That is exactly what this
 * port did before this block existed -- deliberately. EZ has to be a DECISION
 * and not a second behaviour, and the only deadzone number with evidence
 * behind it is the one the game asks for. There is no stage to drive yet, so
 * there is no measurement that could justify a different deadzone or a bend in
 * the curve, and inventing one would be taste dressed up as engineering.
 * When a stage exists, pad_tune_ez() is the ONE place the answer changes, and
 * the knob table below is how a candidate gets tested against it without
 * touching it. That is the whole point of building the seam before the answer.
 *
 * KNOBS -- all optional, none read in EZ, all clamped with a message if they
 * are out of range:
 *   DECK_PAD_TIER=ez|advanced            default ez
 *   DECK_PAD_STEER_DEADZONE=0..10000     both horizontal stick axes
 *   DECK_PAD_STEER_CURVE=0..200          100 linear, <100 soft near centre
 *   DECK_PAD_STEER_SAT=1..10000          where steering reaches full lock
 *   DECK_PAD_TRIGGER_L_DEADZONE=0..10000 the brake trigger, alone
 *   DECK_PAD_TRIGGER_R_DEADZONE=0..10000 the throttle trigger, alone
 *   DECK_PAD_TRIGGER_L_CURVE / _R_CURVE / _L_SAT / _R_SAT
 *   DECK_PAD_STICK_LX|LY|RX|RY_DEADZONE / _CURVE / _SAT
 *   DECK_PAD_AXIS0..7_DEADZONE / _CURVE / _SAT     raw slot, wins over an alias
 *
 * NOT HERE: VIBRATION. It is not an axis, it is the IDirectInputEffect path,
 * and this device answers CreateEffect() with DIERR_UNSUPPORTED on purpose --
 * an effect object that silently does nothing would make the game believe
 * rumble had been configured. Its seam is that function, not this one.
 * ======================================================================== */

#define PAD_TIER_EZ   0
#define PAD_TIER_ADV  1

#define PAD_CURVE_LINEAR 100
#define PAD_EZ_DEADZONE  200      /* the game's own 0xc8, Input.cpp:966 */

typedef struct { int deadzone, saturation, curve; } PadTune;

static int g_tier = -1;
static int pad_tier(void)
{
    if (g_tier < 0) {
        const char *e = getenv("DECK_PAD_TIER");
        if (!e || !*e || !strcmp(e, "ez") || !strcmp(e, "EZ"))
            g_tier = PAD_TIER_EZ;
        else if (!strcmp(e, "advanced") || !strcmp(e, "adv"))
            g_tier = PAD_TIER_ADV;
        else {
            fprintf(stderr, "[DIN] pad: DECK_PAD_TIER=%s is not ez|advanced "
                            "-- using ez\n", e);
            fflush(stderr);
            g_tier = PAD_TIER_EZ;
        }
    }
    return g_tier;
}

/* THE EZ ANSWER. One function, and everything a player who touches nothing
 * will ever feel comes out of it. Change the answer here or not at all. */
static void pad_tune_ez(int idx, PadTune *t)
{
    int dz  = g_axis_deadzone[idx];
    int sat = g_axis_saturation[idx];
    t->deadzone   = (dz  > 0) ? dz  : PAD_EZ_DEADZONE;
    t->saturation = (sat > 0) ? sat : 10000;
    t->curve      = PAD_CURVE_LINEAR;
}

/* ---- the response curve, and why it is not a pow() ----------------------
 * `curve` is a percentage: 100 is linear, below that softens the first part of
 * the throw, above it sharpens it.
 *
 *     y = a*(1000-k)/1000 + k*a*a/1000000,  k = curve - 100  (-100..100)
 *
 * Monotone across the whole range (the derivative never goes negative, at
 * either end of k) and exact at both endpoints: a=0 -> 0, a=1000 -> 1000. So a
 * curve can never invent travel at the centre or clip it at the edge. Integer
 * only, which matters here: this runs once per axis per poll, and a float
 * whose result depended on the build's FPU flags would make two builds of the
 * same source disagree about the car's steering. */
static long tune_curve(long a, int curve)
{
    int k;
    if (a <= 0) return 0;
    if (curve <= 0) curve = 0;
    if (curve > 200) curve = 200;
    if (curve == PAD_CURVE_LINEAR) return a;
    k = curve - PAD_CURVE_LINEAR;
    if (a > 1000) a = 1000;
    long y = (a * (1000 - k)) / 1000 + ((long)k * a * a) / 1000000;
    if (y < 0)    y = 0;
    if (y > 1000) y = 1000;
    return y;
}

/* alias -> the named axis slots it covers. STEER covers both horizontal stick
 * axes on purpose: which one the game's binding table calls "steering" is the
 * game's business (CInput::GetEnabledControllerAxisBinding(slot, 0)) and
 * "steering" is the player's word for the thing, so the knob follows the word.
 * DECK_PAD_AXIS<n>_* is there for anyone who wants exactly one slot and no
 * alias -- and it wins, because it is the more specific ask. */
typedef struct { const char *alias; int a, b; } TuneAlias;
static const TuneAlias TUNE_ALIAS[] = {
    { "STEER",     PAD_AXIS_LEFTX,         PAD_AXIS_RIGHTX         },
    { "STICK_LX",  PAD_AXIS_LEFTX,         -1 },
    { "STICK_LY",  PAD_AXIS_LEFTY,         -1 },
    { "STICK_RX",  PAD_AXIS_RIGHTX,        -1 },
    { "STICK_RY",  PAD_AXIS_RIGHTY,        -1 },
    { "TRIGGER_L", PAD_AXIS_LEFT_TRIGGER,  -1 },
    { "TRIGGER_R", PAD_AXIS_RIGHT_TRIGGER, -1 },
    { 0, 0, 0 }
};

/* -1 means the knob was not set. Read once, not per poll: the tier is fixed
 * for the process and an env lookup in the axis path is a cost with no
 * matching benefit. */
static int  g_knob[PAD_AXES][3];
static int  g_knob_ready = 0;

static int knob_num(const char *alias, const char *what, int lo, int hi)
{
    char name[64];
    const char *e;
    int v;
    snprintf(name, sizeof name, "DECK_PAD_%s_%s", alias, what);
    e = getenv(name);
    if (!e || !*e) return -1;
    v = atoi(e);
    if (v < lo || v > hi) {
        fprintf(stderr, "[DIN] pad: %s=%d is outside %d..%d -- ignored\n",
                name, v, lo, hi);
        fflush(stderr);
        return -1;
    }
    return v;
}

static void knob_read_all(void)
{
    int i, t, j;
    if (g_knob_ready) return;
    g_knob_ready = 1;
    for (i = 0; i < PAD_AXES; i++)
        g_knob[i][0] = g_knob[i][1] = g_knob[i][2] = -1;

    for (t = 0; TUNE_ALIAS[t].alias; t++) {
        const char *n = TUNE_ALIAS[t].alias;
        int dz = knob_num(n, "DEADZONE", 0, 10000);
        int cu = knob_num(n, "CURVE",    0, 200);
        int sa = knob_num(n, "SAT",      1, 10000);
        if (dz < 0 && cu < 0 && sa < 0) continue;
        for (j = 0; j < 2; j++) {
            int idx = j ? TUNE_ALIAS[t].b : TUNE_ALIAS[t].a;
            if (idx < 0) continue;
            if (dz >= 0) g_knob[idx][0] = dz;
            if (cu >= 0) g_knob[idx][1] = cu;
            if (sa >= 0) g_knob[idx][2] = sa;
        }
    }

    for (i = 0; i < PAD_AXES; i++) {
        char raw[16];
        int dz, cu, sa;
        snprintf(raw, sizeof raw, "AXIS%d", i);
        dz = knob_num(raw, "DEADZONE", 0, 10000);
        cu = knob_num(raw, "CURVE",    0, 200);
        sa = knob_num(raw, "SAT",      1, 10000);
        if (dz >= 0) g_knob[i][0] = dz;
        if (cu >= 0) g_knob[i][1] = cu;
        if (sa >= 0) g_knob[i][2] = sa;
    }
}

static void pad_tune_for(int idx, PadTune *t);   /* used by the trace below */

static void pad_tune_trace(void)
{
    static int done = 0;
    const char *e;
    PadTune t;
    int i;
    if (done) return;
    done = 1;                 /* set before the loop: pad_tune_for calls back */
    e = getenv("DECK_DIN_TRACE");
    if (!e || atoi(e) < 1) return;
    fprintf(stderr, "[DIN] pad: tier=%s%s\n",
            pad_tier() == PAD_TIER_ADV ? "advanced" : "ez",
            pad_tier() == PAD_TIER_ADV ? "" : "  (reads no per-axis knob)");
    for (i = 0; i < PAD_AXES; i++) {
        pad_tune_for(i, &t);
        fprintf(stderr, "[DIN] pad: axis%d deadzone=%d curve=%d sat=%d\n",
                i, t.deadzone, t.curve, t.saturation);
    }
    fflush(stderr);
}

/* The one call axis_scaled() makes. EZ stops on the first line and never looks
 * at a knob -- that is the guarantee, and it is enforced here rather than left
 * to a convention that the next writer might not follow. */
static void pad_tune_for(int idx, PadTune *t)
{
    pad_tune_trace();
    pad_tune_ez(idx, t);
    if (pad_tier() == PAD_TIER_EZ) return;
    knob_read_all();
    if (g_knob[idx][0] >= 0) t->deadzone   = g_knob[idx][0];
    if (g_knob[idx][1] >= 0) t->curve      = g_knob[idx][1];
    if (g_knob[idx][2] >= 0) t->saturation = g_knob[idx][2];
}
'''

OLD_DZ = """    long a = n < 0 ? -n : n;
    int dz = g_axis_deadzone[idx];                    /* 0..10000 of full scale */
    int sat = g_axis_saturation[idx];
"""
NEW_DZ = """    PadTune t; pad_tune_for(idx, &t);
    long a = n < 0 ? -n : n;
    int dz  = t.deadzone;                             /* 0..10000 of full scale */
    int sat = t.saturation;
"""

OLD_SAT = "    /* saturation = the position, as a fraction of full travel in 1/10000, at\n"
NEW_SAT = ("    a = tune_curve(a, t.curve);\n"
           "    /* saturation = the position, as a fraction of full travel in 1/10000, at\n")

OLD_PEDAL = """    int lt = trigger_pct(PAD_AXIS_LEFT_TRIGGER);    /* the brake, here        */
    int rt = trigger_pct(PAD_AXIS_RIGHT_TRIGGER);   /* the throttle, here     */
    if (lt < g_pedal_pct && rt < g_pedal_pct)
        return axis_scaled(PAD_AXIS_LEFTY, 0);   /* neither touched: stick owns it */
"""
NEW_PEDAL = """    PadTune tl, tr;
    int lt = trigger_pct(PAD_AXIS_LEFT_TRIGGER);    /* the brake, here        */
    int rt = trigger_pct(PAD_AXIS_RIGHT_TRIGGER);   /* the throttle, here     */
    /* "Touched" is decided by the trigger's own dead zone when one has been
     * asked for, and never below DECK_PAD_PEDAL_PCT. In EZ the dead zone is
     * the game's 2 % and the pedal floor is 4 %, so the floor wins and this is
     * the behaviour it always had -- the tuned dead zone can only ever widen
     * the gate, which is what makes a knob that says "ignore the first 8 % of
     * this trigger" true of the pedal as well as of the axis. */
    pad_tune_for(PAD_AXIS_LEFT_TRIGGER,  &tl);
    pad_tune_for(PAD_AXIS_RIGHT_TRIGGER, &tr);
    int floor_pct = g_pedal_pct;
    if (tl.deadzone / 100 > floor_pct) floor_pct = tl.deadzone / 100;
    if (tr.deadzone / 100 > floor_pct) floor_pct = tr.deadzone / 100;
    if (lt < floor_pct && rt < floor_pct)
        return axis_scaled(PAD_AXIS_LEFTY, 0);   /* neither touched: stick owns it */
"""


def main():
    src = open(SRC).read()

    if MARK in src:
        print("already applied -- nothing to do")
        return 0
    if "--check" in sys.argv:
        print("not applied")
        return 0

    md5 = hashlib.md5(src.encode()).hexdigest()
    if md5 != PRE_MD5:
        raise SystemExit("deck_dinput.cpp is %s, expected %s -- someone else has "
                         "edited it. Look before patching." % (md5, PRE_MD5))

    for needle, label in ((ANCHOR, "saturation defaults"),
                          (OLD_DZ, "axis_scaled deadzone lines"),
                          (OLD_SAT, "axis_scaled saturation comment"),
                          (OLD_PEDAL, "deck_pedal_axis threshold")):
        if src.count(needle) != 1:
            raise SystemExit("anchor %r (%s) matched %d times, expected 1"
                             % (needle[:40], label, src.count(needle)))

    src = src.replace(ANCHOR, ANCHOR + BLOCK, 1)
    src = src.replace(OLD_DZ, NEW_DZ, 1)
    src = src.replace(OLD_SAT, NEW_SAT, 1)
    src = src.replace(OLD_PEDAL, NEW_PEDAL, 1)

    open(SRC, "w").write(src)
    print("applied -> %s" % SRC)
    print("new md5: %s" % hashlib.md5(src.encode()).hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
