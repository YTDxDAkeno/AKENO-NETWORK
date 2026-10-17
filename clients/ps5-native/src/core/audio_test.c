#include "audio_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio_lab_pcm.h"

#define LEAD_MS 250u
#define TONE_MS 1500u
#define TAIL_MS 250u
#define TONE_HZ 440u

static const akn_audio_preset PRESETS[AKN_AUDIO_PRESET_COUNT] = {
    { "s16-stereo-1024", "S16 stereo, 1024 frames (SDL default)", AUDIO_LAB_FORMAT_S16_STEREO, 1024 },
    { "s16-stereo-256", "S16 stereo, 256 frames (passthrough sample)", AUDIO_LAB_FORMAT_S16_STEREO, 256 },
    { "float-stereo-1024", "Float stereo, 1024 frames", AUDIO_LAB_FORMAT_FLOAT_STEREO, 1024 },
};

static const akn_audio_level LEVELS[AKN_AUDIO_LEVEL_COUNT] = {
    { "quiet", "Quiet (-26 dBFS)", -26 },
    { "moderate", "Moderate (-20 dBFS)", -20 },
    { "clearer", "Clearer (-14 dBFS)", -14 },
};

const akn_audio_preset *akn_audio_preset_at(int index) {
    return (index >= 0 && index < AKN_AUDIO_PRESET_COUNT) ? &PRESETS[index] : NULL;
}

const akn_audio_level *akn_audio_level_at(int index) {
    return (index >= 0 && index < AKN_AUDIO_LEVEL_COUNT) ? &LEVELS[index] : NULL;
}

double akn_audio_amplitude(int dbfs) {
    if (dbfs > AKN_AUDIO_HARD_CAP_DBFS) dbfs = AKN_AUDIO_HARD_CAP_DBFS;
    return pow(10.0, dbfs / 20.0);
}

const char *akn_audio_outcome_name(akn_audio_outcome outcome) {
    switch (outcome) {
    case AKN_AUDIO_NOT_RUN: return "not run";
    case AKN_AUDIO_INIT_FAILED: return "failed to initialize";
    case AKN_AUDIO_OPEN_FAILED: return "failed to open the port";
    case AKN_AUDIO_OUTPUT_FAILED: return "failed to submit samples";
    case AKN_AUDIO_CANCELLED: return "cancelled";
    case AKN_AUDIO_ACCEPTED_NOT_PACED: return "accepted, returned faster than real time";
    case AKN_AUDIO_ACCEPTED_PACED: return "accepted and paced at playback speed";
    }
    return "unknown";
}

const char *akn_heard_name(akn_heard heard) {
    switch (heard) {
    case AKN_HEARD_YES: return "HEARD (your report)";
    case AKN_HEARD_NO: return "NOT heard (your report)";
    case AKN_HEARD_UNSURE: return "not sure (your report)";
    case AKN_HEARD_UNANSWERED: break;
    }
    return "not answered";
}

/* 64-byte aligned, like audio lab v2 and SDL's heap buffer. */
static _Alignas(64) unsigned char g_block[AUDIO_LAB_MAX_BLOCK_BYTES];

