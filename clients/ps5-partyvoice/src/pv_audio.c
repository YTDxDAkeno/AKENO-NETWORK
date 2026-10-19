/*
 * AKENO Party Voice: the playback, capture and test-sound threads. Each audio boundary is counted
 * (pv_engine.h); nothing is recorded or stored: microphone samples go to the encoder and the level
 * meter only, decoded samples to the output port and the level meter only.
 *
 * Output timing (0.2.0). 0.1.0 slept a block whenever an output call returned quickly. A console
 * port takes its first blocks at once (it has a small queue), so 0.1.0 slept, the port drained in
 * the meantime, the next call found room again and returned at once: every port read "NOT paced",
 * whether an audio clock consumed it or not (the owner's 12.20 log; reproduced by the simulated
 * queue port in the tests). Now the port is probed once with silent blocks back to back, the
 * port's own blocking paces the thread, and the thread sleeps only when the port runs more than a
 * few blocks ahead of real time (a port that discards). The silence of a background payload on
 * 12.20 is not explained by timing: audio lab v2 submitted back to back, was paced, and was silent.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lab_pcm.h"
#include "pv_internal.h"

#define BLOCK_US ((uint64_t)PV_OUT_GRAIN * 1000000u / PV_OUT_RATE) /* 21.333 ms */
#define BLOCK_MS ((double)PV_OUT_GRAIN * 1000.0 / PV_OUT_RATE)
#define AHEAD_MAX_US ((int64_t)(6 * BLOCK_US))   /* further ahead of real time than a port queue holds: sleep */
#define AHEAD_TARGET_US ((int64_t)(5 * BLOCK_US))   /* then one even sleep per block, no bursts */
#define BEHIND_MAX_US ((int64_t)200000)          /* far behind (the process was paused): start the clock over */
#define OUT_ERROR_LIMIT 50     /* failed output calls in a row before the port is given up */
#define MIC_ERROR_LIMIT 50
#define SOUND_PEAK 64          /* -54 dBFS: a frame with any sound in it, not just rounding noise */
#define RX_WINDOW 4800         /* 100 ms of what is played, for the level and the tone estimate */
#define TONE_HZ 880            /* the beeps of lab_pcm.c */
#define TONE_TOLERANCE 44      /* +-5 % */
#define TONE_BLOCK_LIMIT 400   /* a test sound on its own: at most 8.5 s of output calls */
#define USER_SYSTEM 0xFF

static uint64_t now_us(const pv_engine *e) { return e->be->now_us(e->be->user); }

/* ---- Classification (portable, unit-tested) --------------------------------------------------- */

pv_refusal_kind pv_refusal_of(int32_t rc, int microphone) {
    uint32_t c = (uint32_t)rc;
    if (rc >= 0) return PV_REFUSAL_NONE;
    if (c == 0x809B0081u) return PV_REFUSAL_PROCESS;
    if (rc == PV_NO_LIBRARY) return PV_REFUSAL_NO_LIBRARY;
    if (rc == PV_NO_USER_SERVICE || (c & 0xFFFF0000u) == 0x80960000u) return PV_REFUSAL_NO_USER;
    if (microphone) {
        if (c == 0x80260103u) return PV_REFUSAL_RATE;                     /* INVALID_FREQ */
        if (c == 0x80260107u || c == 0x8026010Au) return PV_REFUSAL_BUSY; /* PORT_FULL, BUSY */
        if (c >= 0x80260101u && c <= 0x80260106u) return PV_REFUSAL_ARGUMENT;
    } else {
        if (c == 0x80260011u) return PV_REFUSAL_RESOURCE;                 /* SYSTEM_RESOURCE */
        if (c == 0x80260008u) return PV_REFUSAL_RATE;                     /* INVALID_SAMPLE_FREQ */
        if (c == 0x80260002u || c == 0x80260005u) return PV_REFUSAL_BUSY; /* BUSY, PORT_FULL */
        if (c >= 0x80260003u && c <= 0x80260016u) return PV_REFUSAL_ARGUMENT;
    }
    return PV_REFUSAL_OTHER;
}

const char *pv_refusal_name(pv_refusal_kind k) {
    switch (k) {
    case PV_REFUSAL_NONE: return "none";
    case PV_REFUSAL_PROCESS: return "process";
    case PV_REFUSAL_RATE: return "rate";
    case PV_REFUSAL_ARGUMENT: return "argument";
    case PV_REFUSAL_RESOURCE: return "resource";
    case PV_REFUSAL_BUSY: return "busy";
    case PV_REFUSAL_NO_USER: return "no-user";
    case PV_REFUSAL_NO_LIBRARY: return "no-library";
    case PV_REFUSAL_OTHER: break;
    }
    return "other";
}

const char *pv_refusal_meaning(pv_refusal_kind k) {
    switch (k) {
    case PV_REFUSAL_NONE: return "no refusal";
    case PV_REFUSAL_PROCESS:
        return "refused to this process: 0x809B0081 has no public meaning; background payloads get the same code from scePadOpen "
               "(12.20, 13.60), and AKENO Native, the foreground app, opens the microphone with this same call on 12.20";
    case PV_REFUSAL_RATE: return "the sample rate was not accepted (PS4 SDK name: invalid frequency)";
    case PV_REFUSAL_ARGUMENT: return "an argument was not accepted (a PS4 SDK argument code of the audio library)";
    case PV_REFUSAL_RESOURCE:
        return "no port for this user (PS4 SDK name SYSTEM_RESOURCE); the system user's port of the same type opens";
    case PV_REFUSAL_BUSY: return "busy or full: another owner holds it (it may be tried again)";
    case PV_REFUSAL_NO_USER: return "no foreground user (the user service did not answer)";
    case PV_REFUSAL_NO_LIBRARY: return "the library or one of its functions is missing";
    case PV_REFUSAL_OTHER: break;
    }
    return "an unknown code";
}

