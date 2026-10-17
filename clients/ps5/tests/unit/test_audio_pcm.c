/* Audio lab test tone PCM (app/audio_lab_pcm.c): formats, silence, level, frequency, channels. */
#include <stdint.h>
#include <stdlib.h>

#include "audio_lab_pcm.h"
#include "test.h"

#define RATE AUDIO_LAB_RATE

static double absd(double v) { return v < 0 ? -v : v; }

static void test_formats(void) {
    CHECK(audio_lab_format_channels(AUDIO_LAB_FORMAT_S16_MONO) == 1);
    CHECK(audio_lab_format_channels(AUDIO_LAB_FORMAT_S16_STEREO) == 2);
    CHECK(audio_lab_format_channels(AUDIO_LAB_FORMAT_FLOAT_MONO) == 1);
    CHECK(audio_lab_format_channels(AUDIO_LAB_FORMAT_FLOAT_STEREO) == 2);
    CHECK(audio_lab_format_channels(2) == 0);
    CHECK(audio_lab_format_channels(-1) == 0);
    CHECK(audio_lab_format_frame_bytes(AUDIO_LAB_FORMAT_S16_MONO) == 2);
    CHECK(audio_lab_format_frame_bytes(AUDIO_LAB_FORMAT_S16_STEREO) == 4);
    CHECK(audio_lab_format_frame_bytes(AUDIO_LAB_FORMAT_FLOAT_MONO) == 4);
    CHECK(audio_lab_format_frame_bytes(AUDIO_LAB_FORMAT_FLOAT_STEREO) == 8);
    CHECK(audio_lab_format_frame_bytes(5) == 0);
    CHECK_STR(audio_lab_format_name(AUDIO_LAB_FORMAT_S16_STEREO), "S16 stereo");
    CHECK_STR(audio_lab_format_name(9), "unknown");
    CHECK(audio_lab_grain_valid(256) && audio_lab_grain_valid(1024) && audio_lab_grain_valid(2048));
    CHECK(!audio_lab_grain_valid(0) && !audio_lab_grain_valid(255) && !audio_lab_grain_valid(300) && !audio_lab_grain_valid(4096));
    CHECK(AUDIO_LAB_MAX_BLOCK_BYTES >= AUDIO_LAB_GRAIN_MAX * audio_lab_format_frame_bytes(AUDIO_LAB_FORMAT_FLOAT_STEREO));
}

/* Sample `channel` of frame `frame` in an interleaved block, as -1..1. */
static double sample_at(const unsigned char *block, int is_float, unsigned channels, unsigned frame, unsigned channel) {
    size_t index = (size_t)frame * channels + channel;
    return is_float ? (double)((const float *)block)[index] : ((const int16_t *)block)[index] / 32767.0;
}

/* Renders lead + tone + tail in blocks of `grain` and checks the signal. */
static void check_tone(int format, unsigned grain, double amplitude) {
    const unsigned long lead = RATE / 4, tone = RATE * 3 / 2, tail = RATE / 4;
    const unsigned long total = (lead + tone + tail + grain - 1) / grain * grain;
    unsigned channels = audio_lab_format_channels(format);
    int is_float = format == AUDIO_LAB_FORMAT_FLOAT_MONO || format == AUDIO_LAB_FORMAT_FLOAT_STEREO;
    /* Only mono and stereo formats produce samples; anything else is a test bug. */
    CHECK(channels == 1 || channels == 2);
    if (channels != 1 && channels != 2) return;
    double *mono = calloc(total, sizeof(double));
    unsigned char *block = malloc(AUDIO_LAB_MAX_BLOCK_BYTES);
    CHECK(mono && block);
    if (!mono || !block) {
        free(mono);
        free(block);
        return;
    }
    audio_lab_osc osc;
    audio_lab_osc_init(&osc, 440, amplitude, lead, tone);
    unsigned long nonzero = 0;
    double peak = 0;
    int channels_equal = 1;
    for (unsigned long done = 0; done < total; done += grain) {
        audio_lab_block_stats stats;
        audio_lab_fill(&osc, format, block, grain, &stats);
        nonzero += stats.nonzero;
        if (stats.peak > peak) peak = stats.peak;
        for (unsigned i = 0; i < grain; i++) {
            double left = sample_at(block, is_float, channels, i, 0);
            double right = channels == 2 ? sample_at(block, is_float, channels, i, 1) : left;
            if (left != right) channels_equal = 0;
            mono[done + i] = left;
        }
    }
    /* Silence before and after the tone, exactly. */
    int silent = 1;
    for (unsigned long i = 0; i < lead; i++) silent &= mono[i] == 0.0;
    for (unsigned long i = lead + tone; i < total; i++) silent &= mono[i] == 0.0;
    CHECK(silent);
    /* The tone is there, at the requested level (fade in/out excluded), and never louder. */
    CHECK(nonzero > tone * 9 / 10 && nonzero <= tone);
    CHECK(peak <= amplitude + 1e-3);
    CHECK(peak >= amplitude * 0.97);
    /* Fade in: the first tone samples are much quieter than the steady state. */
    double early = 0;
    for (unsigned long i = lead; i < lead + 48; i++) early = absd(mono[i]) > early ? absd(mono[i]) : early;
    CHECK(early < amplitude * 0.2);
    /* 440 Hz: about 880 sign changes per second of steady tone. */
    unsigned long from = lead + RATE / 10, to = from + RATE;
    int crossings = 0;
    for (unsigned long i = from + 1; i < to; i++) crossings += (mono[i - 1] < 0) != (mono[i] < 0);
    CHECK(crossings >= 876 && crossings <= 884);
    CHECK(channels_equal);
    free(mono);
    free(block);
}

static void test_tones(void) {
    check_tone(AUDIO_LAB_FORMAT_S16_STEREO, 1024, 0.1);
    check_tone(AUDIO_LAB_FORMAT_S16_MONO, 256, 0.1);
    check_tone(AUDIO_LAB_FORMAT_FLOAT_STEREO, 1024, 0.05);
    check_tone(AUDIO_LAB_FORMAT_FLOAT_MONO, 2048, 0.2);
    check_tone(AUDIO_LAB_FORMAT_S16_STEREO, 2048, 0.2);
}

static void test_limits(void) {
    /* Amplitude is clamped to 0.5 (-6 dBFS) whatever is asked. */
    audio_lab_osc osc;
    audio_lab_osc_init(&osc, 440, 5.0, 0, RATE);
    CHECK(osc.amplitude == 0.5);
    int16_t block[2048 * 2];
    audio_lab_block_stats stats;
    double peak = 0;
    for (int b = 0; b < 20; b++) {
        audio_lab_fill(&osc, AUDIO_LAB_FORMAT_S16_STEREO, block, 2048, &stats);
        if (stats.peak > peak) peak = stats.peak;
    }
    CHECK(peak <= 0.5 + 1e-3 && peak > 0.45);
    audio_lab_osc_init(&osc, 440, -1.0, 0, RATE);
    audio_lab_fill(&osc, AUDIO_LAB_FORMAT_S16_STEREO, block, 2048, &stats);
    CHECK(stats.nonzero == 0 && stats.peak == 0.0);
    /* An unknown format writes nothing and reports silence. */
    memset(block, 0x55, sizeof(block));
    audio_lab_osc_init(&osc, 440, 0.1, 0, RATE);
    audio_lab_fill(&osc, 2, block, 256, &stats);
    CHECK(stats.nonzero == 0 && ((unsigned char *)block)[0] == 0x55);
}

void run_audio_pcm_tests(void);
void run_audio_pcm_tests(void) {
    test_formats();
    test_tones();
    test_limits();
}
