/*
 * AKENO System Explorer unit tests and mock scenarios. Everything runs against the simulated
 * console (ex_backend_host) with a virtual clock, so the timeline tests are deterministic and
 * instant. A passing run here is never evidence of PS5 behaviour.
 *
 * Covered: event ordering, timestamp monotonicity, no invented values, unsupported-status
 * reporting, missing libraries and symbols, bounded buffers, log redaction, reliable cleanup,
 * valid JSON/JSONL, and the mock scenarios (home, game, transitions, query refusals, delayed
 * scheduling, graceful shutdown).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>

#include "ex.h"
#include "ex_backend.h"
#include "ex_backend_host.h"
#include "ex_redact.h"

static int g_checks, g_failures;
#define CHECK(cond)                                                                           \
    do {                                                                                      \
        g_checks++;                                                                           \
        if (!(cond)) {                                                                        \
            g_failures++;                                                                     \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);          \
        }                                                                                     \
    } while (0)

/* ---- a small strict JSON validator (no external deps) ---------------------------------------- */

static const char *js_value(const char *p);
static const char *js_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}
static const char *js_string(const char *p) {
    if (*p != '"') return NULL;
    p++;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (*p == 'u') {
                for (int i = 0; i < 4; i++) {
                    p++;
                    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
                        return NULL;
                }
            } else if (!strchr("\"\\/bfnrt", *p)) {
                return NULL;
            }
        }
        p++;
    }
    return *p == '"' ? p + 1 : NULL;
}
static const char *js_number(const char *p) {
    const char *s = p;
    if (*p == '-') p++;
    if (*p == '0') p++;
    else if (*p >= '1' && *p <= '9') while (*p >= '0' && *p <= '9') p++;
    else return NULL;
    if (*p == '.') { p++; if (!(*p >= '0' && *p <= '9')) return NULL; while (*p >= '0' && *p <= '9') p++; }
    if (*p == 'e' || *p == 'E') { p++; if (*p == '+' || *p == '-') p++; if (!(*p >= '0' && *p <= '9')) return NULL; while (*p >= '0' && *p <= '9') p++; }
    return p > s ? p : NULL;
}
static const char *js_value(const char *p) {
    p = js_ws(p);
    if (*p == '"') return js_string(p);
    if (*p == '{') {
        p = js_ws(p + 1);
        if (*p == '}') return p + 1;
        for (;;) {
            p = js_ws(p);
            p = js_string(p);
            if (!p) return NULL;
            p = js_ws(p);
            if (*p != ':') return NULL;
            p = js_value(p + 1);
            if (!p) return NULL;
            p = js_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == '}') return p + 1;
            return NULL;
        }
    }
    if (*p == '[') {
        p = js_ws(p + 1);
        if (*p == ']') return p + 1;
        for (;;) {
            p = js_value(p);
            if (!p) return NULL;
            p = js_ws(p);
            if (*p == ',') { p++; continue; }
            if (*p == ']') return p + 1;
            return NULL;
        }
    }
    if (!strncmp(p, "true", 4)) return p + 4;
    if (!strncmp(p, "false", 5)) return p + 5;
    if (!strncmp(p, "null", 4)) return p + 4;
    return js_number(p);
}
static int json_valid(const char *s) {
    const char *p = js_value(s);
    if (!p) return 0;
    p = js_ws(p);
    return *p == '\0';
}
static int jsonl_valid(const char *s) {
    char line[8192];
    const char *p = s;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len == 0) { p += 1; continue; }
        if (len >= sizeof(line)) return 0;
        memcpy(line, p, len);
        line[len] = '\0';
        if (!json_valid(line)) return 0;
        if (!nl) break;
        p = nl + 1;
    }
    return 1;
}

/* ---- helpers --------------------------------------------------------------------------------- */

static ex_sim base_sim(void) {
    ex_sim s;
    memset(&s, 0, sizeof(s));
    s.have_fw = 1;
    s.fw = 0x12200000u;
    s.have_proc = s.have_appinfo = s.have_bigapp = s.have_user = 1;
    s.have_rlimit = s.have_rusage = 1;
    s.game_app = 0x60000123;
    s.game_after_samples = -1;
    s.user_id = 0x10000001u;
    return s;
}

static ex_options quick_opts(unsigned session_s, unsigned poll_ms) {
    ex_options o;
    memset(&o, 0, sizeof(o));
    o.quick = 1;
    o.session_s = session_s;
    o.poll_ms = poll_ms;
    return o;
}

static const ex_probe *find_probe(const ex_run *run, const char *name) {
    for (size_t i = 0; i < run->probe_count && i < EX_PROBES_MAX; i++)
        if (strcmp(run->probe[i].name, name) == 0) return &run->probe[i];
    return NULL;
}

