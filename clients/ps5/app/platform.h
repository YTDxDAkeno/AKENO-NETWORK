/*
 * The few things the console app needs from its platform. Every PS5 call used here is taken
 * from a sample of the pinned ps5-payload-sdk (see platform_ps5.c); nothing else is touched.
 */
#ifndef AKENO_APP_PLATFORM_H
#define AKENO_APP_PLATFORM_H

#include <stddef.h>

#include "game_detect.h"

typedef struct {
    const char *name;        /* "PlayStation 5" or the host OS name. */
    char firmware[16];       /* "12.20" on the PS5 when known, "" otherwise. */
    unsigned int firmware_raw;
    const char *notifications; /* How notify() delivers messages. */
    const char *browser;       /* How open_browser() works. */
    const char *game_detection; /* How list_apps() finds running games. */
    const char *actionable;     /* How notify_action() works. */
} akeno_platform_info;

/* Called once at startup. */
void akeno_platform_init(akeno_platform_info *info);
void akeno_platform_cleanup(void);

/* Shows a short system notification (plain UTF-8 text). Returns 0 when it was accepted. */
int akeno_platform_notify(const char *text);

/* Opens url in the system web browser. Returns 0 when the request was accepted. */
int akeno_platform_open_browser(const char *url);

/*
 * Lists processes with the application info the system reports for them (read-only). Returns the
 * number of entries written to out (at most max), or -1 when processes cannot be listed (err says
 * why). *total receives the number of processes seen.
 */
int akeno_platform_list_apps(akeno_proc_info *out, size_t max, size_t *total, char *err, size_t err_len);

/* Directory under which title metadata files are looked up ("" on the console), or NULL if none. */
const char *akeno_platform_metadata_root(void);

/*
 * Experimental: a notification with one button that should open url. Returns 0 when the system
 * accepted it, -1 when it refused, -2 when the function is not available on this system (on the
 * PS5: libSceNotification.sprx or sceNotificationSend could not be loaded at run time). Whether
 * the button opens the URL is not known before it is tried on a console. Callers fall back to
 * akeno_platform_notify() on any non-zero result.
 */
int akeno_platform_notify_action(const char *title, const char *text, const char *button, const char *url);

/* Short diagnostic text about the button notification function ("loaded", "not loaded", why it failed). */
const char *akeno_platform_actionable_status(void);

/* Writes one diagnostic line to the platform's debug channel (in addition to the log file). */
void akeno_platform_debug_line(const char *line);

#ifdef AKENO_EXPERIMENTAL_AUDIO
/*
 * Experimental audio research (PS5 voice prototypes A and C, docs/PS5_AUDIO_RESEARCH.md). Built
 * only into the separate "audio lab" executable; the release build contains none of this. Every
 * call is started by the user from the diagnostics page.
 */

/* Directory of the system's libraries ("/system/common/lib" on the PS5), or NULL if unknown. */
const char *akeno_platform_audio_library_dir(void);

/*
 * Loads library at run time and records in found[i] whether it exports names[i]. Only looks the
 * names up; nothing is called. Returns 0 when the library loaded, -1 otherwise (err says why).
 */
int akeno_platform_audio_lookup(const char *library, const char *const *names, int *found, size_t count, char *err, size_t err_len);

/*
 * Plays a sine tone on the main audio output and returns when it has finished (blocking, about
 * duration_ms). amplitude is 0..1. Returns 0 on success, -1 on failure, -2 when no output
 * function is available (err says why).
 */
int akeno_platform_audio_tone(unsigned frequency_hz, unsigned duration_ms, double amplitude, char *err, size_t err_len);
#endif

#ifdef AKENO_CAPTURE_DIAG
/*
 * Experimental PS5 capture capability diagnostic (docs/PS5_DIRECT_CAPTURE_RESEARCH.md). Built
 * only into the separate capture-diagnostic executable. Everything here is read-only: it never
 * calls a capture, encoder or broadcast function and never changes a console setting.
 */

/* Directory the candidate capture libraries are looked for in, or NULL if unknown. */
const char *akeno_platform_capture_library_dir(void);

/* 1 if library exists in the capture library dir (stat only), 0 otherwise. */
int akeno_platform_capture_file_exists(const char *library);

/* 1 if the firmware exposes read-only per-process application info (sceKernelGetAppInfo). */
int akeno_platform_capture_process_info_available(void);

/*
 * RESOLVE tier (runs only after an explicit user action): loads library at run time and records
 * in found[i] whether it exports names[i]. Only looks the names up; nothing is called. Returns 0
 * when the library loaded, -1 otherwise (err says why).
 */
int akeno_platform_capture_lookup(const char *library, const char *const *names, int *found, size_t count, char *err, size_t err_len);

/*
 * Read-only, redacted summary of the local DNS resolver configuration (e.g. "2 nameserver
 * entries; custom resolver configured"). Never returns the actual addresses. Returns 1 when a
 * summary was produced, 0 when nothing could be read safely.
 */
int akeno_platform_capture_dns_summary(char *out, size_t out_len);
#endif

#endif
