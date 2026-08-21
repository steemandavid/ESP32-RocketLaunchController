/**
 * RLC Display Driver — Phase 4 (FSD §10)
 *
 * ILI9488 480x320 SPI LCD, RGB666, SPI2_HOST @ 20 MHz.
 *
 * Rendering model:
 *   All drawing goes into a PSRAM framebuffer (480*320*3 bytes). Every write
 *   grows a dirty bounding box; `flush()` pushes only that box over SPI
 *   (FSD §10.3 partial refresh). `display_task` (prio 2, core 1 — FSD §9.10)
 *   owns the framebuffer and the SPI device: no other task ever touches SPI,
 *   so the FSM and input tasks never block on the panel.
 *
 * Screens (FSD §10.2) are selected from the remote FSM state, with latched
 * overrides for ERROR / firmware mismatch and a timed overlay for NACKs.
 */

#include "rlc_display.h"
#include "rlc_remote_fsm.h"
#include "rlc_arm_switch.h"
#include "rlc_battery.h"
#include "rlc_link.h"
#include "rlc_config.h"
#include "rlc_protocol.h"
#include "rlc_version.h"
#include "pin_config.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

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
#define C_SHORT      RLC_COLOR_CONT_SHORT
#define C_MARGINAL   RLC_COLOR_CONT_MARGINAL

/* Non-continuity accents (unchanged by the continuity palette) */
#define C_INFO       0x0078FF   /* blue   — link OK, VRO credit */
#define C_WARN       0xFFDC00   /* yellow — warnings */
#define C_FAULT      0xFF0000   /* red    — errors */
#define C_SELECTED   0x00DCFF   /* cyan   — selected channel */
#define C_ARMED_BG   0xB40000   /* red    — armed channel background */
#define C_AMBER      0xFFA000
#define C_GREEN      0x00C000

#define R8(c)  (uint8_t)(((c) >> 16) & 0xFF)
#define G8(c)  (uint8_t)(((c) >>  8) & 0xFF)
#define B8(c)  (uint8_t)( (c)        & 0xFF)

/* Character cell: 5x7 glyph + 1 px spacing, multiplied by scale */
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

static spi_device_handle_t s_spi   = NULL;
static uint8_t            *s_fb    = NULL;   /* PSRAM framebuffer, RGB666 */
static uint8_t            *s_line  = NULL;   /* internal DMA-capable row buffer */
static bool                s_healthy = false;
static int64_t             s_boot_ms = 0;     /* display_init() timestamp */
static uint32_t            s_panel_id = 0;
static TaskHandle_t        s_task = NULL;

/* Dirty bounding box (inclusive); x0 > x1 means "clean" */
static int s_dx0, s_dy0, s_dx1, s_dy1;

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
    int      splash_max;
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

static void spi_send_cmd(uint8_t cmd)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    dc_cmd();
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_send_data(const uint8_t *data, int len)
{
    if (len <= 0) return;
    spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = data };
    dc_data();
    spi_device_polling_transmit(s_spi, &t);
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
    spi_device_polling_transmit(s_spi, &t);

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

static void draw_text_centred_bg(int y, const char *s, int scale,
                                 uint32_t fg, uint32_t bg)
{
    fill_rect(0, y, DW, CHAR_H(scale), bg);
    draw_text((DW - text_width(s, scale)) / 2, y, s, scale, fg);
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
        case CONT_SHORT:    return C_SHORT;
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
        case CONT_SHORT:     return "CONNECTED";  /* deprecated, folded in */
        default:             return "OPEN";
    }
}

/* Continuity glyph per FSD §10.2.2:
 * CONNECTED = filled circle, MARGINAL = triangle, OPEN = empty circle.
 * Shape carries the meaning as well as colour, so the screen stays readable
 * for colour-blind operators. The diamond that marked SHORT is retired with
 * that band; a deprecated value from a pre-merge peer draws as CONNECTED. */
