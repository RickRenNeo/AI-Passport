#include "ecard_netlink.h"
#include "ecard_lan_http.h"
#include "ecard_network.h"
#include "ecard_portal.h"
#include "ecard_setup.h"
#include "ecard_time.h"
#include "ecard_xiaozhi.h"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define TAG "ecard_netlink"

static bool s_inited;
static bool s_started;
static bool s_connected;
static bool s_pending_apply;
static bool s_scanning;
static bool s_reconnecting;
static uint32_t s_elapsed_ms;
static uint8_t s_fail_count;
static bool s_portal_open;
static char s_ssid[36];

/**
 * 断线后的下一轮重试间隔：刚断开时快速重试，连续失败后才退到 2 分钟。
 * 首次开机若赶上信道/AP 忙，5 秒内就会再试一次，而不是让用户干等两分钟。
 */
static uint32_t retry_delay_ms(void) {
    if (!ecard_setup_has_wifi() && s_portal_open &&
        s_fail_count >= ECARD_NETLINK_FAILS_BEFORE_PORTAL) {
        return ECARD_NETLINK_RETRY_MS;
    }
    switch (s_fail_count) {
    case 0:
    case 1:
        return ECARD_NETLINK_FAST_RETRY_MS;
    case 2:
        return 2 * ECARD_NETLINK_FAST_RETRY_MS;
    case 3:
        return 4 * ECARD_NETLINK_FAST_RETRY_MS;
    case 4:
        return 6 * ECARD_NETLINK_FAST_RETRY_MS;
    default:
        return ECARD_NETLINK_RETRY_MS;
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    if (id == WIFI_EVENT_STA_START) {
        if (s_ssid[0]) {
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc =
            (const wifi_event_sta_disconnected_t *)data;
        s_connected = false;
        s_ssid[0] = '\0';
        s_elapsed_ms = 0;
        ecard_lan_http_stop();
        if (s_scanning || s_reconnecting) {
            // 扫描或切换凭据前的主动断开：不计数、不提示，避免配网网页误报失败。
            s_reconnecting = false;
            return;
        }
        // 严格按“每 2 分钟重试一次”执行；配网页在首次开机时已并行开启，
        // 不会因为等待重试而挡住用户配置。
        if (s_fail_count < 255) ++s_fail_count;
        ESP_LOGW(TAG, "Wi-Fi 断开，第 %u 次失败，原因码 %u", (unsigned)s_fail_count,
                 disc ? (unsigned)disc->reason : 0u);
    } else if (id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        s_fail_count = 0;
        s_reconnecting = false;
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "Wi-Fi 已连接: %s，信道 %u，RSSI %d dBm，认证 %u", s_ssid,
                     (unsigned)ap.primary, (int)ap.rssi, (unsigned)ap.authmode);
        } else {
            ESP_LOGI(TAG, "Wi-Fi 已连接: %s", s_ssid);
        }
        // 只有 Wi-Fi 与身份信息都已完成配置后才关闭热点与门户；
        // 否则（例如默认 Wi-Fi 连上，或刚保存新 Wi-Fi 准备填身份）必须保留热点。
        if (ecard_setup_is_provisioned() &&
            (s_portal_open || ecard_portal_is_active())) {
            ecard_portal_stop();
            s_portal_open = false;
        }
        ecard_time_start_sync();
        ecard_network_request_sync();
        ecard_xiaozhi_start();
        if (ecard_setup_is_provisioned() && !ecard_portal_is_active()) {
            (void)ecard_lan_http_start();
        }
    }
}

static void load_credentials(char *ssid, size_t ssid_size, char *pass, size_t pass_size) {
    const ecard_setup_t *cfg = ecard_setup_get();
    if (cfg->has_wifi) {
        snprintf(ssid, ssid_size, "%s", cfg->wifi_ssid);
        snprintf(pass, pass_size, "%s", cfg->wifi_pass);
    } else {
        snprintf(ssid, ssid_size, "%s", ECARD_DEFAULT_WIFI_SSID);
        snprintf(pass, pass_size, "%s", ECARD_DEFAULT_WIFI_PASS);
    }
}

