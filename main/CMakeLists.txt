idf_component_register(
    SRCS "main.c"
         "tianji_logic.c"
         "tianji_font_16.c"
         "tianji_coins.c"
         "tianji_audio.c"
         "tianji_net.c"
         "tianji_ui.c"
         "demo_navigation.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash esp_http_client json mbedtls
)
