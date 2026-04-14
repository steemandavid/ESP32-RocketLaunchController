#include "hw_display.h"
#include "pin_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "hw_disp";

static spi_device_handle_t s_spi = NULL;

/* ILI9488 commands */
#define ILI9488_SWRESET     0x01
#define ILI9488_SLPIN       0x10
#define ILI9488_SLPOUT      0x11
#define ILI9488_DISPOFF     0x28
#define ILI9488_DISPON      0x29
#define ILI9488_CASET       0x2A
#define ILI9488_PASET       0x2B
#define ILI9488_RAMWR       0x2C
#define ILI9488_RAMRD       0x2E
#define ILI9488_MADCTL      0x36
#define ILI9488_PIXFMT      0x3A
#define ILI9488_FRMCTR1     0xB1
#define ILI9488_DFUNCTR     0xB6
#define ILI9488_PWCTR1      0xC0
#define ILI9488_PWCTR2      0xC1
#define ILI9488_VMCTR1      0xC5
#define ILI9488_GMCTRP1     0xE0
#define ILI9488_GMCTRN1     0xE1
#define ILI9488_ID          0x04

/* 5x7 bitmap font (ASCII 0x20-0x7E) */
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

static inline void dc_data(void)  { gpio_set_level(PIN_DISP_DC, 1); }
static inline void dc_cmd(void)   { gpio_set_level(PIN_DISP_DC, 0); }

