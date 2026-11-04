#include "lite_audio_engine.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wn_models.h>

#include <cmath>

#include "wake_words/esp_wake_word.h"

#define TAG "LiteAudioEngine"

LiteAudioEngine::LiteAudioEngine() = default;
LiteAudioEngine::~LiteAudioEngine() = default;

bool LiteAudioEngine::Initialize(AudioCodec* codec, int frame_duration_ms, srmodel_list_t* models_list) {
    codec_ = codec;
    models_list_ = models_list;
    frame_samples_ = frame_duration_ms * 16000 / 1000;
    output_buffer_.reserve(frame_samples_);

    should_have_wake_word_ = models_list != nullptr &&
        esp_srmodel_filter(models_list, ESP_WN_PREFIX, nullptr) != nullptr;
#if CONFIG_USE_ESP_WAKE_WORD
    should_have_wake_word_ = should_have_wake_word_ || models_list == nullptr;
#endif
    {
        std::lock_guard<std::mutex> lock(wake_word_mutex_);
        if (!CreateWakeWordLocked()) {
            return false;
        }
    }

    ESP_LOGI(TAG, "Initialized, WakeNet: %s", wake_word_ ? "yes" : "no");
    return true;
}

void LiteAudioEngine::Feed(std::vector<int16_t>&& data) {
    {
        std::lock_guard<std::mutex> lock(wake_word_mutex_);
        if (wake_word_enabled_ && wake_word_) {
            wake_word_->Feed(data);
        }
    }
    if (voice_processing_enabled_) {
        UpdateVoiceActivity(data);
        OutputRawAudio(data);
    }
}

void LiteAudioEngine::UpdateVoiceActivity(const std::vector<int16_t>& data) {
    if (data.empty() || !vad_state_change_callback_) {
        return;
    }

    int64_t sum = 0;
    int64_t sum_sq = 0;
    for (int16_t sample : data) {
        const int32_t value = sample;
        sum += value;
        sum_sq += static_cast<int64_t>(value) * value;
    }
    const int64_t count = static_cast<int64_t>(data.size());
    const int64_t mean = sum / count;
    int64_t variance = sum_sq / count - mean * mean;
    if (variance < 0) {
        variance = 0;
    }

    const int64_t now = esp_timer_get_time();
    // 约 -52 dBFS 的人声门限；静音时保持 600 ms 迟滞，避免字间停顿被切成多段。
    constexpr int64_t kSpeechVarianceThreshold = 250LL * 250LL;
    constexpr int64_t kHangoverUs = 600 * 1000;

    if (variance > kSpeechVarianceThreshold) {
        vad_last_voice_us_ = now;
        if (!vad_speaking_) {
            vad_speaking_ = true;
            ESP_LOGI(TAG, "VAD speech (rms=%d)",
                     static_cast<int>(std::sqrt(static_cast<double>(variance))));
            vad_state_change_callback_(true);
        }
    } else if (vad_speaking_ && (now - vad_last_voice_us_) > kHangoverUs) {
        vad_speaking_ = false;
        ESP_LOGI(TAG, "VAD silence");
        vad_state_change_callback_(false);
    }

    // 录音期每 2 秒输出一次能量，便于现场校准门限。
    if (now - vad_last_log_us_ > 2 * 1000 * 1000) {
        vad_last_log_us_ = now;
        ESP_LOGI(TAG, "mic rms=%d speaking=%d",
                 static_cast<int>(std::sqrt(static_cast<double>(variance))),
                 vad_speaking_ ? 1 : 0);
    }
}

void LiteAudioEngine::EnableWakeWordDetection(bool enable) {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    if (!wake_word_) {
        wake_word_enabled_ = false;
        return;
    }

    wake_word_enabled_ = enable;
    if (enable) {
        wake_word_->Start();
    } else {
        wake_word_->Stop();
    }
}

