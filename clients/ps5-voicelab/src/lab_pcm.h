/*
 * AKENO Voice Lab: PCM measurements and test signals (portable, unit-tested). Nothing is stored:
 * microphone blocks are reduced to these numbers (and, in the echo test, played back once).
 */
#ifndef AKN_LAB_PCM_H
#define AKN_LAB_PCM_H

#include <stddef.h>
#include <stdint.h>

/* Statistics of captured S16 mono audio, accumulated block by block. */
typedef struct {
    unsigned rate;
    unsigned long long samples;
    unsigned long long nonzero;      /* samples != 0 */
    unsigned long long zero_run_max; /* longest run of exact zeros */
    unsigned long long zero_run;
    double sum_sq;
    double sum;
    int peak;                        /* 0..32768 */
    int min, max;
    /* 20 ms windows */
    unsigned window_len, window_fill;
    double window_sq;
    unsigned long windows, windows_active; /* active: RMS above -45 dBFS */
    double window_db_max;
} akn_lab_mic_stats;

void akn_lab_mic_stats_init(akn_lab_mic_stats *s, unsigned rate);
void akn_lab_mic_stats_add(akn_lab_mic_stats *s, const int16_t *samples, size_t n);

typedef enum {
    AKN_LAB_PCM_NONE = 0,       /* nothing was read */
    AKN_LAB_PCM_ALL_ZERO,       /* the port delivered only exact zeros (muted or not ours) */
    AKN_LAB_PCM_CONSTANT,       /* nonzero but unchanging (a stuck value, not sound) */
    AKN_LAB_PCM_NOISE_ONLY,     /* varying signal, but never above the speech threshold */
    AKN_LAB_PCM_SOUND           /* varying signal with windows above -45 dBFS (sound, e.g. speech) */
} akn_lab_pcm_verdict;

akn_lab_pcm_verdict akn_lab_mic_verdict(const akn_lab_mic_stats *s);
const char *akn_lab_pcm_verdict_name(akn_lab_pcm_verdict v);
double akn_lab_mic_rms_db(const akn_lab_mic_stats *s); /* dBFS; -120 for silence */

/* "Beeps": count short 880 Hz beeps of 150 ms with 150 ms pauses, S16 stereo, 10 ms fades. */
typedef struct {
    unsigned rate;
    unsigned beeps;
    unsigned long frame, total;
    double amplitude;
} akn_lab_beeps;

void akn_lab_beeps_init(akn_lab_beeps *b, unsigned rate, unsigned beeps, double amplitude);
/* Fills frames of S16 stereo; returns the number of non-silent frames written. */
unsigned akn_lab_beeps_fill(akn_lab_beeps *b, int16_t *stereo, unsigned frames);
int akn_lab_beeps_done(const akn_lab_beeps *b);

/* A bounded ring of S16 mono samples (echo test: capture thread -> playback thread). */
typedef struct {
    int16_t *data;
    size_t cap, head, count;
    unsigned long long dropped, underrun;
} akn_lab_ring;

int akn_lab_ring_init(akn_lab_ring *r, size_t cap);
void akn_lab_ring_free(akn_lab_ring *r);
void akn_lab_ring_push(akn_lab_ring *r, const int16_t *s, size_t n);
/* Pops n samples (zeros for what is missing; counted as underrun). */
void akn_lab_ring_pop(akn_lab_ring *r, int16_t *out, size_t n);

/* 16 kHz mono -> 48 kHz stereo by repeating each sample three times, with a gain (0..1). */
void akn_lab_upsample3_stereo(const int16_t *in, size_t n, int16_t *out, double gain);

#endif
