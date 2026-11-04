// main/tianji/tianji_bridge.cc — 天机·六爻与小智 AI 底座交互桥接实现
//
// 语音解卦走小智的流式语音通道：
//   进入问卦页 -> StartListening() -> 服务端 STT/LLM/TTS
//   本地负责：连接态收敛、5 秒静音自动提交、TTS 文本流式上屏、多轮追问。
#include "tianji_bridge.h"
#include "tianji_logic.h"
#include "tianji_ui.h"
#include "tianji_audio.h"
#include "bsp_audio.h"
#include "bsp_display.h"

#include "application.h"
#include "board.h"
#include "audio_codec.h"
#include "mcp_server.h"
#include "wifi_board.h"
#include "wifi_manager.h"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>

#include <string>
#include <vector>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <mutex>

static const char *TAG = "tianji_bridge";

[[maybe_unused]] static tianji_net_state_t s_net_state = TIANJI_NET_STATE_DISABLED;
static tianji_voice_state_t s_voice_state = TIANJI_VOICE_STATE_IDLE;
static tianji_session_t s_current_session;
static tianji_voice_callback_t s_voice_callback = nullptr;

static char s_voice_reply_buffer[1024] = {0};
static char s_user_question[256] = {0};
static char s_error_text[192] = {0};
static bool s_net_enabled = true;
// MCP 工具调用发生在 MQTT 回调任务，与主任务的会话/问题写入需要互斥。
static std::mutex s_session_mutex;

// 本地录音节拍器：小智服务端在 manual 模式下不会替我们判断静音，
// 必须由设备按时发送 listen stop，否则会一直停留在录音态。
namespace {

enum class Phase {
    kIdle = 0,
    kConnecting,   // 等待音频通道 / 服务端 listen start 完成
    kRecording,    // 正在上行 Opus
    kWaiting,      // 已 stop，等待 STT/LLM/TTS
    kSpeaking,     // 正在接收/播放 TTS
    kError,
};

constexpr int64_t kTickUs = 100 * 1000;             // 100 ms 看门狗
constexpr int64_t kSilenceStopUs = 5 * 1000 * 1000; // 静音 5 秒自动结束
constexpr int64_t kMaxRecordUs = 20 * 1000 * 1000;  // 单轮录音硬上限
constexpr int64_t kConnectTimeoutUs = 15 * 1000 * 1000;
constexpr int64_t kAnswerTimeoutUs = 30 * 1000 * 1000;

Phase s_phase = Phase::kIdle;
int64_t s_phase_since_us = 0;
int64_t s_last_voice_us = 0;
bool s_vad_active = false;
esp_timer_handle_t s_watchdog = nullptr;

static tianji_voice_state_t PublicState(Phase phase) {
    switch (phase) {
        case Phase::kIdle:
            return s_voice_reply_buffer[0] ? TIANJI_VOICE_STATE_SUCCESS : TIANJI_VOICE_STATE_IDLE;
        case Phase::kConnecting:
            return TIANJI_VOICE_STATE_CONNECTING;
        case Phase::kRecording:
            return TIANJI_VOICE_STATE_RECORDING;
        case Phase::kWaiting:
            return TIANJI_VOICE_STATE_UPLOADING;
        case Phase::kSpeaking:
            return TIANJI_VOICE_STATE_PLAYING;
        case Phase::kError:
            return TIANJI_VOICE_STATE_ERROR;
    }
    return TIANJI_VOICE_STATE_IDLE;
}

// UI 回调统一投递到主任务，避免在 esp_timer 任务里直接操作 LVGL。
static void NotifyUi(tianji_voice_state_t state, const std::string& text) {
    auto callback = s_voice_callback;
    if (callback == nullptr) {
        return;
    }
    Application::GetInstance().Schedule([callback, state, text]() {
        callback(state, text.c_str());
    });
}

static void EnterPhase(Phase phase, const std::string& message, bool force_notify = false) {
    const Phase previous = s_phase;
    const bool changed = (previous != phase);
    s_phase = phase;
    s_phase_since_us = esp_timer_get_time();
    s_voice_state = PublicState(phase);
    if (changed) {
        ESP_LOGI(TAG, "语音阶段 %d -> %d (%s)", static_cast<int>(previous),
                 static_cast<int>(phase), message.c_str());
    }
    if (changed || force_notify) {
        NotifyUi(s_voice_state, message);
    }
}

static void FailRound(const char* message) {
    snprintf(s_error_text, sizeof(s_error_text), "%s", message ? message : "语音交互异常");
    ESP_LOGW(TAG, "本轮语音失败: %s", s_error_text);
    EnterPhase(Phase::kError, s_error_text);
}

static void StopRecordingToServer(bool auto_stop) {
    if (s_phase != Phase::kRecording) {
        return;
    }
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateListening) {
        // 通道还没真正进入 listening，此时发 stop 会被 Application 丢弃，
        // 界面就会永远停在“请稍候”。保留录音态，等下一拍再判断。
        ESP_LOGW(TAG, "尚未进入 listening，暂缓提交 (state=%d)",
                 static_cast<int>(app.GetDeviceState()));
        return;
    }
    ESP_LOGI(TAG, "%s提交本轮录音", auto_stop ? "静音超时，" : "用户按键，");
    app.StopListening();
    EnterPhase(Phase::kWaiting, "【宗师推演中...】");
}