int pv_refusal_transient(pv_refusal_kind k) { return k == PV_REFUSAL_BUSY; }

int pv_clock_verdict(double blocked_ms, double audio_ms) {
    if (audio_ms < 9.5 * BLOCK_MS) return -1; /* fewer than ten timed calls */
    double ratio = blocked_ms / audio_ms;
    return ratio >= 0.6 ? 1 : ratio <= 0.2 ? 0 : -1;
}

/* ---- The output port: open (once per situation), clock probe, paced submission ---------------- */

typedef struct {
    int32_t handle;               /* < 0: no port (incoming audio is still decoded in real time) */
    int route;
    uint64_t anchor_us, frames;   /* real time vs. audio submitted since the anchor */
    unsigned long calls;
    double blocked_ms, audio_ms;  /* calls 2..n */
    double min_ms, max_ms;
    unsigned long self_sleeps;
} pv_port;

/* 64-byte aligned blocks (as AKENO Native's audible test and audio lab v2), from malloc. */
typedef struct {
    void *raw;
    int16_t *pcm;
} pv_block;

static int block_alloc(pv_block *b, size_t samples) {
    b->raw = malloc(samples * sizeof(int16_t) + 63);
    b->pcm = b->raw ? (int16_t *)(((uintptr_t)b->raw + 63) & ~(uintptr_t)63) : NULL;
    if (b->pcm) memset(b->pcm, 0, samples * sizeof(int16_t));
    return b->pcm ? 0 : -1;
}

static int32_t ensure_out_init(pv_engine *e) {
    pthread_mutex_lock(&e->lock);
    int done = e->out_initialized;
    pthread_mutex_unlock(&e->lock);
    if (done) return 0;
    int32_t rc = e->be->out_init(e->be->user);
    if (rc == 0) {
        pthread_mutex_lock(&e->lock);
        e->out_initialized = 1;
        pthread_mutex_unlock(&e->lock);
    }
    return rc;
}

/* Silent blocks back to back: an audio clock that consumes the port makes calls 2..n block about
 * one block each (the audio labs' measure); a port that discards returns at once. Inaudible. */
static void probe(pv_engine *e, int32_t handle, const int16_t *silence) {
    double blocked = 0, audio = 0;
    int32_t rc = 0;
    int n = 0;
    for (int i = 0; i < PV_PROBE_BLOCKS; i++) {
        uint64_t t0 = now_us(e);
        rc = e->be->out_output(e->be->user, handle, silence);
        double ms = (double)(now_us(e) - t0) / 1000.0;
        if (rc < 0) break;
        n++;
        if (i > 0) {
            blocked += ms;
            audio += BLOCK_MS;
        }
    }
    int verdict = rc < 0 ? -1 : pv_clock_verdict(blocked, audio);
    pthread_mutex_lock(&e->lock);
    e->st.probe_blocks = n;
    e->st.probe_ms = blocked;
    e->st.probe_rc = rc < 0 ? rc : 0;
    e->st.probe_verdict = verdict;
    pthread_mutex_unlock(&e->lock);
    pv_log(e, "voice.output", "clock probe: %d silent blocks, calls 2..%d took %.1f ms for %.1f ms of audio -> %s%s", n, n, blocked, audio,
           verdict == 1 ? "an audio clock consumes the port" : verdict == 0 ? "the calls return at once (the port does not consume in real time)"
                                                             : "undecided",
           rc < 0 ? " (an output call failed)" : "");
}

/* Opens a route's port, unless this exact call was refused before in this situation. The port's
 * state goes to the session's output fields (in_voice) or to the test sound's. */
static int32_t port_open(pv_engine *e, int route, int in_voice, pv_port *p, const int16_t *silence) {
    memset(p, 0, sizeof(*p));
    p->handle = -1;
    p->route = route;
    const pv_route *r = pv_route_get(route);
    char why[160] = "";
    int32_t user = USER_SYSTEM, handle;
    int avoided = 0;
    if (!r || r->retired) {
        snprintf(why, sizeof(why), "route %d is retired (PS4 port numbering; refused like route 2 on 12.20): not opened", route);
        handle = PV_NO_LIBRARY;
    } else if (r->foreground_user && (handle = e->be->foreground_user(e->be->user, &user)) != 0) {
        snprintf(why, sizeof(why), "the foreground user is unknown (0x%08X)", (unsigned)handle);
    } else {
        pthread_mutex_lock(&e->lock);
        const pv_refusal *known = pv_refusal_find_locked(e, route, user);
        handle = known ? known->rc : 0;
        if (known) e->st.out_avoided++;
        pthread_mutex_unlock(&e->lock);
        if (known) {
            avoided = 1;
            snprintf(why, sizeof(why), "not tried again: route %d was refused (0x%08X) in this situation; it is retried when a game starts or ends",
                     route, (unsigned)handle);
        } else {
            int32_t rc = ensure_out_init(e);
            if (rc != 0) {
                snprintf(why, sizeof(why), "sceAudioOutInit failed (0x%08X)", (unsigned)rc);
                handle = rc;
            } else {
                handle = e->be->out_open(e->be->user, route, why, sizeof(why));
            }
        }
    }
    pv_refusal_kind kind = pv_refusal_of(handle, 0);
    pthread_mutex_lock(&e->lock);
    /* only the refusals of this process's kind are remembered for a port: an open happens on a join, a
     * route change or a test sound (never in a loop), so anything else may be tried on the next one */
    if (handle < 0 && !avoided && (kind == PV_REFUSAL_RESOURCE || kind == PV_REFUSAL_PROCESS)) pv_refusal_remember_locked(e, route, user, handle);
    if (in_voice) {
        e->st.out_opens += !avoided && handle >= 0;
        e->st.out_rc = handle < 0 ? handle : 0;
        e->st.out_refusal = kind;
        snprintf(e->st.out_detail, sizeof(e->st.out_detail), "%s", why);
        e->st.out = handle >= 0 ? PV_OUT_OPENING : PV_OUT_FAILED;
        e->st.paced = -1;
        if (handle < 0) {
            pv_note_locked(e, "AKENO Voice: the speaker route %d could not be opened (0x%08X)", route, (unsigned)handle);
            pv_set_failure_locked(e, "speaker route %d not opened: 0x%08X (%s)", route, (unsigned)handle, pv_refusal_name(kind));
        }
    } else {
        e->st.test_open_rc = handle < 0 ? handle : 0;
        snprintf(e->st.test_detail, sizeof(e->st.test_detail), "%s", why);
    }
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    if (handle < 0) {
        pv_log(e, "voice.output", "route %d: no output port: 0x%08X, %s (%s)", route, (unsigned)handle, pv_refusal_meaning(kind), why);
        return handle;
    }
    pv_log(e, "voice.output", "route %d: output port open (%s)", route, why);
    p->handle = handle;
    probe(e, handle, silence);
    if (in_voice) {
        pthread_mutex_lock(&e->lock);
        e->st.out = PV_OUT_ACTIVE;
        e->report_dirty = 1;
        pthread_mutex_unlock(&e->lock);
    }
    return handle;
}

