idf_component_register(SRCS "main.cpp"
                            "sys/lvgl_port.cpp"
                            "hw/gc9a01.cpp"
                            "hw/chsc6x.cpp"
                            "ui/workshop_ui.cpp"
                       PRIV_REQUIRES spi_flash lvgl_cpp esp_lvgl_port lvgl esp_timer driver
                       INCLUDE_DIRS ".")
