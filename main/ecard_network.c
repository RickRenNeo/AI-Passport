#include "ecard_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include "ecard_netlink.h"
#include "ecard_setup.h"
#include "ecard_time.h"
#include "ecard_xiaozhi.h"
#endif

static ecard_wifi_credentials_t s_creds;
static ecard_acl_t s_acl;
static bool s_net_inited = false;

// 标准学校作息课间时段表 (上午与下午各两个主要课间及大课间)
static const ecard_schedule_period_t k_recess_periods[] = {
    {  9, 40, 10,  0 }, // 上午大课间 09:40 ~ 10:00
    { 11, 40, 12, 10 }, // 午放学时段 11:40 ~ 12:10
    { 14, 40, 14, 55 }, // 下午小课间 14:40 ~ 14:55
    { 15, 40, 16,  5 }, // 下午大课间 15:40 ~ 16:05
    { 17, 30, 18, 30 }  // 傍晚放学时段 17:30 ~ 18:30
};

static const size_t k_recess_period_count = sizeof(k_recess_periods) / sizeof(k_recess_periods[0]);

#if defined(ESP_PLATFORM)
static const char *TAG = "ecard_net";
static TaskHandle_t s_sync_task = NULL;
static SemaphoreHandle_t s_acl_mutex = NULL;
static volatile bool s_sync_dirty = false;
static volatile bool s_sync_requested = false;
static volatile bool s_relay_online = false;
static volatile bool s_sos_pending = false;
static char s_sos_student_id[24];
static char s_sos_location[64];
static char s_discovered_relay_url[128];

static void acl_lock(void) {
    if (!s_acl_mutex) {
        s_acl_mutex = xSemaphoreCreateMutex();
    }
    if (s_acl_mutex) {
        (void)xSemaphoreTake(s_acl_mutex, portMAX_DELAY);
    }
}

static void acl_unlock(void) {
    if (s_acl_mutex) {
        (void)xSemaphoreGive(s_acl_mutex);
    }
}
#else
static void acl_lock(void) {}
static void acl_unlock(void) {}
#endif

void ecard_network_init(void) {
    if (s_net_inited) return;

    memset(&s_creds, 0, sizeof(s_creds));
    strncpy(s_creds.ssid, ECARD_DEFAULT_CAMPUS_SSID, sizeof(s_creds.ssid) - 1);
    strncpy(s_creds.password, ECARD_DEFAULT_CAMPUS_PASS, sizeof(s_creds.password) - 1);
    s_creds.channel = 0;
    s_creds.has_bssid = false;

    memset(&s_acl, 0, sizeof(s_acl));
    s_net_inited = true;

#if defined(ESP_PLATFORM)
    const ecard_setup_t *cfg = ecard_setup_get();
    if (cfg && ecard_setup_has_identity()) {
        acl_lock();
        snprintf(s_acl.primary_phone, sizeof(s_acl.primary_phone), "%s", cfg->phone);
        snprintf(s_acl.student_school, sizeof(s_acl.student_school), "%s", cfg->school);
        snprintf(s_acl.student_class, sizeof(s_acl.student_class), "%s", cfg->class_name);
        acl_unlock();
    }
#endif
}

const ecard_wifi_credentials_t *ecard_network_get_credentials(void) {
    if (!s_net_inited) ecard_network_init();
    return &s_creds;
}

void ecard_network_set_credentials(const char *ssid, const char *password) {
    if (!s_net_inited) ecard_network_init();
    if (ssid && strlen(ssid) > 0) {
        strncpy(s_creds.ssid, ssid, sizeof(s_creds.ssid) - 1);
        s_creds.ssid[sizeof(s_creds.ssid) - 1] = '\0';
    }
    if (password) {
        strncpy(s_creds.password, password, sizeof(s_creds.password) - 1);
        s_creds.password[sizeof(s_creds.password) - 1] = '\0';
    }
}

void ecard_network_set_acl(const ecard_acl_t *acl) {
    if (!s_net_inited) ecard_network_init();
    if (!acl) return;
    acl_lock();
    s_acl = *acl;
    acl_unlock();
}

const ecard_acl_t *ecard_network_get_acl(void) {
    if (!s_net_inited) ecard_network_init();
    return &s_acl;
}