/* One block to the port (or, without a port, its time). The port's blocking paces the caller; the
 * caller sleeps only when it is more than AHEAD_MAX_US ahead of real time, so a port that blocks
 * is never slept on, and one that discards is still fed in real time with bounded CPU. */
static int32_t port_submit(pv_engine *e, pv_port *p, const int16_t *stereo, int *called) {
    uint64_t t0 = now_us(e);
    if (!p->anchor_us) {
        p->anchor_us = t0;
        p->frames = 0;
    }
    int32_t rc = 0;
    *called = p->handle >= 0;
    if (*called) {
        rc = e->be->out_output(e->be->user, p->handle, stereo);
        double ms = (double)(now_us(e) - t0) / 1000.0;
        if (p->calls++ > 0) { /* the first call may only fill the queue */
            p->blocked_ms += ms;
            p->audio_ms += BLOCK_MS;
            if (p->calls == 2 || ms < p->min_ms) p->min_ms = ms;
            if (ms > p->max_ms) p->max_ms = ms;
        }
    }
    p->frames += PV_OUT_GRAIN;
    uint64_t now = now_us(e);
    int64_t ahead = (int64_t)(p->frames * 1000000u / PV_OUT_RATE) - (int64_t)(now - p->anchor_us);
    /* without a port there is no queue: keep to the block, so the jitter buffer drains evenly */
    int64_t max = *called ? AHEAD_MAX_US : 0, target = *called ? AHEAD_TARGET_US : 0;
    if (ahead > max) {
        pv_sleep_ms((unsigned)((ahead - target + 999) / 1000));
        p->self_sleeps += *called;
    } else if (ahead < -BEHIND_MAX_US) {
        p->anchor_us = now;
        p->frames = 0;
    }
    return rc;
}

static void port_close(pv_engine *e, pv_port *p, const int16_t *silence) {
    if (p->handle < 0) return;
    for (int i = 0; i < 2; i++) e->be->out_output(e->be->user, p->handle, silence); /* a short silence tail */
    e->be->out_close(e->be->user, p->handle);
    p->handle = -1;
}

/* ---- The test sound: N beeps through the voice pipeline (Opus, jitter buffer, decoder) --------- */

typedef struct {
    akn_voice_encoder *enc;
    akn_voice_decoder *dec;
    akn_jitter jitter;
    akn_lab_beeps beeps;
    uint16_t seq;
    int generating, active, drain;
    unsigned long encoded, decoded;
    int16_t stereo[2 * AKN_VOICE_FRAME];
} pv_tone;

static void tone_start(pv_tone *t, unsigned beeps) {
    akn_jitter_init(&t->jitter);
    akn_lab_beeps_init(&t->beeps, AKN_VOICE_RATE, beeps, PV_TEST_AMPLITUDE);
    t->seq = 0;
    t->generating = t->active = 1;
    t->drain = AKN_JITTER_SLOTS;
    t->encoded = t->decoded = 0;
}

/* The next 20 ms of the test sound as the voice pipeline delivers it: generated, Opus-encoded,
 * through a jitter buffer like a received packet, decoded by the same decoder code as speech. */
static void tone_next(pv_tone *t, int16_t *frame) {
    uint8_t packet[AKN_VOICE_MAX_PACKET];
    if (t->generating) {
        int16_t pcm[AKN_VOICE_FRAME];
        akn_lab_beeps_fill(&t->beeps, t->stereo, AKN_VOICE_FRAME);
        for (int i = 0; i < AKN_VOICE_FRAME; i++) pcm[i] = t->stereo[2 * i];
        int len = akn_voice_encode(t->enc, pcm, packet, sizeof(packet));
        if (len > 0 && akn_jitter_put(&t->jitter, t->seq++, packet, (size_t)len) == 0) t->encoded++;
        if (akn_lab_beeps_done(&t->beeps)) t->generating = 0;
    }
    size_t len = 0;
    akn_jitter_result r = akn_jitter_get(&t->jitter, packet, &len);
    if (r == AKN_JITTER_PACKET && akn_voice_decode(t->dec, packet, len, frame) > 0) t->decoded++;
    else if (r == AKN_JITTER_LOST) akn_voice_decode(t->dec, NULL, 0, frame);
    else memset(frame, 0, sizeof(int16_t) * AKN_VOICE_FRAME);
    if (!t->generating && (akn_jitter_depth(&t->jitter) == 0 || --t->drain <= 0)) t->active = 0;
}

