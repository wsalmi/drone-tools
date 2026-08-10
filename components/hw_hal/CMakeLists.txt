idf_component_register(
    SRCS "src/hal_placeholder.c"
         "src/hal_lora.c"
         "src/hal_gps.c"
         "src/hal_wifi_scanner.c"
         "src/hal_ble_scanner.c"
         "src/hal_nrf24.c"
         "src/hal_sdr.c"
         "src/hal_display.c"
         "src/hal_sd.c"
         "src/hal_buzzer.c"
         "src/hw_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES common driver esp_timer esp_wifi bt usb fatfs
)
