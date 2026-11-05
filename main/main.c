// main/main.c —— FoloToy AI Passport eCard 电子学生证专属入口
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ecard_app.h"
#include "ecard_ui.h"
#include "ecard_profile.h"
#include "ecard_network.h"
#include "ecard_voice.h"

static const char *TAG = "ecard_main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue = NULL;
static TaskHandle_t s_input_task = NULL;
static TaskHandle_t s_tick_task = NULL;
static bool s_input_ready = false;

#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手，按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            if (bsp_lvgl_lock(500)) {
                ecard_app_handle_key(input.btn, input.event);
                bsp_lvgl_unlock();
            }
        }
    }
}

// 周期性时钟心跳任务：每秒更新状态栏（时间、电量）并驱动课间判定与无操作空闲计时
static void tick_task(void *arg) {
    (void)arg;
    uint8_t sim_hour = 9;
    uint8_t sim_min = 40;
    uint8_t sim_sec = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        sim_sec++;
        if (sim_sec >= 60) {
            sim_sec = 0;
            sim_min++;
            if (sim_min >= 60) {
                sim_min = 0;
                sim_hour = (sim_hour + 1) % 24;
            }
        }

        uint8_t soc = 88;
        bsp_battery_soc(&soc);

        if (bsp_lvgl_lock(200)) {
            char time_buf[16];
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d", sim_hour, sim_min);
            ecard_ui_update_status(time_buf, soc, false, false);
            ecard_app_tick(sim_hour, sim_min, 1000);
            bsp_lvgl_unlock();
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "FoloToy AI Passport eCard 电子学生证启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "休眠唤醒原因位图: 0x%02x", (unsigned)wake_causes);
    }
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒: 先屏蔽按键动作，直到那颗键松手");
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，无法启动 eCard");
        return;
    }
    bsp_display_backlight(100);

    // 外设初始化
    (void)bsp_audio_init();
    (void)bsp_battery_init();

    // 初始化 eCard 核心应用引擎与 UI
    ecard_app_init();

    if (bsp_lvgl_lock(1000)) {
        ecard_ui_create();
        bsp_lvgl_unlock();
    }

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (s_input_queue) {
        xTaskCreate(input_task, "ecard_input", 4096, NULL, 5, &s_input_task);
        xTaskCreate(tick_task, "ecard_tick", 3072, NULL, 3, &s_tick_task);
    }

    if (bsp_button_init(on_key, NULL) == ESP_OK) {
        s_input_ready = true;
        if (s_key_guard.armed) {
            const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
            key_guard_sync_released(&s_key_guard, released);
            if (!s_key_guard.armed) {
                ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手，提前撤防");
            }
        }
    }

    ESP_LOGI(TAG, "eCard 电子学生证系统初始化完毕，进入工作循环");
}
