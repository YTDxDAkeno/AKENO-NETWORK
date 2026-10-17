/* Audio lab v3 port-state measurement (app/audio_lab_state.c): guards, written-byte map, decoding. */
#include <stdint.h>
#include <string.h>

#include "audio_lab_state.h"
#include "test.h"

static unsigned char g_arena[AUDIO_LAB_STATE_ARENA_BYTES] __attribute__((aligned(64)));
static int g_calls;

/* PS4 layout: output 0x0001 (primary), channel 2, volume 127, rerouteCounter 3, flag 0. */
static int32_t fn_ps4(int32_t handle, void *state) {
    unsigned char *b = state;
    g_calls++;
    if (handle != 5) return (int32_t)0x80260003u;
    memset(b, 0, 32);
    b[0] = 0x01;
    b[2] = 2;
    b[4] = 127;
    b[6] = 3;
    return 0;
}

/* Writes 0xA5 and 0x5A into bytes that equal the fills, to show both calls are needed. */
static int32_t fn_fill_values(int32_t handle, void *state) {
    unsigned char *b = state;
    (void)handle;
    g_calls++;
    b[0] = 0xA5;
    b[1] = 0x5A;
    b[2] = 0x00;
    return 0;
}

static int32_t fn_error(int32_t handle, void *state) {
    (void)handle;
    (void)state;
    g_calls++;
    return (int32_t)0x80260004u;
}

static int32_t fn_overrun(int32_t handle, void *state) {
    (void)handle;
    g_calls++;
    memset(state, 0, AUDIO_LAB_STATE_WINDOW_BYTES + 8);
    return 0;
}

static int32_t fn_underrun(int32_t handle, void *state) {
    (void)handle;
    g_calls++;
    ((unsigned char *)state)[-1] = 0;
    return 0;
}

static int32_t fn_changing(int32_t handle, void *state) {
    (void)handle;
    unsigned char *b = state;
    b[0] = (unsigned char)(0x10 + g_calls++);
    return 0;
}

static void test_arena_layout(void) {
    audio_lab_state_prepare(g_arena, AUDIO_LAB_STATE_FILL_A);
    unsigned char *w = audio_lab_state_window(g_arena);
    CHECK(((uintptr_t)w & 63u) == 0);
    CHECK(w == g_arena + AUDIO_LAB_STATE_LEAD_BYTES);
    CHECK(g_arena[0] == AUDIO_LAB_STATE_CANARY && g_arena[AUDIO_LAB_STATE_LEAD_BYTES - 1] == AUDIO_LAB_STATE_CANARY);
    CHECK(w[0] == AUDIO_LAB_STATE_FILL_A && w[AUDIO_LAB_STATE_WINDOW_BYTES - 1] == AUDIO_LAB_STATE_FILL_A);
    CHECK(w[AUDIO_LAB_STATE_WINDOW_BYTES] == AUDIO_LAB_STATE_CANARY);
    CHECK(g_arena[AUDIO_LAB_STATE_ARENA_BYTES - 1] == AUDIO_LAB_STATE_CANARY);
}

static void test_ps4_layout(void) {
    audio_lab_state_sample s;
    g_calls = 0;
    CHECK(audio_lab_state_measure(fn_ps4, 5, g_arena, &s) == 0);
    CHECK(g_calls == 2);
    CHECK(s.attempted && s.call[0].called && s.call[1].called);
    CHECK(s.call[0].fill == AUDIO_LAB_STATE_FILL_A && s.call[1].fill == AUDIO_LAB_STATE_FILL_B);
    CHECK(s.call[0].rc == 0 && s.call[1].rc == 0);
    CHECK(s.guards_intact && s.calls_agree);
    CHECK(s.written_count == 32 && s.highest_written == 31);
    CHECK(s.call[0].bytes[0] == 0x01 && s.call[0].bytes[32] == AUDIO_LAB_STATE_FILL_A && s.call[1].bytes[32] == AUDIO_LAB_STATE_FILL_B);
    CHECK(audio_lab_state_shown_bytes(&s) == 32);
    audio_lab_state_decoded d;
    audio_lab_state_decode(&s, &d);
    CHECK(d.output_known && d.channel_known && d.volume_known && d.reroute_known && d.flag_known);
    CHECK(d.output == 1 && d.channel == 2 && d.volume == 127 && d.reroute_counter == 3 && d.flag == 0);
    char names[64];
    audio_lab_state_output_names(d.output, names, sizeof(names));
    CHECK_STR(names, "primary");
    char hex[200];
    audio_lab_state_hex(s.call[0].bytes, 8, hex, sizeof(hex));
    CHECK_STR(hex, "01 00 02 00 7f 00 03 00");
    audio_lab_state_values_hex(&s, 4, hex, sizeof(hex));
    CHECK_STR(hex, "01 00 02 00");
}