static void WatchdogTick(void* arg) {
    (void)arg;
    if (s_phase == Phase::kIdle || s_phase == Phase::kError) {
        return;
    }

    auto& app = Application::GetInstance();
    const int64_t now = esp_timer_get_time();
    const int64_t phase_ms = (now - s_phase_since_us) / 1000;

    switch (s_phase) {
        case Phase::kConnecting: {
            const DeviceState device_state = app.GetDeviceState();
            if (device_state == kDeviceStateListening) {
                s_vad_active = false;
                s_last_voice_us = now;
                EnterPhase(Phase::kRecording, "【请对准设备陈述所测之事...】");
            } else if (device_state == kDeviceStateIdle && phase_ms > 2000) {
                FailRound("无法连接小智语音服务，请检查网络后重试。");
            } else if (phase_ms > kConnectTimeoutUs / 1000) {
                FailRound("语音通道连接超时，请检查 Wi-Fi 后重试。");
            }
            break;
        }

        case Phase::kRecording: {
            const DeviceState device_state = app.GetDeviceState();
            if (device_state == kDeviceStateIdle && phase_ms > 1500) {
                FailRound("语音通道已断开，请重试。");
                break;
            }
            const bool voice = app.IsVoiceDetected();
            if (voice != s_vad_active) {
                s_vad_active = voice;
                ESP_LOGI(TAG, "VAD %s", voice ? "检测到人声" : "静音");
            }
            if (voice) {
                s_last_voice_us = now;
            }
            const int64_t silent_ms = (now - s_last_voice_us) / 1000;
            if (phase_ms > kMaxRecordUs / 1000) {
                ESP_LOGW(TAG, "录音达到 %lld ms 上限，自动提交", (long long)phase_ms);
                StopRecordingToServer(true);
            } else if (silent_ms >= kSilenceStopUs / 1000) {
                ESP_LOGI(TAG, "连续静音 %lld ms，自动结束本轮录音", (long long)silent_ms);
                StopRecordingToServer(true);
            }
            break;
        }

        case Phase::kWaiting:
            if (phase_ms > kAnswerTimeoutUs / 1000) {
                FailRound("未收到宗师回应，请检查网络后重试。");
            }
            break;

        case Phase::kSpeaking:
            // TTS 播放由服务端 tts stop 收尾；这里只做超长兜底。
            if (phase_ms > 120 * 1000) {
                FailRound("宗师回应超时，请重试。");
            }
            break;

        default:
            break;
    }
}

static const char* LinePositionName(int index) {
    static const char* kNames[6] = {"初爻", "二爻", "三爻", "四爻", "五爻", "上爻"};
    return (index >= 0 && index < 6) ? kNames[index] : "?";
}

