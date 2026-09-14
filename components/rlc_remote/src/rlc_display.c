/**
 * RLC Display Driver — Phase 4 (FSD §10)
 *
 * ILI9488 480x320 SPI LCD, RGB666, SPI2_HOST @ 20 MHz.
 *
 * Rendering model:
 *   All drawing goes into a PSRAM framebuffer (480*320*3 bytes). Every write
 *   grows a dirty bounding box, but that box is only a coarse pre-filter:
 *   `flush()` compares the box row by row against a shadow copy of what the
 *   panel was last sent and transmits only the spans that actually changed
 *   (FSD §10.3 partial refresh). The drawing code repaints every field on
 *   every frame regardless of whether its text changed, so on the main status
 *   screen the bounding box spans the whole panel and says nothing useful —
 *   the pixel comparison is what makes the refresh partial.
 *
 *   Diffing rather than hand-maintained invalidation is deliberate: a missed
 *   invalidation leaves a stale pixel, and this screen displays ARMED.
 *
 *   `display_task` (prio 2, core 1 — FSD §9.10) owns the framebuffer and the
 *   SPI device: no other task ever touches SPI, so the FSM and input tasks
 *   never block on the panel. The task is paced with xTaskDelayUntil at a
 *   fixed DISPLAY_FRAME_MS period, so the frame rate does not sag with the
 *   cost of the frame.
 *
 * Screens (FSD §10.2) are selected from the remote FSM state, with latched
 * overrides for ERROR / firmware mismatch and a timed overlay for NACKs.
 */

#include "rlc_display.h"
#include "rlc_remote_fsm.h"
#include "rlc_fsm_events.h"
#include "rlc_arm_switch.h"
#include "rlc_battery.h"
#include "rlc_link.h"
#include "rlc_config.h"
#include "rlc_protocol.h"
#include "rlc_arm_state.h"
#include "rlc_version.h"
#include "pin_config.h"
#include "esp_partition.h"
#include "esp_jpeg_common.h"
#include "esp_jpeg_dec.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "rlc_disp";

/* ── ILI9488 command set ──────────────────────────────────────── */

#define ILI9488_SWRESET     0x01
#define ILI9488_ID          0x04
#define ILI9488_SLPOUT      0x11
#define ILI9488_DISPON      0x29
#define ILI9488_CASET       0x2A
#define ILI9488_PASET       0x2B
#define ILI9488_RAMWR       0x2C
#define ILI9488_MADCTL      0x36
#define ILI9488_PIXFMT      0x3A
#define ILI9488_FRMCTR1     0xB1
#define ILI9488_DFUNCTR     0xB6
#define ILI9488_PWCTR1      0xC0
#define ILI9488_PWCTR2      0xC1
#define ILI9488_VMCTR1      0xC5
#define ILI9488_GMCTRP1     0xE0
#define ILI9488_GMCTRN1     0xE1

#define DW  DISPLAY_WIDTH
#define DH  DISPLAY_HEIGHT

/* ── Colours (RGB888, packed 0xRRGGBB) ────────────────────────── */

#define C_BLACK      0x000000
#define C_WHITE      0xFFFFFF
#define C_GREY       0x808080
#define C_DGREY      0x303030
/* Continuity colours come from rlc_config.h so the display and the base
 * unit's 8-pixel strip always show the same colour for the same state. */
#define C_GOOD       RLC_COLOR_CONT_CONNECTED
#define C_OPEN       RLC_COLOR_CONT_OPEN
#define C_SUSPECT    RLC_COLOR_CONT_SUSPECT
#define C_MARGINAL   RLC_COLOR_CONT_MARGINAL

/* Non-continuity accents (unchanged by the continuity palette) */
#define C_INFO       0x0078FF   /* blue   — link OK, VRO credit */
#define C_WARN       0xFFDC00   /* yellow — warnings */
#define C_FAULT      0xFF0000   /* red    — errors */
#define C_SELECTED   0x00DCFF   /* cyan   — selected channel */
#define C_ARMED_BG   0xB40000   /* red    — armed channel background */
#define C_AMBER      0xFFA000
#define C_GREEN      0x00C000
#define C_BAND_UNKNOWN 0x505050 /* grey   — status band, state not known */
/* One-key yellow and both-keys orange are pushed to the extremes of what the
 * panel can put between red and green: 100% green against 31%. The first
 * attempt used C_WARN (0xFFDC00, 87% green) against 0xFF6000 (38%), which is
 * a clear separation on paper and was indistinguishable on the actual glass —
 * both read as orange. Dedicated constants rather than reusing C_WARN, so
 * tuning the band cannot drag the warning-text colour along with it. */
#define C_BAND_ONEKEY  0xFFFF00 /* yellow — status band, one key turned */
#define C_BAND_READY   0xFF5000 /* orange — status band, both keys turned */

#define R8(c)  (uint8_t)(((c) >> 16) & 0xFF)
#define G8(c)  (uint8_t)(((c) >>  8) & 0xFF)
#define B8(c)  (uint8_t)( (c)        & 0xFF)

/* Character cell: 5x7 glyph + 1 px spacing, multiplied by scale */
/* Frame period of display_task. Declared here rather than next to the task
 * because the splash animation derives its tick from it (see
 * draw_splash_band). */
#define DISPLAY_FRAME_MS  100   /* 10 Hz — FSD §10.3 requires >= 5 Hz */

/* Minimum yield after a frame that overran DISPLAY_FRAME_MS. Guarantees
 * lower-priority tasks pinned to this core — buzzer_task (prio 1) above all —
 * get the CPU every frame even when every frame is late. See the pacing block
 * at the bottom of display_task. */
#define DISPLAY_FRAME_MIN_YIELD_MS  20

#define CHAR_W(s)  (6 * (s))
#define CHAR_H(s)  (8 * (s))

/* ── 5x7 bitmap font (ASCII 0x20-0x7E) ────────────────────────── */

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14}, {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, {0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00}, {0x14,0x08,0x3E,0x08,0x14}, {0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02}, {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00}, {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, {0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40}, {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x0C,0x52,0x52,0x52,0x3E},
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
    {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, {0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00}, {0x08,0x08,0x2A,0x1C,0x08},
};

/* ── Module state ─────────────────────────────────────────────── */

static spi_device_handle_t s_spi    = NULL;
static spi_device_handle_t s_spi_rd = NULL;

/* TWO devices on this bus, and CS is driven BY HAND for both (1.2.18).
 *
 * The pixel path runs at DISPLAY_SPI_CLOCK_HZ (40 MHz) and register reads at
 * DISPLAY_SPI_READ_CLOCK_HZ (10 MHz), because the ILI9488 does not read back
 * at its write clock: the write cycle is 50 ns (20 MHz) but the READ cycle is
 * 150 ns (6.7 MHz) — on a read the panel drives MISO and its output delay is
 * an order of magnitude slower than its input setup requirement. Reads were
 * already outside spec at 20 MHz and got away with it for the life of the
 * project; at 40 MHz they stopped, returning 0x3F603B80 where the panel's
 * actual ID is 0x2A403300.
 *
 * WHY MANUAL CS, which is the whole point of this comment. 1.2.12 tried the
 * obvious thing — a second spi_bus_add_device() at the slower clock, both
 * devices configured with spics_io_num = PIN_DISPLAY_CS — and it fails
 * silently in the worst possible way. ESP-IDF gives each device its OWN
 * hardware CS signal (CS0, CS1, ...) and routes it to the requested pin
 * through the GPIO matrix, so the second add re-routed the pin to CS1 and
 * STOLE IT FROM THE FIRST DEVICE. Every write then ran with CS never
 * asserted: the panel ignored its entire init sequence and sat backlit and
 * unconfigured — a solid white screen — while reads on the second handle
 * worked perfectly and reported a healthy, correctly-identified panel. The
 * logs looked clean because the only path still working was the one being
 * logged.
 *
 * So neither device owns the CS pin: spics_io_num is -1 on both and
 * spi_xfer()/spi_xfer_rd() bracket every transaction with cs_low()/cs_high().
 * This is not a behaviour change — with hardware CS the driver already
 * asserted and released per transaction, including per row in flush_run() —
 * it just moves who does it. Two GPIO writes per transaction, which against
 * a 1440-byte row is nothing.
 *
 * THE RULE: no device on this bus may use hardware CS while another shares
 * the pin. Add a device only with spics_io_num = -1, and only if it brackets
 * its transactions the same way. */
static uint8_t            *s_fb    = NULL;   /* PSRAM framebuffer, RGB666 */
static uint8_t            *s_line  = NULL;   /* internal DMA-capable row buffer */
static bool                s_healthy = false;
static int64_t             s_boot_ms = 0;     /* display_init() timestamp */
static uint32_t            s_panel_id = 0;
static TaskHandle_t        s_task = NULL;

/* Dirty bounding box (inclusive); x0 > x1 means "clean" */
static int s_dx0, s_dy0, s_dx1, s_dy1;

/* Last-transmitted copy of the framebuffer. flush() diffs against this so only
 * genuinely changed pixels are sent to the panel. */
static uint8_t *s_shadow = NULL;

/* Requests posted by other tasks (mutex-protected) */
typedef enum {
    SCR_NONE = 0,
    SCR_SPLASH,
    SCR_MAIN,
    SCR_ARMED,
    SCR_FIRING,
    SCR_FIRE_COMPLETE,
    SCR_LINK_LOST,
    SCR_ERROR,
    SCR_FW_MISMATCH,
} screen_t;

static SemaphoreHandle_t s_req_mutex = NULL;

static struct {
    int      splash_attempt;
    bool     fw_mismatch;
    uint8_t  fw_base[3];
    uint8_t  fw_remote[3];
    bool     error_latched;
    char     error_text[64];
    char     overlay_text[40];
    bool     overlay_is_nack;
    int64_t  overlay_until_ms;
    uint8_t  fire_complete_ch;
    int64_t  fire_complete_until_ms;
} s_req;

static inline int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ── SPI plumbing ─────────────────────────────────────────────── */

static inline void dc_cmd(void)  { gpio_set_level(PIN_DISPLAY_DC, 0); }
static inline void dc_data(void) { gpio_set_level(PIN_DISPLAY_DC, 1); }

/* DS-01: every SPI return code used to be discarded, so a dead bus looked
 * exactly like a working one. Counted here and consumed by the runtime health
 * check in display_task. Not logged per transaction — a failing bus produces
 * thousands per second and would bury everything else in the log. */
static uint32_t s_spi_errors = 0;

/* CS is ours, not the driver's — see the note at s_spi_rd. Idle high. */
static inline void cs_low(void)  { gpio_set_level(PIN_DISPLAY_CS, 0); }
static inline void cs_high(void) { gpio_set_level(PIN_DISPLAY_CS, 1); }

static inline void spi_xfer(spi_transaction_t *t)
{
    cs_low();
    if (spi_device_polling_transmit(s_spi, t) != ESP_OK) s_spi_errors++;
    cs_high();
}

/* Register reads only, at DISPLAY_SPI_READ_CLOCK_HZ. Shares s_spi_errors so a
 * failed read still trips the same health accounting. */
static inline void spi_xfer_rd(spi_transaction_t *t)
{
    cs_low();
    if (spi_device_polling_transmit(s_spi_rd, t) != ESP_OK) s_spi_errors++;
    cs_high();
}

static void spi_send_cmd(uint8_t cmd)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    dc_cmd();
    spi_xfer(&t);
}

static void spi_send_data(const uint8_t *data, int len)
{
    if (len <= 0) return;
    spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = data };
    dc_data();
    spi_xfer(&t);
}

static void spi_read_reg(uint8_t cmd, uint8_t *buf, int len)
{
    int total = 1 + len;
    uint8_t *tx = heap_caps_calloc(1, total, MALLOC_CAP_DMA);
    uint8_t *rx = heap_caps_calloc(1, total, MALLOC_CAP_DMA);
    if (!tx || !rx) { free(tx); free(rx); return; }

    tx[0] = cmd;
    dc_cmd();
    spi_transaction_t t = {
        .length    = (size_t)total * 8,
        .rxlength  = (size_t)total * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_xfer_rd(&t);

    memcpy(buf, rx + 1, len);
    free(tx);
    free(rx);
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t buf[4];
    spi_send_cmd(ILI9488_CASET);
    buf[0] = (uint8_t)(x0 >> 8); buf[1] = (uint8_t)x0;
    buf[2] = (uint8_t)(x1 >> 8); buf[3] = (uint8_t)x1;
    spi_send_data(buf, 4);

    spi_send_cmd(ILI9488_PASET);
    buf[0] = (uint8_t)(y0 >> 8); buf[1] = (uint8_t)y0;
    buf[2] = (uint8_t)(y1 >> 8); buf[3] = (uint8_t)y1;
    spi_send_data(buf, 4);

    spi_send_cmd(ILI9488_RAMWR);
}

/* ── Framebuffer primitives ───────────────────────────────────── */

static inline void dirty_clear(void)
{
    s_dx0 = DW; s_dy0 = DH; s_dx1 = -1; s_dy1 = -1;
}

static inline bool dirty_empty(void)
{
    return (s_dx1 < s_dx0 || s_dy1 < s_dy0);
}

static inline void mark_dirty(int x, int y, int w, int h)
{
    if (x < s_dx0) s_dx0 = x;
    if (y < s_dy0) s_dy0 = y;
    if (x + w - 1 > s_dx1) s_dx1 = x + w - 1;
    if (y + h - 1 > s_dy1) s_dy1 = y + h - 1;
}

static void fill_rect(int x, int y, int w, int h, uint32_t colour)
{
    if (!s_fb) return;
    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DW) w = DW - x;
    if (y + h > DH) h = DH - y;
    if (w <= 0 || h <= 0) return;

    const uint8_t r = R8(colour), g = G8(colour), b = B8(colour);

    for (int row = 0; row < h; row++) {
        uint8_t *p = s_fb + ((size_t)(y + row) * DW + x) * 3;
        for (int col = 0; col < w; col++) {
            *p++ = r; *p++ = g; *p++ = b;
        }
    }
    mark_dirty(x, y, w, h);
}

/* Rectangle outline of the given thickness */
static void draw_frame(int x, int y, int w, int h, int t, uint32_t colour)
{
    fill_rect(x, y, w, t, colour);
    fill_rect(x, y + h - t, w, t, colour);
    fill_rect(x, y, t, h, colour);
    fill_rect(x + w - t, y, t, h, colour);
}

/* Filled circle centred on (cx, cy) */
static void fill_circle(int cx, int cy, int r, uint32_t colour)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, colour);
    }
}

