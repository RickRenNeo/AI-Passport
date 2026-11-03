// main/main.c — 《天机·六爻》随身周易推演掌机 主控入口
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
#include "esp_random.h"
#include <stdlib.h>

#include "tianji_logic.h"
#include "tianji_ui.h"
#include "tianji_audio.h"
#include "tianji_net.h"

static const char *TAG = "tianji_main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue = NULL;
static TaskHandle_t s_input_task = NULL;
static volatile bool s_input_ready = false;

// 防开机长按误判守卫
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static void process_input(const input_event_t *input) {
    tianji_key_action_t action;
    bool has_action = false;

    if (input->event == BSP_BTN_LONG && input->btn == BSP_BTN_OK) {
        action = TIANJI_KEY_OK_LONG;
        has_action = true;
    } else if (input->event == BSP_BTN_CLICK) {
        if (input->btn == BSP_BTN_UP) {
            action = TIANJI_KEY_UP_CLICK;
            has_action = true;
        } else if (input->btn == BSP_BTN_DOWN) {
            action = TIANJI_KEY_DOWN_CLICK;
            has_action = true;
        } else if (input->btn == BSP_BTN_OK) {
            action = TIANJI_KEY_OK_CLICK;
            has_action = true;
        }
    }

    if (has_action) {
        if (bsp_lvgl_lock(100)) {
            tianji_ui_handle_key(action);
            bsp_lvgl_unlock();
        }
    }
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            process_input(&input);
        }
    }
}

// 电池电量定时更新任务
static void battery_monitor_task(void *arg) {
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        int soc = bsp_battery_soc();
        if (bsp_lvgl_lock(100)) {
            tianji_ui_update_battery(soc);
            bsp_lvgl_unlock();
        }
    }
}

static esp_err_t input_dispatch_init(void) {
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(input_task, "tianji_input", 4096, NULL, 5, &s_input_task) != pdPASS) {
        vQueueDelete(s_input_queue);
        s_input_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_input_ready = true;
    return ESP_OK;
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手，按键输入恢复");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== 《天机·六爻》智能随身推演固件启动 ===");

    // 硬件真随机数播种，确保起卦绝对随机
    srand((unsigned)(esp_timer_get_time() ^ esp_random()));

    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & ~(1u << ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "休眠唤醒位图: 0x%02x", (unsigned)wake_causes);
    }
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒守卫已就绪");
    }

    // 1. 初始化 I2C 总线
    bsp_i2c_init();

    // 2. 初始化屏幕与 LVGL 显示系统
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示系统初始化失败，无法启动 UI");
        return;
    }
    bsp_display_backlight(100);

    // 3. 初始化音效系统
    tianji_audio_init();

    // 4. 初始化按键驱动与分发任务
    if (input_dispatch_init() == ESP_OK) {
        if (bsp_button_init(on_key, NULL) == ESP_OK) {
            if (s_key_guard.armed) {
                const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
                key_guard_sync_released(&s_key_guard, released);
            }
        }
    }

    // 5. 初始化电池电量检测
    if (bsp_battery_init() == ESP_OK) {
        xTaskCreate(battery_monitor_task, "tianji_bat", 2048, NULL, 1, NULL);
    }

    // 6. 初始化网络底座并默认启动 Wi-Fi 自动联网（随时待命进行占卜问卦）
    tianji_net_init();
    tianji_net_set_enabled(true);

    // 7. 载入《天机·六爻》极简现代科技风 UI
    if (bsp_lvgl_lock(200)) {
        tianji_ui_init();
        int init_soc = bsp_battery_soc();
        if (init_soc >= 0) {
            tianji_ui_update_battery(init_soc);
        }
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "《天机·六爻》初始化完成，系统进入待命状态");
}
