#include "ecard_portal.h"
#include "ecard_netlink.h"
#include "ecard_opus_arena.h"
#include "ecard_setup.h"
#include <ctype.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ecard_portal"
#define PORTAL_AP_PASSWORD "passport1234"
#define PORTAL_AP_IP "192.168.4.1"
#define HTML_MAX 4096

static bool s_active;
static char s_ap_ssid[32];
static httpd_handle_t s_server;
static esp_netif_t *s_ap_netif;
static TaskHandle_t s_dns_task;
static volatile bool s_dns_stop;
static char *s_html;

static void portal_release_html(void) {
    if (!s_html) return;
    if (ecard_opus_arena_owns(s_html)) {
        ecard_opus_arena_return(s_html);
    } else {
        free(s_html);
    }
    s_html = NULL;
}

static void copy_bounded(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void html_escape(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    size_t out = 0;
    dst[0] = '\0';
    if (!src) return;
    for (const unsigned char *p = (const unsigned char *)src; *p; ++p) {
        const char *rep = NULL;
        char one[2] = { (char)*p, '\0' };
        switch (*p) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': rep = "&quot;"; break;
        case '\'': rep = "&#39;"; break;
        default: rep = one; break;
        }
        const size_t len = strlen(rep);
        if (out + len + 1 > dst_size) break;
        memcpy(dst + out, rep, len);
        out += len;
    }
    dst[out] = '\0';
}

static void json_escape(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    size_t out = 0;
    dst[0] = '\0';
    if (!src) return;
    for (const unsigned char *p = (const unsigned char *)src; *p; ++p) {
        char rep[8];
        const char *text = rep;
        switch (*p) {
        case '"': text = "\\\""; break;
        case '\\': text = "\\\\"; break;
        case '\n': text = "\\n"; break;
        case '\r': text = "\\r"; break;
        case '\t': text = "\\t"; break;
        default:
            if (*p < 0x20) {
                snprintf(rep, sizeof(rep), "\\u%04x", (unsigned)*p);
            } else {
                rep[0] = (char)*p;
                rep[1] = '\0';
            }
            break;
        }
        const size_t len = strlen(text);
        if (out + len + 1 > dst_size) break;
        memcpy(dst + out, text, len);
        out += len;
    }
    dst[out] = '\0';
}

static void url_decode(char *dst, size_t dst_size, const char *src) {
    size_t out = 0;
    for (const char *p = src; *p && out + 1 < dst_size; ++p) {
        if (*p == '+') {
            dst[out++] = ' ';
        } else if (*p == '%' && isxdigit((unsigned char)p[1]) &&
                   isxdigit((unsigned char)p[2])) {
            char hex[3] = { p[1], p[2], '\0' };
            dst[out++] = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            dst[out++] = *p;
        }
    }
    dst[out] = '\0';
}

/** 从 application/x-www-form-urlencoded 中取出指定字段。 */
static bool form_get(const char *body, const char *key, char *out, size_t out_size) {
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "%s=", key);
    size_t key_len = strlen(pattern);

    const char *p = body;
    while (p && *p) {
        const char *hit = strstr(p, pattern);
        if (!hit) return false;
        // 必须是字段开头（串首或紧跟 '&'）
        if (hit != body && hit[-1] != '&') {
            p = hit + key_len;
            continue;
        }
        char raw[256];
        size_t len = 0;
        const char *value = hit + key_len;
        while (value[len] && value[len] != '&' && len < sizeof(raw) - 1) {
            raw[len] = value[len];
            ++len;
        }
        raw[len] = '\0';
        url_decode(out, out_size, raw);
        return true;
    }
    return false;
}

static bool read_body(httpd_req_t *req, char *buf, size_t cap) {
    if (!req || !buf || cap < 2) return false;
    const size_t len = req->content_len;
    if (len == 0 || len >= cap) return false;
    size_t received = 0;
    while (received < len) {
        const int got = httpd_req_recv(req, buf + received, len - received);
        if (got <= 0) return false;
        received += (size_t)got;
    }
    buf[received] = '\0';
    return true;
}

