/**
 * @file traffic_stats.c
 * @brief Network traffic statistics tracking implementation
 */

#include "traffic_stats.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "router_globals.h"
#include "lwip/stats.h"
#include <string.h>

static const char *TAG = "TRAFFIC_STATS";

/* Configuration constants */
#define STATS_UPDATE_INTERVAL_US    1000000  /* 1 second in microseconds */
#define DEFAULT_MTU_SIZE            1500     /* Default MTU for traffic estimation */

/* Global statistics */
static traffic_stats_t g_stats = {0};
static client_stats_t g_clients[MAX_CLIENTS] = {0};
static uint32_t g_start_time = 0;
static SemaphoreHandle_t g_stats_mutex = NULL;
static esp_timer_handle_t g_update_timer = NULL;
static bool g_initialized = false;

/* Previous byte counts for speed calculation */
static uint64_t prev_rx_bytes = 0;
static uint64_t prev_tx_bytes = 0;
static uint32_t prev_update_time = 0;

/* Get current time in milliseconds */
static uint32_t get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Timer callback for periodic updates */
static void update_timer_callback(void *arg)
{
    traffic_stats_update();
}

esp_err_t traffic_stats_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing traffic statistics");

    /* Create mutex */
    g_stats_mutex = xSemaphoreCreateMutex();
    if (g_stats_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Initialize statistics */
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_clients, 0, sizeof(g_clients));
    g_start_time = get_time_ms();
    prev_update_time = g_start_time;

    /* Create periodic update timer (1 second interval) */
    esp_timer_create_args_t timer_args = {
        .callback = update_timer_callback,
        .arg = NULL,
        .name = "traffic_stats_timer"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &g_update_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        vSemaphoreDelete(g_stats_mutex);
        return ret;
    }

    ret = esp_timer_start_periodic(g_update_timer, STATS_UPDATE_INTERVAL_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
        esp_timer_delete(g_update_timer);
        vSemaphoreDelete(g_stats_mutex);
        return ret;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "Traffic statistics initialized");
    
    return ESP_OK;
}

void traffic_stats_deinit(void)
{
    if (!g_initialized) {
        return;
    }

    if (g_update_timer) {
        esp_timer_stop(g_update_timer);
        esp_timer_delete(g_update_timer);
        g_update_timer = NULL;
    }

    if (g_stats_mutex) {
        vSemaphoreDelete(g_stats_mutex);
        g_stats_mutex = NULL;
    }

    g_initialized = false;
    ESP_LOGI(TAG, "Traffic statistics deinitialized");
}

