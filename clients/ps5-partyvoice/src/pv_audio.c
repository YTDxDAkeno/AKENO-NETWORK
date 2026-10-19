/*
 * AKENO Party Voice: the playback, capture and test-sound threads. Each audio boundary is counted
 * (pv_engine.h); nothing is recorded or stored: microphone samples go to the encoder and the level
 * meter only, decoded samples to the output port and the level meter only.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lab_pcm.h"
#include "pv_internal.h"

#define BLOCK_US ((uint64_t)PV_OUT_GRAIN * 1000000u / PV_OUT_RATE) /* 21.333 ms */
#define FAST_CALL_US 2000      /* an output call that returns faster did not wait for an audio clock */
#define OUT_ERROR_LIMIT 50     /* failed output calls in a row before the port is given up */
#define MIC_ERROR_LIMIT 50
#define SOUND_PEAK 64          /* -54 dBFS: a frame with any sound in it, not just rounding noise */
#define RX_WINDOW 4800         /* 100 ms of what is played, for the level and the tone estimate */
#define TEST_AMPLITUDE 0.10    /* -20 dBFS peak, the audio labs' moderate level */
#define NO_LIBRARY ((int32_t)0x80000002)

static uint64_t now_us(const pv_engine *e) { return e->be->now_us(e->be->user); }

