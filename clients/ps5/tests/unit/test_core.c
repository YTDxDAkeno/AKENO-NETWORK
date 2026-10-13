/* Unit tests for the platform-neutral core: JSON, HTTP parsing, WebSocket framing, URLs,
 * backoff, capability policy, Base64/SHA-1 and log redaction. No network access. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akeno/caps.h"
#include "akeno/json.h"
#include "backoff.h"
#include "buf.h"
#include "client_internal.h"
#include "crypto_util.h"
#include "http.h"
#include "test.h"
#include "url.h"
#include "ws.h"

static akeno_json_doc *parse(const char *text) {
    return akeno_json_parse(text, strlen(text), NULL, 0);
}

static void test_json_valid(void) {
    akeno_json_doc *doc = parse(
        "{\"friends\":[{\"onlineId\":\"neo\",\"presence\":{\"status\":\"online\"}}],"
        "\"count\":42,\"big\":9007199254740993,\"ok\":true,\"none\":null,\"text\":\"a\\\"b\\u00e9\\ud83d\\ude00\"}");
    CHECK(doc != NULL);
    const akeno_json *root = akeno_json_root(doc);
    CHECK_STR(akeno_json_string(akeno_json_path(akeno_json_at(akeno_json_get(root, "friends"), 0), "presence.status"), NULL),
              "online");
    int64_t n = 0;
    CHECK(akeno_json_int64(akeno_json_get(root, "count"), &n) == 0 && n == 42);
    CHECK(akeno_json_int64(akeno_json_get(root, "big"), &n) == 0 && n == 9007199254740993LL);
    CHECK(akeno_json_get_bool(root, "ok", 0) == 1);
    CHECK(akeno_json_get(root, "none")->type == AKENO_JSON_NULL);
    CHECK_STR(akeno_json_get_string(root, "text", NULL), "a\"b\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(akeno_json_get_string(root, "missing", NULL) == NULL);
    CHECK(akeno_json_get_string(root, "count", NULL) == NULL); /* Wrong type falls back. */
    akeno_json_free(doc);
}

static void test_json_invalid(void) {
    const char *bad[] = {
        "", "{", "{\"a\":}", "[1,]", "{\"a\" 1}", "01", "1.", "-", "\"unterminated", "\"bad\\x\"", "\"\\ud800\"",
        "\"\\udc00\"", "\"\\u0000\"", "nul", "{} extra", "\"tab\there\"", "1e", "[1 2]", "{\"a\":1,}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char err[64] = "";
        akeno_json_doc *doc = akeno_json_parse(bad[i], strlen(bad[i]), err, sizeof(err));
        if (doc) fprintf(stderr, "unexpectedly parsed: %s\n", bad[i]);
        CHECK(doc == NULL);
        CHECK(err[0] != '\0');
    }
    /* Depth limit protects the stack. */
    char deep[200];
    memset(deep, '[', 100);
    memset(deep + 100, ']', 100);
    CHECK(akeno_json_parse(deep, sizeof(deep), NULL, 0) == NULL);
    char ok[60];
    memset(ok, '[', 30);
    memset(ok + 30, ']', 30);
    akeno_json_doc *doc = akeno_json_parse(ok, sizeof(ok), NULL, 0);
    CHECK(doc != NULL);
    akeno_json_free(doc);
    /* Fractions and exponents are not integers. */
    doc = parse("[1.5, 1e3]");
    int64_t n;
    CHECK(akeno_json_int64(akeno_json_at(akeno_json_root(doc), 0), &n) != 0);
    CHECK(akeno_json_int64(akeno_json_at(akeno_json_root(doc), 1), &n) != 0);
    akeno_json_free(doc);
}

static void test_buf_limits_and_escaping(void) {
    akeno_buf b;
    akeno_buf_init(&b, 8);
    CHECK(akeno_buf_append_str(&b, "12345678") == 0);
    CHECK(akeno_buf_append_str(&b, "9") == -1);
    CHECK(b.len == 8);
    akeno_buf_free(&b);

    akeno_buf_init(&b, 0);
    CHECK(akeno_buf_append_json_string(&b, "quote\" back\\ nl\n ctl\x01") == 0);
    CHECK_STR(b.data, "\"quote\\\" back\\\\ nl\\n ctl\\u0001\"");
    akeno_json_doc *doc = akeno_json_parse(b.data, b.len, NULL, 0);
    CHECK_STR(akeno_json_string(akeno_json_root(doc), NULL), "quote\" back\\ nl\n ctl\x01");
    akeno_json_free(doc);
    akeno_buf_consume(&b, 3);
    CHECK(b.data[0] == 'o');
    akeno_buf_free(&b);
}

