/*
 * Test F: what happens to AKENO Native when it leaves the foreground (PS button, Control Center,
 * home screen) and comes back. It records, from the main loop only, what can be measured:
 *
 *   - pauses: gaps between two main-loop iterations longer than AKN_LIFE_PAUSE_MS (the process
 *     or its thread was not scheduled, or a call blocked), with their length;
 *   - window events SDL delivered (focus lost/gained, hidden/shown, minimized/restored) and
 *     SDL application events, counted, so the report shows whether the PS5 driver sends any;
 *   - the running BigApp ID when it differs from the one at start (another app took over);
 *   - the party voice state and microphone state before and after each pause.
 *
 * Nothing is inferred from a missing event. The report shows the numbers.
 */
#ifndef AKN_LIFECYCLE_H
#define AKN_LIFECYCLE_H

#include <stddef.h>
#include <stdint.h>

#define AKN_LIFE_PAUSE_MS 1000u
#define AKN_LIFE_HISTORY 8

typedef enum {
    AKN_LIFE_FOCUS_LOST = 0,
    AKN_LIFE_FOCUS_GAINED,
    AKN_LIFE_HIDDEN,
    AKN_LIFE_SHOWN,
    AKN_LIFE_MINIMIZED,
    AKN_LIFE_RESTORED,
    AKN_LIFE_APP_BACKGROUND,   /* SDL_APP_WILLENTERBACKGROUND / DIDENTERBACKGROUND */
    AKN_LIFE_APP_FOREGROUND,   /* SDL_APP_WILLENTERFOREGROUND / DIDENTERFOREGROUND */
    AKN_LIFE_EVENT_COUNT
} akn_life_event;

typedef struct {
    uint64_t at_ms;            /* when the loop resumed (ms since start) */
    uint32_t gap_ms;
    int voice_before, voice_after;   /* akn_voice_state values */
    int mic_before, mic_after;       /* akn_mic_state values */
    int32_t bigapp_after;
} akn_life_pause;

typedef struct {
    uint64_t started_ms, last_ms;
    int32_t bigapp_start, bigapp_last;
    unsigned long bigapp_changes;
    unsigned long iterations;
    unsigned long pauses;
    uint32_t longest_ms;
    uint64_t paused_total_ms;
    unsigned long events[AKN_LIFE_EVENT_COUNT];
    akn_life_pause history[AKN_LIFE_HISTORY]; /* the most recent pauses */
    int voice_last, mic_last;
} akn_lifecycle;

void akn_lifecycle_init(akn_lifecycle *l, uint64_t now_ms, int32_t bigapp);
/* Once per main-loop iteration, with the current voice and microphone state.
 * Returns 1 when this iteration ended a pause (the newest history entry describes it). */
int akn_lifecycle_tick(akn_lifecycle *l, uint64_t now_ms, int voice_state, int mic_state);
/* The running BigApp, sampled now and then (it costs a system call). */
void akn_lifecycle_bigapp(akn_lifecycle *l, int32_t bigapp);
void akn_lifecycle_event(akn_lifecycle *l, akn_life_event e);
const char *akn_lifecycle_event_name(akn_life_event e);
/* Report section text. */
size_t akn_lifecycle_report(const akn_lifecycle *l, char *out, size_t len);
const akn_life_pause *akn_lifecycle_last(const akn_lifecycle *l);

#endif
