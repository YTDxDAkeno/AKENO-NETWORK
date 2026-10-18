#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "context.h"
#include "log.h"
#include "redact.h"
#include "test.h"

static void test_redact(void) {
    char out[512];
    akn_redact(out, sizeof(out), "token akd_AbCdEf0123456789_-xyz end");
    CHECK_STR(out, "token akd_[redacted] end");
    akn_redact(out, sizeof(out), "code=akp_0123456789abcdef");
    CHECK_STR(out, "code=akp_[redacted]");
    akn_redact(out, sizeof(out), "akd_abc short"); /* too short to be a secret */
    CHECK_STR(out, "akd_abc short");
    akn_redact(out, sizeof(out), "makd_0123456789abcdef"); /* part of a word */
    CHECK_STR(out, "makd_0123456789abcdef");
    akn_redact(out, sizeof(out), "Authorization: Bearer abc.def-123, next");
    CHECK_STR(out, "Authorization: Bearer [redacted], next");
    akn_redact(out, sizeof(out), "X-Akeno-Instance-Key: 0123abcd\r\nHost: x");
    CHECK_STR(out, "X-Akeno-Instance-Key: [redacted]\r\nHost: x");
    akn_redact(out, sizeof(out), "key 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef!");
    CHECK_STR(out, "key [redacted-hex]!");
    akn_redact(out, sizeof(out), "build 2ff692763c5c ok"); /* short hex stays */
    CHECK_STR(out, "build 2ff692763c5c ok");
    akn_redact(out, sizeof(out), "GET https://akeno.example.org:8443/api/v1/devices/current failed");
    CHECK_STR(out, "GET https://[server]/api/v1/devices/current failed");
    akn_redact(out, sizeof(out), "wss://akeno.example.org/ws and http://127.0.0.1:8642/api/instance");
    CHECK_STR(out, "wss://[server]/ws and http://127.0.0.1:8642/api/instance");
    akn_redact(out, sizeof(out), "\"server\":\"https://my.host\"");
    CHECK_STR(out, "\"server\":\"https://[server]\"");
    /* Truncation always terminates. */
    char small[8];
    akn_redact(small, sizeof(small), "0123456789");
    CHECK_STR(small, "0123456");
    akn_redact(small, sizeof(small), NULL);
    CHECK_STR(small, "");

    CHECK_STR(akn_describe_server_url(""), "not configured");
    CHECK_STR(akn_describe_server_url(NULL), "not configured");
    CHECK_STR(akn_describe_server_url("https://a.b"), "https (host hidden)");
    CHECK_STR(akn_describe_server_url("http://127.0.0.1:3000"), "http to loopback");
    CHECK_STR(akn_describe_server_url("http://evil.example"), "invalid");
}

static uint64_t fake_ms = 1000;
static uint64_t now_fake(void) { return fake_ms; }

static void test_log(void) {
    char dir[] = "/tmp/akn-log-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[128];
    snprintf(path, sizeof(path), "%s/native.log", dir);
    akn_log log;
    CHECK(akn_log_open(&log, path, 600, now_fake) == 0);
    fake_ms = 3500;
    akn_logf(&log, AKN_LOG_INFO, "test.secret", "token akd_0123456789abcdef at https://private.example/x");
    char lines[AKN_LOG_RING][AKN_LOG_LINE];
    CHECK(akn_log_recent(&log, lines, AKN_LOG_RING) == 1);
    CHECK_STR(lines[0], "+2.500s INFO test.secret: token akd_[redacted] at https://[server]/x");
    for (int i = 0; i < 20; i++) akn_logf(&log, AKN_LOG_WARN, "fill", "line %d with some padding text", i);
    CHECK(akn_log_recent(&log, lines, AKN_LOG_RING) == AKN_LOG_RING);
    CHECK_CONTAINS(lines[AKN_LOG_RING - 1], "line 19");
    akn_log_close(&log);
    /* Rotation kept the file under its limit and moved the old part aside. */
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    char buf[2048] = "";
    size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = '\0';
    CHECK(n <= 600);
    CHECK(strstr(buf, "akd_0123") == NULL);
    char old[160];
    snprintf(old, sizeof(old), "%s.1", path);
    CHECK(access(old, F_OK) == 0);
    remove(old);
    remove(path);
    rmdir(dir);
    /* Without a path the ring still works. */
    akn_log mem;
    CHECK(akn_log_open(&mem, NULL, 0, now_fake) == -1);
    akn_logf(&mem, AKN_LOG_ERROR, "e", "x");
    CHECK(akn_log_recent(&mem, lines, 1) == 1);
    akn_log_close(&mem);
}