static int count_events(const ex_run *run, ex_event_kind kind) {
    int n = 0;
    for (size_t i = 0; i < run->event_count && i < EX_EVENTS_MAX; i++)
        if (run->event[i].kind == kind) n++;
    return n;
}

/* ---- tests ----------------------------------------------------------------------------------- */

static void test_redact(void) {
    char out[256];
    ex_redact(out, sizeof(out), "token akd_abcdef123456 here");
    CHECK(strstr(out, "akd_[redacted]") != NULL);
    CHECK(strstr(out, "abcdef123456") == NULL);
    ex_redact(out, sizeof(out), "Bearer verysecretvalue extra");
    CHECK(strstr(out, "Bearer [redacted]") != NULL);
    ex_redact(out, sizeof(out), "key 0123456789abcdef0123456789abcdef done");
    CHECK(strstr(out, "[redacted-hex]") != NULL);
    ex_redact(out, sizeof(out), "see https://host.example:8443/path now");
    CHECK(strstr(out, "[server]") != NULL);
    CHECK(strstr(out, "host.example") == NULL);
    ex_redact(out, sizeof(out), "loopback http://127.0.0.1:8642/x");
    CHECK(strstr(out, "127.0.0.1") != NULL); /* loopback kept */
}

static void test_classify_context(void) {
    ex_sysinfo s;
    memset(&s, 0, sizeof(s));
    snprintf(s.platform, sizeof(s.platform), "PlayStation 5");

    s.have_appinfo = 1; s.appinfo_rc = 0; s.have_bigapp = 1;
    s.own_app_id = 0x60000123; s.bigapp_app_id = 0x60000123;
    CHECK(ex_classify_context(&s) == EX_CTX_FOREGROUND);

    s.own_app_id = 0; s.bigapp_app_id = 0x60000123;
    CHECK(ex_classify_context(&s) == EX_CTX_BACKGROUND);

    s.bigapp_app_id = 0; s.own_app_id = 0;
    CHECK(ex_classify_context(&s) == EX_CTX_BACKGROUND);

    s.appinfo_rc = 0x80020016; s.bigapp_app_id = 0x60000123;
    CHECK(ex_classify_context(&s) == EX_CTX_UNKNOWN);

    snprintf(s.platform, sizeof(s.platform), "PC simulation (not a PS5)");
    CHECK(ex_classify_context(&s) == EX_CTX_DESKTOP);
}

static void test_fill_sysinfo_appinfo(void) {
    ex_raw_proc raw;
    memset(&raw, 0, sizeof(raw));
    raw.have_fw = 1; raw.firmware_raw = 0x12200000u;
    raw.have_appinfo_fn = 1; raw.appinfo_rc = 0;
    uint32_t app = 0x60000123;
    memcpy(raw.appinfo_raw, &app, 4);
    memcpy(raw.appinfo_raw + 20, "PPSA00001", 10); /* samples/ps offset */
    ex_sysinfo s;
    ex_fill_sysinfo(&raw, "PlayStation 5", &s);
    CHECK(strcmp(s.firmware, "12.20") == 0);
    CHECK(s.own_app_id == 0x60000123);
    CHECK(strcmp(s.own_title_id, "PPSA00001") == 0);

    memset(&raw.appinfo_raw, 0, sizeof(raw.appinfo_raw));
    memcpy(raw.appinfo_raw, &app, 4);
    memcpy(raw.appinfo_raw + 16, "CUSA12345", 10); /* samples/test_privileges offset */
    ex_fill_sysinfo(&raw, "PlayStation 5", &s);
    CHECK(strcmp(s.own_title_id, "CUSA12345") == 0);
}

static void test_process_refusals(void) {
    /* Unsupported firmware data + query refusals must be reported as such, not invented. */
    ex_sim sim = base_sim();
    sim.have_fw = 0;
    sim.have_appinfo = 0;       /* sceKernelGetAppInfo not exported */
    sim.have_bigapp = 0;        /* not exported */
    sim.have_user = 1; sim.user_rc = (int32_t)0x80960009;
    sim.have_proc = 0;
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(1, 500);
    static ex_run run;
    ex_execute(&be, &o, &run);

    const ex_probe *p = find_probe(&run, "firmware");
    CHECK(p && p->status == EX_ST_NOT_AVAILABLE);
    p = find_probe(&run, "executable_identity");
    CHECK(p && p->status == EX_ST_UNSUPPORTED_FW);
    p = find_probe(&run, "foreground_app");
    CHECK(p && p->status == EX_ST_UNSUPPORTED_FW);
    p = find_probe(&run, "foreground_user");
    CHECK(p && p->status == EX_ST_ERROR && p->has_rc && (unsigned)p->rc == 0x80960009u);
    p = find_probe(&run, "process_record");
    CHECK(p && p->status == EX_ST_NOT_AVAILABLE);
}

