#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "redact.h"

static const char *level_name(akn_log_level level) {
    switch (level) {
    case AKN_LOG_DEBUG: return "DEBUG";
    case AKN_LOG_INFO: return "INFO";
    case AKN_LOG_WARN: return "WARN";
    case AKN_LOG_ERROR: return "ERROR";
    }
    return "?";
}

/* Appends to path, creating it private (0600): the log is diagnostic data for the owner only. */
static FILE *open_private(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "a");
    if (!f) close(fd);
    return f;
}

static void rotate_locked(akn_log *log) {
    if (log->file) fclose(log->file);
    log->file = NULL;
    char old[530];
    snprintf(old, sizeof(old), "%s.1", log->path);
    remove(old);
    rename(log->path, old);
    log->file = open_private(log->path);
    log->written = 0;
}

int akn_log_open(akn_log *log, const char *path, size_t max_bytes, uint64_t (*now_ms)(void)) {
    memset(log, 0, sizeof(*log));
    pthread_mutex_init(&log->lock, NULL);
    log->now_ms = now_ms;
    log->t0 = now_ms ? now_ms() : 0;
    log->max_bytes = max_bytes ? max_bytes : 512 * 1024;
    if (!path || !path[0]) return -1;
    snprintf(log->path, sizeof(log->path), "%s", path);
    struct stat st;
    if (stat(path, &st) == 0 && (size_t)st.st_size > log->max_bytes) {
        char old[530];
        snprintf(old, sizeof(old), "%s.1", path);
        remove(old);
        rename(path, old);
    }
    log->file = open_private(path);
    if (!log->file) return -1;
    if (stat(path, &st) == 0) log->written = (size_t)st.st_size;
    return 0;
}

void akn_log_close(akn_log *log) {
    pthread_mutex_lock(&log->lock);
    if (log->file) fclose(log->file);
    log->file = NULL;
    pthread_mutex_unlock(&log->lock);
    pthread_mutex_destroy(&log->lock);
}

void akn_logf(akn_log *log, akn_log_level level, const char *event, const char *fmt, ...) {
    char raw[1024], clean[1200], line[1400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(raw, sizeof(raw), fmt, ap);
    va_end(ap);
    akn_redact(clean, sizeof(clean), raw);
    uint64_t t = log->now_ms ? log->now_ms() - log->t0 : 0;
    int n = snprintf(line, sizeof(line), "+%llu.%03llus %s %s: %s\n", (unsigned long long)(t / 1000),
                     (unsigned long long)(t % 1000), level_name(level), event ? event : "-", clean);
    if (n < 0) return;
    size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;

    pthread_mutex_lock(&log->lock);
    log->lines++;
    snprintf(log->ring[log->ring_next], AKN_LOG_LINE, "%.*s", (int)(len - 1), line);
    log->ring_next = (log->ring_next + 1) % AKN_LOG_RING;
    if (log->ring_count < AKN_LOG_RING) log->ring_count++;
    if (log->file) {
        if (log->written + len > log->max_bytes) rotate_locked(log);
        if (log->file) {
            fwrite(line, 1, len, log->file);
            fflush(log->file);
            log->written += len;
        }
    }
    if (log->echo_stderr) fputs(line, stderr);
    pthread_mutex_unlock(&log->lock);
}

int akn_log_recent(akn_log *log, char lines[][AKN_LOG_LINE], int max) {
    pthread_mutex_lock(&log->lock);
    int n = log->ring_count < max ? log->ring_count : max;
    int start = (log->ring_next - n + AKN_LOG_RING) % AKN_LOG_RING;
    for (int i = 0; i < n; i++) memcpy(lines[i], log->ring[(start + i) % AKN_LOG_RING], AKN_LOG_LINE);
    pthread_mutex_unlock(&log->lock);
    return n;
}
