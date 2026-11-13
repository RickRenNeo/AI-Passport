#include "ecard_time.h"
#include <esp_log.h>
#include <esp_sntp.h>
#include <time.h>
#include <sys/time.h>

#define TAG "ecard_time"
#define CST_OFFSET_SEC (8 * 3600)

static bool s_sync_started;
static bool s_synced;

void ecard_time_start_sync(void) {
    if (s_sync_started) return;
    s_sync_started = true;

    // 中国北京时间固定为 UTC+8，不跟随夏令时。
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP 已启动，等待网络授时");
}

bool ecard_time_is_synced(void) {
    if (s_synced) return true;
    const time_t now = time(NULL);
    // 1970 年之后、且已越过 2024 年，视为有效授时。
    if (now > 1700000000) {
        s_synced = true;
    }
    return s_synced;
}

void ecard_time_now(uint8_t *hour, uint8_t *minute, uint8_t *second) {
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    if (hour) *hour = (uint8_t)local.tm_hour;
    if (minute) *minute = (uint8_t)local.tm_min;
    if (second) *second = (uint8_t)local.tm_sec;
}
