/*
 * AKENO Audio Lab 3.1 unit tests. Everything runs against the host backend. A passing run here is
 * never evidence of PS5 behaviour. Covered: command parsing, tone lifecycle and outcomes, the
 * port-state opt-in gate and overrun stop, pacing, cancel, per-launch limit, the audio-state
 * guard/decode primitives, PCM generation, JSON validity and report content.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "al.h"
#include "al_backend.h"
#include "al_backend_host.h"
#include "audio_lab_pcm.h"
#include "audio_lab_state.h"

static int g_checks, g_failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(c)) {                                                                                \
            g_failures++;                                                                          \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                  \
        }                                                                                          \
    } while (0)

/* ---- a small strict JSON validator ----------------------------------------------------------- */
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
                    if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return NULL;
                }
            } else if (!strchr("\"\\/bfnrt", *p)) return NULL;
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

/* ---- helpers --------------------------------------------------------------------------------- */
static al_sim base_sim(void) {
    al_sim s;
    memset(&s, 0, sizeof(s));
    s.have_fw = 1;
    s.fw = 0x12200000u;
    s.pid = 205;
    s.port_state_exported = 1;
    return s;
}

/* ---- tests ----------------------------------------------------------------------------------- */

static void test_parse(void) {
    al_cmd c;
    CHECK(al_parse_command("status", &c) == AL_CMD_STATUS);
    CHECK(al_parse_command("  STOP  ", &c) == AL_CMD_STOP);
    CHECK(al_parse_command("exit", &c) == AL_CMD_EXIT);
    CHECK(al_parse_command("quit", &c) == AL_CMD_EXIT);
    CHECK(al_parse_command("# a comment", &c) == AL_CMD_NONE);
    CHECK(al_parse_command("", &c) == AL_CMD_NONE);
    CHECK(al_parse_command("nonsense", &c) == AL_CMD_NONE);
    CHECK(al_parse_command("tone", &c) == AL_CMD_TONE && c.preset[0] == '\0' && c.level[0] == '\0');
    CHECK(al_parse_command("tone mono16-256 clear", &c) == AL_CMD_TONE);
    CHECK(strcmp(c.preset, "mono16-256") == 0 && strcmp(c.level, "clear") == 0);
    CHECK(al_parse_command("TONE Stereo16-1024", &c) == AL_CMD_TONE && strcmp(c.preset, "stereo16-1024") == 0);
}

static void test_presets(void) {
    CHECK(al_find_preset("") == al_presets(&(size_t){0}));
    CHECK(al_find_preset("nope") == NULL);
    CHECK(al_find_level("")->amplitude == 0.1); /* moderate default */
    CHECK(al_find_level("clear")->amplitude == 0.2);
    CHECK(al_find_level("bogus") == NULL);
}

static void test_tone_accepted_paced(void) {
    al_sim sim = base_sim();
    sim.audio_mode = 0;
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    volatile int cancel = 0;
    int id = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    CHECK(id == 1);
    const al_run *r = al_run_by_id(&st, id);
    CHECK(r && r->outcome == AL_OUT_ACCEPTED);
    CHECK(r->paced == 1);                 /* host pacing is exact */
    CHECK(r->heard == AL_HEARD_UNREPORTED); /* never inferred */
    CHECK(r->result.frames_submitted > 0 && r->result.nonzero_frames > 0);
}

static void test_tone_failures(void) {
    for (int mode = 1; mode <= 4; mode++) {
        al_sim sim = base_sim();
        sim.audio_mode = mode;
        al_backend be;
        al_backend_host(&be, &sim);
        al_state st;
        al_engine_init(&st, &be);
        volatile int cancel = 0;
        int id = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
        const al_run *r = al_run_by_id(&st, id);
        CHECK(r != NULL);
        if (mode == 1) CHECK(r->outcome == AL_OUT_NOT_SUPPORTED);
        if (mode == 2) CHECK(r->outcome == AL_OUT_INIT_FAILED);
        if (mode == 3) CHECK(r->outcome == AL_OUT_OPEN_FAILED && (unsigned)r->result.open_rc == 0x80260011u);
        if (mode == 4) CHECK(r->outcome == AL_OUT_SUBMIT_FAILED);
    }
}

static void test_cancel(void) {
    al_sim sim = base_sim();
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    volatile int cancel = 1; /* cancelled before the first block */
    int id = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    const al_run *r = al_run_by_id(&st, id);
    CHECK(r && r->result.cancelled);
    CHECK(r->outcome == AL_OUT_CANCELLED);
}

static void test_port_state_opt_in_gate(void) {
    /* Opt-out (default): the probe is never attempted even though the function is exported. */
    al_sim sim = base_sim();
    sim.opt_in = 0;
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    CHECK(st.port_state_opt_in == 0);
    volatile int cancel = 0;
    int id = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    const al_run *r = al_run_by_id(&st, id);
    CHECK(r && !r->result.port_state_opt_in);
    CHECK(!r->result.port_state[AUDIO_LAB_STATE_BEFORE_PLAYBACK].attempted);

    /* Opt-in: a PS4-layout sample is measured and decodes tentatively. */
    sim = base_sim();
    sim.opt_in = 1;
    sim.port_state_mode = 0;
    al_backend_host(&be, &sim);
    al_engine_init(&st, &be);
    CHECK(st.port_state_opt_in == 1);
    id = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    r = al_run_by_id(&st, id);
    CHECK(r && r->result.port_state_opt_in);
    CHECK(r->result.port_state[AUDIO_LAB_STATE_BEFORE_PLAYBACK].attempted);
    audio_lab_state_decoded d;
    audio_lab_state_decode(&r->result.port_state[AUDIO_LAB_STATE_BEFORE_PLAYBACK], &d);
    CHECK(d.output_known && d.output == 0x0001);
    CHECK(d.volume_known && d.volume == 0x7fff);
}

