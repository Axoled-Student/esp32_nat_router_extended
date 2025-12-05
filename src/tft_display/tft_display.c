/**
 * @file tft_display.c
 * @brief TFT Display driver implementation for ESP32-S3 NAT Router
 * 
 * Supports ST7789/ILI9341 based TFT displays via SPI.
 */

#include "tft_display.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "TFT_DISPLAY";

/* Display panel handle */
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static bool display_initialized = false;

/* Frame buffer for drawing operations */
#define FB_LINE_SIZE (TFT_WIDTH * 2)
static uint16_t *line_buffer = NULL;

/* Simple 5x7 font bitmap */
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* Space */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, /* * */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
    {0x00, 0x08, 0x14, 0x22, 0x41}, /* < */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
    {0x41, 0x22, 0x14, 0x08, 0x00}, /* > */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x01, 0x01}, /* F */
    {0x3E, 0x41, 0x41, 0x51, 0x32}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x03, 0x04, 0x78, 0x04, 0x03}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x00, 0x7F, 0x41, 0x41}, /* [ */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* \ */
    {0x41, 0x41, 0x7F, 0x00, 0x00}, /* ] */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* ^ */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* _ */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* ` */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* a */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* b */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* c */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* d */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* e */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* f */
    {0x08, 0x14, 0x54, 0x54, 0x3C}, /* g */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* h */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* i */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* j */
    {0x00, 0x7F, 0x10, 0x28, 0x44}, /* k */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* l */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* m */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* n */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* o */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* p */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* q */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* r */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* s */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* t */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* u */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* v */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* w */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* x */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* y */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* z */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* { */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* | */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* } */
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, /* -> */
    {0x08, 0x1C, 0x2A, 0x08, 0x08}, /* <- */
};

/* Initialize backlight PWM */
static esp_err_t init_backlight(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = TFT_PIN_BL,
        .duty = 255,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    return ESP_OK;
}

/* SPI transfer done callback */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io, 
                                 esp_lcd_panel_io_event_data_t *edata, 
                                 void *user_ctx)
{
    return false;
}

esp_err_t tft_display_init(void)
{
    if (display_initialized) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing TFT display...");

    /* Initialize backlight */
    ESP_ERROR_CHECK(init_backlight());
    ESP_LOGI(TAG, "Backlight initialized");

    /* Configure GPIO for reset pin */
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << TFT_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&rst_conf));

    /* Hardware reset */
    gpio_set_level(TFT_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(TFT_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Initialize SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num = TFT_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = TFT_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2 + 8
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TFT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus initialized");

    /* Configure panel IO */
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = TFT_PIN_DC,
        .cs_gpio_num = TFT_PIN_CS,
        .pclk_hz = TFT_SPI_FREQ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TFT_SPI_HOST, 
                                              &io_config, &io_handle));
    ESP_LOGI(TAG, "Panel IO created");

    /* Configure ST7789 panel */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = TFT_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_LOGI(TAG, "Panel created");

    /* Reset and initialize the panel */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    /* Configure display orientation */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    
    /* Turn on display */
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* Allocate line buffer */
    line_buffer = heap_caps_malloc(FB_LINE_SIZE, MALLOC_CAP_DMA);
    if (line_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return ESP_ERR_NO_MEM;
    }

    display_initialized = true;
    ESP_LOGI(TAG, "TFT display initialized successfully");

    /* Clear screen with background color */
    tft_clear(COLOR_BG_PRIMARY);
    
    return ESP_OK;
}

void tft_display_deinit(void)
{
    if (!display_initialized) {
        return;
    }

    if (panel_handle) {
        esp_lcd_panel_del(panel_handle);
        panel_handle = NULL;
    }

    if (io_handle) {
        esp_lcd_panel_io_del(io_handle);
        io_handle = NULL;
    }

    if (line_buffer) {
        free(line_buffer);
        line_buffer = NULL;
    }

    spi_bus_free(TFT_SPI_HOST);
    display_initialized = false;
    ESP_LOGI(TAG, "TFT display deinitialized");
}

void tft_set_backlight(uint8_t brightness)
{
    uint32_t duty = (brightness * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void tft_clear(uint16_t color)
{
    if (!display_initialized || !line_buffer) {
        return;
    }

    /* Fill line buffer with color */
    for (int i = 0; i < TFT_WIDTH; i++) {
        line_buffer[i] = color;
    }

    /* Draw each line */
    for (int y = 0; y < TFT_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, TFT_WIDTH, y + 1, line_buffer);
    }
}

void tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (!display_initialized || !line_buffer) {
        return;
    }

    /* Clamp to screen bounds */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TFT_WIDTH) { w = TFT_WIDTH - x; }
    if (y + h > TFT_HEIGHT) { h = TFT_HEIGHT - y; }
    if (w <= 0 || h <= 0) return;

    /* Fill line buffer with color */
    for (int i = 0; i < w; i++) {
        line_buffer[i] = color;
    }

    /* Draw each line */
    for (int row = y; row < y + h; row++) {
        esp_lcd_panel_draw_bitmap(panel_handle, x, row, x + w, row + 1, line_buffer);
    }
}

void tft_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    tft_draw_hline(x, y, w, color);
    tft_draw_hline(x, y + h - 1, w, color);
    tft_draw_vline(x, y, h, color);
    tft_draw_vline(x + w - 1, y, h, color);
}

void tft_fill_rounded_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color)
{
    /* Draw center rectangle */
    tft_fill_rect(x + r, y, w - 2 * r, h, color);
    
    /* Draw side rectangles */
    tft_fill_rect(x, y + r, r, h - 2 * r, color);
    tft_fill_rect(x + w - r, y + r, r, h - 2 * r, color);
    
    /* Draw corner circles */
    tft_fill_circle(x + r, y + r, r, color);
    tft_fill_circle(x + w - r - 1, y + r, r, color);
    tft_fill_circle(x + r, y + h - r - 1, r, color);
    tft_fill_circle(x + w - r - 1, y + h - r - 1, r, color);
}

void tft_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    tft_fill_rect(x, y, w, 1, color);
}

void tft_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    tft_fill_rect(x, y, 1, h, color);
}

/* Draw a single pixel (used for circles and other shapes) */
static void draw_pixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || x >= TFT_WIDTH || y < 0 || y >= TFT_HEIGHT) return;
    line_buffer[0] = color;
    esp_lcd_panel_draw_bitmap(panel_handle, x, y, x + 1, y + 1, line_buffer);
}

void tft_fill_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    if (r <= 0) return;
    
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 1 - r;

    while (x >= y) {
        tft_draw_hline(x0 - x, y0 + y, 2 * x + 1, color);
        tft_draw_hline(x0 - x, y0 - y, 2 * x + 1, color);
        tft_draw_hline(x0 - y, y0 + x, 2 * y + 1, color);
        tft_draw_hline(x0 - y, y0 - x, 2 * y + 1, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x + 1);
        }
    }
}

void tft_draw_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    if (r <= 0) return;
    
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 1 - r;

    while (x >= y) {
        draw_pixel(x0 + x, y0 + y, color);
        draw_pixel(x0 - x, y0 + y, color);
        draw_pixel(x0 + x, y0 - y, color);
        draw_pixel(x0 - x, y0 - y, color);
        draw_pixel(x0 + y, y0 + x, color);
        draw_pixel(x0 - y, y0 + x, color);
        draw_pixel(x0 + y, y0 - x, color);
        draw_pixel(x0 - y, y0 - x, color);

        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x + 1);
        }
    }
}

void tft_draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size)
{
    if (!display_initialized || !text) return;

    int16_t cursor_x = x;
    int16_t cursor_y = y;

    while (*text) {
        char c = *text++;
        
        if (c == '\n') {
            cursor_x = x;
            cursor_y += 8 * size;
            continue;
        }

        if (c < 32 || c > 127) {
            c = '?';
        }

        int idx = c - 32;
        if (idx >= 0 && idx < 96) {
            for (int col = 0; col < 5; col++) {
                uint8_t line = font5x7[idx][col];
                for (int row = 0; row < 7; row++) {
                    if (line & (1 << row)) {
                        if (size == 1) {
                            draw_pixel(cursor_x + col, cursor_y + row, color);
                        } else {
                            tft_fill_rect(cursor_x + col * size, 
                                         cursor_y + row * size, 
                                         size, size, color);
                        }
                    }
                }
            }
        }
        cursor_x += 6 * size;
    }
}

void tft_draw_text_centered(int16_t y, const char *text, uint16_t color, uint8_t size)
{
    if (!text) return;
    
    int len = strlen(text);
    int text_width = len * 6 * size;
    int x = (TFT_WIDTH - text_width) / 2;
    
    tft_draw_text(x, y, text, color, size);
}

void tft_draw_wifi_icon(int16_t x, int16_t y, int8_t rssi, bool connected)
{
    uint16_t color;
    
    if (!connected) {
        color = COLOR_DANGER;
        /* Draw X mark for disconnected */
        tft_draw_text(x, y, "X", color, 2);
        return;
    }
    
    /* Determine color based on signal strength */
    if (rssi > -50) {
        color = COLOR_SUCCESS;
    } else if (rssi > -70) {
        color = COLOR_WARNING;
    } else {
        color = COLOR_DANGER;
    }

    /* Draw WiFi bars */
    int bars = 0;
    if (rssi > -50) bars = 4;
    else if (rssi > -60) bars = 3;
    else if (rssi > -70) bars = 2;
    else if (rssi > -80) bars = 1;

    int bar_width = 4;
    int gap = 2;
    
    for (int i = 0; i < 4; i++) {
        int bar_height = 4 + i * 4;
        int bar_y = y + (16 - bar_height);
        uint16_t bar_color = (i < bars) ? color : COLOR_DARK_GRAY;
        tft_fill_rect(x + i * (bar_width + gap), bar_y, bar_width, bar_height, bar_color);
    }
}

void tft_draw_progress_bar(int16_t x, int16_t y, int16_t w, int16_t h, 
                           uint8_t percent, uint16_t fg_color, uint16_t bg_color)
{
    if (percent > 100) percent = 100;
    
    /* Draw background */
    tft_fill_rounded_rect(x, y, w, h, h / 2, bg_color);
    
    /* Draw foreground */
    int fill_width = (w * percent) / 100;
    if (fill_width > 0) {
        tft_fill_rounded_rect(x, y, fill_width, h, h / 2, fg_color);
    }
}

void tft_draw_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, 
                     const uint8_t *bitmap, uint16_t color)
{
    if (!bitmap) return;
    
    int byte_width = (w + 7) / 8;
    
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int byte_idx = row * byte_width + col / 8;
            int bit_idx = 7 - (col % 8);
            
            if (bitmap[byte_idx] & (1 << bit_idx)) {
                draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void format_bytes(uint64_t bytes, char *buffer, size_t buffer_size)
{
    if (bytes < 1024) {
        snprintf(buffer, buffer_size, "%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

void format_speed(uint32_t bytes_per_sec, char *buffer, size_t buffer_size)
{
    if (bytes_per_sec < 1024) {
        snprintf(buffer, buffer_size, "%lu B/s", (unsigned long)bytes_per_sec);
    } else if (bytes_per_sec < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB/s", bytes_per_sec / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%.1f MB/s", bytes_per_sec / (1024.0 * 1024.0));
    }
}

void format_uptime(uint32_t seconds, char *buffer, size_t buffer_size)
{
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    
    if (days > 0) {
        snprintf(buffer, buffer_size, "%lud %02lu:%02lu:%02lu", 
                 (unsigned long)days, (unsigned long)hours, 
                 (unsigned long)mins, (unsigned long)secs);
    } else {
        snprintf(buffer, buffer_size, "%02lu:%02lu:%02lu", 
                 (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }
}
