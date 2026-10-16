/* SharePlay Player 2 diagnostic receiver (app/shareplay_rx.c). */
#include <stdio.h>
#include <string.h>

#include "akeno/json.h"
#include "buf.h"
#include "shareplay_rx.h"
#include "test.h"

#define GRANT_A "6f1c0d2e-1111-4a3b-8c9d-000000000001"
#define GRANT_B "6f1c0d2e-1111-4a3b-8c9d-000000000002"
#define SESSION "0b3e5a41-2222-4c3d-9e8f-000000000003"
#define GUEST "9a8b7c6d-3333-4e5f-a0b1-000000000004"

static akeno_sp_result feed(akeno_sp_rx *rx, long long now, const char *json) {
    akeno_json_doc *doc = akeno_json_parse(json, strlen(json), NULL, 0);
    CHECK(doc != NULL);
    if (!doc) return AKENO_SP_INVALID;
    akeno_sp_result result = akeno_sp_rx_apply(rx, akeno_json_root(doc), now);
    akeno_json_free(doc);
    return result;
}

static akeno_sp_result state(akeno_sp_rx *rx, long long now, const char *grant, unsigned epoch, unsigned seq, unsigned buttons, const char *axes) {
    char json[600];
    snprintf(json, sizeof(json),
             "{\"sessionId\":\"" SESSION "\",\"grantId\":\"%s\",\"epoch\":%u,\"guestId\":\"" GUEST "\",\"slot\":2,\"seq\":%u,"
             "\"kind\":\"state\",\"buttons\":%u,\"axes\":%s,\"triggers\":[0,255],\"senderTime\":1,\"serverTime\":2}",
             grant, epoch, seq, buttons, axes);
    return feed(rx, now, json);
}

static void test_ordering(void) {
    akeno_sp_rx rx;
    akeno_sp_rx_init(&rx);
    akeno_sp_state current;
    CHECK(akeno_sp_rx_current(&rx, 1000, &current) == 0); /* never received: neutral, not a trip */
    CHECK(current.buttons == 0);

    CHECK(state(&rx, 1000, GRANT_A, 7, 5, 1, "[32767,-32767,0,100]") == AKENO_SP_APPLIED);
    CHECK(akeno_sp_rx_current(&rx, 1010, &current) == 0);
    CHECK(current.buttons == 1 && current.axes[0] == 32767 && current.axes[1] == -32767 && current.triggers[1] == 255);
    CHECK(state(&rx, 1020, GRANT_A, 7, 4, 2, "[0,0,0,0]") == AKENO_SP_STALE);  /* late */
    CHECK(state(&rx, 1030, GRANT_A, 7, 5, 2, "[0,0,0,0]") == AKENO_SP_STALE);  /* duplicate */
    CHECK(state(&rx, 1040, GRANT_A, 7, 0, 2, "[0,0,0,0]") == AKENO_SP_STALE);  /* zero */
    akeno_sp_rx_current(&rx, 1040, &current);
    CHECK(current.buttons == 1);
    CHECK(rx.stale == 3 && rx.accepted == 1);

    /* A new grant (new approval) starts its numbering again. */
    CHECK(state(&rx, 1050, GRANT_B, 8, 1, 4, "[0,0,0,0]") == AKENO_SP_APPLIED);
    akeno_sp_rx_current(&rx, 1050, &current);
    CHECK(current.buttons == 4);
    CHECK_STR(rx.grant_id, GRANT_B);
}

static void test_release_and_watchdog(void) {
    akeno_sp_rx rx;
    akeno_sp_rx_init(&rx);
    akeno_sp_state current;
    CHECK(state(&rx, 5000, GRANT_A, 1, 10, 32, "[1,2,3,4]") == AKENO_SP_APPLIED);
    /* Releases apply whatever their sequence number. */
    CHECK(feed(&rx, 5010,
               "{\"sessionId\":\"" SESSION "\",\"grantId\":\"" GRANT_A "\",\"epoch\":1,\"guestId\":\"" GUEST "\",\"slot\":2,\"seq\":3,"
               "\"kind\":\"release\",\"buttons\":0,\"axes\":[0,0,0,0],\"triggers\":[0,0],\"senderTime\":0,\"serverTime\":0,"
               "\"reason\":\"The host revoked Player 2.\"}") == AKENO_SP_APPLIED);
    akeno_sp_rx_current(&rx, 5011, &current);
    CHECK(current.buttons == 0 && current.axes[0] == 0 && current.triggers[1] == 0);
    CHECK_STR(rx.last_reason, "The host revoked Player 2.");
    CHECK(rx.releases == 1);

    /* Held input survives within the watchdog window, then becomes neutral. */
    CHECK(state(&rx, 6000, GRANT_A, 1, 11, 1, "[0,0,0,0]") == AKENO_SP_APPLIED);
    CHECK(akeno_sp_rx_current(&rx, 6000 + AKENO_SP_WATCHDOG_MS, &current) == 0);
    CHECK(current.buttons == 1);
    CHECK(akeno_sp_rx_current(&rx, 6001 + AKENO_SP_WATCHDOG_MS, &current) == 1);
    CHECK(current.buttons == 0);
    CHECK(rx.watchdog_trips == 1);
    akeno_sp_rx_current(&rx, 7000, &current);
    CHECK(rx.watchdog_trips == 1); /* counted once per stall */
    CHECK(state(&rx, 7100, GRANT_A, 1, 12, 2, "[0,0,0,0]") == AKENO_SP_APPLIED);
    CHECK(akeno_sp_rx_current(&rx, 7100, &current) == 0 && current.buttons == 2);
}

