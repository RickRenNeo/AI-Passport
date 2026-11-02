// main/tianji_net.c — 《天机·六爻》网络连接与 oMLX (MiniCPM 5) 请求实现
#include "tianji_net.h"
#include "tianji_net_config.h"

#include <string.h>
#include <stdio.h>
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

    // 1. 初始化 NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
        return err;
    }

    // 2. 初始化网络栈
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
        ESP_LOGI(TAG, "启用联机 AI 模式，启动 Wi-Fi 栈...");
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

// 异步大模型请求任务
static void omlx_ai_task(void *pvParameters) {
    (void)pvParameters;
    s_net_state = TIANJI_NET_STATE_INFERENCING;
    if (s_ai_callback) {
        s_ai_callback(s_net_state, "正在连接 oMLX 大模型...");
    }

    const tianji_hexagram_t *prim = tianji_get_hexagram(s_ai_session_copy.primary_bits);
    const tianji_hexagram_t *trans = tianji_get_hexagram(s_ai_session_copy.transformed_bits);

    // 构建提问 Prompt
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
        "给出全盘综合解断与行事指南（200字以内，重点讲清当下态势与应对之策）。直接给出解断正文。",
        prim->name, prim->upper_name, prim->lower_name,
        trans->name, trans->upper_name, trans->lower_name,
        moving_desc, prim->judgment);

    // 构建 OpenAI 标准兼容的 JSON 请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", TIANJI_OMLX_MODEL);
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", "你是一位精通周易六爻的国学大师。言语古朴精炼，一针见血，直接输出卦象解断，绝不废话，切勿做自我介绍或问候。");
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

    ESP_LOGI(TAG, "正在向 oMLX 发起请求: %s", TIANJI_OMLX_URL);

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
        snprintf(s_ai_result, sizeof(s_ai_result),
            "错误：无法连通服务端\n\n"
            "端点：%s\n"
            "原因：网络未连通或连接被拒绝。\n"
            "建议：请确认手机热点网络畅通。", TIANJI_OMLX_HOST);
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "oMLX 响应状态码: %d, 内容长度: %d", status_code, content_length);

    if (status_code == 401) {
        esp_http_client_cleanup(client);
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "401 密钥鉴权失败\n\n请核验 oMLX API Key");
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (status_code != 200) {
        esp_http_client_cleanup(client);
        s_net_state = TIANJI_NET_STATE_INFER_ERROR;
        snprintf(s_ai_result, sizeof(s_ai_result), "服务异常 (HTTP %d)\n\n请确认 MiniCPM5 模型已加载", status_code);
        if (s_ai_callback) s_ai_callback(s_net_state, s_ai_result);
        s_ai_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    // 接收服务端返回的 JSON 数据
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

    // 解析 JSON: choices[0].message.content
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

    if (s_ai_callback) {
        s_ai_callback(s_net_state, s_ai_result);
    }

    s_ai_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t tianji_net_request_ai(const tianji_session_t *session, tianji_net_callback_t cb) {
    if (s_ai_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE; // 已有任务正在运行
    }
    s_ai_session_copy = *session;
    s_ai_callback = cb;

    BaseType_t ret = xTaskCreate(omlx_ai_task, "tianji_omlx", 6144, NULL, 5, &s_ai_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 AI 推理任务失败 (内存不足)");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
