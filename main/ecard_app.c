#include "ecard_app.h"
#include "ecard_network.h"
#include "ecard_profile.h"
#include "ecard_ui.h"
#include "ecard_voice.h"
#include <stdio.h>
#include <string.h>

static ecard_app_state_t s_app_state = ECARD_APP_STATE_IDLE;
static uint8_t s_play_progress_sec = 0;
static uint8_t s_play_total_sec = 0;
static bool s_has_polled_current_recess = false;
static uint8_t s_last_recess_minute = 0xFF;

void ecard_app_init(void) {
    ecard_profile_init_default();
    ecard_network_init();
    ecard_voice_init();
    ecard_ui_feed_activity();
    s_app_state = ECARD_APP_STATE_IDLE;
    s_play_progress_sec = 0;
    s_play_total_sec = 0;
    s_has_polled_current_recess = false;
}

void ecard_app_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // 任何按键动作均重置 2 分钟无操作计时，唤醒屏幕
    ecard_ui_feed_activity();

    switch (btn) {
    case BSP_BTN_OK:
        if (ev == BSP_BTN_LONG) {
            // 长按 OK: 开始录制留言 (无需按住不放，最长30秒倒计时)
            if (s_app_state == ECARD_APP_STATE_IDLE) {
                if (ecard_voice_start_recording()) {
                    s_app_state = ECARD_APP_STATE_RECORDING;
                    ecard_ui_show_recording_modal(ECARD_VOICE_MAX_SEC, 0);
                }
            }
        } else if (ev == BSP_BTN_CLICK) {
            if (s_app_state == ECARD_APP_STATE_RECORDING) {
                // 录音期间短按 OK: 提前结束并自动上传
                const uint8_t *adpcm_data = NULL;
                size_t len = 0;
                uint8_t dur = 0;
                if (ecard_voice_stop_recording(&adpcm_data, &len, &dur)) {
                    ecard_network_upload_voice(adpcm_data, len, dur);
                }
                ecard_ui_hide_modal();
                s_app_state = ECARD_APP_STATE_IDLE;
            } else if (s_app_state == ECARD_APP_STATE_PLAYING) {
                // 播放期间短按 OK: 提前停止收听
                ecard_ui_hide_modal();
                s_app_state = ECARD_APP_STATE_IDLE;
            } else if (s_app_state == ECARD_APP_STATE_IDLE) {
                // 空闲状态且有未读消息时，短按 OK 触发收听
                if (ecard_profile_has_unread()) {
                    const ecard_profile_t *p = ecard_profile_get();
                    s_play_progress_sec = 0;
                    s_play_total_sec = p->voice.duration_sec;
                    s_app_state = ECARD_APP_STATE_PLAYING;
                    ecard_ui_show_playing_modal(0, s_play_total_sec);
                }
            }
        }
        break;

    case BSP_BTN_UP:
        if (ev == BSP_BTN_CLICK) {
            // 短按 UP: 手动联网同步一次未读消息
            if (s_app_state == ECARD_APP_STATE_IDLE) {
                bool has_new = false;
                char sender[32] = {0};
                uint8_t dur = 0;
                if (ecard_network_poll_recess_messages(&has_new, sender, &dur) && has_new) {
                    ecard_profile_set_unread_voice(sender, dur);
                    ecard_ui_refresh_content();
                }
            }
        }
        break;

    case BSP_BTN_DOWN:
        if (ev == BSP_BTN_LONG) {
            // 长按 DOWN (>=3秒): 触发紧急 SOS 报警
            s_app_state = ECARD_APP_STATE_SOS;
            const ecard_profile_t *p = ecard_profile_get();
            ecard_network_send_sos_alert(p->student_id, "校园紧急报警");
            // 报警发送后恢复
            s_app_state = ECARD_APP_STATE_IDLE;
        }
        break;

    default:
        break;
    }
}

void ecard_app_tick(uint8_t current_hour, uint8_t current_minute, uint32_t delta_ms) {
    // 1. 空闲超时步进 (2分钟)
    ecard_ui_check_idle_timeout(delta_ms);

    // 2. 课间时段自动同步检查
    bool is_recess = ecard_network_is_recess_time(current_hour, current_minute);
    if (is_recess) {
        if (!s_has_polled_current_recess || s_last_recess_minute != current_minute) {
            s_has_polled_current_recess = true;
            s_last_recess_minute = current_minute;
            // 课间静默同步
            bool has_new = false;
            char sender[32] = {0};
            uint8_t dur = 0;
            if (ecard_network_poll_recess_messages(&has_new, sender, &dur) && has_new) {
                ecard_profile_set_unread_voice(sender, dur);
                ecard_ui_refresh_content();
            }
        }
    } else {
        s_has_polled_current_recess = false;
    }

    // 3. 正在播放中的倒计时流转
    if (s_app_state == ECARD_APP_STATE_PLAYING) {
        s_play_progress_sec++;
        ecard_ui_show_playing_modal(s_play_progress_sec, s_play_total_sec);
        if (s_play_progress_sec >= s_play_total_sec) {
            // 播放完毕，自动标记已读并复原下方区域为姓名班级
            ecard_profile_clear_unread_voice();
            ecard_ui_refresh_content();
            ecard_ui_hide_modal();
            s_app_state = ECARD_APP_STATE_IDLE;
        }
    }
}

void ecard_app_simulate_audio_tick(void) {
    if (s_app_state == ECARD_APP_STATE_RECORDING) {
        int16_t dummy_pcm[1600]; // 100ms 音频
        for (int i = 0; i < 1600; ++i) dummy_pcm[i] = 1500;

        uint8_t rem_sec = 0;
        uint8_t amp = 0;
        bool ok = ecard_voice_write_pcm(dummy_pcm, 1600, &rem_sec, &amp);
        if (ok) {
            ecard_ui_show_recording_modal(rem_sec, amp);
        } else {
            // 录满 30 秒，自动停止并上传
            const uint8_t *adpcm = NULL;
            size_t len = 0;
            uint8_t dur = 0;
            ecard_voice_stop_recording(&adpcm, &len, &dur);
            ecard_network_upload_voice(adpcm, len, dur);
            ecard_ui_hide_modal();
            s_app_state = ECARD_APP_STATE_IDLE;
        }
    }
}

ecard_app_state_t ecard_app_get_state(void) {
    return s_app_state;
}
