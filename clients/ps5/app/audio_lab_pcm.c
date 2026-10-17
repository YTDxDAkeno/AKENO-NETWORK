/* Audio lab test tone PCM; see audio_lab_pcm.h. */
#include "audio_lab_pcm.h"

#include <stdint.h>
#include <string.h>

unsigned audio_lab_format_channels(int format) {
    switch (format) {
    case AUDIO_LAB_FORMAT_S16_MONO:
    case AUDIO_LAB_FORMAT_FLOAT_MONO: return 1;
    case AUDIO_LAB_FORMAT_S16_STEREO:
    case AUDIO_LAB_FORMAT_FLOAT_STEREO: return 2;
    default: return 0;
    }
}

unsigned audio_lab_format_frame_bytes(int format) {
    unsigned channels = audio_lab_format_channels(format);
    int is_float = format == AUDIO_LAB_FORMAT_FLOAT_MONO || format == AUDIO_LAB_FORMAT_FLOAT_STEREO;
    return channels * (is_float ? 4u : 2u);
}

const char *audio_lab_format_name(int format) {
    switch (format) {
    case AUDIO_LAB_FORMAT_S16_MONO: return "S16 mono";
    case AUDIO_LAB_FORMAT_S16_STEREO: return "S16 stereo";
    case AUDIO_LAB_FORMAT_FLOAT_MONO: return "float mono";
    case AUDIO_LAB_FORMAT_FLOAT_STEREO: return "float stereo";
    default: return "unknown";
    }
}

int audio_lab_grain_valid(unsigned grain) {
    return grain >= AUDIO_LAB_GRAIN_MIN && grain <= AUDIO_LAB_GRAIN_MAX && grain % 256u == 0;
}

void audio_lab_osc_init(audio_lab_osc *osc, unsigned frequency_hz, double amplitude, unsigned long lead_frames, unsigned long tone_frames) {
    memset(osc, 0, sizeof(*osc));
    if (amplitude < 0.0) amplitude = 0.0;
    if (amplitude > 0.5) amplitude = 0.5; /* Never louder than -6 dBFS, whatever the caller asks. */
    if (frequency_hz < 50) frequency_hz = 50;
    if (frequency_hz > 4000) frequency_hz = 4000;
    /*
     * Sine by recurrence, s[n] = 2cos(w)s[n-1] - s[n-2], with cos(w) and sin(w) from short Taylor
     * series (w <= 0.53 rad here): no libm import, which a console has not been shown to resolve.
     */
    const double w = 2.0 * 3.14159265358979323846 * (double)frequency_hz / (double)AUDIO_LAB_RATE, w2 = w * w;
    osc->cos_w = 1.0 - w2 / 2.0 + w2 * w2 / 24.0 - w2 * w2 * w2 / 720.0 + w2 * w2 * w2 * w2 / 40320.0;
    const double sin_w = w - w * w2 / 6.0 + w * w2 * w2 / 120.0 - w * w2 * w2 * w2 / 5040.0;
    osc->s_prev = -sin_w; /* s[-1] = sin(-w), s[0] = sin(0) */
    osc->s_cur = 0.0;
    osc->amplitude = amplitude;
    osc->lead_frames = lead_frames;
    osc->tone_frames = tone_frames;
    osc->ramp_frames = AUDIO_LAB_RATE / 100u;
    if (osc->ramp_frames * 2 > tone_frames) osc->ramp_frames = tone_frames / 2;
}

/* Next sample of the tone (advances the oscillator only while the tone plays). */
static double next_sample(audio_lab_osc *osc) {
    unsigned long n = osc->frame++;
    if (n < osc->lead_frames || n >= osc->lead_frames + osc->tone_frames) return 0.0;
    unsigned long t = n - osc->lead_frames, left = osc->tone_frames - t;
    double gain = osc->amplitude;
    if (osc->ramp_frames && t < osc->ramp_frames) gain *= (double)t / (double)osc->ramp_frames;
    else if (osc->ramp_frames && left < osc->ramp_frames) gain *= (double)left / (double)osc->ramp_frames;
    double value = gain * osc->s_cur;
    double s_next = 2.0 * osc->cos_w * osc->s_cur - osc->s_prev;
    osc->s_prev = osc->s_cur;
    osc->s_cur = s_next;
    return value;
}

void audio_lab_fill(audio_lab_osc *osc, int format, void *out, unsigned frames, audio_lab_block_stats *stats) {
    unsigned channels = audio_lab_format_channels(format);
    int is_float = format == AUDIO_LAB_FORMAT_FLOAT_MONO || format == AUDIO_LAB_FORMAT_FLOAT_STEREO;
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!channels) return;
    int16_t *s16 = out;
    float *f32 = out;
    for (unsigned i = 0; i < frames; i++) {
        double v = next_sample(osc);
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        int16_t q = (int16_t)(32767.0 * v);
        for (unsigned c = 0; c < channels; c++) {
            if (is_float) f32[i * channels + c] = (float)v;
            else s16[i * channels + c] = q;
        }
        double a = v < 0 ? -v : v;
        if (stats) {
            if (a > stats->peak) stats->peak = a;
            if (is_float ? v != 0.0 : q != 0) stats->nonzero++;
        }
    }
}
