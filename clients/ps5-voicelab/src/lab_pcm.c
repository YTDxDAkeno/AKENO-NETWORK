#include "lab_pcm.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ACTIVE_DB (-45.0)

void akn_lab_mic_stats_init(akn_lab_mic_stats *s, unsigned rate) {
    memset(s, 0, sizeof(*s));
    s->rate = rate ? rate : 16000;
    s->window_len = s->rate / 50;
    s->min = 32767;
    s->max = -32768;
    s->window_db_max = -120.0;
}

static double db_of(double mean_sq) {
    if (mean_sq <= 0.0) return -120.0;
    double db = 10.0 * log10(mean_sq / (32768.0 * 32768.0));
    return db < -120.0 ? -120.0 : db;
}

void akn_lab_mic_stats_add(akn_lab_mic_stats *s, const int16_t *samples, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int v = samples[i];
        s->samples++;
        s->sum += v;
        s->sum_sq += (double)v * v;
        if (v != 0) {
            s->nonzero++;
            s->zero_run = 0;
        } else if (++s->zero_run > s->zero_run_max) {
            s->zero_run_max = s->zero_run;
        }
        int a = v < 0 ? -v : v;
        if (a > s->peak) s->peak = a;
        if (v < s->min) s->min = v;
        if (v > s->max) s->max = v;
        s->window_sq += (double)v * v;
        if (++s->window_fill == s->window_len) {
            double db = db_of(s->window_sq / s->window_len);
            s->windows++;
            if (db > ACTIVE_DB) s->windows_active++;
            if (db > s->window_db_max) s->window_db_max = db;
            s->window_fill = 0;
            s->window_sq = 0.0;
        }
    }
}

double akn_lab_mic_rms_db(const akn_lab_mic_stats *s) {
    return s->samples ? db_of(s->sum_sq / (double)s->samples) : -120.0;
}

akn_lab_pcm_verdict akn_lab_mic_verdict(const akn_lab_mic_stats *s) {
    if (s->samples == 0) return AKN_LAB_PCM_NONE;
    if (s->nonzero == 0) return AKN_LAB_PCM_ALL_ZERO;
    if (s->min == s->max) return AKN_LAB_PCM_CONSTANT;
    if (s->windows_active == 0) return AKN_LAB_PCM_NOISE_ONLY;
    return AKN_LAB_PCM_SOUND;
}

const char *akn_lab_pcm_verdict_name(akn_lab_pcm_verdict v) {
    switch (v) {
    case AKN_LAB_PCM_NONE: return "NO_SAMPLES";
    case AKN_LAB_PCM_ALL_ZERO: return "ALL_ZERO";
    case AKN_LAB_PCM_CONSTANT: return "CONSTANT";
    case AKN_LAB_PCM_NOISE_ONLY: return "QUIET_SIGNAL";
    case AKN_LAB_PCM_SOUND: return "SOUND";
    }
    return "?";
}

void akn_lab_beeps_init(akn_lab_beeps *b, unsigned rate, unsigned beeps, double amplitude) {
    memset(b, 0, sizeof(*b));
    b->rate = rate;
    b->beeps = beeps;
    b->amplitude = amplitude < 0 ? 0 : amplitude > 0.5 ? 0.5 : amplitude;
    unsigned long slot = (unsigned long)rate * 300 / 1000; /* 150 ms beep + 150 ms pause */
    b->total = slot * beeps + (unsigned long)rate / 4;   /* + 250 ms of silence */
}

unsigned akn_lab_beeps_fill(akn_lab_beeps *b, int16_t *stereo, unsigned frames) {
    const double two_pi = 6.283185307179586;
    unsigned long slot = (unsigned long)b->rate * 300 / 1000, on = slot / 2, ramp = b->rate / 100;
    unsigned audible = 0;
    for (unsigned i = 0; i < frames; i++, b->frame++) {
        double v = 0.0;
        if (b->frame < slot * b->beeps) {
            unsigned long pos = b->frame % slot;
            if (pos < on) {
                double env = 1.0;
                if (pos < ramp) env = (double)pos / ramp;
                else if (on - pos < ramp) env = (double)(on - pos) / ramp;
                v = b->amplitude * env * sin(two_pi * 880.0 * (double)pos / b->rate);
                audible++;
            }
        }
        int16_t s = (int16_t)lrint(v * 32767.0);
        stereo[2 * i] = s;
        stereo[2 * i + 1] = s;
    }
    return audible;
}

int akn_lab_beeps_done(const akn_lab_beeps *b) { return b->frame >= b->total; }

int akn_lab_ring_init(akn_lab_ring *r, size_t cap) {
    memset(r, 0, sizeof(*r));
    r->data = calloc(cap, sizeof(int16_t));
    if (!r->data) return -1;
    r->cap = cap;
    return 0;
}

void akn_lab_ring_free(akn_lab_ring *r) {
    free(r->data);
    memset(r, 0, sizeof(*r));
}

void akn_lab_ring_push(akn_lab_ring *r, const int16_t *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (r->count == r->cap) {
            r->head = (r->head + 1) % r->cap; /* drop the oldest */
            r->count--;
            r->dropped++;
        }
        r->data[(r->head + r->count) % r->cap] = s[i];
        r->count++;
    }
}

void akn_lab_ring_pop(akn_lab_ring *r, int16_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (r->count == 0) {
            out[i] = 0;
            r->underrun++;
            continue;
        }
        out[i] = r->data[r->head];
        r->head = (r->head + 1) % r->cap;
        r->count--;
    }
}

void akn_lab_upsample3_stereo(const int16_t *in, size_t n, int16_t *out, double gain) {
    for (size_t i = 0; i < n; i++) {
        double v = in[i] * gain;
        int16_t s = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        for (int k = 0; k < 3; k++) {
            out[(i * 3 + k) * 2] = s;
            out[(i * 3 + k) * 2 + 1] = s;
        }
    }
}