/* Circle outline (ring) of the given thickness */
static void draw_circle(int cx, int cy, int r, int t, uint32_t colour)
{
    for (int dy = -r; dy <= r; dy++) {
        int outer = 0, inner = 0;
        while ((outer + 1) * (outer + 1) + dy * dy <= r * r) outer++;
        int ri = r - t;
        while (ri > 0 && (inner + 1) * (inner + 1) + dy * dy <= ri * ri) inner++;
        if (inner > 0 && (dy < ri && dy > -ri)) {
            fill_rect(cx - outer, cy + dy, outer - inner, 1, colour);
            fill_rect(cx + inner + 1, cy + dy, outer - inner, 1, colour);
        } else {
            fill_rect(cx - outer, cy + dy, 2 * outer + 1, 1, colour);
        }
    }
}

/* Upward triangle with the given half-width, apex at (cx, cy - r) */
static void fill_triangle_up(int cx, int cy, int r, uint32_t colour)
{
    for (int dy = 0; dy <= 2 * r; dy++) {
        int half = (dy * r) / (2 * r);
        fill_rect(cx - half, cy - r + dy, 2 * half + 1, 1, colour);
    }
}

/* Filled diamond, half-diagonal r, centred on (cx, cy) — the SUSPECT glyph
 * (FSD v1.71). Widens from the apex to the vertical middle, then narrows
 * again, same row-span fill pattern as the triangle above it. */
static void fill_diamond(int cx, int cy, int r, uint32_t colour)
{
    for (int dy = 0; dy <= 2 * r; dy++) {
        int half = (dy <= r) ? dy : (2 * r - dy);
        fill_rect(cx - half, cy - r + dy, 2 * half + 1, 1, colour);
    }
}


/* ── Text ─────────────────────────────────────────────────────── */

static void draw_char(int x, int y, char ch, int scale, uint32_t fg)
{
    if (ch < 0x20 || ch > 0x7E) ch = 0x20;
    const uint8_t *glyph = font5x7[(int)ch - 0x20];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1u << row)) {
                fill_rect(x + col * scale, y + row * scale, scale, scale, fg);
            }
        }
    }
}

static int text_width(const char *s, int scale)
{
    return (int)strlen(s) * CHAR_W(scale);
}

static void draw_text(int x, int y, const char *s, int scale, uint32_t fg)
{
    if (!s) return;
    for (int i = 0; s[i]; i++) {
        draw_char(x + i * CHAR_W(scale), y, s[i], scale, fg);
    }
}

static void draw_text_centred(int y, const char *s, int scale, uint32_t fg)
{
    draw_text((DW - text_width(s, scale)) / 2, y, s, scale, fg);
}

/* Centred text with an outline, for text that sits on top of the boot
 * splash's video band rather than on black (1.2.12).
 *
 * White-on-footage is a different problem from white-on-black. The asset is
 * graded and hard-capped at 0x9A by tools/mkvideoband.py and the band darkens
 * its own top rows under this text (baked into the asset), but neither can promise
 * anything about a *particular* frame: a plume drifts through, and a glyph
 * stroke that was over dark sky is suddenly over near-white smoke. The
 * outline is what makes the text legible independently of what is behind it,
 * rather than on average.
 *
 * Eight neighbours, not four: font5x7 is blocky and a four-neighbour ring
 * leaks at every diagonal corner. The offset scales with the glyph so the
 * ring stays proportionate at scale 2 and 3.
 *
 * Nine passes over the string, each the same per-pixel fill_rect work
 * ordinary text already does. It costs nothing on the wire — these rows are
 * inside the band and are transmitted every frame regardless. */
static void draw_text_centred_outlined(int y, const char *s, int scale,
                                       uint32_t fg, uint32_t outline)
{
    static const int8_t off[8][2] = {
        { -1, -1 }, { 0, -1 }, { 1, -1 },
        { -1,  0 },            { 1,  0 },
        { -1,  1 }, { 0,  1 }, { 1,  1 },
    };
    int x = (DW - text_width(s, scale)) / 2;
    int d = (scale >= 3) ? 2 : 1;

    for (int i = 0; i < 8; i++) {
        draw_text(x + off[i][0] * d, y + off[i][1] * d, s, scale, outline);
    }
    draw_text(x, y, s, scale, fg);
}

/* Centred text over a cleared background spanning [x, x+w).
 *
 * The bounds are explicit because this fill used to span the full panel width
 * unconditionally, which punched a notch through the left and right edges of
 * whatever frame the text sat inside. Every refresh of a value did it, so the
 * LINK LOST amber border and the ARMED / FIRING / FIRE COMPLETE box outlines
 * visibly broke wherever a live field crossed them. Callers pass the interior
 * of whatever encloses them; text is centred within that span, not the panel. */
static void draw_text_centred_bg_in(int x, int w, int y, const char *s,
                                    int scale, uint32_t fg, uint32_t bg)
{
    fill_rect(x, y, w, CHAR_H(scale), bg);
    draw_text(x + (w - text_width(s, scale)) / 2, y, s, scale, fg);
}

/* Full-panel-width variant, for text that is not inside a frame. */
static void draw_text_centred_bg(int y, const char *s, int scale,
                                 uint32_t fg, uint32_t bg)
{
    draw_text_centred_bg_in(0, DW, y, s, scale, fg, bg);
}

/* Field of fixed width, cleared then written (avoids ghosting when the
 * new value is shorter than the old one). */
static void draw_field(int x, int y, int w, const char *s, int scale,
                       uint32_t fg, uint32_t bg)
{
    fill_rect(x, y, w, CHAR_H(scale), bg);
    draw_text(x, y, s, scale, fg);
}

/* ── Widgets ──────────────────────────────────────────────────── */

/* Horizontal bar gauge, 0..100 % filled */
static void draw_bar(int x, int y, int w, int h, int pct, uint32_t fg, uint32_t bg)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int fill = ((w - 2) * pct) / 100;
    draw_frame(x, y, w, h, 1, C_GREY);
    fill_rect(x + 1, y + 1, w - 2, h - 2, bg);
    if (fill > 0) fill_rect(x + 1, y + 1, fill, h - 2, fg);
}

/* Indeterminate progress: a block ping-ponging inside the frame.
 *
 * For work that is genuinely open-ended, where a filled bar would have to
 * invent a denominator. The boot splash needs this because the remote retries
 * the handshake forever — see draw_splash_dynamic. */
static void draw_bar_sweep(int x, int y, int w, int h, int tick,
                           uint32_t fg, uint32_t bg)
{
    draw_frame(x, y, w, h, 1, C_GREY);
    fill_rect(x + 1, y + 1, w - 2, h - 2, bg);

    int inner = w - 2;
    int bw    = inner / 4;
    int span  = inner - bw;
    if (span <= 0) return;

    int cycle = 2 * span;
    int ph    = (tick * 8) % cycle;          /* 80 px/s at 10 Hz */
    int off   = (ph < span) ? ph : (cycle - ph);

    fill_rect(x + 1 + off, y + 1, bw, h - 2, fg);
}

static int pct_from_range(int value, int lo, int hi)
{
    if (hi <= lo) return 0;
    if (value <= lo) return 0;
    if (value >= hi) return 100;
    return ((value - lo) * 100) / (hi - lo);
}

static uint32_t continuity_colour(uint8_t band)
{
    switch (band) {
        case CONT_CONNECTED:     return C_GOOD;
        case CONT_MARGINAL: return C_MARGINAL;
        case CONT_SUSPECT:  return C_SUSPECT;
        default:            return C_OPEN;
    }
}

static const char *continuity_label(uint8_t band)
{
    switch (band) {
        /* "CONNECTED", not "GOOD": the band means current can flow, which a
         * dead short satisfies too. Claiming the igniter is good would be an
         * assertion the measurement cannot support. */
        case CONT_CONNECTED: return "CONNECTED";
        case CONT_MARGINAL:  return "MARGINAL";
        case CONT_SUSPECT:   return "SUSPECT";
        default:             return "OPEN";
    }
}

/* Continuity glyph per FSD §10.2.2:
 * CONNECTED = filled circle, MARGINAL = triangle, OPEN = empty circle,
 * SUSPECT = filled diamond (FSD v1.71 — the glyph retired with SHORT, revived
 * for the band that took over its enum slot; same diamond, unrelated meaning).
 * Shape carries the meaning as well as colour, so the screen stays readable
 * for colour-blind operators. */
static void draw_continuity_glyph(int cx, int cy, int r, uint8_t band)
{
    uint32_t c = continuity_colour(band);
    switch (band) {
        case CONT_CONNECTED: fill_circle(cx, cy, r, c);        break;
        case CONT_MARGINAL:  fill_triangle_up(cx, cy, r, c);   break;
        case CONT_SUSPECT:   fill_diamond(cx, cy, r, c);       break;
        default:             draw_circle(cx, cy, r, 3, c);     break;
    }
}

static void format_volts(char *buf, size_t len, uint16_t mv)
{
    if (mv == 0) {
        snprintf(buf, len, "--.-V");
    } else {
        snprintf(buf, len, "%u.%02uV", mv / 1000, (mv % 1000) / 10);
    }
}

/* ── Flush ────────────────────────────────────────────────────── */

/* Does this row differ from the shadow copy, and over which pixel span?
 * memcmp first, because the overwhelmingly common answer is "no": it is a
 * fast reject over the whole row before the byte scans run at all. */
static bool row_diff_span(const uint8_t *fb, const uint8_t *sh, int w,
                          int *xa, int *xb)
{
    size_t n = (size_t)w * 3;
    if (memcmp(fb, sh, n) == 0) return false;

    size_t i = 0;
    while (fb[i] == sh[i]) i++;
    size_t j = n - 1;
    while (fb[j] == sh[j]) j--;

    *xa = (int)(i / 3);
    *xb = (int)(j / 3);
    return true;
}

/* Transmit rows ya..yb over the pixel span gx0..gx1, and bring the shadow
 * copy up to date for exactly that region. */
static void flush_run(int gx0, int gx1, int ya, int yb)
{
    int    w        = gx1 - gx0 + 1;
    size_t rowbytes = (size_t)w * 3;

    set_window(gx0, ya, gx1, yb);

    /* The panel auto-increments inside the window, so the rows stream back to
     * back. Rows are copied through an internal-RAM bounce buffer: the
     * framebuffer lives in PSRAM. */
    for (int y = ya; y <= yb; y++) {
        const uint8_t *src = s_fb + ((size_t)y * DW + gx0) * 3;
        memcpy(s_line, src, rowbytes);
        spi_send_data(s_line, rowbytes);
        memcpy(s_shadow + ((size_t)y * DW + gx0) * 3, src, rowbytes);
    }
}

static void flush(void)
{
    if (!s_fb || !s_shadow || !s_spi || dirty_empty()) return;

    int x0 = s_dx0 < 0 ? 0 : s_dx0;
    int y0 = s_dy0 < 0 ? 0 : s_dy0;
    int x1 = s_dx1 >= DW ? DW - 1 : s_dx1;
    int y1 = s_dy1 >= DH ? DH - 1 : s_dy1;
    int w  = x1 - x0 + 1;

    /* The dirty box is only a coarse pre-filter. The drawing code repaints
     * every field on every frame whether or not its text changed, so on the
     * main status screen the box degenerates to the whole panel — updates run
     * from the top bar at y=0 to the instruction line at y=DH-30, and there is
     * one box to hold them all. Comparing against the shadow copy is what
     * actually decides the transfer: only genuinely changed pixels go out.
     *
     * Diffing rather than hand-maintained invalidation is deliberate. A missed
     * invalidation leaves a stale pixel on the panel; this screen displays
     * ARMED, and a stale ARMED is the one failure this display must not have.
     * A pixel comparison cannot get that wrong by construction. */
    int run_y0 = -1;                /* first row of the open run, -1 = none */
    int run_xa = 0, run_xb = 0;     /* absolute pixel span, unioned over run */

    for (int y = y0; y <= y1; y++) {
        const uint8_t *fbrow = s_fb     + ((size_t)y * DW + x0) * 3;
        const uint8_t *shrow = s_shadow + ((size_t)y * DW + x0) * 3;

        int xa, xb;
        if (!row_diff_span(fbrow, shrow, w, &xa, &xb)) {
            if (run_y0 >= 0) {
                flush_run(run_xa, run_xb, run_y0, y - 1);
                run_y0 = -1;
            }
            continue;
        }

        xa += x0;
        xb += x0;
        if (run_y0 < 0) {
            run_y0 = y; run_xa = xa; run_xb = xb;
        } else {
            if (xa < run_xa) run_xa = xa;
            if (xb > run_xb) run_xb = xb;
        }
    }
    if (run_y0 >= 0) flush_run(run_xa, run_xb, run_y0, y1);

    dirty_clear();
}

/* ── Data snapshot ────────────────────────────────────────────── */

typedef struct {
    rlc_state_t                 state;
    uint8_t                     selected;
    uint8_t                     armed;
    rlc_link_status_t           link;
    uint16_t                    vbat_mv;
    bool                        remote_key_armed;
    /* §5.5.2: the two arm-key contacts have been disagreeing. The condition
     * persists until the switch is serviced, so — unlike the 3 s toast that
     * announces it — the indication must persist too. */
    bool                        remote_key_fault;
    bool                        remote_error_latched;   /* set by the frame loop */
    rlc_payload_status_update_t status;
    bool                        status_fresh;
    uint32_t                    prefire_remain_ms;
} disp_data_t;

static void snapshot(disp_data_t *d)
{
    memset(d, 0, sizeof(*d));
    d->state             = remote_fsm_get_state();
    d->selected          = remote_fsm_get_selected_channel();
    d->armed             = remote_fsm_get_armed_channel();
    d->vbat_mv           = rlc_battery_get_voltage_mv();
    d->remote_key_armed  = arm_switch_is_armed();
    d->remote_key_fault  = arm_switch_get_fault();
    d->status_fresh      = remote_fsm_get_status(&d->status);
    d->prefire_remain_ms = remote_fsm_get_prefire_remaining_ms();
    rlc_link_get_status(&d->link);
}

/* ── Base arm state, derived from two distinct signals ────────────
 *
 * The derivation itself lives in rlc_common (rlc_arm_state.c) so the host
 * tests compile the real source. See the header for why ARMED/WELD key off
 * the arm sense, never the key switch.
 */

static const char *base_arm_label(base_arm_state_t s)
{
    switch (s) {
        case BASE_ARM_SAFE:  return "SAFE";
        case BASE_ARM_READY: return "READY";
        case BASE_ARM_ARMED: return "ARMED";
        case BASE_ARM_WELD:  return "WELD!";
        default:             return "?";
    }
}

/* base_arm_colour() lived here and colour-coded the BASE field on the main
 * screen (green/amber/red, flashing on WELD). The status band now carries that
 * mapping across the whole bottom of every screen, so the per-field colour was
 * a quieter duplicate of it — and on a saturated band it would have been the
 * less legible of the two. The words in the field still name the state; see
 * sys_status_colour(). */