akn_audio_outcome akn_audio_run(const akn_audio_ops *ops, int preset, int level, int test_number,
                                const volatile int *cancel, akn_audio_result *out) {
    memset(out, 0, sizeof(*out));
    out->test_number = test_number;
    out->preset = preset;
    out->level = level;
    snprintf(out->backend, sizeof(out->backend), "%s", ops->backend ? ops->backend : "unknown");
    const akn_audio_preset *p = akn_audio_preset_at(preset);
    const akn_audio_level *l = akn_audio_level_at(level);
    if (!p || !l) return out->outcome = AKN_AUDIO_NOT_RUN;
    unsigned frame_bytes = audio_lab_format_frame_bytes(p->format);
    if (!frame_bytes || !audio_lab_grain_valid(p->grain) || p->grain * frame_bytes > sizeof(g_block))
        return out->outcome = AKN_AUDIO_NOT_RUN;
    out->block_us = (uint64_t)p->grain * 1000000u / AUDIO_LAB_RATE;
    uint64_t started = ops->now_us(ops->user);

    out->init_rc = ops->init(ops->user);
    if (out->init_rc != 0 && out->init_rc != AKN_AUDIO_ALREADY_INITIALIZED) return out->outcome = AKN_AUDIO_INIT_FAILED;

    out->open_rc = ops->open(ops->user, p->grain, AUDIO_LAB_RATE, p->format);
    if (out->open_rc <= 0) return out->outcome = AKN_AUDIO_OPEN_FAILED;
    int32_t handle = out->open_rc;

    unsigned long lead = (unsigned long)AUDIO_LAB_RATE * LEAD_MS / 1000u;
    unsigned long tone = (unsigned long)AUDIO_LAB_RATE * TONE_MS / 1000u;
    unsigned long total = lead + tone + (unsigned long)AUDIO_LAB_RATE * TAIL_MS / 1000u;
    audio_lab_osc osc;
    audio_lab_osc_init(&osc, TONE_HZ, akn_audio_amplitude(l->dbfs), lead, tone);

    uint64_t sum = 0;
    unsigned long timed = 0;
    akn_audio_outcome outcome = AKN_AUDIO_ACCEPTED_PACED;
    while (out->frames_submitted < total) {
        if (cancel && *cancel) {
            outcome = AKN_AUDIO_CANCELLED;
            break;
        }
        audio_lab_block_stats stats = { 0, 0 };
        audio_lab_fill(&osc, p->format, g_block, p->grain, &stats);
        uint64_t t0 = ops->now_us(ops->user);
        int32_t rc = ops->output(ops->user, handle, g_block);
        uint64_t t1 = ops->now_us(ops->user);
        out->calls++;
        out->last_rc = rc;
        if (rc < 0) {
            out->first_error = rc;
            out->first_error_call = out->calls;
            outcome = AKN_AUDIO_OUTPUT_FAILED;
            break;
        }
        out->ok_calls++;
        out->frames_submitted += p->grain;
        out->nonzero_frames += stats.nonzero;
        if (stats.peak > out->peak) out->peak = stats.peak;
        if (out->calls >= 2) { /* the first call may only fill a queue */
            uint64_t d = t1 >= t0 ? t1 - t0 : 0;
            if (timed == 0 || d < out->call_min_us) out->call_min_us = d;
            if (d > out->call_max_us) out->call_max_us = d;
            sum += d;
            timed++;
        }
    }
    out->close_rc = ops->close(ops->user, handle);
    out->closed = 1;
    if (timed) out->call_avg_us = sum / timed;
    out->total_us = ops->now_us(ops->user) - started;
    if (outcome == AKN_AUDIO_ACCEPTED_PACED && (timed == 0 || out->call_avg_us * 2 < out->block_us))
        outcome = AKN_AUDIO_ACCEPTED_NOT_PACED;
    return out->outcome = outcome;
}

void akn_audio_format(const akn_audio_result *r, char *buf, size_t len) {
    const akn_audio_preset *p = akn_audio_preset_at(r->preset);
    const akn_audio_level *l = akn_audio_level_at(r->level);
    int n = snprintf(buf, len,
                     "Audio test %d (%s)\n"
                     "  Request:          %s, %s, 440 Hz, 1.5 s, 48000 Hz, main port of the system user (0xFF)\n"
                     "  Init:             0x%08x%s\n",
                     r->test_number, r->backend, p ? p->label : "?", l ? l->label : "?", (unsigned)r->init_rc,
                     r->init_rc == AKN_AUDIO_ALREADY_INITIALIZED ? " (already initialized, OK)" : "");
    if (n < 0 || (size_t)n >= len) return;
    size_t used = (size_t)n;
    if (r->outcome != AKN_AUDIO_INIT_FAILED && r->outcome != AKN_AUDIO_NOT_RUN) {
        if (r->open_rc > 0) n = snprintf(buf + used, len - used, "  Open:             handle %d\n", (int)r->open_rc);
        else n = snprintf(buf + used, len - used, "  Open:             0x%08x\n", (unsigned)r->open_rc);
        if (n < 0 || (size_t)n >= len - used) return;
        used += (size_t)n;
    }
    if (r->calls) {
        n = snprintf(buf + used, len - used,
                     "  Output:           %lu calls, %lu succeeded, last 0x%08x",
                     r->calls, r->ok_calls, (unsigned)r->last_rc);
        if (n < 0 || (size_t)n >= len - used) return;
        used += (size_t)n;
        if (r->first_error_call)
            n = snprintf(buf + used, len - used, ", first error 0x%08x at call %lu\n", (unsigned)r->first_error,
                         r->first_error_call);
        else n = snprintf(buf + used, len - used, "\n");
        if (n < 0 || (size_t)n >= len - used) return;
        used += (size_t)n;
        n = snprintf(buf + used, len - used,
                     "  Submitted:        %lu frames, %lu with nonzero PCM, peak %.3f\n"
                     "  Timing:           block %.1f ms; calls 2..n min %.1f / avg %.1f / max %.1f ms\n",
                     r->frames_submitted, r->nonzero_frames, r->peak, r->block_us / 1000.0, r->call_min_us / 1000.0,
                     r->call_avg_us / 1000.0, r->call_max_us / 1000.0);
        if (n < 0 || (size_t)n >= len - used) return;
        used += (size_t)n;
    }
    if (r->closed) {
        n = snprintf(buf + used, len - used, "  Close:            0x%08x\n", (unsigned)r->close_rc);
        if (n < 0 || (size_t)n >= len - used) return;
        used += (size_t)n;
    }
    snprintf(buf + used, len - used, "  Outcome:          %s\n  Heard:            %s\n", akn_audio_outcome_name(r->outcome),
             akn_heard_name(r->heard));
}
