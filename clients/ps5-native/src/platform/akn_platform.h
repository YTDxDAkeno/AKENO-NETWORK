/*
 * Everything that differs between the PS5 and the PC build. The PS5 implementation is the only
 * code that declares or calls console system functions (see platform_ps5.c for the list and the
 * source of every declaration); CI checks that no other object references them.
 */
#ifndef AKN_PLATFORM_H
#define AKN_PLATFORM_H

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

/* A system notification (PS5) or a line on stderr (PC). Returns 0 on success. */
int akn_platform_notify(const char *text);

/* The audio test backend: direct sceAudioOut on the PS5, an SDL audio device on the PC. */
const akn_audio_ops *akn_platform_audio_ops(void);

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
