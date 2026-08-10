idf_component_register(
    SRCS "src/ui_manager.c"
         "src/screen_splash.c"
         "src/screen_scanner.c"
         "src/screen_map.c"
         "src/screen_spectrum.c"
         "src/screen_settings.c"
         "src/screen_log.c"
         "src/screen_menu.c"
         "src/ui_placeholder.c"
    INCLUDE_DIRS "include"
    REQUIRES common hw_hal services domain
)
