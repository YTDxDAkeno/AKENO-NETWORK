/*
 * Small, allocation-free audio helpers for party voice: microphone sample-rate conversion to the
 * codec's 48 kHz, gain with a hard limit, levels for meters and speaking indicators, mono to the
 * output port's stereo, a frame accumulator and a single-tone frequency estimate (diagnostics and
 * the PC end-to-end test).
 */
#ifndef AKN_VOICE_DSP_H
#define AKN_VOICE_DSP_H

#include <stddef.h>
#include <stdint.h>

#include "voice_codec.h"

/* 16 kHz -> 48 kHz (x3) polyphase low-pass interpolator; keeps its history across calls. */
#define AKN_UP3_TAPS 48
typedef struct {
    float hist[AKN_UP3_TAPS / 3];
} akn_upsample3;

void akn_upsample3_init(akn_upsample3 *u);
/* in: n samples at 16 kHz; out: 3n samples at 48 kHz. */
void akn_upsample3_run(akn_upsample3 *u, const int16_t *in, size_t n, int16_t *out);

/* Gain in percent (0..200) applied with saturation; returns the number of clipped samples. */
size_t akn_pcm_gain(int16_t *pcm, size_t n, int percent);
/* RMS level in dBFS (-100 for silence) and peak (0..32767). */
double akn_pcm_rms_dbfs(const int16_t *pcm, size_t n);
int akn_pcm_peak(const int16_t *pcm, size_t n);
/* 0..100 meter value for a dBFS level (-60 dBFS .. 0 dBFS). */
int akn_meter_percent(double dbfs);
/* Mono to interleaved stereo with gain percent (0..200). */
void akn_pcm_mono_to_stereo(const int16_t *mono, size_t n, int percent, int16_t *stereo);

/* Collects arbitrary-size blocks into codec frames of AKN_VOICE_FRAME samples. */
typedef struct {
    int16_t buf[AKN_VOICE_FRAME];
    size_t fill;
} akn_frame_acc;

void akn_frame_acc_init(akn_frame_acc *a);
/*
 * Adds up to n samples; returns how many were consumed. When a frame is complete (*ready = 1)
 * the caller takes a->buf and calls akn_frame_acc_init / continues with the rest.
 */
size_t akn_frame_acc_add(akn_frame_acc *a, const int16_t *in, size_t n, int *ready);

/* Single-tone frequency estimate (zero crossings), 0 when the block is too quiet. */
int akn_pcm_tone_hz(const int16_t *pcm, size_t n, int rate);

/* A fixed-size FIFO of mono samples (decoded audio waiting for the output port). */
#define AKN_PCM_FIFO 9600   /* 200 ms */
typedef struct {
    int16_t data[AKN_PCM_FIFO];
    size_t head, count;
} akn_pcm_fifo;

void akn_pcm_fifo_init(akn_pcm_fifo *f);
size_t akn_pcm_fifo_write(akn_pcm_fifo *f, const int16_t *in, size_t n);   /* returns written */
size_t akn_pcm_fifo_read(akn_pcm_fifo *f, int16_t *out, size_t n);         /* returns read */

#endif
