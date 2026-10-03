idf_component_register(
    SRCS "main.c" "ssd1306_spi.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_spi esp_driver_gpio
)