/* ── System status band ───────────────────────────────────────────
 *
 * A coloured field across the bottom of every screen, so the state of the
 * fire path is legible at a glance from across a launch site without reading
 * anything. It occupies the area that already held the status and instruction
 * lines, which keeps the channel grid untouched — that grid fills the panel
 * width exactly (see the _Static_assert below) and has no room to give.
 *
 *   GREEN   base SAFE and remote arm switch off — positively confirmed safe
 *   YELLOW  one key turned — the arming sequence cannot proceed
 *   ORANGE  both keys turned — one long-press from a live relay
 *   RED     arm relay engaged — VBAT is live on the fire path
 *   GREY    not known
 *
 * Yellow and orange are separated deliberately. One key turned is a state the
 * hardware will not act on; both turned is one operator action away from
 * energising the igniter circuit. Collapsing them into a single amber would
 * hide the only transition in the sequence where the risk actually changes.
 *
 * Grey rather than green whenever the state is not known, following the
 * §10.2.2 rule that unknown is never displayed as SAFE. Green here is a
 * positive claim that the pad is safe to approach, so it must never appear
 * on a dead link or before the first STATUS_UPDATE.
 *
 * A welded relay flashes red/amber: the fire path is live when it should not
 * be, which is worse than a normal ARM and is the one case worth making
 * impossible to ignore.
 */

/* 252, not 248: the ARMED / FIRING / FIRE COMPLETE box spans y 70..249, and a
 * band starting at 248 would paint over the bottom two rows of its outline.
 * Pinned by a _Static_assert where BOX_H is defined. */
#define BAND_Y  252

typedef enum {
    SYS_UNKNOWN = 0,
    SYS_SAFE,
    SYS_KEY_BASE,       /* base key only   — sequence cannot proceed */
    SYS_KEY_REMOTE,     /* remote arm only — sequence cannot proceed */
    SYS_KEY_BOTH,       /* both            — one long-press from a live relay */
    SYS_BASE_FAULT,
    SYS_REMOTE_FAULT,
    SYS_RELAY_LIVE,
    SYS_WELD,
} sys_status_t;

/* A weld must persist before it is believed.
 *
 * On a normal ARMED -> IDLE disarm the base reports base_state = IDLE before
 * base_arm_sense has fallen — the relay takes time to release, and the sense
 * is debounced on top of that. rlc_base_arm_state() then sees the sense HIGH
 * with the FSM outside the firing path, which is precisely its weld condition,
 * and returns BASE_ARM_WELD. Measured on target at 180 ms and 220 ms across
 * two ordinary disarms.
 *
 * Left alone that flashes the loudest warning the display has, twice a
 * session, during routine operation — which teaches the operator that it means
 * nothing. 500 ms still reports a genuine weld far sooner than the base's own
 * confirm count, which is what the early check in rlc_arm_state.h exists to
 * beat, so the trade costs little of its purpose.
 *
 * The hysteresis lives here rather than in rlc_base_arm_state(): that function
 * is a pure function of one snapshot, is shared with the base, and is compiled
 * directly into the host tests (T-M01..T-M07). Keeping it stateless is worth
 * more than the convenience of putting the timer inside it. */
#define WELD_CONFIRM_MS 500

/* Base arm state with the link gate and the weld hysteresis applied. Every
 * caller should use this rather than rlc_base_arm_state() directly. */
static base_arm_state_t base_arm_state_settled(const disp_data_t *d)
{
    /* Link state gates everything, and does so BEFORE the staleness window.
     *
     * Link loss is declared at 1500 ms; a STATUS_UPDATE is only considered
     * stale after 4000 ms (2 x STATUS_UPDATE_INTERVAL_MS). Keying off
     * freshness alone therefore left a 2.5 s window in which the base could be
     * switched off, the LINK LOST screen up, and the display still showing the
     * last state received before the power was cut. Observed on target. The
     * link being down is itself proof that the base state is not known,
     * whatever the age of the last packet says. */
    bool linked = (d->link.state == RLC_LINK_STATE_LINKED);
    base_arm_state_t bs = rlc_base_arm_state(&d->status, d->status_fresh && linked);

    static int64_t weld_since_ms = 0;
    if (bs == BASE_ARM_WELD) {
        int64_t now = now_ms();
        if (weld_since_ms == 0) weld_since_ms = now;
        if (now - weld_since_ms < WELD_CONFIRM_MS) {
            /* Not believed yet — but the arm sense IS high, so the fire path
             * may well be live. Report ARMED, never anything safer: during a
             * disarm that is the literal truth (the relay is still releasing),
             * and during a real weld it is the conservative reading. */
            return BASE_ARM_ARMED;
        }
    } else {
        weld_since_ms = 0;
    }
    return bs;
}

static sys_status_t system_status(const disp_data_t *d)
{
    base_arm_state_t bs = base_arm_state_settled(d);

    /* Relay state first: it is the only signal that says whether the fire
     * path is live, and it outranks both key switches and every fault. */
    if (bs == BASE_ARM_WELD)  return SYS_WELD;
    if (bs == BASE_ARM_ARMED) return SYS_RELAY_LIVE;

    /* A faulted remote is not a trustworthy reporter of anything below. */
    if (d->remote_error_latched) return SYS_REMOTE_FAULT;

    if (bs == BASE_ARM_UNKNOWN) return SYS_UNKNOWN;

    /* A base in ERROR, or reporting any error flag, gets the same red as a
     * live relay. That is not dilution of the "pad is live" signal: a base
     * that has faulted cannot be trusted to have reported its relay state
     * accurately either, so treating it as possibly live is the honest
     * reading rather than a cautious one. The word says which it is. */
    if (d->status.error_flags != 0 || d->status.base_state == STATE_ERROR)
        return SYS_BASE_FAULT;

    /* One key or two is a real difference, not a shade of the same thing.
     * With one turned the arming sequence cannot start at all — the base
     * refuses without its key, and the remote will not send without its arm
     * switch. With both turned, a single long-press closes the arm relay and
     * puts VBAT on the fire path. That is the step change worth seeing from
     * across a launch site, so the two states get separate colours. */
    bool base_key   = (bs == BASE_ARM_READY);
    bool remote_key = d->remote_key_armed;

    if (base_key && remote_key) return SYS_KEY_BOTH;
    if (base_key)               return SYS_KEY_BASE;
    if (remote_key)             return SYS_KEY_REMOTE;
    return SYS_SAFE;
}

static uint32_t sys_status_colour(sys_status_t s)
{
    switch (s) {
        case SYS_SAFE:       return C_GREEN;
        case SYS_KEY_BASE:   return C_BAND_ONEKEY; /* yellow */
        case SYS_KEY_REMOTE: return C_BAND_ONEKEY; /* yellow */
        case SYS_KEY_BOTH:   return C_BAND_READY;  /* orange, next to red */
        case SYS_BASE_FAULT:
        case SYS_REMOTE_FAULT:
        case SYS_RELAY_LIVE: return C_ARMED_BG;
        case SYS_WELD:       return (((now_ms() / 400) % 2) == 0)
                                        ? C_ARMED_BG : C_AMBER;
        default:             return C_BAND_UNKNOWN;
    }
}

static const char *sys_status_word(sys_status_t s)
{
    switch (s) {
        case SYS_SAFE:       return "SAFE";
        /* Name which key is turned: with only one, the useful information is
         * which end still needs attention. */
        case SYS_KEY_BASE:   return "BASE KEY ARMED";
        case SYS_KEY_REMOTE: return "REMOTE ARMED";
        /* Not "READY TO FIRE" — the next step arms the relay, it does not
         * fire. Overstating it here would be the wrong kind of wrong. */
        case SYS_KEY_BOTH:   return "READY TO ARM";
        case SYS_BASE_FAULT:   return "BASE FAULT";
        case SYS_REMOTE_FAULT: return "REMOTE FAULT";
        case SYS_RELAY_LIVE: return "ARM RELAY LIVE";
        case SYS_WELD:       return "RELAY WELDED";
        default:             return "STATUS UNKNOWN";
    }
}

/* Black or white, whichever reads on the given background (Rec. 601 luma).
 * Text drawn on the band uses this rather than its own colour: the band
 * already carries the state, and guaranteed contrast matters more here than
 * a second colour code layered on top of a saturated field. */
static uint32_t band_fg(uint32_t bg)
{
    unsigned luma = (299u * R8(bg) + 587u * G8(bg) + 114u * B8(bg)) / 1000u;
    return (luma >= 110) ? C_BLACK : C_WHITE;
}

/* Band geometry. `y` is the band's top edge — BAND_Y on every screen except
 * MAIN, where the band takes the space the continuity legend used to occupy
 * (see MAIN_BAND_Y). `inset` is the thickness of any full-screen frame the
 * screen draws, so the band stops short of it instead of painting over it. */
static inline int band_x(int inset) { return inset; }
static inline int band_w(int inset) { return DW - 2 * inset; }
static inline int band_h(int y, int inset) { return DH - y - inset; }

/* Fill the band. `with_word` draws the status in words for screens that have
 * no text of their own down there — colour alone is never the sole carrier of
 * meaning in this UI, the same reason the continuity grid pairs colour with
 * shapes. Returns the background colour so callers can draw onto it. */
static uint32_t draw_status_band(const disp_data_t *d, int y, int inset,
                                 bool with_word)
{
    sys_status_t s  = system_status(d);
    uint32_t     bg = sys_status_colour(s);

    /* One line per transition. This band is a safety indicator, and until now
     * the only way to check it was to look at the panel — which is exactly how
     * the green-while-the-base-is-off case survived. Logging the state makes
     * it verifiable from a capture. WELD is excluded from the comparison
     * because its colour alternates; the state itself does not. */
    static sys_status_t s_last_logged = (sys_status_t)-1;
    if (s != s_last_logged) {
        s_last_logged = s;
        ESP_LOGI(TAG, "status band -> %s", sys_status_word(s));
    }

    fill_rect(band_x(inset), y, band_w(inset), band_h(y, inset), bg);

    if (with_word) {
        int ty = y + (band_h(y, inset) - CHAR_H(3)) / 2;
        draw_text_centred_bg_in(band_x(inset), band_w(inset), ty,
                                sys_status_word(s), 3, band_fg(bg), bg);
    }
    return bg;
}

/* ── Screen: top status bar (shared by MAIN / ARMED / FIRING) ── */

#define BAR_H       50
#define BAR_TXT_Y1  6
#define BAR_TXT_Y2  28

static void draw_top_bar_static(void)
{
    fill_rect(0, 0, DW, BAR_H, C_BLACK);
    fill_rect(0, BAR_H - 2, DW, 2, C_DGREY);
}

static void draw_top_bar_dynamic(const disp_data_t *d)
{
    char buf[32];
    bool linked = (d->link.state == RLC_LINK_STATE_LINKED);

    /* RSSI: -100 dBm (worst) .. -30 dBm (best) */
    snprintf(buf, sizeof(buf), "RSSI %4d", linked ? d->link.rssi_avg_dbm : 0);
    draw_field(6, BAR_TXT_Y1, 9 * CHAR_W(2), buf, 2, C_WHITE, C_BLACK);
    draw_bar(6 + 9 * CHAR_W(2) + 6, BAR_TXT_Y1, 60, CHAR_H(2),
             linked ? pct_from_range(d->link.rssi_avg_dbm, -100, -30) : 0,
             linked ? C_INFO : C_DGREY, C_BLACK);

    /* Link state + round-trip time.
     *
     * MIN-10: a LINKED-but-degraded link is its own thing and used to read
     * "LINK OK" — while the buzzer chirped BEEP_PING_FAIL and the ARM guard
     * refused with "LINK DEGRADED". One condition, three answers. "LINK WEAK"
     * in amber is the one the other two already agree with. */
    bool weak = linked && !rlc_link_is_healthy();
    const char *lnk = "LINK ??";
    switch (d->link.state) {
        case RLC_LINK_STATE_LINKED:  lnk = weak ? "LINK WEAK" : "LINK OK"; break;
        case RLC_LINK_STATE_LINKING: lnk = "LINKING"; break;
        case RLC_LINK_STATE_LOST:    lnk = "NO LINK"; break;
        default:                     lnk = "LINK --"; break;
    }
    draw_field(6, BAR_TXT_Y2, 9 * CHAR_W(2), lnk, 2,
               (linked && !weak) ? C_WHITE : C_WARN, C_BLACK);

    if (linked && d->link.ping_rtt_ms > 0) {
        snprintf(buf, sizeof(buf), "%3ums", d->link.ping_rtt_ms);
    } else {
        snprintf(buf, sizeof(buf), "  --ms");
    }
    draw_field(6 + 9 * CHAR_W(2) + 12, BAR_TXT_Y2, 6 * CHAR_W(2), buf, 2,
               C_WHITE, C_BLACK);

    /* Remote battery (1S LiPo: 3.0-4.2 V) */
    char v[12];
    format_volts(v, sizeof(v), d->vbat_mv);
    snprintf(buf, sizeof(buf), "RC %s", v);
    draw_field(256, BAR_TXT_Y1, 10 * CHAR_W(2), buf, 2,
               (d->vbat_mv && d->vbat_mv < REMOTE_VBAT_MIN_ARM_MV) ? C_WARN : C_WHITE,
               C_BLACK);
    draw_bar(256 + 10 * CHAR_W(2), BAR_TXT_Y1, 92, CHAR_H(2),
             pct_from_range(d->vbat_mv, REMOTE_VBAT_CRITICAL_MV, REMOTE_VBAT_FULL_MV),
             (d->vbat_mv < REMOTE_VBAT_MIN_ARM_MV) ? C_FAULT : C_GREEN, C_BLACK);

    /* Base battery from STATUS_UPDATE (3S: 9.0-12.6 V) */
    uint16_t bmv = d->status_fresh ? d->status.battery_voltage_mv : 0;
    format_volts(v, sizeof(v), bmv);
    snprintf(buf, sizeof(buf), "BS %s", v);
    draw_field(256, BAR_TXT_Y2, 10 * CHAR_W(2), buf, 2,
               (bmv && bmv < BASE_VBAT_MIN_ARM_MV) ? C_WARN : C_WHITE, C_BLACK);
    draw_bar(256 + 10 * CHAR_W(2), BAR_TXT_Y2, 92, CHAR_H(2),
             pct_from_range(bmv, BASE_VBAT_CRITICAL_MV, BASE_VBAT_FULL_MV),
             (bmv && bmv < BASE_VBAT_MIN_ARM_MV) ? C_FAULT : C_GREEN, C_BLACK);
}

/* ── Screen: main status (IDLE) — FSD §10.2.2 ─────────────────── */

/* Scale 2 (12x16 px per character) is the smallest font readable at arm's
 * length in daylight — nothing on any screen goes below it. The grid is sized
 * so the two status rows on the band below it fit at that size. */
#define GRID_Y      58
/* DS-02: the grid must fit inside DW (480). The last column starts at
 * GRID_X + 3*(CELL_W+2) and is CELL_W wide, so the constraint is
 * 2*GRID_X + 4*CELL_W + 3*2 <= DW. At the previous 6/118 that came to 484 and
 * the right-hand border of channels 4 and 8 was silently clipped away by
 * fill_rect/draw_frame. 3/117 lands exactly on 480. */