static void test_port_state_overrun_stop(void) {
    al_sim sim = base_sim();
    sim.opt_in = 1;
    sim.port_state_mode = 2; /* guard trips */
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    volatile int cancel = 0;
    int id = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    const al_run *r = al_run_by_id(&st, id);
    CHECK(r && r->result.port_state_stopped);
}

static void test_launch_limit(void) {
    al_sim sim = base_sim();
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    volatile int cancel = 0;
    int last = 0;
    for (int i = 0; i < AL_RUNS_PER_LAUNCH + 2; i++)
        last = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    CHECK(last == -3); /* the limit is enforced */
    CHECK(st.tone_commands == AL_RUNS_PER_LAUNCH);
}

static void test_feedback(void) {
    al_sim sim = base_sim();
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    volatile int cancel = 0;
    int id = al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    CHECK(al_engine_feedback(&st, id, AL_HEARD_YES, "tv") == 0);
    const al_run *r = al_run_by_id(&st, id);
    CHECK(r->heard == AL_HEARD_YES && strcmp(r->route, "tv") == 0);
    CHECK(al_engine_feedback(&st, 999, AL_HEARD_NO, "tv") == -1);
}

static void test_reports_json(void) {
    al_sim sim = base_sim();
    sim.opt_in = 1;
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    volatile int cancel = 0;
    al_engine_run_tone(&st, &be, al_find_preset("stereo16-1024"), al_find_level("moderate"), &cancel);
    al_engine_feedback(&st, 1, AL_HEARD_NO, "tv");
    static char buf[128 * 1024];
    al_report_json(&st, buf, sizeof(buf));
    CHECK(json_valid(buf));
    CHECK(strstr(buf, "\"accepted+paced-but-inaudible\"") != NULL);
    CHECK(strstr(buf, "\"portState\"") != NULL);
    CHECK(strstr(buf, "ps4LayoutUnverified") != NULL);
    al_report_status_json(&st, "COMPLETED", st.started_unix + 42, buf, sizeof(buf));
    CHECK(json_valid(buf));
    CHECK(strstr(buf, "\"elapsedS\":42") != NULL);
    al_report_txt(&st, buf, sizeof(buf));
    CHECK(strstr(buf, "AKENO AUDIO LAB 3.1") != NULL);
    CHECK(strstr(buf, "WIEDERHOLT") != NULL); /* the repeat-vs-new explanation is present */
    CHECK(strstr(buf, "/data/akeno-audiolab") != NULL);
}

static void test_report_buffer_safety(void) {
    al_sim sim = base_sim();
    al_backend be;
    al_backend_host(&be, &sim);
    al_state st;
    al_engine_init(&st, &be);
    char tiny[16];
    size_t n = al_report_json(&st, tiny, sizeof(tiny));
    CHECK(n < sizeof(tiny) && tiny[n] == '\0');
    n = al_report_txt(&st, tiny, sizeof(tiny));
    CHECK(n < sizeof(tiny) && tiny[n] == '\0');
}

static void test_pcm(void) {
    audio_lab_osc osc;
    audio_lab_osc_init(&osc, 440, 0.1, 48000u * 250u / 1000u, 48000u * 1500u / 1000u);
    static short block[2048 * 2];
    audio_lab_block_stats stats;
    audio_lab_fill(&osc, AUDIO_LAB_FORMAT_S16_STEREO, block, 1024, &stats);
    CHECK(stats.peak >= 0.0 && stats.peak <= 1.0);
    /* frames=0 must be safe (the host backend calls fill with 0). */
    audio_lab_fill(&osc, AUDIO_LAB_FORMAT_S16_STEREO, block, 0, &stats);
    CHECK(stats.nonzero == 0);
    CHECK(audio_lab_format_channels(AUDIO_LAB_FORMAT_S16_STEREO) == 2);
    CHECK(audio_lab_grain_valid(1024) && !audio_lab_grain_valid(1000));
}

static void test_state_guard_primitive(void) {
    /* The production arena is 64-byte aligned (al_backend_ps5.c); the window is then aligned too. */
    static unsigned char arena[AUDIO_LAB_STATE_ARENA_BYTES] __attribute__((aligned(64)));
    audio_lab_state_prepare(arena, AUDIO_LAB_STATE_FILL_A);
    unsigned char *w = audio_lab_state_window(arena);
    CHECK(((uintptr_t)w % 64) == 0);
    audio_lab_state_call call;
    audio_lab_state_capture(arena, 0, AUDIO_LAB_STATE_FILL_A, &call);
    CHECK(call.lead_intact && call.tail_intact); /* nothing written: canaries intact */
}

int main(void) {
    test_parse();
    test_presets();
    test_tone_accepted_paced();
    test_tone_failures();
    test_cancel();
    test_port_state_opt_in_gate();
    test_port_state_overrun_stop();
    test_launch_limit();
    test_feedback();
    test_reports_json();
    test_report_buffer_safety();
    test_pcm();
    test_state_guard_primitive();
    printf("audiolab tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