static void draw_continuity_glyph(int cx, int cy, int r, uint8_t band)
{
    uint32_t c = continuity_colour(band);
    switch (band) {
        case CONT_CONNECTED:
        case CONT_SHORT:    fill_circle(cx, cy, r, c);        break;
        case CONT_MARGINAL: fill_triangle_up(cx, cy, r, c);   break;
        default:            draw_circle(cx, cy, r, 3, c);     break;
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

static void flush(void)
{
    if (!s_fb || !s_spi || dirty_empty()) return;

    int x0 = s_dx0 < 0 ? 0 : s_dx0;
    int y0 = s_dy0 < 0 ? 0 : s_dy0;
    int x1 = s_dx1 >= DW ? DW - 1 : s_dx1;
    int y1 = s_dy1 >= DH ? DH - 1 : s_dy1;
    int w  = x1 - x0 + 1;

    set_window(x0, y0, x1, y1);

    /* The panel auto-increments inside the window, so the rows of the
     * dirty box can be streamed back to back. Rows are copied through an
     * internal-RAM bounce buffer: the framebuffer lives in PSRAM. */
    for (int y = y0; y <= y1; y++) {
        memcpy(s_line, s_fb + ((size_t)y * DW + x0) * 3, (size_t)w * 3);
        spi_send_data(s_line, w * 3);
    }

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
    d->status_fresh      = remote_fsm_get_status(&d->status);
    d->prefire_remain_ms = remote_fsm_get_prefire_remaining_ms();
    rlc_link_get_status(&d->link);
}

/* ── Base arm state, derived from two distinct signals ────────────
 *
 * The key switch (a precondition the operator controls) and the arm sense
 * (the arm relay COM output — the actual hazard) answer different questions,
 * so collapsing them into ARMED/SAFE loses the one that matters. In
 * particular, a welded arm relay leaves the fire path live with the key
 * turned OFF: keying off the display would print SAFE over an energised
 * igniter circuit. ARMED is therefore driven by the arm sense, never the key.
 */
typedef enum {
    BASE_ARM_UNKNOWN = 0,   /* no fresh status — never claim SAFE */
    BASE_ARM_SAFE,          /* key off, fire path dead */
    BASE_ARM_READY,         /* key turned, path still dead — arming permitted */
    BASE_ARM_ARMED,         /* arm relay closed, VBAT live on the fire path */
    BASE_ARM_WELD,          /* sense HIGH while the relay should be de-energised */
} base_arm_state_t;

static base_arm_state_t base_arm_state(const disp_data_t *d)
{
    if (!d->status_fresh) return BASE_ARM_UNKNOWN;

    bool sense = d->status.base_arm_sense != 0;
    bool key   = d->status.base_key_switch != 0;

    /* The relay is only meant to be energised in the firing path states. Sense
     * HIGH anywhere else means the contacts are closed when they should not be.
     * Checked here as well as via ERR_RELAY_FAULT so the warning appears before
     * the base's own weld confirm count elapses. */
    uint8_t st = d->status.base_state;
    bool relay_expected_on = (st == STATE_ARMED || st == STATE_PRE_FIRE ||
                              st == STATE_FIRING);

    if (d->status.error_flags & ERR_RELAY_FAULT) return BASE_ARM_WELD;
    if (sense && !relay_expected_on)             return BASE_ARM_WELD;
    if (sense)                                   return BASE_ARM_ARMED;
    if (key)                                     return BASE_ARM_READY;
    return BASE_ARM_SAFE;
}

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

static uint32_t base_arm_colour(base_arm_state_t s, bool blink_on)
{
    switch (s) {
        case BASE_ARM_SAFE:  return C_GREEN;
        case BASE_ARM_READY: return C_AMBER;
        case BASE_ARM_ARMED: return C_FAULT;
        case BASE_ARM_WELD:  return blink_on ? C_FAULT : C_WARN;  /* flashing */
        default:             return C_GREY;
    }
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

    /* Link state + round-trip time */
    const char *lnk = "LINK ??";
    switch (d->link.state) {
        case RLC_LINK_STATE_LINKED:  lnk = "LINK OK"; break;
        case RLC_LINK_STATE_LINKING: lnk = "LINKING"; break;
        case RLC_LINK_STATE_LOST:    lnk = "NO LINK"; break;
        default:                     lnk = "LINK --"; break;
    }
    draw_field(6, BAR_TXT_Y2, 7 * CHAR_W(2), lnk, 2,
               linked ? C_WHITE : C_WARN, C_BLACK);

    if (linked && d->link.ping_rtt_ms > 0) {
        snprintf(buf, sizeof(buf), "%3ums", d->link.ping_rtt_ms);
    } else {
        snprintf(buf, sizeof(buf), "  --ms");
    }
    draw_field(6 + 7 * CHAR_W(2) + 12, BAR_TXT_Y2, 6 * CHAR_W(2), buf, 2,
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
 * so the legend and two status rows all fit at that size. */
#define GRID_Y      58
#define CELL_W      118
#define CELL_H      80
#define GRID_X      6

static void cell_origin(int ch, int *x, int *y)
{
    int idx = ch - 1;
    *x = GRID_X + (idx % 4) * (CELL_W + 2);
    *y = GRID_Y + (idx / 4) * (CELL_H + 4);
}

static void draw_legend(void)
{
    int y = GRID_Y + 2 * (CELL_H + 4) + 4;
    fill_rect(0, y, DW, CHAR_H(2), C_BLACK);

    struct { uint8_t band; const char *txt; } items[] = {
        { CONT_CONNECTED, "CONNECTED" },
        { CONT_MARGINAL,  "MARGINAL" },
        { CONT_OPEN,      "OPEN" },
    };
    int x = 8;
    for (int i = 0; i < 3; i++) {
        draw_continuity_glyph(x + 6, y + 8, 6, items[i].band);
        draw_text(x + 18, y, items[i].txt, 2, C_GREY);
        x += 18 + text_width(items[i].txt, 2) + 18;
    }
}

static void draw_main_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_top_bar_static();
    draw_legend();
}

static void draw_channel_cell(int ch, const disp_data_t *d)
{
    int x, y;
    cell_origin(ch, &x, &y);

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
     * Labels are abbreviated so the whole row fits at scale 2. */
    char buf[64];
    int y = DH - 66;
    snprintf(buf, sizeof(buf), "SEL CH %u", d->selected);
    draw_field(6, y, 8 * CHAR_W(2), buf, 2, C_SELECTED, C_BLACK);

    /* BASE reflects the fire path, REMOTE the operator's own switch.
     * "SEL CH 1   BASE READY   REMOTE ARMED" is 36 of the 40 characters
     * available at the scale-2 font floor. */
    base_arm_state_t bs = base_arm_state(d);
    bool weld_blink = ((now_ms() / 400) % 2) == 0;
    snprintf(buf, sizeof(buf), "BASE %s   REMOTE %s",
             base_arm_label(bs),
             d->remote_key_armed ? "ARMED" : "SAFE");
    draw_field(120, y, DW - 126, buf, 2, base_arm_colour(bs, weld_blink), C_BLACK);

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
        draw_text_centred_bg(DH - 30, buf, 2, C_FAULT, C_BLACK);
    } else if (!d->remote_key_armed) {
        snprintf(buf, sizeof(buf), "TURN ARM KEY TO ARM CH %u", d->selected);
        draw_text_centred_bg(DH - 30, buf, 2, C_GREY, C_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "HOLD ENCODER TO ARM CH %u", d->selected);
        draw_text_centred_bg(DH - 30, buf, 2, C_WARN, C_BLACK);
    }
}

/* ── Screen: ARMED — FSD §10.2.3 ──────────────────────────────── */

#define BOX_X   60
#define BOX_Y   70
#define BOX_W   360
#define BOX_H   180

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

    uint8_t band = CONT_OPEN;
    if (d->status_fresh && d->armed >= 1 && d->armed <= 8) {
        band = (uint8_t)((d->status.continuity_bands >> (2 * (d->armed - 1))) & 0x3);
    }
    snprintf(buf, sizeof(buf), "CONTINUITY %s", continuity_label(band));
    draw_text_centred_bg(BOX_Y + 112, buf, 2, continuity_colour(band), C_BLACK);

    draw_text_centred(BOX_Y + 140, "HOLD FIRE TO LAUNCH", 2, C_WHITE);

    /* This line used to read "SENSE CONFIRMED" from the KEY switch, asserting
     * arm-relay confirmation the remote had never been sent. It now comes from
     * the real arm sense, so NOT CONFIRMED means the relay has not verified. */
    bool sense_ok = d->status_fresh && d->status.base_arm_sense;
    snprintf(buf, sizeof(buf), "ARM SENSE %s   REMOTE %s",
             sense_ok ? "OK" : (d->status_fresh ? "NOT OK" : "?"),
             d->remote_key_armed ? "ARMED" : "SAFE");
    draw_text_centred_bg(DH - 26, buf, 2, sense_ok ? C_GREEN : C_FAULT, C_BLACK);
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
        draw_text_centred_bg(BOX_Y + 92, "IGNITION ACTIVE", 3, C_WHITE, C_ARMED_BG);
    } else {
        /* Pre-fire countdown, refreshed every 100 ms (FSD §10.3) */
        uint32_t ms = d->prefire_remain_ms;
        snprintf(buf, sizeof(buf), "PRE-FIRE %lu.%lus",
                 (unsigned long)(ms / 1000), (unsigned long)((ms % 1000) / 100));
        draw_text_centred_bg(BOX_Y + 92, buf, 3, C_WARN, C_BLACK);
    }

    draw_text_centred_bg(BOX_Y + 138, "RELEASE TO ABORT", 2,
                         C_WHITE, igniting ? C_ARMED_BG : C_BLACK);
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

    draw_text_centred(BOX_Y + 32, "FIRE COMPLETE", 3, C_GREEN);
    snprintf(buf, sizeof(buf), "CHANNEL %u", ch);
    draw_text_centred(BOX_Y + 80, buf, 3, C_WHITE);

    int64_t left = until_ms - now_ms();
    if (left < 0) left = 0;
    snprintf(buf, sizeof(buf), "IDLE IN %lld.%llds",
             (long long)(left / 1000), (long long)((left % 1000) / 100));
    draw_text_centred_bg(BOX_Y + 132, buf, 2, C_GREY, C_BLACK);
}