#define CELL_W      117
#define CELL_H      80
#define GRID_X      3
_Static_assert(2 * GRID_X + 4 * CELL_W + 3 * 2 <= DW,
               "channel grid overflows display width");

static void cell_origin(int ch, int *x, int *y)
{
    int idx = ch - 1;
    *x = GRID_X + (idx % 4) * (CELL_W + 2);
    *y = GRID_Y + (idx / 4) * (CELL_H + 4);
}

/* On the main screen only, the status band starts where a continuity legend
 * row used to sit. That legend restated what every grid cell already shows —
 * each cell draws the glyph AND the band's name in the band's colour — so it
 * carried no information the grid did not. Its row went to the band instead:
 * 90 px tall here against 68 on the other screens, which is the whole purpose
 * of the band — legible from across a launch site. The other screens cannot
 * follow: their centre box (ARMED / FIRING / FIRE COMPLETE) ends at y=250,
 * pinned to BAND_Y by the _Static_assert at BOX_H. */
#define MAIN_BAND_Y (GRID_Y + 2 * (CELL_H + 4) + 4)

static void draw_main_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_top_bar_static();
}

static void draw_channel_cell(int ch, const disp_data_t *d)
{
    int x, y;
    /* Bound the range explicitly rather than relying on the caller's loop
     * being inlined: without this the compiler cannot prove "CH%d" fits the
     * label buffers below and -Werror=format-truncation rejects the file. */
    if (ch < 1 || ch > NUM_CHANNELS) return;
    cell_origin(ch, &x, &y);

    /* RM-02 / FSD §8.2.2: a base that reports fewer than NUM_CHANNELS channels
     * must not have the missing ones shown as if they were real. Draw them as
     * an explicitly absent slot rather than an OPEN igniter — "OPEN" would
     * read as a channel with a disconnected lead. */
    if (ch > (int)rlc_link_get_peer_num_channels()) {
        fill_rect(x, y, CELL_W, CELL_H, C_BLACK);
        draw_frame(x, y, CELL_W, CELL_H, 1, C_DGREY);
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "CH%d", ch);
        draw_text(x + 6, y + 6, lbl, 2, C_DGREY);
        draw_text(x + (CELL_W - text_width("N/A", 2)) / 2, y + 40,
                  "N/A", 2, C_DGREY);
        return;
    }

    bool armed    = (d->armed == ch);
    bool selected = (d->selected == ch);
    uint32_t bg   = armed ? C_ARMED_BG : C_BLACK;

    uint8_t band = CONT_OPEN;
    if (d->status_fresh) {
        band = (uint8_t)((d->status.continuity_bands >> (2 * (ch - 1))) & 0x3);
    }

    fill_rect(x, y, CELL_W, CELL_H, bg);
    draw_frame(x, y, CELL_W, CELL_H, selected ? 3 : 1,
               selected ? C_SELECTED : C_DGREY);

    char label[8];
    snprintf(label, sizeof(label), "CH%d", ch);
    draw_text(x + 6, y + 6, label, 2, selected ? C_SELECTED : C_WHITE);

    if (d->status_fresh) {
        draw_continuity_glyph(x + CELL_W / 2, y + 42, 13, band);
        const char *txt = continuity_label(band);
        draw_text(x + (CELL_W - text_width(txt, 2)) / 2, y + CELL_H - 20, txt, 2,
                  continuity_colour(band));
    } else {
        draw_text(x + (CELL_W - text_width("NO DATA", 2)) / 2, y + 40,
                  "NO DATA", 2, C_GREY);
    }

    if (armed) {
        draw_text(x + CELL_W - text_width("ARM", 2) - 6, y + 6, "ARM", 2, C_WHITE);
    }
}

static void draw_main_dynamic(const disp_data_t *d)
{
    draw_top_bar_dynamic(d);

    for (int ch = 1; ch <= 8; ch++) {
        draw_channel_cell(ch, d);
    }

    /* Status area — base key switch, arm sense, remote key (FSD §10.2.2).
     * Labels are abbreviated so the whole row fits at scale 2.
     *
     * Both bottom rows sit on the system status band, so they are drawn onto
     * its colour rather than black, and in the band's contrast colour rather
     * than their own. The band already states the arm state louder than a
     * coloured word can, and legibility on a saturated field wins over a
     * second colour code. The words still say it for anyone who cannot rely
     * on the hue. */
    char buf[64];
    uint32_t bg = draw_status_band(d, MAIN_BAND_Y, 0, false);
    uint32_t fg = band_fg(bg);

    /* Re-centred for the taller band: 19 px above the first row, 20 between
     * rows, 19 below the last, instead of the old 2/20/14 against a band that
     * started 22 px lower. */
    int y = DH - 71;
    snprintf(buf, sizeof(buf), "SEL CH %u", d->selected);
    draw_field(6, y, 8 * CHAR_W(2), buf, 2, fg, bg);

    /* BASE reflects the fire path, REMOTE the operator's own switch.
     * "SEL CH 1   BASE READY   REMOTE ARMED" is 36 of the 40 characters
     * available at the scale-2 font floor. */
    /* Same settled state the band uses: this field showed the identical false
     * WELD! flash on every disarm, and it also read SAFE on a dead link for as
     * long as the last status stayed inside its freshness window. */
    base_arm_state_t bs = base_arm_state_settled(d);
    /* §5.5.2: under a contact fault this field must not keep asserting a key
     * position — its two sources disagree, so "ARMED"/"SAFE" would be a claim
     * the remote cannot support. Saying KEY FAULT is both the honest answer
     * and the persistent indication the 3 s toast cannot be.
     * "BASE READY   REMOTE KEY FAULT" is 29 of the 29 characters this field
     * holds at the scale-2 floor. */
    snprintf(buf, sizeof(buf), "BASE %s   REMOTE %s",
             base_arm_label(bs),
             d->remote_key_fault ? "KEY FAULT"
                                 : (d->remote_key_armed ? "ARMED" : "SAFE"));
    draw_field(120, y, DW - 126, buf, 2, fg, bg);

    if (d->status_fresh && d->status.error_flags) {
        /* Name the fault rather than making the operator decode a bitmask.
         * The line holds 40 characters at scale 2, so when several flags are
         * set at once they are cycled one per 2 s with an n/total counter
         * instead of being truncated. */
        uint8_t ef = d->status.error_flags;
        int nflags = rlc_error_flags_count(ef);
        if (nflags > 1) {
            int idx = (int)((now_ms() / 2000) % nflags);
            snprintf(buf, sizeof(buf), "BASE ERROR 0x%02X: %s (%d/%d)",
                     ef, rlc_error_flag_nth(ef, idx), idx + 1, nflags);
        } else {
            snprintf(buf, sizeof(buf), "BASE ERROR 0x%02X: %s",
                     ef, rlc_error_flag_str(ef));
        }
        draw_text_centred_bg_in(0, DW, DH - 35, buf, 2, fg, bg);
    } else if (d->remote_key_fault) {
        /* §5.5.2. Ranked below a base error — that one is about the fire path
         * — but above the next-step prompt, because the prompt is exactly what
         * this fault makes untrustworthy: with a failed NO contact the key
         * reads SAFE, so the line would tell the operator to turn a key they
         * are looking at, already turned. The REMOTE field above says KEY
         * FAULT at the same time, so the fault stays visible even while a base
         * error owns this line. */
        draw_text_centred_bg_in(0, DW, DH - 35,
                                "ARM KEY FAULT - CHECK SWITCH", 2, fg, bg);
    } else {
        /* Name the step that is actually outstanding. This used to test only
         * the remote arm switch, so with the remote armed and the base key
         * still in SAFE it read "HOLD ENCODER TO ARM CH n" — an instruction
         * the base will refuse, because arming needs its key too. It now
         * distinguishes all four combinations, which also gives the one-key
         * and both-keys states a wording difference and not just a hue. */
        switch (system_status(d)) {
            case SYS_KEY_BOTH:
                snprintf(buf, sizeof(buf), "HOLD ENCODER TO ARM CH %u", d->selected);
                break;
            case SYS_KEY_REMOTE:
                snprintf(buf, sizeof(buf), "TURN BASE KEY TO ARM CH %u", d->selected);
                break;
            case SYS_KEY_BASE:
                snprintf(buf, sizeof(buf), "%s", "FLIP REMOTE ARM SWITCH");
                break;
            default:
                snprintf(buf, sizeof(buf), "TURN ARM KEY TO ARM CH %u", d->selected);
                break;
        }
        draw_text_centred_bg_in(0, DW, DH - 35, buf, 2, fg, bg);
    }
}

/* ── Screen: ARMED — FSD §10.2.3 ──────────────────────────────── */

#define BOX_X   60
#define BOX_Y   70
#define BOX_W   360
#define BOX_H   180

/* The status band is painted after the box on these screens, so it must start
 * below it or it silently eats rows off the bottom of the outline. */
_Static_assert(BOX_Y + BOX_H <= BAND_Y,
               "centre box overlaps the status band");

static void draw_armed_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_top_bar_static();
}

static void draw_armed_dynamic(const disp_data_t *d, bool blink_on)
{
    char buf[48];
    draw_top_bar_dynamic(d);

    /* Pulsing red border (FSD §10.2.3) */
    draw_frame(BOX_X, BOX_Y, BOX_W, BOX_H, 6, blink_on ? C_FAULT : C_DGREY);
    fill_rect(BOX_X + 6, BOX_Y + 6, BOX_W - 12, BOX_H - 12, C_BLACK);

    snprintf(buf, sizeof(buf), "CHANNEL %u", d->armed);
    draw_text_centred(BOX_Y + 24, buf, 4, C_WHITE);
    draw_text_centred(BOX_Y + 66, "ARMED", 4, C_FAULT);

    /* DS-03: with stale data this printed "CONTINUITY OPEN". Fail-safe in
     * direction, but it asserts a measurement the remote does not have — and
     * an operator who pulls a lead and sees OPEN reasonably concludes the
     * reading is live. "?" is the honest answer, and it is what the main
     * screen already says ("NO DATA"). */
    bool band_known = d->status_fresh && d->armed >= 1 && d->armed <= 8;
    uint8_t band = CONT_OPEN;
    if (band_known) {
        band = (uint8_t)((d->status.continuity_bands >> (2 * (d->armed - 1))) & 0x3);
    }
    snprintf(buf, sizeof(buf), "CONTINUITY %s",
             band_known ? continuity_label(band) : "?");
    draw_text_centred_bg_in(BOX_X + 6, BOX_W - 12, BOX_Y + 112, buf, 2,
                            band_known ? continuity_colour(band) : C_FAULT,
                            C_BLACK);

    draw_text_centred(BOX_Y + 140, "HOLD FIRE TO LAUNCH", 2, C_WHITE);

    /* This line used to read "SENSE CONFIRMED" from the KEY switch, asserting
     * arm-relay confirmation the remote had never been sent. It now comes from
     * the real arm sense, so NOT CONFIRMED means the relay has not verified. */
    bool sense_ok = d->status_fresh && d->status.base_arm_sense;
    snprintf(buf, sizeof(buf), "BASE ARM SENSE %s   REMOTE %s",
             sense_ok ? "OK" : (d->status_fresh ? "NOT OK" : "?"),
             d->remote_key_fault ? "KEY FAULT"
                                 : (d->remote_key_armed ? "ARMED" : "SAFE"));
    uint32_t bg = draw_status_band(d, BAND_Y, 0, false);
    draw_text_centred_bg_in(0, DW, DH - 26, buf, 2, band_fg(bg), bg);
}

/* ── Screen: PRE_FIRE / FIRING — FSD §10.2.4 ──────────────────── */

static void draw_firing_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_top_bar_static();
}

static void draw_firing_dynamic(const disp_data_t *d, bool blink_on)
{
    char buf[48];
    draw_top_bar_dynamic(d);

    bool igniting = (d->state == STATE_FIRING);
    uint32_t border = igniting ? (blink_on ? C_FAULT : 0x600000) : C_FAULT;

    draw_frame(BOX_X, BOX_Y, BOX_W, BOX_H, 8, border);
    fill_rect(BOX_X + 8, BOX_Y + 8, BOX_W - 16, BOX_H - 16,
              igniting ? C_ARMED_BG : C_BLACK);

    uint8_t ch = d->armed ? d->armed : d->selected;
    snprintf(buf, sizeof(buf), "FIRING CH %u", ch);
    draw_text_centred(BOX_Y + 28, buf, 4, C_WHITE);

    if (igniting) {
        draw_text_centred_bg_in(BOX_X + 8, BOX_W - 16, BOX_Y + 92,
                                "IGNITION ACTIVE", 3, C_WHITE, C_ARMED_BG);
    } else {
        /* Pre-fire countdown, refreshed every 100 ms (FSD §10.3) */
        uint32_t ms = d->prefire_remain_ms;
        snprintf(buf, sizeof(buf), "PRE-FIRE %lu.%lus",
                 (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
        draw_text_centred_bg_in(BOX_X + 8, BOX_W - 16, BOX_Y + 92, buf, 3,
                                C_WARN, C_BLACK);
    }

    draw_text_centred_bg_in(BOX_X + 8, BOX_W - 16, BOX_Y + 138,
                            "RELEASE TO ABORT", 2,
                            C_WHITE, igniting ? C_ARMED_BG : C_BLACK);

    draw_status_band(d, BAND_Y, 0, true);
}

/* ── Screen: fire complete — FSD §10.2.4a ─────────────────────── */

static void draw_fire_complete_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_top_bar_static();
    draw_frame(BOX_X, BOX_Y, BOX_W, BOX_H, 6, C_GREEN);
}

static void draw_fire_complete_dynamic(const disp_data_t *d, uint8_t ch,
                                       int64_t until_ms)
{
    char buf[48];
    draw_top_bar_dynamic(d);

    draw_text_centred(BOX_Y + 24, "FIRE COMPLETE", 3, C_GREEN);
    snprintf(buf, sizeof(buf), "CHANNEL %u", ch);
    draw_text_centred(BOX_Y + 64, buf, 3, C_WHITE);

    /* Live igniter status for the channel just fired (FSD §15.4 T-S19).
     *
     * A good igniter burns through, so the continuity going OPEN is the
     * expected outcome and the operator's first evidence that the shot took.
     * Still CONNECTED after a pulse means current can still flow through it,
     * which usually means it did not fire.
     *
     * The wording stays a description of the measurement, not a verdict on the
     * igniter — same reason the band is labelled CONNECTED rather than GOOD.
     * OPEN says the circuit opened; it cannot distinguish a burned igniter
     * from a lead that fell off, and saying "FIRED" outright would assert more
     * than the reading supports. Redrawn every frame so a lead pulled after
     * the shot shows up immediately. */
    bool band_known = d->status_fresh && ch >= 1 && ch <= NUM_CHANNELS;
    uint8_t band = CONT_OPEN;
    if (band_known) {
        band = (uint8_t)((d->status.continuity_bands >> (2 * (ch - 1))) & 0x3);
    }

    const char *verdict;
    uint32_t    verdict_col;
    if (!band_known) {
        verdict = "IGNITER ?";           verdict_col = C_GREY;
    } else if (band == CONT_OPEN) {
        verdict = "OPEN - LIKELY FIRED"; verdict_col = C_GREEN;
    } else if (band == CONT_SUSPECT) {
        /* FSD v1.71: finite but unreasonably high resistance — not "fired",
         * not "connected". Residue, or a lead/clip fault; investigate. */
        verdict = "SUSPECT - CHECK";     verdict_col = C_SUSPECT;
    } else if (band == CONT_MARGINAL) {
        verdict = "MARGINAL - CHECK";    verdict_col = C_WARN;
    } else {
        verdict = "STILL CONNECTED";     verdict_col = C_FAULT;
    }

    draw_text_centred_bg_in(BOX_X + 6, BOX_W - 12, BOX_Y + 106, verdict, 2,
                            verdict_col, C_BLACK);
    /* Shape as well as colour, matching the channel grid. MIN-08: no glyph at
     * all when the band is unknown — drawing the OPEN ring next to a grey
     * "IGNITER ?" asserted a reading the screen has just said it does not
     * have. */
    if (band_known) {
        draw_continuity_glyph(BOX_X + 26, BOX_Y + 114, 7, band);
    } else {
        fill_rect(BOX_X + 26 - 7, BOX_Y + 114 - 7, 15, 15, C_BLACK);
    }

    int64_t left = until_ms - now_ms();
    if (left < 0) left = 0;
    /* "CLEARS IN", not "IDLE IN". The screen now outlives the base's POST_FIRE
     * cooldown, so after ~2 s the base is already IDLE and an "IDLE IN 2.6s"
     * countdown would be stating something untrue. This counts what it can
     * actually promise: when this screen goes away. */
    snprintf(buf, sizeof(buf), "CLEARS IN %lld.%llds",
             (long long)(left / 1000), (long long)((left % 1000) / 100));
    draw_text_centred_bg_in(BOX_X + 6, BOX_W - 12, BOX_Y + 140, buf, 2,
                            C_GREY, C_BLACK);

    draw_status_band(d, BAND_Y, 0, true);
}

/* ── Screen: link lost — FSD §10.2.5 ──────────────────────────── */

static void draw_link_lost_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_frame(0, 0, DW, DH, 8, C_AMBER);
    draw_text_centred(40, "! LINK LOST !", 4, C_AMBER);
    draw_text_centred(110, "No response from base unit", 2, C_WHITE);
    draw_text_centred(148, "All channels disarmed (assumed)", 2, C_WHITE);
    /* "Attempting to reconnect..." lives on the status band and so is drawn
     * per-frame in the dynamic half — the band is repainted every frame and
     * would otherwise erase it. */
}

