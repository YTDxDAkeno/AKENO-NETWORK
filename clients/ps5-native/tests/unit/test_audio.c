#include "audio_lab_pcm.h"
#include "audio_test.h"
#include "test.h"

/* A fake audio system: a clock that advances by `pace_us` per output call, optional failures. */
typedef struct {
    int32_t init_rc, open_rc, fail_at_call, fail_rc;
    uint64_t now, pace_us;
    int calls, closed, close_handle;
    unsigned grain;
    int format;
    int cancel_after;
    volatile int *cancel;
} fake;

static int32_t f_init(void *u) { return ((fake *)u)->init_rc; }
static int32_t f_open(void *u, unsigned grain, unsigned rate, int format) {
    fake *f = u;
    f->grain = grain;
    f->format = format;
    (void)rate;
    return f->open_rc;
}
static int32_t f_output(void *u, int32_t handle, const void *samples) {
    fake *f = u;
    (void)handle;
    (void)samples;
    f->calls++;
    f->now += f->pace_us;
    if (f->cancel && f->cancel_after && f->calls >= f->cancel_after) *f->cancel = 1;
    if (f->fail_at_call && f->calls == f->fail_at_call) return f->fail_rc;
    return 0;
}
static int32_t f_close(void *u, int32_t handle) {
    fake *f = u;
    f->closed++;
    f->close_handle = handle;
    return 0;
}
static uint64_t f_now(void *u) { return ((fake *)u)->now; }

static akn_audio_ops ops_for(fake *f) {
    akn_audio_ops ops = { f_init, f_open, f_output, f_close, f_now, f, "fake" };
    return ops;
}

static void test_paced_success(void) {
    fake f = { 0 };
    f.open_rc = 7;
    f.pace_us = 21333; /* 1024 frames at 48 kHz */
    akn_audio_ops ops = ops_for(&f);
    akn_audio_result r;
    CHECK(akn_audio_run(&ops, 0, 1, 1, NULL, &r) == AKN_AUDIO_ACCEPTED_PACED);
    CHECK(f.grain == 1024);
    CHECK(f.format == AUDIO_LAB_FORMAT_S16_STEREO);
    CHECK(r.open_rc == 7);
    CHECK(f.closed == 1 && f.close_handle == 7);
    CHECK(r.closed == 1 && r.close_rc == 0);
    /* 250 + 1500 + 250 ms at 48 kHz = 96000 frames -> 94 calls of 1024 */
    CHECK(r.calls == 94 && r.ok_calls == 94);
    CHECK(r.frames_submitted == 94 * 1024);
    CHECK(r.nonzero_frames > 70000 && r.nonzero_frames < 73000); /* the 1.5 s tone */
    CHECK(r.peak > 0.09 && r.peak <= 0.101);                  /* -20 dBFS = 0.1 */
    CHECK(r.call_avg_us == 21333);
    CHECK(r.block_us == 21333);
    char text[1600];
    akn_audio_format(&r, text, sizeof(text));
    CHECK_CONTAINS(text, "Audio test 1 (fake)");
    CHECK_CONTAINS(text, "handle 7");
    CHECK_CONTAINS(text, "94 calls, 94 succeeded");
    CHECK_CONTAINS(text, "accepted and paced at playback speed");
    CHECK_CONTAINS(text, "not answered");
}

static void test_not_paced(void) {
    fake f = { 0 };
    f.open_rc = 3;
    f.pace_us = 50; /* returns at once: samples discarded */
    akn_audio_ops ops = ops_for(&f);
    akn_audio_result r;
    CHECK(akn_audio_run(&ops, 1, 0, 2, NULL, &r) == AKN_AUDIO_ACCEPTED_NOT_PACED);
    CHECK(f.grain == 256);
    CHECK(r.peak > 0.045 && r.peak <= 0.051); /* -26 dBFS */
}

static void test_failures(void) {
    akn_audio_result r;
    fake f = { 0 };
    f.init_rc = (int32_t)0x80260001;
    akn_audio_ops ops = ops_for(&f);
    CHECK(akn_audio_run(&ops, 0, 1, 1, NULL, &r) == AKN_AUDIO_INIT_FAILED);
    CHECK(f.closed == 0 && r.calls == 0);

    fake g = { 0 };
    g.init_rc = AKN_AUDIO_ALREADY_INITIALIZED; /* counts as success */
    g.open_rc = (int32_t)0x8026000A;
    ops = ops_for(&g);
    CHECK(akn_audio_run(&ops, 0, 1, 1, NULL, &r) == AKN_AUDIO_OPEN_FAILED);
    CHECK(g.closed == 0); /* nothing to close */
    char text[1600];
    akn_audio_format(&r, text, sizeof(text));
    CHECK_CONTAINS(text, "already initialized, OK");
    CHECK_CONTAINS(text, "Open:             0x8026000a");

    fake h = { 0 };
    h.open_rc = 5;
    h.pace_us = 20000;
    h.fail_at_call = 4;
    h.fail_rc = (int32_t)0x80260004;
    ops = ops_for(&h);
    CHECK(akn_audio_run(&ops, 0, 1, 1, NULL, &r) == AKN_AUDIO_OUTPUT_FAILED);
    CHECK(r.first_error == (int32_t)0x80260004 && r.first_error_call == 4 && r.ok_calls == 3);
    CHECK(h.closed == 1); /* the port is always closed */

    /* Invalid preset or level: nothing is called. */
    fake k = { 0 };
    ops = ops_for(&k);
    CHECK(akn_audio_run(&ops, 9, 1, 1, NULL, &r) == AKN_AUDIO_NOT_RUN);
    CHECK(akn_audio_run(&ops, 0, -1, 1, NULL, &r) == AKN_AUDIO_NOT_RUN);
}

static void test_cancel(void) {
    volatile int cancel = 0;
    fake f = { 0 };
    f.open_rc = 9;
    f.pace_us = 21333;
    f.cancel = &cancel;
    f.cancel_after = 10;
    akn_audio_ops ops = ops_for(&f);
    akn_audio_result r;
    CHECK(akn_audio_run(&ops, 0, 2, 1, &cancel, &r) == AKN_AUDIO_CANCELLED);
    CHECK(r.calls == 10);
    CHECK(f.closed == 1);
}

static void test_levels(void) {
    CHECK(akn_audio_amplitude(-20) > 0.0999 && akn_audio_amplitude(-20) < 0.1001);
    CHECK(akn_audio_amplitude(0) <= 0.502); /* hard cap -6 dBFS */
    CHECK(akn_audio_amplitude(-6) == akn_audio_amplitude(3));
    CHECK(akn_audio_preset_at(AKN_AUDIO_PRESET_COUNT) == NULL);
    CHECK(akn_audio_level_at(AKN_AUDIO_DEFAULT_LEVEL)->dbfs == -20);
    for (int i = 0; i < AKN_AUDIO_LEVEL_COUNT; i++) CHECK(akn_audio_level_at(i)->dbfs <= -14);
    for (int i = 0; i < AKN_AUDIO_PRESET_COUNT; i++) CHECK(audio_lab_grain_valid(akn_audio_preset_at(i)->grain));
    CHECK_STR(akn_heard_name(AKN_HEARD_YES), "HEARD (your report)");
}

void run_audio_tests(void) {
    test_paced_success();
    test_not_paced();
    test_failures();
    test_cancel();
    test_levels();
}
