// main/main.c — Tic-Tac-Toe Game for FoloToy AI Passport
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "tictactoe_audio.h"
#include "tictactoe_logic.h"
#include "tictactoe_ui.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ttt_main";

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t ev;
} button_event_t;

#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;
static volatile bool s_input_ready = false;
static ttt_game_t s_game;
static QueueHandle_t s_input_queue = NULL;

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手,按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const button_event_t event = { .btn = btn, .ev = ev };
    (void)xQueueSend(s_input_queue, &event, 0);
}

static void handle_input_event(const button_event_t *event) {
    if (!event) return;

    // OK button LONG press: Switch Mode / Reset Scores
    if (event->btn == BSP_BTN_OK && event->ev == BSP_BTN_LONG) {
        bsp_lvgl_lock(portMAX_DELAY);
        ttt_game_toggle_mode(&s_game);
        ttt_ui_update(&s_game);
        bsp_lvgl_unlock();
        ttt_audio_play(TTT_SFX_WIN);
        return;
    }

    // UP button LONG press: Reset current round
    if (event->btn == BSP_BTN_UP && event->ev == BSP_BTN_LONG) {
        bsp_lvgl_lock(portMAX_DELAY);
        ttt_game_start_round(&s_game);
        ttt_ui_update(&s_game);
        bsp_lvgl_unlock();
        ttt_audio_play(TTT_SFX_MOVE);
        return;
    }

    // Only process CLICK events for game moves
    if (event->ev != BSP_BTN_CLICK) return;

    if (event->btn == BSP_BTN_UP) {
        // Move cursor backward
        bsp_lvgl_lock(portMAX_DELAY);
        bool moved = ttt_game_move_cursor(&s_game, -1);
        if (moved) {
            ttt_ui_update(&s_game);
        }
        bsp_lvgl_unlock();
        if (moved) {
            ttt_audio_play(TTT_SFX_MOVE);
        }
        return;
    }

    if (event->btn == BSP_BTN_DOWN) {
        // Move cursor forward
        bsp_lvgl_lock(portMAX_DELAY);
        bool moved = ttt_game_move_cursor(&s_game, 1);
        if (moved) {
            ttt_ui_update(&s_game);
        }
        bsp_lvgl_unlock();
        if (moved) {
            ttt_audio_play(TTT_SFX_MOVE);
        }
        return;
    }

    if (event->btn == BSP_BTN_OK) {
        // If game has ended, OK starts a new round
        if (s_game.state != TTT_STATE_PLAYING) {
            bsp_lvgl_lock(portMAX_DELAY);
            ttt_game_start_round(&s_game);
            ttt_ui_update(&s_game);
            bsp_lvgl_unlock();
            ttt_audio_play(TTT_SFX_MOVE);

            // If in PVE mode and AI starts this round
            if (s_game.mode == TTT_MODE_PVE && s_game.current_turn == TTT_CELL_O) {
                vTaskDelay(pdMS_TO_TICKS(350));
                bsp_lvgl_lock(portMAX_DELAY);
                int ai_move = ttt_ai_best_move(s_game.board, TTT_CELL_O);
                if (ai_move >= 0) {
                    ttt_game_make_move(&s_game, ai_move);
                }
                ttt_ui_update(&s_game);
                bsp_lvgl_unlock();
                ttt_audio_play(TTT_SFX_PLACE);
            }
            return;
        }

        // Active game: attempt to place move at cursor
        bsp_lvgl_lock(portMAX_DELAY);
        bool valid = ttt_game_place_at_cursor(&s_game);
        if (valid) {
            ttt_ui_update(&s_game);
        }
        bsp_lvgl_unlock();

        if (!valid) {
            ttt_audio_play(TTT_SFX_INVALID);
            return;
        }

        // Play placement sound
        ttt_audio_play(TTT_SFX_PLACE);

        // Check if move concluded the game
        if (s_game.state == TTT_STATE_WIN_X) {
            ttt_audio_play(TTT_SFX_WIN);
            return;
        } else if (s_game.state == TTT_STATE_WIN_O) {
            ttt_audio_play(TTT_SFX_LOSE);
            return;
        } else if (s_game.state == TTT_STATE_DRAW) {
            ttt_audio_play(TTT_SFX_DRAW);
            return;
        }

        // If in PVE mode and it's now AI's turn
        if (s_game.mode == TTT_MODE_PVE && s_game.current_turn == TTT_CELL_O) {
            // Small pause for natural game feel
            vTaskDelay(pdMS_TO_TICKS(300));

            bsp_lvgl_lock(portMAX_DELAY);
            int ai_move = ttt_ai_best_move(s_game.board, TTT_CELL_O);
            if (ai_move >= 0) {
                ttt_game_make_move(&s_game, ai_move);
            }
            ttt_ui_update(&s_game);
            bsp_lvgl_unlock();

            if (s_game.state == TTT_STATE_WIN_O) {
                ttt_audio_play(TTT_SFX_LOSE);
            } else if (s_game.state == TTT_STATE_DRAW) {
                ttt_audio_play(TTT_SFX_DRAW);
            } else {
                ttt_audio_play(TTT_SFX_PLACE);
            }
        }
    }
}

static void ttt_app_task(void *arg) {
    (void)arg;
    button_event_t event;
    int last_soc = -2;

    while (1) {
        // Wait up to 1 second for button events; also handles periodic battery refresh
        if (xQueueReceive(s_input_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle_input_event(&event);
        }

        int soc = bsp_battery_soc();
        if (soc != last_soc) {
            last_soc = soc;
            bsp_lvgl_lock(portMAX_DELAY);
            ttt_ui_update_battery(soc);
            bsp_lvgl_unlock();
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting FoloToy AI Passport Tic-Tac-Toe Game");

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    // 1. Initialize shared I2C bus
    if (bsp_i2c_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus");
    }

    // 2. Initialize display (ST7789 + LVGL)
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Failed to initialize display / LVGL");
        return;
    }
    bsp_display_backlight(100);

    // 3. Initialize battery gauge (CW2017)
    if (bsp_battery_init() != ESP_OK) {
        ESP_LOGW(TAG, "Battery gauge init failed or unavailable");
    }

    // 4. Initialize sound effects (ES8311)
    ttt_audio_init();

    // 5. Initialize Game Model
    ttt_game_init(&s_game);

    // 6. Build UI inside LVGL lock
    bsp_lvgl_lock(portMAX_DELAY);
    ttt_ui_create();
    ttt_ui_update(&s_game);
    ttt_ui_update_battery(bsp_battery_soc());
    bsp_lvgl_unlock();

    // 7. Setup input queue and buttons
    s_input_queue = xQueueCreate(16, sizeof(button_event_t));
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize buttons");
    } else {
        if (s_key_guard.armed) {
            const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
            key_guard_sync_released(&s_key_guard, released);
        }
        s_input_ready = true;
    }

    // 8. Launch application task
    xTaskCreate(ttt_app_task, "ttt_app", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Tic-Tac-Toe Game initialized successfully");
}
