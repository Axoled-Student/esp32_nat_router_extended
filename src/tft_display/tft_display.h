/**
 * @file tft_display.h
 * @brief TFT Display driver for ESP32-S3 NAT Router
 * 
 * Pin configuration based on ESP32-S3 Board Pin Map:
 * - TFT_MOSI: IO5 (SPI MOSI)
 * - TFT_SCK:  IO2 (SPI SCLK)
 * - TFT_CS:   IO15 (Chip-select)
 * - TFT_DC:   IO6 (Data/Command)
 * - TFT_RST:  IO7 (Reset)
 * - TFT_LED:  IO16 (Backlight via PWM)
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TFT Pin Definitions for ESP32-S3 */
#define TFT_PIN_MOSI    5
#define TFT_PIN_SCLK    2
#define TFT_PIN_CS      15
#define TFT_PIN_DC      6
#define TFT_PIN_RST     7
#define TFT_PIN_BL      16

/* Display dimensions - ST7789 240x320 or 240x240 */
#define TFT_WIDTH       240
#define TFT_HEIGHT      320

/* SPI Configuration */
#define TFT_SPI_HOST    SPI2_HOST
#define TFT_SPI_FREQ    40000000  /* 40 MHz */

/* Colors in RGB565 format */
#define COLOR_BLACK         0x0000
#define COLOR_WHITE         0xFFFF
#define COLOR_RED           0xF800
#define COLOR_GREEN         0x07E0
#define COLOR_BLUE          0x001F
#define COLOR_CYAN          0x07FF
#define COLOR_MAGENTA       0xF81F
#define COLOR_YELLOW        0xFFE0
#define COLOR_ORANGE        0xFD20
#define COLOR_GRAY          0x8410
#define COLOR_DARK_GRAY     0x4208
#define COLOR_LIGHT_GRAY    0xC618
#define COLOR_NAVY          0x000F
#define COLOR_DARK_GREEN    0x03E0
#define COLOR_DARK_CYAN     0x03EF
#define COLOR_MAROON        0x7800
#define COLOR_PURPLE        0x780F
#define COLOR_OLIVE         0x7BE0
#define COLOR_PINK          0xFC18
#define COLOR_TEAL          0x0410
#define COLOR_LIME          0x07E0
#define COLOR_AQUA          0x04FF
#define COLOR_SILVER        0xC618
#define COLOR_GOLD          0xFEA0

/* Theme colors */
#define COLOR_BG_PRIMARY    0x1082  /* Dark blue-gray background */
#define COLOR_BG_SECONDARY  0x2104  /* Slightly lighter background */
#define COLOR_BG_CARD       0x2965  /* Card background */
#define COLOR_ACCENT        0x3DDF  /* Accent blue */
#define COLOR_SUCCESS       0x2E8B  /* Green for success/online */
#define COLOR_WARNING       0xFE20  /* Orange for warnings */
#define COLOR_DANGER        0xF800  /* Red for errors/offline */
#define COLOR_TEXT_PRIMARY  0xFFFF  /* White text */
#define COLOR_TEXT_SECONDARY 0xB5B6 /* Gray text */

/* UI Screen IDs */
typedef enum {
    SCREEN_DASHBOARD = 0,
    SCREEN_CLIENTS,
    SCREEN_TRAFFIC,
    SCREEN_SETTINGS,
    SCREEN_ABOUT,
    SCREEN_MAX
} screen_id_t;

/* WiFi connection status */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_ERROR
} wifi_status_t;

/* Client info structure */
typedef struct {
    char ip[16];
    char mac[18];
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    bool active;
} client_info_t;

