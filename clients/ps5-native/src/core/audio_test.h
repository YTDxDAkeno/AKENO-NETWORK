/*
 * Foreground audio output test (milestone V1). Plays the same tone as audio lab v2 (440 Hz,
 * 1.5 s, 10 ms fades, 250 ms of silence before and after) through platform callbacks and records
 * every return code, the pacing of the output calls and what was submitted, so a result from the
 * native foreground app can be compared line by line with the silent background results.
 *
 * Portable: the PS5 callbacks call sceAudioOut*, the PC callbacks feed SDL, the unit tests use
 * fakes. The tone itself is clients/ps5/app/audio_lab_pcm.c (unit-tested there).
 */
#ifndef AKN_AUDIO_TEST_H
#define AKN_AUDIO_TEST_H

#include <stddef.h>
#include <stdint.h>

/* sceAudioOutInit returns this when the library is already initialised in the process
 * (PS5-Audio-Passthrough-Research, hardware); it counts as success. */
#define AKN_AUDIO_ALREADY_INITIALIZED ((int32_t)0x8026000E)

typedef struct {
    /* Each returns the raw system result: 0 or a handle for success, a negative code otherwise. */
    int32_t (*init)(void *user);
    int32_t (*open)(void *user, unsigned grain, unsigned rate, int format);
    int32_t (*output)(void *user, int32_t handle, const void *samples);
    int32_t (*close)(void *user, int32_t handle);
    uint64_t (*now_us)(void *user);
    void *user;
    const char *backend; /* "sceAudioOut (direct)", "SDL queue", ... */
} akn_audio_ops;

typedef struct {
    const char *id;
    const char *label;
    int format;     /* sceAudioOutOpen param: AUDIO_LAB_FORMAT_* */
    unsigned grain; /* frames per output call */
} akn_audio_preset;

typedef struct {
    const char *id;
    const char *label;
    int dbfs; /* negative */
} akn_audio_level;

#define AKN_AUDIO_PRESET_COUNT 3
#define AKN_AUDIO_LEVEL_COUNT 3
#define AKN_AUDIO_DEFAULT_PRESET 0
#define AKN_AUDIO_DEFAULT_LEVEL 1
#define AKN_AUDIO_HARD_CAP_DBFS (-6)
#define AKN_AUDIO_MIN_GAP_MS 3000u
#define AKN_AUDIO_MAX_TESTS 30

const akn_audio_preset *akn_audio_preset_at(int index);
const akn_audio_level *akn_audio_level_at(int index);
/* Linear amplitude for a level in dBFS, never above AKN_AUDIO_HARD_CAP_DBFS. */
double akn_audio_amplitude(int dbfs);

typedef enum {
    AKN_AUDIO_NOT_RUN = 0,
    AKN_AUDIO_INIT_FAILED,
    AKN_AUDIO_OPEN_FAILED,
    AKN_AUDIO_OUTPUT_FAILED,
    AKN_AUDIO_CANCELLED,
    AKN_AUDIO_ACCEPTED_NOT_PACED, /* every call succeeded but returned faster than real time */
    AKN_AUDIO_ACCEPTED_PACED      /* every call succeeded and was consumed at playback speed */
} akn_audio_outcome;

typedef enum { AKN_HEARD_UNANSWERED = 0, AKN_HEARD_YES, AKN_HEARD_NO, AKN_HEARD_UNSURE } akn_heard;

typedef struct {
    int test_number;
    char backend[48];
    int preset;
    int level;
    int32_t init_rc;
    int32_t open_rc; /* the handle when > 0 */
    unsigned long calls, ok_calls;
    int32_t first_error;
    unsigned long first_error_call; /* 1-based, 0 = none */
    int32_t last_rc;
    int32_t close_rc;
    int closed;
    unsigned long frames_submitted;
    unsigned long nonzero_frames;
    double peak; /* 0..1 */
    uint64_t call_min_us, call_max_us, call_avg_us; /* calls 2..n */
    uint64_t block_us;                              /* duration of one block at 48 kHz */
    uint64_t total_us;
    akn_audio_outcome outcome;
    akn_heard heard;
} akn_audio_result;

/*
 * Runs one test. Blocks for about two seconds (call it from a worker thread). `cancel` is polled
 * between blocks; the port is closed in every case once it was opened. Returns the outcome.
 */
akn_audio_outcome akn_audio_run(const akn_audio_ops *ops, int preset, int level, int test_number,
                                const volatile int *cancel, akn_audio_result *out);

const char *akn_audio_outcome_name(akn_audio_outcome outcome);
const char *akn_heard_name(akn_heard heard);
/* Multi-line, plain-text report of one result (no secrets in it by construction). */
void akn_audio_format(const akn_audio_result *r, char *buf, size_t len);

#endif
