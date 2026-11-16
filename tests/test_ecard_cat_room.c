#include "ecard_cat_room.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void extract_json_str(const char *json, const char *key, char *out, size_t out_size) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    assert(p != NULL);
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_size) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static void test_defaults_and_points(void) {
    ecard_cat_room_reset_all();
    assert(ecard_cat_room_pending_points() == 0);
    assert(ecard_cat_room_claimed_points() == 0);
    assert(ecard_cat_room_completed_sessions() == 0);
    assert(!ecard_cat_room_is_bound());
    assert(!ecard_cat_room_is_pairing_active());
    assert(ecard_cat_room_parent_count() == 0);

    char code_buf[16];
    ecard_cat_room_get_pair_code(code_buf, sizeof(code_buf));
    assert(strlen(code_buf) == 8);

    for (int s = 0; s < ECARD_ROOM_SLOT_COUNT; ++s) {
        assert(ecard_cat_room_equipped((ecard_room_slot_t)s) == 0);
        const ecard_room_item_t *def_item = ecard_cat_room_item((ecard_room_slot_t)s, 0);
        assert(def_item != NULL);
        assert(def_item->cost == 0);
        assert(ecard_cat_room_is_unlocked((ecard_room_slot_t)s, 0));
        assert(!ecard_cat_room_is_unlocked((ecard_room_slot_t)s, 1));
    }

    char code_before[16];
    snprintf(code_before, sizeof(code_before), "%s", code_buf);
    ecard_cat_room_rotate_pair_code();
    ecard_cat_room_get_pair_code(code_buf, sizeof(code_buf));
    assert(strlen(code_buf) == 8);
    assert(strcmp(code_before, code_buf) != 0);

    char qr[160];
    assert(ecard_cat_room_build_qr_payload("AABBCCDDEEFF", "192.168.1.42", 80, qr, sizeof(qr)));
    assert(strstr(qr, "\"v\":1") != NULL);
    assert(strstr(qr, "\"id\":\"AABBCCDDEEFF\"") != NULL);
    assert(strstr(qr, code_buf) != NULL);
    assert(strstr(qr, "\"ip\":\"192.168.1.42\"") != NULL);

    // 25 min -> +10 pts, 50 min -> +20 pts, 0 min -> 0
    assert(ecard_cat_room_award_focus_minutes(25) == 10);
    assert(ecard_cat_room_award_focus_minutes(50) == 20);
    assert(ecard_cat_room_award_focus_minutes(0) == 0);
    assert(ecard_cat_room_pending_points() == 30);
    assert(ecard_cat_room_completed_sessions() == 2);
}