// 给服务端 LLM 的卦象上下文。设备端 MCP 工具被调用时返回，确保 AI 不会凭空编卦。
static std::string BuildHexagramContext() {
    std::lock_guard<std::mutex> lock(s_session_mutex);
    if (!s_current_session.is_completed) {
        return "当前尚未完成六爻摇卦。请先请用户在主界面完成六次摇卦，再进行解卦。";
    }

    const tianji_hexagram_t* primary = tianji_get_hexagram(s_current_session.primary_bits);
    const tianji_hexagram_t* transformed = tianji_get_hexagram(s_current_session.transformed_bits);
    if (primary == nullptr) {
        return "卦象数据异常，请重新摇卦。";
    }

    std::string text;
    text.reserve(1200);
    auto append = [&text](const char* format, ...) {
        char line[320];
        va_list args;
        va_start(args, format);
        vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        text += line;
    };

    append("【天机·六爻 本次真实摇卦结果】请严格依据以下卦象解卦，不得更改卦名或另起一卦。\n");
    if (s_user_question[0]) {
        append("求测者所问：%s\n", s_user_question);
    }
    append("本卦：第%u卦 %s（上%s%s，下%s%s）\n", static_cast<unsigned>(primary->index),
           primary->name, primary->upper_name,
           tianji_trigram_nature(static_cast<tianji_trigram_t>(
               static_cast<uint8_t>(s_current_session.primary_bits >> 3) & 0x07)),
           primary->lower_name,
           tianji_trigram_nature(static_cast<tianji_trigram_t>(
               static_cast<uint8_t>(s_current_session.primary_bits) & 0x07)));

    if (s_current_session.moving_count == 0) {
        append("动爻：无（六爻皆静，以本卦卦辞断之）\n");
    } else {
        append("动爻：共 %u 爻（自下而上）", static_cast<unsigned>(s_current_session.moving_count));
        for (uint8_t i = 0; i < s_current_session.moving_count; ++i) {
            append("%s%s", i == 0 ? " " : "、",
                   LinePositionName(s_current_session.moving_indices[i]));
        }
        append("\n");
        append("变卦：第%u卦 %s\n", static_cast<unsigned>(transformed->index), transformed->name);
    }

    append("卦辞：%s\n", primary->judgment);
    append("象曰：%s\n", primary->xiang);
    if (primary->career) {
        append("事业参考：%s\n", primary->career);
    }
    if (primary->wealth) {
        append("财运参考：%s\n", primary->wealth);
    }
    if (primary->relationship) {
        append("姻缘参考：%s\n", primary->relationship);
    }
    append("【作答要求】结合求测者所问，用口语化中文先断吉凶，再分事业/财运/姻缘要点，"
           "最后给一条可执行建议；控制在 300 字以内，便于语音播报，不要逐字念出本段格式说明。");
    return text;
}

static void RegisterHexagramTool() {
    McpServer::GetInstance().AddTool(
        "self.tianji.get_hexagram",
        "获取本次六爻摇卦的真实结果（本卦、变卦、动爻、卦辞、象曰与三才断语）。"
        "当用户询问占卜、解卦、问卦、求测事业/财运/姻缘/吉凶时，必须先调用本工具获取卦象，"
        "再结合用户所问之事解卦。严禁在未调用本工具时凭空编造卦象。",
        PropertyList(), [](const PropertyList&) -> ToolResult {
            std::string context = BuildHexagramContext();
            ESP_LOGI(TAG, "服务端 LLM 调取卦象上下文 (%u 字节)", (unsigned)context.size());
            return ReturnValue(std::move(context));
        });
}

}  // namespace

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

    RegisterHexagramTool();

    esp_timer_create_args_t timer_args = {
        .callback = WatchdogTick,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "tianji_voice",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&timer_args, &s_watchdog) == ESP_OK) {
        esp_timer_start_periodic(s_watchdog, kTickUs);
    } else {
        ESP_LOGE(TAG, "语音看门狗创建失败，静音自动结束不可用");
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
    if (!text || text[0] == '\0') return;
    {
        std::lock_guard<std::mutex> lock(s_session_mutex);
        snprintf(s_user_question, sizeof(s_user_question), "%s", text);
    }
    ESP_LOGI(TAG, "求测者口述事项: %s", s_user_question);

    // STT 已回来说明服务端正常，重置应答超时，并让解卦页显示所问。
    if (s_phase == Phase::kWaiting || s_phase == Phase::kConnecting ||
        s_phase == Phase::kRecording) {
        s_phase = Phase::kWaiting;
        s_phase_since_us = esp_timer_get_time();
        s_voice_state = TIANJI_VOICE_STATE_UPLOADING;
        NotifyUi(TIANJI_VOICE_STATE_UPLOADING, s_user_question);
    }
}

void tianji_bridge_on_tts_start(void) {
    if (s_phase == Phase::kIdle || s_phase == Phase::kError) {
        return;
    }
    EnterPhase(Phase::kSpeaking, "【宗师诵读中...】");
}

void tianji_bridge_on_tts_sentence(const char *text) {
    if (!text || text[0] == '\0') return;
    const size_t used = strlen(s_voice_reply_buffer);
    const size_t need = strlen(text);
    if (used + need < sizeof(s_voice_reply_buffer) - 1) {
        memcpy(s_voice_reply_buffer + used, text, need + 1);
    } else {
        ESP_LOGW(TAG, "断语超过 %u 字节，后续句子已截断", (unsigned)sizeof(s_voice_reply_buffer));
    }
    s_phase = Phase::kSpeaking;
    s_voice_state = TIANJI_VOICE_STATE_PLAYING;
    s_phase_since_us = esp_timer_get_time();
    NotifyUi(TIANJI_VOICE_STATE_PLAYING, s_voice_reply_buffer);
}

