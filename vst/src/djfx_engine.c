/* djfx_engine.c — MPC DJ FX engine.
 *
 * One insert effect: isolator -> DJ filter -> beat repeat -> delay -> reverb -> flanger -> limiter,
 * plus an authored macro, a global wet/dry mix and an output trim.
 *
 * Implemented: T1 skeleton, T2 reused DubStation DSP, T3 own isolator + DJ filter, T5/T6 beat repeat,
 * T7 through-zero flanger, T8 limiter/output/panic + live VU, T9 macro. The skin is T10.
 *
 * Contract: wrapper/engine.h — interleaved int16 stereo, 128-frame blocks, 44.1 kHz. No allocation in
 * process(). KEYS must stay in step with vst/params.json (same order = VST parameter index).
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "engine.h"
#include "params.h"      /* generated: defines + PARAMS[] (NPARAMS, HAS_LFO_BPM, ...) */
#include "dsp_common.h"
#include "iso.h"
#include "dub_delay.h"
#include "dub_filter.h"
#include "dub_reverb.h"
#include "repeat.h"
#include "flanger.h"
#include "limiter.h"

#define MAXFRAMES 128
#define VALLEN 40
#define VU_STEPS 24
#define VU_FLOOR_DB (-48.0f)

static const char *const KEYS[] = {
    "iso_lo", "iso_mid", "iso_hi", "iso_lo_x", "iso_hi_x",
    "dj_filter", "dj_reso",
    "rep_mode", "rep_gate", "rep_chance", "rep_count", "rep_mix",
    "dly_time", "dly_sync", "dly_div", "dly_fbk", "dly_tone", "dly_pingpong",
    "verb_mode", "verb_amount", "verb_decay", "verb_damp", "verb_predelay",
    "fl_rate", "fl_depth", "fl_fbk", "fl_mix",
    "mac_lo", "mac_mid", "mac_hi", "mac_lpf", "mac_glitch", "mac_verb", "mac_fade", "mac_fill",
    "output_gain", "limiter", "panic", "vu", "mix"
};
#define NKEYS ((int)(sizeof KEYS / sizeof KEYS[0]))
_Static_assert(sizeof KEYS / sizeof KEYS[0] == NPARAMS,
               "KEYS must list every parameter in params.json, in the same order");

/* defaults must match vst/params.json */
static const char *const DEFS[NKEYS] = {
    "0", "0", "0", "250", "2500",
    "0", "0.2",
    "0", "0.25", "1", "4", "1",
    "350", "0", "3", "0.55", "0.4", "0",
    "0", "0.3", "0.5", "0.5", "20",
    "0.3", "0.5", "0.5", "0.5",
    "1", "1", "1", "1", "0", "0", "0", "0",
    "0", "1", "0", "0", "0.35"
};

enum {
    P_ISO_LO = 0, P_ISO_MID, P_ISO_HI, P_ISO_LO_X, P_ISO_HI_X,
    P_DJ_FILTER, P_DJ_RESO,
    P_REP_MODE, P_REP_GATE, P_REP_CHANCE, P_REP_COUNT, P_REP_MIX,
    P_DLY_TIME, P_DLY_SYNC, P_DLY_DIV, P_DLY_FBK, P_DLY_TONE, P_DLY_PINGPONG,
    P_VERB_MODE, P_VERB_AMOUNT, P_VERB_DECAY, P_VERB_DAMP, P_VERB_PREDELAY,
    P_FL_RATE, P_FL_DEPTH, P_FL_FBK, P_FL_MIX,
    P_MAC_LO, P_MAC_MID, P_MAC_HI, P_MAC_LPF, P_MAC_GLITCH, P_MAC_VERB, P_MAC_FADE, P_MAC_FILL,
    P_OUTPUT_GAIN, P_LIMITER, P_PANIC, P_VU, P_MIX
};

