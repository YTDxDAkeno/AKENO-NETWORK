/*
 * Audio lab v3 (AKENO_EXPERIMENTAL_AUDIO builds only): read-only measurement of AKENO's own audio
 * port with sceAudioOutGetPortState. Portable and unit-tested on Linux; the platform layer only
 * resolves the function and passes it in. See docs/PS5_AUDIO_RESEARCH.md for the evidence.
 *
 * What is known about the function, and how this module guards against what is not:
 *   - Name: exported by the PS5 libSceAudioOut (ps5-payload-sdk stub generated from firmware
 *     exports; looked up on 12.20 by audio lab v2). Resolved with dlsym; not called if missing.
 *   - Arguments: (int32 handle, state *out), agreed on by the OpenOrbis PS4 headers, shadPS4,
 *     fpPS4, SharpProspero (PS5) and AnyPS5 (PS5). No console measurement exists.
 *   - Structure: 32 bytes in shadPS4, fpPS4 and SharpProspero (PS4 SDK layout), 24 in AnyPS5
 *     (unsourced). Not assumed: the call writes into a 256-byte window inside a 4096-byte arena
 *     whose other bytes are canaries, so a larger write is detected and stops further calls.
 *   - Each measurement makes two calls, with the window filled with 0xA5 and then 0x5A. A byte is
 *     "written" when it differs from the fill in at least one call, so a written value that equals
 *     one fill is still seen. This bounds the written range from below; it does not prove the
 *     structure's size (a field the function leaves untouched is invisible).
 *
 * Only the return codes, the raw bytes, the written-byte map and the canaries are measured. The
 * decoded fields use the PS4 layout and are labelled as unverified everywhere they are shown.
 */
#ifndef AKENO_APP_AUDIO_LAB_STATE_H
#define AKENO_APP_AUDIO_LAB_STATE_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_LAB_STATE_ARENA_BYTES 4096u
#define AUDIO_LAB_STATE_LEAD_BYTES 64u    /* canary before the window; keeps the window 64-byte aligned */
#define AUDIO_LAB_STATE_WINDOW_BYTES 256u /* what the function may write into */
#define AUDIO_LAB_STATE_FILL_A 0xA5u
#define AUDIO_LAB_STATE_FILL_B 0x5Au
#define AUDIO_LAB_STATE_CANARY 0xC3u
#define AUDIO_LAB_STATE_LAYOUT_BYTES 32u  /* PS4 SDK layout (shadPS4, fpPS4, SharpProspero) */

/* The three points of a test at which AKENO's own port is measured. */
#define AUDIO_LAB_STATE_BEFORE_PLAYBACK 0
#define AUDIO_LAB_STATE_DURING_PLAYBACK 1
#define AUDIO_LAB_STATE_BEFORE_CLOSE 2
#define AUDIO_LAB_STATE_POINTS 3

/* The function as declared by every source: int32 (int32 handle, void *state). */
typedef int32_t (*audio_lab_port_state_fn)(int32_t handle, void *state);

typedef struct {
    int called;
    int32_t rc;                     /* raw return value */
    unsigned char fill;             /* the window's bytes before the call */
    int lead_intact, tail_intact;   /* the canaries before and after the window are untouched */
    unsigned char bytes[AUDIO_LAB_STATE_WINDOW_BYTES]; /* the raw window after the call */
} audio_lab_state_call;

typedef struct {
    int attempted;                  /* at least one call was made */
    long long at_us;                /* when (monotonic, relative to the first output call; 0 before it) */
    audio_lab_state_call call[2];   /* fill A, then fill B */
    int written_count;              /* bytes written by at least one call */
    int highest_written;            /* offset of the highest written byte, -1 if none */
    int guards_intact;              /* every canary of every call untouched */
    int calls_agree;                /* every byte written by both calls has the same value in both */
    unsigned char written[AUDIO_LAB_STATE_WINDOW_BYTES]; /* 1 where a call wrote the byte */
    unsigned char value[AUDIO_LAB_STATE_WINDOW_BYTES];   /* the value written there (0 elsewhere) */
} audio_lab_state_sample;

/* Fills the arena (AUDIO_LAB_STATE_ARENA_BYTES) with canaries and its window with fill. */
void audio_lab_state_prepare(unsigned char *arena, unsigned char fill);
/* The window inside an arena: 64-byte aligned when the arena is. */
unsigned char *audio_lab_state_window(unsigned char *arena);
/* Records one call's return value, raw window and canaries. */
void audio_lab_state_capture(const unsigned char *arena, int32_t rc, unsigned char fill, audio_lab_state_call *out);
/* Derives the written-byte map, the values and the summary fields from the calls made. */
void audio_lab_state_combine(audio_lab_state_sample *s);

/*
 * One measurement: two calls (fill A, then fill B) of fn on handle, each into a freshly prepared
 * arena. The second call is skipped when the first returned an error. Returns 0, or -1 when a
 * canary was changed: the function wrote outside the window, and the caller must not call it
 * again in this process.
 */
int audio_lab_state_measure(audio_lab_port_state_fn fn, int32_t handle, unsigned char *arena, audio_lab_state_sample *s);

/* Fields decoded with the PS4 SDK layout. UNVERIFIED on the PS5; *_known is 0 when a byte of the
 * field was not written by the call. */
typedef struct {
    int output_known, channel_known, volume_known, reroute_known, flag_known;
    unsigned output;                /* uint16 at 0: 0x01 primary, 0x02 secondary, 0x04 tertiary, 0x40 headphone, 0x80 external */
    unsigned channel;               /* uint8 at 2 */
    int volume;                     /* int16 at 4 */
    unsigned reroute_counter;       /* uint16 at 6 */
    unsigned long long flag;        /* uint64 at 8 */
} audio_lab_state_decoded;
void audio_lab_state_decode(const audio_lab_state_sample *s, audio_lab_state_decoded *d);

/* Names of the PS4 output bits set in output ("primary, headphone"), or "none". */
void audio_lab_state_output_names(unsigned output, char *out, size_t out_len);

/* How many window bytes a report shows: at least the PS4 layout, up to the highest written byte
 * (rounded up to 16), the whole window when a canary was hit. */
size_t audio_lab_state_shown_bytes(const audio_lab_state_sample *s);

/* Lower-case hex, one space between bytes. Returns the length written (truncated to fit). */
size_t audio_lab_state_hex(const unsigned char *bytes, size_t n, char *out, size_t out_len);
/* The reconstructed written values of a sample, "--" where no call wrote the byte. */
size_t audio_lab_state_values_hex(const audio_lab_state_sample *s, size_t n, char *out, size_t out_len);

#endif