ecard_sender_role_t ecard_network_parse_role(const char *role_str) {
    if (!role_str) return ECARD_SENDER_UNKNOWN;
    if (strcmp(role_str, "primary_parent") == 0) return ECARD_SENDER_PRIMARY_PARENT;
    if (strcmp(role_str, "family") == 0) return ECARD_SENDER_FAMILY;
    if (strcmp(role_str, "teacher") == 0) return ECARD_SENDER_TEACHER;
    if (strcmp(role_str, "school_admin") == 0) return ECARD_SENDER_SCHOOL_ADMIN;
    if (strcmp(role_str, "student") == 0) return ECARD_SENDER_STUDENT;
    return ECARD_SENDER_UNKNOWN;
}

bool ecard_network_is_sender_authorized(ecard_sender_role_t role,
                                        const char *phone,
                                        const char *school,
                                        const char *class_name) {
    if (!s_net_inited) ecard_network_init();

    acl_lock();
    ecard_acl_t acl = s_acl;
    acl_unlock();

    switch (role) {
    case ECARD_SENDER_PRIMARY_PARENT:
        return phone && phone[0] && acl.primary_phone[0] &&
               strcmp(phone, acl.primary_phone) == 0;

    case ECARD_SENDER_FAMILY:
        if (!phone || !phone[0]) return false;
        if (acl.primary_phone[0] && strcmp(phone, acl.primary_phone) == 0) {
            return true;
        }
        for (uint8_t i = 0; i < acl.family_count && i < ECARD_ACL_MAX_FAMILY; ++i) {
            if (acl.family[i].phone[0] && strcmp(phone, acl.family[i].phone) == 0) {
                return true;
            }
        }
        return false;

    case ECARD_SENDER_TEACHER:
        if (acl.student_school[0] &&
            (!school || strcmp(school, acl.student_school) != 0)) {
            return false;
        }
        if (acl.student_class[0] &&
            (!class_name || strcmp(class_name, acl.student_class) != 0)) {
            return false;
        }
        if (acl.teacher_count > 0) {
            for (uint8_t i = 0; i < acl.teacher_count && i < ECARD_ACL_MAX_TEACHERS; ++i) {
                const bool school_ok = !acl.teachers[i].school[0] || !school ||
                                       strcmp(acl.teachers[i].school, school) == 0;
                const bool class_ok = !acl.teachers[i].class_name[0] || !class_name ||
                                      strcmp(acl.teachers[i].class_name, class_name) == 0;
                const bool phone_ok = !phone || !phone[0] || !acl.teachers[i].phone[0] ||
                                      strcmp(acl.teachers[i].phone, phone) == 0;
                if (school_ok && class_ok && phone_ok) return true;
            }
            return false;
        }
        return (school && school[0]) || (class_name && class_name[0]);

    case ECARD_SENDER_SCHOOL_ADMIN:
        if (acl.student_school[0] &&
            (!school || strcmp(school, acl.student_school) != 0)) {
            return false;
        }
        if (acl.admin_count > 0) {
            for (uint8_t i = 0; i < acl.admin_count && i < ECARD_ACL_MAX_ADMINS; ++i) {
                const bool school_ok = !acl.admins[i].school[0] || !school ||
                                       strcmp(acl.admins[i].school, school) == 0;
                const bool phone_ok = !phone || !phone[0] || !acl.admins[i].phone[0] ||
                                      strcmp(acl.admins[i].phone, phone) == 0;
                if (school_ok && phone_ok) return true;
            }
            return false;
        }
        return school && school[0];

    case ECARD_SENDER_STUDENT:
    case ECARD_SENDER_UNKNOWN:
    default:
        return false;
    }
}

bool ecard_network_is_recess_time(uint8_t hour, uint8_t minute) {
    uint32_t current_minutes = (uint32_t)hour * 60 + minute;

    for (size_t i = 0; i < k_recess_period_count; ++i) {
        uint32_t start_m = (uint32_t)k_recess_periods[i].start_hour * 60 + k_recess_periods[i].start_min;
        uint32_t end_m = (uint32_t)k_recess_periods[i].end_hour * 60 + k_recess_periods[i].end_min;

        if (current_minutes >= start_m && current_minutes < end_m) {
            return true;
        }
    }

    return false;
}

#if defined(ESP_PLATFORM)