static esp_err_t send_page(httpd_req_t *req, const char *html) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t redirect_to(httpd_req_t *req, const char *location) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

#define HTML_HEAD \
    "<!doctype html><html lang=zh><head><meta charset=utf-8>" \
    "<meta name=viewport content='width=device-width,initial-scale=1'>" \
    "<meta name=theme-color content='#2563eb'>"

#define HTML_STYLE \
    "<style>" \
    "body{margin:0;background:#edf3f9;color:#0f2744;" \
    "font-family:-apple-system,Helvetica,Arial,sans-serif}" \
    ".card{background:#fff;margin:16px;padding:20px;border-radius:18px;" \
    "box-shadow:0 4px 16px rgba(15,39,68,.06)}" \
    "h1{font-size:22px;margin:0 0 4px}" \
    ".sub{color:#5b7089;font-size:13px;margin-bottom:18px}" \
    "label{display:block;font-size:13px;color:#5b7089;margin:14px 0 5px}" \
    "input,select{width:100%%;box-sizing:border-box;padding:11px;border:1px solid #e1e9f2;" \
    "border-radius:10px;font-size:16px;background:#fff;color:#0f2744}" \
    "button{width:100%%;padding:13px;border:0;border-radius:12px;background:#2563eb;" \
    "color:#fff;font-size:16px;font-weight:600}" \
    ".tip{color:#93a6bc;font-size:12px;line-height:1.6;margin-top:14px}" \
    ".err{background:#fff3f3;color:#dc2626;padding:10px;border-radius:10px;" \
    "font-size:14px;margin-bottom:12px}" \
    ".ok{background:#ecfdf5;color:#047857;padding:12px;border-radius:12px;" \
    "font-size:15px;margin-top:12px}" \
    "a{color:#2563eb}" \
    "</style>"

static esp_err_t send_error_page(httpd_req_t *req, const char *message) {
    if (!s_html) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    char message_esc[160];
    html_escape(message_esc, sizeof(message_esc), message);
    snprintf(s_html, HTML_MAX,
             HTML_HEAD HTML_STYLE "</head><body><div class=card>"
             "<h1>无法保存</h1><div class=err>%s</div>"
             "<p><a href=/>返回修改</a></p></div></body></html>",
             message_esc);
    return send_page(req, s_html);
}

static esp_err_t send_wifi_page(httpd_req_t *req) {
    if (!s_html) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    const ecard_setup_t *cfg = ecard_setup_get();
    const char *prefill = "";
    if (cfg->has_wifi && cfg->wifi_ssid[0]) {
        prefill = cfg->wifi_ssid;
    } else if (ecard_netlink_is_connected()) {
        // 设备可能已用内置默认凭据连上默认网络：把当前 SSID 直接填好，
        // 用户只需补密码并确认，不必手动输入网络名。
        prefill = ecard_netlink_ssid();
    }
    char ssid_esc[256];
    html_escape(ssid_esc, sizeof(ssid_esc), prefill);

    snprintf(s_html, HTML_MAX,
             HTML_HEAD HTML_STYLE "</head><body><div class=card>"
             "<h1>欢迎使用</h1>"
             "<div class=sub>第 1 步 / 共 2 步 · 先连接 Wi-Fi</div>"
             "<form method=post action=/wifi>"
             "<label>附近的 Wi-Fi "
             "<a href='javascript:scan()' style='float:right'>重新搜索</a></label>"
             "<select id=pick onchange='pickSsid(this.value)'>"
             "<option value=''>正在搜索…</option></select>"
             "<label>Wi-Fi 名称</label>"
             "<input name=ssid id=ssid value='%s' maxlength=32 required "
             "autocapitalize=none autocomplete=off placeholder='选择上方 Wi-Fi 或手动输入'>"
             "<label>Wi-Fi 密码</label>"
             "<input name=pass type=password minlength=8 maxlength=64 required "
             "autocomplete=off placeholder='至少 8 位'>"
             "<button type=submit>连接 Wi-Fi</button></form>"
             "<div class=tip>设备会自动搜索附近网络。连上后本页会自动进入身份信息填写，"
             "无需手动输入网址。</div>"
             "</div><script>"
             "function pickSsid(v){document.getElementById('ssid').value=v;}"
             "function scanMsg(t){"
             "var sel=document.getElementById('pick');sel.innerHTML='';"
             "var o=document.createElement('option');o.value='';o.textContent=t;sel.add(o);}"
             "function scan(){"
             "var sel=document.getElementById('pick');"
             "scanMsg('正在搜索…');"
             "fetch('/scan',{cache:'no-store'}).then(function(r){return r.json()})"
             ".then(function(list){"
             "if(!list.length){scanMsg('未找到 Wi-Fi，请手动输入');return;}"
             "sel.innerHTML='';"
             "var cur=document.getElementById('ssid').value;"
             "for(var i=0;i<list.length;i++){"
             "var o=document.createElement('option');"
             "o.value=list[i].ssid;"
             "o.textContent=list[i].ssid+(list[i].secure?' 🔒':'')+'  '+list[i].rssi+' dBm';"
             "if(list[i].ssid===cur){o.selected=true;}"
             "sel.add(o);}"
             "if(!cur){document.getElementById('ssid').value=sel.value;}"
             "}).catch(function(){scanMsg('搜索失败，请手动输入');});}"
             "scan();</script></body></html>",
             ssid_esc);
    return send_page(req, s_html);
}

