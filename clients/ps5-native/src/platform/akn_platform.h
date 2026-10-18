/*
 * Everything that differs between the PS5 and the PC build. The PS5 implementation is the only
 * code that declares or calls console system functions (see platform_ps5.c for the list and the
 * source of every declaration); CI checks that no other object references them.
 */
#ifndef AKN_PLATFORM_H
#define AKN_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "audio_test.h"
#include "context.h"

typedef struct {
    const char *name;         /* "PS5" or "PC" */
    char firmware[16];        /* "12.20", or "n/a" */
    uint32_t firmware_raw;
    char akeno_dir[256];      /* the daemon's data: read only */
    char native_dir[256];     /* this app's own data (log, report, screenshots) */
    int daemon_port;          /* the daemon's local page server */
} akn_platform_info;

void akn_platform_info_get(akn_platform_info *info);

/* Monotonic clocks and wall-clock time. */
uint64_t akn_platform_now_ms(void);
uint64_t akn_platform_now_us(void);
long long akn_platform_unix_time(void);

/* Fills the raw process-context answers and returns the classification (PC: AKN_CTX_DESKTOP). */
akn_context_kind akn_platform_context(akn_context_raw *raw);

/* The app ID of the running foreground application, or a value <= 0 when unknown (test F). */
int32_t akn_platform_running_app(void);

/* A system notification (PS5) or a line on stderr (PC). Returns 0 on success. */
int akn_platform_notify(const char *text);

/* The audio test backend: direct sceAudioOut on the PS5, an SDL audio device on the PC. */
const akn_audio_ops *akn_platform_audio_ops(void);

/*
 * Microphone input for party voice (docs/PS5_VOICE.md). Nothing here runs until the user starts
 * voice or the microphone test (after a confirmation): the PS5 implementation then loads
 * libSceAudioIn.sprx with dlopen and opens the foreground user's microphone (DualSense or a
 * headset the system routes to that user): S16 mono, 256 frames per read, 16 kHz (48 kHz as the
 * fallback). PC: a simulated microphone from AKENO_NATIVE_SIM_MIC (tests only).
 */
typedef struct {
    /* Returns a handle (>= 0) or the system error code (< 0); *rate receives the rate opened. */
    int32_t (*open)(unsigned grain, unsigned *rate);
    /* Reads one grain of S16 mono samples (blocks for about one grain). >= 0 on success. */
    int32_t (*read)(int32_t handle, int16_t *samples);
    int32_t (*close)(int32_t handle);
    const char *backend;
} akn_mic_ops;

/* The microphone functions, loaded on first use; NULL with the reason in why when there are none. */
const akn_mic_ops *akn_platform_mic(char *why, size_t why_len);

/*
 * Read-only state of the foreground user's controller as the system reports it, independent of
 * SDL (which ignores the "connected" flag on the PS5). PC: available = 0.
 */
typedef struct {
    int available;    /* the query exists on this platform */
    int has_handle;   /* SDL opened a pad for this user */
    int connected;
    int32_t rc;       /* last scePadGetHandle / scePadReadState result */
    int32_t user_id;
} akn_pad_status;

void akn_platform_pad_status(akn_pad_status *status);

/* Called once before the video subsystem starts (PS5: hide the system splash screen). */
void akn_platform_before_video(void);

/*
 * Leaves the app. PS5 foreground: returns to the system like SDL's PS5 main does
 * (sceSystemServiceLoadExec("exit")); never called for a background process, where it would act
 * on whatever application is in the foreground. PC: nothing.
 */
void akn_platform_exit_to_system(void);

#endif