static void apply_credentials(void) {
    char ssid[36];
    char pass[68];
    load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (!ssid[0]) {
        s_ssid[0] = '\0';
        ESP_LOGI(TAG, "未配置 Wi-Fi 凭据，等待手机配网");
        return;
    }

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, 32));
    memcpy(cfg.sta.password, pass, strnlen(pass, 64));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);

    ESP_LOGI(TAG, "尝试连接 Wi-Fi: %s", ssid);
    if (s_started) {
        esp_wifi_connect();
    }
}

bool ecard_netlink_init(void) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init_cfg) != ESP_OK) return false;

    // 中国路由器的 2.4 GHz 常用 12/13 信道，默认 "world" 国家码不允许主动扫描，
    // 会让设备在 AP 换信道后彻底找不到网络（reason 201）。与北京时间一致设为 CN。
    const esp_err_t cc_err = esp_wifi_set_country_code("CN", true);
    if (cc_err != ESP_OK) {
        ESP_LOGW(TAG, "设置 Wi-Fi 国家码 CN 失败: 0x%x", (unsigned)cc_err);
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event,
                                        NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event,
                                        NULL, NULL);

    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) return false;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;

    // 先写入凭据再启动，WIFI_EVENT_STA_START 会发起第一次连接。
    apply_credentials();
    if (esp_wifi_start() != ESP_OK) return false;
    s_started = true;
    s_inited = true;
    s_elapsed_ms = 0;
    ESP_LOGI(TAG, "Wi-Fi 已启动，断线后先快速重试，随后每 %u 秒一次",
             (unsigned)(ECARD_NETLINK_RETRY_MS / 1000));
    return true;
}

void ecard_netlink_tick(void) {
    if (!s_inited) return;

    // 配网页可能由 main 或本模块开启；统一记录，避免重复拉起。
    if (!s_portal_open && ecard_portal_is_active()) {
        s_portal_open = true;
    }

    if (s_pending_apply) {
        s_pending_apply = false;
        apply_credentials();
        return;
    }

    if (s_connected) return;

    if (s_elapsed_ms < retry_delay_ms()) {
        s_elapsed_ms += 1000;
        return;
    }
    s_elapsed_ms = 0;

    if (!ecard_setup_is_provisioned() &&
        s_fail_count >= ECARD_NETLINK_FAILS_BEFORE_PORTAL && !s_portal_open) {
        ESP_LOGW(TAG, "默认 Wi-Fi 连续失败 %u 次，转入手机配网", (unsigned)s_fail_count);
        if (ecard_portal_start()) {
            s_portal_open = true;
        }
        return;
    }

    apply_credentials();
}

bool ecard_netlink_is_connected(void) {
    return s_connected;
}

uint8_t ecard_netlink_fail_count(void) {
    return s_fail_count;
}

const char *ecard_netlink_ssid(void) {
    return s_connected ? s_ssid : "";
}

void ecard_netlink_reconnect(void) {
    s_fail_count = 0;
    s_elapsed_ms = 0;
    // 不在 HTTP 处理函数里直接重连：先让配网页把“正在连接”发回手机，
    // 下一个 tick 再应用新凭据，避免 AP/STA 切信道时把响应截断。
    s_pending_apply = true;
    // 未连接时不要主动 disconnect，否则会多出一次“第 1 次失败”，
    // 让配网页误报密码错误。
    if (s_inited && s_started && s_connected) {
        s_reconnecting = true;
        (void)esp_wifi_disconnect();
    }
}

void ecard_netlink_set_low_latency(bool on) {
    if (!s_inited || !s_started) return;
    const esp_err_t err = esp_wifi_set_ps(on ? WIFI_PS_NONE : WIFI_PS_MIN_MODEM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "设置 Wi-Fi 省电模式失败: 0x%x", (unsigned)err);
    }
}

void ecard_netlink_scan_begin(void) {
    if (!s_inited || !s_started) return;
    s_scanning = true;
    s_elapsed_ms = 0;
    s_pending_apply = false;
    // 无论已连接还是正在连接，先主动断开/中止，避免 esp_wifi_scan_start 报 ESP_ERR_WIFI_STATE。
    (void)esp_wifi_disconnect();
}

void ecard_netlink_scan_end(void) {
    if (!s_inited || !s_started) return;
    s_scanning = false;
    s_elapsed_ms = 0;
    // 扫描结束后立即用现有凭据再接回去；没有这个动作，设备会一直空闲
    // 到下一轮重试，首页就会长时间显示“网络未连接”。
    s_pending_apply = true;
}