void LiteAudioEngine::EnableVoiceProcessing(bool enable) {
    voice_processing_enabled_ = enable;
    if (!enable) {
        std::lock_guard<std::mutex> lock(output_mutex_);
        output_buffer_.clear();
    }
    if (!enable && vad_speaking_) {
        vad_speaking_ = false;
        if (vad_state_change_callback_) {
            vad_state_change_callback_(false);
        }
    }
}

void LiteAudioEngine::EnableDeviceAec(bool enable) {
    if (enable) {
        ESP_LOGW(TAG, "Device AEC is not supported by the lite engine");
    }
}

bool LiteAudioEngine::HasWakeWord() const {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    return wake_word_ != nullptr;
}

std::vector<std::string> LiteAudioEngine::GetWakeWords() const {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    return wake_word_ ? wake_word_->GetWakeWords() : std::vector<std::string>{};
}

bool LiteAudioEngine::IsWakeWordDetectionEnabled() const {
    return wake_word_enabled_;
}

bool LiteAudioEngine::IsVoiceProcessingEnabled() const {
    return voice_processing_enabled_;
}

size_t LiteAudioEngine::GetFeedSize() const {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    if (wake_word_) {
        return wake_word_->GetFeedSize();
    }
    return frame_samples_;
}

void LiteAudioEngine::OnWakeWordDetected(std::function<void(const std::string& wake_word)> callback) {
    wake_word_detected_callback_ = std::move(callback);
}

void LiteAudioEngine::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = std::move(callback);
}

void LiteAudioEngine::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = std::move(callback);
}

void LiteAudioEngine::EncodeWakeWordData() {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    if (wake_word_) {
        wake_word_->EncodeWakeWordData();
    }
}

bool LiteAudioEngine::GetWakeWordOpus(std::vector<uint8_t>& opus) {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    return wake_word_ && wake_word_->GetWakeWordOpus(opus);
}

const std::string& LiteAudioEngine::GetLastDetectedWakeWord() const {
    return wake_word_ ? wake_word_->GetLastDetectedWakeWord() : empty_wake_word_;
}

void LiteAudioEngine::ReleaseWakeWordResources() {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    wake_word_enabled_ = false;
    if (wake_word_) {
        wake_word_->Stop();
        wake_word_.reset();
        ESP_LOGI(TAG, "Released standalone WakeNet resources");
    }
}

bool LiteAudioEngine::RestoreWakeWordResources() {
    std::lock_guard<std::mutex> lock(wake_word_mutex_);
    return CreateWakeWordLocked();
}

bool LiteAudioEngine::CreateWakeWordLocked() {
    if (!should_have_wake_word_) {
        return true;
    }
    if (wake_word_) {
        return true;
    }

    auto wake_word = std::make_unique<EspWakeWord>();
    wake_word->OnWakeWordDetected([this](const std::string& detected_wake_word) {
        wake_word_enabled_ = false;
        if (wake_word_detected_callback_) {
            wake_word_detected_callback_(detected_wake_word);
        }
    });
    if (!wake_word->Initialize(codec_, models_list_)) {
        ESP_LOGE(TAG, "Failed to initialize standalone WakeNet");
        return false;
    }
    wake_word_ = std::move(wake_word);
    return true;
}

void LiteAudioEngine::OutputRawAudio(const std::vector<int16_t>& data) {
    if (!output_callback_ || codec_ == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(output_mutex_);
    const size_t channels = codec_->input_channels();
    if (channels <= 1) {
        output_buffer_.insert(output_buffer_.end(), data.begin(), data.end());
    } else {
        for (size_t i = 0; i < data.size(); i += channels) {
            output_buffer_.push_back(data[i]);
        }
    }

    while (output_buffer_.size() >= static_cast<size_t>(frame_samples_)) {
        if (output_buffer_.size() == static_cast<size_t>(frame_samples_)) {
            output_callback_(std::move(output_buffer_));
            output_buffer_.clear();
            output_buffer_.reserve(frame_samples_);
        } else {
            output_callback_(std::vector<int16_t>(
                output_buffer_.begin(), output_buffer_.begin() + frame_samples_));
            output_buffer_.erase(output_buffer_.begin(), output_buffer_.begin() + frame_samples_);
        }
    }
}