/* Waits until *next (one block later than the last), unless far behind (then starts over). */
static void pace_by_clock(const pv_engine *e, uint64_t *next) {
    uint64_t now = now_us(e);
    *next += BLOCK_US;
    if (*next > now) pv_sleep_ms((unsigned)((*next - now + 999) / 1000));
    else if (now - *next > 200000) *next = now;
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

/* Opens the output port of a route for the session; the state says what happened. */
static int32_t open_output(pv_engine *e, int route) {
    char why[160] = "";
    int32_t rc = ensure_out_init(e);
    int32_t handle = rc == 0 ? e->be->out_open(e->be->user, route, why, sizeof(why)) : rc;
    pthread_mutex_lock(&e->lock);
    e->st.out_opens++;
    e->st.out_rc = handle < 0 ? handle : 0;
    snprintf(e->st.out_detail, sizeof(e->st.out_detail), "%s", rc != 0 ? "sceAudioOutInit failed" : why);
    e->st.out = handle >= 0 ? PV_OUT_ACTIVE : PV_OUT_FAILED;
    e->st.paced = -1;
    if (handle < 0) {
        pv_note_locked(e, "AKENO Voice: the console's audio output could not be opened (0x%08X)", (unsigned)handle);
        pv_set_failure_locked(e, "audio output open failed on route %d: 0x%08X", route, (unsigned)handle);
    }
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    if (handle < 0) pv_log(e, "voice.output", "route %d: the output port could not be opened: 0x%08X (%s)", route, (unsigned)handle, why);
    else pv_log(e, "voice.output", "route %d: output port open (%s)", route, why);
    return handle;
}

typedef struct {
    unsigned long calls;
    double sum_ms, min_ms, max_ms;
} call_timing;

static void timing_add(call_timing *t, double ms) {
    if (!t->calls || ms < t->min_ms) t->min_ms = ms;
    if (ms > t->max_ms) t->max_ms = ms;
    t->sum_ms += ms;
    t->calls++;
}

/* Calls 2..n of a port that an audio clock consumes block about one block's duration each. */
static int timing_paced(const call_timing *t) {
    if (t->calls < 10) return -1;
    double block_ms = (double)PV_OUT_GRAIN * 1000.0 / PV_OUT_RATE, avg = t->sum_ms / (double)t->calls;
    return avg > block_ms * 0.6 && avg < block_ms * 1.6;
}

void *pv_play_thread(void *arg) {
    pv_session *s = arg;
    pv_engine *e = s->e;
    pthread_mutex_lock(&e->lock);
    int route = e->st.route;
    e->st.out = PV_OUT_OPENING;
    pthread_mutex_unlock(&e->lock);
    int32_t handle = open_output(e, route);
    akn_voice_decoder *dec = akn_voice_decoder_new(NULL);
    akn_pcm_fifo *fifo = malloc(sizeof(*fifo));
    int16_t *mono = malloc(sizeof(int16_t) * PV_OUT_GRAIN), *stereo = malloc(sizeof(int16_t) * 2 * PV_OUT_GRAIN);
    int16_t *beep = malloc(sizeof(int16_t) * 2 * PV_OUT_GRAIN);
    int16_t *window = malloc(sizeof(int16_t) * RX_WINDOW);
    akn_lab_beeps beeps;
    int beeping = 0;
    int16_t frame[AKN_VOICE_FRAME];
    uint8_t packet[AKN_VOICE_MAX_PACKET];
    size_t win_fill = 0;
    call_timing timing;
    memset(&timing, 0, sizeof(timing));
    int errors_in_row = 0;
    uint64_t next = now_us(e);
    if (!dec || !fifo || !mono || !stereo || !beep || !window) goto done;
    akn_pcm_fifo_init(fifo);
    while (s->run) {
        /* the user chose another speaker route: reopen the port on it */
        pthread_mutex_lock(&e->lock);
        int want = e->st.route;
        pthread_mutex_unlock(&e->lock);
        if (want != route) {
            if (handle >= 0) e->be->out_close(e->be->user, handle);
            route = want;
            memset(&timing, 0, sizeof(timing));
            errors_in_row = 0;
            handle = open_output(e, route);
        }
        /* decoded voice: the jitter buffer's next frames, concealment for a lost one, else silence */
        unsigned long decoded = 0, concealed = 0, silent = 0, with_sound = 0;
        while (fifo->count < PV_OUT_GRAIN) {
            size_t len = 0;
            pthread_mutex_lock(&s->lock);
            akn_jitter_result r = akn_jitter_get(&s->jitter, packet, &len);
            unsigned long under = s->jitter.underruns, late = s->jitter.late, over = s->jitter.dropped_overflow;
            pthread_mutex_unlock(&s->lock);
            if (r == AKN_JITTER_PACKET) {
                akn_voice_decode(dec, packet, len, frame);
                decoded++;
                with_sound += akn_pcm_peak(frame, AKN_VOICE_FRAME) > SOUND_PEAK;
            } else if (r == AKN_JITTER_LOST) {
                akn_voice_decode(dec, NULL, 0, frame);
                concealed++;
            } else {
                memset(frame, 0, sizeof(frame));
                silent++;
            }
            akn_pcm_fifo_write(fifo, frame, AKN_VOICE_FRAME);
            pthread_mutex_lock(&e->lock);
            e->st.underruns = under;
            e->st.late = late;
            e->st.overflow = over;
            pthread_mutex_unlock(&e->lock);
        }
        akn_pcm_fifo_read(fifo, mono, PV_OUT_GRAIN);
        pthread_mutex_lock(&e->lock);
        int volume = e->st.deafened ? 0 : e->st.out_volume;
        e->st.decoded += decoded;
        e->st.concealed += concealed;
        e->st.silent_frames += silent;
        e->st.decoded_nonzero += with_sound;
        e->st.queued_frames += decoded + concealed;
        if (e->test_pending && !beeping) {
            akn_lab_beeps_init(&beeps, PV_OUT_RATE, (unsigned)e->test_pending, TEST_AMPLITUDE);
            beeping = 1;
            e->test_pending = 0;
        }
        pthread_mutex_unlock(&e->lock);
        /* what is played: level and tone over 100 ms windows */
        for (int i = 0; i < PV_OUT_GRAIN; i++) {
            window[win_fill++] = volume ? mono[i] : 0;
            if (win_fill == RX_WINDOW) {
                int level = akn_meter_percent(akn_pcm_rms_dbfs(window, win_fill));
                int hz = akn_pcm_tone_hz(window, win_fill, AKN_VOICE_RATE);
                pthread_mutex_lock(&e->lock);
                e->st.rx_level = level;
                if (hz) e->st.rx_tone_hz = hz;
                pthread_mutex_unlock(&e->lock);
                win_fill = 0;
            }
        }
        akn_pcm_mono_to_stereo(mono, PV_OUT_GRAIN, volume, stereo);
        if (beeping) {
            akn_lab_beeps_fill(&beeps, beep, PV_OUT_GRAIN);
            for (int i = 0; i < 2 * PV_OUT_GRAIN; i++) {
                int v = stereo[i] + beep[i];
                stereo[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            if (akn_lab_beeps_done(&beeps)) beeping = 0;
        }
        int block_sound = akn_pcm_peak(stereo, 2 * PV_OUT_GRAIN) > SOUND_PEAK;
        if (handle < 0) {
            pace_by_clock(e, &next); /* no port: incoming audio is still decoded in real time (counted) */
            continue;
        }
        uint64_t t0 = now_us(e);
        int32_t rc = e->be->out_output(e->be->user, handle, stereo);
        uint64_t dt = now_us(e) - t0;
        timing_add(&timing, (double)dt / 1000.0);
        pthread_mutex_lock(&e->lock);
        e->st.out_calls++;
        if (rc < 0) {
            e->st.out_errors++;
            e->st.out_last_rc = rc;
        } else if (block_sound) {
            e->st.out_nonzero_blocks++;
        }
        e->st.paced = timing_paced(&timing);
        e->st.out_call_ms_min = timing.min_ms;
        e->st.out_call_ms_avg = timing.calls ? timing.sum_ms / (double)timing.calls : 0;
        e->st.out_call_ms_max = timing.max_ms;
        pthread_mutex_unlock(&e->lock);
        if (rc < 0) {
            if (++errors_in_row == 1) pv_log(e, "voice.output", "output call failed: 0x%08X", (unsigned)rc);
            if (errors_in_row >= OUT_ERROR_LIMIT) {
                pv_log(e, "voice.output", "%d output calls failed in a row (last 0x%08X); the port is closed, voice stays connected",
                       OUT_ERROR_LIMIT, (unsigned)rc);
                e->be->out_close(e->be->user, handle);
                handle = -1;
                pthread_mutex_lock(&e->lock);
                e->st.out = PV_OUT_FAILED;
                e->st.out_rc = rc;
                pv_note_locked(e, "AKENO Voice: the audio output stopped working (0x%08X)", (unsigned)rc);
                pv_set_failure_locked(e, "audio output failed %d times in a row: 0x%08X", OUT_ERROR_LIMIT, (unsigned)rc);
                pthread_mutex_unlock(&e->lock);
            }
        } else {
            errors_in_row = 0;
        }
        /* a port that returns at once is not paced by an audio clock: keep real time ourselves */
        if (dt < FAST_CALL_US || rc < 0) pace_by_clock(e, &next);
        else next = now_us(e);
    }
done:
    if (handle >= 0) e->be->out_close(e->be->user, handle);
    pthread_mutex_lock(&e->lock);
    if (e->st.out == PV_OUT_ACTIVE || e->st.out == PV_OUT_OPENING) e->st.out = PV_OUT_OFF;
    e->st.rx_level = 0;
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    if (handle >= 0) pv_log(e, "voice.output", "output port closed (%lu calls, %s)", timing.calls,
                            timing_paced(&timing) == 1 ? "paced at playback speed" : timing_paced(&timing) == 0 ? "NOT paced" : "too few calls to judge");
    akn_voice_decoder_free(dec);
    free(fifo);
    free(mono);
    free(stereo);
    free(beep);
    free(window);
    s->play_done = 1;
    return NULL;
}

/* ---- Capture --------------------------------------------------------------------------------- */

static void set_mic(pv_engine *e, pv_mic_state state, int32_t rc, const char *detail) {
    pthread_mutex_lock(&e->lock);
    e->st.mic = state;
    e->st.mic_rc = rc;
    if (detail) snprintf(e->st.mic_detail, sizeof(e->st.mic_detail), "%s", detail);
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
}

void *pv_cap_thread(void *arg) {
    pv_session *s = arg;
    pv_engine *e = s->e;
    char why[160] = "";
    unsigned rate = 0;
    pthread_mutex_lock(&e->lock);
    e->st.mic_opens++;
    pthread_mutex_unlock(&e->lock);
    int32_t handle = e->be->mic_open(e->be->user, &rate, why, sizeof(why));
    if (handle < 0) {
        pv_mic_state state = handle == NO_LIBRARY ? PV_MIC_UNAVAILABLE : PV_MIC_FAILED;
        set_mic(e, state, handle, why);
        pthread_mutex_lock(&e->lock);
        e->st.mic_enabled = 0;
        pv_note_locked(e, "AKENO Voice: microphone unavailable (0x%08X) - you can still listen", (unsigned)handle);
        pv_set_failure_locked(e, "microphone open failed: 0x%08X (%s)", (unsigned)handle, why);
        pthread_mutex_unlock(&e->lock);
        pv_log(e, "voice.mic", "the microphone could not be opened: 0x%08X (%s)", (unsigned)handle, why);
        s->cap_done = 1;
        return NULL;
    }
    pthread_mutex_lock(&e->lock);
    e->st.mic = PV_MIC_ACTIVE;
    e->st.mic_rc = 0;
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

/* ---- The test sound outside voice: N beeps on route N, then the port closes ------------------- */

void *pv_test_thread(void *arg) {
    pv_engine *e = arg;
    pthread_mutex_lock(&e->lock);
    int route = e->test_pending;
    e->test_pending = 0;
    pthread_mutex_unlock(&e->lock);
    char why[160] = "";
    int32_t rc = ensure_out_init(e);
    int32_t handle = rc == 0 ? e->be->out_open(e->be->user, route, why, sizeof(why)) : rc;
    call_timing timing;
    memset(&timing, 0, sizeof(timing));
    static int16_t block[2 * PV_OUT_GRAIN];
    unsigned long ok = 0;
    if (handle >= 0) {
        akn_lab_beeps b;
        akn_lab_beeps_init(&b, PV_OUT_RATE, (unsigned)route, TEST_AMPLITUDE);
        uint64_t next = now_us(e);
        for (int i = 0; i < 400 && !akn_lab_beeps_done(&b); i++) {
            akn_lab_beeps_fill(&b, block, PV_OUT_GRAIN);
            uint64_t t0 = now_us(e);
            int32_t out = e->be->out_output(e->be->user, handle, block);
            uint64_t dt = now_us(e) - t0;
            timing_add(&timing, (double)dt / 1000.0);
            ok += out >= 0;
            if (dt < FAST_CALL_US) pace_by_clock(e, &next);
            else next = now_us(e);
        }
        memset(block, 0, sizeof(block));
        for (int i = 0; i < 4; i++) e->be->out_output(e->be->user, handle, block); /* a short silence tail */
        e->be->out_close(e->be->user, handle);
    }
    pthread_mutex_lock(&e->lock);
    e->st.test_route = route;
    e->st.test_open_rc = handle < 0 ? handle : 0;
    e->st.test_calls = ok;
    e->st.test_paced = timing_paced(&timing);
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    pv_log(e, "voice.test_sound", "route %d: %s; %lu blocks accepted, %s (audible only if you heard %d beep%s)", route,
           handle < 0 ? "the port could not be opened" : "played", ok,
           timing_paced(&timing) == 1 ? "paced" : timing_paced(&timing) == 0 ? "NOT paced" : "timing unknown", route, route == 1 ? "" : "s");
    e->test_done = 1;
    return NULL;
}