void tianji_bridge_on_tts_stop(void) {
    if (s_phase != Phase::kSpeaking && s_phase != Phase::kWaiting) {
        return;
    }
    if (s_voice_reply_buffer[0] == '\0') {
        FailRound("未收到宗师断语，请重试。");
        return;
    }
    ESP_LOGI(TAG, "宗师诵读结束，共 %u 字节", (unsigned)strlen(s_voice_reply_buffer));
    EnterPhase(Phase::kIdle, s_voice_reply_buffer);
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

bool tianji_net_is_config_mode(void) {
    return WifiManager::GetInstance().IsConfigMode();
}

const char *tianji_get_ap_suffix(void) {
    static char suffix[8] = {0};
    uint8_t mac[6];
    // 配网热点由 WifiConfigurationAp 用 SoftAP MAC 生成（末尾比 STA MAC 大 1），
    // 必须用 ESP_MAC_WIFI_SOFTAP，否则屏上提示的热点名和实际 SSID 对不上。
    if (esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) == ESP_OK) {
        snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    } else {
        snprintf(suffix, sizeof(suffix), "C781");
    }
    return suffix;
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
    {
        std::lock_guard<std::mutex> lock(s_session_mutex);
        if (session) {
            s_current_session = *session;
        }
        s_user_question[0] = '\0';
    }
    s_voice_callback = cb;
    s_voice_reply_buffer[0] = '\0';
    s_error_text[0] = '\0';
    s_vad_active = false;
    s_last_voice_us = esp_timer_get_time();

    ESP_LOGI(TAG, "开启小智流式语音交互管道，准备聆听求测者问卦...");
    if (s_phase == Phase::kSpeaking) {
        // 上一轮还在播报：先打断，再复用同一条会话追问。
        Application::GetInstance().AbortSpeaking(kAbortReasonNone);
    }
    EnterPhase(Phase::kConnecting, "【正在连通天机算力...】");

    // 触发小智应用进入聆听状态 (manual 模式，由本地 5 秒静音计时决定何时 stop)
    Application::GetInstance().StartListening();
    return ESP_OK;
}

void tianji_voice_stop_recording(void) {
    if (s_phase == Phase::kConnecting) {
        // 通道尚未完成 listen start，此时提交只会让界面卡在“请稍候”。
        // 保持连接态，等真正开始录音后再让用户按确定键结束。
        ESP_LOGW(TAG, "仍在连接语音通道，忽略本次提前提交");
        return;
    }
    if (s_phase != Phase::kRecording) {
        return;
    }
    StopRecordingToServer(false);
}

void tianji_voice_stop_playing(void) {
    if (s_phase != Phase::kSpeaking) {
        return;
    }
    Application::GetInstance().AbortSpeaking(kAbortReasonNone);
    ESP_LOGI(TAG, "用户静音阅读，保留已收到的断语文本");
    EnterPhase(Phase::kIdle, s_voice_reply_buffer);
}

void tianji_voice_end(void) {
    if (s_phase == Phase::kRecording || s_phase == Phase::kConnecting) {
        Application::GetInstance().StopListening();
    }
    if (s_phase == Phase::kSpeaking) {
        Application::GetInstance().AbortSpeaking(kAbortReasonNone);
    }
    Application::GetInstance().CloseAudioChannel();
    s_vad_active = false;
    EnterPhase(Phase::kIdle, s_voice_reply_buffer);
}

tianji_voice_state_t tianji_voice_get_state(void) {
    return s_voice_state;
}

bool tianji_voice_is_recording(void) {
    return s_phase == Phase::kRecording;
}

bool tianji_voice_is_playing(void) {
    return s_phase == Phase::kSpeaking;
}

int tianji_voice_get_current_volume(void) {
    return Application::GetInstance().IsVoiceDetected() ? 70 : 10;
}

const char *tianji_voice_get_text_reply(void) {
    if (s_phase == Phase::kError && s_error_text[0]) {
        return s_error_text;
    }
    return s_voice_reply_buffer;
}

const char *tianji_voice_get_question(void) {
    return s_user_question;
}

const char *tianji_voice_get_audio_url(void) {
    return "";
}

} // extern "C"
