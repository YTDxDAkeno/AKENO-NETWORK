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

#endif
