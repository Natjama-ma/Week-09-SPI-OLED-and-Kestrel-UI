idf_component_register(SRCS "Lab9-3-ESP32-ClosedLoop.c"
                    INCLUDE_DIRS "."
                    REQUIRES 
                        driver 
                        esp_driver_spi  
                        esp_driver_gpio 
                        esp_driver_uart 
                        esp_adc 
                        esp_timer
)
