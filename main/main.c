// main/main.c — FoloToy AI Passport Snake Game
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "key_guard.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "snake_audio.h"
#include "snake_logic.h"
#include "snake_ui.h"

static const char *TAG = "snake_main";

#define INPUT_QUEUE_SIZE 8
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)

static key_guard_t s_key_guard;
static volatile bool s_input_ready = false;
static QueueHandle_t s_input_queue = NULL;
static snake_game_t s_game;
static int s_battery_soc = -1;
static uint32_t s_last_battery_read_ms = 0;
static uint32_t s_last_tick_time_ms = 0;
static lv_timer_t *s_game_timer = NULL;
static uint32_t s_saved_high_score = 0;

static uint32_t load_high_score(void) {
    nvs_handle_t handle;
    uint32_t hi = 0;
    if (nvs_open("snake", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u32(handle, "hi_score", &hi);
        nvs_close(handle);
        ESP_LOGI(TAG, "Loaded high score: %u", (unsigned)hi);
    }
    return hi;
}

static void save_high_score(uint32_t hi) {
    if (hi <= s_saved_high_score) return;
    s_saved_high_score = hi;

    nvs_handle_t handle;
    if (nvs_open("snake", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u32(handle, "hi_score", hi);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Saved new high score to NVS: %u", (unsigned)hi);
    }
}

// button callbacks run on the shared esp_timer task; enqueue only and return immediately.
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

    // Use BSP_BTN_PRESS for immediate arcade responsiveness
    if (ev != BSP_BTN_PRESS) return;

    snake_cmd_t cmd;
    bool valid = false;

    if (btn == BSP_BTN_UP) {
        cmd = SNAKE_CMD_LEFT;
        valid = true;
    } else if (btn == BSP_BTN_DOWN) {
        cmd = SNAKE_CMD_RIGHT;
        valid = true;
    } else if (btn == BSP_BTN_OK) {
        cmd = SNAKE_CMD_OK;
        valid = true;
    }

    if (valid) {
        (void)xQueueSend(s_input_queue, &cmd, 0);
    }
}

static void game_timer_cb(lv_timer_t *timer) {
    (void)timer;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

    // Update battery periodically every 5 seconds
    if (now_ms - s_last_battery_read_ms >= 5000 || s_last_battery_read_ms == 0) {
        s_last_battery_read_ms = now_ms;
        s_battery_soc = bsp_battery_soc();
    }

    // Process queued button inputs
    snake_cmd_t cmd;
    bool ui_needs_update = false;

    while (xQueueReceive(s_input_queue, &cmd, 0) == pdTRUE) {
        snake_state_t prev_state = s_game.state;
        snake_dir_t prev_dir = s_game.dir;

        snake_event_t ev = snake_input(&s_game, cmd);
        ui_needs_update = true;

        if (ev == SNAKE_EV_START) {
            snake_audio_play(SNAKE_SFX_START);
            s_last_tick_time_ms = now_ms;
        } else if (ev == SNAKE_EV_PAUSE || ev == SNAKE_EV_RESUME) {
            snake_audio_play(SNAKE_SFX_PAUSE);
        } else if (s_game.state == SNAKE_STATE_PLAYING && prev_state == SNAKE_STATE_PLAYING) {
            if (s_game.next_dir != prev_dir) {
                snake_audio_play(SNAKE_SFX_TURN);
            }
        }
    }

    // Advance game logic tick when playing
    if (s_game.state == SNAKE_STATE_PLAYING) {
        uint32_t tick_interval_ms = snake_get_tick_ms(&s_game);
        if (now_ms - s_last_tick_time_ms >= tick_interval_ms) {
            s_last_tick_time_ms = now_ms;
            snake_event_t ev = snake_tick(&s_game);
            ui_needs_update = true;

            switch (ev) {
            case SNAKE_EV_EAT_NORMAL:
                snake_audio_play(SNAKE_SFX_EAT);
                break;
            case SNAKE_EV_EAT_BONUS:
                snake_audio_play(SNAKE_SFX_BONUS);
                break;
            case SNAKE_EV_BONUS_SPAWN:
                snake_audio_play(SNAKE_SFX_BONUS);
                break;
            case SNAKE_EV_DIE:
                snake_audio_play(SNAKE_SFX_DIE);
                save_high_score(s_game.high_score);
                break;
            default:
                break;
            }
        }
    }

    // Render UI frame
    if (ui_needs_update) {
        snake_ui_update(&s_game, s_battery_soc);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "FoloToy AI Passport Snake Game starting...");

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒:先屏蔽按键动作,直到那颗键松手");
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_saved_high_score = load_high_score();

    // Initialize board I2C
    bsp_i2c_init();
    bsp_i2c_scan();

    // Initialize Display & LVGL
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display or LVGL initialization failed!");
        return;
    }
    bsp_display_backlight(100);

    // Initialize Battery fuel gauge
    bsp_battery_init();

    // Initialize Audio sound effects
    snake_audio_init();

    // Create input queue and register button handler
    s_input_queue = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(snake_cmd_t));
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Button initialization failed!");
    } else {
        if (s_key_guard.armed) {
            const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
            key_guard_sync_released(&s_key_guard, released);
            if (!s_key_guard.armed) {
                ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手,提前撤防");
            }
        }
        s_input_ready = true;
    }

    // Initialize game logic state
    uint32_t seed = (uint32_t)esp_timer_get_time();
    snake_init(&s_game, seed, s_saved_high_score);

    // Build redesigned UI
    if (bsp_lvgl_lock(1000)) {
        snake_ui_create();
        snake_ui_update(&s_game, bsp_battery_soc());
        s_game_timer = lv_timer_create(game_timer_cb, 20, NULL);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "Snake Game initialized and running!");
}
