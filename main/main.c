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
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ecard_app.h"
#include "ecard_cards.h"
#include "ecard_messages.h"
#include "ecard_netlink.h"
#include "ecard_portal.h"
#include "ecard_setup.h"
#include "ecard_time.h"
#include <nvs_flash.h>
#include "ecard_ui.h"
#include "ecard_profile.h"
#include "ecard_network.h"
#include "ecard_voice.h"
#include "ecard_xiaozhi.h"

static const char *TAG = "ecard_main";

#define INPUT_QUEUE_DEPTH 8

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
    int64_t t_us; /* 入队时刻：用来定位按键到界面响应的延迟 */
    bool record_full;
} input_event_t;

static QueueHandle_t s_input_queue = NULL;
static TaskHandle_t s_input_task = NULL;
static TaskHandle_t s_tick_task = NULL;
static bool s_input_ready = false;

typedef enum {
    BACKLIGHT_ACTIVE = 0,
    BACKLIGHT_DIM,
    BACKLIGHT_OFF
} backlight_state_t;

static backlight_state_t s_backlight_state = BACKLIGHT_ACTIVE;
static bool s_wake_swallow_active = false;
static bool s_wake_swallow_long = false;
static bool s_wake_swallow_click = false;
static bsp_btn_t s_wake_swallow_btn = BSP_BTN_OK;

#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static void on_record_full(void) {
    if (!s_input_queue) return;
    const input_event_t ev = {
        .btn = BSP_BTN_OK,
        .event = BSP_BTN_CLICK,
        .t_us = esp_timer_get_time(),
        .record_full = true,
    };
    (void)xQueueSend(s_input_queue, &ev, 0);
}

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
    const input_event_t input = {
        .btn = btn,
        .event = ev,
        .t_us = esp_timer_get_time(),
        .record_full = false,
    };
    (void)xQueueSend(s_input_queue, &input, 0);
}

static void input_task(void *arg) {
    (void)arg;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, portMAX_DELAY) == pdTRUE) {
            const int64_t dequeue_us = esp_timer_get_time();
            const bool locked = bsp_lvgl_lock(500);
            const int64_t locked_us = esp_timer_get_time();
            bool saving_clip = false;
            uint8_t save_dur = 0;
            size_t save_bytes = 0;
            if (locked) {
                if (input.record_full) {
                    if (ecard_app_get_state() == ECARD_APP_STATE_RECORDING &&
                        !ecard_app_ai_listening()) {
                        saving_clip = ecard_app_begin_save_recording(&save_dur, &save_bytes);
                    }
                } else {
                    bool swallow = false;
                    if (s_backlight_state == BACKLIGHT_OFF) {
                        s_backlight_state = BACKLIGHT_ACTIVE;
                        bsp_display_backlight(100);
                        ecard_ui_feed_activity();
                        swallow = true;
                        s_wake_swallow_btn = input.btn;
                        if (input.event == BSP_BTN_PRESS) {
                            s_wake_swallow_active = true;
                            s_wake_swallow_long = false;
                            s_wake_swallow_click = false;
                        } else if (input.event == BSP_BTN_LONG) {
                            s_wake_swallow_active = true;
                            s_wake_swallow_long = true;
                            s_wake_swallow_click = false;
                        } else {
                            s_wake_swallow_active = false;
                            s_wake_swallow_long = false;
                            s_wake_swallow_click = false;
                        }
                    } else if (s_wake_swallow_active && input.btn == s_wake_swallow_btn) {
                        swallow = true;
                        if (input.event == BSP_BTN_LONG) {
                            s_wake_swallow_long = true;
                        } else if (input.event == BSP_BTN_RELEASE) {
                            s_wake_swallow_active = false;
                            s_wake_swallow_click = !s_wake_swallow_long;
                        }
                    } else if (s_wake_swallow_click && input.btn == s_wake_swallow_btn &&
                               (input.event == BSP_BTN_CLICK || input.event == BSP_BTN_DOUBLE)) {
                        swallow = true;
                        s_wake_swallow_click = false;
                    } else {
                        if (input.event == BSP_BTN_PRESS) {
                            s_wake_swallow_active = false;
                            s_wake_swallow_long = false;
                            s_wake_swallow_click = false;
                        }
                        if (s_backlight_state == BACKLIGHT_DIM) {
                            s_backlight_state = BACKLIGHT_ACTIVE;
                            bsp_display_backlight(100);
                        }
                    }
                    if (!swallow) {
                        const bool finish_by_key =
                            input.btn == BSP_BTN_OK && input.event == BSP_BTN_CLICK &&
                            ecard_app_get_state() == ECARD_APP_STATE_RECORDING &&
                            !ecard_app_ai_listening();
                        if (finish_by_key) {
                            saving_clip = ecard_app_begin_save_recording(&save_dur, &save_bytes);
                        } else {
                            ecard_app_handle_key(input.btn, input.event);
                        }
                    }
                }
                bsp_lvgl_unlock();
            }
            if (saving_clip) {
                // SPIFFS 写盘耗时且触发 Flash 擦写，必须在 LVGL 锁外执行，避免阻塞屏幕与音频任务。
                const uint32_t msg_id = ecard_app_persist_recorded_clip(save_dur);
                if (bsp_lvgl_lock(500)) {
                    ecard_app_start_recorded_playback(msg_id, save_dur);
                    bsp_lvgl_unlock();
                } else {
                    ecard_voice_clip_release();
                }
            }
            const int64_t done_us = esp_timer_get_time();
            const int64_t queue_ms = (dequeue_us - input.t_us) / 1000;
            const int64_t lock_ms = (locked_us - dequeue_us) / 1000;
            const int64_t work_ms = (done_us - locked_us) / 1000;
            // 只报慢按键，正常点击不打日志，避免刷屏。
            if (!locked || queue_ms + lock_ms + work_ms > 50) {
                ESP_LOGW(TAG, "按键 %d/%d 排队 %lld ms，等锁 %lld ms，处理 %lld ms%s",
                         (int)input.btn, (int)input.event, queue_ms, lock_ms, work_ms,
                         locked ? "" : "（等锁超时，已丢弃）");
            }
        }
    }
}

