/*
 * The few things the console app needs from its platform. Every PS5 call used here is taken
 * from a sample of the pinned ps5-payload-sdk (see platform_ps5.c); nothing else is touched.
 */
#ifndef AKENO_APP_PLATFORM_H
#define AKENO_APP_PLATFORM_H

typedef struct {
    const char *name;        /* "PlayStation 5" or the host OS name. */
    char firmware[16];       /* "12.20" on the PS5 when known, "" otherwise. */
    unsigned int firmware_raw;
    const char *notifications; /* How notify() delivers messages. */
    const char *browser;       /* How open_browser() works. */
} akeno_platform_info;

/* Called once at startup. */
void akeno_platform_init(akeno_platform_info *info);
void akeno_platform_cleanup(void);

/* Shows a short system notification (plain UTF-8 text). Returns 0 when it was accepted. */
int akeno_platform_notify(const char *text);

/* Opens url in the system web browser. Returns 0 when the request was accepted. */
int akeno_platform_open_browser(const char *url);

/* Writes one diagnostic line to the platform's debug channel (in addition to the log file). */
void akeno_platform_debug_line(const char *line);

#endif