/* ---- Playback: one loop for voice and for the test sound on its own ------------------------- */

typedef struct {
    pv_port port;
    akn_voice_decoder *dec;
    akn_pcm_fifo *speech_fifo, *tone_fifo;
    pv_tone *tone;
    pv_block silence, out, speech_st, tone_st;
    int16_t *speech, *tone_mono, *window, *left;
    size_t win_fill;
    int errors_in_row;
    /* the test sound in progress */
    int testing, tail;
    unsigned long t_calls, t_blocks, t_verified;
    int t_hz_min, t_hz_max, t_peak;
    double t_blocked_ms, t_audio_ms;
} pv_player;

static int player_init(pv_player *pl) {
    memset(pl, 0, sizeof(*pl));
    pl->port.handle = -1;
    pl->dec = akn_voice_decoder_new(NULL);
    pl->speech_fifo = malloc(sizeof(akn_pcm_fifo));
    pl->tone_fifo = malloc(sizeof(akn_pcm_fifo));
    pl->tone = calloc(1, sizeof(pv_tone));
    pl->speech = malloc(sizeof(int16_t) * PV_OUT_GRAIN);
    pl->tone_mono = malloc(sizeof(int16_t) * PV_OUT_GRAIN);
    pl->left = malloc(sizeof(int16_t) * PV_OUT_GRAIN);
    pl->window = malloc(sizeof(int16_t) * RX_WINDOW);
    int bad = block_alloc(&pl->silence, 2 * PV_OUT_GRAIN) | block_alloc(&pl->out, 2 * PV_OUT_GRAIN) |
              block_alloc(&pl->speech_st, 2 * PV_OUT_GRAIN) | block_alloc(&pl->tone_st, 2 * PV_OUT_GRAIN);
    if (pl->tone) {
        pl->tone->enc = akn_voice_encoder_new(NULL);
        pl->tone->dec = akn_voice_decoder_new(NULL);
    }
    if (bad || !pl->dec || !pl->speech_fifo || !pl->tone_fifo || !pl->tone || !pl->tone->enc || !pl->tone->dec || !pl->speech ||
        !pl->tone_mono || !pl->left || !pl->window)
        return -1;
    akn_pcm_fifo_init(pl->speech_fifo);
    akn_pcm_fifo_init(pl->tone_fifo);
    return 0;
}

static void player_free(pv_player *pl) {
    akn_voice_decoder_free(pl->dec);
    if (pl->tone) {
        akn_voice_encoder_free(pl->tone->enc);
        akn_voice_decoder_free(pl->tone->dec);
    }
    free(pl->tone);
    free(pl->speech_fifo);
    free(pl->tone_fifo);
    free(pl->speech);
    free(pl->tone_mono);
    free(pl->left);
    free(pl->window);
    free(pl->silence.raw);
    free(pl->out.raw);
    free(pl->speech_st.raw);
    free(pl->tone_st.raw);
}

static void test_begin(pv_engine *e, pv_player *pl, int route, int in_voice) {
    tone_start(pl->tone, (unsigned)route);
    pl->testing = 1;
    pl->tail = 0;
    pl->t_calls = pl->t_blocks = pl->t_verified = 0;
    pl->t_hz_min = pl->t_hz_max = pl->t_peak = 0;
    pl->t_blocked_ms = pl->t_audio_ms = 0;
    pthread_mutex_lock(&e->lock);
    e->st.test_state = PV_TEST_PLAYING;
    e->st.test_route = route;
    e->st.test_in_voice = in_voice;
    e->st.test_calls = e->st.test_tone_blocks = e->st.test_verified_blocks = 0;
    e->st.test_hz_min = e->st.test_hz_max = e->st.test_peak_percent = 0;
    e->st.test_encoded = e->st.test_decoded = 0;
    e->st.test_paced = -1;
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
}

static void test_end(pv_engine *e, pv_player *pl) {
    pl->testing = 0;
    pv_test_state state = pl->port.handle < 0 ? PV_TEST_NOT_PLAYED : pl->t_blocks ? PV_TEST_SUBMITTED : PV_TEST_OUTPUT_FAILED;
    int paced = pv_clock_verdict(pl->t_blocked_ms, pl->t_audio_ms);
    pthread_mutex_lock(&e->lock);
    int route = e->st.test_route, in_voice = e->st.test_in_voice;
    int32_t open_rc = in_voice ? e->st.out_rc : e->st.test_open_rc;
    e->st.test_state = state;
    e->st.test_paced = paced;
    e->st.test_encoded = pl->tone->encoded;
    e->st.test_decoded = pl->tone->decoded;
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    if (state == PV_TEST_NOT_PLAYED)
        pv_log(e, "voice.test_sound", "route %d: NOT played - the output port could not be opened (0x%08X)", route, (unsigned)open_rc);
    else
        pv_log(e, "voice.test_sound",
               "route %d%s: %lu Opus packets encoded, %lu decoded; %lu accepted output blocks carried the tone, %lu measured at %d Hz +-5 %% in "
               "the submitted buffer (%d..%d Hz), peak %d%%; timing %s. Audible only if you heard %d beep%s",
               route, in_voice ? " (mixed into voice)" : " (on its own)", pl->tone->encoded, pl->tone->decoded, pl->t_blocks, pl->t_verified,
               TONE_HZ, pl->t_hz_min, pl->t_hz_max, pl->t_peak, paced == 1 ? "paced" : paced == 0 ? "NOT paced" : "undecided", route,
               route == 1 ? "" : "s");
}

/* Refills both FIFOs to one block: party speech from the session's jitter buffer (decoded or
 * concealed; deafen silences it) and the test sound, frame by frame in step. */