static void device_id_str(char *out, size_t size) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void url_encode(const char *src, char *dst, size_t dst_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    for (const unsigned char *p = (const unsigned char *)src; *p; ++p) {
        const unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            if (out + 2 > dst_size) break;
            dst[out++] = (char)c;
        } else {
            if (out + 4 > dst_size) break;
            dst[out++] = '%';
            dst[out++] = hex[(c >> 4) & 0x0F];
            dst[out++] = hex[c & 0x0F];
        }
    }
    dst[out] = '\0';
}

static bool discover_lan_relay(char *out_url, size_t out_size) {
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return false;

    int broadcast = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    struct timeval tv = { .tv_sec = 0, .tv_usec = 650000 };
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(0);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    (void)bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));

    static const char probe[] = "AI_PASSPORT_RELAY_DISCOVER";
    struct sockaddr_in dst_addr;
    memset(&dst_addr, 0, sizeof(dst_addr));
    dst_addr.sin_family = AF_INET;
    dst_addr.sin_port = htons(ECARD_RELAY_DISCOVER_PORT);

    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK && ip_info.netmask.addr != 0) {
        dst_addr.sin_addr.s_addr = (ip_info.ip.addr & ip_info.netmask.addr) | (~ip_info.netmask.addr);
        (void)sendto(sock, probe, sizeof(probe) - 1, 0,
                     (struct sockaddr *)&dst_addr, sizeof(dst_addr));
    }

    dst_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    (void)sendto(sock, probe, sizeof(probe) - 1, 0,
                 (struct sockaddr *)&dst_addr, sizeof(dst_addr));

    char buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    const int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           (struct sockaddr *)&from, &from_len);
    close(sock);
    if (n <= 0) return false;

    buf[n] = '\0';
    cJSON *root = cJSON_Parse(buf);
    if (!root) return false;
    const cJSON *svc = cJSON_GetObjectItem(root, "service");
    const cJSON *url = cJSON_GetObjectItem(root, "url");
    bool ok = false;
    if (cJSON_IsString(svc) && strcmp(svc->valuestring, "ai-passport-relay") == 0 &&
        cJSON_IsString(url) && url->valuestring[0]) {
        snprintf(out_url, out_size, "%s", url->valuestring);
        ok = true;
    }
    cJSON_Delete(root);
    return ok;
}

static bool resolve_relay_url(char *out_url, size_t out_size) {
    const char *configured = ecard_setup_get_relay_url();
    if (configured && configured[0]) {
        snprintf(out_url, out_size, "%s", configured);
        return true;
    }
    if (s_discovered_relay_url[0]) {
        snprintf(out_url, out_size, "%s", s_discovered_relay_url);
        return true;
    }
    if (discover_lan_relay(s_discovered_relay_url, sizeof(s_discovered_relay_url))) {
        ESP_LOGI(TAG, "自动发现局域网中转服务: %s", s_discovered_relay_url);
        snprintf(out_url, out_size, "%s", s_discovered_relay_url);
        return true;
    }
    if (ECARD_DEFAULT_RELAY_URL[0] != '\0') {
        snprintf(s_discovered_relay_url, sizeof(s_discovered_relay_url), "%s",
                 ECARD_DEFAULT_RELAY_URL);
        ESP_LOGI(TAG, "使用固件内置默认中转服务: %s", s_discovered_relay_url);
        snprintf(out_url, out_size, "%s", s_discovered_relay_url);
        return true;
    }
    return false;
}

static int http_json_post(const char *url, const char *body, char *resp, size_t resp_size) {
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = (strncmp(url, "https://", 8) == 0) ? esp_crt_bundle_attach : NULL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return -1;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    const size_t body_len = body ? strlen(body) : 0;
    if (esp_http_client_open(client, (int)body_len) != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }
    if (body_len > 0 && esp_http_client_write(client, body, (int)body_len) != (int)body_len) {
        esp_http_client_cleanup(client);
        return -1;
    }
    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);

    int total = 0;
    if (resp && resp_size > 1) {
        int r = 0;
        while (total < (int)resp_size - 1 &&
               (r = esp_http_client_read(client, resp + total, (int)resp_size - 1 - total)) > 0) {
            total += r;
        }
        resp[total] = '\0';
    }
    esp_http_client_cleanup(client);
    return status;
}

