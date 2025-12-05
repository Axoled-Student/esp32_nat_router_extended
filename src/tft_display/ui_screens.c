/**
 * @file ui_screens.c
 * @brief UI Screen implementations for ESP32-S3 NAT Router TFT display
 * 
 * Beautiful dashboard UI with:
 * - Status bar with WiFi signal and connection status
 * - Traffic monitor showing upload/download speeds
 * - Connected clients list with IP and MAC addresses
 * - Settings menu for router configuration
 * - System info panel (uptime, memory, etc.)
 */

#include "ui_screens.h"
#include "tft_display.h"
#include "traffic_stats.h"
#include "router_globals.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "UI_SCREENS";

/* UI State */
static screen_id_t current_screen = SCREEN_DASHBOARD;
static bool ui_initialized = false;
static TaskHandle_t ui_task_handle = NULL;
static bool ui_task_running = false;

/* Screen names for navigation */
static const char *screen_names[] = {
    "Dashboard",
    "Clients",
    "Traffic",
    "Settings",
    "About"
};

/* UI Configuration constants */
#define UI_UPDATE_INTERVAL_MS   1000    /* Screen update interval in milliseconds */
#define UI_TASK_STACK_SIZE      4096    /* Stack size for UI task */
#define UI_TASK_PRIORITY        5       /* Priority for UI task */
#define MAX_DISPLAYED_CLIENTS   5       /* Maximum clients to show on screen */

/* UI Layout constants */
#define STATUS_BAR_HEIGHT   30
#define NAV_BAR_HEIGHT      40
#define CONTENT_START_Y     (STATUS_BAR_HEIGHT + 5)
#define CONTENT_HEIGHT      (TFT_HEIGHT - STATUS_BAR_HEIGHT - NAV_BAR_HEIGHT - 10)
#define MARGIN              10
#define CARD_PADDING        8
#define CARD_RADIUS         8

/* Draw status bar at top of screen */
static void draw_status_bar(void)
{
    /* Background */
    tft_fill_rect(0, 0, TFT_WIDTH, STATUS_BAR_HEIGHT, COLOR_BG_SECONDARY);
    
    /* Draw WiFi icon and status */
    int8_t rssi = traffic_stats_get_rssi();
    bool connected = traffic_stats_is_sta_connected();
    tft_draw_wifi_icon(MARGIN, 7, rssi, connected);
    
    /* Draw SSID or connection status */
    extern char *ssid;
    char status_text[32];
    if (connected && ssid && strlen(ssid) > 0) {
        snprintf(status_text, sizeof(status_text), "%.16s", ssid);
        tft_draw_text(MARGIN + 30, 10, status_text, COLOR_TEXT_PRIMARY, 1);
    } else {
        tft_draw_text(MARGIN + 30, 10, "Not Connected", COLOR_WARNING, 1);
    }
    
    /* Draw client count on the right */
    uint8_t client_count = traffic_stats_get_client_count();
    snprintf(status_text, sizeof(status_text), "%d", client_count);
    int x = TFT_WIDTH - MARGIN - 40;
    
    /* Client icon (simple person shape) */
    tft_fill_circle(x + 6, 10, 4, COLOR_ACCENT);
    tft_fill_rect(x + 2, 16, 8, 8, COLOR_ACCENT);
    
    tft_draw_text(x + 16, 10, status_text, COLOR_TEXT_PRIMARY, 1);
    
    /* Separator line */
    tft_draw_hline(0, STATUS_BAR_HEIGHT - 1, TFT_WIDTH, COLOR_DARK_GRAY);
}

