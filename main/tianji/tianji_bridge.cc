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
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>

static const char *TAG = "tianji_bridge";

[[maybe_unused]] static tianji_net_state_t s_net_state = TIANJI_NET_STATE_DISABLED;
static tianji_voice_state_t s_voice_state = TIANJI_VOICE_STATE_IDLE;
static tianji_session_t s_current_session;
static tianji_voice_callback_t s_voice_callback = nullptr;

static char s_transcript[4096] = {0};
static char s_user_question[256] = {0};
static char s_error_text[192] = {0};
static bool s_net_enabled = true;
// 解卦会话状态：进入解卦页后保持同一条小智通道，断语播毕自动再开麦，实现无键多轮。
static bool s_session_active = false;
static bool s_round_voice_heard = false;
static bool s_round_answered = false;
static bool s_followup_pending = false;
static int64_t s_followup_ready_us = 0;
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
constexpr int64_t kSilenceStopUs = 2 * 1000 * 1000; // 说话结束后静音 2 秒自动呈报（贴近小智原生 VAD）
constexpr int64_t kNoVoiceWaitUs = 5 * 1000 * 1000; // 一直没开口：5 秒后自动收起麦克风，不提交空录音
constexpr int64_t kMaxRecordUs = 20 * 1000 * 1000;  // 单轮录音硬上限
constexpr int64_t kConnectTimeoutUs = 15 * 1000 * 1000;
constexpr int64_t kAnswerTimeoutUs = 8 * 1000 * 1000;   // 提交后 8 秒内连 STT 都没回，判定本轮空转
constexpr int64_t kFollowupDelayUs = 300 * 1000;    // 断语播毕后稍等再开麦
constexpr int64_t kFollowupDrainWaitUs = 8 * 1000 * 1000; // 等播放队列排空的兜底
constexpr int64_t kVoiceRunTicksNeeded = 3;         // 连续 300 ms 人声才算真的开了口
constexpr int64_t kMicSettleUs = 1200 * 1000;       // 开麦后的自噪声/回声屏蔽窗

Phase s_phase = Phase::kIdle;
int64_t s_phase_since_us = 0;
int64_t s_last_voice_us = 0;
int64_t s_submit_us = 0;
int64_t s_last_tts_us = 0;
int64_t s_record_start_us = 0;
int64_t s_last_pulse_us = 0;
uint32_t s_voice_run_ticks = 0;
bool s_gap_announced = false;
// 空轮（用户没开口）虽然本地不"提交"，但音频早已随聆听一起上行，
// 服务端仍会对这 5 秒噪声跑一遍识别并作答。置位后丢弃这一轮的服务端消息。
bool s_discard_turn = false;
int64_t s_discard_since_us = 0;
bool s_vad_active = false;
esp_timer_handle_t s_watchdog = nullptr;

static tianji_voice_state_t PublicState(Phase phase) {
    switch (phase) {
        case Phase::kIdle:
            return s_transcript[0] ? TIANJI_VOICE_STATE_SUCCESS : TIANJI_VOICE_STATE_IDLE;
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

// 对话文本累积：解卦页只显示这一段，问与答按轮追加，超长时丢弃最早的一半。
// MCP 工具调用在 MQTT 任务里读取同一段文本，必须与会话互斥锁共用。
static void AppendTranscript(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    std::lock_guard<std::mutex> lock(s_session_mutex);
    size_t used = strlen(s_transcript);
    const size_t need = strlen(text);
    if (used + need + 1 > sizeof(s_transcript)) {
        size_t keep = sizeof(s_transcript) / 2;
        size_t drop = (used > keep) ? (used - keep) : 0;
        const char* newline = strchr(s_transcript + drop, '\n');
        if (newline != nullptr) {
            drop = static_cast<size_t>(newline - s_transcript) + 1;
        }
        memmove(s_transcript, s_transcript + drop, used - drop + 1);
        used = strlen(s_transcript);
        if (used + need + 1 > sizeof(s_transcript)) {
            used = sizeof(s_transcript) - need - 1;
            s_transcript[used] = '\0';
        }
    }
    memcpy(s_transcript + used, text, need + 1);
}

// 服务端偶尔会把 MCP 工具调用标记当成 TTS 句子下发（如 “% self.tianji.get_hexagram...”），
// 这类内容绝不能上屏，否则屏幕上会出现英文调用信息。
static bool ContainsCjk(const char* text) {
    for (const char* p = text; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c >= 0xE4 && c <= 0xE9) {  // UTF-8 常用汉字首字节区间
            return true;
        }
    }
    return false;
}

static bool IsToolCallArtifact(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return true;
    }
    if (text[0] == '%' || text[0] == '{') {
        return true;
    }
    if (strstr(text, "self.") != nullptr || strstr(text, "get_hexagram") != nullptr ||
        strstr(text, "\"type\"") != nullptr || strstr(text, "```") != nullptr) {
        return true;
    }
    // 中文解卦断语必然含汉字；纯英文短串只可能是协议/工具残留。
    return !ContainsCjk(text);
}