static void test_values_equal_to_a_fill(void) {
    audio_lab_state_sample s;
    g_calls = 0;
    CHECK(audio_lab_state_measure(fn_fill_values, 1, g_arena, &s) == 0);
    /* 0xA5 at 0 is invisible to the first call, 0x5A at 1 to the second; together all three show. */
    CHECK(s.written_count == 3 && s.highest_written == 2);
    CHECK(s.written[0] && s.value[0] == 0xA5);
    CHECK(s.written[1] && s.value[1] == 0x5A);
    CHECK(s.written[2] && s.value[2] == 0x00);
    CHECK(!s.written[3]);
    CHECK(s.calls_agree);
    char hex[64];
    audio_lab_state_values_hex(&s, 5, hex, sizeof(hex));
    CHECK_STR(hex, "a5 5a 00 -- --");
    audio_lab_state_decoded d;
    audio_lab_state_decode(&s, &d);
    CHECK(d.output_known && d.output == 0x5AA5 && d.channel_known && !d.volume_known && !d.flag_known);
}

static void test_error_stops_after_first_call(void) {
    audio_lab_state_sample s;
    g_calls = 0;
    CHECK(audio_lab_state_measure(fn_error, 5, g_arena, &s) == 0);
    CHECK(g_calls == 1);
    CHECK(s.call[0].called && !s.call[1].called);
    CHECK((uint32_t)s.call[0].rc == 0x80260004u);
    CHECK(s.written_count == 0 && s.highest_written == -1 && s.guards_intact);
    audio_lab_state_decoded d;
    audio_lab_state_decode(&s, &d);
    CHECK(!d.output_known && !d.channel_known);
    g_calls = 0;
    CHECK(audio_lab_state_measure(fn_ps4, 6, g_arena, &s) == 0); /* wrong handle */
    CHECK(g_calls == 1 && (uint32_t)s.call[0].rc == 0x80260003u);
}

static void test_guards(void) {
    audio_lab_state_sample s;
    g_calls = 0;
    CHECK(audio_lab_state_measure(fn_overrun, 1, g_arena, &s) == -1);
    CHECK(g_calls == 1); /* no second call once a canary changed */
    CHECK(!s.guards_intact && s.call[0].lead_intact && !s.call[0].tail_intact);
    CHECK(audio_lab_state_shown_bytes(&s) == AUDIO_LAB_STATE_WINDOW_BYTES);
    g_calls = 0;
    CHECK(audio_lab_state_measure(fn_underrun, 1, g_arena, &s) == -1);
    CHECK(g_calls == 1 && !s.call[0].lead_intact && s.call[0].tail_intact);
}

static void test_changing_values(void) {
    audio_lab_state_sample s;
    g_calls = 0;
    CHECK(audio_lab_state_measure(fn_changing, 1, g_arena, &s) == 0);
    CHECK(s.written_count == 1 && !s.calls_agree);
}

static void test_output_names(void) {
    char names[80];
    audio_lab_state_output_names(0, names, sizeof(names));
    CHECK_STR(names, "none");
    audio_lab_state_output_names(0x41, names, sizeof(names));
    CHECK_STR(names, "primary, headphone");
    audio_lab_state_output_names(0x108, names, sizeof(names));
    CHECK_STR(names, "unknown bits 0x108");
    char tiny[4];
    audio_lab_state_output_names(0x41, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));
    char hex[5];
    unsigned char bytes[4] = { 1, 2, 3, 4 };
    CHECK(audio_lab_state_hex(bytes, 4, hex, sizeof(hex)) == 2);
    CHECK_STR(hex, "01");
}

void run_audio_state_tests(void) {
    test_arena_layout();
    test_ps4_layout();
    test_values_equal_to_a_fill();
    test_error_stops_after_first_call();
    test_guards();
    test_changing_values();
    test_output_names();
}
