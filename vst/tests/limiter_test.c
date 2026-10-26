/* limiter_test.c — unit test for the DJ FX look-ahead brickwall limiter (own code).
 *   1. off -> transparent; 2. a loud sine is held at the ceiling; 3. an impulse train never overshoots.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../include/limiter.h"

#define FS 44100.0f
#define N  44100
#define M_PIf 3.14159265358979323846f

static float g_in[N], g_l[N], g_r[N];

static float peak(const float *b) { float p = 0; for (int i = 0; i < N; i++) { const float a = fabsf(b[i]); if (a > p) p = a; } return p; }

static int fails = 0;
static void check(const char *what, int ok, const char *detail)
{
    printf("%-56s %s%s%s\n", what, ok ? "ok" : "FAIL", detail[0] ? "  " : "", detail);
    if (!ok) fails++;
}

int main(void)
{
    limiter_t *lim = limiter_create(FS);
    if (!lim) { printf("alloc failed\n"); return 2; }
    char b[96];

    /* 1) off = transparent */
    limiter_set_on(lim, 0);
    for (int i = 0; i < N; i++) { const float x = 0.9f * sinf(2.0f * M_PIf * 220.0f * i / FS); g_l[i] = x; g_r[i] = x; g_in[i] = x; }
    limiter_process(lim, g_l, g_r, N);
    {
        float md = 0; for (int i = 0; i < N; i++) { const float d = fabsf(g_l[i] - g_in[i]); if (d > md) md = d; }
        snprintf(b, sizeof b, "(max|diff| = %.2e)", md);
        check("off: transparent", md == 0.0f, b);
    }

    /* 2) a loud sine is held at the ceiling (-1 dBFS = 0.891) */
    limiter_set_on(lim, 1);
    limiter_set_ceiling(lim, -1.0f);
    limiter_reset(lim);
    for (int i = 0; i < N; i++) { const float x = 1.2f * sinf(2.0f * M_PIf * 220.0f * i / FS); g_l[i] = x; g_r[i] = x; }
    limiter_process(lim, g_l, g_r, N);
    {
        const float p = peak(g_l);
        snprintf(b, sizeof b, "(peak %.4f, ceiling 0.891)", p);
        check("loud sine held at the ceiling", p <= 0.90f && p > 0.5f, b);
    }

    /* 3) an impulse train never overshoots */
    limiter_reset(lim);
    for (int i = 0; i < N; i++) { const float x = (i % 3000 == 0) ? 2.5f : 0.0f; g_l[i] = x; g_r[i] = x; }
    limiter_process(lim, g_l, g_r, N);
    {
        const float p = peak(g_l);
        snprintf(b, sizeof b, "(peak %.4f)", p);
        check("impulse train never exceeds the ceiling", p <= 0.90f, b);
    }

    limiter_destroy(lim);
    printf("\n%s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}