/* Router statistics */
typedef struct {
    /* Traffic stats */
    uint64_t total_rx_bytes;
    uint64_t total_tx_bytes;
    uint32_t current_rx_speed;  /* bytes per second */
    uint32_t current_tx_speed;  /* bytes per second */
    
    /* Connection status */
    wifi_status_t sta_status;
    int8_t sta_rssi;
    char sta_ssid[33];
    char sta_ip[16];
    
    /* AP status */
    uint8_t ap_clients;
    char ap_ssid[33];
    char ap_ip[16];
    
    /* System info */
    uint32_t uptime_seconds;
    uint32_t free_heap;
    float temperature;
    
    /* NAT status */
    bool nat_enabled;
} router_stats_t;

/**
 * @brief Initialize TFT display
 * @return ESP_OK on success
 */
esp_err_t tft_display_init(void);

/**
 * @brief Deinitialize TFT display
 */
void tft_display_deinit(void);

/**
 * @brief Set display backlight brightness
 * @param brightness 0-100 percent
 */
void tft_set_backlight(uint8_t brightness);

/**
 * @brief Clear the entire display
 * @param color Fill color in RGB565 format
 */
void tft_clear(uint16_t color);

/**
 * @brief Fill a rectangle area
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param color Fill color in RGB565 format
 */
void tft_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief Draw a rectangle outline
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param color Line color in RGB565 format
 */
void tft_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief Draw a rounded rectangle
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param r Corner radius
 * @param color Fill color in RGB565 format
 */
void tft_fill_rounded_rect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);

/**
 * @brief Draw a horizontal line
 * @param x X start
 * @param y Y position
 * @param w Width
 * @param color Line color
 */
void tft_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);

/**
 * @brief Draw a vertical line
 * @param x X position
 * @param y Y start
 * @param h Height
 * @param color Line color
 */
void tft_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);

/**
 * @brief Draw text at position
 * @param x X coordinate
 * @param y Y coordinate
 * @param text Text string
 * @param color Text color
 * @param size Font size multiplier (1, 2, 3...)
 */
void tft_draw_text(int16_t x, int16_t y, const char *text, uint16_t color, uint8_t size);

/**
 * @brief Draw centered text
 * @param y Y coordinate
 * @param text Text string
 * @param color Text color
 * @param size Font size multiplier
 */
void tft_draw_text_centered(int16_t y, const char *text, uint16_t color, uint8_t size);

/**
 * @brief Draw a WiFi signal strength icon
 * @param x X coordinate
 * @param y Y coordinate
 * @param rssi RSSI value in dBm
 * @param connected Whether connected
 */
void tft_draw_wifi_icon(int16_t x, int16_t y, int8_t rssi, bool connected);

/**
 * @brief Draw a progress bar
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param percent Fill percentage (0-100)
 * @param fg_color Foreground color
 * @param bg_color Background color
 */
void tft_draw_progress_bar(int16_t x, int16_t y, int16_t w, int16_t h, 
                           uint8_t percent, uint16_t fg_color, uint16_t bg_color);

/**
 * @brief Draw a circular indicator
 * @param x Center X
 * @param y Center Y
 * @param r Radius
 * @param value Current value (0-100)
 * @param color Color
 */
void tft_draw_circle(int16_t x, int16_t y, int16_t r, uint16_t color);

/**
 * @brief Fill a circle
 * @param x Center X
 * @param y Center Y
 * @param r Radius
 * @param color Fill color
 */
void tft_fill_circle(int16_t x, int16_t y, int16_t r, uint16_t color);

/**
 * @brief Draw an icon from bitmap
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param bitmap Bitmap data
 * @param color Icon color
 */
void tft_draw_bitmap(int16_t x, int16_t y, int16_t w, int16_t h, 
                     const uint8_t *bitmap, uint16_t color);

/**
 * @brief Format bytes to human readable string
 * @param bytes Number of bytes
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 */
void format_bytes(uint64_t bytes, char *buffer, size_t buffer_size);

/**
 * @brief Format speed to human readable string
 * @param bytes_per_sec Speed in bytes per second
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 */
void format_speed(uint32_t bytes_per_sec, char *buffer, size_t buffer_size);

/**
 * @brief Format uptime to string
 * @param seconds Uptime in seconds
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 */
void format_uptime(uint32_t seconds, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
