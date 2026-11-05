#include "ecard_network.h"
#include <string.h>

static ecard_wifi_credentials_t s_creds;
static bool s_net_inited = false;

// 标准学校作息课间时段表 (上午与下午各两个主要课间及大课间)
static const ecard_schedule_period_t k_recess_periods[] = {
    {  9, 40, 10,  0 }, // 上午大课间 09:40 ~ 10:00
    { 11, 40, 12, 10 }, // 午前放学时段 11:40 ~ 12:10
    { 14, 40, 14, 55 }, // 下午小课间 14:40 ~ 14:55
    { 15, 40, 16, 05 }, // 下午大课间 15:40 ~ 16:05
    { 17, 30, 18, 30 }  // 傍晚放学时段 17:30 ~ 18:30
};

static const size_t k_recess_period_count = sizeof(k_recess_periods) / sizeof(k_recess_periods[0]);

void ecard_network_init(void) {
    if (s_net_inited) return;

    memset(&s_creds, 0, sizeof(s_creds));
    strncpy(s_creds.ssid, ECARD_DEFAULT_CAMPUS_SSID, sizeof(s_creds.ssid) - 1);
    strncpy(s_creds.password, ECARD_DEFAULT_CAMPUS_PASS, sizeof(s_creds.password) - 1);
    s_creds.channel = 0;
    s_creds.has_bssid = false;

    s_net_inited = true;
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

bool ecard_network_poll_recess_messages(bool *out_has_new, char *out_sender, uint8_t *out_duration) {
    if (!out_has_new) return false;

    // 默认测试桩模拟：成功连网查询
    *out_has_new = true;
    if (out_sender) {
        strncpy(out_sender, "妈妈", 16);
    }
    if (out_duration) {
        *out_duration = 12; // 12秒语音
    }

    return true;
}

bool ecard_network_upload_voice(const uint8_t *adpcm_data, size_t data_len, uint8_t duration_sec) {
    if (!adpcm_data || data_len == 0 || duration_sec == 0) return false;
    // 模拟极速上传：连上预置校网 -> POST -> 释放 WiFi
    return true;
}

bool ecard_network_send_sos_alert(const char *student_id, const char *location_desc) {
    if (!student_id) return false;
    (void)location_desc;
    // 模拟高优先级并发报警包发送
    return true;
}
