idf_component_register(
    SRCS "main.c"
         "ecard_barcode.c"
         "ecard_qr.c"
         "ecard_profile.c"
         "ecard_quiz.c"
         "ecard_ai_prompt.c"
         "ecard_vad.c"
         "ecard_opus_arena.c"
         "ecard_cards.c"
         "ecard_flashcard.c"
         "ecard_messages.c"
         "ecard_pomodoro.c"
         "ecard_cat_room.c"
         "ecard_setup.c"
         "ecard_setup_validate.c"
         "ecard_netlink.c"
         "ecard_lan_http.c"
         "ecard_portal.c"
         "ecard_time.c"
         "ecard_xiaozhi.c"
         "ecard_ui.c"
         "ecard_voice.c"
         "ecard_network.c"
         "ecard_app.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash esp_http_server esp_http_client json esp_app_format spiffs
)

# Generated with tools/generate_ecard_fonts.py. Keep the command and source
# license in assets/README.md; do not edit these files by hand.
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ecard_font_9.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ecard_font_12.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ecard_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ecard_font_22.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/ecard_font_48.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/images/ecard_portrait.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/images/ecard_cat_sprites.c"
)