/* ── Screen: link lost — FSD §10.2.5 ──────────────────────────── */

static void draw_link_lost_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_frame(0, 0, DW, DH, 8, C_AMBER);
    draw_text_centred(40, "! LINK LOST !", 4, C_AMBER);
    draw_text_centred(110, "No response from base unit", 2, C_WHITE);
    draw_text_centred(148, "All channels disarmed (assumed)", 2, C_WHITE);
    draw_text_centred(250, "Attempting to reconnect...", 2, C_WHITE);
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
    draw_text_centred_bg(185, buf, 2, C_WHITE, C_BLACK);

    /* Reconnect attempts, not ping misses — linkreq_attempts is the count that
     * actually advances while the remote is retrying LINK_REQUEST. */
    snprintf(buf, sizeof(buf), "Attempts %u   RSSI %d dBm",
             d->link.linkreq_attempts, d->link.rssi_avg_dbm);
    draw_text_centred_bg(288, buf, 2, C_GREY, C_BLACK);
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
    for (int off = 0; off < len && y < 220; off += per_line) {
        int n = len - off;
        if (n > per_line) n = per_line;
        memcpy(line, text + off, n);
        line[n] = '\0';
        draw_text_centred(y, line, 2, C_WHITE);
        y += CHAR_H(2) + 6;
    }

    draw_text_centred(248, "System halted - power cycle", 2, C_WARN);
}

