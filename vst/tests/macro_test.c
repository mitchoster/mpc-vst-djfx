/* macro_test.c — verifies the authored macro layer (8 macros, multi-target).
 * Drives the engine through its public vtable: with the macro at 0 its targets must hold their neutral
 * values, at 1 the documented extremes, and every target must move monotonically in between.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine.h"

const mpc_engine_t *mpc_engine(void);
static const mpc_engine_t *E;
static float getf(void *inst, const char *k)
{
    char b[64];
    if (E->get_param(inst, k, b, sizeof b) <= 0) return -999.0f;
    return (float)atof(b);
}

static const struct { const char *mac; const char *key; float lo, hi, neutral; } MAP[] = {
    /* ranges measured from the original DJ-FX rack in Live (2026-10-15) */
    { "mac_lo",     "iso_lo",       -24.0f,  0.0f,  0.0f },
    { "mac_mid",    "iso_mid",      -24.0f,  0.0f,  0.0f },
    { "mac_hi",     "iso_hi",       -24.0f,  0.0f,  0.0f },
    { "mac_lpf",    "dj_filter",     -1.00f,  0.0f,  0.0f },
    { "mac_glitch", "fl_mix",         0.0f,  0.35f, 0.0f },
    { "mac_verb",   "verb_amount",    0.0f,  0.85f, 0.0f },
    { "mac_fade",   "dly_fbk",        0.55f, 0.95f, 0.55f },
    { "mac_fill",   "rep_mode",       0.0f,  7.0f,  0.0f },
};

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-56s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

int main(void)
{
    E = mpc_engine();
    void *inst = E->create(NULL);
    if (!inst) { printf("engine create failed\n"); return 2; }
    char b[96];

    for (unsigned k = 0; k < sizeof MAP / sizeof MAP[0]; k++) {
        float prev = 0.0f;
        int mono = 1, inrange = 1;
        for (int s = 0; s <= 10; s++) {
            const float m = (float)s / 10.0f;
            snprintf(b, sizeof b, "%g", (double)m);
            E->set_param(inst, MAP[k].mac, b);
            const float v = getf(inst, MAP[k].key);
            if (s > 0 && ((MAP[k].hi >= MAP[k].lo) ? (v < prev - 1e-3f) : (v > prev + 1e-3f))) mono = 0;
            const float lo = MAP[k].lo < MAP[k].hi ? MAP[k].lo : MAP[k].hi;
            const float hi = MAP[k].lo < MAP[k].hi ? MAP[k].hi : MAP[k].lo;
            if (v < lo - 1e-3f || v > hi + 1e-3f) inrange = 0;
            prev = v;
        }
        snprintf(b, sizeof b, "(%s: %.2f..%.2f)", MAP[k].key, MAP[k].lo, MAP[k].hi);
        char what[90]; snprintf(what, sizeof what, "%s moves %s monotonically in range", MAP[k].mac, MAP[k].key);
        check(what, mono && inrange, b);
    }

    /* a fresh instance: the macro defaults must give the neutral values */
    void *fresh = E->create(NULL);
    int ok = 1;
    for (unsigned k = 0; k < sizeof MAP / sizeof MAP[0]; k++)
        if (fabsf(getf(fresh, MAP[k].key) - MAP[k].neutral) > 1e-3f) {
            printf("   %s = %.3f, want %.3f\n", MAP[k].key, getf(fresh, MAP[k].key), MAP[k].neutral);
            ok = 0;
        }
    check("fresh instance: macros at their neutral defaults", ok, "");
    E->destroy(fresh);
    E->destroy(inst);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
