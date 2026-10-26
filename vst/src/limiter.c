/* limiter.c - MPC DJ FX look-ahead brickwall limiter (own code; see include/limiter.h). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "limiter.h"

#define LIM_LOOKAHEAD_MS 2.0f
#define LIM_RELEASE_MS   80.0f    /* the gain recovers slowly after a peak */

struct limiter {
    float fs;
    int   on;
    float ceiling;      /* linear */
    float att, rel;     /* per-sample coefficients (release; attack kept for reference) */
    float env;          /* peak envelope, instant attack + slow release */
    float gain;         /* current gain reduction */
    int   la;           /* look-ahead in samples */
    int   pos;
    float *buf[2];
    float *gbuf;        /* the gain computed when each sample entered the delay line */
};

static void alloc_buf(limiter_t *l, float fs)
{
    l->la = (int)(LIM_LOOKAHEAD_MS * 0.001f * fs);
    if (l->la < 1) l->la = 1;
    for (int ch = 0; ch < 2; ch++) {
        free(l->buf[ch]);
        l->buf[ch] = (float *)calloc((size_t)l->la + 1, sizeof(float));
    }
    free(l->gbuf);
    l->gbuf = (float *)calloc((size_t)l->la + 1, sizeof(float));
    l->att = 1.0f - expf(-3.0f / (float)l->la);   /* the gain reaches the target inside the look-ahead */
    l->rel = 1.0f - expf(-1.0f / (LIM_RELEASE_MS * 0.001f * fs));
    l->fs  = fs;
    l->pos = 0;
}

limiter_t *limiter_create(float fs)
{
    limiter_t *l = (limiter_t *)calloc(1, sizeof *l);
    if (!l) return NULL;
    l->on = 1;
    l->ceiling = 0.98f;
    l->gain = 1.0f;
    l->env = 0.0f;
    alloc_buf(l, fs > 0.0f ? fs : 44100.0f);
    if (!l->buf[0] || !l->buf[1] || !l->gbuf) { limiter_destroy(l); return NULL; }
    return l;
}

void limiter_destroy(limiter_t *l)
{
    if (!l) return;
    free(l->buf[0]);
    free(l->buf[1]);
    free(l->gbuf);
    free(l);
}

void limiter_reset(limiter_t *l)
{
    if (!l) return;
    for (int ch = 0; ch < 2; ch++)
        if (l->buf[ch]) memset(l->buf[ch], 0, ((size_t)l->la + 1) * sizeof(float));
    if (l->gbuf) memset(l->gbuf, 0, ((size_t)l->la + 1) * sizeof(float));
    l->gain = 1.0f;
    l->env = 0.0f;
    l->pos = 0;
}

void limiter_set_sample_rate(limiter_t *l, float fs)
{
    if (!l || fs <= 0.0f || fabsf(fs - l->fs) < 0.5f) return;
    alloc_buf(l, fs);
    limiter_reset(l);
}

void limiter_set_on(limiter_t *l, int on) { if (l) l->on = on ? 1 : 0; }
void limiter_set_ceiling(limiter_t *l, float db)
{
    if (!l) return;
    if (db > 0.0f) db = 0.0f;
    l->ceiling = powf(10.0f, db / 20.0f);
}

void limiter_process(limiter_t *l, float *left, float *right, int n)
{
    if (!l || n <= 0) return;
    if (!l->on) return;                       /* bypass */
    const int la = l->la;
    for (int i = 0; i < n; i++) {
        const float infl = left[i], inr = right[i];
        /* peak envelope: instant attack, slow release. The look-ahead delay below lets the envelope
         * be in place before a peak reaches the output, and a signal peak is not forgotten after one
         * sample (a target-only per-sample gain would recover immediately on an impulse). */
        float peak = fabsf(infl);
        const float ar = fabsf(inr);
        if (ar > peak) peak = ar;
        if (peak > l->env) l->env = peak;
        else               l->env *= (1.0f - l->rel);
        l->gain = (l->env > l->ceiling) ? (l->ceiling / l->env) : 1.0f;

        /* output the delayed sample with the gain that was computed when it entered the line, so a
         * peak cannot outrun its own gain reduction (writing the CURRENT gain instead lets a decaying
         * envelope overshoot by a fraction of a dB) */
        const int q = l->pos;
        const float dl = l->buf[0][q], dr = l->buf[1][q], dg = l->gbuf[q];
        l->buf[0][q] = infl;
        l->buf[1][q] = inr;
        l->gbuf[q]   = l->gain;
        l->pos = (l->pos + 1) % (la + 1);

        left[i]  = dl * dg;
        right[i] = dr * dg;
    }
}
