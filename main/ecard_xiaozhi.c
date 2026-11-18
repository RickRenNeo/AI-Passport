#include "ecard_xiaozhi.h"
#include "ecard_netlink.h"
#include "ecard_opus_arena.h"
#include "ecard_vad.h"
#include "ecard_voice.h"
#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_websocket_client.h>
#include <esp_chip_info.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_audio_dec.h>
#include <esp_audio_dec_default.h>
#include <esp_audio_enc.h>
#include <esp_audio_enc_default.h>
#include <esp_opus_dec.h>
#include <esp_opus_enc.h>
#include <freertos/semphr.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <errno.h>
#include <esp_transport.h>
#include <esp_transport_ssl.h>
#include <esp_transport_tcp.h>
#include <esp_transport_ws.h>
#include <lwip/sockets.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ecard_xiaozhi"
#define NVS_NAMESPACE "xiaozhi"
#define RESPONSE_MAX 2048
#define OTA_BODY_MAX 768
#define OTA_TASK_STACK 8192
#define WS_BUFFER_SIZE 1024
#define WS_TASK_STACK 6144
#define TEXT_MAX 192

static ecard_vad_t s_vad;

static bool s_started;
static bool s_ready;
static char s_ws_url[160];
static char s_token[96];
static char s_activation[32];
/* Client-Id 必须是 UUID；服务端对 MAC 形式的 Client-Id 直接返回 400 Invalid client ID。 */
static char s_client_id[40];

/* WebSocket 传输层状态：连接、会话与最近一次服务端事件，供后续 UI 读取。 */
static esp_websocket_client_handle_t s_ws;
static esp_transport_handle_t s_ws_base;
static esp_transport_handle_t s_ws_trans;
static bool s_ws_started;
static bool s_ota_running;
static bool s_ws_connected;
static volatile bool s_force_ota;
static volatile bool s_connect_failed;
static volatile bool s_ota_abort;
static volatile bool s_retried_ota_this_session;
static volatile bool s_listen_pending;
static volatile int64_t s_listen_pending_until_us;
static uint32_t s_ws_tx_drop_count;
static int64_t s_ws_tx_drop_log_us;
/* 客户端任务已经 FINISH（服务端关闭且未自动重连）：必须整会话重建，
 * 否则 session_start() 会一直认为“会话已在进行中”，学生永远看到“连接中”。 */
static volatile bool s_ws_dead;
static volatile bool s_ws_stopping;
static char s_pending_prompt[128];
static bool s_pending_prompt_valid;
static char s_prompt_fallback[96];
static bool s_prompt_fallback_valid;
static uint32_t s_prompt_seq;
static char s_session_id[64];
static char s_last_stt[TEXT_MAX];
static char s_last_llm[TEXT_MAX];
static char s_last_event[32];
static char s_last_tts_state[32];
static int s_tts_audio_bytes;
static int s_pcm_decoded;
/* 诊断计数：服务端音频是否真的被读取/解码，用来区分“服务器断流”和
 * “设备自己卡住不读”。只在 AI 会话期间每 5 秒打印一次。 */
static volatile uint32_t s_rx_audio_frames;
static volatile uint32_t s_rx_audio_bytes;
static volatile uint32_t s_audio_queue_drops;
static volatile uint32_t s_pcm_overflows;
static volatile uint32_t s_decoded_frames;
static volatile int64_t s_last_rx_us;
/* 最近一次提问与重播次数：服务端 RST 断流后自动重问一次，
 * 否则学生只会听到“两句话就没了”，必须手动再按一次卡。 */
static char s_last_prompt[128];
static char s_last_prompt_fallback[96];
static uint8_t s_replay_count;
/* 本轮回答结束后要自动追问的短句（单词卡例句）。任何新提问都会覆盖它。 */
static char s_follow_prompt[96];
static bool s_follow_valid;

/* 控制消息发送队列：UI 线程只入队，绝不直接在 LVGL 锁内写 socket。
 * 之前 socket 半死时 send_text 会把按键线程卡住 10 秒，队列满后按键全丢。 */
#define XZ_TX_ITEM_MAX 256
#define XZ_TX_QUEUE_LEN 6

typedef enum {
    XZ_TX_OP_TEXT = 0,
    XZ_TX_OP_ABORT_PLAYBACK,
    XZ_TX_OP_LISTEN_START,
    XZ_TX_OP_LISTEN_STOP,
    XZ_TX_OP_LISTEN_CANCEL,
} xz_tx_op_t;

typedef struct {
    uint8_t op;
    uint16_t len;
    uint32_t seq;
    char data[XZ_TX_ITEM_MAX];
} xz_tx_item_t;

#define XZ_TX_STACK_BYTES 3584

static QueueHandle_t s_tx_q;
static TaskHandle_t s_tx_task;
/* 队列项放静态区：3.5 KB 任务栈里再放 384 字节大结构只剩少量余量。 */
static xz_tx_item_t s_tx_item;

typedef enum {
    XZ_AUDIO_DECODE = 0,
    XZ_AUDIO_ENCODE = 1,
} xz_audio_mode_t;

static volatile xz_audio_mode_t s_audio_mode = XZ_AUDIO_DECODE;
static volatile xz_audio_mode_t s_audio_req_mode = XZ_AUDIO_DECODE;

static bool ensure_audio_worker(void);
static void release_audio_worker(void);
static bool switch_audio_mode_timeout(xz_audio_mode_t target, uint32_t wait_ms);
static bool switch_audio_mode(xz_audio_mode_t target);
static void arm_replay_after_disconnect(bool interrupted);
static SemaphoreHandle_t s_state_lock;
static volatile bool s_tx_stop;
static volatile bool s_tx_done;
static TickType_t s_prompt_retry_at;
static volatile uint32_t s_listen_seq;
static volatile bool s_abort_queued;
/* 已发送 detect 或 listen:stop、正在等/播本轮服务端 TTS：
 * 切题 abort 时即使 tts:start 还没到也要发 abort，并丢弃未发出新请求前的迟到 tts:start。 */
static volatile bool s_awaiting_tts;
/* 每次切题/打断递增代号，防止解码线程把刚被 abort 的旧帧重新写进 PCM 缓冲。 */
static volatile uint32_t s_tts_gen;



/** 生成并持久化一次 UUID v4 作为 Client-Id。 */
static void ensure_client_id(void) {
    nvs_handle_t handle;
    size_t len = sizeof(s_client_id);
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        const esp_err_t id_err = nvs_get_str(handle, "client_id", s_client_id, &len);
        if (id_err == ESP_OK && strlen(s_client_id) >= 32) {
            nvs_close(handle);
            return;
        }
        ESP_LOGW(TAG, "读取 Client-Id 失败: %s", esp_err_to_name(id_err));
        nvs_close(handle);
    }

    uint8_t bytes[16];
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] = (uint8_t)(esp_random() & 0xFF);
    }
    bytes[6] = (uint8_t)((bytes[6] & 0x0F) | 0x40); // version 4
    bytes[8] = (uint8_t)((bytes[8] & 0x3F) | 0x80); // RFC 4122 variant
    snprintf(s_client_id, sizeof(s_client_id),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14],
             bytes[15]);

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        (void)nvs_set_str(handle, "client_id", s_client_id);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "生成 Client-Id: %s", s_client_id);
}

static void load_settings(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    size_t len = sizeof(s_ws_url);
    if (nvs_get_str(handle, "ws_url", s_ws_url, &len) != ESP_OK) s_ws_url[0] = '\0';
    len = sizeof(s_token);
    if (nvs_get_str(handle, "token", s_token, &len) != ESP_OK) s_token[0] = '\0';
    nvs_close(handle);
    ensure_client_id();
    s_ready = (s_ws_url[0] != '\0' && s_token[0] != '\0');
}

static void store_settings(void) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (s_ws_url[0]) (void)nvs_set_str(handle, "ws_url", s_ws_url);
    if (s_token[0]) (void)nvs_set_str(handle, "token", s_token);
    (void)nvs_commit(handle);
    nvs_close(handle);
}

static void device_mac(char *out, size_t size) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void build_body(char *out, size_t size) {
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    char mac[24];
    device_mac(mac, sizeof(mac));

    const char *wifi_ssid = ecard_netlink_ssid();
    if (!wifi_ssid || !wifi_ssid[0]) wifi_ssid = "";
    char wifi_ip[16] = "";
    wifi_ap_record_t ap;
    int wifi_rssi = 0;
    int wifi_channel = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        wifi_rssi = ap.rssi;
        wifi_channel = ap.primary;
    }
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK) {
        snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&ip_info.ip));
    }

    // 官方 ota.cc 的字段集合：缺少语言/传输能力/板卡运行时信息时服务端会返回 400。
    snprintf(out, size,
             "{\"version\":2,\"language\":\"zh-CN\","
             "\"flash_size\":%u,\"minimum_free_heap_size\":%u,"
             "\"mac_address\":\"%s\",\"uuid\":\"%s\","
             "\"chip_model_name\":\"esp32c3\","
             "\"chip_info\":{\"model\":%d,\"cores\":%d,\"revision\":%d,\"features\":%d},"
             "\"application\":{\"name\":\"%s\",\"version\":\"%s\",\"idf_version\":\"%s\"},"
             "\"board\":{\"type\":\"ai-passport\",\"name\":\"ai-passport\","
             "\"manufacturer\":\"folotoy\",\"ssid\":\"%s\",\"rssi\":%d,"
             "\"channel\":%d,\"ip\":\"%s\",\"mac\":\"%s\"},"
             "\"has_mqtt\":false,\"has_websocket\":true}",
             (unsigned)(8u * 1024u * 1024u), (unsigned)esp_get_minimum_free_heap_size(),
             mac, s_client_id, chip.model, chip.cores, chip.revision, (int)chip.features,
             app ? app->project_name : "ai-passport",
             app ? app->version : "0.0.0",
             app ? app->idf_ver : "unknown",
             wifi_ssid, wifi_rssi, wifi_channel, wifi_ip, mac);
}