static esp_err_t send_connecting_page(httpd_req_t *req, const char *ssid) {
    if (!s_html) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    char ssid_esc[128];
    html_escape(ssid_esc, sizeof(ssid_esc), ssid);

    snprintf(s_html, HTML_MAX,
             HTML_HEAD HTML_STYLE "</head><body><div class=card>"
             "<h1>正在连接…</h1>"
             "<div class=sub>Wi-Fi：%s</div>"
             "<p class=tip id=msg>请稍候，连上后会自动进入身份信息页面。</p>"
             "<p><a href=/identity style='display:block;text-align:center;padding:12px;"
             "border-radius:12px;background:#2563eb;color:#fff;text-decoration:none;"
             "font-weight:600'>下一步：填写身份信息</a></p>"
             "<p style='text-align:center'><a href=/wifi>返回修改 Wi-Fi</a></p>"
             "</div><script>"
             "var n=0;"
             "function poll(){"
             "fetch('/status',{cache:'no-store'}).then(function(r){return r.json()})"
             ".then(function(s){"
             "if(s.identity||s.provisioned){location.href='/done';return;}"
             "if(s.wifi){location.href='/identity';return;}"
             "var m=document.getElementById('msg');"
             "if(s.fails>0){m.textContent='Wi-Fi 连接失败，可返回修改密码，或先点下方填写身份信息。';}"
             "else if(++n>6){m.textContent='连接耗时较长，可继续等待，或直接点下方填写身份信息。';}"
             "setTimeout(poll,1500);"
             "}).catch(function(){setTimeout(poll,1500)})}"
             "poll();</script></body></html>",
             ssid_esc);
    return send_page(req, s_html);
}