static void ResetRoundState(void) {
    {
        std::lock_guard<std::mutex> lock(s_session_mutex);
        s_user_question[0] = '\0';
    }
    s_error_text[0] = '\0';
    s_vad_active = false;
    s_round_voice_heard = false;
    s_round_answered = false;
    s_voice_run_ticks = 0;
    s_submit_us = 0;
    s_last_tts_us = 0;
    s_last_pulse_us = 0;
    s_gap_announced = false;
    s_discard_turn = false;
    s_last_voice_us = esp_timer_get_time();
}

static void BeginListeningRound(bool fresh);
static void FailRound(const char* message);

// 断语播完后等播放队列排空，再自动开麦，用户无需按任何键即可追问。
static void MaybeStartFollowUp(void) {
    if (!s_followup_pending) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (now < s_followup_ready_us) {
        return;
    }
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle || !app.IsAudioPlaybackIdle()) {
        if (now - s_followup_ready_us > kFollowupDrainWaitUs) {
            ESP_LOGW(TAG, "等待播放结束超时，本轮转为按键追问");
            s_followup_pending = false;
        }
        return;
    }
    s_followup_pending = false;
    ESP_LOGI(TAG, "断语播毕，自动开启下一轮聆听");
    BeginListeningRound(false);
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

static void BeginListeningRound(bool fresh) {
    auto& app = Application::GetInstance();
    if (fresh) {
        s_transcript[0] = '\0';
        s_session_active = true;
    }
    ResetRoundState();
    if (!WifiManager::GetInstance().IsConnected()) {
        FailRound("网络未连接，请检查 Wi-Fi 后重试");
        return;
    }
    if (s_phase == Phase::kSpeaking) {
        // 上一轮还在播报：先打断，再复用同一条会话追问。
        app.AbortSpeaking(kAbortReasonNone);
    }
    EnterPhase(Phase::kConnecting, "【正在连通天机算力……】");
    // manual 模式，由本地静音计时决定何时 stop
    app.StartListening();
}

static void FailRound(const char* message) {
    snprintf(s_error_text, sizeof(s_error_text), "%s", message ? message : "语音交互异常");
    ESP_LOGW(TAG, "本轮语音失败: %s", s_error_text);
    s_followup_pending = false;
    AppendTranscript("\n");
    AppendTranscript(s_error_text);
    AppendTranscript("\n");
    EnterPhase(Phase::kError, s_error_text);
}

// 结束本轮聆听。只有真的听到过人声才提交，避免空录音换来一句无意义的回应。
static void FinishListeningRound(bool user_pressed) {
    if (s_phase != Phase::kRecording) {
        return;
    }
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateListening) {
        // 通道还没真正进入 listening，此时发 stop 会被 Application 丢弃，
        // 保留录音态，等下一拍再判断。
        ESP_LOGW(TAG, "尚未进入 listening，暂缓提交 (state=%d)",
                 static_cast<int>(app.GetDeviceState()));
        return;
    }
    if (!s_round_voice_heard) {
        ESP_LOGI(TAG, "本轮未检测到人声，结束聆听且不提交空录音");
        s_followup_pending = false;
        // 上行音频已经发出去了，服务端很快还会回一轮"幻觉识别"。
        // 先打断它，并在本轮结束前丢弃这条通道上收到的所有 STT/TTS。
        s_discard_turn = true;
        s_discard_since_us = esp_timer_get_time();
        Application::GetInstance().AbortSpeaking(kAbortReasonNone);
        app.StopListening();
        EnterPhase(Phase::kIdle, "静候追问");
        return;
    }
    ESP_LOGI(TAG, "%s提交本轮录音", user_pressed ? "用户按键，" : "静音超时，");
    s_submit_us = esp_timer_get_time();
    app.StopListening();
    EnterPhase(Phase::kWaiting, "【宗师推演中……】");
}

