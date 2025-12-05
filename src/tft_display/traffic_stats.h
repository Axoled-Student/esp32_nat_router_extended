/**
 * @file traffic_stats.h
 * @brief Network traffic statistics tracking for ESP32-S3 NAT Router
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Traffic statistics structure */
typedef struct {
    uint64_t rx_bytes;          /* Total received bytes */
    uint64_t tx_bytes;          /* Total transmitted bytes */
    uint32_t rx_speed;          /* Current RX speed (bytes/sec) */
    uint32_t tx_speed;          /* Current TX speed (bytes/sec) */
    uint32_t peak_rx_speed;     /* Peak RX speed */
    uint32_t peak_tx_speed;     /* Peak TX speed */
    uint32_t last_update;       /* Last update timestamp (ms) */
} traffic_stats_t;

/* Per-client statistics */
typedef struct {
    uint8_t mac[6];
    uint32_t ip;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint32_t last_active;
    bool active;
} client_stats_t;

#define MAX_CLIENTS 16

/**
 * @brief Initialize traffic statistics tracking
 * @return ESP_OK on success
 */
esp_err_t traffic_stats_init(void);

/**
 * @brief Deinitialize traffic statistics
 */
void traffic_stats_deinit(void);

/**
 * @brief Get current traffic statistics
 * @param stats Pointer to traffic_stats_t structure to fill
 */
void traffic_stats_get(traffic_stats_t *stats);

/**
 * @brief Get per-client statistics
 * @param clients Array to fill with client statistics
 * @param max_clients Maximum number of clients to return
 * @return Number of active clients
 */
int traffic_stats_get_clients(client_stats_t *clients, int max_clients);

/**
 * @brief Reset traffic statistics
 */
void traffic_stats_reset(void);

/**
 * @brief Reset peak statistics
 */
void traffic_stats_reset_peak(void);

/**
 * @brief Update traffic statistics (called periodically)
 */
void traffic_stats_update(void);

/**
 * @brief Get total uptime in seconds
 * @return Uptime in seconds
 */
uint32_t traffic_stats_get_uptime(void);

/**
 * @brief Get free heap size
 * @return Free heap in bytes
 */
uint32_t traffic_stats_get_free_heap(void);

/**
 * @brief Get WiFi STA signal strength
 * @return RSSI in dBm, or 0 if not connected
 */
int8_t traffic_stats_get_rssi(void);

/**
 * @brief Get number of connected clients
 * @return Number of connected clients
 */
uint8_t traffic_stats_get_client_count(void);

/**
 * @brief Check if STA is connected to uplink
 * @return true if connected
 */
bool traffic_stats_is_sta_connected(void);

#ifdef __cplusplus
}
#endif