static bool download_audio_to_file(const char *url, const char *dst_path, size_t *out_bytes) {
    if (out_bytes) *out_bytes = 0;
    FILE *fp = fopen(dst_path, "wb");
    if (!fp) return false;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .crt_bundle_attach = (strncmp(url, "https://", 8) == 0) ? esp_crt_bundle_attach : NULL,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        fclose(fp);
        (void)remove(dst_path);
        return false;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        fclose(fp);
        (void)remove(dst_path);
        return false;
    }
    (void)esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        esp_http_client_cleanup(client);
        fclose(fp);
        (void)remove(dst_path);
        return false;
    }

    uint8_t chunk[512];
    size_t total = 0;
    bool write_ok = true;
    int r = 0;
    while ((r = esp_http_client_read(client, (char *)chunk, sizeof(chunk))) > 0) {
        if (fwrite(chunk, 1, (size_t)r, fp) != (size_t)r) {
            write_ok = false;
            break;
        }
        total += (size_t)r;
    }
    (void)fflush(fp);
    (void)fclose(fp);
    esp_http_client_cleanup(client);

    if (!write_ok || total == 0) {
        (void)remove(dst_path);
        return false;
    }
    if (out_bytes) *out_bytes = total;
    return true;
}

static void sync_time_from_json(const cJSON *root) {
    const cJSON *st = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsNumber(st) && st->valuedouble > 1700000000.0) {
        const int64_t epoch = (int64_t)st->valuedouble;
        ecard_time_set_epoch(epoch);
        ecard_messages_sync_epoch(epoch);
    }
}

static void update_acl_from_json(const cJSON *acl_obj, const ecard_setup_t *cfg) {
    ecard_acl_t acl;
    memset(&acl, 0, sizeof(acl));
    if (cfg) {
        snprintf(acl.primary_phone, sizeof(acl.primary_phone), "%s", cfg->phone);
        snprintf(acl.student_school, sizeof(acl.student_school), "%s", cfg->school);
        snprintf(acl.student_class, sizeof(acl.student_class), "%s", cfg->class_name);
    }
    if (!cJSON_IsObject(acl_obj)) {
        ecard_network_set_acl(&acl);
        return;
    }

    const cJSON *pphone = cJSON_GetObjectItem(acl_obj, "primary_phone");
    if (cJSON_IsString(pphone) && pphone->valuestring[0]) {
        snprintf(acl.primary_phone, sizeof(acl.primary_phone), "%s", pphone->valuestring);
    }

    const cJSON *family = cJSON_GetObjectItem(acl_obj, "family");
    if (cJSON_IsArray(family)) {
        const cJSON *it = NULL;
        cJSON_ArrayForEach(it, family) {
            if (acl.family_count >= ECARD_ACL_MAX_FAMILY) break;
            const cJSON *ph = cJSON_GetObjectItem(it, "phone");
            const cJSON *lb = cJSON_GetObjectItem(it, "label");
            if (!cJSON_IsString(ph) || !ph->valuestring[0]) continue;
            ecard_acl_member_t *dst = &acl.family[acl.family_count++];
            snprintf(dst->phone, sizeof(dst->phone), "%s", ph->valuestring);
            if (cJSON_IsString(lb)) {
                snprintf(dst->label, sizeof(dst->label), "%s", lb->valuestring);
            }
        }
    }

    const cJSON *teachers = cJSON_GetObjectItem(acl_obj, "teachers");
    if (cJSON_IsArray(teachers)) {
        const cJSON *it = NULL;
        cJSON_ArrayForEach(it, teachers) {
            if (acl.teacher_count >= ECARD_ACL_MAX_TEACHERS) break;
            const cJSON *id = cJSON_GetObjectItem(it, "id");
            const cJSON *nm = cJSON_GetObjectItem(it, "name");
            const cJSON *ph = cJSON_GetObjectItem(it, "phone");
            const cJSON *sc = cJSON_GetObjectItem(it, "school");
            const cJSON *cl = cJSON_GetObjectItem(it, "class_name");
            ecard_acl_staff_t *dst = &acl.teachers[acl.teacher_count++];
            if (cJSON_IsString(id)) snprintf(dst->id, sizeof(dst->id), "%s", id->valuestring);
            if (cJSON_IsString(nm)) snprintf(dst->name, sizeof(dst->name), "%s", nm->valuestring);
            if (cJSON_IsString(ph)) snprintf(dst->phone, sizeof(dst->phone), "%s", ph->valuestring);
            if (cJSON_IsString(sc)) snprintf(dst->school, sizeof(dst->school), "%s", sc->valuestring);
            if (cJSON_IsString(cl)) snprintf(dst->class_name, sizeof(dst->class_name), "%s", cl->valuestring);
        }
    }

    const cJSON *admins = cJSON_GetObjectItem(acl_obj, "admins");
    if (cJSON_IsArray(admins)) {
        const cJSON *it = NULL;
        cJSON_ArrayForEach(it, admins) {
            if (acl.admin_count >= ECARD_ACL_MAX_ADMINS) break;
            const cJSON *id = cJSON_GetObjectItem(it, "id");
            const cJSON *nm = cJSON_GetObjectItem(it, "name");
            const cJSON *ph = cJSON_GetObjectItem(it, "phone");
            const cJSON *sc = cJSON_GetObjectItem(it, "school");
            ecard_acl_staff_t *dst = &acl.admins[acl.admin_count++];
            if (cJSON_IsString(id)) snprintf(dst->id, sizeof(dst->id), "%s", id->valuestring);
            if (cJSON_IsString(nm)) snprintf(dst->name, sizeof(dst->name), "%s", nm->valuestring);
            if (cJSON_IsString(ph)) snprintf(dst->phone, sizeof(dst->phone), "%s", ph->valuestring);
            if (cJSON_IsString(sc)) snprintf(dst->school, sizeof(dst->school), "%s", sc->valuestring);
        }
    }

    ecard_network_set_acl(&acl);
}