// 周期性时钟心跳任务：200ms 驱动 AI 端点检测/波形/多轮续聊，整秒更新状态栏（时间、电量）与网络心跳
static void tick_task(void *arg) {
    (void)arg;
    uint8_t sim_hour = 9;
    uint8_t sim_min = 40;
    uint8_t sim_sec = 0;
    uint32_t sim_ms = 0;
    uint32_t sec_accum_ms = 1000;
    int lvgl_log = 0;
    int64_t last_tick_us = esp_timer_get_time();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        const int64_t now_us = esp_timer_get_time();
        int64_t raw_ms = (now_us - last_tick_us) / 1000;
        if (raw_ms < 1) raw_ms = 1;
        if (raw_ms > 10000) {
            raw_ms = 10000;
            last_tick_us = now_us;
        } else {
            last_tick_us += raw_ms * 1000;
        }
        const uint32_t delta_ms = (uint32_t)raw_ms;

        sim_ms += delta_ms;
        while (sim_ms >= 1000) {
            sim_ms -= 1000;
            sim_sec++;
            if (sim_sec >= 60) {
                sim_sec = 0;
                sim_min++;
                if (sim_min >= 60) {
                    sim_min = 0;
                    sim_hour = (sim_hour + 1) % 24;
                }
            }
        }

        sec_accum_ms += delta_ms;
        const bool full_sec = (sec_accum_ms >= 1000);
        if (full_sec) {
            sec_accum_ms %= 1000;
            ecard_netlink_tick();
            ecard_xiaozhi_log_stats();
        }

        int soc_val = full_sec ? bsp_battery_soc() : 0;
        uint8_t hour = sim_hour;
        uint8_t minute = sim_min;
        if (ecard_time_is_synced()) {
            ecard_time_now(&hour, &minute, NULL);
            if (!ecard_messages_epoch_synced()) {
                ecard_messages_sync_epoch(ecard_time_epoch_sec());
            }
        }

        if (bsp_lvgl_lock(200)) {
            if (full_sec) {
                char time_buf[16];
                snprintf(time_buf, sizeof(time_buf), "%02d:%02d", hour, minute);
                ecard_ui_update_status(time_buf, soc_val, false, ecard_netlink_is_connected());
                ecard_ui_setup_refresh();
            }
            // 小智 AI 会话只按需连接：状态栏绿点表示当前可以长按 UP 追问。
            ecard_ui_set_ai_connected(ecard_xiaozhi_ws_connected() &&
                                      ecard_xiaozhi_session_ready());
            const ecard_ui_idle_action_t idle_act = ecard_ui_poll_idle_state(delta_ms);
            if (idle_act == ECARD_UI_IDLE_DIM) {
                s_backlight_state = BACKLIGHT_DIM;
                bsp_display_backlight(8);
            } else if (idle_act == ECARD_UI_IDLE_OFF) {
                s_backlight_state = BACKLIGHT_OFF;
                bsp_display_backlight(0);
            }
            ecard_app_tick(hour, minute, delta_ms);
            if (s_backlight_state != BACKLIGHT_ACTIVE &&
                !ecard_ui_is_screen_off() &&
                !ecard_ui_is_dimmed()) {
                s_backlight_state = BACKLIGHT_ACTIVE;
                bsp_display_backlight(100);
            }
            if (full_sec && ++lvgl_log >= 5) {
                lvgl_log = 0;
                lv_mem_monitor_t mon;
                lv_mem_monitor(&mon);
                ESP_LOGI(TAG, "LVGL 内存: 已用 %u KB / %u KB，峰值 %u KB，碎片 %u%%",
                         (unsigned)((mon.total_size - mon.free_size) / 1024),
                         (unsigned)(mon.total_size / 1024),
                         (unsigned)(mon.max_used / 1024), (unsigned)mon.frag_pct);
            }
            bsp_lvgl_unlock();
        }
    }
}