/* Draw navigation bar at bottom */
static void draw_nav_bar(void)
{
    int y = TFT_HEIGHT - NAV_BAR_HEIGHT;
    
    /* Background */
    tft_fill_rect(0, y, TFT_WIDTH, NAV_BAR_HEIGHT, COLOR_BG_SECONDARY);
    
    /* Separator line */
    tft_draw_hline(0, y, TFT_WIDTH, COLOR_DARK_GRAY);
    
    /* Navigation dots */
    int dot_spacing = 20;
    int total_width = (SCREEN_MAX - 1) * dot_spacing;
    int start_x = (TFT_WIDTH - total_width) / 2;
    
    for (int i = 0; i < SCREEN_MAX; i++) {
        int dot_x = start_x + i * dot_spacing;
        int dot_y = y + NAV_BAR_HEIGHT / 2;
        
        if (i == current_screen) {
            tft_fill_circle(dot_x, dot_y, 5, COLOR_ACCENT);
        } else {
            tft_fill_circle(dot_x, dot_y, 3, COLOR_DARK_GRAY);
        }
    }
    
    /* Screen title */
    tft_draw_text_centered(y + 5, screen_names[current_screen], COLOR_TEXT_PRIMARY, 1);
}

/* Draw a card with title */
static void draw_card(int16_t x, int16_t y, int16_t w, int16_t h, const char *title, uint16_t title_color)
{
    /* Card background */
    tft_fill_rounded_rect(x, y, w, h, CARD_RADIUS, COLOR_BG_CARD);
    
    /* Title if provided */
    if (title != NULL) {
        tft_draw_text(x + CARD_PADDING, y + CARD_PADDING, title, title_color, 1);
        tft_draw_hline(x + CARD_PADDING, y + CARD_PADDING + 12, w - 2 * CARD_PADDING, COLOR_DARK_GRAY);
    }
}

/* Draw speed gauge */
static void draw_speed_gauge(int16_t x, int16_t y, const char *label, uint32_t speed, 
                             uint32_t max_speed, uint16_t color)
{
    char speed_str[32];
    format_speed(speed, speed_str, sizeof(speed_str));
    
    /* Label */
    tft_draw_text(x, y, label, COLOR_TEXT_SECONDARY, 1);
    
    /* Speed value */
    tft_draw_text(x, y + 12, speed_str, color, 2);
    
    /* Progress bar */
    uint8_t percent = 0;
    if (max_speed > 0 && speed > 0) {
        percent = (speed * 100) / max_speed;
        if (percent > 100) percent = 100;
    }
    tft_draw_progress_bar(x, y + 35, 100, 8, percent, color, COLOR_DARK_GRAY);
}