static void draw_link_lost_dynamic(const disp_data_t *d)
{
    char buf[48];

    /* Real elapsed time since the last frame from the base. This used to be
     * derived from missed_pings, which stops incrementing the moment the link
     * is declared LOST — so it froze at the failure threshold (3 misses x
     * 500 ms = "1 s ago") and never advanced again. */
    uint32_t secs = d->link.ms_since_contact / 1000;
    if (secs < 600) {
        snprintf(buf, sizeof(buf), "Last contact: %lu s ago", (unsigned long)secs);
    } else {
        snprintf(buf, sizeof(buf), "Last contact: %lu min ago",
                 (unsigned long)(secs / 60));
    }
    /* Inset to the interior of the amber frame. A full-width clear here was
     * what notched the frame's left and right edges every second. */
    draw_text_centred_bg_in(8, DW - 16, 185, buf, 2, C_WHITE, C_BLACK);

    /* NO status band on this screen. system_status() gates on link state, so
     * with the link down it can only ever return SYS_UNKNOWN — the band would
     * be grey here every single time. A field that can show exactly one value
     * carries no information, and this one was covering the reconnect text
     * that does. Same reasoning as the splash and firmware-mismatch screens. */
    draw_text_centred_bg_in(8, DW - 16, 250, "Attempting to reconnect...", 2,
                            C_WHITE, C_BLACK);

    /* Reconnect attempts, not ping misses — linkreq_attempts is the count that
     * actually advances while the remote is retrying LINK_REQUEST. */
    snprintf(buf, sizeof(buf), "Attempts %u   RSSI %d dBm",
             d->link.linkreq_attempts, d->link.rssi_avg_dbm);
    draw_text_centred_bg_in(8, DW - 16, 288, buf, 2, C_GREY, C_BLACK);
}

/* ── Screen: error — FSD §10.2.6 ──────────────────────────────── */

static void draw_error_screen(const char *text)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_frame(0, 0, DW, DH, 8, C_FAULT);
    draw_text_centred(50, "X  ERROR  X", 4, C_FAULT);

    /* Wrap the description across up to two lines of 34 chars at scale 2 */
    char line[40];
    const int per_line = 34;
    int len = text ? (int)strlen(text) : 0;
    int y = 140;
    for (int off = 0; off < len && y < 200; off += per_line) {
        int n = len - off;
        if (n > per_line) n = per_line;
        memcpy(line, text + off, n);
        line[n] = '\0';
        draw_text_centred(y, line, 2, C_WHITE);
        y += CHAR_H(2) + 6;
    }

    /* Clear of BAND_Y: the status band is repainted every frame and this
     * screen has no dynamic half to redraw over it. The error description is
     * at most two lines (64-char buffer, 34 per line), ending by y=178. */
    draw_text_centred(216, "System halted - power cycle", 2, C_WARN);
}

/* ── Screen: splash / firmware mismatch — FSD §10.2.1 ─────────── */

/* Boot-splash video band (1.2.9).
 *
 * A letterboxed strip of real footage — two side boosters landing — played
 * behind the splash text from a JPEG frame sequence in the `splash` flash
 * partition.
 *
 * WHY A BAND AND NOT THE WHOLE PANEL. The ILI9488 is 18-bit-only over SPI, so
 * flush_run() ships 3 bytes/pixel at DISPLAY_SPI_CLOCK_HZ. Transfers go out
 * with spi_device_polling_transmit(), so that time is CPU time on this core
 * too — and measurement showed the transfer is not even the whole cost.
 *
 * Bytes per frame, and the transfer alone:
 *
 *                                        @20 MHz   @40 MHz
 *   480x80   38,400 px    115,200 B        46 ms     23 ms   (<= 1.2.11)
 *   480x128  61,440 px    184,320 B        74 ms     37 ms   (1.2.12)
 *   480x160  76,800 px    230,400 B        92 ms     46 ms   (current)
 *   480x320 153,600 px    460,800 B       184 ms     92 ms
 *
 * Measured on target at 480x128 / 20 MHz, flush() cost 99 ms against that
 * 74 ms of transfer — the remainder is per-row memcmp, the s_line bounce copy
 * and the shadow update. Decode was 12 ms and the blit 9 ms; neither ever
 * mattered, and neither did flash space.
 *
 * So the current 480x160 needs the 40 MHz clock (see DISPLAY_SPI_CLOCK_HZ),
 * and even with it a frame on which the clip ADVANCES costs ~120 ms against a
 * 100 ms period. That is why the asset runs at 5 Hz and why the early-outs
 * below matter: the frames in between cost almost nothing, and the measured
 * average is 73 ms. Full panel is out at any clock this panel will take.
 *
 * If the band is ever resized, VBAND_H here and DISPLAY_SPI_CLOCK_HZ there are
 * a pair, the asset must be rebuilt to match, and the verdict on a clock
 * change comes from LOOKING AT THE PANEL — never from the boot log.
 *
 * The band is blitted whole rather than diffed by hand; flush()'s shadow
 * comparison decides what actually goes out, so nothing here needs a
 * hand-maintained erase list.
 *
 * The clip loops (1.2.11, by operator preference). STATE_LINKING maps to the
 * splash screen, so a remote with no base in range sits here indefinitely and
 * the loop runs for as long as it is powered — a decode and ~46 ms of band
 * over SPI every 100 ms. Within budget, but not free the way the previous
 * hold-on-last-frame was.
 *
 * The asset is deliberately NOT embedded in the app binary. It lives in its
 * own partition so it can be reflashed (tools/mkvideoband.py, then
 * `./build_remote.sh splash <file>`) without rebuilding or reflashing
 * firmware, and so that iterating on the footage never touches the image that
 * runs the fire path.
 *
 * Everything here degrades to a plain dark band: no partition, no asset, a
 * blank partition, a corrupt header, wrong dimensions or a frame that fails to
 * decode all end with the splash drawing and the remote booting normally. A
 * boot screen decoration must never be able to stop the unit coming up.
 */

/* The band starts at the top of the panel and runs edge to edge (1.2.12). It
 * used to be a 480x80 strip at y=112 with 15 blank rows of margin above and
 * below, because a photograph butted against text reads as a rendering fault.
 * That reasoning does not survive the band reaching the top edge: there is no
 * text above it any more to butt against — the title block is drawn *on* it —
 * and a letterbox margin above a top-anchored image looks like a mistake
 * rather than a frame. The margin below is what remains: the 36 blank rows
 * between the band (ending y=159) and the headline (y=196).
 *
 * 160 — the top half — since 1.2.14, and it only fits because of the 40 MHz
 * clock (see DISPLAY_SPI_CLOCK_HZ). At 20 MHz the largest that fits the frame
 * period is 128, and before that 480x80 was the practical 10 Hz ceiling. If
 * the clock is ever given back, this must come down with it. */
#define VBAND_X        0
#define VBAND_Y        0
#define VBAND_W        480
#define VBAND_H        160     /* multiple of 16 — whole MCUs, no pad row */

#define C_VBAND_EMPTY  0x0A0C12   /* shown when there is no playable asset */

/* Scrim (1.2.12) — BAKED INTO THE ASSET, not applied here.
 *
 * The title block is drawn over the band's top rows and the grading alone
 * cannot carry it: --cap 0x9A bounds the footage but a plume still arrives as
 * a near-white field behind white glyphs. So those rows are darkened on a
 * ramp: KEEP/256 of the original pixel survives at the top row, rising to all
 * of it by SCRIM_H, which sits just below the club credit (ending y=85). A
 * ramp rather than a box, because a hard-edged dark rectangle behind the
 * titles is more obviously a patch than the contrast problem it solves.
 *
 * THIS RAN ON THE REMOTE FOR EXACTLY ONE FIRMWARE REVISION AND MUST NOT AGAIN.
 * As a per-frame framebuffer pass it was ~150 KB of byte-wise
 * read-modify-write in PSRAM inside a display task with a 100 ms budget, and
 * it was a large part of the 153 ms frame that starved buzzer_task and
 * rebooted the remote on its task watchdog. tools/mkvideoband.py applies the
 * identical integer maths once, on a host, before the JPEG is encoded, where
 * it costs nothing at run time.
 *
 * These constants are therefore a CONTRACT WITH THE TOOL, not live parameters
 * — they are what --scrim-h and --scrim-keep must be built with, and changing
 * one here without rebuilding the asset changes nothing on the panel. They
 * stay in the firmware because the layout they protect (the title rows) is
 * defined here, and the next person to move the title block needs to see
 * them. */
#define VBAND_SCRIM_H      56     /* covers the single title line (y 10..33) */
#define VBAND_SCRIM_KEEP   56     /* 22% at the very top row */

/* Bottom scrim, under the club credit (1.2.15).
 *
 * The credit moved from y 70 to the foot of the band so the MIDDLE of the
 * picture is clear of text — the boosters were landing behind it, and the
 * rows below the smoke were ground and trees doing nothing. With the credit
 * at the bottom and the titles at the top, rows ~80..119 are both text-free
 * and full strength, and that band is where a cut should put the touchdown.
 *
 * Same contract with the tool as the top scrim: these are what --scrim-bot-h
 * and --scrim-bot-keep must be built with, not live parameters. */
#define VBAND_SCRIM_BOT_H     40  /* rows 120..159 */
#define VBAND_SCRIM_BOT_KEEP  72  /* 28% on the last row */

/* Club credit baseline — the foot of the band, not under the title block. */
#define VBAND_CREDIT_Y  (VBAND_Y + VBAND_H - 22)

/* One line since 1.2.16, where it was "ESP32 WIRELESS ROCKET" / "LAUNCH
 * CONTROLLER" over two. Dropping the second line and shortening the top scrim
 * to match (80 -> 56 rows) is worth ~50 rows of clear, full-strength picture
 * in the middle of the band — the boosters now descend through open sky
 * rather than behind a title block.
 *
 * At scale 3 this is 24 chars x 18 px = 432 px in a 480 px panel: 24 px of
 * margin each side, 22 once the glyph outline is counted. There is no room
 * for a longer string, hence the assert — a title that silently ran off both
 * edges would be a poor way to find that out. */
#define SPLASH_TITLE  "ROCKET LAUNCH CONTROLLER"
_Static_assert((sizeof(SPLASH_TITLE) - 1) * CHAR_W(3) + 4 <= DW,
               "splash title too wide for the panel at scale 3");

/* RLCV container, written by tools/mkvideoband.py. Little-endian throughout.
 *
 *   0  u32  magic 'RLCV'
 *   4  u16  format version
 *   6  u16  frame count
 *   8  u16  width
 *  10  u16  height
 *  12  u16  frame interval, ms
 *  14  u16  reserved
 *  16  frame_count x { u32 offset; u32 length; }
 *      JPEG payloads
 *
 * Read through explicit byte loads rather than a cast to a packed struct: the
 * blob is memory-mapped flash written by an external tool, and it is not this
 * file's business to assume its alignment or the compiler's packing. */
#define RLCV_MAGIC       0x56434C52u
#define RLCV_FORMAT      1
#define RLCV_HDR_BYTES   16
#define RLCV_MAX_FRAMES  600     /* 60 s at 10 Hz — sanity bound, not a spec */

/* Set false whenever something has overwritten the title rows — a full
 * repaint, or a band frame blitted over them. Outside the guard below: the
 * fault-injection build draws no band but still draws the titles. */
static bool s_titles_drawn = false;

/* The whole playback path is compiled out of a fault-injection build.
 *
 * That build draws no band at all (see draw_splash_static), so through 1.2.11
 * every function here was still compiled and only the call site was #if'd
 * out, which left `draw_splash_band defined but not used` on every --inject
 * build and — worse, because it was silent — had splash_video_init() map the
 * partition and allocate a 230 KB PSRAM frame buffer that nothing would ever
 * read. Guarding the code rather than the call site fixes both. The VBAND_*
 * geometry above stays outside the guard: the fault banner is keyed off it. */
#if !CONFIG_RLC_REMOTE_FAULT_INJECTION

