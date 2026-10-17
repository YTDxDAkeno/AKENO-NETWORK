/*
 * Audio lab (AKENO_EXPERIMENTAL_AUDIO builds only): test tone PCM generation. Portable and
 * unit-tested on Linux; the platform layer only hands the generated blocks to the system.
 *
 * Formats are exactly those of the PS5 audio driver of ps5-payload-dev/SDL
 * (src/audio/ps5/SDL_ps5audio.h, commit ee4c47d): the value is the sceAudioOutOpen "param".
 * Interleaved samples, left channel first.
 */
#ifndef AKENO_APP_AUDIO_LAB_PCM_H
#define AKENO_APP_AUDIO_LAB_PCM_H

#include <stddef.h>

#define AUDIO_LAB_FORMAT_S16_MONO 0
#define AUDIO_LAB_FORMAT_S16_STEREO 1
#define AUDIO_LAB_FORMAT_FLOAT_MONO 3
#define AUDIO_LAB_FORMAT_FLOAT_STEREO 4

#define AUDIO_LAB_RATE 48000u        /* The only rate the SDL driver opens. */
#define AUDIO_LAB_GRAIN_MIN 256u     /* Frames per sceAudioOutOutput call: 256..2048 in steps of 256. */
#define AUDIO_LAB_GRAIN_MAX 2048u
#define AUDIO_LAB_MAX_BLOCK_BYTES (AUDIO_LAB_GRAIN_MAX * 2u * 4u)

/* Channels of a format (1 or 2), or 0 for an unknown format. */
unsigned audio_lab_format_channels(int format);
/* Bytes per frame (all channels), or 0 for an unknown format. */
unsigned audio_lab_format_frame_bytes(int format);
/* "S16 mono", "S16 stereo", "float mono", "float stereo" or "unknown". */
const char *audio_lab_format_name(int format);
/* 1 when grain is one of the frame counts the SDL driver uses. */
int audio_lab_grain_valid(unsigned grain);

/*
 * A sine tone of `tone_frames` frames that starts after `lead_frames` frames of silence and is
 * followed by silence; 10 ms fade in and out (no clicks). amplitude is 0..0.5 (clamped).
 */
typedef struct {
    double cos_w;
    double s_prev, s_cur;
    double amplitude;
    unsigned long frame;
    unsigned long lead_frames, tone_frames, ramp_frames;
} audio_lab_osc;

void audio_lab_osc_init(audio_lab_osc *osc, unsigned frequency_hz, double amplitude, unsigned long lead_frames, unsigned long tone_frames);

typedef struct {
    double peak;            /* Largest absolute sample written (0..1). */
    unsigned long nonzero;  /* Frames with at least one nonzero sample. */
} audio_lab_block_stats;

/* Writes the next `frames` frames in `format` to out (which holds frames * frame bytes). */
void audio_lab_fill(audio_lab_osc *osc, int format, void *out, unsigned frames, audio_lab_block_stats *stats);

#endif
