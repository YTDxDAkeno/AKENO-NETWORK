/*
 * Structured, redacted, size-limited log. Each line: "+12.345s LEVEL event: message".
 * Every message passes through akn_redact() before it reaches the file or the in-memory ring that
 * the diagnostics screen shows. Thread-safe. Without a file it still keeps the ring.
 */
#ifndef AKN_LOG_H
#define AKN_LOG_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { AKN_LOG_DEBUG = 0, AKN_LOG_INFO, AKN_LOG_WARN, AKN_LOG_ERROR } akn_log_level;

#define AKN_LOG_RING 12
#define AKN_LOG_LINE 200

typedef struct {
    pthread_mutex_t lock;
    FILE *file;
    char path[512];
    size_t max_bytes, written;
    uint64_t (*now_ms)(void);
    uint64_t t0;
    char ring[AKN_LOG_RING][AKN_LOG_LINE];
    int ring_next, ring_count;
    unsigned long lines;
    int echo_stderr;
} akn_log;

/* Opens (appends) path; an existing file larger than max_bytes is rotated to path.1 first.
 * path may be NULL (ring only). Returns 0 when the file is open. */
int akn_log_open(akn_log *log, const char *path, size_t max_bytes, uint64_t (*now_ms)(void));
void akn_log_close(akn_log *log);
void akn_logf(akn_log *log, akn_log_level level, const char *event, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
/* Copies the newest `max` ring lines, oldest first; returns how many. */
int akn_log_recent(akn_log *log, char lines[][AKN_LOG_LINE], int max);

#endif