static void process_pending_uploads(const char *relay_url, const char *dev_id, const ecard_setup_t *cfg) {
    ecard_message_t msg;
    int attempts = 0;
    while (attempts++ < 4 && ecard_messages_next_pending_upload(&msg)) {
        char path[128];
        if (!ecard_messages_audio_path(msg.id, path, sizeof(path))) break;
        FILE *fp = fopen(path, "rb");
        if (!fp) {
            ecard_messages_mark_uploaded(msg.id, "", NULL);
            continue;
        }
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            break;
        }
        const long fsize = ftell(fp);
        rewind(fp);
        if (fsize <= 0) {
            fclose(fp);
            ecard_messages_mark_uploaded(msg.id, "", NULL);
            continue;
        }

        char enc_name[96], enc_school[144], enc_class[72];
        url_encode(cfg->name, enc_name, sizeof(enc_name));
        url_encode(cfg->school, enc_school, sizeof(enc_school));
        url_encode(cfg->class_name, enc_class, sizeof(enc_class));

        char *url = malloc(640);
        char *resp = malloc(1024);
        if (!url || !resp) {
            free(url);
            free(resp);
            fclose(fp);
            break;
        }
        snprintf(url, 640,
                 "%s/api/v1/device/upload?device_id=%s&student_id=%s&student_name=%s"
                 "&school=%s&class_name=%s&primary_phone=%s&duration_sec=%u&audio_format=%s",
                 relay_url, dev_id, cfg->id, enc_name, enc_school, enc_class,
                 cfg->phone, (unsigned)msg.duration_sec,
                 msg.audio_is_8k ? "adpcm_8k" : "adpcm_16k");

        esp_http_client_config_t hcfg = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .timeout_ms = 12000,
            .crt_bundle_attach = (strncmp(url, "https://", 8) == 0) ? esp_crt_bundle_attach : NULL,
        };
        esp_http_client_handle_t client = esp_http_client_init(&hcfg);
        if (!client) {
            free(url);
            free(resp);
            fclose(fp);
            break;
        }
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream");

        bool sent_ok = (esp_http_client_open(client, (int)fsize) == ESP_OK);
        uint8_t chunk[512];
        while (sent_ok) {
            const size_t nr = fread(chunk, 1, sizeof(chunk), fp);
            if (nr == 0) break;
            if (esp_http_client_write(client, (const char *)chunk, (int)nr) != (int)nr) {
                sent_ok = false;
            }
        }
        fclose(fp);

        if (!sent_ok) {
            esp_http_client_cleanup(client);
            free(url);
            free(resp);
            break;
        }
        (void)esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        int total = 0;
        int r = 0;
        while (total < 1023 &&
               (r = esp_http_client_read(client, resp + total, 1023 - total)) > 0) {
            total += r;
        }
        resp[total] = '\0';
        esp_http_client_cleanup(client);

        if (status == 200) {
            cJSON *root = cJSON_Parse(resp);
            if (root) {
                sync_time_from_json(root);
                const cJSON *ok = cJSON_GetObjectItem(root, "ok");
                if (cJSON_IsTrue(ok)) {
                    const cJSON *rid = cJSON_GetObjectItem(root, "remote_id");
                    const cJSON *txt = cJSON_GetObjectItem(root, "text");
                    ecard_messages_mark_uploaded(
                        msg.id,
                        cJSON_IsString(rid) ? rid->valuestring : "",
                        cJSON_IsString(txt) ? txt->valuestring : "");
                    s_sync_dirty = true;
                    s_relay_online = true;
                    ESP_LOGI(TAG, "本机留言 #%lu 上传完成: text=%.60s",
                             (unsigned long)msg.id,
                             cJSON_IsString(txt) ? txt->valuestring : "");
                }
                cJSON_Delete(root);
            }
        } else {
            ESP_LOGW(TAG, "留言上传失败 HTTP %d", status);
            free(url);
            free(resp);
            break;
        }
        free(url);
        free(resp);
    }
}

