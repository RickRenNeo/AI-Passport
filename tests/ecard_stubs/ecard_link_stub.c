/* Host-test stubs: networking, provisioning portal, and SNTP are ESP-only. */
#include "ecard_netlink.h"
#include "ecard_lan_http.h"
#include "ecard_portal.h"
#include "ecard_time.h"
#include "ecard_xiaozhi.h"
#include <stdio.h>

bool ecard_stub_ai_ready = true;
bool ecard_stub_ai_speaking = false;
bool ecard_stub_ai_connect_failed = false;
bool ecard_stub_lan_ai_busy = false;
unsigned ecard_stub_ai_session_start_calls = 0;
unsigned ecard_stub_ai_session_stop_calls = 0;

bool ecard_lan_http_start(void) { return true; }
void ecard_lan_http_stop(void) {}
bool ecard_lan_http_is_running(void) { return !ecard_stub_lan_ai_busy; }
void ecard_lan_http_set_ai_busy(bool busy) { ecard_stub_lan_ai_busy = busy; }
bool ecard_lan_http_get_ip(char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    snprintf(out, out_size, "192.168.1.100");
    return true;
}

bool ecard_netlink_init(void) { return true; }
void ecard_netlink_tick(void) {}
bool ecard_netlink_is_connected(void) { return true; }
uint8_t ecard_netlink_fail_count(void) { return 0; }
const char *ecard_netlink_ssid(void) { return ""; }
void ecard_netlink_reconnect(void) {}
void ecard_netlink_scan_begin(void) {}
void ecard_netlink_scan_end(void) {}
void ecard_netlink_set_low_latency(bool on) { (void)on; }

bool ecard_portal_start(void) { return true; }
void ecard_portal_stop(void) {}
bool ecard_portal_is_active(void) { return false; }
const char *ecard_portal_ssid(void) { return "AI-Passport-TEST"; }
const char *ecard_portal_password(void) { return "passport1234"; }

void ecard_time_start_sync(void) {}
bool ecard_time_is_synced(void) { return false; }
int64_t ecard_time_epoch_sec(void) { return 0; }
void ecard_time_set_epoch(int64_t epoch_sec) { (void)epoch_sec; }
void ecard_time_now(uint8_t *h, uint8_t *m, uint8_t *s) {
    if (h) *h = 9;
    if (m) *m = 40;
    if (s) *s = 0;
}

bool ecard_xiaozhi_ws_connected(void) { return ecard_stub_ai_ready; }
bool ecard_xiaozhi_is_busy(void) { return ecard_stub_ai_ready; }
bool ecard_xiaozhi_connect_failed(void) { return ecard_stub_ai_connect_failed; }
bool ecard_xiaozhi_session_ready(void) { return ecard_stub_ai_ready; }
void ecard_xiaozhi_ask(const char *text) { (void)text; }
void ecard_xiaozhi_ask_with_fallback(const char *text, const char *fallback) {
    (void)text;
    (void)fallback;
}
void ecard_xiaozhi_ask_with_follow(const char *text, const char *follow) {
    (void)text;
    (void)follow;
}
bool ecard_xiaozhi_listen_start(void) { return true; }
bool ecard_xiaozhi_listen_had_audio(void) { return true; }
void ecard_xiaozhi_listen_stop(void) {}
void ecard_xiaozhi_feed_mic_pcm(const int16_t *pcm, size_t samples) {
    (void)pcm; (void)samples;
}
void ecard_xiaozhi_reset_mic(void) {}
bool ecard_xiaozhi_is_speaking(void) { return ecard_stub_ai_speaking; }
unsigned ecard_stub_reset_playback_calls = 0;
void ecard_xiaozhi_reset_playback(void) { ++ecard_stub_reset_playback_calls; }
bool ecard_xiaozhi_restart_pending(void) { return false; }
size_t ecard_xiaozhi_read_pcm(int16_t *out, size_t max_samples) {
    (void)out; (void)max_samples; return 0;
}

void ecard_xiaozhi_session_start(void) {
    ecard_stub_ai_connect_failed = false;
    ++ecard_stub_ai_session_start_calls;
}
void ecard_xiaozhi_session_stop(void) { ++ecard_stub_ai_session_stop_calls; }