void traffic_stats_update(void)
{
    if (!g_initialized) {
        return;
    }

    if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    uint32_t current_time = get_time_ms();
    uint32_t time_diff = current_time - prev_update_time;
    
    if (time_diff == 0) {
        time_diff = 1; /* Prevent division by zero */
    }

    /* Get traffic data from network interface */
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    uint64_t total_rx = 0;
    uint64_t total_tx = 0;

    /* Try to get traffic stats using esp_netif API */
#if CONFIG_ESP_NETIF_REPORT_DATA_TRAFFIC
    /* Get STA interface stats */
    if (sta_netif != NULL) {
        esp_netif_stats_t sta_stats = {0};
        if (esp_netif_get_io_stats(sta_netif, true, &sta_stats) == ESP_OK) {
            total_rx += sta_stats.rx_bytes;
            total_tx += sta_stats.tx_bytes;
        }
    }

    /* Get AP interface stats */
    if (ap_netif != NULL) {
        esp_netif_stats_t ap_stats = {0};
        if (esp_netif_get_io_stats(ap_netif, true, &ap_stats) == ESP_OK) {
            total_rx += ap_stats.rx_bytes;
            total_tx += ap_stats.tx_bytes;
        }
    }
#else
    /* Fallback: use LWIP link stats if available */
    #if LWIP_STATS && LINK_STATS
    struct stats_proto *link = &lwip_stats.link;
    total_rx = (uint64_t)link->recv * DEFAULT_MTU_SIZE;
    total_tx = (uint64_t)link->xmit * DEFAULT_MTU_SIZE;
    #endif
#endif

    /* Update total bytes */
    g_stats.rx_bytes = total_rx;
    g_stats.tx_bytes = total_tx;

    /* Calculate speed (bytes per second) */
    if (prev_rx_bytes > 0 && total_rx >= prev_rx_bytes) {
        uint64_t rx_diff = total_rx - prev_rx_bytes;
        g_stats.rx_speed = (uint32_t)((rx_diff * 1000) / time_diff);
    }
    
    if (prev_tx_bytes > 0 && total_tx >= prev_tx_bytes) {
        uint64_t tx_diff = total_tx - prev_tx_bytes;
        g_stats.tx_speed = (uint32_t)((tx_diff * 1000) / time_diff);
    }

    /* Update peak speeds */
    if (g_stats.rx_speed > g_stats.peak_rx_speed) {
        g_stats.peak_rx_speed = g_stats.rx_speed;
    }
    if (g_stats.tx_speed > g_stats.peak_tx_speed) {
        g_stats.peak_tx_speed = g_stats.tx_speed;
    }

    /* Store previous values */
    prev_rx_bytes = total_rx;
    prev_tx_bytes = total_tx;
    prev_update_time = current_time;
    g_stats.last_update = current_time;

    /* Update client list */
    wifi_sta_list_t wifi_sta_list;
    wifi_sta_mac_ip_list_t adapter_sta_list;
    
    memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
    memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));
    
    if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
        esp_wifi_ap_get_sta_list_with_ip(&wifi_sta_list, &adapter_sta_list);
        
        /* Mark all clients as inactive first */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            g_clients[i].active = false;
        }
        
        /* Update client information */
        for (int i = 0; i < adapter_sta_list.num && i < MAX_CLIENTS; i++) {
            esp_netif_pair_mac_ip_t *sta = &adapter_sta_list.sta[i];
            
            /* Find or create client entry */
            int slot = -1;
            for (int j = 0; j < MAX_CLIENTS; j++) {
                if (memcmp(g_clients[j].mac, sta->mac, 6) == 0) {
                    slot = j;
                    break;
                }
            }
            
            if (slot < 0) {
                /* Find empty slot */
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (!g_clients[j].active && g_clients[j].ip == 0) {
                        slot = j;
                        break;
                    }
                }
            }
            
            if (slot >= 0) {
                memcpy(g_clients[slot].mac, sta->mac, 6);
                g_clients[slot].ip = sta->ip.addr;
                g_clients[slot].active = true;
                g_clients[slot].last_active = current_time;
            }
        }
    }

    xSemaphoreGive(g_stats_mutex);
}

void traffic_stats_get(traffic_stats_t *stats)
{
    if (!g_initialized || stats == NULL) {
        if (stats) memset(stats, 0, sizeof(*stats));
        return;
    }

    if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(stats, &g_stats, sizeof(*stats));
        xSemaphoreGive(g_stats_mutex);
    }
}

int traffic_stats_get_clients(client_stats_t *clients, int max_clients)
{
    if (!g_initialized || clients == NULL || max_clients <= 0) {
        return 0;
    }

    int count = 0;
    
    if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < MAX_CLIENTS && count < max_clients; i++) {
            if (g_clients[i].active) {
                memcpy(&clients[count], &g_clients[i], sizeof(client_stats_t));
                count++;
            }
        }
        xSemaphoreGive(g_stats_mutex);
    }

    return count;
}

void traffic_stats_reset(void)
{
    if (!g_initialized) {
        return;
    }

    if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&g_stats, 0, sizeof(g_stats));
        prev_rx_bytes = 0;
        prev_tx_bytes = 0;
        g_start_time = get_time_ms();
        xSemaphoreGive(g_stats_mutex);
    }
}

void traffic_stats_reset_peak(void)
{
    if (!g_initialized) {
        return;
    }

    if (xSemaphoreTake(g_stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_stats.peak_rx_speed = 0;
        g_stats.peak_tx_speed = 0;
        xSemaphoreGive(g_stats_mutex);
    }
}

uint32_t traffic_stats_get_uptime(void)
{
    if (!g_initialized) {
        return 0;
    }
    return (get_time_ms() - g_start_time) / 1000;
}

uint32_t traffic_stats_get_free_heap(void)
{
    return esp_get_free_heap_size();
}

int8_t traffic_stats_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

uint8_t traffic_stats_get_client_count(void)
{
    return getConnectCount();
}

bool traffic_stats_is_sta_connected(void)
{
    extern bool ap_connect;
    return ap_connect;
}