static void put_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void test_context(void) {
    akn_context_raw raw;
    memset(&raw, 0, sizeof(raw));
    CHECK(akn_context_classify(&raw) == AKN_CTX_UNKNOWN);
    CHECK(!akn_context_allows_devices(&raw, AKN_CTX_UNKNOWN));

    /* Foreground: our app ID is the running big app (FAKE00000 under a homebrew launcher). */
    raw.have_appinfo_fn = 1;
    raw.have_bigapp_fn = 1;
    put_le32(raw.appinfo_raw, 0x60000123u);
    memcpy(raw.appinfo_raw + 20, "FAKE00000", 9);
    akn_context_decode(&raw);
    CHECK(raw.own_app_id == 0x60000123u);
    CHECK_STR(raw.own_title_id, "FAKE00000");
    raw.bigapp_app_id = 0x60000123;
    CHECK(akn_context_classify(&raw) == AKN_CTX_FOREGROUND);
    CHECK(akn_context_allows_devices(&raw, AKN_CTX_FOREGROUND));
    char text[200];
    akn_context_describe(&raw, AKN_CTX_FOREGROUND, text, sizeof(text));
    CHECK_STR(text, "foreground application (own app 0x60000123 FAKE00000, foreground app 0x60000123)");

    /* A game is in front: background. */
    raw.bigapp_app_id = 0x60000456;
    CHECK(akn_context_classify(&raw) == AKN_CTX_BACKGROUND);
    CHECK(!akn_context_allows_devices(&raw, AKN_CTX_BACKGROUND));
    /* Home screen, nothing in front: background. */
    raw.bigapp_app_id = (int32_t)0x80AA0001;
    CHECK(akn_context_classify(&raw) == AKN_CTX_BACKGROUND);
    /* A daemon process without an app ID. */
    memset(raw.appinfo_raw, 0, sizeof(raw.appinfo_raw));
    akn_context_decode(&raw);
    raw.bigapp_app_id = 0x60000456;
    CHECK(akn_context_classify(&raw) == AKN_CTX_BACKGROUND);
    /* The launcher argument wins, and the disagreement is reported. */
    raw.launcher_arg = 1;
    CHECK(akn_context_allows_devices(&raw, AKN_CTX_BACKGROUND));
    akn_context_describe(&raw, AKN_CTX_BACKGROUND, text, sizeof(text));
    CHECK_CONTAINS(text, "--launcher");
    CHECK_CONTAINS(text, "check disagrees");
    /* Title ID at the alternative offset (samples/test_privileges layout). */
    memset(raw.appinfo_raw, 0, sizeof(raw.appinfo_raw));
    put_le32(raw.appinfo_raw, 7);
    memcpy(raw.appinfo_raw + 16, "FAKE00000", 9);
    akn_context_decode(&raw);
    CHECK_STR(raw.own_title_id, "FAKE00000");
    /* A failing query gives "unknown". */
    raw.appinfo_rc = -1;
    akn_context_decode(&raw);
    CHECK(raw.own_app_id == 0);
    CHECK(akn_context_classify(&raw) == AKN_CTX_UNKNOWN);
    akn_context_describe(&raw, AKN_CTX_UNKNOWN, text, sizeof(text));
    CHECK_CONTAINS(text, "error 0xffffffff");
    akn_context_describe(&raw, AKN_CTX_DESKTOP, text, sizeof(text));
    CHECK_CONTAINS(text, "desktop");
}

void run_redact_tests(void) {
    test_redact();
    test_log();
}

void run_context_tests(void) { test_context(); }
