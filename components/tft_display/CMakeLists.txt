if(CONFIG_IDF_TARGET_ESP32S3)
    idf_component_register(
        SRCS 
            "tft_display.c"
            "traffic_stats.c"
            "ui_screens.c"
        INCLUDE_DIRS "."
        REQUIRES 
            driver
            esp_lcd
            esp_timer
            esp_wifi
            nvs_flash
            cmd_router
            esp_netif
            freertos
            esp_system
            spi_flash
    )
else()
    # For non-ESP32-S3 targets, register as a stub component
    idf_component_register(
        INCLUDE_DIRS "."
    )
endif()
