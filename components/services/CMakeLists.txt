idf_component_register(
    SRCS "src/services_placeholder.c"
          "src/protocol_classifier.c"
          "src/remoteid_decoder.c"
          "src/mavlink_decoder.c"
          "src/elrs_decoder.c"
          "src/geolocation_service.c"
          "src/pilot_locator.c"
          "src/telemetry_decoder.c"
          "src/detection_service.c"
          "src/alert_engine.c"
          "src/spectrum_analyzer.c"
          "src/data_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES common hw_hal domain
)
