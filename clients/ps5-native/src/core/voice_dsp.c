#include "voice_dsp.h"

#include <math.h>
#include <string.h>

/*
 * Windowed-sinc low-pass for x3 interpolation: cutoff 7.2 kHz at 48 kHz (just under the 8 kHz
 * Nyquist of the 16 kHz input), Blackman window, 48 taps = 16 per phase. Computed once.
 */
static float g_up3[AKN_UP3_TAPS];
static int g_up3_ready;

static void up3_coefficients(void) {
    if (g_up3_ready) return;
    const double pi = 3.14159265358979323846;
    const double fc = 7200.0 / 48000.0;
    const double mid = (AKN_UP3_TAPS - 1) / 2.0;
    double sum = 0;
    for (int i = 0; i < AKN_UP3_TAPS; i++) {
        double x = i - mid;
        double sinc = fabs(x) < 1e-9 ? 2 * fc : sin(2 * pi * fc * x) / (pi * x);
        double w = 0.42 - 0.5 * cos(2 * pi * i / (AKN_UP3_TAPS - 1)) + 0.08 * cos(4 * pi * i / (AKN_UP3_TAPS - 1));
        g_up3[i] = (float)(sinc * w);
        sum += g_up3[i];
    }
    /* unity gain per phase after zero stuffing: the taps sum to 3 */
    for (int i = 0; i < AKN_UP3_TAPS; i++) g_up3[i] = (float)(g_up3[i] * 3.0 / sum);
    g_up3_ready = 1;
}

void akn_upsample3_init(akn_upsample3 *u) {
    memset(u, 0, sizeof(*u));
    up3_coefficients();
}

static int16_t sat16(float v) {
    if (v > 32767.f) return 32767;
    if (v < -32768.f) return -32768;
    return (int16_t)lrintf(v);
}

void akn_upsample3_run(akn_upsample3 *u, const int16_t *in, size_t n, int16_t *out) {
    const int per = AKN_UP3_TAPS / 3;
    for (size_t k = 0; k < n; k++) {
        /* shift the input history (newest first) */
        memmove(&u->hist[1], &u->hist[0], sizeof(float) * (size_t)(per - 1));
        u->hist[0] = in[k];
        for (int phase = 0; phase < 3; phase++) {
            float acc = 0;
            for (int t = 0; t < per; t++) acc += u->hist[t] * g_up3[t * 3 + phase];
            out[k * 3 + (size_t)phase] = sat16(acc);
        }
    }
}

size_t akn_pcm_gain(int16_t *pcm, size_t n, int percent) {
    if (percent < 0) percent = 0;
    if (percent > 200) percent = 200;
    size_t clipped = 0;
    if (percent == 100) return 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = (int32_t)pcm[i] * percent / 100;
        if (v > 32767) v = 32767, clipped++;
        else if (v < -32768) v = -32768, clipped++;
        pcm[i] = (int16_t)v;
    }
    return clipped;
}

double akn_pcm_rms_dbfs(const int16_t *pcm, size_t n) {
    if (!n) return -100.0;
    double acc = 0;
    for (size_t i = 0; i < n; i++) acc += (double)pcm[i] * pcm[i];
    double rms = sqrt(acc / (double)n) / 32768.0;
    if (rms < 1e-5) return -100.0;
    return 20.0 * log10(rms);
}

int akn_pcm_peak(const int16_t *pcm, size_t n) {
    int peak = 0;
    for (size_t i = 0; i < n; i++) {
        int v = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
        if (v > peak) peak = v;
    }
    return peak > 32767 ? 32767 : peak;
}

int akn_meter_percent(double dbfs) {
    if (dbfs <= -60.0) return 0;
    if (dbfs >= 0.0) return 100;
    return (int)lrint((dbfs + 60.0) * 100.0 / 60.0);
}

void akn_pcm_mono_to_stereo(const int16_t *mono, size_t n, int percent, int16_t *stereo) {
    if (percent < 0) percent = 0;
    if (percent > 200) percent = 200;
    for (size_t i = 0; i < n; i++) {
        int32_t v = (int32_t)mono[i] * percent / 100;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        stereo[2 * i] = stereo[2 * i + 1] = (int16_t)v;
    }
}

void akn_frame_acc_init(akn_frame_acc *a) { a->fill = 0; }

size_t akn_frame_acc_add(akn_frame_acc *a, const int16_t *in, size_t n, int *ready) {
    size_t room = AKN_VOICE_FRAME - a->fill;
    size_t take = n < room ? n : room;
    memcpy(a->buf + a->fill, in, take * sizeof(int16_t));
    a->fill += take;
    *ready = a->fill == AKN_VOICE_FRAME;
    return take;
}

int akn_pcm_tone_hz(const int16_t *pcm, size_t n, int rate) {
    if (n < 64 || akn_pcm_rms_dbfs(pcm, n) < -50.0) return 0;
    /* crossings of a small hysteresis band, so noise near zero does not count */
    const int band = 200;
    int state = pcm[0] > band ? 1 : pcm[0] < -band ? -1 : 0;
    size_t first = 0, last = 0, crossings = 0;
    for (size_t i = 1; i < n; i++) {
        int s = pcm[i] > band ? 1 : pcm[i] < -band ? -1 : 0;
        if (s == 0 || s == state) continue;
        if (state != 0) {
            if (!crossings) first = i;
            last = i;
            crossings++;
        }
        state = s;
    }
    if (crossings < 3 || last <= first) return 0;
    double period_samples = 2.0 * (double)(last - first) / (double)(crossings - 1);
    return (int)lrint((double)rate / period_samples);
}

void akn_pcm_fifo_init(akn_pcm_fifo *f) { f->head = f->count = 0; }

size_t akn_pcm_fifo_write(akn_pcm_fifo *f, const int16_t *in, size_t n) {
    size_t w = 0;
    while (w < n && f->count < AKN_PCM_FIFO) {
        f->data[(f->head + f->count) % AKN_PCM_FIFO] = in[w++];
        f->count++;
    }
    return w;
}

size_t akn_pcm_fifo_read(akn_pcm_fifo *f, int16_t *out, size_t n) {
    size_t r = 0;
    while (r < n && f->count) {
        out[r++] = f->data[f->head];
        f->head = (f->head + 1) % AKN_PCM_FIFO;
        f->count--;
    }
    return r;
}