typedef struct {
    iso_t        *iso;
    dub_filter_t *djf;      /* the DJ filter (bipolar LPF/HPF sweep) */
    repeat_t     *rep;
    dub_delay_t  *delay;
    dub_reverb_t *rv;
    flanger_t    *fl;
    limiter_t    *lim;
    float fs;
    float bpm;              /* host tempo (HAS_LFO_BPM) */
    float mix;
    float out_gain;         /* linear */
    unsigned display_rev;   /* HAS_DISPLAY_REV */
    int   vu_step;          /* last reported VU step (-1 = never) */
    float vu_env;
    int   panic_cd;         /* panic debounce, in samples */
    int   panic_state;      /* panic flush staged across blocks (0 = idle, 1..3 = stage) */
    char  val[NKEYS][VALLEN];
    float l[MAXFRAMES], r[MAXFRAMES], w1[MAXFRAMES], w2[MAXFRAMES];
} ctx_t;

/* 12 sync divisions of a beat, in beats */
static const float DIV_BEATS[12] = { 4.0f, 2.0f, 3.0f, 1.0f, 1.5f, 0.5f, 0.75f, 1.0f / 3.0f,
                                     0.25f, 0.375f, 1.0f / 6.0f, 0.125f };
#define N_DIV 12

/* The macro layer is OUR design: the Ableton rack's macro->parameter mapping is not stored in the
 * .adg (every ModulationTarget Id is 0 - verified 2026-10-15), so the targets below were read from the
 * rack's Map view in Live and rebuilt here with the same devices. Each macro drives 1..N of our
 * parameters; values are written back into the parameter cells so the controls follow on screen. */
typedef struct { int param; float lo, hi; } macro_map_t;

static const macro_map_t MAP_LO[]     = { { P_ISO_LO,  -24.0f, 0.0f } };   /* measured: kill (-inf) .. 0 dB */
static const macro_map_t MAP_MID[]    = { { P_ISO_MID, -24.0f, 0.0f } };
static const macro_map_t MAP_HI[]     = { { P_ISO_HI,  -24.0f, 0.0f } };
/* LPF: measured 2026-10-15 in Live - macro 0 = 26.0 Hz (closed), 64 = 738 Hz, 127 = 19.9 kHz
 * (open). Frequency only; the original maps no resonance. Neutral default is therefore 1.0. */
static const macro_map_t MAP_LPF[]    = { { P_DJ_FILTER, -1.0f, 0.0f } };
/* GLITCH: the original blends a Grain Delay in (device off->on, spray 1->150 Hz, wet 0->35 %, grain
 * time 22->2 ms) - our nearest equivalent is the modulated short delay (flanger/comb) plus its rate */
static const macro_map_t MAP_GLITCH[] = { { P_FL_MIX, 0.0f, 0.35f }, { P_FL_DEPTH, 0.0f, 0.40f },
                                          { P_FL_RATE, 0.10f, 0.60f }, { P_MIX, 0.35f, 0.80f } };
/* VERB: the original brings in a Delay (on, fb 95 %, wet 40 %), an HPF sweep (26 Hz..15.4 kHz, reso up)
 * and the Reverb (dry/wet 0..100 %) */
static const macro_map_t MAP_VERB[]   = { { P_VERB_AMOUNT, 0.0f, 0.85f }, { P_VERB_PREDELAY, 20.0f, 35.0f },
                                          { P_DLY_FBK, 0.55f, 0.85f }, { P_MIX, 0.35f, 0.60f } };
/* FADE ("Fade to Grey"): measured 2026-10-15 - the band narrows onto ~2 kHz (low 50 Hz -> 2.03 kHz,
 * high 18 kHz -> 2.18 kHz), the mids dip to -6.2 dB and a Delay fades in at 95 % feedback / 95 % wet.
 * Our isolator shelves stand in for the moving EQ crossovers. */
static const macro_map_t MAP_FADE[]   = { { P_ISO_LO, 0.0f, -24.0f }, { P_ISO_HI, 0.0f, -24.0f },
                                          { P_ISO_MID, 0.0f, -6.0f }, { P_DLY_FBK, 0.55f, 0.95f },
                                          { P_MIX, 0.35f, 0.95f } };
/* AUTO-FILL: measured - only the Beat Repeat's Device On and Grid are macro-driven (Grid 1/6 -> 1/32 in
 * the original, ours Off .. 1/32). Gate/Chance keep their hand-set values (4/16 = 25 %, 100 %) - not mapped. */
static const macro_map_t MAP_FILL[]   = { { P_REP_MODE, 0.0f, 7.0f } };

