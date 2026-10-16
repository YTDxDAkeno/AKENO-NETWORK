/* PS5 capture capability diagnostic report builder (app/capture_diag.c). */
#include <string.h>

#include "akeno/json.h"
#include "buf.h"
#include "capture_diag.h"
#include "test.h"

/* The catalogue is honest: every entry has a cited source, and no entry claims live game frames. */
static void test_catalogue(void) {
    size_t count = 0;
    const akeno_capture_candidate *cat = akeno_capture_catalogue(&count);
    CHECK(count >= 4 && count <= AKENO_CAPTURE_MAX_LIBRARIES);
    int saw_sdk = 0, saw_ext = 0;
    for (size_t i = 0; i < count; i++) {
        CHECK(cat[i].library && cat[i].library[0]);
        CHECK(cat[i].source && cat[i].source[0]);  /* never an uncited name */
        CHECK(cat[i].note && cat[i].note[0]);
        CHECK(cat[i].symbol_count <= AKENO_CAPTURE_MAX_SYMBOLS);
        if (cat[i].in_pinned_sdk) saw_sdk = 1; else saw_ext = 1;
    }
    CHECK(saw_sdk && saw_ext);
}

/* Level rises with evidence, and never reaches hardware-tested from a probe. */
static void test_levels(void) {
    size_t count = 0;
    const akeno_capture_candidate *cat = akeno_capture_catalogue(&count);
    akeno_capture_probe p;
    memset(&p, 0, sizeof(p));
    CHECK(akeno_capture_level(&cat[0], NULL) == AKENO_CAPLV_SOURCE_VERIFIED);
    CHECK(akeno_capture_level(&cat[0], &p) == AKENO_CAPLV_SOURCE_VERIFIED);
    p.file_present = 1;
    CHECK(akeno_capture_level(&cat[0], &p) == AKENO_CAPLV_RUNTIME_DETECTED);
    p.resolved = 1;
    p.load_ok = 1;
    p.symbol_found[0] = 1;
    CHECK(akeno_capture_level(&cat[0], &p) == AKENO_CAPLV_SYMBOL_PRESENT);
    /* Loaded but no symbol resolved falls back to the file-present level. */
    p.symbol_found[0] = 0;
    CHECK(akeno_capture_level(&cat[0], &p) == AKENO_CAPLV_RUNTIME_DETECTED);
}

static akeno_json_doc *build(akeno_buf *b, int with_probes) {
    akeno_capture_sysinfo sys;
    memset(&sys, 0, sizeof(sys));
    sys.platform = "PlayStation 5";
    sys.firmware = "12.20";
    sys.firmware_raw = 0x12200000;
    sys.app_version = "0.2.0-alpha";
    sys.build_id = "abcdef-ps5-capturediag";
    sys.sdk_commit = "f5d40f0";
    sys.library_dir = "/system/common/lib";
    sys.library_dir_readable = 1;
    sys.process_info_available = 1;
    sys.resolve_ran = with_probes;

    akeno_capture_network net;
    memset(&net, 0, sizeof(net));
    net.have = 1;
    net.server_configured = 1;
    net.server_reachable = 0;
    strcpy(net.server_detail, "not probed");
    net.dns_readable = 1;
    strcpy(net.dns_summary, "2 nameserver entries configured (addresses redacted)");

    size_t count = 0;
    akeno_capture_catalogue(&count);
    akeno_capture_probe probes[AKENO_CAPTURE_MAX_LIBRARIES];
    memset(probes, 0, sizeof(probes));
    if (with_probes) {
        for (size_t i = 0; i < count; i++) {
            probes[i].file_present = (i == 0);
            probes[i].resolved = 1;
            probes[i].load_ok = (i == 0);
            if (i == 0) probes[i].symbol_found[0] = 1;
            if (i == 1) strcpy(probes[i].error, "dlopen failed: not permitted");
        }
    }
    CHECK(akeno_capture_report_json(b, &sys, &net, with_probes ? probes : NULL, count, "2026-10-16T13:00:00Z") == 0);
    return akeno_json_parse(b->data, b->len, NULL, 0);
}

/* The report is valid JSON with an honest, fixed capability verdict and never claims capture works. */
static void test_report_static(void) {
    akeno_buf b;
    akeno_buf_init(&b, 16384);
    akeno_json_doc *doc = build(&b, 0);
    CHECK(doc != NULL);
    if (doc) {
        const akeno_json *root = akeno_json_root(doc);
        CHECK(akeno_json_get_bool(root, "experimental", 0) == 1);
        CHECK_STR(akeno_json_get_string(root, "kind", ""), "ps5-capture-diagnostic");
        const akeno_json *caps = akeno_json_get(root, "capabilities");
        CHECK(caps != NULL);
        /* Every capability verdict is "not supported" without hardware evidence. */
        CHECK(akeno_json_get_bool(akeno_json_get(caps, "liveFramebufferCapture"), "supported", 1) == 0);
        CHECK(akeno_json_get_bool(akeno_json_get(caps, "nativeBroadcastReuse"), "supported", 1) == 0);
        CHECK(akeno_json_get_bool(akeno_json_get(caps, "hardwareEncoder"), "supported", 1) == 0);
        const akeno_json *libs = akeno_json_get(root, "libraries");
        CHECK(libs && libs->type == AKENO_JSON_ARRAY && libs->count >= 4);
        /* Static report: nothing resolved, so found is null and no library claims live frames. */
        for (const akeno_json *lib = libs->child; lib; lib = lib->next) {
            CHECK(akeno_json_get_bool(lib, "givesLiveGameFrames", 1) == 0);
        }
        akeno_json_free(doc);
    }
    akeno_buf_free(&b);
}

/* With probes, a resolved SDK symbol reads back as symbol-present; errors are reported. */
static void test_report_resolved(void) {
    akeno_buf b;
    akeno_buf_init(&b, 16384);
    akeno_json_doc *doc = build(&b, 1);
    CHECK(doc != NULL);
    if (doc) {
        const akeno_json *root = akeno_json_root(doc);
        CHECK(akeno_json_get_bool(akeno_json_get(root, "system"), "resolveRan", 0) == 1);
        const akeno_json *libs = akeno_json_get(root, "libraries");
        const akeno_json *first = libs->child;
        CHECK_STR(akeno_json_get_string(first, "level", ""), "symbol-present");
        CHECK(akeno_json_get_bool(first, "filePresent", 0) == 1);
        const akeno_json *second = first->next;
        CHECK_STR(akeno_json_get_string(second, "level", ""), "source-verified");
        CHECK(akeno_json_get_string(second, "error", "")[0] != '\0');
        akeno_json_free(doc);
    }
    akeno_buf_free(&b);
}

/* No candidate symbol name is a system/credential string, and the report carries the safety notes. */
static void test_redaction(void) {
    akeno_buf b;
    akeno_buf_init(&b, 16384);
    akeno_json_doc *doc = build(&b, 1);
    CHECK(doc != NULL);
    /* The report must not contain the actual jailbreak DNS address or any "token"/"password". */
    CHECK(strstr(b.data, "62.210.38.117") == NULL);
    CHECK(strstr(b.data, "password") == NULL);
    CHECK(strstr(b.data, "PSN") == NULL || strstr(b.data, "never") != NULL);
    CHECK(strstr(b.data, "no capture, encoder or broadcast function is called") != NULL);
    if (doc) akeno_json_free(doc);
    akeno_buf_free(&b);
}

void run_capture_diag_tests(void);
void run_capture_diag_tests(void) {
    test_catalogue();
    test_levels();
    test_report_static();
    test_report_resolved();
    test_redaction();
}
