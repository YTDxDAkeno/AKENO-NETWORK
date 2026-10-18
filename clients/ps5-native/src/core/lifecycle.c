#include "lifecycle.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "voice.h"

void akn_lifecycle_init(akn_lifecycle *l, uint64_t now_ms, int32_t bigapp) {
    memset(l, 0, sizeof(*l));
    l->started_ms = l->last_ms = now_ms;
    l->bigapp_start = l->bigapp_last = bigapp;
}

int akn_lifecycle_tick(akn_lifecycle *l, uint64_t now_ms, int voice_state, int mic_state) {
    int paused = 0;
    l->iterations++;
    if (now_ms > l->last_ms && now_ms - l->last_ms > AKN_LIFE_PAUSE_MS) {
        uint64_t gap = now_ms - l->last_ms;
        uint32_t g = gap > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)gap;
        akn_life_pause p = { now_ms - l->started_ms, g, l->voice_last, voice_state, l->mic_last, mic_state, l->bigapp_last };
        memmove(&l->history[1], &l->history[0], sizeof(l->history) - sizeof(l->history[0]));
        l->history[0] = p;
        l->pauses++;
        l->paused_total_ms += gap;
        if (g > l->longest_ms) l->longest_ms = g;
        paused = 1;
    }
    l->last_ms = now_ms;
    l->voice_last = voice_state;
    l->mic_last = mic_state;
    return paused;
}

void akn_lifecycle_bigapp(akn_lifecycle *l, int32_t bigapp) {
    if (bigapp != l->bigapp_last) l->bigapp_changes++;
    l->bigapp_last = bigapp;
    if (l->pauses) l->history[0].bigapp_after = bigapp;
}

void akn_lifecycle_event(akn_lifecycle *l, akn_life_event e) {
    if ((unsigned)e < AKN_LIFE_EVENT_COUNT) l->events[e]++;
}

const char *akn_lifecycle_event_name(akn_life_event e) {
    static const char *names[AKN_LIFE_EVENT_COUNT] = { "focus lost", "focus gained", "hidden", "shown",
                                                       "minimized", "restored", "app background", "app foreground" };
    return (unsigned)e < AKN_LIFE_EVENT_COUNT ? names[e] : "?";
}

const akn_life_pause *akn_lifecycle_last(const akn_lifecycle *l) { return l->pauses ? &l->history[0] : NULL; }

static const char *voice_name(int s) {
    switch (s) {
    case AKN_VOICE_OFF: return "off";
    case AKN_VOICE_REQUESTING: return "requesting";
    case AKN_VOICE_CONNECTING: return "connecting";
    case AKN_VOICE_CONNECTED: return "connected";
    case AKN_VOICE_RECONNECTING: return "reconnecting";
    case AKN_VOICE_FAILED: return "failed";
    default: return "?";
    }
}

static const char *mic_name(int s) {
    switch (s) {
    case AKN_MIC_CLOSED: return "closed";
    case AKN_MIC_OPEN: return "open";
    case AKN_MIC_UNAVAILABLE: return "unavailable";
    case AKN_MIC_FAILED: return "failed";
    default: return "?";
    }
}

typedef struct {
    char *out;
    size_t len, used;
} sb;

static void put(sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(sb *b, const char *fmt, ...) {
    if (!b->len || b->used + 1 >= b->len) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->out + b->used, b->len - b->used, fmt, ap);
    va_end(ap);
    if (n > 0) b->used += (size_t)n < b->len - b->used ? (size_t)n : b->len - b->used - 1;
}

size_t akn_lifecycle_report(const akn_lifecycle *l, char *out, size_t len) {
    sb b = { out, len, 0 };
    if (len) out[0] = '\0';
    put(&b, "Pauses > %u ms:   %lu (longest %u ms, %.1f s in total) over %lu loop iterations\n", AKN_LIFE_PAUSE_MS, l->pauses,
        l->longest_ms, (double)l->paused_total_ms / 1000.0, l->iterations);
    put(&b, "Window events:   ");
    for (int i = 0; i < AKN_LIFE_EVENT_COUNT; i++) put(&b, "%s %s=%lu", i ? "," : "", akn_lifecycle_event_name((akn_life_event)i), l->events[i]);
    put(&b, "\nRunning app:      0x%x at start, 0x%x last seen, %lu changes\n", (unsigned)l->bigapp_start, (unsigned)l->bigapp_last,
        l->bigapp_changes);
    unsigned long shown = l->pauses < AKN_LIFE_HISTORY ? l->pauses : AKN_LIFE_HISTORY;
    for (unsigned long i = 0; i < shown; i++) {
        const akn_life_pause *p = &l->history[i];
        put(&b, "  pause at %.1f s: %.1f s; voice %s -> %s; microphone %s -> %s; running app 0x%x\n", (double)p->at_ms / 1000.0,
            (double)p->gap_ms / 1000.0, voice_name(p->voice_before), voice_name(p->voice_after), mic_name(p->mic_before),
            mic_name(p->mic_after), (unsigned)p->bigapp_after);
    }
    return b.used;
}
