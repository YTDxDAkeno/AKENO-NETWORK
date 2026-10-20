/* AKENO Audio Lab 3.1 core: presets and run bookkeeping (portable, unit-tested). */
#include "al.h"

#include <string.h>

/* The exact preset and level sets of Audio Lab v3 (clients/ps5/app/audio_lab.c, 05ad4f3). */
static const al_preset k_presets[] = {
    { "stereo16-1024", "S16 stereo, 1024 frames per call (the SDL driver's usual path)", AUDIO_LAB_FORMAT_S16_STEREO, 1024 },
    { "mono16-256", "S16 mono, 256 frames per call (as audio lab v1, for comparison)", AUDIO_LAB_FORMAT_S16_MONO, 256 },
    { "stereofloat-1024", "Float stereo, 1024 frames per call", AUDIO_LAB_FORMAT_FLOAT_STEREO, 1024 },
    { "stereo16-2048", "S16 stereo, 2048 frames per call (largest block)", AUDIO_LAB_FORMAT_S16_STEREO, 2048 },
};

static const al_level k_levels[] = {
    { "quiet", "Quiet (-26 dBFS)", 0.05 },
    { "moderate", "Moderate (-20 dBFS)", 0.1 },
    { "clear", "Clearer (-14 dBFS)", 0.2 },
};

const al_preset *al_presets(size_t *count) {
    *count = sizeof(k_presets) / sizeof(k_presets[0]);
    return k_presets;
}
const al_level *al_levels(size_t *count) {
    *count = sizeof(k_levels) / sizeof(k_levels[0]);
    return k_levels;
}

const al_preset *al_find_preset(const char *id) {
    if (!id || !id[0]) return &k_presets[0];
    for (size_t i = 0; i < sizeof(k_presets) / sizeof(k_presets[0]); i++)
        if (strcmp(k_presets[i].id, id) == 0) return &k_presets[i];
    return NULL;
}
const al_level *al_find_level(const char *id) {
    if (!id || !id[0]) return &k_levels[1]; /* moderate */
    for (size_t i = 0; i < sizeof(k_levels) / sizeof(k_levels[0]); i++)
        if (strcmp(k_levels[i].id, id) == 0) return &k_levels[i];
    return NULL;
}

const char *al_outcome_name(al_outcome o) {
    switch (o) {
    case AL_OUT_NOT_RUN: return "not-run";
    case AL_OUT_NOT_SUPPORTED: return "not-supported";
    case AL_OUT_INIT_FAILED: return "init-failed";
    case AL_OUT_OPEN_FAILED: return "open-failed";
    case AL_OUT_SUBMIT_FAILED: return "submit-failed";
    case AL_OUT_CANCELLED: return "cancelled";
    case AL_OUT_ACCEPTED: return "accepted";
    }
    return "unknown";
}

al_run *al_run_by_id(al_state *st, int id) {
    if (id <= 0) return NULL;
    al_run *r = &st->runs[id % AL_RUNS_KEPT];
    return r->id == id ? r : NULL;
}

/* ---- outcome and pacing (identical rules to Audio Lab v3) ---------------------------------- */

al_outcome al_classify(int rc, const al_audio_result *r) {
    if (rc == -2 || !r->library_loaded) return AL_OUT_NOT_SUPPORTED;
    if (r->init_rc != 0) return AL_OUT_INIT_FAILED;
    if (!r->open_called || r->open_rc < 1) return AL_OUT_OPEN_FAILED;
    if (r->first_error_rc < 0) return AL_OUT_SUBMIT_FAILED;
    if (r->cancelled) return AL_OUT_CANCELLED;
    return AL_OUT_ACCEPTED; /* a failed close after accepted samples stays accepted (detail in result) */
}

/* Consumed at playback speed when calls 2..n took at least 70 % of the audio they carried. */
int al_paced(const al_audio_result *r, unsigned grain) {
    if (r->calls_ok < 4) return -1;
    double expected_us = (double)(r->calls_ok - 1) * (double)grain * 1e6 / (double)AUDIO_LAB_RATE;
    return (double)r->call_total_us >= 0.7 * expected_us ? 1 : 0;
}