static void WatchdogTick(void* arg) {
    (void)arg;

    if (s_phase == Phase::kError) {
        return;
    }
    if (s_phase == Phase::kIdle) {
        // 空轮丢弃最多持续 4 秒：服务端既没回 tts_stop 也不会把标记留到下一轮。
        if (s_discard_turn && (esp_timer_get_time() - s_discard_since_us) > 4 * 1000 * 1000) {
            ESP_LOGI(TAG, "空轮丢弃超时，恢复接收服务端消息");
            s_discard_turn = false;
        }
        MaybeStartFollowUp();
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
                s_round_voice_heard = false;
                s_voice_run_ticks = 0;
                s_last_voice_us = now;
                // 开麦瞬间的自噪声（提示音尾音、I2S 启动瞬态）不算人声。
                s_record_start_us = now;
                EnterPhase(Phase::kRecording, "【请对准设备陈述所测之事……】");
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
            const bool voice = app.IsVoiceDetected() && (now - s_record_start_us) > kMicSettleUs;
            if (voice != s_vad_active) {
                s_vad_active = voice;
                ESP_LOGI(TAG, "VAD %s", voice ? "检测到人声" : "静音");
            }
            if (voice) {
                if (s_voice_run_ticks < 1000) {
                    ++s_voice_run_ticks;
                }
                // 单拍 100 ms 的按键/环境噪声不算"开口"，必须连续 300 ms 人声。
                if (s_voice_run_ticks >= kVoiceRunTicksNeeded) {
                    s_round_voice_heard = true;
                }
                s_last_voice_us = now;
            } else {
                s_voice_run_ticks = 0;
            }
            const int64_t silent_ms = (now - s_last_voice_us) / 1000;
            if (phase_ms > kMaxRecordUs / 1000) {
                ESP_LOGW(TAG, "录音达到 %d ms 上限，自动提交", static_cast<int>(phase_ms));
                FinishListeningRound(false);
            } else if (s_round_voice_heard && silent_ms >= kSilenceStopUs / 1000) {
                ESP_LOGI(TAG, "连续静音 %d ms，自动结束本轮录音", static_cast<int>(silent_ms));
                FinishListeningRound(false);
            } else if (!s_round_voice_heard && phase_ms >= kNoVoiceWaitUs / 1000) {
                ESP_LOGI(TAG, "等候 %d ms 未开口，自动收起麦克风（不提交）", static_cast<int>(phase_ms));
                FinishListeningRound(false);
            }
            // 录音期间每 500 ms 让界面顶部的"倾听"徽标呼吸一次，明确告诉用户麦克风是开的。
            if (now - s_last_pulse_us >= 500 * 1000) {
                s_last_pulse_us = now;
                NotifyUi(TIANJI_VOICE_STATE_RECORDING, "pulse");
            }
            break;
        }

        case Phase::kWaiting:
            if (phase_ms > kAnswerTimeoutUs / 1000) {
                // 服务端没回应时不弹"请重试"的失败页：安静回到可继续追问，
                // 用户按确定键即可再来一问（也避免整段会话被判为异常）。
                ESP_LOGW(TAG, "宗师 %d ms 内未回应，回到可继续追问", static_cast<int>(phase_ms));
                s_followup_pending = false;
                EnterPhase(Phase::kIdle, "宗师未应，可再问");
            }
            break;

        case Phase::kSpeaking:
            // TTS 播放由服务端 tts stop 收尾；这里只做超长兜底。
            if (phase_ms > 120 * 1000) {
                FailRound("宗师回应超时，请重试。");
            } else if (!s_gap_announced && s_last_tts_us != 0 &&
                       (now - s_last_tts_us) > 6 * 1000 * 1000) {
                // 服务端逐句合成偶尔会停几十秒，先给用户一个"仍在思索"的交代。
                s_gap_announced = true;
                ESP_LOGI(TAG, "断语句间停顿超过 6 秒，提示宗师思索中");
                NotifyUi(TIANJI_VOICE_STATE_PLAYING, "宗师思索中");
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

    // 同一页内的追问可能发生在服务端重开会话之后（断线、超时都会让服务端失忆），
    // 把本页此前的问答一起带回去，保证"不离开页面就一直有上下文"。
    size_t used = strlen(s_transcript);
    if (used > 0) {
        size_t start = (used > 900) ? used - 900 : 0;
        const char* cursor = s_transcript + start;
        if (start > 0) {
            const char* newline = strchr(cursor, '\n');
            if (newline != nullptr) {
                cursor = newline + 1;
            }
        }
        text += "【本页此前的问答记录（同一场解卦，请接着上文讲）】\n";
        text.append(cursor, strlen(cursor));
        text += "\n";
        append("若求测者本次是在追问、要求详解或复述上文，请直接接着解释，"
               "不要重新自我介绍、不要说“第一次跟我说话”或重新开场。\n");
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
    append("【作答要求】用口语化中文直接断吉凶，再点一条最要紧的可执行建议。"
           "全文不得超过 100 字，只写 2~3 个短句，每句不超过 30 字。"
           "本机边生成边播报，句子越多、句子越长，语音停顿越明显，必须短。"
           "不要念出本段格式说明，也不要输出任何英文、符号或工具名。");
    return text;
}

static void RegisterHexagramTool() {
    McpServer::GetInstance().AddTool(
        "self.tianji.get_hexagram",
        "获取本次六爻摇卦的真实结果（本卦、变卦、动爻、卦辞、象曰与三才断语），"
        "同时带回本页此前的问答记录。"
        "当用户询问占卜、解卦、问卦、求测事业/财运/姻缘/吉凶，或要求详细解释、追问、复述上文时，"
        "必须先调用本工具获取卦象与上文，再作答。收到请求后请立即调用，调用前不要输出任何寒暄、"
        "过渡语、百分号或工具名。作答必须控制在 100 字以内、2~3 个短句（本机语音逐句播报，"
        "长答案会造成长时间停顿）。严禁在未调用本工具时凭空编造卦象或复述上文。",
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

extern "C" int tianji_volume_get(void) {
    auto codec = Board::GetInstance().GetAudioCodec();
    return codec ? codec->output_volume() : -1;
}

extern "C" int tianji_volume_step(int delta) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec) return -1;
    int volume = codec->output_volume() + delta;
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    codec->SetOutputVolume(volume);
    ESP_LOGI(TAG, "音量调到 %d%%", volume);
    return volume;
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
            tianji_ui_update_battery(soc, chg);
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

void tianji_bridge_update_battery(int soc, bool charging) {
    if (bsp_lvgl_lock(100)) {
        tianji_ui_update_battery(soc, charging);
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
    if (s_discard_turn) {
        ESP_LOGI(TAG, "丢弃空轮的服务端识别结果: %s", text);
        return;
    }
    if (IsToolCallArtifact(text)) {
        ESP_LOGW(TAG, "忽略疑似工具调用的识别文本");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s_session_mutex);
        snprintf(s_user_question, sizeof(s_user_question), "%s", text);
    }
    ESP_LOGI(TAG, "求测者口述事项: %s", s_user_question);

    // 解卦页按轮累积：先记下所问，再等宗师断语逐句追加。
    if (s_transcript[0] != '\0') {
        AppendTranscript("\n\n");
    }
    AppendTranscript("问：");
    AppendTranscript(s_user_question);
    AppendTranscript("\n答：");

    // STT 已回来说明服务端正常，重置应答超时。
    if (s_phase == Phase::kWaiting || s_phase == Phase::kConnecting ||
        s_phase == Phase::kRecording) {
        s_phase = Phase::kWaiting;
        s_phase_since_us = esp_timer_get_time();
        s_voice_state = TIANJI_VOICE_STATE_UPLOADING;
        NotifyUi(TIANJI_VOICE_STATE_UPLOADING, s_transcript);
    }
}

void tianji_bridge_on_tts_start(void) {
    if (s_discard_turn) {
        // 空轮触发的服务端作答：立刻掐掉，别让宗师念一段跟噪声的对话。
        Application::GetInstance().AbortSpeaking(kAbortReasonNone);
        return;
    }
    if (s_phase == Phase::kIdle || s_phase == Phase::kError) {
        return;
    }
    EnterPhase(Phase::kSpeaking, "【宗师诵读中……】");
}

void tianji_bridge_on_tts_sentence(const char *text) {
    if (!text || text[0] == '\0') return;
    if (s_discard_turn) {
        return;
    }
    if (IsToolCallArtifact(text)) {
        ESP_LOGW(TAG, "忽略工具调用残留句子: %s", text);
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (!s_round_answered) {
        if (s_submit_us != 0) {
            ESP_LOGI(TAG, "首句断语送达 %d ms（自提交起）", static_cast<int>((now - s_submit_us) / 1000));
        }
    } else if (s_last_tts_us != 0) {
        ESP_LOGI(TAG, "断语句间隔 %d ms", static_cast<int>((now - s_last_tts_us) / 1000));
    }
    s_last_tts_us = now;
    s_gap_announced = false;
    s_round_answered = true;
    AppendTranscript(text);
    s_phase = Phase::kSpeaking;
    s_voice_state = TIANJI_VOICE_STATE_PLAYING;
    s_phase_since_us = now;
    NotifyUi(TIANJI_VOICE_STATE_PLAYING, s_transcript);
}

void tianji_bridge_on_tts_stop(void) {
    if (s_discard_turn) {
        ESP_LOGI(TAG, "空轮服务端作答结束，已整轮丢弃");
        s_discard_turn = false;
        return;
    }
    if (s_phase != Phase::kSpeaking && s_phase != Phase::kWaiting) {
        return;
    }
    if (!s_round_answered) {
        // 服务端只发了 tts start/stop（例如刚被打断）时不要弹失败页，安静收尾即可。
        ESP_LOGW(TAG, "本轮未见断语，直接收尾");
        s_followup_pending = false;
        EnterPhase(Phase::kIdle, "宗师未应，可再问");
        return;
    }
    ESP_LOGI(TAG, "宗师诵读结束，共 %u 字节", (unsigned)strlen(s_transcript));
    EnterPhase(Phase::kIdle, "断语播毕");
    if (s_session_active) {
        // 断语播完自动再开麦：用户不必按任何键即可继续追问。
        s_followup_pending = true;
        s_followup_ready_us = esp_timer_get_time() + kFollowupDelayUs;
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
    return s_transcript;
}

esp_err_t tianji_voice_start(const tianji_session_t *session, tianji_voice_callback_t cb) {
    {
        std::lock_guard<std::mutex> lock(s_session_mutex);
        if (session) {
            s_current_session = *session;
        }
    }
    s_voice_callback = cb;
    ESP_LOGI(TAG, "开启小智流式语音交互管道，准备聆听求测者问卦...");
    BeginListeningRound(true);
    return ESP_OK;
}

void tianji_voice_followup(void) {
    if (s_phase == Phase::kRecording || s_phase == Phase::kConnecting ||
        s_phase == Phase::kWaiting || s_phase == Phase::kSpeaking) {
        return;
    }
    ESP_LOGI(TAG, "求测者追问，复用同一条小智语音通道");
    BeginListeningRound(false);
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
    FinishListeningRound(true);
}

void tianji_voice_stop_playing(void) {
    if (s_phase != Phase::kSpeaking) {
        return;
    }
    s_followup_pending = false;
    Application::GetInstance().AbortSpeaking(kAbortReasonNone);
    ESP_LOGI(TAG, "用户静音阅读，保留已收到的断语文本");
    EnterPhase(Phase::kIdle, s_transcript);
}

void tianji_voice_end(void) {
    s_followup_pending = false;
    s_session_active = false;
    if (s_phase == Phase::kRecording || s_phase == Phase::kConnecting) {
        Application::GetInstance().StopListening();
    }
    if (s_phase == Phase::kSpeaking) {
        Application::GetInstance().AbortSpeaking(kAbortReasonNone);
    }
    Application::GetInstance().CloseAudioChannel();
    s_vad_active = false;
    EnterPhase(Phase::kIdle, s_transcript);
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

bool tianji_voice_is_waiting_sentence(void) {
    if (s_phase != Phase::kSpeaking || s_last_tts_us == 0) {
        return false;
    }
    return (esp_timer_get_time() - s_last_tts_us) > 6 * 1000 * 1000;
}

int tianji_voice_get_current_volume(void) {
    return Application::GetInstance().IsVoiceDetected() ? 70 : 10;
}

const char *tianji_voice_get_text_reply(void) {
    return s_transcript;
}

const char *tianji_voice_get_question(void) {
    return s_user_question;
}

const char *tianji_voice_get_audio_url(void) {
    return "";
}

} // extern "C"