typedef struct { const macro_map_t *m; int n; } macro_t;
#define N_MACROS 8
static const macro_t MACROS[N_MACROS] = {
    { MAP_LO, 1 }, { MAP_MID, 1 }, { MAP_HI, 1 }, { MAP_LPF, 1 },
    { MAP_GLITCH, 4 }, { MAP_VERB, 4 }, { MAP_FADE, 5 }, { MAP_FILL, 1 },
};

static int key_idx(const char *k)
{
    for (int i = 0; i < NKEYS; i++)
        if (!strcmp(k, KEYS[i])) return i;
    return -1;
}

static float effective_time_ms(ctx_t *c)
{
    float ms = (float)atof(c->val[P_DLY_TIME]);
    if (atoi(c->val[P_DLY_SYNC]) == 1 && c->bpm > 1.0f) {
        int d = atoi(c->val[P_DLY_DIV]);
        if (d < 0 || d >= N_DIV) d = 0;
        ms = DIV_BEATS[d] * 60000.0f / c->bpm;
    }
    return ms;
}

static void flush_tails(ctx_t *c)
{
    dub_delay_reset(c->delay);
    dub_reverb_reset(c->rv);
    repeat_reset(c->rep);
    flanger_reset(c->fl);
    iso_reset(c->iso);
    dub_filter_reset(c->djf);
}

/* the numeric part of a parameter change; the macro uses it too */
static void apply_scalar(ctx_t *c, int i, float f)
{
    switch (i) {
        case P_ISO_LO:     iso_set_lo_db(c->iso, f); break;
        case P_ISO_MID:    iso_set_mid_db(c->iso, f); break;
        case P_ISO_HI:     iso_set_hi_db(c->iso, f); break;
        case P_ISO_LO_X:   iso_set_lo_x(c->iso, f); break;
        case P_ISO_HI_X:   iso_set_hi_x(c->iso, f); break;

        case P_DJ_RESO:    dub_filter_set_resonance(c->djf, f); break;
        case P_DJ_FILTER: {
            const int on = (fabsf(f) > 0.001f);
            dub_filter_set_enable(c->djf, on);
            if (on) {
                if (f < 0.0f) {
                    const float t = -f;
                    dub_filter_set_hpf(c->djf, 20.0f);
                    dub_filter_set_lpf(c->djf, 20000.0f * powf(26.0f / 20000.0f, t));   /* 20k .. 26 Hz (as the original) */
                } else {
                    const float t = f;
                    dub_filter_set_lpf(c->djf, 20000.0f);
                    dub_filter_set_hpf(c->djf, 20.0f * powf(400.0f, t));     /* 20 .. 8000 Hz */
                }
            }
            break;
        }

        case P_REP_MODE:   repeat_set_mode(c->rep, (int)lrintf(f)); break;
        case P_REP_GATE:   repeat_set_gate(c->rep, f); break;
        case P_REP_CHANCE: repeat_set_chance(c->rep, f); break;
        case P_REP_COUNT:  repeat_set_count(c->rep, (int)lrintf(f)); break;
        case P_REP_MIX:    repeat_set_mix(c->rep, f); break;

        case P_DLY_TIME: case P_DLY_SYNC: case P_DLY_DIV:
            dub_delay_set_time_ms(c->delay, effective_time_ms(c)); break;
        case P_DLY_FBK:      dub_delay_set_feedback(c->delay, f); break;
        case P_DLY_TONE:     dub_delay_set_tone(c->delay, f); break;
        case P_DLY_PINGPONG: dub_delay_set_ping_pong(c->delay, (int)lrintf(f)); break;

        case P_VERB_MODE:      dub_reverb_set_mode(c->rv, (int)lrintf(f)); break;
        case P_VERB_AMOUNT:    dub_reverb_set_amount(c->rv, f); break;
        case P_VERB_DECAY:     dub_reverb_set_decay(c->rv, f); break;
        case P_VERB_DAMP:      dub_reverb_set_damping(c->rv, f); break;
        case P_VERB_PREDELAY:  dub_reverb_set_predelay(c->rv, db_clampf(f / 200.0f, 0.0f, 1.0f)); break;

        case P_FL_RATE:  flanger_set_rate(c->fl, f); break;
        case P_FL_DEPTH: flanger_set_depth(c->fl, f); break;
        case P_FL_FBK:   flanger_set_fbk(c->fl, f); break;
        case P_FL_MIX:   flanger_set_mix(c->fl, f); break;

        case P_OUTPUT_GAIN: c->out_gain = powf(10.0f, f / 20.0f); break;
        case P_LIMITER:     limiter_set_on(c->lim, (int)lrintf(f)); break;
        case P_PANIC:
            if (f > 0.5f && c->panic_cd <= 0) {
                c->panic_state = 1;                   /* the flush itself is staged over the next blocks */
                c->panic_cd = (int)(0.20f * c->fs);   /* debounce: one flush per 200 ms */
            }
            break;

        case P_MIX: c->mix = db_clampf(f, 0.0f, 1.0f); break;
        default: break;
    }
}

