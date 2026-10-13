/* Unit tests for the console app's local HTTP parsing and request checks, and for JSON output. */
#include <stdio.h>
#include <string.h>

#include "akeno/json.h"
#include "buf.h"
#include "json_write.h"
#include "test.h"
#include "ui_http.h"

static int parse(const char *text, ui_request *req) { return ui_http_parse_request(text, strlen(text), req); }

static void test_parse_valid(void) {
    ui_request req;
    const char *get = "GET /api/events?after=12 HTTP/1.1\r\nHost: 127.0.0.1:8642\r\nX-Akeno-Token: abc\r\n"
                      "Origin: http://127.0.0.1:8642\r\n\r\n";
    CHECK(parse(get, &req) == UI_PARSE_OK);
    CHECK_STR(req.method, "GET");
    CHECK_STR(req.path, "/api/events");
    CHECK_STR(req.query, "after=12");
    CHECK_STR(req.host, "127.0.0.1:8642");
    CHECK_STR(req.token, "abc");
    CHECK(req.content_length == -1 && req.head_len == strlen(get));
    char value[24];
    CHECK(ui_http_query_param(req.query, "after", value, sizeof(value)) == 0);
    CHECK_STR(value, "12");
    CHECK(ui_http_query_param("x=1&after=7", "after", value, sizeof(value)) == 0 && strcmp(value, "7") == 0);
    CHECK(ui_http_query_param("after=1;2", "after", value, sizeof(value)) == -1);
    CHECK(ui_http_query_param("afterx=1", "after", value, sizeof(value)) == -1);

    const char *post = "POST /api/settings HTTP/1.1\r\nhost: localhost:8642\r\nContent-Type: application/json\r\n"
                       "Content-Length: 2\r\n\r\n{}";
    CHECK(parse(post, &req) == UI_PARSE_OK);
    CHECK(req.content_length == 2);
    CHECK_STR(req.content_type, "application/json");
    CHECK(parse("GET / HTTP/1.1\r\nHost: 127.0.0.1:1\r\n", &req) == UI_PARSE_INCOMPLETE);
}

static void test_parse_hostile(void) {
    ui_request req;
    const char *bad[] = {
        "GET / HTTP/1.1\r\n\r\n",                                                  /* no Host */
        "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n",                            /* duplicate Host */
        "get / HTTP/1.1\r\nHost: a\r\n\r\n",                                       /* lowercase method */
        "GET relative HTTP/1.1\r\nHost: a\r\n\r\n",                                /* target not absolute path */
        "GET / HTTP/2.0\r\nHost: a\r\n\r\n",
        "GET /a b HTTP/1.1\r\nHost: a\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\r\nNoColon\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\r\n folded: x\r\n\r\n",                         /* obsolete line folding */
        "GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\r\nContent-Length: -1\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 1x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: a\x01\r\n\r\n",
        "VERYLONGMETHOD / HTTP/1.1\r\nHost: a\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        int rc = parse(bad[i], &req);
        if (rc != UI_PARSE_BAD) fprintf(stderr, "case %zu returned %d\n", i, rc);
        CHECK(rc == UI_PARSE_BAD);
    }
    CHECK(parse("POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n", &req) == UI_PARSE_UNSUPPORTED);
    CHECK(parse("POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999\r\n\r\n", &req) == UI_PARSE_TOO_LARGE);

    char big[UI_HTTP_MAX_HEAD + 64];
    memset(big, 'a', sizeof(big));
    memcpy(big, "GET / HTTP/1.1\r\nHost: x\r\nX-Pad: ", 32);
    CHECK(ui_http_parse_request(big, sizeof(big), &req) == UI_PARSE_TOO_LARGE);
    char long_token[300];
    snprintf(long_token, sizeof(long_token), "GET / HTTP/1.1\r\nHost: a\r\nX-Akeno-Token: %0200d\r\n\r\n", 7);
    CHECK(parse(long_token, &req) == UI_PARSE_TOO_LARGE);
}

static void test_request_checks(void) {
    CHECK(ui_http_host_allowed("127.0.0.1:8642", 8642));
    CHECK(ui_http_host_allowed("localhost:8642", 8642));
    CHECK(!ui_http_host_allowed("127.0.0.1:8643", 8642));
    CHECK(!ui_http_host_allowed("evil.example:8642", 8642));     /* DNS rebinding */
    CHECK(!ui_http_host_allowed("127.0.0.1", 8642));
    CHECK(!ui_http_host_allowed("192.168.1.20:8642", 8642));
    CHECK(ui_http_origin_allowed("", 8642));
    CHECK(ui_http_origin_allowed("http://127.0.0.1:8642", 8642));
    CHECK(!ui_http_origin_allowed("https://evil.example", 8642));
    CHECK(!ui_http_origin_allowed("null", 8642));
    CHECK(!ui_http_origin_allowed("http://127.0.0.1:8642.evil.example", 8642));
    CHECK(ui_http_secret_equal("abcd", "abcd"));
    CHECK(!ui_http_secret_equal("abcd", "abce"));
    CHECK(!ui_http_secret_equal("abcd", "abc"));
    CHECK(!ui_http_secret_equal("", ""));
}

static void test_json_roundtrip(void) {
    const char *inputs[] = {
        "{\"a\":[1,-2.5e3,true,false,null],\"b\":{\"c\":\"quote\\\" nl\\n \\u00e9\"},\"d\":[]}",
        "[]", "\"x\"", "0", "{\"e\":{}}",
    };
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        akeno_json_doc *doc = akeno_json_parse(inputs[i], strlen(inputs[i]), NULL, 0);
        CHECK(doc != NULL);
        akeno_buf out;
        akeno_buf_init(&out, 0);
        CHECK(akeno_json_write(akeno_json_root(doc), &out) == 0);
        akeno_json_doc *again = akeno_json_parse(out.data, out.len, NULL, 0);
        CHECK(again != NULL);
        akeno_buf second;
        akeno_buf_init(&second, 0);
        CHECK(akeno_json_write(akeno_json_root(again), &second) == 0);
        CHECK_STR(second.data, out.data); /* Stable after one round trip. */
        akeno_buf_free(&out);
        akeno_buf_free(&second);
        akeno_json_free(doc);
        akeno_json_free(again);
    }
    akeno_json_doc *doc = akeno_json_parse("{\"k\":\"<script>\"}", 16, NULL, 0);
    akeno_buf out;
    akeno_buf_init(&out, 8);
    CHECK(akeno_json_write(akeno_json_root(doc), &out) == -1); /* Limit respected. */
    akeno_buf_free(&out);
    akeno_json_free(doc);
    akeno_buf_init(&out, 0);
    CHECK(akeno_json_write(NULL, &out) == 0);
    CHECK_STR(out.data, "null");
    akeno_buf_free(&out);
}

void run_app_tests(void) {
    test_parse_valid();
    test_parse_hostile();
    test_request_checks();
    test_json_roundtrip();
}
