#include "ecard_time.h"
#include <esp_log.h>
#include <esp_sntp.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#define TAG "ecard_time"
#define VALID_EPOCH_MIN 1700000000LL

static bool s_sync_started;
static bool s_synced;

static void ensure_tz(void) {
    // 中国北京时间固定为 UTC+8，不跟随夏令时。
    setenv("TZ", "CST-8", 1);
    tzset();
}

void ecard_time_start_sync(void) {
    if (s_sync_started) return;
    s_sync_started = true;

    ensure_tz();
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
    if (now > VALID_EPOCH_MIN) {
        s_synced = true;
    }
    return s_synced;
}

int64_t ecard_time_epoch_sec(void) {
    if (!ecard_time_is_synced()) return 0;
    return (int64_t)time(NULL);
}

void ecard_time_set_epoch(int64_t epoch_sec) {
    if (epoch_sec < VALID_EPOCH_MIN) return;
    const time_t cur = time(NULL);
    if (s_synced && cur >= VALID_EPOCH_MIN && llabs((long long)(cur - epoch_sec)) < 60LL) {
        return;
    }
    ensure_tz();
    struct timeval tv = {
        .tv_sec = (time_t)epoch_sec,
        .tv_usec = 0,
    };
    if (settimeofday(&tv, NULL) == 0) {
        s_synced = true;
        ESP_LOGI(TAG, "系统时间已通过服务端时间戳校准: %lld", (long long)epoch_sec);
    }
}

void ecard_time_now(uint8_t *hour, uint8_t *minute, uint8_t *second) {
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    if (hour) *hour = (uint8_t)local.tm_hour;
    if (minute) *minute = (uint8_t)local.tm_min;
    if (second) *second = (uint8_t)local.tm_sec;
}
