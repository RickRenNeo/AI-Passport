# main/CMakeLists.txt — 《天机·六爻》小智 AI 固件主构建定义
set(SOURCES
    # Core Application
    "main.cc"
    "application.cc"
    "assets.cc"
    "device_state_machine.cc"
    "mcp_server.cc"
    "ota.cc"
    "settings.cc"
    "system_info.cc"

    # Audio Pipeline
    "audio/audio_codec.cc"
    "audio/audio_debugger.cc"
    "audio/audio_service.cc"
    "audio/demuxer/ogg_demuxer.cc"
    "audio/codecs/es8311_audio_codec.cc"
    "audio/codecs/no_audio_codec.cc"
    "notify/notify_player.cc"

    # Display & Graphics
    "display/display.cc"
    "display/lcd_display.cc"
    "display/text_glyph.cc"
    "display/lvgl_display/lvgl_display.cc"
    "display/lvgl_display/lvgl_theme.cc"
    "display/lvgl_display/lvgl_font.cc"
    "display/lvgl_display/dynamic_glyph_cache.cc"
    "display/lvgl_display/lvgl_image.cc"

    # Protocols
    "protocols/protocol.cc"
    "protocols/text_glyph_payload.cc"
    "protocols/websocket_protocol.cc"
    "protocols/mqtt_protocol.cc"

    # LED
    "led/single_led.cc"
    "led/gpio_led.cc"

    # Board: FoloToy AI Passport & Common
    "boards/common/board.cc"
    "boards/common/wifi_board.cc"
    "boards/common/adc_battery_monitor.cc"
    "boards/common/backlight.cc"
    "boards/common/button.cc"
    "boards/common/power_save_timer.cc"
    "boards/common/sleep_timer.cc"
    "boards/common/system_reset.cc"
    "boards/folotoy/ai-passport/ai_passport_board.cc"
    "boards/folotoy/ai-passport/cw2017_battery_monitor.cc"

    # Tianji Liuyao (天机·六爻) Core Modules
    "tianji/tianji_bridge.cc"
    "tianji/tianji_logic.c"
    "tianji/tianji_ui.c"
    "tianji/tianji_coins.c"
    "tianji/tianji_font_16.c"
    "tianji/tianji_audio.c"
    "tianji/ui_pixel.c"
    "tianji/ui_pixel_math.c"
)

set(INCLUDE_DIRS
    "."
    "tianji"
    "display"
    "display/lvgl_display"
    "audio"
    "audio/demuxer"
    "protocols"
    "boards/common"
    "boards/folotoy/ai-passport"
)

# Board configuration
if(CONFIG_BOARD_TYPE_FOLOTOY_AI_PASSPORT)
    set(BOARD_DIR "folotoy/ai-passport")
    set(BUILTIN_TEXT_FONT font_noto_sans_basic_20_4)
    set(BUILTIN_ICON_FONT font_material_symbols_20_4)
endif()
set(BOARD_TYPE "ai-passport")
set(BOARD_NAME "ai-passport")
set(BOARD_MANUFACTURER "folotoy")

# Language configuration: Chinese zh-CN
if(CONFIG_LANGUAGE_ZH_CN)
    set(LANG_DIR "zh-CN")
endif()
set(LANG_JSON "${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/language.json")
set(LANG_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/assets/lang_config.h")

file(GLOB LANG_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/locales/${LANG_DIR}/*.ogg)
file(GLOB COMMON_SOUNDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/common/*.ogg)

idf_component_register(
    SRCS ${SOURCES}
    EMBED_FILES ${LANG_SOUNDS} ${COMMON_SOUNDS}
    INCLUDE_DIRS ${INCLUDE_DIRS}
    WHOLE_ARCHIVE
    PRIV_REQUIRES
        esp_pm
        esp_netif
        esp_driver_gpio
        esp_driver_uart
        esp_driver_spi
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_ledc
        esp_app_format
        app_update
        spi_flash
        mbedtls
        console
        efuse
        bt
        fatfs
        xiaozhi-fonts
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>)
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    BOARD_TYPE=\"${BOARD_TYPE}\"
    BOARD_NAME=\"${BOARD_NAME}\"
    BOARD_MANUFACTURER=\"${BOARD_MANUFACTURER}\"
)