static void spi_send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    dc_cmd();
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_send_data(const uint8_t *data, int len)
{
    if (len <= 0) return;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    dc_data();
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_read_data(uint8_t cmd, uint8_t *buf, int len)
{
    int total = 1 + len;
    uint8_t *tx = calloc(1, total);
    uint8_t *rx = calloc(1, total);
    if (!tx || !rx) { free(tx); free(rx); return; }

    tx[0] = cmd;

    gpio_set_level(PIN_DISP_CS, 0);
    dc_cmd();

    spi_transaction_t t = {
        .length    = total * 8,
        .rxlength  = total * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_spi, &t);

    gpio_set_level(PIN_DISP_CS, 1);

    memcpy(buf, rx + 1, len);
    free(tx);
    free(rx);
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t buf[4];
    spi_send_cmd(ILI9488_CASET);
    buf[0] = (x0 >> 8) & 0xFF; buf[1] = x0 & 0xFF;
    buf[2] = (x1 >> 8) & 0xFF; buf[3] = x1 & 0xFF;
    spi_send_data(buf, 4);

    spi_send_cmd(ILI9488_PASET);
    buf[0] = (y0 >> 8) & 0xFF; buf[1] = y0 & 0xFF;
    buf[2] = (y1 >> 8) & 0xFF; buf[3] = y1 & 0xFF;
    spi_send_data(buf, 4);

    spi_send_cmd(ILI9488_RAMWR);
}

static void hardware_reset(void)
{
    gpio_set_level(PIN_DISP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_DISP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

void hw_display_init(void)
{
    if (s_spi) {
        ESP_LOGI(TAG, "Display already initialised");
        return;
    }

    /* Configure control pins */
    uint64_t mask = (1ULL << PIN_DISP_DC)
                  | (1ULL << PIN_DISP_RST) | (1ULL << PIN_DISP_BACKLIGHT);
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* Backlight on */
    gpio_set_level(PIN_DISP_BACKLIGHT, 1);

    /* SPI bus init */
    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_DISP_MOSI,
        .miso_io_num     = PIN_DISP_MISO,
        .sclk_io_num     = PIN_DISP_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 3 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = DISPLAY_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = PIN_DISP_CS,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(DISPLAY_SPI_HOST, &dev, &s_spi));

    /* Hardware reset */
    hardware_reset();

    /* Software reset */
    spi_send_cmd(ILI9488_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Out of sleep */
    spi_send_cmd(ILI9488_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    /* Power control 1 */
    spi_send_cmd(ILI9488_PWCTR1);
    uint8_t pw1[] = { 0x07, 0x42, 0x18 };
    spi_send_data(pw1, 3);

    /* Power control 2 */
    spi_send_cmd(ILI9488_PWCTR2);
    uint8_t pw2[] = { 0x00 };
    spi_send_data(pw2, 1);

    /* VCOM control */
    spi_send_cmd(ILI9488_VMCTR1);
    uint8_t vm1[] = { 0x00, 0x07 };
    spi_send_data(vm1, 2);

    /* Memory access control: landscape (480x320) */
    spi_send_cmd(ILI9488_MADCTL);
    uint8_t madctl = 0x68;  /* MY=0, MX=1, MV=1, ML=0, BGR */
    spi_send_data(&madctl, 1);

    /* Pixel format: 18-bit RGB666 (3 bytes per pixel) */
    spi_send_cmd(ILI9488_PIXFMT);
    uint8_t pixfmt = 0x66;
    spi_send_data(&pixfmt, 1);

    /* Frame rate */
    spi_send_cmd(ILI9488_FRMCTR1);
    uint8_t frm[] = { 0x10, 0x10 };
    spi_send_data(frm, 2);

    /* Display function control */
    spi_send_cmd(ILI9488_DFUNCTR);
    uint8_t dfc[] = { 0x02, 0x22 };
    spi_send_data(dfc, 2);

    /* Gamma positive */
    spi_send_cmd(ILI9488_GMCTRP1);
    uint8_t gmp[] = { 0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
                      0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00 };
    spi_send_data(gmp, 15);

    /* Gamma negative */
    spi_send_cmd(ILI9488_GMCTRN1);
    uint8_t gmn[] = { 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
                      0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00 };
    spi_send_data(gmn, 15);

    /* Display on */
    spi_send_cmd(ILI9488_DISPON);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Clear to black */
    display_fill(0, 0, 0);

    ESP_LOGI(TAG, "ILI9488 display initialised (%dx%d, RGB666, %d MHz)",
             DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_SPI_FREQ_HZ / 1000000);
}

bool display_read_id(uint32_t *out_id)
{
    uint8_t buf[4] = {0};
    spi_read_data(ILI9488_ID, buf, 4);
    if (out_id) {
        *out_id = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
                | ((uint32_t)buf[2] << 8)  | buf[3];
    }
    return (buf[0] != 0 || buf[1] != 0 || buf[2] != 0 || buf[3] != 0);
}

void display_fill(uint8_t r, uint8_t g, uint8_t b)
{
    set_window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);

    /* Send in chunks — 3 bytes per pixel (RGB666) */
    int pixels = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    int chunk  = 4096;
    uint8_t *buf = malloc(chunk * 3);
    if (!buf) return;

    for (int i = 0; i < chunk; i++) {
        buf[i * 3]     = r;
        buf[i * 3 + 1] = g;
        buf[i * 3 + 2] = b;
    }

    int remaining = pixels;
    while (remaining > 0) {
        int n = remaining > chunk ? chunk : remaining;
        spi_send_data(buf, n * 3);
        remaining -= n;
    }
    free(buf);
}

void display_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    set_window(x, y, x, y);
    uint8_t buf[3] = { r, g, b };
    spi_send_data(buf, 3);
}

void display_rect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    set_window(x, y, x + w - 1, y + h - 1);

    int total = w * h;
    int chunk = 4096;
    uint8_t *buf = malloc(chunk * 3);
    if (!buf) return;

    for (int i = 0; i < chunk; i++) {
        buf[i * 3]     = r;
        buf[i * 3 + 1] = g;
        buf[i * 3 + 2] = b;
    }

    int remaining = total;
    while (remaining > 0) {
        int n = remaining > chunk ? chunk : remaining;
        spi_send_data(buf, n * 3);
        remaining -= n;
    }
    free(buf);
}

static void draw_char(int x, int y, char ch, int scale, uint8_t r, uint8_t g, uint8_t b)
{
    if (ch < 0x20 || ch > 0x7E) ch = 0x20;
    int idx = ch - 0x20;
    for (int col = 0; col < 5; col++) {
        uint8_t bits = font5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                display_rect(x + col * scale, y + row * scale, scale, scale, r, g, b);
            }
        }
    }
}