static void fill(pv_engine *e, pv_session *s, pv_player *pl, int deafened) {
    int16_t frame[AKN_VOICE_FRAME], tone[AKN_VOICE_FRAME];
    uint8_t packet[AKN_VOICE_MAX_PACKET];
    unsigned long decoded = 0, concealed = 0, silent = 0, with_sound = 0;
    while (pl->speech_fifo->count < PV_OUT_GRAIN) {
        akn_jitter_result r = AKN_JITTER_EMPTY;
        size_t len = 0;
        if (s) {
            pthread_mutex_lock(&s->lock);
            r = akn_jitter_get(&s->jitter, packet, &len);
            unsigned long under = s->jitter.underruns, late = s->jitter.late, over = s->jitter.dropped_overflow;
            pthread_mutex_unlock(&s->lock);
            pthread_mutex_lock(&e->lock);
            e->st.underruns = under;
            e->st.late = late;
            e->st.overflow = over;
            pthread_mutex_unlock(&e->lock);
        }
        if (r == AKN_JITTER_PACKET) {
            akn_voice_decode(pl->dec, packet, len, frame);
            decoded++;
            with_sound += akn_pcm_peak(frame, AKN_VOICE_FRAME) > SOUND_PEAK;
        } else if (r == AKN_JITTER_LOST) {
            akn_voice_decode(pl->dec, NULL, 0, frame);
            concealed++;
        } else {
            memset(frame, 0, sizeof(frame));
            silent += s != NULL;
        }
        if (deafened) memset(frame, 0, sizeof(frame));
        if (pl->testing && pl->tone->active) tone_next(pl->tone, tone);
        else memset(tone, 0, sizeof(tone));
        akn_pcm_fifo_write(pl->speech_fifo, frame, AKN_VOICE_FRAME);
        akn_pcm_fifo_write(pl->tone_fifo, tone, AKN_VOICE_FRAME);
    }
    if (!s) return;
    pthread_mutex_lock(&e->lock);
    e->st.decoded += decoded;
    e->st.concealed += concealed;
    e->st.silent_frames += silent;
    e->st.decoded_nonzero += with_sound;
    e->st.queued_frames += decoded + concealed;
    pthread_mutex_unlock(&e->lock);
}

/* What is played, over 100 ms windows: the level and tone of party speech. */
static void measure_rx(pv_engine *e, pv_player *pl, int volume) {
    for (int i = 0; i < PV_OUT_GRAIN; i++) {
        pl->window[pl->win_fill++] = volume ? pl->speech[i] : 0;
        if (pl->win_fill == RX_WINDOW) {
            int level = akn_meter_percent(akn_pcm_rms_dbfs(pl->window, pl->win_fill));
            int hz = akn_pcm_tone_hz(pl->window, pl->win_fill, AKN_VOICE_RATE);
            pthread_mutex_lock(&e->lock);
            e->st.rx_level = level;
            if (hz) e->st.rx_tone_hz = hz;
            pthread_mutex_unlock(&e->lock);
            pl->win_fill = 0;
        }
    }
}

