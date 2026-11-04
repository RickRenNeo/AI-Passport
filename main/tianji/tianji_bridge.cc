// main/tianji/tianji_bridge.cc — 天机·六爻与小智 AI 底座交互桥接实现
#include "tianji_bridge.h"
#include "tianji_logic.h"
#include "tianji_ui.h"
#include "tianji_audio.h"
#include "bsp_audio.h"
#include "bsp_display.h"

#include "application.h"
#include "board.h"
#include "audio_codec.h"
#include "wifi_board.h"
#include "wifi_manager.h"

#include <esp_log.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

static const char *TAG = "tianji_bridge";

[[maybe_unused]] static tianji_net_state_t s_net_state = TIANJI_NET_STATE_DISABLED;
static tianji_voice_state_t s_voice_state = TIANJI_VOICE_STATE_IDLE;
static tianji_session_t s_current_session;
static tianji_voice_callback_t s_voice_callback = nullptr;

static char s_voice_reply_buffer[1024] = {0};
static char s_user_question[256] = {0};
static bool s_net_enabled = true;

// ============================================================================
// bsp_audio 兼容实现
// ============================================================================

extern "C" esp_err_t bsp_audio_write(const void *data, size_t size) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec || !data || size == 0) return ESP_FAIL;
    if (!codec->output_enabled()) {
        codec->EnableOutput(true);
    }
    size_t sample_count = size / sizeof(int16_t);
    std::vector<int16_t> pcm(static_cast<const int16_t*>(data),
                             static_cast<const int16_t*>(data) + sample_count);
    codec->OutputData(pcm);
    return ESP_OK;
}

extern "C" esp_err_t bsp_audio_set_volume(uint8_t volume) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) return ESP_FAIL;
    codec->SetOutputVolume(volume);
    return ESP_OK;
}

// ============================================================================
// tianji_bridge 核心接口
// ============================================================================

void tianji_bridge_init(void) {
    ESP_LOGI(TAG, "初始化天机·六爻小智桥接层...");
    tianji_audio_init();

    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec && !codec->output_enabled()) {
        codec->EnableOutput(true);
    }

    // 在 LVGL 锁保护下初始化六爻国风 UI
    if (bsp_lvgl_lock(200)) {
        tianji_ui_init();
        int soc = 0;
        bool chg = false, dchg = false;
        if (Board::GetInstance().GetBatteryLevel(soc, chg, dchg) && soc >= 0) {
            tianji_ui_update_battery(soc);
        }
        bsp_lvgl_unlock();
    }
}

void tianji_bridge_handle_key(tianji_key_action_t action) {
    if (bsp_lvgl_lock(100)) {
        tianji_ui_handle_key(action);
        bsp_lvgl_unlock();
    }
}

void tianji_bridge_update_battery(int soc) {
    if (bsp_lvgl_lock(100)) {
        tianji_ui_update_battery(soc);
        bsp_lvgl_unlock();
    }
}

void tianji_bridge_set_net_state(tianji_net_state_t state) {
    s_net_state = state;
    if (bsp_lvgl_lock(100)) {
        tianji_ui_update_net_state(state);
        bsp_lvgl_unlock();
    }
}

void tianji_bridge_on_stt_text(const char *text) {
    if (!text) return;
    snprintf(s_user_question, sizeof(s_user_question), "%s", text);
    ESP_LOGI(TAG, "求测者口述事项: %s", s_user_question);
}

void tianji_bridge_on_tts_start(void) {
    s_voice_state = TIANJI_VOICE_STATE_PLAYING;
    if (s_voice_callback) {
        s_voice_callback(s_voice_state, "【宗师诵读中...】");
    }
}

void tianji_bridge_on_tts_sentence(const char *text) {
    if (!text) return;
    if (strlen(s_voice_reply_buffer) + strlen(text) < sizeof(s_voice_reply_buffer) - 2) {
        strcat(s_voice_reply_buffer, text);
    }
    s_voice_state = TIANJI_VOICE_STATE_SUCCESS;
    if (s_voice_callback) {
        s_voice_callback(s_voice_state, s_voice_reply_buffer);
    }
}

void tianji_bridge_on_tts_stop(void) {
    s_voice_state = TIANJI_VOICE_STATE_IDLE;
    if (s_voice_callback) {
        s_voice_callback(s_voice_state, s_voice_reply_buffer);
    }
}

// ============================================================================
// tianji_net 契约实现（无缝兼容现有 tianji_ui）
// ============================================================================

extern "C" {

esp_err_t tianji_net_init(void) {
    return ESP_OK;
}

void tianji_net_set_enabled(bool enable) {
    s_net_enabled = enable;
}

bool tianji_net_is_enabled(void) {
    return s_net_enabled;
}

bool tianji_net_is_connected(void) {
    return WifiManager::GetInstance().IsConnected();
}

tianji_net_state_t tianji_net_get_state(void) {
    if (!s_net_enabled) return TIANJI_NET_STATE_DISABLED;
    if (WifiManager::GetInstance().IsConnected()) return TIANJI_NET_STATE_CONNECTED;
    return TIANJI_NET_STATE_CONNECTING;
}

const char *tianji_net_get_status_label(void) {
    if (!s_net_enabled) return "离线";
    if (WifiManager::GetInstance().IsConnected()) return "已联机";
    return "连接中";
}

esp_err_t tianji_net_request_ai(const tianji_session_t *session, tianji_net_callback_t cb) {
    (void)session;
    (void)cb;
    return ESP_OK;
}

const char *tianji_net_get_ai_result(void) {
    return s_voice_reply_buffer;
}

esp_err_t tianji_voice_start(const tianji_session_t *session, tianji_voice_callback_t cb) {
    if (session) {
        s_current_session = *session;
    }
    s_voice_callback = cb;
    s_voice_state = TIANJI_VOICE_STATE_RECORDING;
    s_voice_reply_buffer[0] = '\0';
    s_user_question[0] = '\0';

    ESP_LOGI(TAG, "开启小智流式语音交互管道，准备聆听求测者问卦...");

    // 触发小智应用进入聆听状态 (流式 Opus 直传)
    Application::GetInstance().StartListening();

    if (s_voice_callback) {
        s_voice_callback(s_voice_state, "【请对准设备陈述所测之事...】");
    }
    return ESP_OK;
}

void tianji_voice_stop_recording(void) {
    s_voice_state = TIANJI_VOICE_STATE_UPLOADING;
    if (s_voice_callback) {
        s_voice_callback(s_voice_state, "【宗师推演中...】");
    }
    Application::GetInstance().StopListening();
}

void tianji_voice_stop_playing(void) {
    s_voice_state = TIANJI_VOICE_STATE_IDLE;
    Application::GetInstance().AbortSpeaking(kAbortReasonNone);
}

tianji_voice_state_t tianji_voice_get_state(void) {
    return s_voice_state;
}

bool tianji_voice_is_recording(void) {
    return s_voice_state == TIANJI_VOICE_STATE_RECORDING;
}

bool tianji_voice_is_playing(void) {
    return s_voice_state == TIANJI_VOICE_STATE_PLAYING;
}

int tianji_voice_get_current_volume(void) {
    return Application::GetInstance().IsVoiceDetected() ? 70 : 10;
}

const char *tianji_voice_get_text_reply(void) {
    return s_voice_reply_buffer;
}

const char *tianji_voice_get_audio_url(void) {
    return "";
}

} // extern "C"
