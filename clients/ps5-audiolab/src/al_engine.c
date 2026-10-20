/*
 * AKENO Audio Lab 3.1 engine: initialises the run state and executes one tone through the backend.
 * Portable (the backend is the only platform seam) and unit-tested. One tone at a time; the caller
 * (main's worker thread, or a test) guarantees no overlap.
 */
#include "al.h"

#include <stdio.h>
#include <string.h>

void al_engine_init(al_state *st, const al_backend *be) {
    memset(st, 0, sizeof(*st));
    snprintf(st->build_id, sizeof(st->build_id), "%s", AL_BUILD_ID);
    uint32_t raw = 0;
    st->have_fw = be->fw(be->user, &raw);
    st->firmware_raw = raw;
    if (st->have_fw && raw)
        snprintf(st->firmware, sizeof(st->firmware), "%x.%02x", (raw >> 24) & 0xFF, (raw >> 16) & 0xFF);
    st->pid = be->pid(be->user);
    st->port_state_opt_in = be->port_state_opt_in(be->user);
    st->next_run = 1;
    st->started_unix = be->now_unix(be->user);
    st->last_heartbeat_unix = st->started_unix;
}

typedef struct {
    audio_lab_osc osc;
    int format;
} fill_ctx;

static unsigned long fill_block(void *user, void *block, unsigned frames, double *peak) {
    fill_ctx *c = user;
    audio_lab_block_stats stats;
    audio_lab_fill(&c->osc, c->format, block, frames, &stats);
    *peak = stats.peak;
    return stats.nonzero;
}

/*
 * Runs one tone. Returns the run id (>=1), or a negative code:
 *   -3 per-launch tone limit reached
 * The caller handles "busy" and "cooldown" (timing concerns it owns).
 */
int al_engine_run_tone(al_state *st, const al_backend *be, const al_preset *preset, const al_level *level,
                       volatile int *cancel) {
    if (st->tone_commands >= AL_RUNS_PER_LAUNCH) {
        snprintf(st->last_note, sizeof(st->last_note), "Test limit for this launch reached (%d). Restart the lab.",
                 AL_RUNS_PER_LAUNCH);
        return -3;
    }
    int id = st->next_run++;
    st->tone_commands++;
    al_run *run = &st->runs[id % AL_RUNS_KEPT];
    memset(run, 0, sizeof(*run));
    run->id = id;
    run->started_unix = be->now_unix(be->user);
    run->preset = preset;
    run->level = level;
    run->paced = -1;
    run->heard = AL_HEARD_UNREPORTED;

    unsigned long total = (unsigned long)AUDIO_LAB_RATE * (AL_TONE_LEAD_MS + AL_TONE_MS + AL_TONE_TAIL_MS) / 1000u;
    run->frames_planned = (total + preset->grain - 1) / preset->grain * preset->grain;

    fill_ctx ctx;
    ctx.format = preset->format;
    audio_lab_osc_init(&ctx.osc, AL_TONE_FREQ_HZ, level->amplitude,
                       (unsigned long)AUDIO_LAB_RATE * AL_TONE_LEAD_MS / 1000u,
                       (unsigned long)AUDIO_LAB_RATE * AL_TONE_MS / 1000u);
    al_audio_params p;
    memset(&p, 0, sizeof(p));
    p.user_id = 0xFF;
    p.port_type = 0;
    p.format = preset->format;
    p.grain = preset->grain;
    p.rate = AUDIO_LAB_RATE;
    p.frames = run->frames_planned;
    p.fill = fill_block;
    p.fill_user = &ctx;
    p.cancel = cancel;
    p.measure_port_state = st->port_state_opt_in;

    run->rc = be->audio_run(be->user, &p, &run->result);
    st->port_state_supported = run->result.port_state_exported;
    if (cancel && *cancel && !run->result.open_called) run->outcome = AL_OUT_CANCELLED;
    else run->outcome = al_classify(run->rc, &run->result);
    run->paced = al_paced(&run->result, preset->grain);
    st->tones_played++;
    return id;
}

/* The owner's verdict for a run. Returns 0, or -1 for an unknown run. */
int al_engine_feedback(al_state *st, int run_id, al_heard heard, const char *route) {
    al_run *run = al_run_by_id(st, run_id);
    if (!run) return -1;
    run->heard = heard;
    snprintf(run->route, sizeof(run->route), "%s", route ? route : "");
    return 0;
}