static void test_http_pairing_privacy_and_permissions(void) {
    ecard_cat_room_reset_all();
    ecard_cat_room_set_now_ms(1000);
    char resp[3072];
    bool dirty = false;

    // Public status must NOT expose pair_code, invite_code, parent_id, or tokens
    char pair_code[16];
    ecard_cat_room_get_pair_code(pair_code, sizeof(pair_code));
    int code = ecard_cat_room_handle_http("GET", "/api/pair/status", NULL, NULL,
                                          "AABBCCDDEEFF", "192.168.1.42", 80,
                                          resp, sizeof(resp), &dirty);
    assert(code == 200);
    assert(!dirty);
    assert(strstr(resp, "\"bound\":false") != NULL);
    assert(strstr(resp, "\"parent_count\":0") != NULL);
    assert(strstr(resp, pair_code) == NULL);

    // Unauthenticated endpoints return 401
    assert(ecard_cat_room_handle_http("GET", "/api/cat-room", NULL, NULL,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 401);
    assert(ecard_cat_room_handle_http("GET", "/api/pair/members", NULL, NULL,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 401);

    // Bind fails with 403 pairing_window_closed when pairing page is not active
    char body[192];
    snprintf(body, sizeof(body),
             "{\"code\":\"%s\",\"parent_id\":\"p_mom\",\"label\":\"Mom\"}", pair_code);
    assert(ecard_cat_room_handle_http("POST", "/api/pair/bind", NULL, body,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 403);
    assert(strstr(resp, "\"pairing_window_closed\"") != NULL);

    // Open pairing window: wrong code -> 403; 5 failures -> rate-limit lockout + auto pair_code rotation
    ecard_cat_room_set_pairing_active(true);
    assert(ecard_cat_room_is_pairing_active());
    for (int i = 0; i < 4; ++i) {
        dirty = false;
        assert(ecard_cat_room_handle_http("POST", "/api/pair/bind", NULL,
                                            "{\"code\":\"WRONGCOD\",\"parent_id\":\"p_mom\"}",
                                            "AABBCCDDEEFF", "192.168.1.42", 80,
                                            resp, sizeof(resp), &dirty) == 403);
        assert(!dirty);
    }
    dirty = false;
    assert(ecard_cat_room_handle_http("POST", "/api/pair/bind", NULL,
                                        "{\"code\":\"WRONGCOD\",\"parent_id\":\"p_mom\"}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 403);
    assert(dirty);
    char rotated_code[16];
    ecard_cat_room_get_pair_code(rotated_code, sizeof(rotated_code));
    assert(strcmp(rotated_code, pair_code) != 0);

    // Locked out for 60s -> even valid code returns 429 rate_limited
    snprintf(body, sizeof(body),
             "{\"code\":\"%s\",\"parent_id\":\"p_mom\",\"label\":\"Mom\"}", rotated_code);
    assert(ecard_cat_room_handle_http("POST", "/api/pair/bind", NULL, body,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 429);
    assert(strstr(resp, "\"rate_limited\"") != NULL);

    // After 60s lockout expires, valid rotated code binds primary parent
    ecard_cat_room_set_now_ms(62000);
    code = ecard_cat_room_handle_http("POST", "/api/pair/bind", NULL, body,
                                      "AABBCCDDEEFF", "192.168.1.42", 80,
                                      resp, sizeof(resp), &dirty);
    assert(code == 200);
    assert(ecard_cat_room_is_bound());
    assert(ecard_cat_room_parent_count() == 1);
    assert(strstr(resp, "\"role\":\"primary\"") != NULL);

    char mom_token[48];
    char invite_code[24];
    extract_json_str(resp, "token", mom_token, sizeof(mom_token));
    extract_json_str(resp, "invite_code", invite_code, sizeof(invite_code));
    assert(strlen(mom_token) == 32);
    assert(strlen(invite_code) >= 6);

    // Re-bind when already bound -> 409
    assert(ecard_cat_room_handle_http("POST", "/api/pair/bind", NULL, body,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 409);

    // Public status while bound still hides parent IDs and invite code
    code = ecard_cat_room_handle_http("GET", "/api/pair/status", NULL, NULL,
                                      "AABBCCDDEEFF", "192.168.1.42", 80,
                                      resp, sizeof(resp), &dirty);
    assert(code == 200);
    assert(strstr(resp, "\"bound\":true") != NULL);
    assert(strstr(resp, "p_mom") == NULL);
    assert(strstr(resp, invite_code) == NULL);
    assert(strstr(resp, mom_token) == NULL);

    // Join rate-limit: 5 bad invite codes trigger 60s 429 lockout
    for (int i = 0; i < 5; ++i) {
        assert(ecard_cat_room_handle_http("POST", "/api/pair/join", NULL,
                                            "{\"invite_code\":\"BADINVIT\",\"parent_id\":\"p_dad\"}",
                                            "AABBCCDDEEFF", "192.168.1.42", 80,
                                            resp, sizeof(resp), &dirty) == 403);
    }
    snprintf(body, sizeof(body),
             "{\"invite_code\":\"%s\",\"parent_id\":\"p_dad\",\"label\":\"Dad\"}", invite_code);
    assert(ecard_cat_room_handle_http("POST", "/api/pair/join", NULL, body,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 429);
    ecard_cat_room_set_now_ms(125000);

    // Join secondary parent succeeds after lockout expires
    char mom_auth[80];
    snprintf(mom_auth, sizeof(mom_auth), "Bearer %s", mom_token);
    code = ecard_cat_room_handle_http("POST", "/api/pair/join", NULL, body,
                                      "AABBCCDDEEFF", "192.168.1.42", 80,
                                      resp, sizeof(resp), &dirty);
    assert(code == 200);
    assert(strstr(resp, "\"role\":\"secondary\"") != NULL);
    assert(ecard_cat_room_parent_count() == 2);

    char dad_token[48];
    extract_json_str(resp, "token", dad_token, sizeof(dad_token));
    char dad_auth[80];
    snprintf(dad_auth, sizeof(dad_auth), "Bearer %s", dad_token);

    // Malformed bool (trueabc) must not rotate invite_code
    assert(ecard_cat_room_handle_http("POST", "/api/pair/invite", mom_auth,
                                        "{\"rotate\":trueabc}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(strstr(resp, invite_code) != NULL);

    // Secondary cannot rotate invite via POST /api/pair/invite (403), but can GET /api/pair/invite
    assert(ecard_cat_room_handle_http("POST", "/api/pair/invite", dad_auth, "{\"rotate\":true}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 403);
    assert(ecard_cat_room_handle_http("GET", "/api/pair/invite", dad_auth, NULL,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(strstr(resp, invite_code) != NULL);

    // Members listing returns both parents with roles & labels
    assert(ecard_cat_room_handle_http("GET", "/api/pair/members", dad_auth, NULL,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(strstr(resp, "\"parent_id\":\"p_mom\"") != NULL);
    assert(strstr(resp, "\"parent_id\":\"p_dad\"") != NULL);
    assert(strstr(resp, "\"label\":\"Dad\"") != NULL);

    // Max 4 secondary parents -> 5th returns 409
    for (int i = 2; i <= 4; ++i) {
        snprintf(body, sizeof(body),
                 "{\"invite_code\":\"%s\",\"parent_id\":\"p_ext%d\"}", invite_code, i);
        assert(ecard_cat_room_handle_http("POST", "/api/pair/join", NULL, body,
                                            "AABBCCDDEEFF", "192.168.1.42", 80,
                                            resp, sizeof(resp), &dirty) == 200);
    }
    assert(ecard_cat_room_parent_count() == 5);
    snprintf(body, sizeof(body),
             "{\"invite_code\":\"%s\",\"parent_id\":\"p_overflow\"}", invite_code);
    assert(ecard_cat_room_handle_http("POST", "/api/pair/join", NULL, body,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 409);

    // Secondary cannot unbind primary (403), can unbind self (200)
    assert(ecard_cat_room_handle_http("POST", "/api/pair/unbind", dad_auth,
                                        "{\"target_parent_id\":\"p_mom\"}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 403);
    assert(ecard_cat_room_handle_http("POST", "/api/pair/unbind", dad_auth, "{}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(ecard_cat_room_parent_count() == 4);

    // Claim, redeem, equip as primary parent
    (void)ecard_cat_room_award_focus_minutes(50); // +20 pending
    dirty = false;
    assert(ecard_cat_room_handle_http("POST", "/api/cat-room/claim", mom_auth, "{}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(dirty);
    assert(strstr(resp, "\"claimed_delta\":20") != NULL);
    assert(ecard_cat_room_pending_points() == 0);
    assert(ecard_cat_room_claimed_points() == 20);

    // Second claim is atomic & idempotent with 0 delta
    dirty = false;
    assert(ecard_cat_room_handle_http("POST", "/api/cat-room/claim", mom_auth, "{}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(!dirty);
    assert(strstr(resp, "\"claimed_delta\":0") != NULL);

    // Equip locked chair fails with 403
    assert(ecard_cat_room_handle_http("POST", "/api/cat-room/equip", mom_auth,
                                        "{\"slot\":\"chair\",\"item_id\":\"chair_cat_pink\"}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 403);

    // Redeem too-expensive item fails with 400
    assert(ecard_cat_room_handle_http("POST", "/api/cat-room/redeem", mom_auth,
                                        "{\"item_id\":\"chair_crown\"}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 400);

    // Redeem affordable item (chair_cat_pink = 20 pts) -> 200 + ui_dirty, then equip -> 200 + ui_dirty
    dirty = false;
    assert(ecard_cat_room_handle_http("POST", "/api/cat-room/redeem", mom_auth,
                                        "{\"item_id\":\"chair_cat_pink\"}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(dirty);
    assert(ecard_cat_room_claimed_points() == 0);
    assert(ecard_cat_room_is_unlocked(ECARD_ROOM_SLOT_CHAIR, 1));

    dirty = false;
    assert(ecard_cat_room_handle_http("POST", "/api/cat-room/equip", mom_auth,
                                        "{\"slot\":\"chair\",\"item_id\":\"chair_cat_pink\"}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(dirty);
    assert(ecard_cat_room_equipped(ECARD_ROOM_SLOT_CHAIR) == 1);

    // Oversized buffer (4096B) returns 200 with full 24 items cleanly terminated
    char large_resp[4096];
    assert(ecard_cat_room_handle_http("GET", "/api/cat-room", mom_auth, NULL,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        large_resp, sizeof(large_resp), &dirty) == 200);
    assert(strstr(large_resp, "\"chair\":\"chair_cat_pink\"") != NULL);
    assert(strstr(large_resp, "\"desk_lucky_cat\"") != NULL);
    assert(strstr(large_resp, "\"rug_paw\"") != NULL);

    // Undersized buffer must return clean JSON error ("resp_overflow"), never truncated JSON
    char small_resp[512];
    assert(ecard_cat_room_handle_http("GET", "/api/cat-room", mom_auth, NULL,
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        small_resp, sizeof(small_resp), &dirty) == 500);
    assert(strstr(small_resp, "\"resp_overflow\"") != NULL);

    // Nested JSON key spoofing & oversized parent_id must fail cleanly
    assert(ecard_cat_room_handle_http(
               "POST", "/api/pair/join", NULL,
               "{\"extra\":{\"invite_code\":\"FAKE1234\"},\"parent_id\":\"p_nest\"}",
               "AABBCCDDEEFF", "192.168.1.42", 80,
               resp, sizeof(resp), &dirty) == 400);
    assert(ecard_cat_room_handle_http(
               "POST", "/api/pair/join", NULL,
               "{\"invite_code\":\"FAKE1234\",\"parent_id\":\"01234567890123456789012345678901234567890123456789\"}",
               "AABBCCDDEEFF", "192.168.1.42", 80,
               resp, sizeof(resp), &dirty) == 400);

    // Primary self-unbind clears all parents while preserving room unlocks/equipped
    assert(ecard_cat_room_handle_http("POST", "/api/pair/unbind", mom_auth, "{}",
                                        "AABBCCDDEEFF", "192.168.1.42", 80,
                                        resp, sizeof(resp), &dirty) == 200);
    assert(!ecard_cat_room_is_bound());
    assert(ecard_cat_room_parent_count() == 0);
    assert(ecard_cat_room_equipped(ECARD_ROOM_SLOT_CHAIR) == 1);
    assert(ecard_cat_room_is_unlocked(ECARD_ROOM_SLOT_CHAIR, 1));
    ecard_cat_room_set_pairing_active(false);
}

int main(void) {
    printf("Running ecard_cat_room tests...\n");
    test_defaults_and_points();
    test_http_pairing_privacy_and_permissions();
    printf("ecard_cat_room tests passed!\n");
    return 0;
}