static int http_post(const char *body, char *response, size_t size) {
    esp_http_client_config_t cfg = {
        .url = ECARD_XIAOZHI_OTA_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        // 必须挂证书包，否则 HTTPS 握手失败，perform 只会返回 -1。
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;

    char mac[24];
    device_mac(mac, sizeof(mac));
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Device-Id", mac);
    esp_http_client_set_header(client, "Client-Id", s_client_id);
    esp_http_client_set_header(client, "Activation-Version", "1");
    // 手动 open/write/fetch/read：perform + read_response 在这里会拿到 0 字节，
    // 因为响应体已被内部消费；手动读取能稳定拿到 OTA 的 JSON。
    const size_t body_len = strlen(body);
    if (esp_http_client_open(client, (int)body_len) != ESP_OK) {
        ESP_LOGW(TAG, "OTA 打开连接失败");
        esp_http_client_cleanup(client);
        return -1;
    }
    if (esp_http_client_write(client, body, (int)body_len) != (int)body_len) {
        ESP_LOGW(TAG, "OTA 发送请求体失败");
        esp_http_client_cleanup(client);
        return -1;
    }
    (void)esp_http_client_fetch_headers(client);

    int status = esp_http_client_get_status_code(client);
    int total = 0;
    int read_len = 0;
    while (total < (int)size - 1 &&
           (read_len = esp_http_client_read(client, response + total,
                                           (int)size - 1 - total)) > 0) {
        total += read_len;
    }
    response[total] = '\0';
    ESP_LOGI(TAG, "OTA HTTP %d，响应 %d 字节: %.240s", status, total, response);

    esp_http_client_cleanup(client);
    return status;
}

static void parse_response(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "OTA 响应不是有效 JSON: %.80s", json ? json : "");
        return;
    }

    const cJSON *activation = cJSON_GetObjectItem(root, "activation");
    const cJSON *code = cJSON_GetObjectItem(activation, "code");
    if (cJSON_IsString(code) && code->valuestring[0]) {
        snprintf(s_activation, sizeof(s_activation), "%s", code->valuestring);
        ESP_LOGW(TAG, "需要激活，验证码: %s", s_activation);
    }

    const cJSON *ws = cJSON_GetObjectItem(root, "websocket");
    const cJSON *url = cJSON_GetObjectItem(ws, "url");
    const cJSON *token = cJSON_GetObjectItem(ws, "token");
    if (cJSON_IsString(url) && cJSON_IsString(token) && url->valuestring[0] &&
        token->valuestring[0]) {
        snprintf(s_ws_url, sizeof(s_ws_url), "%s", url->valuestring);
        snprintf(s_token, sizeof(s_token), "%s", token->valuestring);
        s_activation[0] = '\0';
        s_ready = true;
        store_settings();
        ESP_LOGI(TAG, "激活完成，WebSocket: %s", s_ws_url);
    }
    cJSON_Delete(root);
}

/* ------------------------------------------------------------------------- */
/* WebSocket 传输层（小智官方协议）                                          */
/* ------------------------------------------------------------------------- */

static char s_ws_headers[352];

static void copy_text(char *dst, size_t size, const char *src) {
    if (!dst || size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t len = strlen(src);
    if (len >= size) len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool ws_send_text_now(const char *json) {
    if (!s_ws || !s_ws_connected || !json) return false;
    const int len = (int)strlen(json);
    if (len <= 0) return false;
    if (esp_websocket_client_send_text(s_ws, json, len, pdMS_TO_TICKS(1500)) <= 0) {
        ESP_LOGW(TAG, "发送 JSON 失败");
        return false;
    }
    return true;
}

static bool ws_send_ctrl(xz_tx_op_t op, uint32_t seq, const char *json) {
    if (!json) return false;
    const size_t len = strlen(json);
    if (len == 0 || len >= XZ_TX_ITEM_MAX) {
        ESP_LOGW(TAG, "控制消息过长，已丢弃（%u 字节）", (unsigned)len);
        return false;
    }
    // 队列尚未建立时退回同步发送，保证 hello 等关键消息不会消失。
    if (!s_tx_q) {
        return ws_send_text_now(json);
    }
    xz_tx_item_t item;
    item.op = (uint8_t)op;
    item.len = (uint16_t)len;
    item.seq = seq;
    memcpy(item.data, json, len + 1);
    if (xQueueSend(s_tx_q, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "控制消息队列已满，已丢弃");
        return false;
    }
    return true;
}

static bool ws_send_text(const char *json) {
    return ws_send_ctrl(XZ_TX_OP_TEXT, 0, json);
}

static void log_ws_tx_drop(const char *reason, int err_code) {
    ++s_ws_tx_drop_count;
    const int64_t now_us = esp_timer_get_time();
    if (s_ws_tx_drop_log_us != 0 && now_us - s_ws_tx_drop_log_us < 1000000LL) {
        return;
    }
    s_ws_tx_drop_log_us = now_us;
    wifi_ap_record_t ap;
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = ap.rssi;
    }
    ESP_LOGW(TAG, "上行拥塞丢弃麦克风帧（%s=%d，累计丢=%u，堆=%u，最大块=%u，RSSI=%d dBm）",
             reason, err_code, (unsigned)s_ws_tx_drop_count,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             rssi);
}

static bool ws_send_binary(const uint8_t *payload, size_t len) {
    if (!s_ws || !s_ws_connected || !payload || len == 0) return false;
    // lwIP tcp_write 从系统堆分配发送缓冲；已启用 1/4 MSS (360B)，单帧仅需 ~460B。
    // 仅在堆极度紧张时先丢弃这一帧语音，防止在阻塞 socket 上触发 ERR_MEM。
    const size_t free_heap = esp_get_free_heap_size();
    const size_t max_blk = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (free_heap < 2560 || max_blk < 1024) {
        log_ws_tx_drop("低堆内存", (int)max_blk);
        return false;
    }
    // esp_websocket_client 在 transport_poll_write 超时后会直接 abort 断开整条连接：
    // 先对底层 socket 做短时非阻塞可写预检；拥塞期安全丢弃本帧 60 ms 语音，避免整条会话掉线。
    if (s_ws_trans) {
        const int fd = esp_transport_get_socket(s_ws_trans);
        if (fd < 0 || fd >= FD_SETSIZE) {
            log_ws_tx_drop("fd", fd);
            return false;
        }
        fd_set writeset;
        fd_set errset;
        FD_ZERO(&writeset);
        FD_ZERO(&errset);
        FD_SET(fd, &writeset);
        FD_SET(fd, &errset);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 35000};
        const int sel = select(fd + 1, NULL, &writeset, &errset, &tv);
        if (sel == 0) {
            log_ws_tx_drop("写阻塞", 0);
            return false;
        }
        if (sel < 0) {
            log_ws_tx_drop("select", errno);
            return false;
        }
        if (FD_ISSET(fd, &errset)) {
            int sock_err = 0;
            socklen_t optlen = sizeof(sock_err);
            (void)getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &optlen);
            log_ws_tx_drop("SO_ERROR", sock_err);
            return false;
        }
    }
    if (!s_ws || !s_ws_connected) return false;
    if (esp_websocket_client_send_bin(s_ws, (const char *)payload, (int)len,
                                      pdMS_TO_TICKS(1500)) <= 0) {
        ESP_LOGW(TAG, "发送二进制帧失败");
        return false;
    }
    return true;
}

static void state_lock(void) {
    if (s_state_lock) xSemaphoreTake(s_state_lock, portMAX_DELAY);
}

static void state_unlock(void) {
    if (s_state_lock) xSemaphoreGive(s_state_lock);
}

static void xz_tx_task(void *arg);
static void send_hello(void);

/* 服务端 hello 丢包保护：连上 5 秒还没 session_id 就重发一次 hello，
 * 每 3 秒重试，最多 4 次。实测偶发握手成功但服务端不回 hello，
 * 表现为“连上了但一直不能说话”。 */
#define XZ_HELLO_GRACE_MS 2500
#define XZ_HELLO_RETRY_MS 2000
#define XZ_HELLO_RETRY_MAX 4
/* 连接断开后多久仍连不上就整会话重建（自动重连间隔是 2 秒）。 */
#define XZ_DOWN_GRACE_MS 25000
static TickType_t s_hello_retry_at;
static int s_hello_retries;
static TickType_t s_down_since;
/* hello 看门狗请求整会话重启：由应用层在安全上下文执行。 */
static volatile bool s_session_restart;
#define xz_session_restart_requested s_session_restart

static void session_watchdog(void) {
    const TickType_t now = xTaskGetTickCount();
    if (!s_ws_connected) {
        s_hello_retry_at = 0;
        s_hello_retries = 0;
        // 服务端主动关闭或自动重连卡死时，客户端不会再回到 CONNECTED。
        // 不能让学生永远停在“小智连接中”：超过宽限期就请求整会话重建。
        if (s_ws_started || s_ws_dead) {
            if (s_down_since == 0) {
                s_down_since = now;
            } else if ((int32_t)(now - s_down_since) >
                       (int32_t)pdMS_TO_TICKS(XZ_DOWN_GRACE_MS)) {
                s_down_since = 0;
                ESP_LOGW(TAG, "WebSocket 已 %d ms 未连上，请求重建会话",
                         XZ_DOWN_GRACE_MS);
                s_session_restart = true;
            }
        } else {
            s_down_since = 0;
        }
        return;
    }
    s_down_since = 0;
    bool has_sid;
    state_lock();
    has_sid = s_session_id[0] != '\0';
    state_unlock();
    if (has_sid) {
        s_hello_retry_at = 0;
        s_hello_retries = 0;
        return;
    }
    if (s_hello_retry_at == 0) {
        s_hello_retry_at = now + pdMS_TO_TICKS(XZ_HELLO_GRACE_MS);
        return;
    }
    if ((int32_t)(now - s_hello_retry_at) < 0) return;
    if (s_hello_retries >= XZ_HELLO_RETRY_MAX) {
        // 重发无果：整个会话推倒重来（会关 TLS/解码器并释放内存），
        // 由 ecard_app_tick 在 AI 页面时重新调用 session_start。
        ESP_LOGW(TAG, "服务端多次未回 hello，重启会话");
        s_hello_retries = 0;
        s_hello_retry_at = 0;
        xz_session_restart_requested = true;
        return;
    }
    ++s_hello_retries;
    ESP_LOGW(TAG, "服务端未回 hello，第 %d 次重发", s_hello_retries);
    send_hello();
    s_hello_retry_at = now + pdMS_TO_TICKS(XZ_HELLO_RETRY_MS);
}

static bool ensure_tx_worker(void) {
    if (!s_state_lock) s_state_lock = xSemaphoreCreateMutex();
    // 上一次会话停止时若任务还没退出，等它自己收尾后再重建。
    if (s_tx_stop) {
        for (int i = 0; i < 20 && s_tx_task && !s_tx_done; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_tx_done) s_tx_task = NULL;
    }
    if (!s_tx_q) s_tx_q = xQueueCreate(XZ_TX_QUEUE_LEN, sizeof(xz_tx_item_t));
    if (!s_tx_q) return false;
    if (!s_tx_task) {
        s_tx_stop = false;
        s_tx_done = false;
        if (xTaskCreate(xz_tx_task, "xz_tx", XZ_TX_STACK_BYTES, NULL, 3,
                        &s_tx_task) != pdPASS) {
            s_tx_task = NULL;
            return false;
        }
    }
    return true;
}

/** 中止上一轮回答的实现放在音频区之后（要访问 PCM 缓冲与 TTS 状态）。 */
static void abort_tts_inline(const char *sid);

