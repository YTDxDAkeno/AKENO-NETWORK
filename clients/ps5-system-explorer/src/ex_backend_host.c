/* PC simulation backend for the Explorer (ex_backend_host.h). */
#define _POSIX_C_SOURCE 200809L
#include "ex_backend.h"
#include "ex_backend_host.h"
#include "ex.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t h_now_us(void *u) {
    ex_sim *s = u;
    if (s->real_time) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
    }
    return s->vclock_us;
}

static void h_sleep_ms(void *u, unsigned ms) {
    ex_sim *s = u;
    s->sleep_calls++;
    if (s->real_time) {
        struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
        return;
    }
    s->vclock_us += (uint64_t)ms * 1000u;
    if (s->gap_at_poll && s->sleep_calls == s->gap_at_poll && s->gap_ms > 0)
        s->vclock_us += (uint64_t)s->gap_ms * 1000u;
}

static long long h_unix(void *u) {
    (void)u;
    return (long long)time(NULL);
}

static int h_notify(void *u, const char *text) {
    ex_sim *s = u;
    s->notifications++;
    snprintf(s->last_notification, sizeof(s->last_notification), "%s", text);
    return 0;
}

static const char *h_platform(void *u) {
    (void)u;
    return "PC simulation (not a PS5)";
}

static void h_read_proc(void *u, ex_raw_proc *out) {
    ex_sim *s = u;
    memset(out, 0, sizeof(*out));
    out->have_fw = s->have_fw;
    out->firmware_raw = s->fw;
    out->pid = 4321;
    out->ppid = 1;
    out->have_proc = s->have_proc;
    if (s->have_proc) {
        snprintf(out->comm, sizeof(out->comm), "%s", "akeno-sysexp");
        out->proc_stat = 2;
        out->start_unix = (long long)time(NULL) - 1;
        out->rss_bytes = 3 * 1024 * 1024;
    }
    out->have_appinfo_fn = s->have_appinfo;
    if (s->have_appinfo) {
        if (s->foreground) {
            out->appinfo_rc = 0;
            memcpy(out->appinfo_raw, &s->game_app, 4);        /* own app id = the game */
            memcpy(out->appinfo_raw + 20, "PPSA00001", 10);   /* title id at the samples/ps offset */
        } else {
            out->appinfo_rc = s->appinfo_err ? s->appinfo_err : (int32_t)0x80020016;
        }
    }
    out->have_bigapp_fn = s->have_bigapp;
    out->bigapp_app_id = s->foreground ? s->game_app : 0; /* home screen by default */
    out->have_user_fn = s->have_user;
    out->user_rc = s->user_rc;
    out->foreground_user = s->user_id ? s->user_id : 0x10000001u;
    out->have_rlimit = s->have_rlimit;
    if (s->have_rlimit) {
        out->rlimit_nofile_cur = 1024;
        out->rlimit_nofile_max = 4096;
        out->rlimit_as_cur = -1;
    }
    out->have_rusage = s->have_rusage;
    if (s->have_rusage) {
        out->cpu_user_s = 0.012;
        out->cpu_sys_s = 0.004;
        out->maxrss_bytes = 4 * 1024 * 1024;
    }
}

static int catalogue_index(const char *library) {
    size_t count;
    const ex_lib_entry *cat = ex_lib_catalogue(&count);
    for (size_t i = 0; i < count; i++)
        if (strcmp(cat[i].library, library) == 0) return (int)i;
    return -1;
}

static int h_file_present(void *u, const char *path) {
    ex_sim *s = u;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    int idx = catalogue_index(base);
    if (idx < 0) return 0;
    return (s->absent_files & (1u << idx)) ? 0 : 1;
}

static int h_lib_lookup(void *u, const char *library, const char *const *names, int *found, size_t count,
                        char *note, size_t note_len) {
    ex_sim *s = u;
    (void)names;
    int idx = catalogue_index(library);
    for (size_t i = 0; i < count; i++) found[i] = 0;
    if (idx >= 0 && (s->load_fail & (1u << idx))) {
        snprintf(note, note_len, "dlopen failed (simulated)");
        return -1;
    }
    for (size_t i = 0; i < count; i++) found[i] = 1;
    if (idx >= 0 && (s->missing_last_sym & (1u << idx)) && count > 0) found[count - 1] = 0;
    snprintf(note, note_len, "loaded (simulated), %zu names looked up", count);
    return 0;
}

static int h_read_control(void *u, char *buf, size_t len) {
    ex_sim *s = u;
    if (!s->control) return 0;
    size_t remaining = strlen(s->control) - s->control_pos;
    if (remaining == 0) return 0;
    size_t want = s->control_chunk ? s->control_chunk : remaining;
    if (want > remaining) want = remaining;
    if (want > len) want = len;
    memcpy(buf, s->control + s->control_pos, want);
    s->control_pos += want;
    return (int)want;
}

static void h_sample_context(void *u, int32_t *bigapp, int32_t *user) {
    ex_sim *s = u;
    s->sample_calls++;
    if (s->foreground) {
        *bigapp = s->game_app;
    } else if (s->game_after_samples >= 0 && (int)s->sample_calls > s->game_after_samples && s->game_app) {
        *bigapp = s->game_app; /* a game has started */
    } else {
        *bigapp = 0;           /* home screen */
    }
    *user = (int32_t)(s->user_id ? s->user_id : 0x10000001u);
}

void ex_backend_host(ex_backend *be, void *sim) {
    ex_backend b = { "PC simulation (not a PS5)", sim, h_now_us, h_sleep_ms, h_unix, h_notify, h_read_proc,
                     h_platform, h_file_present, h_lib_lookup, h_read_control, h_sample_context };
    *be = b;
}