/* write one macro's targets and apply them */
static void apply_macro(ctx_t *c, int idx, float m)
{
    if (idx < 0 || idx >= N_MACROS) return;
    m = db_clampf(m, 0.0f, 1.0f);
    const macro_t *M = &MACROS[idx];
    for (int k = 0; k < M->n; k++) {
        const float v = M->m[k].lo + (M->m[k].hi - M->m[k].lo) * m;
        snprintf(c->val[M->m[k].param], VALLEN, "%g", (double)v);
        apply_scalar(c, M->m[k].param, v);
    }
}

static void apply(ctx_t *c, int i)
{
    const float f = (float)atof(c->val[i]);
    if (i >= P_MAC_LO && i <= P_MAC_FILL) apply_macro(c, i - P_MAC_LO, f);
    else                                  apply_scalar(c, i, f);
}

static void set_state(ctx_t *c, const char *s)
{
    const char *p = s;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const int linelen = nl ? (int)(nl - p) : (int)strlen(p);
        const char *eq = (const char *)memchr(p, '=', (size_t)linelen);
        if (eq) {
            const int kl = (int)(eq - p);
            if (kl > 0 && kl < 64) {
                char key[64];
                memcpy(key, p, (size_t)kl);
                key[kl] = 0;
                const int i = key_idx(key);
                if (i >= 0) {
                    int vl = linelen - (kl + 1);
                    if (vl < 0) vl = 0;
                    if (vl > VALLEN - 1) vl = VALLEN - 1;
                    memcpy(c->val[i], eq + 1, (size_t)vl);
                    c->val[i][vl] = 0;
                    /* A macro is stored for the panel, but NOT re-applied on load: it owns its
                     * targets, and re-running it would overwrite the target values that were saved
                     * beside it (same reasoning as DubStation's stored `preset` selection). */
                    if (!(i >= P_MAC_LO && i <= P_MAC_FILL)) apply(c, i);
                }
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static void build_dsp(ctx_t *c)
{
    c->iso   = iso_create(c->fs);
    c->djf   = dub_filter_create(c->fs);
    c->rep   = repeat_create(c->fs);
    c->delay = dub_delay_create(c->fs);
    c->rv    = dub_reverb_create(c->fs);
    c->fl    = flanger_create(c->fs);
    c->lim   = limiter_create(c->fs);
    if (c->delay) {
        dub_delay_set_filter(c->delay, NULL);
        dub_delay_set_wet(c->delay, 1.0f);
        dub_delay_set_dry(c->delay, 0.0f);
    }
    if (c->rv) dub_reverb_set_enable(c->rv, 1);
    if (c->lim) limiter_set_ceiling(c->lim, -0.3f);
}

static void free_dsp(ctx_t *c)
{
    iso_destroy(c->iso);
    dub_filter_destroy(c->djf);
    repeat_destroy(c->rep);
    dub_delay_destroy(c->delay);
    dub_reverb_destroy(c->rv);
    flanger_destroy(c->fl);
    limiter_destroy(c->lim);
    c->iso = NULL; c->djf = NULL; c->rep = NULL; c->delay = NULL;
    c->rv = NULL; c->fl = NULL; c->lim = NULL;
}

static int dsp_ok(const ctx_t *c)
{
    return c->iso && c->djf && c->rep && c->delay && c->rv && c->fl && c->lim;
}

static void *create(const char *data_dir)
{
    (void)data_dir;
    ctx_t *c = (ctx_t *)calloc(1, sizeof *c);
    if (!c) return NULL;
    c->fs = 44100.0f;
    c->mix = 0.35f;
    c->out_gain = 1.0f;
    c->vu_step = -1;
    build_dsp(c);
    if (!dsp_ok(c)) { free_dsp(c); free(c); return NULL; }
    for (int i = 0; i < NKEYS; i++) {
        snprintf(c->val[i], VALLEN, "%s", DEFS[i]);
        apply(c, i);
    }
    return c;
}

static void destroy(void *inst)
{
    ctx_t *c = inst;
    if (!c) return;
    free_dsp(c);
    free(c);
}

static void midi(void *inst, const uint8_t *msg, int len) { (void)inst; (void)msg; (void)len; }

static void engine_set_sample_rate(void *inst, float fs);
static void engine_reset(void *inst);

static void set_param(void *inst, const char *key, const char *val)
{
    ctx_t *c = inst;
    if (!c) return;
    if (!strcmp(key, "state")) { set_state(c, val); return; }
    if (!strcmp(key, "sample_rate")) { engine_set_sample_rate(c, (float)atof(val)); return; }
    if (!strcmp(key, "reset")) { engine_reset(c); return; }
    if (!strcmp(key, "lfo_bpm")) { c->bpm = (float)atof(val); repeat_set_bpm(c->rep, c->bpm); apply(c, P_DLY_TIME); return; }
    const int i = key_idx(key);
    if (i < 0) return;
    snprintf(c->val[i], VALLEN, "%s", val);
    apply(c, i);
}

static int get_param(void *inst, const char *key, char *buf, int buf_len)
{
    ctx_t *c = inst;
    if (!c || buf_len <= 0) return 0;
    if (!strcmp(key, "state")) {
        int p = 0;
        for (int i = 0; i < NKEYS; i++) {
            const int n = snprintf(buf + p, (size_t)(buf_len - p), "%s=%s\n", KEYS[i], c->val[i]);
            if (n < 0 || n >= buf_len - p) break;
            p += n;
        }
        return p;
    }
    const int i = key_idx(key);
    if (i < 0) {
        if (!strcmp(key, "display_rev")) {
            /* the live VU is display-only: derive its step here (on the poll) and count it into
             * display_rev so the host re-reads the value (DISPLAY_REV_NO_UPDATE: moved values only) */
            int s = (int)lrintf((20.0f * log10f(c->vu_env + 1e-6f) - VU_FLOOR_DB)
                                / (0.0f - VU_FLOOR_DB) * (float)(VU_STEPS - 1));
            if (s < 0) s = 0;
            if (s >= VU_STEPS) s = VU_STEPS - 1;
            if (s != c->vu_step) {
                c->vu_step = s;
                snprintf(c->val[P_VU], VALLEN, "%d", s);
                c->display_rev++;
            }
            snprintf(buf, (size_t)buf_len, "%u", c->display_rev);
            return 1;
        }
        const size_t klen = strlen(key);
        if (klen > 8 && !strcmp(key + klen - 8, "_display")) {
            char base[64];
            const size_t bl = klen - 8;
            if (bl >= sizeof base) return 0;
            memcpy(base, key, bl);
            base[bl] = 0;
            if (!strcmp(base, "dly_time")) {
                const float ms = effective_time_ms(c);
                char t[24];
                if (ms < 1000.0f) snprintf(t, sizeof t, "%.0f ms", ms);
                else              snprintf(t, sizeof t, "%.2f s", ms / 1000.0f);
                if (atoi(c->val[P_DLY_SYNC]) == 1 && c->bpm > 1.0f) {
                    const int d = atoi(c->val[P_DLY_DIV]);
                    const char *lbl = (d >= 0 && d < PARAMS[P_DLY_DIV].nopts) ? PARAMS[P_DLY_DIV].opts[d] : "";
                    snprintf(buf, (size_t)buf_len, "%s (%s)", t, lbl);
                } else {
                    snprintf(buf, (size_t)buf_len, "%s", t);
                }
                return 1;
            }
            const int bi = key_idx(base);
            if (bi < 0) return 0;
            snprintf(buf, (size_t)buf_len, "%.0f %%", (float)atof(c->val[bi]) * 100.0f);
            return 1;
        }
        return 0;
    }
    snprintf(buf, (size_t)buf_len, "%s", c->val[i]);
    return 1;
}

static void render(void *inst, int16_t *out_lr, int frames)
{
    (void)inst;
    if (frames < 0) frames = 0;
    if (frames > MAXFRAMES) frames = MAXFRAMES;
    memset(out_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
}

static void process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames)
{
    ctx_t *c = inst;
    if (!c || frames <= 0) return;
    if (frames > MAXFRAMES) frames = MAXFRAMES;

    for (int i = 0; i < frames; i++) {
        c->l[i] = (float)in_lr[2 * i]     * (1.0f / 32768.0f);
        c->r[i] = (float)in_lr[2 * i + 1] * (1.0f / 32768.0f);
    }

    /* full-signal section: isolator -> DJ filter -> beat repeat */
    iso_process(c->iso, c->l, c->r, frames);
    dub_filter_process(c->djf, c->l, c->r, frames);
    repeat_process(c->rep, c->l, c->r, frames);

    /* wet path: delay (wet only) -> reverb -> flanger */
    dub_delay_process(c->delay, c->l, c->r, c->w1, c->w2, frames);
    dub_reverb_process(c->rv, c->w1, c->w2, frames);
    flanger_process(c->fl, c->w1, c->w2, frames);

    const float wet = c->mix, dry = 1.0f - c->mix;
    float pk = 0.0f;
    for (int i = 0; i < frames; i++) {
        c->l[i] = db_sanitize(c->l[i] * dry + c->w1[i] * wet) * c->out_gain;
        c->r[i] = db_sanitize(c->r[i] * dry + c->w2[i] * wet) * c->out_gain;
        const float al = fabsf(c->l[i]), ar = fabsf(c->r[i]);
        if (al > pk) pk = al;
        if (ar > pk) pk = ar;
    }
    limiter_process(c->lim, c->l, c->r, frames);

    /* panic: clear the module buffers in stages, one module group per block, so a single block never
     * memsets several MB (delay + repeat buffers are ~2.8 + ~1.8 MB, which would blow the block budget
     * on ARM). The output is silenced while flushing - that is what panic means. */
    if (c->panic_state) {
        switch (c->panic_state) {
            case 1: iso_reset(c->iso); dub_filter_reset(c->djf); flanger_reset(c->fl); dub_reverb_reset(c->rv); break;
            case 2: repeat_reset(c->rep); break;
            default: dub_delay_reset(c->delay); break;
        }
        c->panic_state = (c->panic_state >= 3) ? 0 : c->panic_state + 1;
        memset(out_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }

    for (int i = 0; i < frames; i++) {
        long a = lrintf(db_hard_clip(c->l[i], 1.0f) * 32768.0f);
        long b = lrintf(db_hard_clip(c->r[i], 1.0f) * 32768.0f);
        if (a > 32767) a = 32767; else if (a < -32768) a = -32768;
        if (b > 32767) b = 32767; else if (b < -32768) b = -32768;
        out_lr[2 * i]     = (int16_t)a;
        out_lr[2 * i + 1] = (int16_t)b;
    }

    /* master VU: peak with a slow release (sampled by the 100 ms display poll) */
    c->vu_env = pk > c->vu_env ? pk : c->vu_env * 0.9995f;
    if (c->panic_cd > 0) { c->panic_cd -= frames; if (c->panic_cd < 0) c->panic_cd = 0; }
}

static void engine_set_sample_rate(void *inst, float fs)
{
    ctx_t *c = inst;
    if (!c || !(fs > 8000.0f && fs < 192000.0f) || fabsf(fs - c->fs) < 0.5f) return;
    free_dsp(c);
    c->fs = fs;
    build_dsp(c);
    if (!dsp_ok(c)) return;
    for (int i = 0; i < NKEYS; i++) apply(c, i);
}

static void engine_reset(void *inst)
{
    ctx_t *c = inst;
    if (!c) return;
    flush_tails(c);
    limiter_reset(c->lim);
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, process,
                                     NULL /* song_pos: HAS_SONG_POS unset */ };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