void display_text(const char *str)
{
    if (!str) return;
    int len = strlen(str);
    int scale = 2;
    int char_w = 6 * scale;  /* (5 pixels + 1 spacing) * scale */
    int char_h = 8 * scale;  /* (7 pixels + 1 spacing) * scale */
    int total_w = len * char_w;
    int total_h = char_h;
    int x_start = (DISPLAY_WIDTH - total_w) / 2;
    int y_start = (DISPLAY_HEIGHT - total_h) / 2;
    if (x_start < 0) x_start = 0;

    /* Clear to black */
    display_fill(0, 0, 0);

    for (int c = 0; c < len; c++) {
        draw_char(x_start + c * char_w, y_start, str[c], scale, 255, 255, 255);
    }
}

void display_grid(void)
{
    display_fill(0, 0, 0);

    /* 4 columns x 2 rows grid for channels 1-8 */
    int cols = 4, rows = 2;
    int cell_w = DISPLAY_WIDTH / cols;
    int cell_h = DISPLAY_HEIGHT / rows;

    static const uint8_t ch_colours[][3] = {
        {0,0,255}, {0,255,0}, {255,0,0}, {255,140,0},
        {255,255,0}, {0,255,255}, {255,0,255}, {255,255,255}
    };

    for (int ch = 0; ch < 8; ch++) {
        int col = ch % cols;
        int row = ch / cols;
        int x = col * cell_w;
        int y = row * cell_h;

        /* Cell border */
        display_rect(x, y, cell_w, 1, 80, 80, 80);
        display_rect(x, y, 1, cell_h, 80, 80, 80);

        /* Colour indicator square (16x16 centred) */
        int bx = x + (cell_w - 16) / 2;
        int by = y + (cell_h - 16) / 2 - 6;
        display_rect(bx, by, 16, 16,
                     ch_colours[ch][0], ch_colours[ch][1], ch_colours[ch][2]);

        /* Channel number */
        char num = '1' + ch;
        draw_char(x + (cell_w - 10) / 2, by + 20, num, 2, 255, 255, 255);
    }
}

void display_gradient(void)
{
    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        uint8_t v = (uint8_t)((x * 255) / (DISPLAY_WIDTH - 1));
        display_rect(x, 0, 1, DISPLAY_HEIGHT, v, v, v);
    }
}

void display_test(void)
{
    printf("Red fill...\r\n");
    display_fill(255, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("Green fill...\r\n");
    display_fill(0, 255, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("Blue fill...\r\n");
    display_fill(0, 0, 255);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("White fill...\r\n");
    display_fill(255, 255, 255);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("Black fill...\r\n");
    display_fill(0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("Horizontal colour bars...\r\n");
    int bar_h = DISPLAY_HEIGHT / 8;
    uint8_t colours[][3] = {
        {255,0,0}, {255,127,0}, {255,255,0}, {0,255,0},
        {0,255,255}, {0,0,255}, {127,0,255}, {255,0,255}
    };
    for (int i = 0; i < 8; i++) {
        display_rect(0, i * bar_h, DISPLAY_WIDTH, bar_h,
                     colours[i][0], colours[i][1], colours[i][2]);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("Gradient...\r\n");
    display_gradient();
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("Text test...\r\n");
    display_text("RLC Remote HW Test");

    printf("Display test complete.\r\n");
}

void display_speed(void)
{
    int64_t t0 = esp_timer_get_time();
    display_fill(255, 0, 0);
    int64_t t1 = esp_timer_get_time();
    int ms = (int)((t1 - t0) / 1000);
    int pixels = DISPLAY_WIDTH * DISPLAY_HEIGHT;
    printf("Full screen fill: %d ms  (%d pixels, %.1f MPix/s)\r\n",
           ms, pixels, pixels / ((t1 - t0) / 1e6) / 1e6);
}

void display_backlight(int on)
{
    gpio_set_level(PIN_DISP_BACKLIGHT, on ? 1 : 0);
}
