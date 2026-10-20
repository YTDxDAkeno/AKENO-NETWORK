/* Focused host tests for the automatic, single-run Audio Lab payload. */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "al.h"
#include "al_backend.h"
#include "al_backend_host.h"

static int g_checks, g_failures;
#define CHECK(c) do { \
    g_checks++; \
    if (!(c)) { \
        g_failures++; \
        fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
    } \
} while (0)

static al_sim base_sim(void) {
    al_sim s;
    memset(&s, 0, sizeof(s));
    s.have_fw = 1;
    s.fw = 0x12200000u;
    s.pid = 205;
    s.unix0 = 1700000000;
    return s;
}

static void test_one_shot_uses_unchanged_defaults(void) {
    al_sim sim = base_sim();
    al_backend be;
    al_state st;
    atomic_int cancel;
    al_backend_host(&be, &sim);
    al_engine_init(&st, &be);
    atomic_init(&cancel, 0);

    int id = al_engine_run_one_shot(&st, &be, &cancel);
    const al_run *run = al_run_by_id(&st, id);
    CHECK(id == 1);
    CHECK(sim.sleep_calls == 1 && sim.last_sleep_ms == AL_ONESHOT_DELAY_MS);
    CHECK(sim.vclock_us == (uint64_t)AL_ONESHOT_DELAY_MS * 1000u);
    CHECK(sim.audio_runs == 1 && st.tone_commands == 1 && st.tones_played == 1);
    CHECK(run && run->outcome == AL_OUT_ACCEPTED);
    CHECK(run && run->preset == al_find_preset(NULL));
    CHECK(run && run->level == al_find_level(NULL));
    CHECK(run && run->result.close_called && run->result.close_rc == 0);
    CHECK(run && !run->result.port_state_opt_in && !run->result.port_state_exported);
    CHECK(st.port_state_opt_in == 0 && st.port_state_supported == 0);
}

static void test_cancel_before_tone_does_not_open_audio(void) {
    al_sim sim = base_sim();
    al_backend be;
    al_state st;
    atomic_int cancel;
    al_backend_host(&be, &sim);
    al_engine_init(&st, &be);
    atomic_init(&cancel, 1);

    CHECK(al_engine_run_one_shot(&st, &be, &cancel) == -2);
    CHECK(sim.sleep_calls == 1 && sim.last_sleep_ms == AL_ONESHOT_DELAY_MS);
    CHECK(sim.audio_runs == 0 && st.tone_commands == 0);
}

static void test_audio_errors_are_recorded_and_closed_when_opened(void) {
    for (int mode = 1; mode <= 4; mode++) {
        al_sim sim = base_sim();
        al_backend be;
        al_state st;
        atomic_int cancel;
        sim.audio_mode = mode;
        al_backend_host(&be, &sim);
        al_engine_init(&st, &be);
        atomic_init(&cancel, 0);
        int id = al_engine_run_one_shot(&st, &be, &cancel);
        const al_run *run = al_run_by_id(&st, id);
        CHECK(id == 1 && run != NULL);
        if (mode == 1) CHECK(run->outcome == AL_OUT_NOT_SUPPORTED && !run->result.open_called);
        if (mode == 2) CHECK(run->outcome == AL_OUT_INIT_FAILED && !run->result.open_called);
        if (mode == 3) CHECK(run->outcome == AL_OUT_OPEN_FAILED && run->result.open_called);
        if (mode == 4) CHECK(run->outcome == AL_OUT_SUBMIT_FAILED && run->result.close_called);
    }
}

static void test_reports_describe_a_completed_single_run(void) {
    al_sim sim = base_sim();
    al_backend be;
    al_state st;
    atomic_int cancel;
    static char report[128 * 1024];
    al_backend_host(&be, &sim);
    al_engine_init(&st, &be);
    atomic_init(&cancel, 0);
    CHECK(al_engine_run_one_shot(&st, &be, &cancel) == 1);
    al_report_json(&st, report, sizeof(report));
    CHECK(strstr(report, "\"tonesPlayed\":1") != NULL);
    CHECK(strstr(report, "\"portStateOptIn\":false") != NULL);
    CHECK(strstr(report, "\"closeCalled\":true") != NULL);
    al_report_status_json(&st, "COMPLETED", st.started_unix + 7, report, sizeof(report));
    CHECK(strstr(report, "\"state\":\"COMPLETED\"") != NULL);
}

int main(void) {
    test_one_shot_uses_unchanged_defaults();
    test_cancel_before_tone_does_not_open_audio();
    test_audio_errors_are_recorded_and_closed_when_opened();
    test_reports_describe_a_completed_single_run();
    printf("audiolab tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
