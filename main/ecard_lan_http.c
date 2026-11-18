#include "ecard_lan_http.h"
#include "bsp_display.h"
#include "ecard_cat_room.h"
#include "ecard_netlink.h"
#include "ecard_portal.h"
#include "ecard_setup.h"
#include "ecard_ui.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/ip4_addr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ecard_lan_http"
#define LAN_HTTP_RESP_MAX 4096
#define LAN_HTTP_BODY_MAX 512

typedef struct {
    char resp[LAN_HTTP_RESP_MAX];
    char body[LAN_HTTP_BODY_MAX];
    char path[96];
    char auth[128];
} lan_http_scratch_t;

static httpd_handle_t s_server;
static lan_http_scratch_t *s_scratch;
static bool s_ai_busy;
static SemaphoreHandle_t s_ctrl_mutex;
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

static void ctrl_lock(void) {
    if (!s_ctrl_mutex) {
        SemaphoreHandle_t created = xSemaphoreCreateMutex();
        taskENTER_CRITICAL(&s_spinlock);
        if (!s_ctrl_mutex) {
            s_ctrl_mutex = created;
            created = NULL;
        }
        taskEXIT_CRITICAL(&s_spinlock);
        if (created) vSemaphoreDelete(created);
    }
    if (s_ctrl_mutex) (void)xSemaphoreTake(s_ctrl_mutex, portMAX_DELAY);
}

static void ctrl_unlock(void) {
    if (s_ctrl_mutex) (void)xSemaphoreGive(s_ctrl_mutex);
}

static const char *status_text(int code) {
    switch (code) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 409: return "409 Conflict";
    case 429: return "429 Too Many Requests";
    default: return "500 Internal Server Error";
    }
}

static void get_device_id(char *out, size_t out_size) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_size, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool read_req_body(httpd_req_t *req, char *buf, size_t cap) {
    if (!req || !buf || cap == 0) return false;
    buf[0] = '\0';
    if (req->content_len == 0) return true;
    if (req->content_len >= cap) return false;

    size_t received = 0;
    while (received < req->content_len) {
        const int got = httpd_req_recv(req, buf + received,
                                       req->content_len - received);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (got <= 0) {
            return false;
        }
        received += (size_t)got;
    }
    buf[received] = '\0';
    return true;
}

static esp_err_t api_handler(httpd_req_t *req) {
    if (!s_scratch) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    }

    const size_t path_len = strcspn(req->uri, "?");
    if (path_len >= sizeof(s_scratch->path)) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"not_found\"}",
                               HTTPD_RESP_USE_STRLEN);
    }
    memcpy(s_scratch->path, req->uri, path_len);
    s_scratch->path[path_len] = '\0';

    s_scratch->auth[0] = '\0';
    const size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (auth_len > 0 && auth_len < sizeof(s_scratch->auth)) {
        (void)httpd_req_get_hdr_value_str(req, "Authorization",
                                          s_scratch->auth, sizeof(s_scratch->auth));
    }

    if (!read_req_body(req, s_scratch->body, sizeof(s_scratch->body))) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"invalid_body\"}",
                               HTTPD_RESP_USE_STRLEN);
    }

    char device_id[16];
    char ip_str[20] = "0.0.0.0";
    get_device_id(device_id, sizeof(device_id));
    (void)ecard_lan_http_get_ip(ip_str, sizeof(ip_str));

    const char *method = "OTHER";
    if (req->method == HTTP_GET) {
        method = "GET";
    } else if (req->method == HTTP_POST) {
        method = "POST";
    }

    bool ui_dirty = false;
    const int status = ecard_cat_room_handle_http(
        method, s_scratch->path, s_scratch->auth, s_scratch->body,
        device_id, ip_str, 80, s_scratch->resp, sizeof(s_scratch->resp),
        &ui_dirty);

    if (ui_dirty && bsp_lvgl_lock(100)) {
        const ecard_ui_page_t page = ecard_ui_get_page();
        if (page == ECARD_UI_PAGE_POMODORO) {
            ecard_ui_pomodoro_refresh();
        } else if (page == ECARD_UI_PAGE_PAIR) {
            ecard_ui_pair_refresh();
        } else if (page == ECARD_UI_PAGE_POEM_PICK || page == ECARD_UI_PAGE_WORD_PICK) {
            ecard_ui_show_page(page);
        } else if (page == ECARD_UI_PAGE_DEVICE) {
            ecard_ui_refresh_content();
        }
        bsp_lvgl_unlock();
    }

    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_scratch->resp, HTTPD_RESP_USE_STRLEN);
}

bool ecard_lan_http_get_ip(char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) return false;
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(sta, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    snprintf(out, out_size, IPSTR, IP2STR(&info.ip));
    return true;
}

static bool start_locked(void) {
    if (s_server) return true;
    if (s_ai_busy || !ecard_setup_is_provisioned() ||
        ecard_portal_is_active() || !ecard_netlink_is_connected()) {
        return false;
    }

    if (!s_scratch) {
        s_scratch = calloc(1, sizeof(*s_scratch));
        if (!s_scratch) {
            ESP_LOGE(TAG, "无法分配局域网 HTTP 响应缓冲");
            return false;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 3;
    config.stack_size = 6144;
    config.recv_wait_timeout = 2;
    config.send_wait_timeout = 2;
    config.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "STA 局域网 HTTP 启动失败");
        free(s_scratch);
        s_scratch = NULL;
        s_server = NULL;
        return false;
    }

    httpd_uri_t get_api = {
        .uri = "/api/*",
        .method = HTTP_GET,
        .handler = api_handler,
        .user_ctx = NULL,
    };
    httpd_uri_t post_api = {
        .uri = "/api/*",
        .method = HTTP_POST,
        .handler = api_handler,
        .user_ctx = NULL,
    };
    (void)httpd_register_uri_handler(s_server, &get_api);
    (void)httpd_register_uri_handler(s_server, &post_api);

    char ip_str[20] = "0.0.0.0";
    (void)ecard_lan_http_get_ip(ip_str, sizeof(ip_str));
    ESP_LOGI(TAG, "STA 局域网服务已启动: http://%s:80/api/*", ip_str);
    return true;
}

static void stop_locked(void) {
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "STA 局域网服务已停止");
    }
    if (s_scratch) {
        free(s_scratch);
        s_scratch = NULL;
    }
}

bool ecard_lan_http_start(void) {
    ctrl_lock();
    const bool ok = start_locked();
    ctrl_unlock();
    return ok;
}

void ecard_lan_http_stop(void) {
    ctrl_lock();
    stop_locked();
    ctrl_unlock();
}

bool ecard_lan_http_is_running(void) {
    ctrl_lock();
    const bool running = (s_server != NULL);
    ctrl_unlock();
    return running;
}

void ecard_lan_http_set_ai_busy(bool busy) {
    ctrl_lock();
    s_ai_busy = busy;
    if (busy) {
        stop_locked();
    } else {
        (void)start_locked();
    }
    ctrl_unlock();
}