/* One block: mix, check, submit, count. Returns the output call's result (0 without a port). */
static int32_t play_block(pv_engine *e, pv_session *s, pv_player *pl, int in_voice) {
    pthread_mutex_lock(&e->lock);
    int volume = e->st.out_volume, deafened = e->st.deafened;
    pthread_mutex_unlock(&e->lock);
    fill(e, s, pl, deafened);
    akn_pcm_fifo_read(pl->speech_fifo, pl->speech, PV_OUT_GRAIN);
    akn_pcm_fifo_read(pl->tone_fifo, pl->tone_mono, PV_OUT_GRAIN);
    if (s) measure_rx(e, pl, deafened ? 0 : volume);
    /* the same volume and mono -> stereo conversion for both, then a saturating mix */
    int16_t *sp = pl->speech_st.pcm, *tn = pl->tone_st.pcm, *out = pl->out.pcm;
    akn_pcm_mono_to_stereo(pl->speech, PV_OUT_GRAIN, volume, sp);
    akn_pcm_mono_to_stereo(pl->tone_mono, PV_OUT_GRAIN, volume, tn);
    for (int i = 0; i < 2 * PV_OUT_GRAIN; i++) {
        int v = sp[i] + tn[i];
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    int speech_peak = akn_pcm_peak(sp, 2 * PV_OUT_GRAIN), tone_peak = akn_pcm_peak(tn, 2 * PV_OUT_GRAIN);
    int speech_sound = speech_peak > SOUND_PEAK, tone_sound = tone_peak > SOUND_PEAK;
    /* the tone as it is in the buffer handed to the console: left channel, its frequency, left == right */
    int hz = 0, stereo_ok = 1;
    if (tone_sound) {
        for (int i = 0; i < PV_OUT_GRAIN; i++) {
            pl->left[i] = out[2 * i];
            stereo_ok &= out[2 * i] == out[2 * i + 1];
        }
        hz = akn_pcm_tone_hz(pl->left, PV_OUT_GRAIN, PV_OUT_RATE);
    }
    int called = 0;
    uint64_t t0 = now_us(e);
    int32_t rc = port_submit(e, &pl->port, out, &called);
    double ms = (double)(now_us(e) - t0) / 1000.0;
    if (pl->testing && called) {
        if (pl->t_calls++ > 0) {
            pl->t_blocked_ms += ms < 0 ? 0 : ms;
            pl->t_audio_ms += BLOCK_MS;
        }
        if (rc >= 0 && tone_sound) {
            int pct = tone_peak * 100 / 32767;
            pl->t_blocks++;
            if (stereo_ok && hz >= TONE_HZ - TONE_TOLERANCE && hz <= TONE_HZ + TONE_TOLERANCE) pl->t_verified++;
            if (hz && (!pl->t_hz_min || hz < pl->t_hz_min)) pl->t_hz_min = hz;
            if (hz > pl->t_hz_max) pl->t_hz_max = hz;
            if (pct > pl->t_peak) pl->t_peak = pct;
        }
    }
    pthread_mutex_lock(&e->lock);
    if (in_voice && called) {
        e->st.out_calls++;
        if (rc < 0) {
            e->st.out_errors++;
            e->st.out_last_rc = rc;
        } else {
            e->st.out_nonzero_blocks += speech_sound || tone_sound;
            if (speech_sound) {
                e->st.out_speech_blocks++;
                if (speech_peak * 100 / 32767 > e->st.out_speech_peak) e->st.out_speech_peak = speech_peak * 100 / 32767;
            }
        }
        e->st.out_blocked_ms = pl->port.blocked_ms;
        e->st.out_audio_ms = pl->port.audio_ms;
        e->st.out_self_sleeps = pl->port.self_sleeps;
        e->st.paced = pv_clock_verdict(pl->port.blocked_ms, pl->port.audio_ms);
        e->st.out_call_ms_min = pl->port.min_ms;
        e->st.out_call_ms_avg = pl->port.calls > 1 ? pl->port.blocked_ms / (double)(pl->port.calls - 1) : 0;
        e->st.out_call_ms_max = pl->port.max_ms;
    }
    if (pl->testing) {
        e->st.test_calls = pl->t_calls;
        e->st.test_tone_blocks = pl->t_blocks;
        e->st.test_verified_blocks = pl->t_verified;
        e->st.test_hz_min = pl->t_hz_min;
        e->st.test_hz_max = pl->t_hz_max;
        e->st.test_peak_percent = pl->t_peak;
    }
    pthread_mutex_unlock(&e->lock);
    /* the tone has left the FIFO: two more blocks, then it is over */
    if (pl->testing && !pl->tone->active && ++pl->tail > 2) test_end(e, pl);
    return rc;
}

static void close_after_errors(pv_engine *e, pv_player *pl, int32_t rc) {
    pv_log(e, "voice.output", "%d output calls failed in a row (last 0x%08X); the port is closed, voice stays connected", OUT_ERROR_LIMIT,
           (unsigned)rc);
    e->be->out_close(e->be->user, pl->port.handle);
    pl->port.handle = -1;
    pthread_mutex_lock(&e->lock);
    e->st.out = PV_OUT_FAILED;
    e->st.out_rc = rc;
    pv_note_locked(e, "AKENO Voice: the audio output stopped working (0x%08X)", (unsigned)rc);
    pv_set_failure_locked(e, "audio output failed %d times in a row: 0x%08X", OUT_ERROR_LIMIT, (unsigned)rc);
    pthread_mutex_unlock(&e->lock);
}

static void log_port_closed(pv_engine *e, const pv_port *p, const char *what) {
    int v = pv_clock_verdict(p->blocked_ms, p->audio_ms);
    pv_log(e, "voice.output", "%s output port closed: %lu calls, %.0f of %.0f ms inside the calls, %lu sleeps ahead of real time -> %s", what,
           p->calls,
           p->blocked_ms, p->audio_ms, p->self_sleeps,
           v == 1 ? "paced at playback speed by the port" : v == 0 ? "NOT paced (the port returned at once)" : "undecided");
}

void *pv_play_thread(void *arg) {
    pv_session *s = arg;
    pv_engine *e = s->e;
    pv_player *pl = malloc(sizeof(*pl));
    if (!pl || player_init(pl) != 0) {
        if (pl) player_free(pl);
        free(pl);
        pthread_mutex_lock(&e->lock);
        e->st.out = PV_OUT_FAILED;
        pthread_mutex_unlock(&e->lock);
        s->play_done = 1;
        return NULL;
    }
    pthread_mutex_lock(&e->lock);
    int route = e->st.route;
    e->st.out = PV_OUT_OPENING;
    pthread_mutex_unlock(&e->lock);
    port_open(e, route, 1, &pl->port, pl->silence.pcm);
    while (s->run) {
        pthread_mutex_lock(&e->lock);
        int want = e->st.route, test = e->test_pending;
        if (test && !pl->testing) e->test_pending = 0;
        pthread_mutex_unlock(&e->lock);
        if (want != route) { /* the user chose another speaker route: reopen the port on it */
            if (pl->testing) test_end(e, pl);
            if (pl->port.handle >= 0) {
                port_close(e, &pl->port, pl->silence.pcm);
                log_port_closed(e, &pl->port, "voice");
            }
            route = want;
            pl->errors_in_row = 0;
            port_open(e, route, 1, &pl->port, pl->silence.pcm);
        }
        if (test && !pl->testing) test_begin(e, pl, route, 1);
        int32_t rc = play_block(e, s, pl, 1);
        if (pl->port.handle < 0) continue;
        if (rc < 0) {
            if (++pl->errors_in_row == 1) pv_log(e, "voice.output", "output call failed: 0x%08X", (unsigned)rc);
            if (pl->errors_in_row >= OUT_ERROR_LIMIT) close_after_errors(e, pl, rc);
        } else {
            pl->errors_in_row = 0;
        }
    }
    if (pl->testing) test_end(e, pl);
    if (pl->port.handle >= 0) {
        port_close(e, &pl->port, pl->silence.pcm);
        log_port_closed(e, &pl->port, "voice");
    }
    pthread_mutex_lock(&e->lock);
    if (e->st.out == PV_OUT_ACTIVE || e->st.out == PV_OUT_OPENING) e->st.out = PV_OUT_OFF;
    e->st.rx_level = 0;
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    player_free(pl);
    free(pl);
    s->play_done = 1;
    return NULL;
}

/* ---- The test sound outside voice (test B): N beeps on route N, then the port closes ---------- */

void *pv_test_thread(void *arg) {
    pv_engine *e = arg;
    pthread_mutex_lock(&e->lock);
    int route = e->test_pending;
    e->test_pending = 0;
    pthread_mutex_unlock(&e->lock);
    pv_player *pl = malloc(sizeof(*pl));
    if (pl && player_init(pl) == 0) {
        test_begin(e, pl, route, 0);
        if (port_open(e, route, 0, &pl->port, pl->silence.pcm) < 0) test_end(e, pl); /* nothing to play it on */
        for (int i = 0; i < TONE_BLOCK_LIMIT && pl->testing; i++) play_block(e, NULL, pl, 0);
        if (pl->testing) test_end(e, pl);
        if (pl->port.handle >= 0) {
            port_close(e, &pl->port, pl->silence.pcm);
            log_port_closed(e, &pl->port, "test sound");
        }
    } else {
        pthread_mutex_lock(&e->lock);
        e->st.test_state = PV_TEST_OUTPUT_FAILED;
        pthread_mutex_unlock(&e->lock);
    }
    if (pl) player_free(pl);
    free(pl);
    e->test_done = 1;
    return NULL;
}

/* ---- Capture ---------------------------------------------------------------------------------- */

static void set_mic(pv_engine *e, pv_mic_state state, int32_t rc, const char *detail) {
    pthread_mutex_lock(&e->lock);
    e->st.mic = state;
    e->st.mic_rc = rc;
    if (detail) snprintf(e->st.mic_detail, sizeof(e->st.mic_detail), "%s", detail);
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
}

/* AKENO Native 1.1's configuration first (16 kHz, the one that opened on 12.20 in the foreground);
 * 48 kHz only when 16 kHz was refused for its rate. Any other refusal is not a rate problem, so
 * the other rate is not tried. */
static int32_t mic_open(pv_engine *e, int32_t user, unsigned *rate, char *why, size_t why_len) {
    static const unsigned rates[] = { 16000, 48000 };
    char attempts[160] = "";
    size_t used = 0;
    int32_t rc = PV_NO_LIBRARY;
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        pthread_mutex_lock(&e->lock);
        int allowed = e->st.mic_open_calls < PV_MIC_OPEN_LIMIT;
        if (allowed) e->st.mic_open_calls++;
        pthread_mutex_unlock(&e->lock);
        if (!allowed) break;
        rc = e->be->mic_open(e->be->user, user, rates[i], why, why_len);
        int n = snprintf(attempts + used, sizeof(attempts) - used, "%s%u Hz -> %s0x%08X", used ? "; " : "", rates[i], rc >= 0 ? "handle " : "",
                         (unsigned)rc);
        if (n > 0 && (size_t)n < sizeof(attempts) - used) used += (size_t)n;
        if (rc >= 0) {
            *rate = rates[i];
            break;
        }
        if (pv_refusal_of(rc, 1) != PV_REFUSAL_RATE) break;
    }
    pthread_mutex_lock(&e->lock);
    snprintf(e->st.mic_attempts, sizeof(e->st.mic_attempts), "%s", attempts);
    pthread_mutex_unlock(&e->lock);
    return rc;
}