// 音频任务：真实麦克风采集与扬声器回放都放在这里，绝不占用按键回调或 LVGL 任务。
// 每块 20ms：采集 16kHz × 320 样本，回放 8kHz × 160 样本（ADPCM 解码后扩到 16kHz）。
#define AUDIO_CAPTURE_CHUNK 320
#define AUDIO_PLAY_CHUNK    160
// ES8311 输入增益在 BSP 里固定为 30 dB，实测正常说话峰值只有 4048/32767(-18 dBFS)。
// ADPCM 量化噪声按满量程固定，这里补 12 dB 数字增益把语音推到 -6 dBFS 附近，
// 带饱和限幅，避免突然大声时削顶溢出。
#define MIC_DIGITAL_GAIN    4

static TaskHandle_t s_audio_task = NULL;

static void audio_task(void *arg) {
    (void)arg;

    if (bsp_audio_set_format(ECARD_VOICE_SAMPLE_RATE, 16, 1) != ESP_OK) {
        ESP_LOGE(TAG, "音频格式初始化失败，录音/回放不可用");
        vTaskDelete(NULL);
        return;
    }
    bsp_audio_set_volume(80);

    int16_t capture[AUDIO_CAPTURE_CHUNK];
    int16_t playback[AUDIO_PLAY_CHUNK * 2];
    ecard_app_state_t last_state = ECARD_APP_STATE_IDLE;

    for (;;) {
        const ecard_app_state_t state = ecard_app_get_state();

        if (state != last_state) {
            if (state == ECARD_APP_STATE_RECORDING) {
                const size_t cap = ecard_voice_buffer_capacity();
                ESP_LOGI(TAG, "开始录音: 缓冲 %u 字节 (约 %u 秒), 堆剩余 %u 字节",
                         (unsigned)cap, (unsigned)(cap / ECARD_VOICE_BYTES_PER_SEC),
                         (unsigned)esp_get_free_heap_size());
            } else if (last_state == ECARD_APP_STATE_RECORDING) {
                ESP_LOGI(TAG, "录音结束: 峰值 %u/32767 (接近 32767 说明麦克风增益削顶)",
                         (unsigned)ecard_voice_peak_level());
            }
            last_state = state;
        }

        if (state == ECARD_APP_STATE_RECORDING) {
            if (bsp_audio_read(capture, sizeof(capture)) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            for (int i = 0; i < AUDIO_CAPTURE_CHUNK; ++i) {
                int32_t v = (int32_t)capture[i] * MIC_DIGITAL_GAIN;
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                capture[i] = (int16_t)v;
            }
            if (ecard_app_ai_listening()) {
                // AI 追问：只编码上传，不写本地录音缓冲。
                ecard_xiaozhi_feed_mic_pcm(capture, AUDIO_CAPTURE_CHUNK);
            } else if (bsp_lvgl_lock(200)) {
                ecard_app_feed_mic_pcm(capture, AUDIO_CAPTURE_CHUNK);
                bsp_lvgl_unlock();
            }
            continue;
        }

        if (state == ECARD_APP_STATE_PLAYING) {
            size_t frames = 0;
            if (bsp_lvgl_lock(200)) {
                frames = ecard_app_render_playback_pcm(playback, AUDIO_PLAY_CHUNK);
                bsp_lvgl_unlock();
            }
            if (frames == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            } else if (bsp_audio_write(playback, frames * sizeof(int16_t)) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }

        if (ecard_xiaozhi_is_speaking()) {
            const size_t frames = ecard_xiaozhi_read_pcm(playback, AUDIO_PLAY_CHUNK);
            if (frames == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            } else if (bsp_audio_write(playback, frames * sizeof(int16_t)) != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
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

    // NVS 是身份、Wi-Fi 凭据与 Wi-Fi 驱动自身的持久化基础，必须先初始化。
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        (void)nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: 0x%x", (unsigned)nvs_err);
    }

    // 首次配置存储：NVS 中的身份与 Wi-Fi
    ecard_setup_init();

    // 挂载 1 MB SPIFFS 语音留言分区（必须在 ecard_app_init 之前，否则留言会回退成内存演示数据）
    const bool spiffs_ok = ecard_messages_mount_spiffs();

    // 初始化 eCard 核心应用引擎与 UI
    ecard_app_init();
    // 必须在 ecard_app_init() 之后应用：该函数会把档案重置为出厂默认值，
    // 过早写入会被覆盖，导致首页与证件页看不到用户配置的身份。
    ecard_setup_apply_profile();
    if (spiffs_ok) {
        // 只在 SPIFFS 保留最多 3 个年级的古诗古文/英语题库包（首次开机或配置变更时物化并删除非保留年级包）
        (void)ecard_cards_mount_dir("/spiffs");
    }
    ESP_LOGI(TAG, "堆剩余 %u 字节, 最大连续块 %u 字节",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    // Wi-Fi：优先使用 NVS 凭据，每 2 分钟重试，失败后回退手机配网。
    // 先启动网络与热点，再创建界面，首次配网页才能直接显示热点名称，
    // 而不是一直停留在“正在启动热点…”。
    if (!ecard_netlink_init()) {
        ESP_LOGW(TAG, "Wi-Fi 初始化失败，仅保留离线功能");
    }
    ecard_network_start_sync_worker();
    if (!ecard_setup_is_provisioned()) {
        (void)ecard_portal_start();
    }

    if (bsp_lvgl_lock(1000)) {
        ecard_ui_create();
        if (!ecard_setup_is_provisioned()) {
            ecard_ui_show_page(ECARD_UI_PAGE_SETUP);
        }
        bsp_lvgl_unlock();
    }

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (s_input_queue) {
        ecard_app_set_record_full_cb(on_record_full);
        // 编码器/解码器开关已移入 xz_audio 独立任务；按键任务含录音落盘与索引保存，取 7 KB 栈。
        xTaskCreate(input_task, "ecard_input", 7168, NULL, 5, &s_input_task);
        xTaskCreate(tick_task, "ecard_tick", 4608, NULL, 3, &s_tick_task);
        xTaskCreate(audio_task, "ecard_audio", 4096, NULL, 4, &s_audio_task);
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