static const uint8_t             *s_vid          = NULL;
static esp_partition_mmap_handle_t s_vid_map     = 0;
static size_t                     s_vid_size     = 0;
static uint16_t                   s_vid_frames   = 0;
static uint16_t                   s_vid_interval = DISPLAY_FRAME_MS;
static uint8_t                   *s_vid_rgb      = NULL;  /* decoded frame */
static int                        s_vid_cur      = -1;    /* index in s_vid_rgb */
static bool                       s_vid_logged   = false; /* decode-fail log once */
static bool                       s_vid_painted  = false; /* band rows valid in s_fb */

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Validate the whole container up front, so the per-frame path can index the
 * table without re-checking bounds every time. A blank (erased, all-0xFF)
 * partition must fail here as cleanly as a corrupt one. */
static bool splash_video_validate(const uint8_t *b, size_t size)
{
    if (size < RLCV_HDR_BYTES)             return false;
    if (rd32(b) != RLCV_MAGIC)             return false;
    if (rd16(b + 4) != RLCV_FORMAT)        return false;

    uint16_t frames = rd16(b + 6);
    if (frames == 0 || frames > RLCV_MAX_FRAMES) return false;
    if (rd16(b + 8)  != VBAND_W)            return false;
    if (rd16(b + 10) != VBAND_H)            return false;
    if (rd16(b + 12) == 0)                 return false;

    size_t table = (size_t)frames * 8;
    if (table / 8 != frames)               return false;   /* overflow */
    if (RLCV_HDR_BYTES + table > size)     return false;

    for (uint16_t i = 0; i < frames; i++) {
        uint32_t off = rd32(b + RLCV_HDR_BYTES + (size_t)i * 8);
        uint32_t len = rd32(b + RLCV_HDR_BYTES + (size_t)i * 8 + 4);
        if (len == 0)                                  return false;
        if (off < RLCV_HDR_BYTES + table)              return false;
        if ((size_t)off + len > size)                  return false;
    }
    return true;
}

/* Map the splash partition and adopt its contents if they are playable.
 * Every failure path is non-fatal and leaves s_vid NULL. */
static void splash_video_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, "splash");
    if (!part) {
        ESP_LOGW(TAG, "splash: no 'splash' partition — plain band");
        return;
    }

    const void *map = NULL;
    esp_err_t err = esp_partition_mmap(part, 0, part->size,
                                       ESP_PARTITION_MMAP_DATA, &map, &s_vid_map);
    if (err != ESP_OK || !map) {
        ESP_LOGW(TAG, "splash: mmap failed (%s) — plain band", esp_err_to_name(err));
        return;
    }

    if (!splash_video_validate((const uint8_t *)map, part->size)) {
        ESP_LOGW(TAG, "splash: no valid RLCV asset — plain band "
                      "(flash one with ./build_remote.sh splash <file>)");
        esp_partition_munmap(s_vid_map);
        s_vid_map = 0;
        return;
    }

    const uint8_t *b = (const uint8_t *)map;

    /* 16-byte aligned for the decoder, and in PSRAM: 115 KB of internal RAM
     * for a boot decoration would be taken from tasks that need it. */
    s_vid_rgb = heap_caps_aligned_alloc(16, (size_t)VBAND_W * VBAND_H * 3,
                                        MALLOC_CAP_SPIRAM);
    if (!s_vid_rgb) {
        ESP_LOGW(TAG, "splash: frame buffer alloc failed — plain band");
        esp_partition_munmap(s_vid_map);
        s_vid_map = 0;
        return;
    }

    s_vid          = b;
    s_vid_size     = part->size;
    s_vid_frames   = rd16(b + 6);
    s_vid_interval = rd16(b + 12);
    s_vid_cur      = -1;

    ESP_LOGI(TAG, "splash: %u frames, %ux%u, %u ms/frame (%.1f s)",
             s_vid_frames, VBAND_W, VBAND_H, s_vid_interval,
             (double)s_vid_frames * s_vid_interval / 1000.0);
}

/* Decode one frame into s_vid_rgb. Returns 0 on success; on failure the
 * caller keeps whatever was there, so a bad frame shows the last good one
 * rather than garbage on the boot screen. */
static int splash_video_decode(int idx)
{
    const uint8_t *tab = s_vid + RLCV_HDR_BYTES + (size_t)idx * 8;
    uint32_t off = rd32(tab);
    uint32_t len = rd32(tab + 4);

    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB888;   /* R,G,B low->high: the
                                                   * framebuffer's own order,
                                                   * so the blit is a memcpy */
    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return -1;

    jpeg_dec_io_t io = {
        .inbuf     = (uint8_t *)(s_vid + off),
        .inbuf_len = (int)len,
        .outbuf    = s_vid_rgb,
    };
    jpeg_dec_header_info_t info = { 0 };

    int rc = -1;
    if (jpeg_dec_parse_header(dec, &io, &info) == JPEG_ERR_OK &&
        info.width == VBAND_W && info.height == VBAND_H &&
        jpeg_dec_process(dec, &io) == JPEG_ERR_OK) {
        rc = 0;
    }
    jpeg_dec_close(dec);

    if (rc != 0 && !s_vid_logged) {
        s_vid_logged = true;   /* once: this runs at 10 Hz */
        ESP_LOGW(TAG, "splash: frame %d failed to decode", idx);
    }
    return rc;
}

static void draw_splash_band(int64_t elapsed_ms)
{
    if (!s_vid || !s_vid_rgb) {
        fill_rect(VBAND_X, VBAND_Y, VBAND_W, VBAND_H, C_VBAND_EMPTY);
        return;
    }

    /* Loop, by operator preference (1.2.11). This replaces a deliberate hold
     * on the last frame, and the tradeoff is worth stating rather than
     * rediscovering: STATE_LINKING maps to this screen, so a remote switched
     * on with no base in range sits here indefinitely, and looping means it
     * decodes a frame and pushes ~46 ms of band over SPI every 100 ms for as
     * long as it is powered — where a held last frame cost nothing at all,
     * the blit writing pixels flush()'s per-row memcmp then rejected. It is
     * within budget (it is the same load the clip's first ten seconds already
     * carry, measured with room to spare) but it is no longer free.
     *
     * The cut is graded to pull back to roughly its opening width, so the
     * wrap is not a visible jump. Any replacement asset should do the same. */
    int64_t f = elapsed_ms / s_vid_interval;
    if (f < 0) f = 0;
    int idx = (int)(f % (int64_t)s_vid_frames);

    /* Nothing to do when the clip has not advanced (1.2.12).
     *
     * This is the whole reason a band larger than 480x80 is affordable. The
     * expensive part of a band frame is not the decode (12 ms) or the blit
     * (9 ms) but the flush: 128 full-width rows is 99 ms of per-row memcmp,
     * bounce copy, shadow update and SPI. Repainting identical pixels still
     * pays the memcmp and the blit even though flush() then transmits
     * nothing, and at 128 rows that is most of a frame period spent proving
     * the picture did not change.
     *
     * So when the frame index has not moved, leave the framebuffer alone
     * entirely. Safe because nothing else paints over the band region on this
     * screen — and when something does repaint the whole screen,
     * draw_splash_static() clears s_vid_painted and the next call redraws.
     *
     * This is what lets the asset run at its own rate, below the display's.
     * At 5 Hz the costly path runs every other frame and the frames between
     * cost nothing. */
    if (idx == s_vid_cur && s_vid_painted) {
        return;
    }

    if (idx != s_vid_cur) {
        if (splash_video_decode(idx) == 0) {
            s_vid_cur = idx;
        } else if (s_vid_cur < 0) {
            /* RLC-REVIEW-ALL-010 R-MIN3: the decode failed before ANY good
             * frame, so s_vid_rgb is uninitialized PSRAM — blitting would
             * show garbage where the documented behaviour is "last good
             * frame", of which there is none yet. Leave the plain band;
             * the title and dynamic fields still paint over it. */
            return;
        }
    }

    for (int y = 0; y < VBAND_H; y++) {
        memcpy(s_fb + ((size_t)(VBAND_Y + y) * DW + VBAND_X) * 3,
               s_vid_rgb + (size_t)y * VBAND_W * 3,
               (size_t)VBAND_W * 3);
    }
    mark_dirty(VBAND_X, VBAND_Y, VBAND_W, VBAND_H);
    s_vid_painted = true;
    s_titles_drawn = false;   /* the blit went over the title rows */
}

#else  /* CONFIG_RLC_REMOTE_FAULT_INJECTION */

/* No band, so nothing to map and nothing to decode. display_init() calls this
 * unconditionally; it is a no-op here rather than a call site to remember. */
static inline void splash_video_init(void) { }

#endif /* !CONFIG_RLC_REMOTE_FAULT_INJECTION */

/* Fault-injection banner geometry, keyed off the band so the two cannot drift
 * apart. It occupies the band's rows *below* the title block: since 1.2.12 the
 * band is the top half and the titles are drawn on top of it, so "exactly the
 * band's rows" — which is what 1.2.10 asked for — would now mean painting over
 * the title block. Below the scrim, above the band's bottom edge. */
#define FAULT_BLOCK_Y   (VBAND_Y + 70)
#define FAULT_BLOCK_H   (VBAND_H - 70)
#define FAULT_TXT1_DY   8
#define FAULT_TXT2_DY   34

/* The fault build suppresses the club credit, so the block starts where that
 * would have been (y 70) and runs to the band's bottom edge. It is sized by
 * VBAND_H, which has already moved twice in one firmware revision — assert
 * that the two text lines still fit inside it rather than discovering a
 * banner with its second line clipped off, on the one screen whose entire
 * purpose is to be unmissable. */
_Static_assert(FAULT_TXT2_DY + CHAR_H(2) + FAULT_TXT1_DY <= FAULT_BLOCK_H,
               "fault-injection banner text does not fit its block");
_Static_assert(FAULT_BLOCK_Y + FAULT_BLOCK_H <= DH,
               "fault-injection banner runs off the panel");

/* The credit is carried by the bottom scrim, so it must lie inside it, and
 * inside the band. Both are derived from VBAND_H and have moved more than
 * once; neither should be able to drift apart silently. */
_Static_assert(VBAND_CREDIT_Y >= VBAND_Y + VBAND_H - VBAND_SCRIM_BOT_H,
               "club credit sits above the bottom scrim that carries it");
_Static_assert(VBAND_CREDIT_Y + CHAR_H(2) <= VBAND_Y + VBAND_H,
               "club credit runs off the bottom of the band");

/* Header block (1.2.12: drawn per frame, not once).
 *
 * The title block used to be painted here in the static half, on black, above
 * the band. It is now drawn ON the band, which means it has to be repainted
 * after every band blit or the memcpy erases it — so it lives in
 * draw_splash_titles(), called from the dynamic half.
 *
 * This costs nothing on the wire. Those rows are inside the band and are
 * transmitted every frame regardless of what is drawn on them.
 *
 * The version string is deliberately NOT in the title block: it moved onto the
 * copyright line at the foot of the screen in 1.2.10, which had room for it
 * and reads as the same kind of small print. */
static void draw_splash_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
#if !CONFIG_RLC_REMOTE_FAULT_INJECTION
    s_vid_painted = false;   /* the fill wiped the band; force a repaint */
#endif
    s_titles_drawn = false;  /* ...and the title block with it */

#if CONFIG_RLC_REMOTE_FAULT_INJECTION
    /* A fault-injection build lies to its operator by construction, so the
     * boot screen SHALL say so before anything else can be believed. The
     * compile #warning, the boot banner and the flash-time warning are all on
     * the developer's terminal; this is the only one of the four an operator
     * standing at a firing point can see. It displaces the club credit — and,
     * since 1.2.9, the video band with it — deliberately: an abnormal
     * build should not look normal, least of all prettier.
     *
     * Static, unlike the titles: with no band drawn there is nothing
     * overwriting it, so it is painted once. */
    draw_frame(0, 0, DW, DH, 6, C_FAULT);
    fill_rect(24, FAULT_BLOCK_Y, DW - 48, FAULT_BLOCK_H, C_FAULT);
    draw_text_centred(FAULT_BLOCK_Y + FAULT_TXT1_DY,
                      "!! FAULT INJECTION BUILD !!", 2, C_WHITE);
    draw_text_centred(FAULT_BLOCK_Y + FAULT_TXT2_DY,
                      "NOT SAFE FOR LIVE USE", 2, C_WHITE);
#endif

    /* The title block, the copyright line and the progress bar are all drawn
     * per-frame in the dynamic half, not here. */
}

/* Title block, drawn over the band. Outlined rather than plain because it sits
 * on footage now; see draw_text_centred_outlined(). In a fault-injection build
 * there is no band, the text sits on black and the outline is invisible —
 * left in place rather than branched, since it costs only CPU on a boot
 * screen and one code path is worth more than the microseconds. */
static void draw_splash_titles(void)
{
    /* Only when something has actually overwritten them (1.2.12).
     *
     * Drawing these costs nothing on the wire — the rows are inside the band
     * and flush()'s memcmp rejects identical pixels — but that is not the same
     * as costing nothing. Nine outlined passes over three strings is a few
     * thousand fill_rect() calls, each with its own clipping and dirty-box
     * update, and it drags the dirty box across the whole band so flush()
     * re-compares every one of those rows. Measured, redrawing them on a frame
     * where the clip had not advanced was most of a 78 ms frame that should
     * have been nearly free.
     *
     * Nothing else paints over the title rows on this screen, so if neither
     * the band nor a full repaint has touched them they are still intact in
     * the framebuffer and there is nothing to do. */
    if (s_titles_drawn) return;
    s_titles_drawn = true;

    draw_text_centred_outlined(10, SPLASH_TITLE, 3, C_WHITE, C_BLACK);
#if !CONFIG_RLC_REMOTE_FAULT_INJECTION
    draw_text_centred_outlined(VBAND_CREDIT_Y,
                               "VRO - VLAAMSE RAKET ORGANISATIE", 2,
                               C_INFO, C_BLACK);
#endif
}

/* `attempt` is the remote's live LINK_REQUEST count and is NOT bounded.
 *
 * This used to read "Attempt N / LINK_REQUEST_MAX_RETRIES", clamped to the
 * maximum with the bar pinned at 100%. Every part of that was wrong.
 * LINK_REQUEST_MAX_RETRIES was never a give-up count — it is the threshold at
 * which tick_remote() drops to LINK_REQUEST_SLOW_INTERVAL_MS, and the remote
 * goes on retrying forever either side of it. Rendering a backoff threshold as
 * a denominator promised an end that never came, and the clamp then froze the
 * counter at "5 / 5" while the unit was in fact still working. A boot screen
 * that stops moving while the firmware has not stopped trying reads as a hung
 * remote, which is the one thing this screen must not do.
 *
 * So: no denominator, no clamp, and past the threshold an indeterminate sweep
 * instead of a full bar — the number keeps climbing, which is the clearest
 * available proof that the remote is still trying. The headline says what the
 * silence means; the counter says it has not given up.
 */
