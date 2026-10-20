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

#define H_SDK_KINFO 1088          /* KINFO_PROC_SIZE in the pinned SDK headers */

static void fill_proc_query(ex_sim *s, ex_raw_proc *out) {
    ex_proc_query *q = &out->pq;
    memset(q, 0, sizeof(*q));
    q->attempted = 1;
    q->sdk_size = H_SDK_KINFO;
    q->buffer_bound = 8192;
    switch (s->pq_mode) {
    case 1: /* decoded */
        q->size_reported = H_SDK_KINFO;
        q->requested = q->buffer_bound;
        q->returned = H_SDK_KINFO;
        q->have_structsize = 1;
        q->structsize = H_SDK_KINFO;
        q->have_pid_at_sdk_offset = 1;
        q->pid_at_sdk_offset = out->pid;
        q->decoded = 1;
        out->have_proc = 1;
        snprintf(out->comm, sizeof(out->comm), "%s", "akeno-sysexp");
        out->proc_stat = 2;
        out->start_unix = (long long)time(NULL) - 1;
        out->rss_bytes = 3 * 1024 * 1024;
        break;
    case 2: /* size query denied */
        q->size_rc = -1;
        q->size_errno = 1;
        q->size_errno_kind = EX_ERRNO_PERM;
        break;
    case 3: /* unsupported node */
        q->size_rc = -1;
        q->size_errno = 2;
        q->size_errno_kind = EX_ERRNO_NOENT;
        break;
    case 4: /* read ENOMEM: kernel record larger than the buffer that was passed */
        q->size_reported = H_SDK_KINFO;
        q->requested = H_SDK_KINFO;
        q->read_rc = -1;
        q->read_errno = 12;
        q->read_errno_kind = EX_ERRNO_NOMEM;
        break;
    case 5: /* layout mismatch: within the bound but ki_structsize disagrees with the SDK's */
        q->size_reported = H_SDK_KINFO;
        q->requested = q->buffer_bound;
        q->returned = H_SDK_KINFO;
        q->have_structsize = 1;
        q->structsize = H_SDK_KINFO - 8;
        q->have_pid_at_sdk_offset = 1;
        q->pid_at_sdk_offset = out->pid;
        break;
    default: /* 0: empty read (the FW 12.20 observation: NOT_AVAILABLE) */
        q->size_reported = H_SDK_KINFO;
        q->requested = q->buffer_bound;
        q->returned = 0;
        break;
    }
}

static void h_read_proc(void *u, ex_raw_proc *out) {
    ex_sim *s = u;
    memset(out, 0, sizeof(*out));
    out->have_fw = s->have_fw;
    out->firmware_raw = s->fw;
    out->pid = 4321;
    out->ppid = 1;
    /* have_proc and the record come from the per-process query model (pq_mode). The legacy
     * have_proc flag forces the decoded path for tests that only set it. */
    if (s->have_proc && s->pq_mode == 0) s->pq_mode = 1;
    fill_proc_query(s, out);
    out->have_appinfo_fn = s->have_appinfo;
    if (s->have_appinfo) {
        if (s->foreground) {
            out->appinfo_rc = 0;
            memcpy(out->appinfo_raw, &s->game_app, 4);        /* own app id = the game */
            memcpy(out->appinfo_raw + 20, "PPSA00001", 10);   /* title id at the samples/ps offset */
        } else if (s->appinfo_ok_zero) {
            out->appinfo_rc = 0; /* the call succeeds but the app id is 0: a background payload (owner's 6.1 case) */
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

static int h_read_control(void *u, char *buf, size_t len, int *reset) {
    ex_sim *s = u;
    *reset = 0;
    /* Swap in the replacement stream once, modelling a control file that was rewritten. */
    if (s->control2 && s->control_swap_at && s->sample_calls >= s->control_swap_at && !s->control_reset_pending &&
        s->control != s->control2) {
        s->control = s->control2;
        s->control_pos = 0;
        s->control_reset_pending = 1;
        *reset = 1;
    }
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

static void h_sample_context(void *u, ex_ctx_sample *out) {
    ex_sim *s = u;
    s->sample_calls++;
    memset(out, 0, sizeof(*out));
    out->have_bigapp_fn = s->have_bigapp;
    out->have_user_fn = s->have_user;
    out->user_rc = s->user_rc;
    if (s->have_bigapp) {
        if (s->foreground) out->bigapp = s->game_app;
        else if (s->game_after_samples >= 0 && (int)s->sample_calls > s->game_after_samples && s->game_app)
            out->bigapp = s->game_app; /* a game has started */
        else out->bigapp = 0;          /* home screen */
    }
    out->user = s->user_id ? s->user_id : 0x10000001u;
}

/* Finds "<app_id-hex>=<title>" for app_id in a ";"-separated map. Returns 1 and the value (which
 * may be empty), 0 if not found. */
static int title_lookup(const char *map, int32_t app_id, char *val, size_t val_len) {
    val[0] = '\0';
    if (!map) return 0;
    char key[16];
    snprintf(key, sizeof(key), "0x%x=", (unsigned)app_id);
    const char *p = strstr(map, key);
    if (!p) {
        snprintf(key, sizeof(key), "0x%08x=", (unsigned)app_id);
        p = strstr(map, key);
    }
    if (!p) return 0;
    p += strlen(key);
    size_t n = 0;
    while (p[n] && p[n] != ';' && n + 1 < val_len) {
        val[n] = p[n];
        n++;
    }
    val[n] = '\0';
    return 1;
}

static void h_query_title(void *u, int32_t app_id, ex_title_query *out) {
    ex_sim *s = u;
    s->title_calls++;
    memset(out, 0, sizeof(*out));
    out->nul_at = -1;
    out->guard_ok = 1;
    if (s->title_no_fn) {
        out->have_fn = 0;
        return;
    }
    out->have_fn = 1;
    out->called = 1;
    char val[32];
    int found = title_lookup(s->title_map, app_id, val, sizeof(val));
    if (s->title_overrun) {
        out->guard_ok = 0; /* the backend detected a write past the window */
        return;
    }
    if (!found || val[0] == '\0') {
        out->rc = s->title_err_rc ? s->title_err_rc : (int32_t)0x80A70001; /* a plausible error code */
        return;
    }
    const char *str = s->title_malformed ? "bad-title" : val;
    size_t n = strlen(str);
    if (n >= EX_TITLE_RAW_KEEP) n = EX_TITLE_RAW_KEEP - 1;
    memcpy(out->raw, str, n);
    out->raw[n] = '\0';
    out->nul_at = (int)n;
    out->written = (int)n + 1;
}

static void h_read_usage(void *u, ex_usage *out) {
    ex_sim *s = u;
    memset(out, 0, sizeof(*out));
    if (!s->have_rusage) return;
    out->have = 1;
    out->cpu_user_s = 0.02;
    out->cpu_sys_s = 0.01;
    out->maxrss_bytes = s->pq_maxrss_end ? s->pq_maxrss_end : 4 * 1024 * 1024;
}

void ex_backend_host(ex_backend *be, void *sim) {
    ex_backend b = { "PC simulation (not a PS5)", sim, h_now_us, h_sleep_ms, h_unix, h_notify, h_read_proc,
                     h_platform, h_file_present, h_lib_lookup, h_read_control, h_sample_context,
                     h_query_title, h_read_usage };
    *be = b;
}