/* ── Screen: splash / firmware mismatch — FSD §10.2.1 ─────────── */

static void draw_splash_static(void)
{
    fill_rect(0, 0, DW, DH, C_BLACK);
    draw_text_centred(26, "ESP32 WIRELESS ROCKET", 3, C_WHITE);
    draw_text_centred(64, "LAUNCH CONTROLLER", 3, C_WHITE);
    draw_text_centred(106, "v" RLC_VERSION_STRING, 2, C_SELECTED);

    fill_rect(90, 138, DW - 180, 1, C_DGREY);
    draw_text_centred(152, "VRO - VLAAMSE RAKET ORGANISATIE", 2, C_INFO);

    draw_text_centred(DH - 26, "(C) 2026 David Steeman", 2, C_GREY);
}

static void draw_splash_dynamic(const disp_data_t *d, int attempt,
                                int max_attempts, int64_t hold_until_ms)
{
    char buf[40];
    if (max_attempts <= 0) max_attempts = LINK_REQUEST_MAX_RETRIES;
    if (attempt > max_attempts) attempt = max_attempts;

    bool linked = (d->link.state == RLC_LINK_STATE_LINKED);

    draw_text_centred_bg(196, linked ? "Connected to base" : "Connecting to base...",
                         2, linked ? C_GREEN : C_WHITE, C_BLACK);

    if (linked) {
        snprintf(buf, sizeof(buf), "RSSI %d dBm", d->link.rssi_avg_dbm);
    } else {
        snprintf(buf, sizeof(buf), "Attempt %d / %d", attempt, max_attempts);
    }
    draw_text_centred_bg(228, buf, 2, C_WHITE, C_BLACK);

    /* Once linked, the bar runs out the remaining splash hold so the operator
     * can see how long the screen stays up (SPLASH_MIN_DURATION_MS). */
    int pct;
    if (linked) {
        int64_t left = hold_until_ms - now_ms();
        if (left < 0) left = 0;
        pct = 100 - (int)((left * 100) / SPLASH_MIN_DURATION_MS);
    } else {
        pct = (attempt * 100) / max_attempts;
    }
    draw_bar(90, 262, 300, 20, pct, linked ? C_GREEN : C_SELECTED, C_BLACK);
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

#define DISPLAY_FRAME_MS  100   /* 10 Hz — FSD §10.3 requires >= 5 Hz */

static void display_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "display task started (prio 2, core 1)");

    screen_t current = SCR_NONE;
    bool     overlay_drawn = false;
    int      frame = 0;

    while (1) {
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
        int      splash_max   = s_req.splash_max;
        bool     overlay_on   = (s_req.overlay_until_ms > now_ms());
        bool     overlay_nack = s_req.overlay_is_nack;
        char     overlay_txt[40];
        memcpy(overlay_txt, s_req.overlay_text, sizeof(overlay_txt));
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

        screen_t want;
        if (fw_mismatch)                 want = SCR_FW_MISMATCH;
        else if (err_latched)            want = SCR_ERROR;
        else if (now_ms() < splash_until_ms) want = SCR_SPLASH;
        else if (fire_done)              want = SCR_FIRE_COMPLETE;
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
                                                   : (int)d.link.linkreq_attempts + 1,
                                    splash_max, splash_until_ms);
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
            case SCR_ERROR:
                if (full) draw_error_screen(err_text);
                break;
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
        frame++;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_FRAME_MS));
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
                        (1ULL << PIN_DISPLAY_BL),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_DISPLAY_BL, 1);

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

    spi_device_interface_config_t dev = {
        .clock_speed_hz = DISPLAY_SPI_CLOCK_HZ,
        .mode           = 0,
        .spics_io_num   = PIN_DISPLAY_CS,
        .queue_size     = 1,
    };
    if (spi_bus_add_device(DISPLAY_SPI_HOST, &dev, &s_spi) != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed");
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
    memset(s_fb, 0, (size_t)DW * DH * 3);
    dirty_clear();
    s_boot_ms = now_ms();

    /* §9.13 step 6: health check — panel ID read-back */
    uint8_t id[4] = {0};
    spi_read_reg(ILI9488_ID, id, 4);
    s_panel_id = ((uint32_t)id[0] << 24) | ((uint32_t)id[1] << 16) |
                 ((uint32_t)id[2] << 8) | id[3];
    s_healthy = (s_panel_id != 0);

    /* Clear the panel to black so no garbage shows before the first frame */
    mark_dirty(0, 0, DW, DH);
    flush();

    ESP_LOGI(TAG, "ILI9488 init: %dx%d RGB666 @ %d MHz, ID 0x%08lX (%s)",
             DW, DH, DISPLAY_SPI_CLOCK_HZ / 1000000,
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

void display_splash(int attempt, int max_attempts)
{
    if (!s_req_mutex) return;
    xSemaphoreTake(s_req_mutex, portMAX_DELAY);
    s_req.splash_attempt = attempt;
    s_req.splash_max     = max_attempts;
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
void display_main_status(void) { }

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
    s_req.fire_complete_until_ms = now_ms() + POST_FIRE_COOLDOWN_MS;
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
