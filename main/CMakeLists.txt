idf_component_register(
    SRCS "main.c"
         "tictactoe_logic.c"
         "tictactoe_ui.c"
         "tictactoe_audio.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer
)