static void process_poll_and_ack(const char *relay_url, const char *dev_id, const ecard_setup_t *cfg) {
    cJSON *req = cJSON_CreateObject();
    if (!req) return;
    cJSON_AddStringToObject(req, "device_id", dev_id);
    cJSON_AddStringToObject(req, "role",
                            (cfg->role == ECARD_ACCOUNT_STUDENT) ? "student" : "teacher");
    cJSON_AddStringToObject(req, "student_id", cfg->id);
    cJSON_AddStringToObject(req, "student_name", cfg->name);
    cJSON_AddStringToObject(req, "school", cfg->school);
    cJSON_AddStringToObject(req, "class_name", cfg->class_name);
    cJSON_AddStringToObject(req, "primary_phone", cfg->phone);
    char *req_str = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!req_str) return;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/device/poll", relay_url);
    char *resp = malloc(4096);
    if (!resp) {
        cJSON_free(req_str);
        return;
    }

    const int status = http_json_post(url, req_str, resp, 4096);
    cJSON_free(req_str);
    if (status != 200) {
        s_relay_online = false;
        s_discovered_relay_url[0] = '\0';
        free(resp);
        return;
    }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return;

    s_relay_online = true;
    sync_time_from_json(root);
    update_acl_from_json(cJSON_GetObjectItem(root, "acl"), cfg);

    cJSON *ack_ids = cJSON_CreateArray();
    const cJSON *msgs = cJSON_GetObjectItem(root, "messages");
    if (cJSON_IsArray(msgs)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, msgs) {
            const cJSON *j_rid = cJSON_GetObjectItem(item, "remote_id");
            const cJSON *j_role = cJSON_GetObjectItem(item, "sender_role");
            const cJSON *j_name = cJSON_GetObjectItem(item, "sender_name");
            const cJSON *j_phone = cJSON_GetObjectItem(item, "sender_phone");
            const cJSON *j_school = cJSON_GetObjectItem(item, "sender_school");
            const cJSON *j_class = cJSON_GetObjectItem(item, "sender_class");
            const cJSON *j_text = cJSON_GetObjectItem(item, "text");
            const cJSON *j_dur = cJSON_GetObjectItem(item, "duration_sec");
            const cJSON *j_fmt = cJSON_GetObjectItem(item, "audio_format");
            const cJSON *j_ep = cJSON_GetObjectItem(item, "created_epoch");

            if (!cJSON_IsString(j_rid) || !j_rid->valuestring[0]) continue;
            const char *rid = j_rid->valuestring;

            if (ecard_messages_has_remote_id(rid)) {
                cJSON_AddItemToArray(ack_ids, cJSON_CreateString(rid));
                continue;
            }

            const ecard_sender_role_t role = ecard_network_parse_role(
                cJSON_IsString(j_role) ? j_role->valuestring : "");
            const char *phone = cJSON_IsString(j_phone) ? j_phone->valuestring : "";
            const char *school = cJSON_IsString(j_school) ? j_school->valuestring : "";
            const char *klass = cJSON_IsString(j_class) ? j_class->valuestring : "";

            if (!ecard_network_is_sender_authorized(role, phone, school, klass)) {
                ESP_LOGW(TAG, "ACL 拒绝未授权留言: rid=%s role=%d phone=%s", rid, (int)role, phone);
                cJSON_AddItemToArray(ack_ids, cJSON_CreateString(rid));
                continue;
            }

            char dl_url[256];
            char tmp_path[128];
            snprintf(dl_url, sizeof(dl_url), "%s/api/v1/device/audio/%s?device_id=%s",
                     relay_url, rid, dev_id);
            size_t dl_bytes = 0;
            bool has_dl = ecard_messages_temp_audio_path(tmp_path, sizeof(tmp_path)) &&
                          download_audio_to_file(dl_url, tmp_path, &dl_bytes);

            ecard_message_t incoming;
            memset(&incoming, 0, sizeof(incoming));
            incoming.direction = ECARD_MESSAGE_RECEIVED;
            incoming.sender_role = role;
            incoming.unread = true;
            incoming.duration_sec = cJSON_IsNumber(j_dur) ? (uint8_t)j_dur->valueint : 5;
            if (incoming.duration_sec == 0) incoming.duration_sec = 1;
            snprintf(incoming.remote_id, sizeof(incoming.remote_id), "%s", rid);
            snprintf(incoming.peer, sizeof(incoming.peer), "%s",
                     (cJSON_IsString(j_name) && j_name->valuestring[0]) ? j_name->valuestring
                                                                         : "家长");
            snprintf(incoming.sender_phone, sizeof(incoming.sender_phone), "%s", phone);
            if (cJSON_IsString(j_text)) {
                snprintf(incoming.text, sizeof(incoming.text), "%s", j_text->valuestring);
            }
            incoming.audio_is_8k = !(cJSON_IsString(j_fmt) &&
                                     strcmp(j_fmt->valuestring, "adpcm_16k") == 0);
            if (cJSON_IsNumber(j_ep) && j_ep->valuedouble > 1700000000.0) {
                incoming.created_epoch_sec = (int64_t)j_ep->valuedouble;
            }

            const uint32_t saved_id = has_dl
                ? ecard_messages_adopt_audio_file(&incoming, 0, tmp_path, dl_bytes)
                : ecard_messages_add(&incoming, 0);

            if (saved_id != 0) {
                cJSON_AddItemToArray(ack_ids, cJSON_CreateString(rid));
                s_sync_dirty = true;
                ESP_LOGI(TAG, "已落盘留言 #%lu (%s, %u 字节, 文本=%.40s)",
                         (unsigned long)saved_id, incoming.peer,
                         (unsigned)dl_bytes, incoming.text);
            }
        }
    }
    cJSON_Delete(root);

    if (cJSON_GetArraySize(ack_ids) > 0) {
        cJSON *ack = cJSON_CreateObject();
        if (ack) {
            cJSON_AddStringToObject(ack, "device_id", dev_id);
            cJSON_AddItemToObject(ack, "remote_ids", ack_ids);
            char *ack_str = cJSON_PrintUnformatted(ack);
            cJSON_Delete(ack);
            if (ack_str) {
                char ack_url[256];
                snprintf(ack_url, sizeof(ack_url), "%s/api/v1/device/ack", relay_url);
                char ack_resp[256];
                const int ack_st = http_json_post(ack_url, ack_str, ack_resp, sizeof(ack_resp));
                ESP_LOGI(TAG, "已向中转服务确认删除 %s -> HTTP %d", ack_str, ack_st);
                cJSON_free(ack_str);
            }
        } else {
            cJSON_Delete(ack_ids);
        }
    } else {
        cJSON_Delete(ack_ids);
    }
}