static void draw_splash_dynamic(const disp_data_t *d, int attempt,
                                int64_t hold_until_ms)
{
    char buf[40];
    int64_t elapsed = now_ms() - s_boot_ms;

    if (attempt < 1) attempt = 1;

#if !CONFIG_RLC_REMOTE_FAULT_INJECTION
    draw_splash_band(elapsed);   /* paints y=0..159, then scrims its top rows */
#endif
    draw_splash_titles();        /* on top of the band — must follow it */

    bool linked   = (d->link.state == RLC_LINK_STATE_LINKED);
    bool backedoff = (attempt >= LINK_REQUEST_MAX_RETRIES);

    /* A refusal is not the same as silence, and neither is the same as a base
     * that has simply not been switched on yet. Say which it is. */
    const char *headline;
    uint32_t    headline_fg;
    if (linked) {
        headline = "Connected to base";  headline_fg = C_GREEN;
    } else if (d->link.last_reject == LINK_REJECT_BUSY) {
        headline = "Base busy - armed or firing"; headline_fg = C_WARN;
    } else if (backedoff) {
        headline = "No response from base"; headline_fg = C_WARN;
    } else {
        headline = "Connecting to base..."; headline_fg = C_WHITE;
    }
    draw_text_centred_bg(196, headline, 2, headline_fg, C_BLACK);

    if (linked) {
        snprintf(buf, sizeof(buf), "RSSI %d dBm", d->link.rssi_avg_dbm);
    } else {
        snprintf(buf, sizeof(buf), "Attempt %d", attempt);
    }
    draw_text_centred_bg(228, buf, 2, C_WHITE, C_BLACK);

    /* NO status band while booting. The operator has not begun a sequence yet,
     * so it answers a question nobody is asking — and it sat on top of the
     * progress bar, which is the one thing this screen exists to show. */
    if (linked) {
        /* Once linked, the bar runs out the remaining splash hold so the
         * operator can see how long the screen stays up. */
        int64_t left = hold_until_ms - now_ms();
        if (left < 0) left = 0;
        int pct = 100 - (int)((left * 100) / SPLASH_MIN_DURATION_MS);
        draw_bar(90, 262, 300, 20, pct, C_GREEN, C_BLACK);
    } else if (backedoff) {
        draw_bar_sweep(90, 262, 300, 20,
                       (int)((elapsed / DISPLAY_FRAME_MS) & 0x7FFF),
                       C_SELECTED, C_BLACK);
    } else {
        /* Below the threshold the bar does measure something real: progress
         * through the fast-retry phase, after which the cadence changes. */
        draw_bar(90, 262, 300, 20,
                 (attempt * 100) / LINK_REQUEST_MAX_RETRIES,
                 C_SELECTED, C_BLACK);
    }

    /* The version rides on the copyright line rather than owning a row of its
     * own — same small print, and the row it used to cost is now the band's
     * top margin. It is still on the boot screen, which is what matters: the
     * strict version check makes "which firmware is this unit running" a
     * question an operator has to be able to answer without a serial cable. */
    draw_text_centred_bg_in(0, DW, DH - 26,
                            "(C) 2026 David Steeman  v" RLC_VERSION_STRING, 2,
                            C_GREY, C_BLACK);
}

static void draw_fw_mismatch(const uint8_t *base_ver, const uint8_t *remote_ver)
{
    char buf[40];
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_frame(0, 0, DW, DH, 8, C_WARN);
    draw_text_centred(34, "ROCKET LAUNCH CONTROLLER", 2, C_GREY);
    draw_text_centred(70, "! FIRMWARE MISMATCH !", 3, C_WARN);

    snprintf(buf, sizeof(buf), "Base:   v%u.%u.%u",
             base_ver[0], base_ver[1], base_ver[2]);
    draw_text(110, 140, buf, 2, C_WHITE);
    snprintf(buf, sizeof(buf), "Remote: v%u.%u.%u",
             remote_ver[0], remote_ver[1], remote_ver[2]);
    draw_text(110, 175, buf, 2, C_WHITE);

    draw_text_centred(226, "Reflash both units with", 2, C_GREY);
    draw_text_centred(250, "matching firmware.", 2, C_GREY);
}

/* ── Overlay: NACK / toast — FSD §10.2.7 ──────────────────────── */

static void draw_overlay(const char *text, bool is_nack)
{
    int h = 70;
    int y = (DH - h) / 2;
    uint32_t bg = is_nack ? C_ARMED_BG : 0x604000;
    fill_rect(20, y, DW - 40, h, bg);
    draw_frame(20, y, DW - 40, h, 3, is_nack ? C_FAULT : C_AMBER);
    draw_text_centred(y + 12, is_nack ? "COMMAND REJECTED" : "NOTICE", 2, C_WHITE);
    draw_text_centred(y + 40, text, 2, C_WHITE);
}

/* ── Screen selection ─────────────────────────────────────────── */

static screen_t screen_for_state(const disp_data_t *d)
{
    switch (d->state) {
        case STATE_BOOT:
        case STATE_LINKING:   return SCR_SPLASH;
        case STATE_IDLE:      return SCR_MAIN;
        case STATE_ARMED:     return SCR_ARMED;
        case STATE_PRE_FIRE:
        case STATE_FIRING:    return SCR_FIRING;
        case STATE_POST_FIRE: return SCR_FIRE_COMPLETE;
        case STATE_LINK_LOST: return SCR_LINK_LOST;
        case STATE_ERROR:     return SCR_ERROR;
        default:              return SCR_MAIN;
    }
}

/* ── Display task ─────────────────────────────────────────────── */

/* ── DS-01: runtime display health check (FSD §5.5.6) ──────────────
 *
 * Until now the panel ID was read exactly once, at boot. A panel, flex or
 * connector that failed mid-session simply froze the last rendered frame — and
 * the last frame can say "ARMED / CONTINUITY CONNECTED" while the FSM goes on
 * accepting fire commands. The TWDT cannot catch this: display_task keeps
 * flushing happily into a dead bus and feeds the watchdog on time.
 *
 * So re-read the ID periodically and compare it with the value latched at
 * boot. Done inside display_task itself, between frames, so it is serialised
 * with every other SPI write by construction — no lock, no half-written frame.
 *
 * Two consecutive bad reads are required before declaring failure: one read
 * can be lost to noise on a long flex, and disarming the pad on a single
 * glitch is its own hazard. Two misses is ~10 s of a genuinely dead panel.
 */
#define DISPLAY_HEALTH_INTERVAL_MS   5000
#define DISPLAY_HEALTH_FAIL_LIMIT    2

/**
 * Re-read the panel ID and update health state.
 * Returns true when the panel has just been declared failed (edge).
 */
static bool display_health_check(void)
{
    static int  fail_streak = 0;
    static bool failed_reported = false;

    uint32_t before_errors = s_spi_errors;
    uint8_t id[4] = {0};
    spi_read_reg(ILI9488_ID, id, 4);
    uint32_t id_now = ((uint32_t)id[0] << 24) | ((uint32_t)id[1] << 16) |
                      ((uint32_t)id[2] << 8) | id[3];

    bool ok = (s_spi_errors == before_errors) &&
              (id_now != 0) && (id_now == s_panel_id);

    if (ok) {
        if (fail_streak > 0) {
            ESP_LOGW(TAG, "display ID re-read recovered after %d miss(es)",
                     fail_streak);
        }
        fail_streak = 0;
        return false;
    }

    fail_streak++;
    ESP_LOGE(TAG, "display health check FAILED (%d/%d): ID 0x%08lX, expected "
                  "0x%08lX, spi_errors=%lu",
             fail_streak, DISPLAY_HEALTH_FAIL_LIMIT,
             (unsigned long)id_now, (unsigned long)s_panel_id,
             (unsigned long)s_spi_errors);

    if (fail_streak >= DISPLAY_HEALTH_FAIL_LIMIT && !failed_reported) {
        failed_reported = true;
        s_healthy = false;
        return true;
    }
    return false;
}

/* RLC-REVIEW-ALL-010 R-MIN4: the health check latches failed_reported and
 * posts EVT_DISPLAY_FAULT exactly once, so a full FSM queue used to lose
 * the fault for the whole power cycle. This latch defers instead: the send
 * is retried at the top of every frame until the FSM takes it. */
static bool s_fault_send_pending = false;

static void display_task(void *arg)
{
    (void)arg;
    /* 5.10: TWDT coverage — a hung SPI transaction previously froze the
     * last-rendered screen ("ARMED"/"PRE-FIRE") forever with no reset. The
     * loop wakes every DISPLAY_FRAME_MS, so a healthy task feeds easily. */
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "display task started (prio 2, core 1)");

    screen_t current = SCR_NONE;
    bool     overlay_drawn = false;
    int      frame = 0;
    int64_t  last_health_ms = now_ms();   /* DS-01 */
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        /* R-MIN4 retry — zero-timeout so a still-full queue costs nothing. */
        if (s_fault_send_pending) {
            QueueHandle_t q = remote_fsm_get_queue();
            rlc_fsm_event_t ev = {0};
            ev.type = EVT_DISPLAY_FAULT;
            if (!q || xQueueSend(q, &ev, 0) == pdTRUE) {
                s_fault_send_pending = false;
                ESP_LOGW(TAG, "EVT_DISPLAY_FAULT delivered on retry");
            }
        }

        disp_data_t d;
        snapshot(&d);

        /* Latched overrides take precedence over the FSM-derived screen. */
        xSemaphoreTake(s_req_mutex, portMAX_DELAY);
        bool     fw_mismatch  = s_req.fw_mismatch;
        uint8_t  fw_base[3], fw_remote[3];
        memcpy(fw_base, s_req.fw_base, 3);
        memcpy(fw_remote, s_req.fw_remote, 3);
        bool     err_latched  = s_req.error_latched;
        char     err_text[64];
        memcpy(err_text, s_req.error_text, sizeof(err_text));
        int      splash_att   = s_req.splash_attempt;
        bool     overlay_on   = (s_req.overlay_until_ms > now_ms());
        bool     overlay_nack = s_req.overlay_is_nack;
        char     overlay_txt[40];
        memcpy(overlay_txt, s_req.overlay_text, sizeof(overlay_txt));
        d.remote_error_latched = s_req.error_latched;
        bool     fire_done    = (s_req.fire_complete_until_ms > now_ms());
        uint8_t  fire_done_ch = s_req.fire_complete_ch;
        int64_t  fire_until   = s_req.fire_complete_until_ms;
        xSemaphoreGive(s_req_mutex);

        /* The link manager latches VERSION_MISMATCH itself (FSD §6.4.1);
         * pick it up here so the screen appears without an explicit call. */
        if (!fw_mismatch && d.link.state == RLC_LINK_STATE_VERSION_MISMATCH &&
            d.link.peer_fw_known) {
            fw_mismatch = true;
            memcpy(fw_base, d.link.peer_fw, 3);
            fw_remote[0] = RLC_VERSION_MAJOR;
            fw_remote[1] = RLC_VERSION_MINOR;
            fw_remote[2] = RLC_VERSION_PATCH;
        }

        /* The splash stays up for at least SPLASH_MIN_DURATION_MS — linking
         * usually completes in well under a second, which is too fast to read.
         * Errors still take precedence over the hold. */
        int64_t splash_until_ms = s_boot_ms + SPLASH_MIN_DURATION_MS;

        /* MAJ-04/05: one predicate for "a state that must not be covered by a
         * held screen", instead of a different escape set per special case.
         * ARMED/PRE_FIRE/FIRING are live-pad states; LINK_LOST is an alarmed
         * one — its own screen carries the disarm assumption and the alarm is
         * already sounding. */
        bool live_state = (d.state == STATE_ARMED ||
                           d.state == STATE_PRE_FIRE ||
                           d.state == STATE_FIRING);
        bool alarmed_state = (d.state == STATE_LINK_LOST);

        screen_t want;
        if (fw_mismatch)                 want = SCR_FW_MISMATCH;
        else if (err_latched)            want = SCR_ERROR;
        /* MAJ-04: the splash used to outrank every FSM state for its whole
         * 10 s hold. Inputs are live long before that and linking completes in
         * about a second, so the remote could sit in ARMED — relay closed,
         * base siren running, buzzer heartbeat — under a "Connected to base"
         * splash for the best part of nine seconds. Same argument as the
         * fire_done escape below, with more force. */
        else if (now_ms() < splash_until_ms && !live_state && !alarmed_state)
                                         want = SCR_SPLASH;
        /* fire_done outranks the FSM-derived screen, which was harmless while
         * the screen and the base's cooldown both ended at 2000 ms. Now that
         * the screen outlives the cooldown, the operator can re-arm while it
         * is still up — and a summary of the last shot must never sit on top
         * of a live ARMED/PRE_FIRE/FIRING state. Cancel it the moment the FSM
         * leaves an idle state.
         *
         * MAJ-05: LINK_LOST cancels it too. A base powered down right after a
         * shot put the FSM into LINK_LOST with its alarm sounding while a
         * green FIRE COMPLETE screen sat on top for the rest of the hold, the
         * igniter line greying out underneath. FSD §10.2.4a's cancel list was
         * updated to match. */
        else if (fire_done && !live_state && !alarmed_state)
                                         want = SCR_FIRE_COMPLETE;
        else                             want = screen_for_state(&d);

        /* A retiring overlay leaves a hole — force a full redraw. */
        bool full = (want != current) || (overlay_drawn && !overlay_on);

        if (full) {
            switch (want) {
                case SCR_SPLASH:        draw_splash_static();        break;
                case SCR_MAIN:          draw_main_static();          break;
                case SCR_ARMED:         draw_armed_static();         break;
                case SCR_FIRING:        draw_firing_static();        break;
                case SCR_FIRE_COMPLETE: draw_fire_complete_static(); break;
                case SCR_LINK_LOST:     draw_link_lost_static();     break;
                default: break;
            }
            current = want;
            overlay_drawn = false;
        }

        bool blink_on = ((frame / 3) % 2) == 0;   /* ~1.6 Hz */

        switch (want) {
            case SCR_SPLASH:
                draw_splash_dynamic(&d,
                                    splash_att > 0 ? splash_att
                                                   : (int)d.link.linkreq_attempts,
                                    splash_until_ms);
                break;
            case SCR_MAIN:
                draw_main_dynamic(&d);
                break;
            case SCR_ARMED:
                draw_armed_dynamic(&d, blink_on);
                break;
            case SCR_FIRING:
                draw_firing_dynamic(&d, blink_on);
                break;
            case SCR_FIRE_COMPLETE:
                draw_fire_complete_dynamic(&d, fire_done_ch, fire_until);
                break;
            case SCR_LINK_LOST:
                draw_link_lost_dynamic(&d);
                break;
            /* These two have no dynamic half, so the band is driven from here.
             * It still updates every frame: a welded relay must keep flashing
             * even on a latched ERROR screen, and the arm state can change
             * underneath a halted remote. Both draw an 8 px frame, hence the
             * inset. */
            case SCR_ERROR:
                if (full) draw_error_screen(err_text);
                draw_status_band(&d, VBAND_Y, 8, true);
                break;
            /* No band on FW_MISMATCH: the link never reaches LINKED, so
             * system_status() can only return SYS_UNKNOWN — always grey, no
             * information, and it displaced the "reflash both units" text. */
            case SCR_FW_MISMATCH:
                if (full) draw_fw_mismatch(fw_base, fw_remote);
                break;
            default:
                break;
        }

        if (overlay_on) {
            draw_overlay(overlay_txt, overlay_nack);
            overlay_drawn = true;
        }

        flush();

        /* DS-01 / FSD §5.5.6: 5 s panel-ID re-read, run here so it is
         * serialised with the frame writes above. Skipped while an FSM event
         * is already pending would be pointless — the check is cheap (one
         * short SPI read) next to a full flush. */
        if (now_ms() - last_health_ms >= DISPLAY_HEALTH_INTERVAL_MS) {
            last_health_ms = now_ms();
            if (display_health_check()) {
                ESP_LOGE(TAG, "DISPLAY FAILED — notifying FSM");
                /* Latch the error screen too. It may well not be visible —
                 * that is the whole point — but if the fault is intermittent
                 * the operator must not come back to a stale ARMED screen. */
                display_error("DISPLAY FAULT");
                QueueHandle_t q = remote_fsm_get_queue();
                if (q) {
                    rlc_fsm_event_t ev = {0};
                    ev.type = EVT_DISPLAY_FAULT;
                    /* Short blocking send: this is a safety event, and
                     * display_task (prio 2) can afford to wait. */
                    if (xQueueSend(q, &ev, pdMS_TO_TICKS(10)) != pdTRUE) {
                        /* R-MIN4: no longer terminal — retried every frame
                         * until the FSM drains its queue. */
                        ESP_LOGE(TAG, "FSM queue full — EVT_DISPLAY_FAULT deferred");
                        s_fault_send_pending = true;
                    }
                }
            }
        }

        frame++;
        esp_task_wdt_reset();

        /* Fixed-period pacing. A plain vTaskDelay here delays DISPLAY_FRAME_MS
         * *after* the frame's work, so the period was work + 100 ms and could
         * never be the 100 ms FSD §10.3 requires of the pre-fire countdown —
         * it measured 300 ms before the flush was made incremental. Delaying
         * until a fixed wake time makes the period 100 ms regardless of how
         * long the frame took, as long as it took less than that. */
        if (!xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DISPLAY_FRAME_MS))) {
            /* The frame overran its budget — a full redraw on a screen change
             * still does, at ~230 ms.
             *
             * Re-basing alone is NOT enough, and 1.2.12 found out the hard
             * way. xTaskDelayUntil returns pdFALSE when the wake time has
             * already passed, meaning it did not block at all; re-basing to
             * now then leaves the NEXT frame to overrun the new deadline just
             * as badly. For an occasional slow frame that self-corrects, which
             * is what this guard was written for. For a frame that overruns
             * EVERY time — a 480x128 band at 153 ms average — it degenerates
             * into a free-running loop that never yields once, display_task
             * (prio 2) stays runnable 100% of the time on core 1, and
             * buzzer_task (prio 1) never runs again. The remote rebooted on
             * buzzer_task's 5 s task-watchdog, roughly six seconds into every
             * boot.
             *
             * So yield explicitly. This bounds display_task's share of core 1
             * no matter how slow a frame becomes, which is a property this
             * loop should always have had: the display is prio 2 and sits
             * above the buzzer, and the buzzer is how a remote tells its
             * operator anything when the screen is the thing that is wrong. */
            vTaskDelay(pdMS_TO_TICKS(DISPLAY_FRAME_MIN_YIELD_MS));
            last_wake = xTaskGetTickCount();
        }
    }
}