/** 服务端 hello 下发 session_id 后才允许发 detect，避免和 hello 撞车。 */
static void send_pending_prompt(void) {
    if (!s_pending_prompt_valid || !s_ws || !s_ws_connected) return;
    // 发送失败后 1 秒再试，避免每 50 ms 往半死 socket 上撞一次。
    if (s_prompt_retry_at &&
        (int32_t)(xTaskGetTickCount() - s_prompt_retry_at) < 0) {
        return;
    }

    char prompt[96];
    char sid[64];
    uint32_t seq;
    state_lock();
    const bool valid = s_pending_prompt_valid && s_pending_prompt[0];
    if (valid) {
        // 提示词实测被服务端限制在 16 字以内，这里显式截断到缓冲区容量，
        // 避免 %s 的潜在截断告警（-Werror=format-truncation）。
        snprintf(prompt, sizeof(prompt), "%.95s", s_pending_prompt);
        snprintf(sid, sizeof(sid), "%.63s", s_session_id);
        seq = s_prompt_seq;
    }
    state_unlock();
    if (!valid || !sid[0]) return;

    // 新一轮讲解前先停掉上一段回答：实测服务端在 TTS 播放中收到
    // 新 detect 会先补完旧回答，音频拖尾几秒，学生以为没反应。
    abort_tts_inline(sid);

    state_lock();
    if (!s_pending_prompt_valid || s_prompt_seq != seq) {
        state_unlock();
        return;
    }
    s_pending_prompt_valid = false;
    s_awaiting_tts = true;
    state_unlock();

    // 含中文的提示词上限 16 字；纯英文例句可稍长，JSON 留到 256 字节。
    char json[256];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%.40s\",\"type\":\"listen\",\"state\":\"detect\","
             "\"text\":\"%s\"}",
             sid, prompt);
    const int sent = esp_websocket_client_send_text(s_ws, json, (int)strlen(json),
                                                   pdMS_TO_TICKS(1500));
    if (sent <= 0) {
        state_lock();
        if (s_prompt_seq == seq) {
            s_pending_prompt_valid = true;
            s_awaiting_tts = false;
        }
        state_unlock();
        s_prompt_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
        ESP_LOGW(TAG, "提问发送失败，1 秒后重试");
        return;
    }
    s_prompt_retry_at = 0;
    ESP_LOGI(TAG, "已发送提问: %.60s", prompt);
}

static void xz_tx_task(void *arg) {
    (void)arg;
    for (;;) {
        if (s_tx_stop) break;
        if (xQueueReceive(s_tx_q, &s_tx_item, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (s_tx_stop) break;
            const xz_tx_op_t op = (xz_tx_op_t)s_tx_item.op;
            const bool cur_seq = (s_tx_item.seq == s_listen_seq);
            if (op == XZ_TX_OP_ABORT_PLAYBACK) {
                s_abort_queued = false;
            }
            if (s_ws && s_ws_connected) {
                // 先把残留麦克风帧编码发完并切回解码模式（释放编码器、重开解码器），
                // 再向服务端发 listen:stop / listen_cancel，避免服务端秒回 TTS 时解码器还没就绪。
                // 注意：XZ_TX_OP_ABORT_PLAYBACK 是打断上一轮 TTS 播报，绝不能关闭刚打开的 Opus 编码器！
                if (op == XZ_TX_OP_LISTEN_STOP && cur_seq) {
                    (void)switch_audio_mode_timeout(XZ_AUDIO_DECODE, 250);
                    s_awaiting_tts = true;
                } else if (op == XZ_TX_OP_LISTEN_CANCEL && cur_seq) {
                    (void)switch_audio_mode_timeout(XZ_AUDIO_DECODE, 250);
                    s_awaiting_tts = false;
                }
                const int sent = esp_websocket_client_send_text(
                    s_ws, s_tx_item.data, s_tx_item.len, pdMS_TO_TICKS(1500));
                if (op == XZ_TX_OP_LISTEN_START && cur_seq) {
                    s_listen_pending = false;
                    s_listen_pending_until_us = 0;
                }
                if (sent <= 0) {
                    if (op == XZ_TX_OP_LISTEN_STOP && cur_seq) {
                        s_awaiting_tts = false;
                    }
                    ESP_LOGW(TAG, "发送 JSON 失败（控制队列）");
                } else if (op == XZ_TX_OP_ABORT_PLAYBACK ||
                           op == XZ_TX_OP_LISTEN_CANCEL) {
                    // 给服务端留出 ~60ms 先清理上一轮 TTS 管线，再接收紧随其后的 listen:start，
                    // 防止服务端因旧 TTS 状态尚未收敛而丢弃新一轮上行语音。
                    vTaskDelay(pdMS_TO_TICKS(60));
                }
            } else {
                if (op == XZ_TX_OP_LISTEN_START && cur_seq) {
                    s_listen_pending = false;
                    s_listen_pending_until_us = 0;
                } else if ((op == XZ_TX_OP_LISTEN_STOP ||
                            op == XZ_TX_OP_LISTEN_CANCEL) && cur_seq) {
                    s_audio_req_mode = XZ_AUDIO_DECODE;
                }
            }
        }
        if (s_tx_stop) break;
        send_pending_prompt();
        session_watchdog();
    }
    s_tx_done = true;
    ESP_LOGI(TAG, "发送任务栈余量 %u 字节",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------------- */
/* Opus 音频通道：服务端 TTS 解码播放 / 麦克风 ASR 编码上传                 */
/* ------------------------------------------------------------------------- */

#define XZ_MIC_FRAME_SAMPLES 960   /* 60 ms @ 16 kHz */
#define XZ_PCM_RING_SAMPLES 3072   /* 16 kHz，192 ms 抖动缓冲（录音期复用为编码帧与队列存储，>5760 B） */
#define XZ_DEC_PCM_BYTES 3072      /* 24 kHz/60 ms 解码输出 2880 B；编码期复用 1920+768=2688 B */
#define XZ_ENC_OUT_BYTES 768
#define XZ_OPUS_ITEM_MAX 512
#define XZ_OPUS_QUEUE_LEN 3
#define XZ_ENC_QUEUE_LEN 2

static int16_t s_pcm_ring[XZ_PCM_RING_SAMPLES] __attribute__((aligned(4)));
// 解码输出放在静态区（编码时复用前 2688 字节）：局部大数组会把任务栈压爆。
static uint8_t s_dec_pcm[XZ_DEC_PCM_BYTES] __attribute__((aligned(4)));
static volatile int s_pcm_head;
static volatile int s_pcm_tail;
static volatile int s_pcm_count;
static SemaphoreHandle_t s_pcm_lock;
static esp_audio_dec_handle_t s_opus_dec;
static esp_audio_enc_handle_t s_opus_enc;
static volatile bool s_tts_active;

typedef struct {
    uint16_t len;
    uint8_t data[XZ_OPUS_ITEM_MAX];
} xz_opus_item_t;

static xz_opus_item_t s_opus_item;

/* ESP-IDF 的 RISC-V 端口把 portSTACK_TYPE 定义为 uint8_t：栈深参数与
 * StackType_t 数组都按“字节”计算。16 kHz / 60 ms SILK 编码静音帧实测 ~21.5 KB，
 * 开口说话进入 silk_pitch_analysis_core 时仅在该函数即达 ~23.5 KB（含后续调用
 * 与中断上下文需 ~25 KB）；静态音频任务栈取 28 KB 留足 3 KB 以上安全余量。 */
#define XZ_AUDIO_STACK_BYTES 28672

static StackType_t s_audio_stack[XZ_AUDIO_STACK_BYTES] __attribute__((aligned(16)));
static StaticTask_t s_audio_tcb;
static StaticQueue_t s_enc_q_struct;
static QueueHandle_t s_opus_q;
static QueueHandle_t s_enc_q;
static TaskHandle_t s_audio_task;
static volatile bool s_audio_stop;
static volatile bool s_audio_done;
static volatile bool s_audio_switch_ok;
static SemaphoreHandle_t s_audio_ack_sem;
static SemaphoreHandle_t s_audio_ctrl_lock;

_Static_assert(XZ_MIC_FRAME_SAMPLES * sizeof(int16_t) * (1 + XZ_ENC_QUEUE_LEN) <= sizeof(s_pcm_ring),
               "s_pcm_ring too small for encode frame and queue");
_Static_assert(XZ_MIC_FRAME_SAMPLES * sizeof(int16_t) + XZ_ENC_OUT_BYTES <= XZ_DEC_PCM_BYTES,
               "s_dec_pcm too small for xz_audio encode scratch");

static volatile uint32_t s_enc_sent;
static size_t s_enc_fill;
/* 本机 Opus 编码器固定按 60 ms / 960 样本（服务端 _decode_opus_packet 约定）。 */
static size_t s_enc_frame_samples = XZ_MIC_FRAME_SAMPLES;
static bool s_enc_drop_logged;

static bool ensure_pcm_lock(void) {
    if (!s_pcm_lock) {
        s_pcm_lock = xSemaphoreCreateMutex();
    }
    return s_pcm_lock != NULL;
}

/** 打开解码器：与编码器互斥复用 25 KB 静态后备区，把系统堆留给 TLS 与任务栈。 */
static bool ensure_decoder(void) {
    if (s_opus_dec) return true;
    static TickType_t s_dec_retry_at;
    if (s_dec_retry_at &&
        (int32_t)(xTaskGetTickCount() - s_dec_retry_at) < 0) {
        return false;
    }
    if (!ensure_pcm_lock()) return false;

    static bool s_dec_registered;
    if (!s_dec_registered) {
        esp_audio_dec_register_default();
        s_dec_registered = true;
    }

    // 服务端 hello 声明 24 kHz / 60 ms；解码后重采样到本机 16 kHz I2S。
    esp_opus_dec_cfg_t dec_cfg = {
        .sample_rate = ESP_AUDIO_SAMPLE_RATE_24K,
        .channel = ESP_AUDIO_MONO,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
        .self_delimited = false,
    };
    esp_audio_dec_cfg_t dec_open = {
        .type = ESP_AUDIO_TYPE_OPUS,
        .cfg = &dec_cfg,
        .cfg_sz = sizeof(dec_cfg),
    };
    ecard_opus_arena_begin();
    esp_audio_err_t dec_err = esp_audio_dec_open(&dec_open, &s_opus_dec);
    ecard_opus_arena_end();
    if (dec_err != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "Opus 解码器打开失败: %d（堆 %u，最大连续块 %u，后备区=%d，500ms 后重试）",
                 (int)dec_err,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 (int)ecard_opus_arena_busy());
        s_opus_dec = NULL;
        s_dec_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(500);
        return false;
    }
    ESP_LOGI(TAG, "Opus 解码器就绪（堆剩余 %u，后备区=%d）",
             (unsigned)esp_get_free_heap_size(), (int)ecard_opus_arena_busy());
    return true;
}

/** 录音时才打开编码器，避免和解码器抢占 25 KB 静态后备区。 */
static bool ensure_encoder(bool force) {
    if (s_opus_enc) return true;
    static TickType_t s_enc_retry_at;
    if (!force && s_enc_retry_at &&
        (int32_t)(xTaskGetTickCount() - s_enc_retry_at) < 0) {
        return false;
    }
    if (!ensure_pcm_lock()) return false;

    static bool s_enc_registered;
    if (!s_enc_registered) {
        esp_audio_enc_register_default();
        s_enc_registered = true;
    }

    // 只支持 60 ms 帧：服务端 xiaozhi-server 的 _decode_opus_packet 固定按
    // 960 样本（60 ms @16 kHz）解码设备音频，20/10 ms 帧会被解坏。
    const size_t enc_samples = XZ_MIC_FRAME_SAMPLES;

    esp_opus_enc_config_t enc_cfg = {
        .sample_rate = 16000,
        .channel = ESP_AUDIO_MONO,
        .bits_per_sample = ESP_AUDIO_BIT16,
        .bitrate = 32000,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP,
        .complexity = 0,
    };
    esp_audio_enc_config_t enc_open = {
        .type = ESP_AUDIO_TYPE_OPUS,
        .cfg = &enc_cfg,
        .cfg_sz = sizeof(enc_cfg),
    };
    ecard_opus_arena_begin();
    esp_audio_err_t enc_err = esp_audio_enc_open(&enc_open, &s_opus_enc);
    ecard_opus_arena_end();
    if (enc_err == ESP_AUDIO_ERR_OK && s_opus_enc) {
        s_enc_frame_samples = enc_samples;
        ESP_LOGI(TAG, "Opus 编码器就绪（60 ms 帧，堆剩余 %u，最大连续块 %u，后备区=%d）",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 (int)ecard_opus_arena_busy());
        return true;
    }
    ESP_LOGW(TAG, "Opus 编码器打开失败: %d（堆 %u，最大连续块 %u，后备区=%d）",
             (int)enc_err,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (int)ecard_opus_arena_busy());
    s_opus_enc = NULL;
    s_enc_retry_at = xTaskGetTickCount() + pdMS_TO_TICKS(500);
    return false;
}

static void pcm_ring_push_unlocked(int16_t sample) {
    ++s_pcm_decoded;
    if (s_pcm_count >= XZ_PCM_RING_SAMPLES) {
        // 缓冲满：丢最旧的一个样本，保证最新语音不中断。
        s_pcm_tail = (s_pcm_tail + 1) % XZ_PCM_RING_SAMPLES;
        --s_pcm_count;
        ++s_pcm_overflows;
    }
    s_pcm_ring[s_pcm_head] = sample;
    s_pcm_head = (s_pcm_head + 1) % XZ_PCM_RING_SAMPLES;
    ++s_pcm_count;
}

static void pcm_ring_reset(void) {
    if (s_pcm_lock) xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
    if (s_audio_mode == XZ_AUDIO_DECODE) {
        s_pcm_head = 0;
        s_pcm_tail = 0;
        s_pcm_count = 0;
    }
    if (s_pcm_lock) xSemaphoreGive(s_pcm_lock);
}

/** 新提问/新录音前先中止上一轮回答：否则服务端会同时推两段 TTS，
 * 既把堆和 PCM 缓冲打满，学生也会听到两段声音重叠。 */
static void abort_tts_inline(const char *sid) {
    const bool need_abort = s_tts_active || s_pcm_count > 0 || s_awaiting_tts;
    s_tts_active = false;
    s_awaiting_tts = false;
    ++s_tts_gen;
    if (s_opus_q) (void)xQueueReset(s_opus_q);
    pcm_ring_reset();
    if (!s_ws || !s_ws_connected || !sid || !sid[0]) return;
    if (!need_abort) return;
    char json[128];
    snprintf(json, sizeof(json), "{\"session_id\":\"%.40s\",\"type\":\"abort\"}", sid);
    (void)esp_websocket_client_send_text(s_ws, json, (int)strlen(json),
                                        pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "已中止上一轮回答");
    // 给服务端一点时间先处理 abort，再发新的 listen。
    vTaskDelay(pdMS_TO_TICKS(50));
}

/** 24 kHz -> 16 kHz：每 3 个输入样本输出 2 个，奇数位取相邻均值抗混叠。 */
static void resample_24k_to_16k(const int16_t *src, size_t count, uint32_t gen) {
    if (!s_pcm_lock) return;
    xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
    if (s_audio_mode != XZ_AUDIO_DECODE || !s_tts_active || s_tts_gen != gen) {
        xSemaphoreGive(s_pcm_lock);
        return;
    }
    const size_t out_n = count * 2 / 3;
    for (size_t m = 0; m < out_n; ++m) {
        const size_t pos = (3 * m) / 2;
        int16_t value;
        if ((3 * m) % 2 == 0 || pos + 1 >= count) {
            value = src[pos];
        } else {
            int32_t avg = ((int32_t)src[pos] + (int32_t)src[pos + 1]) / 2;
            value = (int16_t)avg;
        }
        pcm_ring_push_unlocked(value);
    }
    xSemaphoreGive(s_pcm_lock);
}

static void enqueue_opus_frame(const uint8_t *data, size_t len) {
    if (s_audio_mode != XZ_AUDIO_DECODE || !s_opus_q || !data || len == 0 ||
        len > XZ_OPUS_ITEM_MAX) {
        ++s_audio_queue_drops;
        return;
    }
    xz_opus_item_t item;
    item.len = (uint16_t)len;
    memcpy(item.data, data, len);
    if (xQueueSend(s_opus_q, &item, 0) != pdTRUE) {
        ++s_audio_queue_drops;
    }
}

static void tts_decode_frame(const uint8_t *data, size_t len, uint32_t gen) {
    if (s_tts_gen != gen || !s_tts_active) return;
    if (!ensure_decoder()) return;

    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t *)data,
        .len = (uint32_t)len,
    };
    esp_audio_dec_out_frame_t out = {
        .buffer = s_dec_pcm,
        .len = sizeof(s_dec_pcm),
    };

    while (raw.len) {
        if (s_tts_gen != gen || !s_tts_active) break;
        esp_audio_err_t err = esp_audio_dec_process(s_opus_dec, &raw, &out);
        if (err != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "Opus 解码失败: %d", (int)err);
            break;
        }
        if (raw.consumed == 0) break;
        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;
        if (out.decoded_size >= 2) {
            ++s_decoded_frames;
            resample_24k_to_16k((const int16_t *)s_dec_pcm, out.decoded_size / 2, gen);
        }
    }
}

