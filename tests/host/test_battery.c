/* Host test for the battery ADC sampling in rlc_battery.c.
 *
 * This is a safety path: the reported pack voltage gates arming and drives the
 * critical-battery ERROR transition. The specific failure being guarded against
 * was demonstrated on the bench — a noisy supply clipped individual samples at
 * ADC full scale, and a clipped sample can only bias a MEAN upward, making a
 * flat pack look healthy. These tests pin the median behaviour that fixes it. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int  g_adc_script[512];
int  g_adc_script_len = 0;
int  g_adc_script_pos = 0;
int  g_adc_fail_all   = 0;

#include "rlc_battery.c"

static int fails = 0, checks = 0;

static void expect_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) { printf("  FAIL %-46s got %d, want %d\n", what, got, want); fails++; }
}

static void expect_near(const char *what, int got, int want, int tol)
{
    checks++;
    if (got < want - tol || got > want + tol) {
        printf("  FAIL %-46s got %d, want %d +/-%d\n", what, got, want, tol);
        fails++;
    }
}

/* Reset driver state so each case starts clean. */
static void reset(float ratio)
{
    s_adc_handle   = (void *)1;
    s_cali_handle  = NULL;      /* use the deterministic fallback path */
    s_channel      = 0;
    s_divider_ratio = ratio;
    memset(s_samples, 0, sizeof(s_samples));
    s_sample_idx = 0;
    s_buffer_full = false;
    s_voltage_mv = 0;
    g_adc_script_pos = 0;
    g_adc_fail_all = 0;
}

static void script(const int *vals, int n)
{
    for (int i = 0; i < n; i++) g_adc_script[i] = vals[i];
    g_adc_script_len = n;
    g_adc_script_pos = 0;
}

/* What the fallback path turns a raw count into, per rlc_battery.c. */
static int raw_to_mv(int raw) { return (raw * 3300) / 4095; }

int main(void)
{
    printf("RLC battery sampling — host tests\n\n");

    /* ── T-B01: insertion sort ── */
    printf("T-B01 sort helper\n");
    int a[7] = {5, 3, 9, 1, 7, 2, 8};
    sort_ints(a, 7);
    int sorted_ok = 1;
    for (int i = 1; i < 7; i++) if (a[i-1] > a[i]) sorted_ok = 0;
    expect_int("ascending order", sorted_ok, 1);
    expect_int("min first", a[0], 1);
    expect_int("max last",  a[6], 9);

    /* ── T-B02: clean burst ── */
    printf("T-B02 clean burst returns that value\n");
    reset(1.0f);
    int clean[1] = {2000};
    script(clean, 1);
    int med = sample_burst_median(NULL);
    expect_int("median of a constant burst", med, 2000);

    /* ── T-B03: THE case — clipped samples must not move the median ── */
    printf("T-B03 clipped samples are rejected (the bench failure)\n");
    reset(1.0f);
    /* 33-sample burst: 8 clipped at full scale, the rest a steady 2000. */
    int mixed[33];
    for (int i = 0; i < 33; i++) mixed[i] = (i % 4 == 0) ? 4095 : 2000;
    script(mixed, 33);
    int railed = 0;
    med = sample_burst_median(&railed);
    expect_int("median ignores the clipped samples", med, 2000);
    expect_int("clipped samples are counted", railed, 9);

    /* Show what a mean would have done with the same data. */
    int sum = 0;
    for (int i = 0; i < 33; i++) sum += mixed[i];
    int mean = sum / 33;
    checks++;
    if (mean <= 2000) { printf("  FAIL mean should have been biased upward\n"); fails++; }
    printf("       (for contrast: mean of the same burst = %d, +%d counts high)\n",
           mean, mean - 2000);

    /* ── T-B04: low outliers rejected too ── */
    printf("T-B04 dropouts are rejected\n");
    reset(1.0f);
    int low[33];
    for (int i = 0; i < 33; i++) low[i] = (i % 5 == 0) ? 0 : 2500;
    script(low, 33);
    expect_int("median ignores zero dropouts", sample_burst_median(NULL), 2500);

    /* ── T-B05: end-to-end through rlc_battery_sample, with divider ── */
    printf("T-B05 full sample path applies the divider ratio\n");
    reset(4.3148f);
    script(mixed, 33);            /* steady 2000 with clipped spikes */
    uint16_t mv = rlc_battery_sample();
    int expect = (int)(raw_to_mv(2000) * 4.3148f);
    expect_near("reported mV unaffected by clipping", mv, expect, 2);
    expect_int("cached value matches", rlc_battery_get_voltage_mv(), mv);

    /* ── T-B06: total ADC failure keeps the last good value ── */
    printf("T-B06 read failure holds the previous reading\n");
    reset(1.0f);
    script(clean, 1);
    uint16_t good = rlc_battery_sample();
    g_adc_fail_all = 1;
    uint16_t after = rlc_battery_sample();
    expect_int("previous value retained on total failure", after, good);
    checks++;
    if (good == 0) { printf("  FAIL setup: first reading should be non-zero\n"); fails++; }

    /* ── T-B07: burst is odd-sized so the median is a real sample ── */
    printf("T-B07 burst size is odd\n");
    expect_int("VBAT_BURST_SAMPLES odd", VBAT_BURST_SAMPLES % 2, 1);

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
