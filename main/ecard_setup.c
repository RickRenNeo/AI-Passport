#include "ecard_setup.h"
#include "ecard_cat_room.h"
#include "ecard_profile.h"
#include <string.h>

#if defined(ESP_PLATFORM)
#include "nvs.h"
#include "nvs_flash.h"
#define SETUP_NAMESPACE "ecard"
#endif

static ecard_setup_t s_setup;
static char s_relay_url[ECARD_RELAY_URL_MAX];
static bool s_loaded;

static void copy_field(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

#if defined(ESP_PLATFORM)
static bool save_blob(void) {
    nvs_handle_t handle;
    if (nvs_open(SETUP_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t err = nvs_set_blob(handle, "config", &s_setup, sizeof(s_setup));
    if (err == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}
#endif

void ecard_setup_init(void) {
    memset(&s_setup, 0, sizeof(s_setup));
    s_setup.role = ECARD_ACCOUNT_STUDENT;
    memset(s_relay_url, 0, sizeof(s_relay_url));

#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(SETUP_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t size = sizeof(s_setup);
        if (nvs_get_blob(handle, "config", &s_setup, &size) != ESP_OK || size == 0) {
            memset(&s_setup, 0, sizeof(s_setup));
            s_setup.role = ECARD_ACCOUNT_STUDENT;
        }
        size_t url_len = sizeof(s_relay_url);
        if (nvs_get_str(handle, "relay_url", s_relay_url, &url_len) != ESP_OK) {
            s_relay_url[0] = '\0';
        }
        nvs_close(handle);
    }
#endif
    s_loaded = true;
}

bool ecard_setup_has_wifi(void) {
    if (!s_loaded) ecard_setup_init();
    return s_setup.has_wifi && ecard_setup_valid_text(s_setup.wifi_ssid, 32);
}

bool ecard_setup_has_identity(void) {
    if (!s_loaded) ecard_setup_init();
    if (s_setup.role == ECARD_ACCOUNT_STUDENT) {
        return ecard_setup_valid_student(s_setup.id, s_setup.school, s_setup.class_name,
                                         s_setup.name, s_setup.phone);
    }
    return ecard_setup_valid_teacher(s_setup.id, s_setup.school, s_setup.name,
                                     s_setup.phone);
}

bool ecard_setup_is_provisioned(void) {
    return ecard_setup_has_wifi() && ecard_setup_has_identity();
}

const ecard_setup_t *ecard_setup_get(void) {
    if (!s_loaded) ecard_setup_init();
    return &s_setup;
}

bool ecard_setup_save_identity(const ecard_setup_t *identity) {
    if (!identity) return false;

    const bool ok = (identity->role == ECARD_ACCOUNT_STUDENT)
        ? ecard_setup_valid_student(identity->id, identity->school, identity->class_name,
                                    identity->name, identity->phone)
        : ecard_setup_valid_teacher(identity->id, identity->school, identity->name,
                                    identity->phone);
    if (!ok) return false;

    copy_field(s_setup.id, sizeof(s_setup.id), identity->id);
    copy_field(s_setup.school, sizeof(s_setup.school), identity->school);
    copy_field(s_setup.class_name, sizeof(s_setup.class_name), identity->class_name);
    copy_field(s_setup.name, sizeof(s_setup.name), identity->name);
    copy_field(s_setup.phone, sizeof(s_setup.phone), identity->phone);
    s_setup.role = identity->role;
#if defined(ESP_PLATFORM)
    return save_blob();
#else
    return true;
#endif
}

bool ecard_setup_save_wifi(const char *ssid, const char *password) {
    if (!ecard_setup_valid_wifi(ssid, password)) return false;
    copy_field(s_setup.wifi_ssid, sizeof(s_setup.wifi_ssid), ssid);
    copy_field(s_setup.wifi_pass, sizeof(s_setup.wifi_pass), password);
    s_setup.has_wifi = true;
#if defined(ESP_PLATFORM)
    return save_blob();
#else
    return true;
#endif
}

const char *ecard_setup_get_relay_url(void) {
    if (!s_loaded) ecard_setup_init();
    return s_relay_url;
}

bool ecard_setup_save_relay_url(const char *url) {
    if (!s_loaded) ecard_setup_init();
    if (!url || !url[0]) {
        s_relay_url[0] = '\0';
#if defined(ESP_PLATFORM)
        nvs_handle_t handle;
        if (nvs_open(SETUP_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
        const esp_err_t err = nvs_erase_key(handle, "relay_url");
        (void)nvs_commit(handle);
        nvs_close(handle);
        return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
#else
        return true;
#endif
    }

    while (*url == ' ' || *url == '\t') ++url;
    size_t len = strlen(url);
    while (len > 0 && (url[len - 1] == ' ' || url[len - 1] == '\t' || url[len - 1] == '/')) {
        --len;
    }
    if (len == 0 || len >= ECARD_RELAY_URL_MAX) return false;

    const bool is_http = (len > 7 && strncmp(url, "http://", 7) == 0);
    const bool is_https = (len > 8 && strncmp(url, "https://", 8) == 0);
    if (!is_http && !is_https) return false;

    memcpy(s_relay_url, url, len);
    s_relay_url[len] = '\0';
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(SETUP_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t err = nvs_set_str(handle, "relay_url", s_relay_url);
    if (err == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
#else
    return true;
#endif
}

bool ecard_setup_clear(void) {
    memset(&s_setup, 0, sizeof(s_setup));
    s_setup.role = ECARD_ACCOUNT_STUDENT;
    memset(s_relay_url, 0, sizeof(s_relay_url));
    ecard_cat_room_reset_all();
#if defined(ESP_PLATFORM)
    nvs_handle_t handle;
    if (nvs_open(SETUP_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t err1 = nvs_erase_key(handle, "config");
    const esp_err_t err2 = nvs_erase_key(handle, "relay_url");
    (void)nvs_commit(handle);
    nvs_close(handle);
    return (err1 == ESP_OK || err1 == ESP_ERR_NVS_NOT_FOUND) &&
           (err2 == ESP_OK || err2 == ESP_ERR_NVS_NOT_FOUND);
#else
    return true;
#endif
}

void ecard_setup_apply_profile(void) {
    if (!ecard_setup_is_provisioned()) return;

    const ecard_setup_t *cfg = ecard_setup_get();
    ecard_identity_t identity;
    memset(&identity, 0, sizeof(identity));
    copy_field(identity.school_name, sizeof(identity.school_name), cfg->school);
    copy_field(identity.student_name, sizeof(identity.student_name), cfg->name);
    copy_field(identity.class_name, sizeof(identity.class_name), cfg->class_name);
    copy_field(identity.student_id, sizeof(identity.student_id), cfg->id);
    identity.role = (cfg->role == ECARD_ACCOUNT_STUDENT) ? ECARD_ROLE_STUDENT
                                                         : ECARD_ROLE_FACULTY;
    ecard_profile_apply_identity(&identity);
}