static void test_libraries(void) {
    ex_sim sim = base_sim();
    /* libSceAudioOut (idx 0): dlopen fails but file present -> ERROR; its symbols NOT_AVAILABLE. */
    sim.load_fail = (1u << 0);
    /* libSceAudioIn (idx 1): last symbol missing. */
    sim.missing_last_sym = (1u << 1);
    /* libSceUserService (idx 2): file absent. */
    sim.absent_files = (1u << 2);
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(1, 500);
    static ex_run run;
    ex_execute(&be, &o, &run);

    const ex_probe *p = find_probe(&run, "libSceAudioOut.sprx");
    CHECK(p && p->status == EX_ST_ERROR);               /* present but failed to load */
    p = find_probe(&run, "sceAudioOutInit");
    CHECK(p && p->status == EX_ST_NOT_AVAILABLE);        /* not looked up: load failed */

    p = find_probe(&run, "sceAudioInGetSilentState");   /* last symbol of libSceAudioIn */
    CHECK(p && p->status == EX_ST_NOT_AVAILABLE);
    p = find_probe(&run, "sceAudioInOpen");
    CHECK(p && p->status == EX_ST_OK);

    p = find_probe(&run, "libSceUserService.sprx");
    /* absent file but load simulated-ok -> load_ok wins (OK); its absence is in the value text. */
    CHECK(p && (p->status == EX_ST_OK || p->status == EX_ST_NOT_AVAILABLE));

    /* Quarantined libraries are SKIPPED and never loaded. */
    p = find_probe(&run, "libSceVideoRecording.sprx");
    CHECK(p && p->status == EX_ST_SKIPPED);
    CHECK(strstr(p->value, "quarantined") != NULL);
    p = find_probe(&run, "sceVideoRecordingStart");
    CHECK(p && p->status == EX_ST_SKIPPED);
}

static void test_audio_background(void) {
    static ex_run run;
    memset(&run, 0, sizeof(run));
    run.lib_count = 0;              /* no library info: module must still be safe */
    run.context = EX_CTX_BACKGROUND;
    ex_sim sim = base_sim();
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_module_audio(&be, &run);

    const ex_probe *p = find_probe(&run, "active_audio_probe");
    CHECK(p && p->status == EX_ST_SKIPPED);
    p = find_probe(&run, "output_background");
    CHECK(p && p->status == EX_ST_OK && p->evidence == EX_EV_HARDWARE_VERIFIED);
    CHECK(p && strstr(p->value, "not heard") != NULL);
    p = find_probe(&run, "microphone_background");
    CHECK(p && p->status == EX_ST_OK && strstr(p->value, "0x809B0081") != NULL);
    p = find_probe(&run, "audio_focus_interface");
    CHECK(p && p->status == EX_ST_SKIPPED);
}

static void test_shellui(void) {
    static ex_run run;
    memset(&run, 0, sizeof(run));
    ex_module_shellui(&run);
    const ex_probe *p = find_probe(&run, "game_base_backend");
    CHECK(p && p->evidence == EX_EV_UNKNOWN && p->status == EX_ST_NOT_AVAILABLE);
    p = find_probe(&run, "onionHEN_plugin_pages");
    CHECK(p && p->evidence == EX_EV_HARDWARE_VERIFIED);
    p = find_probe(&run, "liveshell_animated_background");
    CHECK(p && strstr(p->limitation, "out of bounds") != NULL);
}

static void test_timeline_ordering_markers(void) {
    ex_sim sim = base_sim();
    sim.control = "mark home screen\nmark akd_secret123456 note\nextend 1\nstop\n";
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(3, 200);
    static ex_run run;
    ex_execute(&be, &o, &run);

    CHECK(run.event_count >= 3);
    CHECK(run.event[0].kind == EX_EVT_START);
    CHECK(run.event[run.event_count - 1].kind == EX_EVT_STOP);
    CHECK(run.markers == 2);
    CHECK(run.stopped_early == 1);
    /* a marker whose text carried a secret must be redacted */
    int saw_marker = 0, leaked = 0;
    long long prev = -1;
    for (size_t i = 0; i < run.event_count; i++) {
        if (run.event[i].kind == EX_EVT_MARKER) {
            saw_marker = 1;
            if (strstr(run.event[i].text, "secret123456")) leaked = 1;
        }
        CHECK((long long)run.event[i].t_us >= prev); /* timestamps non-decreasing */
        prev = (long long)run.event[i].t_us;
    }
    CHECK(saw_marker && !leaked);
    CHECK(run.requested_session_s == 3 + 60); /* extend 1 minute */
}

