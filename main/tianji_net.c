// main/tianji_net.c — 《天机·六爻》网络连接、oMLX 大模型与语音解卦网关实现
#include "tianji_net.h"
#include "tianji_net_config.h"
#include "bsp_audio.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "tianji_net";

#define AI_RESULT_BUF_SIZE 2048
#define HTTP_RECV_BUF_SIZE 4096
#define MULTIPART_BOUNDARY "----TianjiVoiceBoundary9876543210"

static bool s_net_initialized = false;
static bool s_net_enabled = false;
static tianji_net_state_t s_net_state = TIANJI_NET_STATE_DISABLED;
static esp_netif_t *s_sta_netif = NULL;
static esp_event_handler_instance_t s_wifi_handler = NULL;
static esp_event_handler_instance_t s_ip_handler = NULL;

static char s_ai_result[AI_RESULT_BUF_SIZE] = {0};
static tianji_net_callback_t s_ai_callback = NULL;
static TaskHandle_t s_ai_task_handle = NULL;
static tianji_session_t s_ai_session_copy;

// 语音网关交互上下文
static volatile tianji_voice_state_t s_voice_state = TIANJI_VOICE_STATE_IDLE;
static tianji_voice_callback_t s_voice_callback = NULL;
static TaskHandle_t s_voice_task_handle = NULL;
static TaskHandle_t s_playback_task_handle = NULL;
static tianji_session_t s_voice_session_copy;
static volatile bool s_voice_stop_recording_flag = false;
static volatile bool s_voice_stop_playing_flag = false;
static volatile int s_voice_current_volume = 0;

static char s_voice_text_reply[AI_RESULT_BUF_SIZE] = {0};
static char s_voice_audio_url[256] = {0};

#pragma pack(push, 1)
typedef struct {
    char riff[4];              // "RIFF"
    uint32_t overall_size;     // 文件总大小 - 8
    char wave[4];              // "WAVE"
    char fmt_chunk_marker[4];  // "fmt "
    uint32_t length_of_fmt;    // 16
    uint16_t format_type;      // 1 (PCM)
    uint16_t channels;         // 1
    uint32_t sample_rate;      // 16000
    uint32_t byterate;         // 32000
    uint16_t block_align;      // 2
    uint16_t bits_per_sample;  // 16
    char data_chunk_header[4]; // "data"
    uint32_t data_size;        // pcm_bytes
} wav_header_t;
#pragma pack(pop)

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Wi-Fi STA 已启动，连接热点 [%s]...", TIANJI_WIFI_SSID);
            s_net_state = TIANJI_NET_STATE_CONNECTING;
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_net_enabled) {
                ESP_LOGW(TAG, "Wi-Fi 断开连接，尝试重新连接...");
                s_net_state = TIANJI_NET_STATE_CONNECTING;
                esp_wifi_connect();
            } else {
                s_net_state = TIANJI_NET_STATE_DISABLED;
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "已成功获取 IP: " IPSTR "，热点连接成功！", IP2STR(&event->ip_info.ip));
        s_net_state = TIANJI_NET_STATE_CONNECTED;
    }
}

esp_err_t tianji_net_init(void) {
    if (s_net_initialized) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_wifi_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_ip_handler));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    s_net_initialized = true;
    s_net_state = TIANJI_NET_STATE_DISABLED;
    ESP_LOGI(TAG, "网络模块底座初始化完成，默认处于离线待命状态");
    return ESP_OK;
}