static void process_sos(const char *relay_url, const char *dev_id, const ecard_setup_t *cfg) {
    if (!s_sos_pending) return;
    s_sos_pending = false;

    cJSON *req = cJSON_CreateObject();
    if (!req) return;
    cJSON_AddStringToObject(req, "device_id", dev_id);
    cJSON_AddStringToObject(req, "student_id",
                            s_sos_student_id[0] ? s_sos_student_id : cfg->id);
    cJSON_AddStringToObject(req, "student_name", cfg->name);
    cJSON_AddStringToObject(req, "school", cfg->school);
    cJSON_AddStringToObject(req, "class_name", cfg->class_name);
    cJSON_AddStringToObject(req, "primary_phone", cfg->phone);
    cJSON_AddStringToObject(req, "location", s_sos_location);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) return;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/v1/device/sos", relay_url);
    char resp[256];
    const int st = http_json_post(url, body, resp, sizeof(resp));
    ESP_LOGI(TAG, "SOS 上报中转服务 -> HTTP %d", st);
    cJSON_free(body);
}

static void sync_worker_task(void *arg) {
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15000));

        if (!ecard_netlink_is_connected() || !ecard_setup_is_provisioned()) {
            continue;
        }
        // 小智对话活跃时让出堆和带宽（紧急 SOS 除外）
        if (!s_sos_pending && ecard_xiaozhi_ws_connected()) {
            continue;
        }

        char relay_url[128];
        if (!resolve_relay_url(relay_url, sizeof(relay_url))) {
            s_relay_online = false;
            continue;
        }

        const ecard_setup_t *cfg = ecard_setup_get();
        char dev_id[24];
        device_id_str(dev_id, sizeof(dev_id));

        process_sos(relay_url, dev_id, cfg);
        process_pending_uploads(relay_url, dev_id, cfg);
        process_poll_and_ack(relay_url, dev_id, cfg);
        s_sync_requested = false;
    }
}
#endif