static void test_invalid(void) {
    akeno_sp_rx rx;
    akeno_sp_rx_init(&rx);
    /* Blocked buttons (PS = 1<<16, Create = 1<<8), unknown bits, out-of-range axes and triggers, wrong slot. */
    CHECK(state(&rx, 1, GRANT_A, 1, 1, 1u << 16, "[0,0,0,0]") == AKENO_SP_INVALID);
    CHECK(state(&rx, 1, GRANT_A, 1, 1, 1u << 8, "[0,0,0,0]") == AKENO_SP_INVALID);
    CHECK(state(&rx, 1, GRANT_A, 1, 1, 1u << 20, "[0,0,0,0]") == AKENO_SP_INVALID);
    CHECK(state(&rx, 1, GRANT_A, 1, 1, 0, "[-32768,0,0,0]") == AKENO_SP_INVALID);
    CHECK(state(&rx, 1, GRANT_A, 1, 1, 0, "[0,0,0]") == AKENO_SP_INVALID);
    CHECK(state(&rx, 1, GRANT_A, 1, 1, 0, "[0.5,0,0,0]") == AKENO_SP_INVALID);
    CHECK(state(&rx, 1, "not-a-uuid!", 1, 1, 0, "[0,0,0,0]") == AKENO_SP_INVALID);
    CHECK(feed(&rx, 1, "{\"kind\":\"state\"}") == AKENO_SP_INVALID);
    CHECK(feed(&rx, 1, "[]") == AKENO_SP_INVALID);
    CHECK(feed(&rx, 1,
               "{\"sessionId\":\"" SESSION "\",\"grantId\":\"" GRANT_A "\",\"epoch\":1,\"guestId\":\"" GUEST "\",\"slot\":1,\"seq\":1,"
               "\"kind\":\"state\",\"buttons\":0,\"axes\":[0,0,0,0],\"triggers\":[0,0]}") == AKENO_SP_INVALID);
    CHECK(feed(&rx, 1,
               "{\"sessionId\":\"" SESSION "\",\"grantId\":\"" GRANT_A "\",\"epoch\":1,\"guestId\":\"" GUEST "\",\"slot\":2,\"seq\":1,"
               "\"kind\":\"state\",\"buttons\":0,\"axes\":[0,0,0,0],\"triggers\":[0,256]}") == AKENO_SP_INVALID);
    CHECK(rx.invalid == 11 && rx.accepted == 0 && !rx.have_grant);
}

static void test_report(void) {
    akeno_sp_rx rx;
    akeno_sp_rx_init(&rx);
    state(&rx, 100, GRANT_A, 3, 1, 1, "[5,0,0,0]");
    akeno_buf out;
    akeno_buf_init(&out, 4096);
    CHECK(akeno_sp_rx_write_json(&rx, 120, &out) == 0);
    akeno_json_doc *doc = akeno_json_parse(out.data, out.len, NULL, 0);
    CHECK(doc != NULL);
    if (doc) {
        const akeno_json *root = akeno_json_root(doc);
        CHECK_STR(akeno_json_string(akeno_json_path(root, "receiver.grantId"), ""), GRANT_A);
        CHECK(akeno_json_get_bool(akeno_json_get(root, "receiver"), "neutralByWatchdog", 1) == 0);
        CHECK(akeno_json_get_bool(akeno_json_path(root, "capabilities.virtualController"), "supported", 1) == 0);
        CHECK(akeno_json_get_bool(akeno_json_path(root, "capabilities.nativeCapture"), "supported", 1) == 0);
        akeno_json_free(doc);
    }
    akeno_buf_free(&out);
    const char *reason = NULL;
    CHECK(akeno_sp_virtual_pad_open(&reason) == -1 && reason && strstr(reason, "Unsupported") != NULL);
    CHECK(akeno_sp_native_capture_open(&reason) == -1 && reason && strstr(reason, "Unsupported") != NULL);
}

void run_shareplay_tests(void);
void run_shareplay_tests(void) {
    test_ordering();
    test_release_and_watchdog();
    test_invalid();
    test_report();
}
