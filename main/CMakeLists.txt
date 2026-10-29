idf_component_register(
    SRCS "main.c"
         "snake_logic.c"
         "snake_audio.c"
         "snake_ui.c"
         "lv_font_cn_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer nvs_flash esp_hw_support
)