void *pv_cap_thread(void *arg) {
    pv_session *s = arg;
    pv_engine *e = s->e;
    char why[160] = "";
    unsigned rate = 0;
    pthread_mutex_lock(&e->lock);
    e->st.mic_opens++;
    pthread_mutex_unlock(&e->lock);
    int32_t handle = mic_open(e, s->cap_user, &rate, why, sizeof(why));
    if (handle < 0) {
        pv_refusal_kind kind = pv_refusal_of(handle, 1);
        set_mic(e, kind == PV_REFUSAL_NO_LIBRARY ? PV_MIC_UNAVAILABLE : PV_MIC_FAILED, handle, why);
        pthread_mutex_lock(&e->lock);
        e->st.mic_enabled = 0;
        e->st.mic_refusal = kind;
        e->st.mic_refused_rc = handle;
        e->st.mic_refused_kind = kind;
        if (!pv_refusal_transient(kind)) pv_refusal_remember_locked(e, 0, s->cap_user, handle);
        if (kind == PV_REFUSAL_PROCESS)
            pv_note_locked(e, "AKENO Voice: the PS5 refused the microphone to this background service (0x%08X)", (unsigned)handle);
        else
            pv_note_locked(e, "AKENO Voice: microphone unavailable (0x%08X)", (unsigned)handle);
        pv_set_failure_locked(e, "microphone open failed: 0x%08X, %s (%s)", (unsigned)handle, pv_refusal_name(kind), why);
        char attempts[160];
        snprintf(attempts, sizeof(attempts), "%s", e->st.mic_attempts);
        pthread_mutex_unlock(&e->lock);
        pv_log(e, "voice.mic", "the microphone could not be opened: 0x%08X - %s (%s); tried: %s", (unsigned)handle, pv_refusal_meaning(kind), why,
               attempts);
        s->cap_done = 1;
        return NULL;
    }
    pthread_mutex_lock(&e->lock);
    e->st.mic = PV_MIC_ACTIVE;
    e->st.mic_rc = 0;
    e->st.mic_refusal = PV_REFUSAL_NONE;
    e->st.mic_open_ok++;
    e->st.mic_rate = rate;
    e->st.mic_peak_percent = 0;
    snprintf(e->st.mic_detail, sizeof(e->st.mic_detail), "%s", why);
    pv_note_locked(e, "AKENO Voice: microphone on");
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    pv_log(e, "voice.mic", "microphone open (%s)", why);

    akn_voice_encoder *enc = akn_voice_encoder_new(NULL);
    akn_upsample3 *up = malloc(sizeof(*up));
    akn_frame_acc *acc = malloc(sizeof(*acc));
    int16_t in[PV_MIC_GRAIN], wide[PV_MIC_GRAIN * 3];
    uint8_t opus[AKN_VOICE_MAX_PACKET];
    uint16_t seq = 0;
    int errors = 0, fast = 0, frames_in_second = 0;
    double second_sq = 0;
    int32_t failed_rc = 0;
    if (!enc || !up || !acc) goto done;
    akn_upsample3_init(up);
    akn_frame_acc_init(acc);
    while (s->run && s->cap_run) {
        uint64_t t0 = now_us(e);
        int32_t rc = e->be->mic_read(e->be->user, handle, in);
        pthread_mutex_lock(&e->lock);
        e->st.mic_reads++;
        if (rc < 0) {
            e->st.mic_read_errors++;
            e->st.mic_last_read_rc = rc;
        }
        pthread_mutex_unlock(&e->lock);
        if (rc < 0) {
            errors++;
            if (errors == 1 || errors % 25 == 0) pv_log(e, "voice.mic", "microphone read failed: 0x%08X (%d in a row)", (unsigned)rc, errors);
            if (errors >= MIC_ERROR_LIMIT) {
                failed_rc = rc;
                break;
            }
            pv_sleep_ms(16);
            continue;
        }
        errors = 0;
        /* a read that returns at once every time would spin: pace it to the grain */
        if (now_us(e) - t0 < 2000) {
            if (++fast >= 20) pv_sleep_ms(PV_MIC_GRAIN * 1000 / (rate ? rate : 16000));
        } else {
            fast = 0;
        }
        const int16_t *pcm = in;
        size_t n = PV_MIC_GRAIN;
        if (rate == 16000) {
            akn_upsample3_run(up, in, PV_MIC_GRAIN, wide);
            pcm = wide;
            n = PV_MIC_GRAIN * 3;
        }
        size_t used = 0;
        while (used < n) {
            int ready = 0;
            used += akn_frame_acc_add(acc, pcm + used, n - used, &ready);
            if (!ready) continue;
            int nonzero = 0;
            for (int i = 0; i < AKN_VOICE_FRAME && !nonzero; i++) nonzero = acc->buf[i] != 0;
            pthread_mutex_lock(&e->lock);
            int gain = e->st.mic_gain, transmitting = pv_transmitting_locked(e);
            pthread_mutex_unlock(&e->lock);
            akn_pcm_gain(acc->buf, AKN_VOICE_FRAME, gain);
            double db = akn_pcm_rms_dbfs(acc->buf, AKN_VOICE_FRAME);
            int level = akn_meter_percent(db);
            int peak = akn_pcm_peak(acc->buf, AKN_VOICE_FRAME);
            int hz = akn_pcm_tone_hz(acc->buf, AKN_VOICE_FRAME, AKN_VOICE_RATE);
            for (int i = 0; i < AKN_VOICE_FRAME; i++) second_sq += (double)acc->buf[i] * acc->buf[i];
            pthread_mutex_lock(&s->lock);
            int connected = s->connected;
            pthread_mutex_unlock(&s->lock);
            int len = transmitting && connected ? akn_voice_encode(enc, acc->buf, opus, sizeof(opus)) : 0;
            if (len > 0) {
                pthread_mutex_lock(&s->lock);
                if (s->tx_count == PV_TX_QUEUE) { /* the network is behind: the oldest goes */
                    s->tx_head = (s->tx_head + 1) % PV_TX_QUEUE;
                    s->tx_count--;
                    pthread_mutex_lock(&e->lock);
                    e->st.tx_dropped++;
                    pthread_mutex_unlock(&e->lock);
                }
                pv_tx_packet *p = &s->tx[(s->tx_head + s->tx_count) % PV_TX_QUEUE];
                int packed = akn_voice_pack_audio(seq++, opus, (size_t)len, p->data, sizeof(p->data));
                if (packed > 0) {
                    p->len = (size_t)packed;
                    p->nonzero = nonzero;
                    s->tx_count++;
                }
                pthread_mutex_unlock(&s->lock);
            }
            pthread_mutex_lock(&e->lock);
            e->st.mic_frames++;
            e->st.mic_nonzero_frames += nonzero != 0;
            e->st.mic_level = level;
            if (peak * 100 / 32767 > e->st.mic_peak_percent) e->st.mic_peak_percent = peak * 100 / 32767;
            if (hz) e->st.tx_tone_hz = hz;
            if (len > 0) e->st.encoded++;
            else if (len < 0) e->st.encode_errors++;
            if (++frames_in_second == 50) {
                double rms = second_sq > 0 ? 20.0 * log10(sqrt(second_sq / (50.0 * AKN_VOICE_FRAME)) / 32768.0) : -100.0;
                if (rms < -100.0) rms = -100.0;
                e->st.mic_rms_db = rms;
                if (rms > e->st.mic_rms_db_max || e->st.mic_frames == 50) e->st.mic_rms_db_max = rms;
                frames_in_second = 0;
                second_sq = 0;
            }
            pthread_mutex_unlock(&e->lock);
            akn_frame_acc_init(acc);
        }
    }
done:
    e->be->mic_close(e->be->user, handle);
    pthread_mutex_lock(&e->lock);
    if (failed_rc) {
        e->st.mic = PV_MIC_FAILED;
        e->st.mic_rc = failed_rc;
        e->st.mic_enabled = 0;
        pv_note_locked(e, "AKENO Voice: the microphone stopped working (0x%08X)", (unsigned)failed_rc);
        pv_set_failure_locked(e, "microphone read failed %d times in a row: 0x%08X", MIC_ERROR_LIMIT, (unsigned)failed_rc);
    } else if (e->st.mic == PV_MIC_ACTIVE) {
        e->st.mic = PV_MIC_OFF;
    }
    e->st.mic_level = 0;
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    pv_log(e, "voice.mic", "microphone closed%s", failed_rc ? " after read failures" : "");
    akn_voice_encoder_free(enc);
    free(up);
    free(acc);
    s->cap_done = 1;
    return NULL;
}