/* ── Public API ───────────────────────────────────────────────── */

int display_init(void)
{
    if (s_spi) return 0;

    s_req_mutex = xSemaphoreCreateMutex();
    if (!s_req_mutex) return -1;
    memset(&s_req, 0, sizeof(s_req));

    /* Control pins */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DISPLAY_DC) | (1ULL << PIN_DISPLAY_RST) |
                        (1ULL << PIN_DISPLAY_BL) | (1ULL << PIN_DISPLAY_CS),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_DISPLAY_BL, 1);
    gpio_set_level(PIN_DISPLAY_CS, 1);   /* deasserted before anything clocks */

    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_DISPLAY_MOSI,
        .miso_io_num     = PIN_DISPLAY_MISO,
        .sclk_io_num     = PIN_DISPLAY_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DW * 3 + 16,
    };
    if (spi_bus_initialize(DISPLAY_SPI_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return -1;
    }

    /* spics_io_num = -1 on BOTH devices: CS is driven by hand. See s_spi_rd. */
    spi_device_interface_config_t dev = {
        .clock_speed_hz = DISPLAY_SPI_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = -1,
        .queue_size     = 1,
    };
    if (spi_bus_add_device(DISPLAY_SPI_HOST, &dev, &s_spi) != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed");
        return -1;
    }

    spi_device_interface_config_t dev_rd = dev;
    dev_rd.clock_speed_hz = DISPLAY_SPI_READ_CLOCK_HZ;
    if (spi_bus_add_device(DISPLAY_SPI_HOST, &dev_rd, &s_spi_rd) != ESP_OK) {
        ESP_LOGE(TAG, "SPI read-device add failed");
        return -1;
    }

    /* Hardware reset */
    gpio_set_level(PIN_DISPLAY_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_DISPLAY_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    spi_send_cmd(ILI9488_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));
    spi_send_cmd(ILI9488_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t pw1[] = { 0x07, 0x42, 0x18 };
    spi_send_cmd(ILI9488_PWCTR1);  spi_send_data(pw1, 3);
    uint8_t pw2[] = { 0x00 };
    spi_send_cmd(ILI9488_PWCTR2);  spi_send_data(pw2, 1);
    uint8_t vm1[] = { 0x00, 0x07 };
    spi_send_cmd(ILI9488_VMCTR1);  spi_send_data(vm1, 2);

    uint8_t madctl = 0x68;  /* landscape 480x320, BGR */
    spi_send_cmd(ILI9488_MADCTL);  spi_send_data(&madctl, 1);
    uint8_t pixfmt = 0x66;  /* 18-bit RGB666, 3 bytes/pixel */
    spi_send_cmd(ILI9488_PIXFMT);  spi_send_data(&pixfmt, 1);

    uint8_t frm[] = { 0x10, 0x10 };
    spi_send_cmd(ILI9488_FRMCTR1); spi_send_data(frm, 2);
    uint8_t dfc[] = { 0x02, 0x22 };
    spi_send_cmd(ILI9488_DFUNCTR); spi_send_data(dfc, 2);

    uint8_t gmp[] = { 0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
                      0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00 };
    spi_send_cmd(ILI9488_GMCTRP1); spi_send_data(gmp, 15);
    uint8_t gmn[] = { 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
                      0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00 };
    spi_send_cmd(ILI9488_GMCTRN1); spi_send_data(gmn, 15);

    spi_send_cmd(ILI9488_DISPON);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Framebuffer in PSRAM; row bounce buffer in internal DMA-capable RAM */
    s_fb = heap_caps_malloc((size_t)DW * DH * 3, MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGE(TAG, "framebuffer alloc failed (%d bytes PSRAM)", DW * DH * 3);
        return -1;
    }
    s_line = heap_caps_malloc((size_t)DW * 3, MALLOC_CAP_DMA);
    if (!s_line) {
        ESP_LOGE(TAG, "line buffer alloc failed");
        return -1;
    }
    /* Shadow copy of what the panel was last sent (see flush()). Deliberately
     * initialised to a value the cleared framebuffer cannot match, so the
     * first flush repaints every row rather than trusting an untransmitted
     * buffer that happens to compare equal. */
    s_shadow = heap_caps_malloc((size_t)DW * DH * 3, MALLOC_CAP_SPIRAM);
    if (!s_shadow) {
        ESP_LOGE(TAG, "shadow buffer alloc failed (%d bytes PSRAM)", DW * DH * 3);
        return -1;
    }
    memset(s_fb, 0, (size_t)DW * DH * 3);
    memset(s_shadow, 0xFF, (size_t)DW * DH * 3);
    dirty_clear();
    splash_video_init();   /* non-fatal: falls back to a plain band */

    s_boot_ms = now_ms();

    /* §9.13 step 6: health check — panel ID read-back.
     *
     * Two things this used to get wrong, both of which a disconnected MOSI
     * (FSD T-S10) would have walked straight through:
     *
     * 1. The SPI status was discarded. §5.5.6 requires return codes to be
     *    checked — "a health check that succeeds only because the SPI layer
     *    swallowed an error is not a health check". The periodic check has
     *    done this since 1.1.9; the boot read had not. Same s_spi_errors
     *    snapshot pattern, so both halves now agree.
     *
     * 2. The test was `s_panel_id != 0`. A broken MOSI leaves the panel
     *    without a command to answer and MISO undriven — which reads 0x00000000
     *    (caught) or floats to 0xFFFFFFFF (NOT caught), and the remote would
     *    boot believing a dead panel healthy. §5.5.6 contradicts itself here:
     *    "any non-zero read-back is considered valid" against "only a zero or
     *    GARBAGE read-back ... is treated as a fault". All-ones is garbage, so
     *    the second clause governs. Both undriven signatures are now rejected;
     *    a real panel — including the 0x2A403300 clone this hardware uses —
     *    reports neither. */
    uint32_t spi_errors_before = s_spi_errors;
    uint8_t id[4] = {0};
    spi_read_reg(ILI9488_ID, id, 4);
    s_panel_id = ((uint32_t)id[0] << 24) | ((uint32_t)id[1] << 16) |
                 ((uint32_t)id[2] << 8) | id[3];

    bool spi_ok = (s_spi_errors == spi_errors_before);
    bool id_ok  = (s_panel_id != 0x00000000u) && (s_panel_id != 0xFFFFFFFFu);
    s_healthy = spi_ok && id_ok;

    if (!s_healthy) {
        ESP_LOGE(TAG, "panel ID read-back FAILED: id=0x%08lX, spi_errors %lu->%lu",
                 (unsigned long)s_panel_id,
                 (unsigned long)spi_errors_before,
                 (unsigned long)s_spi_errors);
    }

    /* Clear the panel to black so no garbage shows before the first frame.
     * This is the only flush in which every pixel differs from the shadow. */
    mark_dirty(0, 0, DW, DH);
    flush();

    ESP_LOGI(TAG, "ILI9488 init: %dx%d RGB666 @ %d MHz (reads %d MHz), "
                  "ID 0x%08lX (%s)",
             DW, DH, DISPLAY_SPI_CLOCK_HZ / 1000000,
             DISPLAY_SPI_READ_CLOCK_HZ / 1000000,
             (unsigned long)s_panel_id, s_healthy ? "healthy" : "ID READ FAILED");

    return 0;
}

bool display_is_healthy(void)
{
    return s_healthy;
}

uint32_t display_get_id(void)
{
    return s_panel_id;
}

int display_start_task(void)
{
    if (s_task) return 0;
    if (!s_fb) return -1;
    /* FSD §9.10: display_task — priority 2, core 1, 8192 stack */
    if (xTaskCreatePinnedToCore(display_task, "display_task", 8192, NULL, 2,
                                &s_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "display task create failed");
        return -1;
    }
    return 0;
}

void display_splash(int attempt)
{
    if (!s_req_mutex) return;
    xSemaphoreTake(s_req_mutex, portMAX_DELAY);
    s_req.splash_attempt = attempt;
    xSemaphoreGive(s_req_mutex);
}

void display_firmware_mismatch(const uint8_t *base_ver, const uint8_t *remote_ver)
{
    ESP_LOGW(TAG, "FIRMWARE MISMATCH — Base v%d.%d.%d / Remote v%d.%d.%d",
             base_ver[0], base_ver[1], base_ver[2],
             remote_ver[0], remote_ver[1], remote_ver[2]);
    if (!s_req_mutex) return;
    xSemaphoreTake(s_req_mutex, portMAX_DELAY);
    memcpy(s_req.fw_base, base_ver, 3);
    memcpy(s_req.fw_remote, remote_ver, 3);
    s_req.fw_mismatch = true;
    xSemaphoreGive(s_req_mutex);
}

/* The task derives IDLE/ARMED/FIRING/LINK_LOST from the FSM state, so these
 * remain as call sites for the FSM (and for logging) without duplicating the
 * selection logic. */
void display_armed(uint8_t channel)
{
    ESP_LOGI(TAG, "[ARMED] channel %u", channel);
}

void display_firing(uint8_t channel, uint32_t countdown_ms)
{
    ESP_LOGI(TAG, "[FIRING] channel %u, countdown %lu ms",
             channel, (unsigned long)countdown_ms);
}

void display_link_lost(uint32_t seconds_since_contact, int ping_attempts)
{
    ESP_LOGW(TAG, "[LINK LOST] %lu s ago, %d attempts",
             (unsigned long)seconds_since_contact, ping_attempts);
}

void display_fire_complete(uint8_t channel)
{
    if (!s_req_mutex) return;
    xSemaphoreTake(s_req_mutex, portMAX_DELAY);
    s_req.fire_complete_ch       = channel;
    s_req.fire_complete_until_ms = now_ms() + FIRE_COMPLETE_SCREEN_MS;
    xSemaphoreGive(s_req_mutex);
}

void display_error(const char *error_text)
{
    ESP_LOGE(TAG, "[ERROR] %s", error_text ? error_text : "");
    if (!s_req_mutex) return;
    xSemaphoreTake(s_req_mutex, portMAX_DELAY);
    snprintf(s_req.error_text, sizeof(s_req.error_text), "%s",
             error_text ? error_text : "UNKNOWN ERROR");
    s_req.error_latched = true;
    xSemaphoreGive(s_req_mutex);
}

static void overlay_post(const char *text, bool is_nack)
{
    if (!s_req_mutex || !text) return;
    xSemaphoreTake(s_req_mutex, portMAX_DELAY);
    snprintf(s_req.overlay_text, sizeof(s_req.overlay_text), "%s", text);
    s_req.overlay_is_nack   = is_nack;
    s_req.overlay_until_ms  = now_ms() + NACK_DISPLAY_DURATION_MS;
    xSemaphoreGive(s_req_mutex);
}

void display_nack(const char *reason_text)
{
    ESP_LOGW(TAG, "[NACK] %s", reason_text ? reason_text : "");
    overlay_post(reason_text, true);
}

void display_toast(const char *text)
{
    ESP_LOGI(TAG, "[TOAST] %s", text ? text : "");
    overlay_post(text, false);
}

void display_backlight(bool on)
{
    gpio_set_level(PIN_DISPLAY_BL, on ? 1 : 0);
}
