#include "ecard_network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_preset_wifi_credentials(void) {
    ecard_network_init();
    const ecard_wifi_credentials_t *creds = ecard_network_get_credentials();
    assert(creds != NULL);
    assert(strcmp(creds->ssid, ECARD_DEFAULT_CAMPUS_SSID) == 0);
    assert(strcmp(creds->password, ECARD_DEFAULT_CAMPUS_PASS) == 0);

    // 修改凭证
    ecard_network_set_credentials("My_Lab_WiFi", "secret666");
    creds = ecard_network_get_credentials();
    assert(strcmp(creds->ssid, "My_Lab_WiFi") == 0);
    assert(strcmp(creds->password, "secret666") == 0);
}

static void test_recess_time_detection(void) {
    // 09:45 处于上午大课间 (09:40 ~ 10:00) -> true
    assert(ecard_network_is_recess_time(9, 45) == true);
    assert(ecard_network_is_recess_time(9, 40) == true);

    // 09:30 处于上课时段 (不上课间) -> false
    assert(ecard_network_is_recess_time(9, 30) == false);

    // 10:00 课间已结束上课 -> false
    assert(ecard_network_is_recess_time(10, 0) == false);

    // 11:50 处于午休放学时段 (11:40 ~ 12:10) -> true
    assert(ecard_network_is_recess_time(11, 50) == true);

    // 15:55 处于下午大课间 (15:40 ~ 16:05) -> true
    assert(ecard_network_is_recess_time(15, 55) == true);

    // 16:20 处于自习/上课时段 -> false
    assert(ecard_network_is_recess_time(16, 20) == false);
}

static void test_network_operations(void) {
    bool has_new = false;
    char sender[32] = {0};
    uint8_t dur = 0;

    bool ok = ecard_network_poll_recess_messages(&has_new, sender, &dur);
    assert(ok);
    assert(has_new);
    assert(dur > 0);

    uint8_t dummy_adpcm[16] = {0};
    ok = ecard_network_upload_voice(dummy_adpcm, 16, 2);
    assert(ok);

    ok = ecard_network_send_sos_alert("2026110508", "操场");
    assert(ok);
}

int main(void) {
    printf("Running ecard_network tests...\n");
    test_preset_wifi_credentials();
    test_recess_time_detection();
    test_network_operations();
    printf("ecard_network tests passed!\n");
    return 0;
}