void ecard_network_start_sync_worker(void) {
#if defined(ESP_PLATFORM)
    if (s_sync_task) return;
    if (xTaskCreate(sync_worker_task, "ecard_sync", 6144, NULL, 2, &s_sync_task) != pdPASS) {
        s_sync_task = NULL;
    }
#endif
}

void ecard_network_request_sync(void) {
#if defined(ESP_PLATFORM)
    s_sync_requested = true;
    if (s_sync_task) {
        xTaskNotifyGive(s_sync_task);
    }
#endif
}

void ecard_network_request_upload(uint32_t local_msg_id) {
    (void)local_msg_id;
#if defined(ESP_PLATFORM)
    if (s_sync_task) {
        xTaskNotifyGive(s_sync_task);
    }
#endif
}

void ecard_network_request_sos(const char *student_id, const char *location_desc) {
#if defined(ESP_PLATFORM)
    snprintf(s_sos_student_id, sizeof(s_sos_student_id), "%s", student_id ? student_id : "");
    snprintf(s_sos_location, sizeof(s_sos_location), "%s", location_desc ? location_desc : "");
    s_sos_pending = true;
    if (s_sync_task) {
        xTaskNotifyGive(s_sync_task);
    }
#else
    (void)student_id;
    (void)location_desc;
#endif
}

bool ecard_network_consume_sync_dirty(void) {
#if defined(ESP_PLATFORM)
    if (!s_sync_dirty) return false;
    s_sync_dirty = false;
    return true;
#else
    return false;
#endif
}

const char *ecard_network_active_relay_url(void) {
#if defined(ESP_PLATFORM)
    const char *configured = ecard_setup_get_relay_url();
    if (configured && configured[0]) return configured;
    if (s_discovered_relay_url[0]) return s_discovered_relay_url;
    return ECARD_DEFAULT_RELAY_URL;
#else
    return ECARD_DEFAULT_RELAY_URL;
#endif
}

bool ecard_network_is_relay_online(void) {
#if defined(ESP_PLATFORM)
    return s_relay_online;
#else
    return false;
#endif
}

bool ecard_network_poll_recess_messages(bool *out_has_new, char *out_sender,
                                        uint8_t *out_duration, uint32_t *out_message_id) {
    if (!out_has_new) return false;
    *out_has_new = false;
    (void)out_sender;
    (void)out_duration;
    (void)out_message_id;
    ecard_network_request_sync();
    return true;
}

bool ecard_network_upload_voice(const uint8_t *adpcm_data, size_t data_len, uint8_t duration_sec) {
    if (!adpcm_data || data_len == 0 || duration_sec == 0) return false;
    ecard_network_request_sync();
    return true;
}

bool ecard_network_send_sos_alert(const char *student_id, const char *location_desc) {
    if (!student_id) return false;
    ecard_network_request_sos(student_id, location_desc);
    return true;
}