static void encode_one_frame(const int16_t *frame, uint32_t *enc_frames) {
    if (!s_ws_connected || !s_opus_enc || !frame) return;
    if (s_listen_pending && s_vad.total_ms <= ECARD_VAD_SETTLE_MS) {
        return;
    }
    uint8_t *const out_buf = s_dec_pcm + XZ_MIC_FRAME_SAMPLES * sizeof(int16_t);
    esp_audio_enc_in_frame_t in = {
        .buffer = (uint8_t *)frame,
        .len = s_enc_frame_samples * sizeof(int16_t),
    };
    esp_audio_enc_out_frame_t out = {
        .buffer = out_buf,
        .len = XZ_ENC_OUT_BYTES,
    };
    const size_t heap_before = (*enc_frames == 0) ? esp_get_free_heap_size() : 0;
    const esp_audio_err_t enc_ret = esp_audio_enc_process(s_opus_enc, &in, &out);
    if (*enc_frames == 0) {
        ESP_LOGI(TAG, "首帧编码堆: %u -> %u (最大块=%u)",
                 (unsigned)heap_before,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    if (enc_ret == ESP_AUDIO_ERR_OK && out.encoded_bytes > 0) {
        if (ws_send_binary(out_buf, out.encoded_bytes)) {
            ++s_enc_sent;
        }
    }
    const uint32_t n = ++(*enc_frames);
    if (n == 1 || (n & 0x1f) == 0) {
        ESP_LOGI(TAG, "Opus 编码栈余量 %u 字节（帧 %u）",
                 (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                 (unsigned)n);
    }
}

static void xz_audio_task(void *arg) {
    (void)arg;
    uint32_t dec_frames = 0;
    uint32_t enc_frames = 0;
    int16_t *const enc_in_frame = (int16_t *)s_dec_pcm;
    uint8_t *const enc_q_buf = (uint8_t *)s_pcm_ring + XZ_MIC_FRAME_SAMPLES * sizeof(int16_t);

    for (;;) {
        if (s_audio_stop) break;

        if (s_audio_req_mode != s_audio_mode) {
            const xz_audio_mode_t target = s_audio_req_mode;
            if (target == XZ_AUDIO_ENCODE) {
                if (s_opus_q) (void)xQueueReset(s_opus_q);
                if (s_opus_dec) {
                    esp_audio_dec_close(s_opus_dec);
                    s_opus_dec = NULL;
                }
                pcm_ring_reset();
                // 切到编码模式期间复用 s_pcm_ring 作为麦克风积帧缓冲与编码队列存储，避免堆碎片。
                if (s_pcm_lock) xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
                s_enc_fill = 0;
                s_enc_drop_logged = false;
                s_enc_q = xQueueCreateStatic(XZ_ENC_QUEUE_LEN,
                                             XZ_MIC_FRAME_SAMPLES * sizeof(int16_t),
                                             enc_q_buf, &s_enc_q_struct);
                if (s_pcm_lock) xSemaphoreGive(s_pcm_lock);

                if (s_enc_q && ensure_encoder(true)) {
                    enc_frames = 0;
                    s_audio_mode = XZ_AUDIO_ENCODE;
                    s_audio_switch_ok = true;
                } else {
                    if (s_pcm_lock) xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
                    s_enc_q = NULL;
                    s_enc_fill = 0;
                    if (s_pcm_lock) xSemaphoreGive(s_pcm_lock);
                    pcm_ring_reset();
                    (void)ensure_decoder();
                    s_audio_mode = XZ_AUDIO_DECODE;
                    s_audio_req_mode = XZ_AUDIO_DECODE;
                    s_audio_switch_ok = false;
                }
                if (s_audio_ack_sem) xSemaphoreGive(s_audio_ack_sem);
            } else {
                // 切回解码模式前先把队列里最后一两帧麦克风语音编码发完，避免尾音被切掉。
                while (s_enc_q && xQueueReceive(s_enc_q, enc_in_frame, 0) == pdTRUE) {
                    encode_one_frame(enc_in_frame, &enc_frames);
                }
                if (s_pcm_lock) xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
                s_audio_mode = XZ_AUDIO_DECODE;
                s_enc_q = NULL;
                s_enc_fill = 0;
                s_pcm_head = 0;
                s_pcm_tail = 0;
                s_pcm_count = 0;
                if (s_pcm_lock) xSemaphoreGive(s_pcm_lock);

                if (s_opus_enc) {
                    esp_audio_enc_close(s_opus_enc);
                    s_opus_enc = NULL;
                }
                (void)ensure_decoder();
                s_audio_switch_ok = true;
                if (s_audio_ack_sem) xSemaphoreGive(s_audio_ack_sem);
            }
            continue;
        }

        if (s_audio_mode == XZ_AUDIO_DECODE) {
            if (s_opus_q &&
                xQueueReceive(s_opus_q, &s_opus_item, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (s_audio_stop || s_audio_req_mode != XZ_AUDIO_DECODE) continue;
                const uint32_t gen = s_tts_gen;
                if (!s_tts_active) continue;
                tts_decode_frame(s_opus_item.data, s_opus_item.len, gen);
                if ((++dec_frames & 0x1f) == 0) {
                    ESP_LOGI(TAG, "Opus 解码栈余量 %u 字节",
                             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
                }
            }
        } else {
            if (s_listen_pending) {
                const int64_t deadline = s_listen_pending_until_us;
                if (deadline == 0 || esp_timer_get_time() >= deadline) {
                    s_listen_pending = false;
                    s_listen_pending_until_us = 0;
                } else if (s_vad.total_ms <= ECARD_VAD_SETTLE_MS) {
                    // 前 120ms 扬声器尾音静默期内直接丢弃残响帧，防止把小智自己的尾音录进去。
                    if (s_enc_q) {
                        (void)xQueueReceive(s_enc_q, enc_in_frame, pdMS_TO_TICKS(15));
                    } else {
                        vTaskDelay(pdMS_TO_TICKS(15));
                    }
                    continue;
                } else {
                    // 超过 120ms 但 listen:start 仍在队列发送中：暂缓出队，把学生的起首字保留在队列中。
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
            }
            if (s_enc_q &&
                xQueueReceive(s_enc_q, enc_in_frame, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (s_audio_stop) continue;
                encode_one_frame(enc_in_frame, &enc_frames);
            }
        }
    }

    if (s_pcm_lock) xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
    s_enc_q = NULL;
    s_enc_fill = 0;
    if (s_pcm_lock) xSemaphoreGive(s_pcm_lock);
    if (s_opus_enc) {
        esp_audio_enc_close(s_opus_enc);
        s_opus_enc = NULL;
    }
    if (s_opus_dec) {
        esp_audio_dec_close(s_opus_dec);
        s_opus_dec = NULL;
    }
    s_audio_mode = XZ_AUDIO_DECODE;
    s_audio_req_mode = XZ_AUDIO_DECODE;
    s_audio_switch_ok = false;
    if (s_audio_ack_sem) xSemaphoreGive(s_audio_ack_sem);
    s_audio_done = true;
    vTaskDelete(NULL);
}

void ecard_xiaozhi_feed_mic_pcm(const int16_t *pcm, size_t samples) {
    if (!s_ws_connected || !pcm || samples == 0 || !s_pcm_lock) {
        return;
    }
    xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
    ecard_vad_feed(&s_vad, pcm, samples, ECARD_VOICE_SAMPLE_RATE);
    QueueHandle_t q = (s_audio_mode == XZ_AUDIO_ENCODE) ? s_enc_q : NULL;
    if (q) {
        int16_t *const enc_frame = s_pcm_ring;
        for (size_t i = 0; i < samples; ++i) {
            enc_frame[s_enc_fill++] = pcm[i];
            if (s_enc_fill < s_enc_frame_samples) continue;
            s_enc_fill = 0;
            if (xQueueSend(q, enc_frame, 0) != pdTRUE) {
                if (!s_enc_drop_logged) {
                    s_enc_drop_logged = true;
                    ESP_LOGW(TAG, "编码队列已满，丢弃麦克风帧");
                }
            }
        }
    }
    xSemaphoreGive(s_pcm_lock);
}

void ecard_xiaozhi_reset_mic(void) {
    if (!s_pcm_lock) {
        s_enc_fill = 0;
        ecard_vad_reset(&s_vad);
        return;
    }
    xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
    s_enc_fill = 0;
    ecard_vad_reset(&s_vad);
    if (s_audio_mode == XZ_AUDIO_ENCODE && s_enc_q) {
        (void)xQueueReset(s_enc_q);
    }
    xSemaphoreGive(s_pcm_lock);
}

bool ecard_xiaozhi_mic_active(void) {
    return s_vad.total_ms > 0;
}

uint8_t ecard_xiaozhi_mic_amplitude(void) {
    return s_vad.amplitude;
}

bool ecard_xiaozhi_mic_voice_detected(void) {
    return s_vad.voice_detected;
}

bool ecard_xiaozhi_mic_vad_complete(void) {
    return s_vad.end_of_speech;
}

bool ecard_xiaozhi_is_speaking(void) {
    return s_tts_active || s_pcm_count > 0;
}

void ecard_xiaozhi_reset_playback(void) {
    const bool was_speaking = s_tts_active || s_pcm_count > 0;
    const bool need_abort = was_speaking || s_awaiting_tts;
    s_tts_active = false;
    s_awaiting_tts = false;
    ++s_tts_gen;
    if (s_opus_q) (void)xQueueReset(s_opus_q);
    pcm_ring_reset();

    // 切题/离开页面时同时作废未发出的旧提问、长文兜底和单词例句追问，避免新旧语音串台。
    char sid[64];
    state_lock();
    ++s_prompt_seq;
    s_pending_prompt_valid = false;
    s_prompt_fallback_valid = false;
    s_follow_valid = false;
    s_follow_prompt[0] = '\0';
    s_last_prompt[0] = '\0';
    s_last_prompt_fallback[0] = '\0';
    snprintf(sid, sizeof(sid), "%.63s", s_session_id);
    state_unlock();

    if (!need_abort || !sid[0] || s_abort_queued) return;

    // 只清本地缓冲还不够：服务端仍会继续推完这一轮回答，下一题提交时
    // 新旧两段 TTS 会叠在一起。这里用显式 XZ_TX_OP_ABORT_PLAYBACK 入队，由 xz_tx 在
    // 下一条 listen/detect 之前发出，既立即停声、不阻塞按键线程，也绝不会误关 Opus 编码器。
    char json[128];
    snprintf(json, sizeof(json), "{\"session_id\":\"%.40s\",\"type\":\"abort\"}", sid);
    s_abort_queued = true;
    if (!ws_send_ctrl(XZ_TX_OP_ABORT_PLAYBACK, 0, json) || !s_tx_q) {
        s_abort_queued = false;
    }
    ESP_LOGI(TAG, "已请求中止上一轮回答");
}

size_t ecard_xiaozhi_read_pcm(int16_t *out, size_t max_samples) {
    if (!out || max_samples == 0 || !s_pcm_lock) return 0;

    size_t n = 0;
    xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
    if (s_audio_mode == XZ_AUDIO_DECODE) {
        while (n < max_samples && s_pcm_count > 0) {
            out[n++] = s_pcm_ring[s_pcm_tail];
            s_pcm_tail = (s_pcm_tail + 1) % XZ_PCM_RING_SAMPLES;
            --s_pcm_count;
        }
    }
    xSemaphoreGive(s_pcm_lock);
    return n;
}

static void send_hello(void) {
    // 小智协议：客户端 hello 声明 Opus / 16 kHz / 单声道 / 60 ms。
    // 与服务端 hello 里的 audio_params 对齐后即可收发语音帧。
    // mcp 必须声明 false：本机没有实现 MCP，声明 true 只会让服务端发来
    // initialize/vision 请求并等待应答，白白占用会话状态。
    char hello[320];
    snprintf(hello, sizeof(hello),
             "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\","
             "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":16000,"
             "\"channels\":1,\"frame_duration\":60},"
             "\"features\":{\"mcp\":false,\"aec\":false}}");
    if (s_tx_q && xTaskGetCurrentTaskHandle() != s_tx_task) {
        ws_send_text(hello);
    } else {
        ws_send_text_now(hello);
    }
    ESP_LOGI(TAG, "已提交 hello（Opus 16 kHz / 60 ms，堆剩余 %u）",
             (unsigned)esp_get_free_heap_size());
}

static void handle_json(const char *json, size_t len) {
    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, json, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        ESP_LOGW(TAG, "服务端 JSON 解析失败");
        return;
    }

    const cJSON *type_item = cJSON_GetObjectItem(root, "type");
    const char *type = cJSON_IsString(type_item) ? type_item->valuestring : "";
    copy_text(s_last_event, sizeof(s_last_event), type);

    if (strcmp(type, "hello") == 0) {
        const cJSON *sid = cJSON_GetObjectItem(root, "session_id");
        char sid_log[64] = {0};
        state_lock();
        if (cJSON_IsString(sid)) {
            copy_text(s_session_id, sizeof(s_session_id), sid->valuestring);
        }
        snprintf(sid_log, sizeof(sid_log), "%s", s_session_id);
        state_unlock();
        // xz_tx 任务会在 50 ms 内看到 session_id 并发出缓存的提问。
        const cJSON *params = cJSON_GetObjectItem(root, "audio_params");
        const cJSON *rate = cJSON_GetObjectItem(params, "sample_rate");
        const cJSON *frame = cJSON_GetObjectItem(params, "frame_duration");
        ESP_LOGI(TAG, "服务端 hello: session=%s 采样率=%d 帧长=%d",
                 sid_log, cJSON_IsNumber(rate) ? rate->valueint : 0,
                 cJSON_IsNumber(frame) ? frame->valueint : 0);
    } else if (strcmp(type, "stt") == 0) {
        const cJSON *text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            copy_text(s_last_stt, sizeof(s_last_stt), text->valuestring);
            ESP_LOGI(TAG, "ASR: %s", s_last_stt);
        }
    } else if (strcmp(type, "llm") == 0) {
        const cJSON *text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            // 服务端流式下发，按块拼接成完整回复，供答题讲解/留言转写使用。
            const size_t used = strlen(s_last_llm);
            const size_t room = sizeof(s_last_llm) - used - 1;
            if (room > 0) {
                strncat(s_last_llm, text->valuestring, room);
            }
            ESP_LOGI(TAG, "LLM: %s", text->valuestring);
        }
    } else if (strcmp(type, "tts") == 0) {
        const cJSON *state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(state)) {
            copy_text(s_last_tts_state, sizeof(s_last_tts_state), state->valuestring);
            const cJSON *text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text) && text->valuestring[0]) {
                ESP_LOGI(TAG, "TTS %s: %s", s_last_tts_state, text->valuestring);
            } else {
                ESP_LOGI(TAG, "TTS: %s", s_last_tts_state);
            }
            if (strcmp(state->valuestring, "start") == 0) {
                if (!s_awaiting_tts || s_pending_prompt_valid ||
                    s_audio_mode != XZ_AUDIO_DECODE ||
                    s_audio_req_mode != XZ_AUDIO_DECODE) {
                    ESP_LOGW(TAG, "丢弃已作废轮次的迟到 tts:start");
                } else {
                    s_prompt_fallback_valid = false;
                    s_tts_audio_bytes = 0;
                    s_pcm_decoded = 0;
                    s_rx_audio_frames = 0;
                    s_rx_audio_bytes = 0;
                    s_audio_queue_drops = 0;
                    s_pcm_overflows = 0;
                    s_decoded_frames = 0;
                    s_last_rx_us = esp_timer_get_time();
                    if (s_opus_q) (void)xQueueReset(s_opus_q);
                    pcm_ring_reset();
                    ++s_tts_gen;
                    s_tts_active = true;
                }
            } else if (strcmp(state->valuestring, "stop") == 0) {
                const bool was_active = s_tts_active;
                s_tts_active = false;
                if (was_active) {
                    s_awaiting_tts = false;
                }
                ESP_LOGI(TAG, "TTS 结束：收到 %d 字节 Opus，解码 %d 个 16 kHz 样本",
                         s_tts_audio_bytes, s_pcm_decoded);
                // 单词卡第二轮：第一轮（单词+词义）播完后自动追问例句。
                if (was_active) {
                    char follow[sizeof(s_follow_prompt)];
                    bool has_follow = false;
                    state_lock();
                    if (s_follow_valid && s_follow_prompt[0]) {
                        snprintf(follow, sizeof(follow), "%s", s_follow_prompt);
                        s_follow_valid = false;
                        s_follow_prompt[0] = '\0';
                        has_follow = true;
                    }
                    state_unlock();
                    if (has_follow) {
                        ESP_LOGI(TAG, "发出例句追问: %.60s", follow);
                        ecard_xiaozhi_ask(follow);
                    }
                }
            }
        }
    } else if (strcmp(type, "alert") == 0) {
        const cJSON *status_item = cJSON_GetObjectItem(root, "status");
        const cJSON *message_item = cJSON_GetObjectItem(root, "message");
        const char *status = cJSON_IsString(status_item) ? status_item->valuestring : "";
        const char *message = cJSON_IsString(message_item) ? message_item->valuestring : "";
        ESP_LOGW(TAG, "服务端 alert: status=%s message=%s", status, message);
        // 长文本被拒时改发调用方准备的短提示词；学生只会多等一次轮询。
        if (strstr(message, "long texts") != NULL) {
            char fallback[sizeof(s_prompt_fallback)];
            char sid[64];
            bool has = false;
            state_lock();
            snprintf(sid, sizeof(sid), "%.63s", s_session_id);
            if (s_prompt_fallback_valid) {
                snprintf(fallback, sizeof(fallback), "%s", s_prompt_fallback);
                s_prompt_fallback_valid = false;
                has = true;
            }
            state_unlock();
            // 实测：长文本 alert 之后同会话的短 detect 会被静默丢弃；
            // 先发 abort 复位服务端状态，再发短提示词才能正常回答。
            if (sid[0]) {
                char abort_json[160];
                snprintf(abort_json, sizeof(abort_json),
                         "{\"session_id\":\"%.40s\",\"type\":\"abort\"}", sid);
                ws_send_text_now(abort_json);
            }
            s_tts_active = false;
            s_awaiting_tts = false;
            ++s_tts_gen;
            if (s_opus_q) (void)xQueueReset(s_opus_q);
            pcm_ring_reset();
            if (has && fallback[0]) {
                ESP_LOGW(TAG, "长提示词被拒，改发短提示词: %s", fallback);
                ecard_xiaozhi_ask(fallback);
            }
        }
    } else {
        ESP_LOGI(TAG, "事件: %s", type);
    }

    cJSON_Delete(root);
}

static void handle_binary(const uint8_t *data, size_t len) {
    if (!data || len == 0) return;
    // 只有服务端发过本轮 tts start 才收音频。翻卡/追问时旧回答的残帧
    // 若继续解码，会和下一轮 TTS 叠在一起，听起来像两个声音在念。
    if (!s_tts_active) return;
    // WebSocket 版小智协议：文本帧承载 JSON，二进制帧就是裸 Opus 音频，
    // 没有额外的类型前缀（MQTT/UDP 版本才有首字节类型）。
    s_tts_audio_bytes += (int)len;
    ++s_rx_audio_frames;
    s_rx_audio_bytes += (uint32_t)len;
    s_last_rx_us = esp_timer_get_time();
    // 解码放到独立任务：Opus 解码栈需求远大于 WebSocket 事件任务栈。
    enqueue_opus_frame(data, len);
}

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                             void *event_data) {
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_ws_connected = true;
        s_connect_failed = false;
        s_force_ota = false;
        s_retried_ota_this_session = false;
        // 禁用 Nagle：60 ms Opus 上行每次按 WS 头 + 载荷两次 ssl_write 发出，
        // 不开 TCP_NODELAY 易与对端延迟 ACK 叠成发送队列积压触发 poll_write 超时断流。
        if (s_ws_trans) {
            const int fd = esp_transport_get_socket(s_ws_trans);
            if (fd >= 0) {
                int nodelay = 1;
                if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) != 0) {
                    ESP_LOGW(TAG, "设置 TCP_NODELAY 失败: errno=%d", errno);
                }
                // 默认 SO_SNDTIMEO 等于 network_timeout_ms (10s)，阻塞 socket 遇瞬间拥塞会卡死发送锁 10 秒。
                struct timeval snd_tv = {.tv_sec = 1, .tv_usec = 500000};
                if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_tv, sizeof(snd_tv)) != 0) {
                    ESP_LOGW(TAG, "设置 SO_SNDTIMEO 失败: errno=%d", errno);
                }
            }
        }
        // 握手完成后只启动后台音频/发送任务，Opus 解码器延迟到 28 KB 静态栈的 xz_audio_task 首次解码时打开，
        // 避免在 WebSocket 事件任务栈上执行解码器初始化与注册导致首连栈峰值过高。
        (void)ensure_audio_worker();
        if (!ensure_tx_worker()) {
            ESP_LOGW(TAG, "发送任务创建失败，控制消息将走同步发送");
        }
        ESP_LOGI(TAG, "WebSocket 已连接（握手后堆剩余 %u，任务栈余量 %u 字节）",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        send_hello();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_ws_connected = false;
        s_listen_pending = false;
        s_listen_pending_until_us = 0;
        {
            const bool interrupted = s_tts_active;
            state_lock();
            s_session_id[0] = '\0';
            state_unlock();
            // 连接断了就不能继续“播放”：残留的 tts_active 会让音频任务一直空转。
            s_tts_active = false;
            s_awaiting_tts = false;
            ++s_tts_gen;
            if (s_opus_q) (void)xQueueReset(s_opus_q);
            pcm_ring_reset();
            // 断线回调可能直接运行在 xz_audio_task 上（send_bin 失败触发 abort），
            // 此处只能非阻塞置请求位，由 xz_audio_task 自行切回解码模式，避免 1000ms 自死锁。
            s_audio_req_mode = XZ_AUDIO_DECODE;
            arm_replay_after_disconnect(interrupted);
        }
        ESP_LOGW(TAG, "WebSocket 断开，等待自动重连（任务栈余量 %u 字节）",
                 (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        break;
    case WEBSOCKET_EVENT_DATA:
        if (!data) break;
        if (data->op_code == 0x01 && data->data_ptr) {
            handle_json(data->data_ptr, (size_t)data->data_len);
        } else if (data->op_code == 0x02 && data->data_ptr) {
            handle_binary((const uint8_t *)data->data_ptr, (size_t)data->data_len);
        } else if (data->op_code == 0x08) {
            ESP_LOGW(TAG, "服务端关闭 WebSocket");
        }
        break;
    case WEBSOCKET_EVENT_ERROR: {
        const int ws_status = data ? data->error_handle.esp_ws_handshake_status_code : 0;
        if (data && data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGW(TAG, "WebSocket 错误，握手状态码 %d，TLS err=0x%x/%d errno=%d",
                     ws_status,
                     (unsigned)data->error_handle.esp_tls_last_esp_err,
                     data->error_handle.esp_tls_stack_err,
                     data->error_handle.esp_transport_sock_errno);
        } else {
            ESP_LOGW(TAG, "WebSocket 错误，类型 %d 握手状态码 %d: %.*s",
                     data ? (int)data->error_handle.error_type : -1,
                     ws_status,
                     (data && data->data_ptr && data->data_len > 0) ? data->data_len : 0,
                     (data && data->data_ptr) ? data->data_ptr : "");
        }
        if ((ws_status == 401 || ws_status == 403) && !s_retried_ota_this_session) {
            s_retried_ota_this_session = true;
            s_force_ota = true;
            s_session_restart = true;
            ESP_LOGW(TAG, "WebSocket 鉴权失败(%d)，将重走 OTA 刷新凭证", ws_status);
        } else if (!s_ws_connected) {
            s_connect_failed = true;
        }
        break;
    }
    case WEBSOCKET_EVENT_CLOSED:
        s_ws_connected = false;
        s_listen_pending = false;
        s_listen_pending_until_us = 0;
        {
            const bool interrupted = s_tts_active;
            state_lock();
            s_session_id[0] = '\0';
            state_unlock();
            s_tts_active = false;
            s_awaiting_tts = false;
            ++s_tts_gen;
            if (s_opus_q) (void)xQueueReset(s_opus_q);
            pcm_ring_reset();
            s_audio_req_mode = XZ_AUDIO_DECODE;
            arm_replay_after_disconnect(interrupted);
        }
        ESP_LOGW(TAG, "WebSocket 连接已关闭");
        break;
    case WEBSOCKET_EVENT_FINISH:
        // 客户端任务已经退出：仅在非主动关闭时置位 s_ws_dead，避免 session_stop 后误伤下一次建连。
        s_ws_connected = false;
        s_listen_pending = false;
        s_listen_pending_until_us = 0;
        if (!s_ws_stopping) {
            s_ws_dead = true;
            ESP_LOGW(TAG, "WebSocket 任务已退出，等待重建会话");
        }
        break;
    default:
        break;
    }
}

/** 断流时自动重问：只在“回答说到一半”被打断时重播一次。 */
static void arm_replay_after_disconnect(bool interrupted) {
    if (!interrupted || !s_last_prompt[0] || s_replay_count >= 1) return;
    s_replay_count = 1;
    state_lock();
    snprintf(s_pending_prompt, sizeof(s_pending_prompt), "%s", s_last_prompt);
    if (s_last_prompt_fallback[0]) {
        snprintf(s_prompt_fallback, sizeof(s_prompt_fallback), "%s",
                 s_last_prompt_fallback);
        s_prompt_fallback_valid = true;
    } else {
        s_prompt_fallback_valid = false;
    }
    ++s_prompt_seq;
    s_pending_prompt_valid = true;
    state_unlock();
    ESP_LOGW(TAG, "断流后自动重问（重连 hello 后发出）: %.60s", s_last_prompt);
}

static void ws_release_transports(void) {
    if (s_ws_trans) {
        esp_transport_destroy(s_ws_trans);
        s_ws_trans = NULL;
    }
    if (s_ws_base) {
        esp_transport_destroy(s_ws_base);
        s_ws_base = NULL;
    }
}

static esp_transport_handle_t ws_create_transports(const char *url, const char *headers) {
    ws_release_transports();
    if (!url || !url[0]) return NULL;

    const bool is_wss = (strncmp(url, "wss://", 6) == 0);
    const int default_port = is_wss ? 443 : 80;
    s_ws_base = is_wss ? esp_transport_ssl_init() : esp_transport_tcp_init();
    if (!s_ws_base) return NULL;
    esp_transport_set_default_port(s_ws_base, default_port);
    if (is_wss) {
        esp_transport_ssl_crt_bundle_attach(s_ws_base, esp_crt_bundle_attach);
    }

    s_ws_trans = esp_transport_ws_init(s_ws_base);
    if (!s_ws_trans) {
        ws_release_transports();
        return NULL;
    }
    esp_transport_set_default_port(s_ws_trans, default_port);

    const char *scheme_end = strstr(url, "://");
    const char *authority = scheme_end ? (scheme_end + 3) : url;
    const char *slash = strchr(authority, '/');
    const char *query = strchr(authority, '?');
    char path_buf[96];
    if (slash) {
        snprintf(path_buf, sizeof(path_buf), "%s", slash);
    } else if (query) {
        snprintf(path_buf, sizeof(path_buf), "/%s", query);
    } else {
        snprintf(path_buf, sizeof(path_buf), "/");
    }

    const esp_transport_ws_config_t ws_cfg = {
        .ws_path = path_buf,
        .headers = headers,
        .propagate_control_frames = true,
    };
    if (esp_transport_ws_set_config(s_ws_trans, &ws_cfg) != ESP_OK) {
        ws_release_transports();
        return NULL;
    }
    return s_ws_trans;
}

static void ws_start(void) {
    if (s_ws_started || !s_ready) return;
    s_ws_stopping = false;
    s_ws_dead = false;
    s_down_since = 0;
    s_ws_started = true;

    // 解码器、音频任务与发送任务统一在 WEBSOCKET_EVENT_CONNECTED（握手完成）之后创建，
    // 保留尽量多的连续堆内存给 TLS 握手，防止 mbedTLS PK 验证报 0x4290 (MPI alloc failed)。

    char mac[24];
    device_mac(mac, sizeof(mac));
    snprintf(s_ws_headers, sizeof(s_ws_headers),
             "Authorization: Bearer %s\r\nProtocol-Version: 1\r\n"
             "Device-Id: %s\r\nClient-Id: %s\r\n",
             s_token, mac, mac);

    esp_transport_handle_t ext_trans = ws_create_transports(s_ws_url, s_ws_headers);
    if (!ext_trans) {
        s_ws_started = false;
        s_connect_failed = true;
        ESP_LOGE(TAG, "WebSocket 传输层创建失败");
        return;
    }

    esp_websocket_client_config_t cfg = {
        .uri = s_ws_url,
        .headers = s_ws_headers,
        .buffer_size = WS_BUFFER_SIZE,
        .task_stack = WS_TASK_STACK,
        // TTS 下行约 4 KB/s，解码/音频任务会把 CPU 占满；
        // 接收任务优先级必须高于它们，否则窗口填满后服务端直接 RST。
        .task_prio = 6,
        .reconnect_timeout_ms = 2000,
        .network_timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .ext_transport = ext_trans,
        .disable_auto_reconnect = false,
        // 服务端主动关闭（换会话、长时间空闲）后仍要能连回来；
        // 否则客户端任务直接退出，学生界面会永远停在“小智连接中”。
        .enable_close_reconnect = true,
    };
    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) {
        ws_release_transports();
        s_ws_started = false;
        s_connect_failed = true;
        ESP_LOGE(TAG, "WebSocket 客户端创建失败");
        return;
    }
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket 启动失败");
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
        ws_release_transports();
        s_ws_started = false;
        s_connect_failed = true;
        return;
    }
    ESP_LOGI(TAG, "WebSocket 连接中: %s（堆剩余 %u）", s_ws_url,
             (unsigned)esp_get_free_heap_size());
}

