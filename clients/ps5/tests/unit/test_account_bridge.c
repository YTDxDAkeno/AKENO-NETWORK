/* AKENO Account Bridge console-side logic (app/account_bridge.c). Pure logic, no system calls. */
#include <string.h>

#include "account_bridge.h"
#include "buf.h"
#include "test.h"

static void test_clean_name(void) {
    char out[AKENO_BRIDGE_NAME_MAX + 1];

    akeno_bridge_clean_name("Justin PS5", out, sizeof(out));
    CHECK_STR(out, "Justin PS5");

    /* Disallowed ASCII becomes '_', surrounding spaces are trimmed. */
    akeno_bridge_clean_name("  a/b<c>  ", out, sizeof(out));
    CHECK_STR(out, "a_b_c_");

    /* Empty or whitespace-only falls back to a safe placeholder. */
    akeno_bridge_clean_name("   ", out, sizeof(out));
    CHECK_STR(out, "PS5 user");
    akeno_bridge_clean_name(NULL, out, sizeof(out));
    CHECK_STR(out, "PS5 user");

    /* Multibyte UTF-8 is kept and never cut mid-sequence. "ゆ" is 3 bytes; 11 of them is 33 bytes. */
    char many[64] = "";
    for (int i = 0; i < 11; i++) strcat(many, "\xe3\x82\x86");
    akeno_bridge_clean_name(many, out, sizeof(out));
    CHECK(strlen(out) <= AKENO_BRIDGE_NAME_MAX);
    CHECK(strlen(out) % 3 == 0); /* whole characters only */
    CHECK(strlen(out) == 30);    /* 10 characters fit in 32 bytes; the 11th would overflow */
}

static void test_account_state(void) {
    akeno_account_read read;
    memset(&read, 0, sizeof(read));
    CHECK_STR(akeno_bridge_account_state(NULL), "not_checked");
    read.done = 1;
    read.slot = 0;
    CHECK_STR(akeno_bridge_account_state(&read), "check_failed");
    read.slot = 3;
    read.account_rc = 0;
    read.has_account_id = 0;
    CHECK_STR(akeno_bridge_account_state(&read), "no_account_id");
    read.has_account_id = 1;
    CHECK_STR(akeno_bridge_account_state(&read), "account_id_present");
    read.account_rc = -1;
    CHECK_STR(akeno_bridge_account_state(&read), "check_failed");
}

static void test_link_body(void) {
    akeno_console_user user;
    user.user_id = 268435457;
    snprintf(user.name, sizeof(user.name), "%s", "Justin");
    akeno_buf b;
    akeno_buf_init(&b, 128);
    CHECK(akeno_bridge_link_body(&user, NULL, &b) == 0);
    CHECK_STR(b.data, "{\"localUserId\":268435457,\"userName\":\"Justin\"}");
    akeno_buf_reset(&b);
    CHECK(akeno_bridge_link_body(&user, "no_account_id", &b) == 0);
    CHECK_STR(b.data, "{\"localUserId\":268435457,\"userName\":\"Justin\",\"accountState\":\"no_account_id\"}");
    akeno_buf_free(&b);
}

static void present_libs(akeno_account_presence *p) {
    memset(p, 0, sizeof(*p));
    p->done = 1;
    p->regmgr_loaded = p->regmgr_get_int = p->regmgr_get_bin = 1;
    p->remoteplay_loaded = 1;
    for (int i = 0; i < AKENO_RP_PAIRING_NAMES; i++) p->remoteplay_names[i] = 1;
}

static void test_assess(void) {
    akeno_account_presence p;
    akeno_account_read read;
    memset(&read, 0, sizeof(read));

    CHECK_STR(akeno_bridge_assess(NULL, NULL).code, "not-checked");

    /* Remote Play library/functions missing is a hard block. */
    present_libs(&p);
    p.remoteplay_names[2] = 0;
    CHECK_STR(akeno_bridge_assess(&p, NULL).code, "blocked-no-library");

    /* Libraries present, no read yet: presence only, never a claim that pairing works. */
    present_libs(&p);
    CHECK_STR(akeno_bridge_assess(&p, NULL).code, "presence-only");

    /* No account ID blocks pairing; AKENO does not create one. */
    read.done = 1;
    read.slot = 2;
    read.account_rc = 0;
    read.rp_global_rc = 0;
    read.has_account_id = 0;
    read.rp_global = 1;
    CHECK_STR(akeno_bridge_assess(&p, &read).code, "blocked-no-account-id");

    /* Account ID present but Remote Play off in settings. */
    read.has_account_id = 1;
    read.rp_global = 0;
    CHECK_STR(akeno_bridge_assess(&p, &read).code, "blocked-setting-off");

    /* Everything readable is in place: prerequisites present, still not verified. */
    read.rp_global = 1;
    CHECK_STR(akeno_bridge_assess(&p, &read).code, "prerequisites-present");

    /* A failed read is inconclusive, never a green light. */
    read.account_rc = -1;
    CHECK_STR(akeno_bridge_assess(&p, &read).code, "incomplete");
}

static void test_results_json_stays_presence_only(void) {
    akeno_account_presence p;
    present_libs(&p);
    akeno_account_read read;
    memset(&read, 0, sizeof(read));
    read.done = 1;
    read.slot = 1;
    read.account_rc = 0;
    read.has_account_id = 1;
    read.rp_global_rc = 0;
    read.rp_global = 1;
    akeno_buf b;
    akeno_buf_init(&b, 1024);
    akeno_buf_append_str(&b, "{");
    akeno_bridge_results_json(&p, &read, &b);
    akeno_buf_append_str(&b, "}");
    /* The account ID value is never serialized; only its presence. */
    CHECK(strstr(b.data, "hasAccountId") != NULL);
    CHECK(strstr(b.data, "accountId\":") == NULL);
    CHECK(strstr(b.data, "\"accountState\":\"account_id_present\"") != NULL);
    CHECK(strstr(b.data, "prerequisites-present") != NULL);
    akeno_buf_free(&b);
}

void run_account_bridge_tests(void) {
    test_clean_name();
    test_account_state();
    test_link_body();
    test_assess();
    test_results_json_stays_presence_only();
}
