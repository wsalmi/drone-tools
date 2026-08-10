idf_component_register(
    SRCS "main.c"
         "task_manager.c"
         "data_pipeline.c"
    INCLUDE_DIRS "."
    REQUIRES
        hw_hal
        services
        domain
        ui
        common
)
