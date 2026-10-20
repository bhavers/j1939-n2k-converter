idf_component_register(
    SRCS "main.cpp" "NMEA2000_esp32c5.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_twai esp_wifi nvs_flash app_update esp_timer lwip esp_netif
)