esp_err_t ui_init(void)
{
    if (ui_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing UI");

    /* Initialize TFT display */
    esp_err_t ret = tft_display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TFT display: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize traffic statistics */
    ret = traffic_stats_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize traffic stats: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set backlight to full */
    tft_set_backlight(100);

    ui_initialized = true;
    current_screen = SCREEN_DASHBOARD;

    ESP_LOGI(TAG, "UI initialized successfully");
    
    /* Draw initial screen */
    ui_update();
    
    return ESP_OK;
}

void ui_deinit(void)
{
    if (!ui_initialized) {
        return;
    }

    ui_stop_task();
    traffic_stats_deinit();
    tft_display_deinit();
    ui_initialized = false;
}

void ui_switch_screen(screen_id_t screen)
{
    if (screen >= SCREEN_MAX) {
        screen = SCREEN_DASHBOARD;
    }
    
    if (screen != current_screen) {
        current_screen = screen;
        ui_update();
    }
}

screen_id_t ui_get_current_screen(void)
{
    return current_screen;
}

void ui_next_screen(void)
{
    screen_id_t next = current_screen + 1;
    if (next >= SCREEN_MAX) {
        next = SCREEN_DASHBOARD;
    }
    ui_switch_screen(next);
}

void ui_prev_screen(void)
{
    screen_id_t prev = current_screen;
    if (prev == 0) {
        prev = SCREEN_MAX - 1;
    } else {
        prev--;
    }
    ui_switch_screen(prev);
}

void ui_update(void)
{
    if (!ui_initialized) {
        return;
    }

    /* Clear screen */
    tft_clear(COLOR_BG_PRIMARY);
    
    /* Draw status bar */
    draw_status_bar();
    
    /* Draw current screen content */
    switch (current_screen) {
        case SCREEN_DASHBOARD:
            ui_draw_dashboard();
            break;
        case SCREEN_CLIENTS:
            ui_draw_clients();
            break;
        case SCREEN_TRAFFIC:
            ui_draw_traffic();
            break;
        case SCREEN_SETTINGS:
            ui_draw_settings();
            break;
        case SCREEN_ABOUT:
            ui_draw_about();
            break;
        default:
            ui_draw_dashboard();
            break;
    }
    
    /* Draw navigation bar */
    draw_nav_bar();
}

void ui_draw_dashboard(void)
{
    int y = CONTENT_START_Y;
    
    /* Connection Status Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 70, "Connection Status", COLOR_ACCENT);
    
    bool sta_connected = traffic_stats_is_sta_connected();
    int8_t rssi = traffic_stats_get_rssi();
    
    /* STA Status */
    tft_draw_text(MARGIN + CARD_PADDING, y + 25, "Uplink:", COLOR_TEXT_SECONDARY, 1);
    if (sta_connected) {
        extern char *ssid;
        char buf[48];
        snprintf(buf, sizeof(buf), "%s (%d dBm)", ssid ? ssid : "Connected", rssi);
        tft_draw_text(MARGIN + 60, y + 25, buf, COLOR_SUCCESS, 1);
    } else {
        tft_draw_text(MARGIN + 60, y + 25, "Disconnected", COLOR_DANGER, 1);
    }
    
    /* AP Status */
    uint8_t clients = traffic_stats_get_client_count();
    extern char *ap_ssid;
    tft_draw_text(MARGIN + CARD_PADDING, y + 40, "AP:", COLOR_TEXT_SECONDARY, 1);
    char ap_buf[48];
    snprintf(ap_buf, sizeof(ap_buf), "%s (%d clients)", ap_ssid ? ap_ssid : "ESP32", clients);
    tft_draw_text(MARGIN + 60, y + 40, ap_buf, COLOR_ACCENT, 1);
    
    /* NAT Status */
    int32_t nat_disabled = 0;
    get_config_param_int("nat_disabled", &nat_disabled);
    tft_draw_text(MARGIN + CARD_PADDING, y + 55, "NAT:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 60, y + 55, nat_disabled ? "Disabled" : "Enabled", 
                  nat_disabled ? COLOR_WARNING : COLOR_SUCCESS, 1);
    
    y += 80;
    
    /* Traffic Overview Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 90, "Traffic Monitor", COLOR_ACCENT);
    
    traffic_stats_t stats;
    traffic_stats_get(&stats);
    
    /* Download speed */
    draw_speed_gauge(MARGIN + CARD_PADDING, y + 25, "Download", stats.rx_speed, 
                     stats.peak_rx_speed > 0 ? stats.peak_rx_speed : 1000000, COLOR_SUCCESS);
    
    /* Upload speed */
    draw_speed_gauge(MARGIN + CARD_PADDING + 120, y + 25, "Upload", stats.tx_speed,
                     stats.peak_tx_speed > 0 ? stats.peak_tx_speed : 1000000, COLOR_CYAN);
    
    y += 100;
    
    /* System Info Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 60, "System Info", COLOR_ACCENT);
    
    /* Uptime */
    char uptime_str[32];
    format_uptime(traffic_stats_get_uptime(), uptime_str, sizeof(uptime_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 25, "Uptime:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 60, y + 25, uptime_str, COLOR_TEXT_PRIMARY, 1);
    
    /* Free heap */
    char heap_str[32];
    format_bytes(traffic_stats_get_free_heap(), heap_str, sizeof(heap_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 40, "Memory:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 60, y + 40, heap_str, COLOR_TEXT_PRIMARY, 1);
}

void ui_draw_clients(void)
{
    int y = CONTENT_START_Y;
    
    /* Title card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 30, NULL, COLOR_ACCENT);
    
    uint8_t count = traffic_stats_get_client_count();
    char title[32];
    snprintf(title, sizeof(title), "Connected Clients (%d)", count);
    tft_draw_text(MARGIN + CARD_PADDING, y + 8, title, COLOR_ACCENT, 1);
    
    y += 40;
    
    /* Get client list */
    wifi_sta_list_t wifi_sta_list;
    wifi_sta_mac_ip_list_t adapter_sta_list;
    memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
    memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));
    esp_wifi_ap_get_sta_list(&wifi_sta_list);
    esp_wifi_ap_get_sta_list_with_ip(&wifi_sta_list, &adapter_sta_list);
    
    if (adapter_sta_list.num == 0) {
        /* No clients message */
        tft_draw_text_centered(y + 50, "No clients connected", COLOR_TEXT_SECONDARY, 1);
        return;
    }
    
    /* Client list */
    for (int i = 0; i < adapter_sta_list.num && i < MAX_DISPLAYED_CLIENTS; i++) {
        esp_netif_pair_mac_ip_t *sta = &adapter_sta_list.sta[i];
        
        /* Client card */
        draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 38, NULL, COLOR_ACCENT);
        
        /* Client number */
        char num_str[4];
        snprintf(num_str, sizeof(num_str), "%d.", i + 1);
        tft_draw_text(MARGIN + CARD_PADDING, y + 8, num_str, COLOR_ACCENT, 1);
        
        /* IP Address */
        char ip_str[16];
        esp_ip4addr_ntoa(&sta->ip, ip_str, sizeof(ip_str));
        tft_draw_text(MARGIN + 25, y + 8, ip_str, COLOR_TEXT_PRIMARY, 1);
        
        /* MAC Address */
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 sta->mac[0], sta->mac[1], sta->mac[2],
                 sta->mac[3], sta->mac[4], sta->mac[5]);
        tft_draw_text(MARGIN + CARD_PADDING, y + 22, mac_str, COLOR_TEXT_SECONDARY, 1);
        
        y += 42;
    }
    
    if (adapter_sta_list.num > MAX_DISPLAYED_CLIENTS) {
        char more_str[32];
        snprintf(more_str, sizeof(more_str), "+%d more...", adapter_sta_list.num - MAX_DISPLAYED_CLIENTS);
        tft_draw_text(MARGIN + CARD_PADDING, y + 5, more_str, COLOR_TEXT_SECONDARY, 1);
    }
}

void ui_draw_traffic(void)
{
    int y = CONTENT_START_Y;
    
    traffic_stats_t stats;
    traffic_stats_get(&stats);
    
    /* Total Traffic Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 80, "Total Traffic", COLOR_ACCENT);
    
    /* Total downloaded */
    char bytes_str[32];
    format_bytes(stats.rx_bytes, bytes_str, sizeof(bytes_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 28, "Downloaded:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 100, y + 28, bytes_str, COLOR_SUCCESS, 1);
    
    /* Total uploaded */
    format_bytes(stats.tx_bytes, bytes_str, sizeof(bytes_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 48, "Uploaded:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 100, y + 48, bytes_str, COLOR_CYAN, 1);
    
    /* Total */
    format_bytes(stats.rx_bytes + stats.tx_bytes, bytes_str, sizeof(bytes_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 63, "Total:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 100, y + 63, bytes_str, COLOR_TEXT_PRIMARY, 1);
    
    y += 90;
    
    /* Current Speed Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 75, "Current Speed", COLOR_ACCENT);
    
    /* Download speed with large display */
    char speed_str[32];
    format_speed(stats.rx_speed, speed_str, sizeof(speed_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 28, "DL:", COLOR_SUCCESS, 1);
    tft_draw_text(MARGIN + 35, y + 24, speed_str, COLOR_SUCCESS, 2);
    
    /* Upload speed */
    format_speed(stats.tx_speed, speed_str, sizeof(speed_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 52, "UL:", COLOR_CYAN, 1);
    tft_draw_text(MARGIN + 35, y + 48, speed_str, COLOR_CYAN, 2);
    
    y += 85;
    
    /* Peak Speed Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 55, "Peak Speed", COLOR_ACCENT);
    
    /* Peak download */
    format_speed(stats.peak_rx_speed, speed_str, sizeof(speed_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 28, "Peak DL:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 80, y + 28, speed_str, COLOR_SUCCESS, 1);
    
    /* Peak upload */
    format_speed(stats.peak_tx_speed, speed_str, sizeof(speed_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 42, "Peak UL:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 80, y + 42, speed_str, COLOR_CYAN, 1);
}

void ui_draw_settings(void)
{
    int y = CONTENT_START_Y;
    
    /* Settings Title Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 30, NULL, COLOR_ACCENT);
    tft_draw_text(MARGIN + CARD_PADDING, y + 8, "Router Settings", COLOR_ACCENT, 1);
    
    y += 40;
    
    /* AP Settings Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 70, "Access Point", COLOR_ACCENT);
    
    extern char *ap_ssid;
    extern char *ap_passwd;
    extern char *ap_ip;
    
    tft_draw_text(MARGIN + CARD_PADDING, y + 25, "SSID:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 60, y + 25, ap_ssid ? ap_ssid : "ESP32", COLOR_TEXT_PRIMARY, 1);
    
    tft_draw_text(MARGIN + CARD_PADDING, y + 40, "Pass:", COLOR_TEXT_SECONDARY, 1);
    if (ap_passwd && strlen(ap_passwd) > 0) {
        tft_draw_text(MARGIN + 60, y + 40, "********", COLOR_TEXT_PRIMARY, 1);
    } else {
        tft_draw_text(MARGIN + 60, y + 40, "(open)", COLOR_WARNING, 1);
    }
    
    tft_draw_text(MARGIN + CARD_PADDING, y + 55, "IP:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 60, y + 55, ap_ip ? ap_ip : "192.168.4.1", COLOR_TEXT_PRIMARY, 1);
    
    y += 80;
    
    /* STA Settings Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 55, "Uplink WiFi", COLOR_ACCENT);
    
    extern char *ssid;
    extern char *passwd;
    
    tft_draw_text(MARGIN + CARD_PADDING, y + 25, "SSID:", COLOR_TEXT_SECONDARY, 1);
    if (ssid && strlen(ssid) > 0) {
        tft_draw_text(MARGIN + 60, y + 25, ssid, COLOR_TEXT_PRIMARY, 1);
    } else {
        tft_draw_text(MARGIN + 60, y + 25, "(not set)", COLOR_WARNING, 1);
    }
    
    tft_draw_text(MARGIN + CARD_PADDING, y + 40, "Pass:", COLOR_TEXT_SECONDARY, 1);
    if (passwd && strlen(passwd) > 0) {
        tft_draw_text(MARGIN + 60, y + 40, "********", COLOR_TEXT_PRIMARY, 1);
    } else {
        tft_draw_text(MARGIN + 60, y + 40, "(not set)", COLOR_WARNING, 1);
    }
    
    y += 65;
    
    /* Advanced Settings Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 55, "Advanced", COLOR_ACCENT);
    
    int32_t nat_disabled = 0;
    int32_t led_disabled = 0;
    get_config_param_int("nat_disabled", &nat_disabled);
    get_config_param_int("led_disabled", &led_disabled);
    
    tft_draw_text(MARGIN + CARD_PADDING, y + 25, "NAT:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 60, y + 25, nat_disabled ? "Disabled" : "Enabled",
                  nat_disabled ? COLOR_WARNING : COLOR_SUCCESS, 1);
    
    tft_draw_text(MARGIN + CARD_PADDING, y + 40, "LED:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 60, y + 40, led_disabled ? "Disabled" : "Enabled",
                  led_disabled ? COLOR_WARNING : COLOR_SUCCESS, 1);
    
    y += 65;
    
    /* Web UI hint */
    tft_draw_text_centered(y + 5, "Configure via Web UI", COLOR_TEXT_SECONDARY, 1);
    
    extern uint32_t my_ap_ip;
    if (my_ap_ip != 0) {
        char ip_str[32];
        esp_ip4_addr_t addr;
        addr.addr = my_ap_ip;
        snprintf(ip_str, sizeof(ip_str), "http://" IPSTR, IP2STR(&addr));
        tft_draw_text_centered(y + 20, ip_str, COLOR_ACCENT, 1);
    }
}

void ui_draw_about(void)
{
    int y = CONTENT_START_Y;
    
    /* Title */
    tft_draw_text_centered(y, "ESP32 NAT Router", COLOR_ACCENT, 2);
    tft_draw_text_centered(y + 25, "Extended Edition", COLOR_TEXT_SECONDARY, 1);
    
    y += 50;
    
    /* Device Info Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 90, "Device Information", COLOR_ACCENT);
    
    /* Chip info */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    char info_str[48];
    tft_draw_text(MARGIN + CARD_PADDING, y + 25, "Chip:", COLOR_TEXT_SECONDARY, 1);
    snprintf(info_str, sizeof(info_str), "ESP32-S3 (%d cores)", chip_info.cores);
    tft_draw_text(MARGIN + 60, y + 25, info_str, COLOR_TEXT_PRIMARY, 1);
    
    /* Flash size */
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    tft_draw_text(MARGIN + CARD_PADDING, y + 40, "Flash:", COLOR_TEXT_SECONDARY, 1);
    format_bytes(flash_size, info_str, sizeof(info_str));
    tft_draw_text(MARGIN + 60, y + 40, info_str, COLOR_TEXT_PRIMARY, 1);
    
    /* Free heap */
    tft_draw_text(MARGIN + CARD_PADDING, y + 55, "Heap:", COLOR_TEXT_SECONDARY, 1);
    format_bytes(esp_get_free_heap_size(), info_str, sizeof(info_str));
    tft_draw_text(MARGIN + 60, y + 55, info_str, COLOR_TEXT_PRIMARY, 1);
    
    /* Uptime */
    tft_draw_text(MARGIN + CARD_PADDING, y + 70, "Uptime:", COLOR_TEXT_SECONDARY, 1);
    format_uptime(traffic_stats_get_uptime(), info_str, sizeof(info_str));
    tft_draw_text(MARGIN + 60, y + 70, info_str, COLOR_TEXT_PRIMARY, 1);
    
    y += 100;
    
    /* Network Card */
    draw_card(MARGIN, y, TFT_WIDTH - 2 * MARGIN, 70, "Network Status", COLOR_ACCENT);
    
    /* STA MAC */
    uint8_t sta_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, sta_mac);
    snprintf(info_str, sizeof(info_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
    tft_draw_text(MARGIN + CARD_PADDING, y + 25, "STA MAC:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 65, y + 25, info_str, COLOR_TEXT_PRIMARY, 1);
    
    /* AP MAC */
    uint8_t ap_mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, ap_mac);
    snprintf(info_str, sizeof(info_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             ap_mac[0], ap_mac[1], ap_mac[2], ap_mac[3], ap_mac[4], ap_mac[5]);
    tft_draw_text(MARGIN + CARD_PADDING, y + 40, "AP MAC:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 65, y + 40, info_str, COLOR_TEXT_PRIMARY, 1);
    
    /* Total clients served */
    traffic_stats_t stats;
    traffic_stats_get(&stats);
    format_bytes(stats.rx_bytes + stats.tx_bytes, info_str, sizeof(info_str));
    tft_draw_text(MARGIN + CARD_PADDING, y + 55, "Traffic:", COLOR_TEXT_SECONDARY, 1);
    tft_draw_text(MARGIN + 65, y + 55, info_str, COLOR_TEXT_PRIMARY, 1);
}

/* UI Task - handles periodic updates */
static void ui_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UI task started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_interval = pdMS_TO_TICKS(UI_UPDATE_INTERVAL_MS);
    
    while (ui_task_running) {
        ui_update();
        vTaskDelayUntil(&last_wake_time, update_interval);
    }
    
    ESP_LOGI(TAG, "UI task stopped");
    vTaskDelete(NULL);
}

esp_err_t ui_start_task(void)
{
    if (ui_task_handle != NULL) {
        ESP_LOGW(TAG, "UI task already running");
        return ESP_OK;
    }

    ui_task_running = true;
    
    BaseType_t ret = xTaskCreate(
        ui_task,
        "ui_task",
        UI_TASK_STACK_SIZE,
        NULL,
        UI_TASK_PRIORITY,
        &ui_task_handle
    );
    
    if (ret != pdPASS) {
        ui_task_running = false;
        ESP_LOGE(TAG, "Failed to create UI task");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

void ui_stop_task(void)
{
    if (ui_task_handle == NULL) {
        return;
    }
    
    ui_task_running = false;
    
    /* Wait for task to finish */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ui_task_handle = NULL;
}