static esp_err_t send_identity_page(httpd_req_t *req, bool show_error) {
    if (!s_html) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    char relay_esc[256];
    html_escape(relay_esc, sizeof(relay_esc), ecard_setup_get_relay_url());
    snprintf(s_html, HTML_MAX,
             HTML_HEAD HTML_STYLE "</head><body><div class=card>"
             "<h1>填写身份</h1>"
             "<div class=sub>第 2 步 / 共 2 步 · Wi-Fi 已保存</div>"
             "%s"
             "<form method=post action=/identity>"
             "<label>身份</label><select name=role onchange=syncRole()>"
             "<option value=student selected>学生</option>"
             "<option value=teacher>教师</option></select>"
             "<label>教育 ID（8 位）或教师工号</label>"
             "<input name=id maxlength=16 required autocapitalize=none autocomplete=off>"
             "<label>学校</label><input name=school maxlength=40 required>"
             "<div id=classRow><label>班级（学生）</label>"
             "<input name=class maxlength=16>"
             "<label>当前年级（题库默认保留本年级及前后各一年）</label>"
             "<select name=grade>"
             "<option value='' selected>按班级自动识别</option>"
             "<option value=1>一年级</option><option value=2>二年级</option>"
             "<option value=3>三年级</option><option value=4>四年级</option>"
             "<option value=5>五年级</option><option value=6>六年级</option>"
             "</select></div>"
             "<label>姓名</label><input name=name maxlength=24 required>"
             "<label>主家长绑定手机号（11 位）</label>"
             "<input name=phone maxlength=11 inputmode=numeric pattern='1[0-9]{10}' required>"
             "<label>留言中转服务（可选，留空自动发现）</label>"
             "<input name=relay value='%s' maxlength=120 autocapitalize=none autocomplete=off placeholder='http://192.168.1.10:8787'>"
             "<button type=submit>保存并完成</button></form>"
             "<div class=tip>信息仅保存在本机，用于首页工牌与家长绑定。</div>"
             "<p style='text-align:center;margin-top:14px'><a href=/wifi>返回修改 Wi-Fi</a></p>"
             "</div><script>"
             "function syncRole(){"
             "var t=document.querySelector('select[name=role]').value==='teacher';"
             "var id=document.querySelector('input[name=id]');"
             "id.pattern=t?'[A-Za-z0-9]{1,16}':'[0-9]{8}';"
             "id.maxLength=t?16:8;"
             "id.placeholder=t?'教师工号':'8 位教育 ID';"
             "document.getElementById('classRow').style.display=t?'none':'block';"
             "}syncRole();</script></body></html>",
             show_error ? "<div class=err>信息不完整或格式不正确，请检查后重试。</div>" : "",
             relay_esc);
    return send_page(req, s_html);
}

static esp_err_t send_done_page(httpd_req_t *req) {
    if (!s_html) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    snprintf(s_html, HTML_MAX,
             HTML_HEAD HTML_STYLE "</head><body><div class=card>"
             "<h1>配置完成</h1>"
             "<div class=ok>Wi-Fi 与身份信息已保存。</div>"
             "<div class=tip>设备正在重启，约 3 秒后即可正常使用。</div>"
             "</div></body></html>");
    return send_page(req, s_html);
}

static esp_err_t root_get(httpd_req_t *req) {
    // 配网必须先保存 Wi-Fi 凭据。设备可能在开机时用内置默认凭据短暂连上，
    // 但这不代表“已配网”：如果按连接状态放行到身份页，刚保存完身份就会
    // 因为 has_wifi=false 被打回 Wi-Fi 页，用户会看到页面来回跳。
    if (ecard_setup_has_wifi()) {
        if (!ecard_setup_has_identity()) {
            return redirect_to(req, "/identity");
        }
        return redirect_to(req, "/done");
    }
    return send_wifi_page(req);
}

static esp_err_t wifi_get(httpd_req_t *req) {
    if (ecard_setup_is_provisioned()) {
        return redirect_to(req, "/done");
    }
    return send_wifi_page(req);
}

static esp_err_t wifi_post(httpd_req_t *req) {
    char body[512];
    if (!read_body(req, body, sizeof(body))) {
        return send_error_page(req, "提交内容为空或过大。");
    }

    char ssid[40] = {0};
    char pass[72] = {0};
    if (!form_get(body, "ssid", ssid, sizeof(ssid)) ||
        !form_get(body, "pass", pass, sizeof(pass))) {
        return send_error_page(req, "Wi-Fi 名称或密码缺失。");
    }
    if (!ecard_setup_save_wifi(ssid, pass)) {
        return send_error_page(req, "Wi-Fi 名称不能为空，密码需要 8 到 64 位。");
    }

    // 身份已经存在时（例如只回来改 Wi-Fi），直接完成并重启，
    // 不需要再走一次身份页。
    if (ecard_setup_has_identity()) {
        return redirect_to(req, "/done");
    }

    const esp_err_t err = send_connecting_page(req, ssid);
    // 先让手机收到“正在连接”，再切换 STA 信道，避免响应被截断。
    vTaskDelay(pdMS_TO_TICKS(400));
    ecard_netlink_reconnect();
    return err;
}