static void test_timeline_home_to_game(void) {
    ex_sim sim = base_sim();
    sim.game_after_samples = 2;
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(2, 200);
    static ex_run run;
    ex_execute(&be, &o, &run);
    CHECK(count_events(&run, EX_EVT_CONTEXT_CHANGE) >= 1);
    CHECK(run.context_changes >= 1);
    int saw_game = 0;
    for (size_t i = 0; i < run.event_count; i++)
        if (run.event[i].bigapp_app_id == 0x60000123) saw_game = 1;
    CHECK(saw_game);
}

static void test_timeline_sched_gap(void) {
    ex_sim sim = base_sim();
    sim.gap_at_poll = 2;
    sim.gap_ms = 5000;
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(10, 100); /* long enough to keep polling after the injected 5 s jump */
    static ex_run run;
    ex_execute(&be, &o, &run);
    CHECK(run.sched_gaps >= 1);
    CHECK(run.gap_max_ms >= 5000);
    int found = 0;
    for (size_t i = 0; i < run.event_count; i++)
        if (run.event[i].kind == EX_EVT_SCHED_GAP && strstr(run.event[i].text, "not interpreted")) found = 1;
    CHECK(found);
}

static void test_no_invented_values(void) {
    /*
     * Every probe states both a value and its limitation (no bare value without saying what it does
     * NOT establish), and no probe carries an error code unless it actually ran a call that failed.
     * Firmware/query absence is reported as a status, verified concretely in test_process_refusals.
     */
    ex_sim sim = base_sim();
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(1, 500);
    static ex_run run;
    ex_execute(&be, &o, &run);
    for (size_t i = 0; i < run.probe_count && i < EX_PROBES_MAX; i++) {
        const ex_probe *p = &run.probe[i];
        CHECK(p->value[0] != '\0');
        CHECK(p->limitation[0] != '\0');
        if (p->status != EX_ST_ERROR) CHECK(!p->has_rc); /* a code only when a call errored */
    }
}

static void test_bounds_and_cleanup(void) {
    ex_sim sim = base_sim();
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(2, 100);
    static ex_run run;
    ex_execute(&be, &o, &run);
    CHECK(run.probe_count < EX_PROBES_MAX);     /* the catalogue fits without truncation */
    CHECK(run.event_count <= EX_EVENTS_MAX);
    CHECK(run.events_truncated == 0);
    size_t first_probes = run.probe_count;
    /* reliable cleanup: a second execute on the same struct starts fresh */
    ex_execute(&be, &o, &run);
    CHECK(run.probe_count == first_probes);
    CHECK(run.markers == 0);
}

static void test_report_buffer_safety(void) {
    /* Log output errors: a tiny buffer must never overflow and must stay NUL-terminated. */
    ex_sim sim = base_sim();
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(1, 500);
    static ex_run run;
    ex_execute(&be, &o, &run);
    char tiny[16];
    size_t n = ex_report_json(&run, tiny, sizeof(tiny));
    CHECK(n < sizeof(tiny));
    CHECK(tiny[n] == '\0');
    n = ex_report_summary_txt(&run, tiny, sizeof(tiny));
    CHECK(n < sizeof(tiny) && tiny[n] == '\0');
}

static void test_json_valid(void) {
    ex_sim sim = base_sim();
    sim.game_after_samples = 2;
    sim.control = "mark a game started\nstop\n";
    ex_backend be;
    ex_backend_host(&be, &sim);
    ex_options o = quick_opts(2, 200);
    static ex_run run;
    ex_execute(&be, &o, &run);

    static char buf[256 * 1024];
    ex_report_json(&run, buf, sizeof(buf));
    CHECK(json_valid(buf));
    ex_report_modules_json(&run, buf, sizeof(buf));
    CHECK(json_valid(buf));
    ex_report_audio_json(&run, buf, sizeof(buf));
    CHECK(json_valid(buf));
    ex_report_events_jsonl(&run, buf, sizeof(buf));
    CHECK(jsonl_valid(buf));
}

int main(void) {
    test_redact();
    test_classify_context();
    test_fill_sysinfo_appinfo();
    test_process_refusals();
    test_libraries();
    test_audio_background();
    test_shellui();
    test_timeline_ordering_markers();
    test_timeline_home_to_game();
    test_timeline_sched_gap();
    test_no_invented_values();
    test_bounds_and_cleanup();
    test_report_buffer_safety();
    test_json_valid();

    printf("explorer tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
