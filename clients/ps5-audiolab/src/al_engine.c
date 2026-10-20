/*
 * AKENO Audio Lab 3.1 engine: initialises the run state and executes one tone through the backend.
 * Portable (the backend is the only platform seam) and unit-tested. One tone at a time; the caller
 * (main's worker thread, or a test) guarantees no overlap.
 *
 * Thread-safety: the tone worker and the main thread (which writes report.json / report.txt) both
 * touch al_state. The worker holds st->lock only for the short "prepare" and "commit" sections and
 * NEVER during the blocking audio call; the report writers and the main loop hold st->lock while
 * they read. The stop flag is an atomic_int shared without the lock (read inside the audio loop).
 */
#include "al.h"

#include <stdio.h>
#include <string.h>

void al_state_lock(al_state *st) {
    if (st->lock_ready) pthread_mutex_lock(&st->lock);
}
void al_state_unlock(al_state *st) {
    if (st->lock_ready) pthread_mutex_unlock(&st->lock);
}

void al_engine_init(al_state *st, const al_backend *be) {
    memset(st, 0, sizeof(*st));
    pthread_mutex_init(&st->lock, NULL);
    st->lock_ready = 1;
    snprintf(st->build_id, sizeof(st->build_id), "%s", AL_BUILD_ID);
    uint32_t raw = 0;
    st->have_fw = be->fw(be->user, &raw);
    st->firmware_raw = raw;
    if (st->have_fw && raw)
        snprintf(st->firmware, sizeof(st->firmware), "%x.%02x", (raw >> 24) & 0xFF, (raw >> 16) & 0xFF);
    st->pid = be->pid(be->user);
    /* Port-state inspection is deliberately disabled for this payload. */
    st->port_state_opt_in = 0;
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

int al_engine_run_tone(al_state *st, const al_backend *be, const al_preset *preset, const al_level *level,
                       atomic_int *cancel) {
    /* Prepare: allocate the run id and a placeholder under the lock (short). */
    al_state_lock(st);
    if (st->tone_commands >= AL_RUNS_PER_LAUNCH) {
        snprintf(st->last_note, sizeof(st->last_note), "Test limit for this launch reached (%d). Restart the lab.",
                 AL_RUNS_PER_LAUNCH);
        al_state_unlock(st);
        return -3;
    }
    int id = st->next_run++;
    st->tone_commands++;
    al_run *slot = &st->runs[id % AL_RUNS_KEPT];
    memset(slot, 0, sizeof(*slot));
    slot->id = id;
    slot->started_unix = be->now_unix(be->user);
    slot->preset = preset;
    slot->level = level;
    slot->paced = -1;
    slot->heard = AL_HEARD_UNREPORTED;
    unsigned long total = (unsigned long)AUDIO_LAB_RATE * (AL_TONE_LEAD_MS + AL_TONE_MS + AL_TONE_TAIL_MS) / 1000u;
    unsigned long frames = (total + preset->grain - 1) / preset->grain * preset->grain;
    slot->frames_planned = frames;
    al_state_unlock(st);

    /* Execute: the blocking audio call, with NO lock held. Results go into a local struct. */
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
    p.frames = frames;
    p.fill = fill_block;
    p.fill_user = &ctx;
    p.cancel = cancel;
    p.measure_port_state = 0;
    al_audio_result result;
    int rc = be->audio_run(be->user, &p, &result);
    int cancelled_early = cancel && atomic_load(cancel) && !result.open_called;
    al_outcome outcome = cancelled_early ? AL_OUT_CANCELLED : al_classify(rc, &result);
    int paced = al_paced(&result, preset->grain);

    /* Commit: write the finished run back under the lock (short). */
    al_state_lock(st);
    al_run *run = al_run_by_id(st, id);
    if (run) {
        run->rc = rc;
        run->result = result;
        run->outcome = outcome;
        run->paced = paced;
    }
    st->port_state_supported = result.port_state_exported;
    st->tones_played++;
    al_state_unlock(st);
    return id;
}

int al_engine_run_one_shot(al_state *st, const al_backend *be, atomic_int *cancel) {
    const al_preset *preset;
    const al_level *level;

    if (!st || !be || !be->sleep_ms)
        return -1;

    be->sleep_ms(be->user, AL_ONESHOT_DELAY_MS);
    if (cancel && atomic_load_explicit(cancel, memory_order_relaxed))
        return -2;

    preset = al_find_preset(NULL);
    level = al_find_level(NULL);
    if (!preset || !level)
        return -1;
    return al_engine_run_tone(st, be, preset, level, cancel);
}

int al_engine_feedback(al_state *st, int run_id, al_heard heard, const char *route) {
    al_state_lock(st);
    al_run *run = al_run_by_id(st, run_id);
    int rc = run ? 0 : -1;
    if (run) {
        run->heard = heard;
        snprintf(run->route, sizeof(run->route), "%s", route ? route : "");
    }
    al_state_unlock(st);
    return rc;
}