static esp_err_t identity_get(httpd_req_t *req) {
    // 身份填写只依赖“Wi-Fi 凭据已保存”，不要求此刻 STA 正好连着：
    // 保存凭据后信道切换会让 STA 短暂掉线，这时把手机打回 Wi-Fi 页会
    // 让用户以为身份没保存上。
    if (!ecard_setup_has_wifi()) {
        return redirect_to(req, "/");
    }
    if (ecard_setup_has_identity()) {
        return redirect_to(req, "/done");
    }
    return send_identity_page(req, false);
}

static esp_err_t identity_post(httpd_req_t *req) {
    if (!ecard_setup_has_wifi()) {
        return redirect_to(req, "/");
    }

    char body[1024];
    if (!read_body(req, body, sizeof(body))) {
        return send_identity_page(req, true);
    }

    char role[16] = {0};
    char id[24] = {0};
    char school[48] = {0};
    char klass[24] = {0};
    char grade[8] = {0};
    char name[32] = {0};
    char phone[16] = {0};
    char relay[128] = {0};

    (void)form_get(body, "role", role, sizeof(role));
    const bool ok = form_get(body, "id", id, sizeof(id)) &&
                    form_get(body, "school", school, sizeof(school)) &&
                    form_get(body, "name", name, sizeof(name)) &&
                    form_get(body, "phone", phone, sizeof(phone));
    (void)form_get(body, "class", klass, sizeof(klass));
    (void)form_get(body, "grade", grade, sizeof(grade));
    (void)form_get(body, "relay", relay, sizeof(relay));
    if (!ok) {
        return send_identity_page(req, true);
    }

    ecard_setup_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.role = (strcmp(role, "teacher") == 0) ? ECARD_ACCOUNT_TEACHER
                                              : ECARD_ACCOUNT_STUDENT;
    copy_bounded(cfg.id, sizeof(cfg.id), id);
    copy_bounded(cfg.school, sizeof(cfg.school), school);
    copy_bounded(cfg.class_name, sizeof(cfg.class_name), klass);
    copy_bounded(cfg.name, sizeof(cfg.name), name);
    copy_bounded(cfg.phone, sizeof(cfg.phone), phone);

    if (!ecard_setup_save_identity(&cfg)) {
        return send_identity_page(req, true);
    }
    if (cfg.role == ECARD_ACCOUNT_STUDENT &&
        grade[0] >= '1' && grade[0] <= '6' && grade[1] == '\0') {
        (void)ecard_setup_save_curriculum((uint8_t)(grade[0] - '0'), NULL, 0);
    }
    if (relay[0] && !ecard_setup_save_relay_url(relay)) {
        return send_identity_page(req, true);
    }
    return redirect_to(req, "/done");
}

#define SCAN_MAX 12

typedef struct {
    const char *ssid;
    int8_t rssi;
    bool secure;
} scan_item_t;

static int scan_item_cmp(const void *a, const void *b) {
    const scan_item_t *x = (const scan_item_t *)a;
    const scan_item_t *y = (const scan_item_t *)b;
    return (int)y->rssi - (int)x->rssi;
}