void tianji_net_set_enabled(bool enable) {
    if (!s_net_initialized) {
        tianji_net_init();
    }

    s_net_enabled = enable;
    if (enable) {
        ESP_LOGI(TAG, "启用联机模式，启动 Wi-Fi 栈...");
        s_net_state = TIANJI_NET_STATE_CONNECTING;

        wifi_config_t wifi_cfg = {0};
        strncpy((char *)wifi_cfg.sta.ssid, TIANJI_WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
        strncpy((char *)wifi_cfg.sta.password, TIANJI_WIFI_PASS, sizeof(wifi_cfg.sta.password) - 1);

        esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
        esp_wifi_start();
    } else {
        ESP_LOGI(TAG, "切换为本地离线模式，关闭 Wi-Fi 释放系统资源...");
        esp_wifi_stop();
        s_net_state = TIANJI_NET_STATE_DISABLED;
    }
}

bool tianji_net_is_enabled(void) {
    return s_net_enabled;
}

tianji_net_state_t tianji_net_get_state(void) {
    return s_net_state;
}

const char *tianji_net_get_status_label(void) {
    switch (s_net_state) {
    case TIANJI_NET_STATE_DISABLED:
        return "脱机";
    case TIANJI_NET_STATE_CONNECTING:
        return "热点连接中";
    case TIANJI_NET_STATE_CONNECTED:
        return "热点已连接";
    case TIANJI_NET_STATE_INFERENCING:
        return "推演中";
    case TIANJI_NET_STATE_SUCCESS:
        return "已连接";
    case TIANJI_NET_STATE_FAILED:
        return "连接断开";
    case TIANJI_NET_STATE_INFER_ERROR:
        return "连接受阻";
    default:
        return "脱机";
    }
}

const char *tianji_net_get_ai_result(void) {
    return s_ai_result;
}

// ============================================================================
// 原生 oMLX (MiniCPM 5) 文本解卦（备用路径）
// ============================================================================

static void omlx_ai_task(void *pvParameters) {
    (void)pvParameters;
    s_net_state = TIANJI_NET_STATE_INFERENCING;
    if (s_ai_callback) {
        s_ai_callback(s_net_state, "正在连接 oMLX 大模型...");
    }

    const tianji_hexagram_t *prim = tianji_get_hexagram(s_ai_session_copy.primary_bits);
    const tianji_hexagram_t *trans = tianji_get_hexagram(s_ai_session_copy.transformed_bits);

    char prompt_buf[512];
    char moving_desc[64] = "六爻皆静";
    if (s_ai_session_copy.moving_count > 0) {
        int pos = 0;
        pos += snprintf(moving_desc + pos, sizeof(moving_desc) - pos, "动爻在：");
        const char *lnames[6] = {"初爻", "二爻", "三爻", "四爻", "五爻", "上爻"};
        for (int i = 0; i < 6; i++) {
            if (tianji_line_is_moving(s_ai_session_copy.lines[i])) {
                pos += snprintf(moving_desc + pos, sizeof(moving_desc) - pos, "%s ", lnames[i]);
            }
        }
    }

    snprintf(prompt_buf, sizeof(prompt_buf),
        "求卦者起得本卦为【%s】（%s上%s下），变卦为【%s】（%s上%s下），%s。"
        "卦辞为：“%s”。请作为易学宗师，结合全盘卦象与动爻深意，以通俗、精炼、深刻的白话"
        "给出全盘综合诊断与行事指南（200字以内，重点讲清当下态势与应对之策）。直接给出诊断正文。",
        prim->name, prim->upper_name, prim->lower_name,
        trans->name, trans->upper_name, trans->lower_name,
        moving_desc, prim->judgment);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", TIANJI_OMLX_MODEL);
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", "你是一位精通周易六爻的国学大师。言语古朴精炼，一针见血，直接输出卦象诊断，绝不废话，切勿做自我介绍或问候。");
    cJSON_AddItemToArray(messages, sys_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", prompt_buf);
    cJSON_AddItemToArray(messages, user_msg);

    cJSON_AddNumberToObject(root, "max_tokens", 450);
    cJSON_AddNumberToObject(root, "temperature", 0.7);

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!post_data) {
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "错误：内存不足，无法构建请求");
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t http_cfg = {
        .url = TIANJI_OMLX_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = TIANJI_OMLX_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        free(post_data);
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "错误：网络客户端创建失败");
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    char auth_header[160];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", TIANJI_OMLX_API_KEY);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err == ESP_OK) {
        int wlen = esp_http_client_write(client, post_data, strlen(post_data));
        if (wlen < 0) {
            err = ESP_FAIL;
        }
    }
    free(post_data);

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "错误：无法连通服务端\n\n建议：请确认手机热点网络畅通。");
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "oMLX 响应状态码: %d", status_code);

    if (status_code != 200) {
        esp_http_client_cleanup(client);
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "服务异常 (HTTP %d)\n\n请确认 MiniCPM5 模型已加载", status_code);
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    char *recv_buf = malloc(HTTP_RECV_BUF_SIZE);
    if (!recv_buf) {
        esp_http_client_cleanup(client);
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "内存不足：无法分配接收缓冲");
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int total_read = 0;
    while (total_read < HTTP_RECV_BUF_SIZE - 1) {
        int r = esp_http_client_read(client, recv_buf + total_read, HTTP_RECV_BUF_SIZE - 1 - total_read);
        if (r <= 0) break;
        total_read += r;
    }
    recv_buf[total_read] = '\0';
    esp_http_client_cleanup(client);

    cJSON *resp_json = cJSON_Parse(recv_buf);
    free(recv_buf);

    if (resp_json) {
        cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
        if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
            cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
            cJSON *message = cJSON_GetObjectItem(first_choice, "message");
            if (message) {
                cJSON *content = cJSON_GetObjectItem(message, "content");
                if (cJSON_IsString(content) && content->valuestring) {
                    strncpy(s_ai_result, content->valuestring, sizeof(s_ai_result) - 1);
                    s_ai_result[sizeof(s_ai_result) - 1] = '\0';
                    s_net_state = TIANJI_NET_STATE_SUCCESS;
                }
            }
        }
        cJSON_Delete(resp_json);
    }

    if (s_net_state != TIANJI_NET_STATE_SUCCESS) {
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "解析大模型响应失败");
    }

    if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
    s_ai_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t tianji_net_request_ai(const tianji_session_t *session, tianji_net_callback_t cb) {
    if (s_ai_task_handle != NULL) return ESP_ERR_INVALID_STATE;
    s_ai_session_copy = *session;
    s_ai_callback = cb;

    BaseType_t ret = xTaskCreate(omlx_ai_task, "tianji_omlx", 6144, NULL, 5, &s_ai_task_handle);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

// ============================================================================
// 语音解卦网关实现 (SenseVoice + MiniCPM5 + EdgeTTS)
// ============================================================================

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
    return s_voice_current_volume;
}

