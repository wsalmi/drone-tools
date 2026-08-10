idf_component_register(
    SRCS "src/domain_placeholder.c"
          "src/config_store.c"
          "src/aircraft_registry.c"
          "src/protocol_signatures.c"
    INCLUDE_DIRS "include"
    REQUIRES common hw_hal cjson
)