/** 扫描附近 Wi-Fi，按信号强度返回最多 12 个去重后的网络。 */
static esp_err_t scan_get(httpd_req_t *req) {
    wifi_ap_record_t *records = NULL;
    uint16_t count = 0;

    // ESP-IDF 拒绝在 STA 连接过程中扫描，所以先由 netlink 主动断开；
    // 这段断开不计入失败次数，扫描结束后 netlink 会立刻重新连接。
    ecard_netlink_scan_begin();

    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err == ESP_ERR_WIFI_STATE) {
        // 驱动仍在收尾上一次连接：等一小会儿再扫一次。
        vTaskDelay(pdMS_TO_TICKS(300));
        err = esp_wifi_scan_start(NULL, true);
    }

    if (err == ESP_OK) {
        uint16_t total = 0;
        if (esp_wifi_scan_get_ap_num(&total) == ESP_OK && total > 0) {
            if (total > 24) total = 24;
            records = calloc(total, sizeof(*records));
            if (records) {
                uint16_t got = total;
                if (esp_wifi_scan_get_ap_records(&got, records) == ESP_OK) {
                    count = got;
                } else {
                    count = 0;
                }
            } else {
                esp_wifi_clear_ap_list();
            }
        }
    } else {
        ESP_LOGW(TAG, "Wi-Fi 扫描失败: 0x%x", (unsigned)err);
    }

    scan_item_t items[SCAN_MAX];
    int item_count = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        if (!ssid[0]) continue;

        bool dup = false;
        for (int j = 0; j < item_count; ++j) {
            if (strcmp(items[j].ssid, ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        scan_item_t item = {
            .ssid = ssid,
            .rssi = records[i].rssi,
            .secure = records[i].authmode != WIFI_AUTH_OPEN,
        };
        if (item_count < SCAN_MAX) {
            items[item_count++] = item;
        } else {
            int worst = 0;
            for (int j = 1; j < SCAN_MAX; ++j) {
                if (items[j].rssi < items[worst].rssi) worst = j;
            }
            if (item.rssi > items[worst].rssi) items[worst] = item;
        }
    }
    qsort(items, item_count, sizeof(items[0]), scan_item_cmp);

    if (!s_html) {
        free(records);
        ecard_netlink_scan_end();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
    }
    size_t out = 0;
    int n = snprintf(s_html, HTML_MAX, "[");
    if (n > 0) out = (size_t)n;
    for (int i = 0; i < item_count; ++i) {
        char ssid_json[200];
        json_escape(ssid_json, sizeof(ssid_json), items[i].ssid);
        n = snprintf(s_html + out, HTML_MAX - out,
                     "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
                     i ? "," : "", ssid_json, (int)items[i].rssi,
                     items[i].secure ? "true" : "false");
        if (n < 0 || (size_t)n >= HTML_MAX - out) break;
        out += (size_t)n;
    }
    if (out + 2 <= HTML_MAX) {
        s_html[out++] = ']';
        s_html[out] = '\0';
    }

    free(records);
    ecard_netlink_scan_end();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_get(httpd_req_t *req) {
    char json[192];
    snprintf(json, sizeof(json),
             "{\"wifi\":%s,\"identity\":%s,\"provisioned\":%s,\"fails\":%u}",
             ecard_netlink_is_connected() ? "true" : "false",
             ecard_setup_has_identity() ? "true" : "false",
             ecard_setup_is_provisioned() ? "true" : "false",
             (unsigned)ecard_netlink_fail_count());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t done_get(httpd_req_t *req) {
    if (!ecard_setup_is_provisioned()) {
        return redirect_to(req, "/");
    }
    const esp_err_t err = send_done_page(req);
    vTaskDelay(pdMS_TO_TICKS(1800));
    esp_restart();
    return err;
}

/** 手机系统探测“是否有外网”时返回 302，触发系统自带的配网弹窗。 */
static esp_err_t captive_probe(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" PORTAL_AP_IP "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req,
                           "<meta http-equiv=refresh content='0;url=http://" PORTAL_AP_IP "/'>",
                           HTTPD_RESP_USE_STRLEN);
}

/* ------------------------------------------------------------------------- */
/* 强制门户 DNS：把所有域名解析到设备自身                                    */
/* ------------------------------------------------------------------------- */

static void dns_task(void *arg) {
    (void)arg;
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket 创建失败");
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    (void)setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS 53 端口绑定失败");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t query[512];
    uint8_t response[512];
    for (;;) {
        if (s_dns_stop) break;
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        const int len = recvfrom(sock, query, sizeof(query), 0,
                                 (struct sockaddr *)&from, &from_len);
        if (s_dns_stop) break;
        if (len < 12) continue;
        if (query[2] & 0x80) continue;  // 不响应响应包，避免环路

        const uint16_t qdcount = ((uint16_t)query[4] << 8) | query[5];
        if (qdcount == 0) continue;

        size_t off = 12;
        bool valid = false;
        while (off < (size_t)len) {
            const uint8_t label = query[off];
            if (label == 0) {
                ++off;
                valid = true;
                break;
            }
            if ((label & 0xC0) == 0xC0) {
                off += 2;
                valid = true;
                break;
            }
            if (label > 63) break;
            off += 1u + label;
        }
        if (!valid || off + 4 > (size_t)len) continue;

        const uint16_t qtype = ((uint16_t)query[off] << 8) | query[off + 1];
        const size_t qend = off + 4;
        if (qtype == 1 && qend + 16 > sizeof(response)) continue;

        memcpy(response, query, qend);
        response[2] = 0x81;  // QR=1, RD=1, RA=1
        response[3] = 0x80;
        response[4] = 0x00;  // QDCOUNT = 1
        response[5] = 0x01;
        response[8] = 0x00;  // NSCOUNT = 0
        response[9] = 0x00;
        response[10] = 0x00; // ARCOUNT = 0
        response[11] = 0x00;

        size_t out = qend;
        if (qtype == 1) {
            response[6] = 0x00;
            response[7] = 0x01;  // ANCOUNT = 1
            response[out++] = 0xC0;
            response[out++] = 0x0C;
            response[out++] = 0x00;
            response[out++] = 0x01;  // A
            response[out++] = 0x00;
            response[out++] = 0x01;  // IN
            response[out++] = 0x00;
            response[out++] = 0x00;
            response[out++] = 0x00;
            response[out++] = 0x3C;  // TTL 60
            response[out++] = 0x00;
            response[out++] = 0x04;
            response[out++] = 192;
            response[out++] = 168;
            response[out++] = 4;
            response[out++] = 1;
        } else {
            response[6] = 0x00;
            response[7] = 0x00;  // 非 A 查询返回空答案，让客户端回退到 A
        }

        (void)sendto(sock, response, out, 0, (struct sockaddr *)&from, from_len);
    }
    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static const char *CAPTIVE_PROBES[] = {
    "/generate_204",
    "/gen_204",
    "/hotspot-detect.html",
    "/library/test/success.html",
    "/connecttest.txt",
    "/ncsi.txt",
    "/success.txt",
    "/canonical.html",
    "/redirect",
};

static void register_probe_uris(void) {
    httpd_uri_t probe = {
        .uri = NULL,
        .method = HTTP_GET,
        .handler = captive_probe,
        .user_ctx = NULL,
    };
    for (size_t i = 0; i < sizeof(CAPTIVE_PROBES) / sizeof(CAPTIVE_PROBES[0]); ++i) {
        probe.uri = CAPTIVE_PROBES[i];
        (void)httpd_register_uri_handler(s_server, &probe);
    }
    // 未匹配的路径也交给强制门户处理；必须最后注册，避免抢占精确 URI。
    probe.uri = "/*";
    (void)httpd_register_uri_handler(s_server, &probe);
}

bool ecard_portal_start(void) {
    if (s_active) return true;
    if (!s_html) {
        uint8_t *arena_buf = NULL;
        size_t arena_cap = 0;
        if (ecard_opus_arena_borrow(&arena_buf, &arena_cap) && arena_buf && arena_cap >= HTML_MAX) {
            memset(arena_buf, 0, HTML_MAX);
            s_html = (char *)arena_buf;
        } else {
            if (arena_buf) {
                ecard_opus_arena_return(arena_buf);
            }
            s_html = calloc(1, HTML_MAX);
        }
        if (!s_html) {
            ESP_LOGE(TAG, "无法分配配网页面缓冲");
            return false;
        }
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "AI-Passport-%02X%02X", mac[4], mac[5]);

    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t ap_cfg;
    memset(&ap_cfg, 0, sizeof(ap_cfg));
    snprintf((char *)ap_cfg.ap.ssid, sizeof(ap_cfg.ap.ssid), "%s", s_ap_ssid);
    snprintf((char *)ap_cfg.ap.password, sizeof(ap_cfg.ap.password), "%s",
             PORTAL_AP_PASSWORD);
    ap_cfg.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.channel = 6;
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "无法启动配网热点");
        portal_release_html();
        return false;
    }

    // 把热点自己的 IP 作为 DNS 下发给手机，配合 DNS 劫持实现自动弹页。
    if (s_ap_netif) {
        esp_netif_dns_info_t dns;
        memset(&dns, 0, sizeof(dns));
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        if (esp_netif_str_to_ip4(PORTAL_AP_IP, &dns.ip.u_addr.ip4) == ESP_OK) {
            const esp_err_t dns_err =
                esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
            if (dns_err != ESP_OK) {
                ESP_LOGW(TAG, "热点 DNS 下发失败: 0x%x", (unsigned)dns_err);
            }
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 24;
    config.stack_size = 6144;
    config.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败");
        portal_release_html();
        return false;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get,
                         .user_ctx = NULL };
    httpd_uri_t wifi_form = { .uri = "/wifi", .method = HTTP_GET, .handler = wifi_get,
                              .user_ctx = NULL };
    httpd_uri_t wifi_save = { .uri = "/wifi", .method = HTTP_POST, .handler = wifi_post,
                              .user_ctx = NULL };
    httpd_uri_t identity_form = { .uri = "/identity", .method = HTTP_GET,
                                  .handler = identity_get, .user_ctx = NULL };
    httpd_uri_t identity_save = { .uri = "/identity", .method = HTTP_POST,
                                  .handler = identity_post, .user_ctx = NULL };
    httpd_uri_t status = { .uri = "/status", .method = HTTP_GET, .handler = status_get,
                           .user_ctx = NULL };
    httpd_uri_t scan = { .uri = "/scan", .method = HTTP_GET, .handler = scan_get,
                         .user_ctx = NULL };
    httpd_uri_t done = { .uri = "/done", .method = HTTP_GET, .handler = done_get,
                         .user_ctx = NULL };

    (void)httpd_register_uri_handler(s_server, &root);
    (void)httpd_register_uri_handler(s_server, &wifi_form);
    (void)httpd_register_uri_handler(s_server, &wifi_save);
    (void)httpd_register_uri_handler(s_server, &identity_form);
    (void)httpd_register_uri_handler(s_server, &identity_save);
    (void)httpd_register_uri_handler(s_server, &status);
    (void)httpd_register_uri_handler(s_server, &scan);
    (void)httpd_register_uri_handler(s_server, &done);
    register_probe_uris();

    s_dns_stop = false;
    if (xTaskCreate(dns_task, "ecard_dns", 3072, NULL, 3, &s_dns_task) != pdPASS) {
        ESP_LOGW(TAG, "DNS 服务任务创建失败，自动弹页可能不可用");
        s_dns_task = NULL;
    }

    s_active = true;
    ESP_LOGI(TAG, "配网热点已开启: %s / %s，配置页 http://%s/", s_ap_ssid,
             PORTAL_AP_PASSWORD, PORTAL_AP_IP);
    return true;
}

void ecard_portal_stop(void) {
    if (!s_active) return;
    s_active = false;

    if (s_dns_task) {
        s_dns_stop = true;
        for (int i = 0; i < 25 && s_dns_task; ++i) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (s_dns_task) {
            vTaskDelete(s_dns_task);
            s_dns_task = NULL;
        }
    }

    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    portal_release_html();

    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "配网热点与 Web 服务已关闭，内存已释放（堆剩余 %u）",
             (unsigned)esp_get_free_heap_size());
}

bool ecard_portal_is_active(void) {
    return s_active;
}

const char *ecard_portal_ssid(void) {
    return s_active ? s_ap_ssid : "";
}

const char *ecard_portal_password(void) {
    return PORTAL_AP_PASSWORD;
}