static void ota_task(void *arg) {
    (void)arg;
    char *body = malloc(OTA_BODY_MAX);
    char *response = malloc(RESPONSE_MAX);
    if (!body || !response) {
        free(body);
        free(response);
        ESP_LOGE(TAG, "无法分配 OTA 缓冲");
        s_connect_failed = true;
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }
    build_body(body, OTA_BODY_MAX);

    for (int attempt = 0; attempt < 3 && !s_ota_abort; ++attempt) {
        const int status = http_post(body, response, RESPONSE_MAX);
        if (status == 200) {
            parse_response(response);
            if (s_ready) {
                s_force_ota = false;
                break;
            }
        } else {
            ESP_LOGW(TAG, "OTA 请求失败，状态码 %d，响应: %.300s", status, response);
        }
        if (s_ota_abort) {
            break;
        }
        // 未激活设备需等待用户配对，按 30 秒轮询；若仅网络/TLS 出错，快速重试一次即可上报失败。
        const uint32_t wait_ms = (s_activation[0] != '\0') ? ECARD_XIAOZHI_POLL_MS : 1000u;
        if (s_activation[0] == '\0' && attempt >= 1) {
            break;
        }
        for (uint32_t waited = 0; waited < wait_ms && !s_ota_abort; waited += 200u) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    free(body);
    free(response);
    const bool start_ws = (s_ready && !s_ota_abort);
    if (!s_ready && !s_ota_abort) {
        s_connect_failed = true;
    }
    s_ota_running = false;
    if (start_ws) {
        ws_start();
    }
    ESP_LOGI(TAG, "OTA 任务结束（栈最低余量 %u 字节）",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
    vTaskDelete(NULL);
}

void ecard_xiaozhi_start(void) {
    if (s_started) return;
    s_started = true;
    load_settings();
    // 不做开机自动连接：长按录音、首页等日常操作不应占用 TLS/Opus 的内存，
    // 否则录音缓冲会被挤到 2 秒。只有明确的 AI 入口才调用 session_start()。
    ESP_LOGI(TAG, "小智按需连接：仅在答题讲解/朗读等入口触发");
}

void ecard_xiaozhi_session_start(void) {
    if (!s_started) {
        s_started = true;
        load_settings();
    }
    // 看门狗判死 / 客户端任务已经退出：旧连接还挂着，不能在这里内联重建
    // （会泄漏旧对象，而且这是一条会阻塞的销毁）。只置位，让 app 层走
    // restart_pending() → 异步 session_stop() → session_start()。
    if (s_session_restart || s_ws_dead) {
        s_session_restart = true;
        ESP_LOGW(TAG, "连接需要重建（restart=%d dead=%d）",
                 (int)s_session_restart, (int)s_ws_dead);
        return;
    }
    if (s_ws_connected || s_ws_started || s_ota_running) {
        ESP_LOGI(TAG, "会话已在进行中（ws=%d started=%d ota=%d）", (int)s_ws_connected,
                 (int)s_ws_started, (int)s_ota_running);
        return;
    }
    s_connect_failed = false;
    s_ota_abort = false;
    // AI 语音是持续下行流，省电模式会让 DTIM 休眠拖慢接收，长 TTS 容易断流。
    ecard_netlink_set_low_latency(true);
    // 已有本地缓存的 WS 地址和 token 时直接发起 WebSocket（省去一次 HTTPS/OTA 峰值并提速），
    // 若遇 401/403 鉴权失败再自动回落到 OTA 刷新凭证。
    if (s_ready && !s_force_ota) {
        ws_start();
        return;
    }
    s_ota_running = true;
    if (xTaskCreate(ota_task, "xiaozhi_ota", OTA_TASK_STACK, NULL, 3, NULL) != pdPASS) {
        s_ota_running = false;
        s_connect_failed = true;
        ESP_LOGE(TAG, "OTA 任务创建失败");
    }
}

void ecard_xiaozhi_session_stop(void) {
    s_ws_stopping = true;
    s_ota_abort = true;
    for (int i = 0; i < 100 && s_ota_running; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    s_retried_ota_this_session = false;
    s_ws_connected = false;
    s_listen_pending = false;
    s_listen_pending_until_us = 0;
    s_abort_queued = false;
    s_ws_tx_drop_count = 0;
    s_ws_tx_drop_log_us = 0;
    s_tx_stop = true;
    for (int i = 0; i < 50 && s_tx_task && !s_tx_done; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_tx_task && s_tx_done) {
        // 任务自己在退出前已经 vTaskDelete，句柄只用于状态判断。
        s_tx_task = NULL;
    }
    if (s_tx_q) {
        if (!s_tx_task) {
            vQueueDelete(s_tx_q);
            s_tx_q = NULL;
        } else {
            (void)xQueueReset(s_tx_q);
        }
    }
    // 先停掉音频任务再关编解码器，避免它拿着已释放的句柄。
    release_audio_worker();
    if (s_ws) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }
    ws_release_transports();
    s_ws_started = false;
    s_ws_dead = false;
    s_session_restart = false;
    s_connect_failed = false;
    s_down_since = 0;
    s_ws_stopping = false;
    state_lock();
    ++s_prompt_seq;
    s_session_id[0] = '\0';
    s_pending_prompt_valid = false;
    s_prompt_fallback_valid = false;
    s_follow_valid = false;
    s_follow_prompt[0] = '\0';
    s_last_prompt[0] = '\0';
    s_last_prompt_fallback[0] = '\0';
    state_unlock();
    s_prompt_retry_at = 0;
    s_hello_retry_at = 0;
    s_hello_retries = 0;
    s_tts_active = false;
    s_awaiting_tts = false;
    ++s_tts_gen;
    pcm_ring_reset();
    if (s_opus_dec) {
        esp_audio_dec_close(s_opus_dec);
        s_opus_dec = NULL;
    }
    if (s_opus_enc) {
        esp_audio_enc_close(s_opus_enc);
        s_opus_enc = NULL;
    }
    ecard_netlink_set_low_latency(false);
    ESP_LOGI(TAG, "小智会话已关闭，内存已释放");
}

void ecard_xiaozhi_ask_with_fallback(const char *text, const char *fallback) {
    if (!text || !text[0]) return;
    s_last_llm[0] = '\0';
    state_lock();
    snprintf(s_pending_prompt, sizeof(s_pending_prompt), "%s", text);
    // 记住这次提问：断流重连后自动重问一次，学生不用再按一次卡。
    snprintf(s_last_prompt, sizeof(s_last_prompt), "%s", text);
    if (fallback && fallback[0]) {
        snprintf(s_prompt_fallback, sizeof(s_prompt_fallback), "%s", fallback);
        snprintf(s_last_prompt_fallback, sizeof(s_last_prompt_fallback), "%s", fallback);
        s_prompt_fallback_valid = true;
    } else {
        s_prompt_fallback_valid = false;
        s_last_prompt_fallback[0] = '\0';
    }
    s_replay_count = 0;
    // 任何新提问都作废上一轮挂着的追问，避免把旧卡的例句念到新卡上。
    s_follow_valid = false;
    s_follow_prompt[0] = '\0';
    ++s_prompt_seq;
    s_pending_prompt_valid = true;
    state_unlock();
    // 下一次 xz_tx 轮询（≤50 ms）会发送；WS 未连上时保留到 session hello 之后。
}

void ecard_xiaozhi_ask(const char *text) {
    ecard_xiaozhi_ask_with_fallback(text, NULL);
}

void ecard_xiaozhi_ask_with_follow(const char *text, const char *follow) {
    if (!text || !text[0]) return;
    ecard_xiaozhi_ask(text);
    state_lock();
    if (follow && follow[0]) {
        snprintf(s_follow_prompt, sizeof(s_follow_prompt), "%s", follow);
        s_follow_valid = true;
    } else {
        s_follow_valid = false;
        s_follow_prompt[0] = '\0';
    }
    state_unlock();
    if (follow && follow[0]) {
        ESP_LOGI(TAG, "本轮回答结束后追问: %.60s", follow);
    }
}

/* 统一静态音频任务（22 KB 静态栈）在“TTS 解码”和“麦克风编码”两种模式间原地切换，
 * 并与 25 KB 静态编解码后备区互斥复用，彻底消除编码任务动态栈溢出与堆碎片化。 */
static bool ensure_audio_worker(void) {
    if (!ensure_pcm_lock()) return false;
    if (!s_audio_ack_sem) {
        s_audio_ack_sem = xSemaphoreCreateBinary();
    }
    if (!s_audio_ctrl_lock) {
        s_audio_ctrl_lock = xSemaphoreCreateMutex();
    }
    if (!s_opus_q) {
        s_opus_q = xQueueCreate(XZ_OPUS_QUEUE_LEN, sizeof(xz_opus_item_t));
    }
    if (!s_audio_ack_sem || !s_audio_ctrl_lock || !s_opus_q) {
        ESP_LOGE(TAG, "Opus 音频控制对象创建失败（堆 %u）",
                 (unsigned)esp_get_free_heap_size());
        return false;
    }
    if (!s_audio_task) {
        s_audio_stop = false;
        s_audio_done = false;
        s_audio_mode = XZ_AUDIO_DECODE;
        s_audio_req_mode = XZ_AUDIO_DECODE;
        s_audio_task = xTaskCreateStatic(xz_audio_task, "xz_audio",
                                         XZ_AUDIO_STACK_BYTES, NULL, 5,
                                         s_audio_stack, &s_audio_tcb);
        if (!s_audio_task) {
            ESP_LOGE(TAG, "Opus 音频任务创建失败");
            return false;
        }
    }
    return true;
}

static void release_audio_worker(void) {
    s_audio_stop = true;
    for (int i = 0; i < 50 && s_audio_task && !s_audio_done; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_audio_task) {
        if (s_audio_done) {
            s_audio_task = NULL;
        } else {
            ESP_LOGW(TAG, "Opus 音频任务未在 500ms 内退出");
        }
    }
    if (s_opus_q) {
        vQueueDelete(s_opus_q);
        s_opus_q = NULL;
    }
    if (s_pcm_lock) xSemaphoreTake(s_pcm_lock, portMAX_DELAY);
    s_enc_q = NULL;
    s_enc_fill = 0;
    s_audio_mode = XZ_AUDIO_DECODE;
    s_audio_req_mode = XZ_AUDIO_DECODE;
    if (s_pcm_lock) xSemaphoreGive(s_pcm_lock);
    if (s_opus_dec) {
        esp_audio_dec_close(s_opus_dec);
        s_opus_dec = NULL;
    }
    if (s_opus_enc) {
        esp_audio_enc_close(s_opus_enc);
        s_opus_enc = NULL;
    }
    pcm_ring_reset();
    s_tts_active = false;
    s_awaiting_tts = false;
    ++s_tts_gen;
}

static bool switch_audio_mode_timeout(xz_audio_mode_t target, uint32_t wait_ms) {
    if (xTaskGetCurrentTaskHandle() == s_audio_task) {
        s_audio_req_mode = target;
        return (s_audio_mode == target);
    }
    if (!s_audio_ctrl_lock || !s_audio_ack_sem) return false;
    if (xSemaphoreTake(s_audio_ctrl_lock, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        ESP_LOGW(TAG, "音频模式切换抢锁超时（目标=%d，当前=%d）",
                 (int)target, (int)s_audio_mode);
        return false;
    }
    if (!s_audio_task || s_audio_stop || s_audio_done) {
        xSemaphoreGive(s_audio_ctrl_lock);
        return false;
    }
    if (s_audio_mode == target && s_audio_req_mode == target) {
        xSemaphoreGive(s_audio_ctrl_lock);
        return true;
    }
    (void)xSemaphoreTake(s_audio_ack_sem, 0);
    s_audio_switch_ok = false;
    s_audio_req_mode = target;
    bool ok = false;
    if (xSemaphoreTake(s_audio_ack_sem, pdMS_TO_TICKS(wait_ms)) == pdTRUE) {
        ok = s_audio_switch_ok;
    } else {
        ESP_LOGW(TAG, "音频模式切换超时（目标=%d，当前=%d）",
                 (int)target, (int)s_audio_mode);
    }
    xSemaphoreGive(s_audio_ctrl_lock);
    return ok;
}

static bool switch_audio_mode(xz_audio_mode_t target) {
    return switch_audio_mode_timeout(target, 3500);
}

bool ecard_xiaozhi_listen_start(void) {
    if (!s_ws_connected) return false;
    char sid[64];
    state_lock();
    snprintf(sid, sizeof(sid), "%s", s_session_id);
    state_unlock();
    if (!sid[0]) return false;

    // 开始录音前：若上一轮 TTS 仍在播报或等待中，通过控制队列先发 XZ_TX_OP_ABORT_PLAYBACK
    //（绝不在按键/LVGL 锁线程内直接写 WebSocket），再让 28 KB 静态音频任务原地切到 Opus 编码模式。
    const bool need_abort = s_tts_active || s_pcm_count > 0 || s_awaiting_tts;
    s_tts_active = false;
    s_awaiting_tts = false;
    ++s_tts_gen;
    if (s_opus_q) (void)xQueueReset(s_opus_q);
    pcm_ring_reset();
    if (need_abort && !s_abort_queued) {
        char abort_json[128];
        snprintf(abort_json, sizeof(abort_json),
                 "{\"session_id\":\"%.40s\",\"type\":\"abort\"}", sid);
        s_abort_queued = true;
        if (!ws_send_ctrl(XZ_TX_OP_ABORT_PLAYBACK, 0, abort_json) || !s_tx_q) {
            s_abort_queued = false;
        }
    }

    const uint32_t seq = ++s_listen_seq;
    s_last_stt[0] = '\0';
    s_last_llm[0] = '\0';
    s_tts_audio_bytes = 0;
    s_enc_sent = 0;
    s_ws_tx_drop_count = 0;
    s_ws_tx_drop_log_us = 0;
    ecard_vad_reset(&s_vad);

    s_listen_pending = (s_tx_q != NULL);
    s_listen_pending_until_us = s_listen_pending ? (esp_timer_get_time() + 650000LL) : 0;
    if (!switch_audio_mode(XZ_AUDIO_ENCODE)) {
        s_listen_pending = false;
        s_listen_pending_until_us = 0;
        ESP_LOGW(TAG, "追问启动失败：Opus 编码器无法打开（堆 %u，最大连续块 %u）",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return false;
    }

    char json[224];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"start\","
             "\"mode\":\"manual\"}",
             sid);
    if (!ws_send_ctrl(XZ_TX_OP_LISTEN_START, seq, json) || !s_tx_q) {
        s_listen_pending = false;
        s_listen_pending_until_us = 0;
    }
    ESP_LOGI(TAG, "listen start（免持手动模式，seq=%u）", (unsigned)seq);
    return true;
}

bool ecard_xiaozhi_listen_had_audio(void) {
    return s_enc_sent > 0;
}

void ecard_xiaozhi_listen_stop(void) {
    const uint32_t seq = s_listen_seq;
    if (!s_ws_connected) {
        s_audio_req_mode = XZ_AUDIO_DECODE;
        return;
    }
    char sid[64];
    state_lock();
    snprintf(sid, sizeof(sid), "%s", s_session_id);
    state_unlock();
    if (!sid[0]) {
        s_audio_req_mode = XZ_AUDIO_DECODE;
        return;
    }
    char json[224];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"}",
             sid);
    if (!ws_send_ctrl(XZ_TX_OP_LISTEN_STOP, seq, json) || !s_tx_q) {
        s_audio_req_mode = XZ_AUDIO_DECODE;
    }
    // 编码任务的释放交给 xz_tx：它要等任务退出，不能卡住 LVGL 线程。
    ESP_LOGI(TAG, "listen stop（seq=%u）", (unsigned)seq);
}

void ecard_xiaozhi_listen_cancel(void) {
    const uint32_t seq = ++s_listen_seq;
    s_listen_pending = false;
    s_listen_pending_until_us = 0;
    ecard_xiaozhi_reset_mic();
    s_awaiting_tts = false;
    if (!s_ws_connected) {
        s_audio_req_mode = XZ_AUDIO_DECODE;
        return;
    }
    char sid[64];
    state_lock();
    snprintf(sid, sizeof(sid), "%s", s_session_id);
    state_unlock();
    if (!sid[0]) {
        s_audio_req_mode = XZ_AUDIO_DECODE;
        return;
    }
    char json[160];
    snprintf(json, sizeof(json),
             "{\"session_id\":\"%.40s\",\"type\":\"abort\"}", sid);
    if (!ws_send_ctrl(XZ_TX_OP_LISTEN_CANCEL, seq, json) || !s_tx_q) {
        s_audio_req_mode = XZ_AUDIO_DECODE;
    }
    ESP_LOGI(TAG, "listen cancel（无有效人声或主动取消，seq=%u）", (unsigned)seq);
}

void ecard_xiaozhi_send_audio(const uint8_t *opus, size_t len) {
    ws_send_binary(opus, len);
}

bool ecard_xiaozhi_ws_connected(void) {
    return s_ws_connected;
}

bool ecard_xiaozhi_is_busy(void) {
    return s_ota_running || s_ws_started || s_ws_connected;
}

bool ecard_xiaozhi_connect_failed(void) {
    return s_connect_failed;
}

/** 会话期间每 5 秒打印一次音频收发/内存快照，用于定位“断流”发生在哪一侧。 */
void ecard_xiaozhi_log_stats(void) {
    if (!s_started) return;
    static int64_t last_us;
    const int64_t now_us = esp_timer_get_time();
    if (last_us && now_us - last_us < 5 * 1000 * 1000) return;
    last_us = now_us;
    if (!s_ws_connected && !s_tts_active) return;
    ESP_LOGI(TAG,
             "统计: ws=%d tts=%d 收帧=%u 收字节=%u 解码帧=%u 样本=%d PCM=%d "
             "队列丢=%u 溢出=%u 停读=%dms 堆=%u 最大块=%u 编码器=%d",
             (int)s_ws_connected, (int)s_tts_active,
             (unsigned)s_rx_audio_frames, (unsigned)s_rx_audio_bytes,
             (unsigned)s_decoded_frames, s_pcm_decoded, s_pcm_count,
             (unsigned)s_audio_queue_drops, (unsigned)s_pcm_overflows,
             s_last_rx_us ? (int)((now_us - s_last_rx_us) / 1000) : 0,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             s_opus_enc ? 1 : 0);
    // TTS 期间连续 8 秒收不到任何二进制帧：说明下行真的停了（服务端断流
    // 或本机长时间不读 socket），要在串口上留下证据，而不是等 RST。
    if (s_tts_active && s_last_rx_us && (now_us - s_last_rx_us) > 8 * 1000 * 1000) {
        ESP_LOGW(TAG, "TTS 期间已 %d ms 未收到音频帧（收帧=%u 解码帧=%u）",
                 (int)((now_us - s_last_rx_us) / 1000),
                 (unsigned)s_rx_audio_frames, (unsigned)s_decoded_frames);
    }
}

/** hello 看门狗请求重启会话（应用层 tick 调用，返回 true 后自行 start/stop）。 */
bool ecard_xiaozhi_restart_pending(void) {
    if (!s_session_restart) return false;
    s_session_restart = false;
    // 看门狗判死时连接多半已半死：这里不再留恋，stop 会整体释放重建。
    return true;
}

bool ecard_xiaozhi_session_ready(void) {
    if (!s_ws_connected) return false;
    bool ready;
    state_lock();
    ready = s_session_id[0] != '\0';
    state_unlock();
    return ready;
}

const char *ecard_xiaozhi_last_stt(void) {
    return s_last_stt;
}

const char *ecard_xiaozhi_last_llm(void) {
    return s_last_llm;
}

const char *ecard_xiaozhi_last_event(void) {
    return s_last_event;
}

const char *ecard_xiaozhi_last_tts_state(void) {
    return s_last_tts_state;
}

int ecard_xiaozhi_tts_bytes(void) {
    return s_tts_audio_bytes;
}

bool ecard_xiaozhi_is_ready(void) {
    return s_ready;
}

const char *ecard_xiaozhi_activation_code(void) {
    return s_activation;
}

const char *ecard_xiaozhi_websocket_url(void) {
    return s_ready ? s_ws_url : "";
}
