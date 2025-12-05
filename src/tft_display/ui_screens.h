/**
 * @file ui_screens.h
 * @brief UI Screen definitions for ESP32-S3 NAT Router TFT display
 */

#pragma once

#include "tft_display.h"
#include "traffic_stats.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UI system
 * @return ESP_OK on success
 */
esp_err_t ui_init(void);

/**
 * @brief Deinitialize UI system
 */
void ui_deinit(void);

/**
 * @brief Switch to a specific screen
 * @param screen Screen ID to switch to
 */
void ui_switch_screen(screen_id_t screen);

/**
 * @brief Get current screen ID
 * @return Current screen ID
 */
screen_id_t ui_get_current_screen(void);

/**
 * @brief Navigate to next screen
 */
void ui_next_screen(void);

/**
 * @brief Navigate to previous screen
 */
void ui_prev_screen(void);

/**
 * @brief Update current screen (called periodically)
 */
void ui_update(void);

/**
 * @brief Draw the dashboard screen
 */
void ui_draw_dashboard(void);

/**
 * @brief Draw the clients screen
 */
void ui_draw_clients(void);

/**
 * @brief Draw the traffic screen
 */
void ui_draw_traffic(void);

/**
 * @brief Draw the settings screen
 */
void ui_draw_settings(void);

/**
 * @brief Draw the about screen
 */
void ui_draw_about(void);

/**
 * @brief Start the UI task
 * @return ESP_OK on success
 */
esp_err_t ui_start_task(void);

/**
 * @brief Stop the UI task
 */
void ui_stop_task(void);

#ifdef __cplusplus
}
#endif