const char *tianji_voice_get_text_reply(void) {
    return s_voice_text_reply;
}

const char *tianji_voice_get_audio_url(void) {
    return s_voice_audio_url;
}

void tianji_voice_stop_recording(void) {
    s_voice_stop_recording_flag = true;
}

void tianji_voice_stop_playing(void) {
    s_voice_stop_playing_flag = true;
}

// 发送 HTTP Chunk 工具函数
static esp_err_t send_http_chunk(esp_http_client_handle_t client, const void *data, size_t len) {
    char chunk_hdr[16];
    int hlen = snprintf(chunk_hdr, sizeof(chunk_hdr), "%X\r\n", (unsigned int)len);
    if (esp_http_client_write(client, chunk_hdr, hlen) < 0) return ESP_FAIL;
    if (len > 0 && esp_http_client_write(client, (const char *)data, len) < 0) return ESP_FAIL;
    if (esp_http_client_write(client, "\r\n", 2) < 0) return ESP_FAIL;
    return ESP_OK;
}

// 宗师语音流式拉取与扬声器播放后台任务
static void voice_playback_task(void *pvParameters) {
    (void)pvParameters;
    s_voice_state = TIANJI_VOICE_STATE_PLAYING;
    s_voice_stop_playing_flag = false;
    ESP_LOGI(TAG, "启动宗师语音拉取与流式播放任务: %s", s_voice_audio_url);

    char full_audio_url[384];
    if (strncmp(s_voice_audio_url, "http://", 7) == 0 || strncmp(s_voice_audio_url, "https://", 8) == 0) {
        strncpy(full_audio_url, s_voice_audio_url, sizeof(full_audio_url) - 1);
    } else {
        snprintf(full_audio_url, sizeof(full_audio_url), "%s%s", TIANJI_GATEWAY_AUDIO_BASE, s_voice_audio_url);
    }

    esp_http_client_config_t play_cfg = {
        .url = full_audio_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t p_client = esp_http_client_init(&play_cfg);
    if (!p_client) {
        ESP_LOGE(TAG, "创建音频下载客户端失败");
        s_voice_state = TIANJI_VOICE_STATE_IDLE;
        s_playback_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(p_client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "主域名音频拉取失败 (%s)，尝试回退至局域网直连拉取...", esp_err_to_name(err));
        esp_http_client_cleanup(p_client);

        snprintf(full_audio_url, sizeof(full_audio_url), "%s%s", TIANJI_GATEWAY_LAN_AUDIO_BASE, s_voice_audio_url);
        play_cfg.url = full_audio_url;
        play_cfg.crt_bundle_attach = NULL;
        p_client = esp_http_client_init(&play_cfg);
        if (p_client) {
            err = esp_http_client_open(p_client, 0);
        }
    }

    if (err != ESP_OK || !p_client) {
        ESP_LOGE(TAG, "最终无法连接音频下载服务: %s", esp_err_to_name(err));
        if (p_client) esp_http_client_cleanup(p_client);
        s_voice_state = TIANJI_VOICE_STATE_IDLE;
        s_playback_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_fetch_headers(p_client);
    int status_code = esp_http_client_get_status_code(p_client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "音频下载服务响应状态异常: HTTP %d", status_code);
        esp_http_client_cleanup(p_client);
        s_voice_state = TIANJI_VOICE_STATE_IDLE;
        s_playback_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // 设置扬声器输出格式为 16kHz 16-bit 单声道
    bsp_audio_set_format(16000, 16, 1);
    bsp_audio_set_volume(90);

    // 1. 读取并跳过 44 字节 WAV 头
    char header_buf[44];
    int head_read = 0;
    while (head_read < 44 && !s_voice_stop_playing_flag) {
        int r = esp_http_client_read(p_client, header_buf + head_read, 44 - head_read);
        if (r <= 0) break;
        head_read += r;
    }

    // 2. 循环读取流式 PCM 音频写入扬声器
    uint8_t *pcm_play_buf = malloc(1024);
    if (pcm_play_buf) {
        while (!s_voice_stop_playing_flag) {
            int r = esp_http_client_read(p_client, (char *)pcm_play_buf, 1024);
            if (r <= 0) {
                break; // 流式传输结束
            }
            bsp_audio_write(pcm_play_buf, (size_t)r);
        }
        free(pcm_play_buf);
    }

    esp_http_client_cleanup(p_client);
    s_voice_state = TIANJI_VOICE_STATE_IDLE;
    s_playback_task_handle = NULL;
    ESP_LOGI(TAG, "宗师语音播放完成或已打断停止");
    vTaskDelete(NULL);
}

// 语音交互核心任务：录音采集 -> Chunked 上传 -> 解析响应 -> 触发播放
static void voice_interaction_task(void *pvParameters) {
    (void)pvParameters;

    s_voice_state = TIANJI_VOICE_STATE_RECORDING;
    s_voice_stop_recording_flag = false;
    s_voice_current_volume = 0;

    if (s_voice_callback) {
        s_voice_callback(s_voice_state, "【天机倾听中...】");
    }

    const tianji_hexagram_t *prim = tianji_get_hexagram(s_voice_session_copy.primary_bits);
    const tianji_hexagram_t *trans = tianji_get_hexagram(s_voice_session_copy.transformed_bits);

    char moving_desc[64] = "六爻皆静";
    if (s_voice_session_copy.moving_count > 0) {
        int pos = 0;
        pos += snprintf(moving_desc + pos, sizeof(moving_desc) - pos, "动爻在：");
        const char *lnames[6] = {"初九动", "六二动", "九三动", "六四动", "九五动", "上九动"};
        for (int i = 0; i < 6; i++) {
            if (tianji_line_is_moving(s_voice_session_copy.lines[i])) {
                pos += snprintf(moving_desc + pos, sizeof(moving_desc) - pos, "%s ", lnames[i]);
            }
        }
    }

    // 1. 初始化 HTTP 客户端并开启 Chunked 流式 POST
    // 优先尝试主网关地址，若失败自动回退尝试局域网直连地址
    const char *target_url = TIANJI_GATEWAY_URL;
    esp_http_client_config_t v_cfg = {
        .url = target_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = TIANJI_GATEWAY_TIMEOUT_MS,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&v_cfg);
    if (!client) {
        ESP_LOGE(TAG, "创建网关 HTTP 客户端失败");
        s_voice_state = TIANJI_VOICE_STATE_ERROR;
        snprintf(s_voice_text_reply, sizeof(s_voice_text_reply), "网络客户端创建失败，请稍后再试。");
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);
        s_voice_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=" MULTIPART_BOUNDARY);
    esp_http_client_set_header(client, "Transfer-Encoding", "chunked");

    esp_err_t err = esp_http_client_open(client, -1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "主网关连接失败 (%s)，尝试回退至局域网直连地址: %s", esp_err_to_name(err), TIANJI_GATEWAY_LAN_URL);
        esp_http_client_cleanup(client);

        target_url = TIANJI_GATEWAY_LAN_URL;
        v_cfg.url = target_url;
        v_cfg.crt_bundle_attach = NULL; // 局域网 http 不需要证书校验
        client = esp_http_client_init(&v_cfg);
        if (client) {
            esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=" MULTIPART_BOUNDARY);
            esp_http_client_set_header(client, "Transfer-Encoding", "chunked");
            err = esp_http_client_open(client, -1);
        }
    }

    if (err != ESP_OK || !client) {
        ESP_LOGE(TAG, "打开网关 HTTP 连接最终失败: %s", esp_err_to_name(err));
        if (client) esp_http_client_cleanup(client);
        s_voice_state = TIANJI_VOICE_STATE_ERROR;
        snprintf(s_voice_text_reply, sizeof(s_voice_text_reply), "无法连通语音网关，请确认手机热点已连接。");
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);
        s_voice_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // 2. 发送 multipart/form-data 前置字段与 audio_file 头部
    char meta_part[768];
    int meta_len = snprintf(meta_part, sizeof(meta_part),
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"primary_hex\"\r\n\r\n%s\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"transformed_hex\"\r\n\r\n%s\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"moving_desc\"\r\n\r\n%s\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"judgment\"\r\n\r\n%s\r\n"
        "--" MULTIPART_BOUNDARY "\r\n"
        "Content-Disposition: form-data; name=\"audio_file\"; filename=\"user_voice.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n",
        prim->name, trans->name, moving_desc, prim->judgment
    );

    send_http_chunk(client, meta_part, meta_len);

    // 发送标准 44 字节 WAV 头（声明 16kHz 16-bit 单声道 PCM）
    wav_header_t wav_hdr = {
        .riff = {'R', 'I', 'F', 'F'},
        .overall_size = 0x7FFFFFFF, // chunked 流式传输使用最大占位
        .wave = {'W', 'A', 'V', 'E'},
        .fmt_chunk_marker = {'f', 'm', 't', ' '},
        .length_of_fmt = 16,
        .format_type = 1,
        .channels = 1,
        .sample_rate = 16000,
        .byterate = 32000,
        .block_align = 2,
        .bits_per_sample = 16,
        .data_chunk_header = {'d', 'a', 't', 'a'},
        .data_size = 0x7FFFFFD0
    };
    send_http_chunk(client, &wav_hdr, sizeof(wav_hdr));

    // 3. 实时麦克风录音并流式上传
    bsp_audio_set_format(16000, 16, 1);
    uint8_t *rec_buf = malloc(1024);
    if (!rec_buf) {
        esp_http_client_cleanup(client);
        s_voice_state = TIANJI_VOICE_STATE_ERROR;
        snprintf(s_voice_text_reply, sizeof(s_voice_text_reply), "内存不足：无法分配音频采集缓冲区");
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);
        s_voice_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    uint32_t total_chunks = 0;
    uint32_t silence_chunks = 0;
    bool has_spoken = false;
    const uint32_t MAX_CHUNKS = 250;       // 8.0 秒上限 (250 * 32ms)
    const uint32_t INITIAL_SILENCE_LIMIT = 156; // 开始录音后5.0秒未说话自动截止
    const uint32_t TAIL_SILENCE_LIMIT = 110;    // 说话后停顿3.5秒自动截止
    const int VOICE_ACTIVE_RMS = 500;           // 语音激活RMS阈值

    ESP_LOGI(TAG, "麦克风采集已开启，请对准设备陈述求测之事...");

    while (!s_voice_stop_recording_flag && total_chunks < MAX_CHUNKS) {
        esp_err_t r_err = bsp_audio_read(rec_buf, 1024);
        if (r_err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 计算去直流偏置的真实交流 RMS 能量
        int16_t *samples = (int16_t *)rec_buf;
        int64_t mean_sum = 0;
        for (int i = 0; i < 512; i++) {
            mean_sum += samples[i];
        }
        int16_t mean_val = (int16_t)(mean_sum / 512);

        int64_t ac_sum_sq = 0;
        for (int i = 0; i < 512; i++) {
            int32_t diff = samples[i] - mean_val;
            ac_sum_sq += diff * diff;
        }
        int rms = (int)sqrt((double)ac_sum_sq / 512.0);
        int vol = (rms > 6000) ? 100 : (rms * 100 / 6000);
        s_voice_current_volume = vol;

        // 语音活动与静音计数判定
        if (rms >= VOICE_ACTIVE_RMS) {
            has_spoken = true;
            silence_chunks = 0;
        } else {
            silence_chunks++;
        }

        // 边录边发一个 chunk
        if (send_http_chunk(client, rec_buf, 1024) != ESP_OK) {
            ESP_LOGW(TAG, "发送音频 Chunk 失败，提前终止录音");
            break;
        }
        total_chunks++;

        // 1. 若 5 秒整未检测到有效说话，自动截止
        if (!has_spoken && total_chunks >= INITIAL_SILENCE_LIMIT) {
            ESP_LOGI(TAG, "连续 5 秒未检测到说话声音，自动结束录音");
            break;
        }
        // 2. 若开口说话后尾随停顿达 3.5 秒，自动截止
        if (has_spoken && silence_chunks >= TAIL_SILENCE_LIMIT) {
            ESP_LOGI(TAG, "说话后停顿超过 3.5 秒，自动结束录音");
            break;
        }
    }

    free(rec_buf);
    s_voice_current_volume = 0;

    ESP_LOGI(TAG, "录音结束，共采集发送 %lu 块 (约 %.2f 秒)", (unsigned long)total_chunks, total_chunks * 0.032f);

    // 4. 发送 multipart 尾部及 HTTP 最终终止 chunk (0\r\n\r\n)
    char tail_part[128];
    int tail_len = snprintf(tail_part, sizeof(tail_part), "\r\n--" MULTIPART_BOUNDARY "--\r\n");
    send_http_chunk(client, tail_part, tail_len);
    esp_http_client_write(client, "0\r\n\r\n", 5);

    // 5. 切换到【神思审度中...】等待网关处理
    s_voice_state = TIANJI_VOICE_STATE_UPLOADING;
    if (s_voice_callback) {
        s_voice_callback(s_voice_state, "【神思审度中...】");
    }

    // 6. 接收并解析服务端 JSON 响应
    esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "网关响应 HTTP 状态码: %d", status_code);

    if (status_code != 200) {
        esp_http_client_cleanup(client);
        s_voice_state = TIANJI_VOICE_STATE_ERROR;
        snprintf(s_voice_text_reply, sizeof(s_voice_text_reply), "网关服务异常 (HTTP %d)，请稍后重试。", status_code);
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);
        s_voice_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    char *resp_buf = malloc(HTTP_RECV_BUF_SIZE);
    if (!resp_buf) {
        esp_http_client_cleanup(client);
        s_voice_state = TIANJI_VOICE_STATE_ERROR;
        snprintf(s_voice_text_reply, sizeof(s_voice_text_reply), "内存不足：无法解析网关响应");
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);
        s_voice_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int total_read = 0;
    while (total_read < HTTP_RECV_BUF_SIZE - 1) {
        int r = esp_http_client_read(client, resp_buf + total_read, HTTP_RECV_BUF_SIZE - 1 - total_read);
        if (r <= 0) break;
        total_read += r;
    }
    resp_buf[total_read] = '\0';
    esp_http_client_cleanup(client);

    // 7. 解析 JSON 结构
    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);

    if (!root) {
        s_voice_state = TIANJI_VOICE_STATE_ERROR;
        snprintf(s_voice_text_reply, sizeof(s_voice_text_reply), "解析网关返回结果异常");
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);
        s_voice_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    cJSON *status_item = cJSON_GetObjectItem(root, "status");
    cJSON *reply_item = cJSON_GetObjectItem(root, "text_reply");
    cJSON *audio_item = cJSON_GetObjectItem(root, "audio_url");

    if (reply_item && reply_item->valuestring) {
        strncpy(s_voice_text_reply, reply_item->valuestring, sizeof(s_voice_text_reply) - 1);
        s_voice_text_reply[sizeof(s_voice_text_reply) - 1] = '\0';
    }
    if (audio_item && audio_item->valuestring) {
        strncpy(s_voice_audio_url, audio_item->valuestring, sizeof(s_voice_audio_url) - 1);
        s_voice_audio_url[sizeof(s_voice_audio_url) - 1] = '\0';
    }

    bool is_success = (status_item && status_item->valuestring && strcmp(status_item->valuestring, "success") == 0);
    bool is_need_more = (status_item && status_item->valuestring && strcmp(status_item->valuestring, "need_more_info") == 0);
    cJSON_Delete(root);

    if (is_need_more) {
        s_voice_state = TIANJI_VOICE_STATE_NEED_MORE;
        ESP_LOGI(TAG, "网关判定用户意图不足，进入意念未明引导");
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);

        // 如果服务端生成了引导语音频，启动后台播放任务朗读提示语
        if (strlen(s_voice_audio_url) > 0) {
            xTaskCreate(voice_playback_task, "voice_play", 4096, NULL, 4, &s_playback_task_handle);
        }
    } else if (is_success) {
        s_voice_state = TIANJI_VOICE_STATE_SUCCESS;
        ESP_LOGI(TAG, "网关解卦成功，开始宗师批断呈现与语音播放");
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);

        // 启动后台任务流式播放宗师音频
        if (strlen(s_voice_audio_url) > 0) {
            xTaskCreate(voice_playback_task, "voice_play", 4096, NULL, 4, &s_playback_task_handle);
        }
    } else {
        s_voice_state = TIANJI_VOICE_STATE_ERROR;
        if (s_voice_callback) s_voice_callback(s_voice_state, s_voice_text_reply);
    }

    s_voice_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t tianji_voice_start(const tianji_session_t *session, tianji_voice_callback_t cb) {
    if (s_voice_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE; // 已有语音会话在运行
    }
    // 停止正在播放的语音
    tianji_voice_stop_playing();

    s_voice_session_copy = *session;
    s_voice_callback = cb;

    BaseType_t ret = xTaskCreate(voice_interaction_task, "tianji_voice", 6144, NULL, 5, &s_voice_task_handle);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