static void test_http_head(void) {
    akeno_http_head h;
    const char *partial = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n";
    CHECK(akeno_http_parse_head(partial, strlen(partial), &h) == 0);
    const char *full = "HTTP/1.1 200 OK\r\ncontent-length: 5\r\nContent-Type: application/json\r\n\r\nhello";
    CHECK(akeno_http_parse_head(full, strlen(full), &h) == 1);
    CHECK(h.status == 200 && h.content_length == 5 && h.head_len == strlen(full) - 5);
    CHECK_STR(h.content_type, "application/json");

    const char *upgrade = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";
    CHECK(akeno_http_parse_head(upgrade, strlen(upgrade), &h) == 1);
    CHECK(h.status == 101);
    CHECK_STR(h.ws_accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    const char *chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\n";
    CHECK(akeno_http_parse_head(chunked, strlen(chunked), &h) == 1 && h.chunked && h.content_length == -1);

    const char *bad[] = {
        "HTTP/2 200 OK\r\n\r\n", "HTTP/1.1 2x0 OK\r\n\r\n", "HTTP/1.1 200 OK\r\nNoColon\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(akeno_http_parse_head(bad[i], strlen(bad[i]), &h) == -1);
}

static void test_http_chunked(void) {
    akeno_buf out;
    akeno_buf_init(&out, 64);
    size_t consumed = 0;
    const char *body = "4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\nTrailer: x\r\n\r\n";
    CHECK(akeno_http_decode_chunked(body, strlen(body), &out, &consumed) == 1);
    CHECK_STR(out.data, "Wikipedia");
    CHECK(consumed == strlen(body));
    CHECK(akeno_http_decode_chunked("4\r\nWi", 5, &out, &consumed) == 0);
    CHECK(akeno_http_decode_chunked("zz\r\n", 4, &out, &consumed) == -1);
    CHECK(akeno_http_decode_chunked("4\r\nWikiXX", 9, &out, &consumed) == -1);
    akeno_buf_free(&out);
    akeno_buf small;
    akeno_buf_init(&small, 3);
    CHECK(akeno_http_decode_chunked("4\r\nWiki\r\n0\r\n\r\n", 14, &small, &consumed) == -1);
    akeno_buf_free(&small);
}

static void test_ws_frames(void) {
    /* RFC 6455 section 1.3 example. */
    char accept[29];
    akeno_ws_accept_for_key("dGhlIHNhbXBsZSBub25jZQ==", accept);
    CHECK_STR(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    akeno_buf out;
    akeno_buf_init(&out, 0);
    const uint8_t mask[4] = { 0x37, 0xfa, 0x21, 0x3d };
    CHECK(akeno_ws_encode(&out, 0x1, "Hello", 5, mask) == 0);
    const uint8_t expected[] = { 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 };
    CHECK(out.len == sizeof(expected) && memcmp(out.data, expected, sizeof(expected)) == 0);
    akeno_ws_frame f;
    CHECK(akeno_ws_parse_header((const uint8_t *)out.data, out.len, &f) == 1);
    CHECK(f.fin && f.opcode == 1 && f.masked && f.payload_len == 5 && f.header_len == 6);
    akeno_buf_reset(&out);
    char big[300];
    memset(big, 'x', sizeof(big));
    CHECK(akeno_ws_encode(&out, 0x1, big, sizeof(big), mask) == 0);
    CHECK(akeno_ws_parse_header((const uint8_t *)out.data, out.len, &f) == 1 && f.payload_len == 300 && f.header_len == 8);
    akeno_buf_free(&out);

    const uint8_t server_text[] = { 0x81, 0x02, 'h', 'i' };
    CHECK(akeno_ws_parse_header(server_text, sizeof(server_text), &f) == 1 && !f.masked && f.payload_len == 2);
    CHECK(akeno_ws_parse_header(server_text, 1, &f) == 0);
    const uint8_t rsv[] = { 0xC1, 0x00 };
    CHECK(akeno_ws_parse_header(rsv, sizeof(rsv), &f) == -1);
    const uint8_t fragmented_ping[] = { 0x09, 0x00 };
    CHECK(akeno_ws_parse_header(fragmented_ping, sizeof(fragmented_ping), &f) == -1);
    const uint8_t long_ping[] = { 0x89, 0x7E, 0x00, 0x80 };
    CHECK(akeno_ws_parse_header(long_ping, sizeof(long_ping), &f) == -1);
    const uint8_t non_minimal[] = { 0x81, 0x7E, 0x00, 0x05 };
    CHECK(akeno_ws_parse_header(non_minimal, sizeof(non_minimal), &f) == -1);
    const uint8_t reserved_opcode[] = { 0x83, 0x00 };
    CHECK(akeno_ws_parse_header(reserved_opcode, sizeof(reserved_opcode), &f) == -1);
}

static void test_sha1_base64(void) {
    uint8_t digest[20];
    char hex[41];
    akeno_sha1("abc", 3, digest);
    for (int i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    CHECK_STR(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");
    const char *long_input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    akeno_sha1(long_input, strlen(long_input), digest);
    for (int i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    CHECK_STR(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    char b64[16];
    CHECK(akeno_base64_encode((const uint8_t *)"foob", 4, b64, sizeof(b64)) == 0);
    CHECK_STR(b64, "Zm9vYg==");
    CHECK(akeno_base64_encode((const uint8_t *)"foob", 4, b64, 8) == -1);
}

static void test_urls(void) {
    akeno_url u;
    CHECK(akeno_url_parse("https://85.190.112.10.sslip.io", &u) == 0);
    CHECK(u.tls && u.port == 443 && strcmp(u.host, "85.190.112.10.sslip.io") == 0 && u.base_path[0] == '\0');
    CHECK(akeno_url_parse("http://127.0.0.1:3000/", &u) == 0 && !u.tls && u.port == 3000 && akeno_url_is_loopback(&u));
    CHECK(akeno_url_parse("https://Example.ORG:8443/akeno/", &u) == 0);
    CHECK(strcmp(u.host, "example.org") == 0 && u.port == 8443 && strcmp(u.base_path, "/akeno") == 0);
    CHECK(akeno_url_parse("https://[::1]:4443", &u) == 0 && strcmp(u.host, "::1") == 0 && akeno_url_is_loopback(&u));
    CHECK(akeno_url_parse("http://192.168.1.10", &u) == 0 && !akeno_url_is_loopback(&u));
    const char *bad[] = { "ftp://x", "https://", "https://user@host", "https://host:0", "https://host:99999",
                          "https://host?x=1", "https://host#frag", "https://ho st", "https://host:12ab" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(akeno_url_parse(bad[i], &u) != 0);
}

static void test_backoff(void) {
    CHECK(akeno_backoff_delay_ms(0, 1000, 60000, 0.0) == 500);
    CHECK(akeno_backoff_delay_ms(0, 1000, 60000, 0.999) <= 1000);
    CHECK(akeno_backoff_delay_ms(3, 1000, 60000, 0.0) == 4000);
    CHECK(akeno_backoff_delay_ms(30, 1000, 60000, 0.999) <= 60000);
    CHECK(akeno_backoff_delay_ms(1000000, 1000, 60000, 0.5) >= 30000);
    CHECK(akeno_backoff_delay_ms(-5, 1000, 60000, 2.0) <= 1000);
    for (int attempt = 0; attempt < 64; attempt++) {
        int d = akeno_backoff_delay_ms(attempt, 1000, 60000, 0.7);
        CHECK(d > 0 && d <= 60000);
    }
}

static void test_capabilities(void) {
    int major, minor;
    CHECK(akeno_firmware_parse("12.20", &major, &minor) == 0 && major == 12 && minor == 20);
    CHECK(akeno_firmware_parse("9.60", &major, &minor) == 0 && major == 9 && minor == 60);
    const char *bad[] = { NULL, "", "12", "12.2", "12.200", "x2.20", "12,20", "-1.20", "00.00", "123.45" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(akeno_firmware_parse(bad[i], &major, &minor) != 0);

    akeno_capability_report r;
    akeno_platform_facts ps5 = { AKENO_PLATFORM_PS5, "PlayStation 5", "12.20", 1, 1, 1 };
    akeno_caps_evaluate(&ps5, &r);
    CHECK(r.firmware.known && !r.firmware.hardware_tested);
    CHECK(r.features[AKENO_FEATURE_REST_API].state == AKENO_CAP_ENABLED);
    CHECK(r.features[AKENO_FEATURE_DEVICE_PAIRING].state == AKENO_CAP_ENABLED);
    CHECK(r.features[AKENO_FEATURE_SYSTEM_NOTIFICATIONS].state == AKENO_CAP_DISABLED);
    for (int f = AKENO_FEATURE_NATIVE_FRIENDS_LIST; f <= AKENO_FEATURE_NATIVE_GAME_INVITES; f++)
        CHECK(r.features[f].state == AKENO_CAP_UNSUPPORTED);

    /* Unknown, unlisted and malformed firmware never enable firmware-specific features. */
    const char *firmwares[] = { "13.60", "99.99", "garbage", NULL };
    for (size_t i = 0; i < 4; i++) {
        akeno_platform_facts facts = { AKENO_PLATFORM_PS5, "PlayStation 5", firmwares[i], 1, 1, 1 };
        akeno_caps_evaluate(&facts, &r);
        CHECK(r.features[AKENO_FEATURE_SYSTEM_NOTIFICATIONS].state != AKENO_CAP_ENABLED);
        CHECK(r.features[AKENO_FEATURE_NATIVE_PARTY].state == AKENO_CAP_UNSUPPORTED);
        CHECK(r.firmware.hardware_tested == 0);
        CHECK(r.firmware.known == (i == 0));
    }

    akeno_platform_facts no_tls = { AKENO_PLATFORM_PS5, "PlayStation 5", "13.60", 1, 0, 1 };
    akeno_caps_evaluate(&no_tls, &r);
    CHECK(r.features[AKENO_FEATURE_REST_API].state == AKENO_CAP_DISABLED);
    CHECK(r.features[AKENO_FEATURE_REALTIME].state == AKENO_CAP_DISABLED);
    CHECK(r.features[AKENO_FEATURE_DEVICE_PAIRING].state == AKENO_CAP_DISABLED);

    akeno_platform_facts no_storage = { AKENO_PLATFORM_HOST, "Linux", NULL, 1, 1, 0 };
    akeno_caps_evaluate(&no_storage, &r);
    CHECK(r.features[AKENO_FEATURE_CREDENTIAL_STORAGE].state == AKENO_CAP_DISABLED);
    CHECK(r.features[AKENO_FEATURE_DEVICE_PAIRING].state == AKENO_CAP_DISABLED);
    CHECK(r.features[AKENO_FEATURE_REST_API].state == AKENO_CAP_ENABLED);
    for (int f = 0; f < AKENO_FEATURE_COUNT; f++) CHECK(r.features[f].reason && r.features[f].reason[0]);
}

static void test_redaction(void) {
    char out[128];
    akeno_redact("token akd_AbC-123_xyz and code akp_QQQ end", out, sizeof(out));
    CHECK_STR(out, "token akd_[redacted] and code akp_[redacted] end");
    akeno_redact("akd_ alone", out, sizeof(out));
    CHECK_STR(out, "akd_ alone");
    char tiny[8];
    akeno_redact("akd_aaaaaaaaaaaaaaaa", tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 7 && strstr(tiny, "aaaa") == NULL);
    /* Many short matches must not overflow. */
    char many[400] = "";
    for (int i = 0; i < 60; i++) strcat(many, "akd_x ");
    char red[64];
    akeno_redact(many, red, sizeof(red));
    CHECK(strlen(red) < sizeof(red));
}

void run_core_tests(void) {
    test_json_valid();
    test_json_invalid();
    test_buf_limits_and_escaping();
    test_http_head();
    test_http_chunked();
    test_ws_frames();
    test_sha1_base64();
    test_urls();
    test_backoff();
    test_capabilities();
    test_redaction();
}
