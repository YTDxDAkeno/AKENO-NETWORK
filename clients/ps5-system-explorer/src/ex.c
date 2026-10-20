/* AKENO System Explorer core: probe/event recording, context classification, orchestration. */
#define _POSIX_C_SOURCE 200809L
#include "ex.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ex_redact.h"

const char *ex_evidence_name(ex_evidence e) {
    switch (e) {
    case EX_EV_SOURCE_VERIFIED: return "SOURCE_VERIFIED";
    case EX_EV_HARDWARE_VERIFIED: return "HARDWARE_VERIFIED";
    case EX_EV_HYPOTHESIS: return "HYPOTHESIS";
    case EX_EV_UNKNOWN: break;
    }
    return "UNKNOWN";
}

const char *ex_status_name(ex_status s) {
    switch (s) {
    case EX_ST_OK: return "OK";
    case EX_ST_NOT_AVAILABLE: return "NOT_AVAILABLE";
    case EX_ST_UNSUPPORTED_FW: return "UNSUPPORTED_ON_FW_12_20";
    case EX_ST_SKIPPED: return "SKIPPED";
    case EX_ST_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

const char *ex_context_name(ex_context_kind k) {
    switch (k) {
    case EX_CTX_FOREGROUND: return "foreground application";
    case EX_CTX_BACKGROUND: return "background process";
    case EX_CTX_DESKTOP: return "desktop build";
    case EX_CTX_UNKNOWN: break;
    }
    return "unknown";
}

const char *ex_event_kind_name(ex_event_kind k) {
    switch (k) {
    case EX_EVT_START: return "start";
    case EX_EVT_SAMPLE: return "sample";
    case EX_EVT_CONTEXT_CHANGE: return "context_change";
    case EX_EVT_SCHED_GAP: return "sched_gap";
    case EX_EVT_MARKER: return "marker";
    case EX_EVT_EXTEND: return "extend";
    case EX_EVT_STOP: return "stop";
    }
    return "unknown";
}

/*
 * sceKernelGetAppInfo writes an undocumented structure; the pinned SDK's samples disagree on where
 * the title ID is (offset 20 in samples/ps, 16 in samples/test_privileges). We do not guess: we
 * record a title-ID-shaped run ([A-Z]{4}[0-9]{5}) at either offset. This mirrors the daemon's
 * game_detect.c, kept self-contained so the Explorer does not pull in the daemon's JSON code.
 */
static int looks_like_title_id(const unsigned char *s) {
    for (int i = 0; i < 4; i++)
        if (s[i] < 'A' || s[i] > 'Z') return 0;
    for (int i = 4; i < 9; i++)
        if (s[i] < '0' || s[i] > '9') return 0;
    return s[9] == '\0';
}

static void parse_appinfo(const unsigned char *raw, size_t len, uint32_t *app_id, char *title, size_t title_len) {
    *app_id = 0;
    title[0] = '\0';
    if (len >= 4) memcpy(app_id, raw, 4);
    const size_t offsets[2] = { 20, 16 };
    for (size_t i = 0; i < 2; i++) {
        size_t off = offsets[i];
        if (off + 10 <= len && looks_like_title_id(raw + off)) {
            snprintf(title, title_len, "%.9s", (const char *)(raw + off));
            return;
        }
    }
}

void ex_fill_sysinfo(const ex_raw_proc *raw, const char *platform, ex_sysinfo *s) {
    memset(s, 0, sizeof(*s));
    snprintf(s->platform, sizeof(s->platform), "%s", platform ? platform : "unknown");
    s->have_fw = raw->have_fw;
    s->firmware_raw = raw->firmware_raw;
    if (raw->have_fw && raw->firmware_raw)
        snprintf(s->firmware, sizeof(s->firmware), "%x.%02x", (raw->firmware_raw >> 24) & 0xFF,
                 (raw->firmware_raw >> 16) & 0xFF);
    s->pid = raw->pid;
    s->ppid = raw->ppid;
    s->have_proc = raw->have_proc;
    snprintf(s->comm, sizeof(s->comm), "%s", raw->comm);
    s->proc_stat = raw->proc_stat;
    s->start_unix = raw->start_unix;
    s->rss_bytes = raw->have_proc ? raw->rss_bytes : -1;
    s->have_appinfo = raw->have_appinfo_fn;
    s->appinfo_rc = raw->appinfo_rc;
    if (raw->have_appinfo_fn && raw->appinfo_rc == 0)
        parse_appinfo(raw->appinfo_raw, sizeof(raw->appinfo_raw), &s->own_app_id, s->own_title_id,
                      sizeof(s->own_title_id));
    s->have_bigapp = raw->have_bigapp_fn;
    s->bigapp_app_id = raw->bigapp_app_id;
    s->have_user = raw->have_user_fn;
    s->user_rc = raw->user_rc;
    s->foreground_user = raw->foreground_user;
    s->have_rlimit = raw->have_rlimit;
    s->rlimit_nofile_cur = raw->rlimit_nofile_cur;
    s->rlimit_nofile_max = raw->rlimit_nofile_max;
    s->rlimit_as_cur = raw->rlimit_as_cur;
    s->have_rusage = raw->have_rusage;
    s->cpu_user_s = raw->cpu_user_s;
    s->cpu_sys_s = raw->cpu_sys_s;
    s->maxrss_bytes = raw->maxrss_bytes;
}

ex_context_kind ex_classify_context(const ex_sysinfo *s) {
    if (s->platform[0] == 'P' && s->platform[1] == 'C') return EX_CTX_DESKTOP;
    if (!s->have_appinfo || s->appinfo_rc != 0 || !s->have_bigapp) return EX_CTX_UNKNOWN;
    if (s->bigapp_app_id <= 0) return EX_CTX_BACKGROUND; /* no foreground application at all */
    if (s->own_app_id == 0) return EX_CTX_BACKGROUND;    /* not an application process */
    return s->own_app_id == (uint32_t)s->bigapp_app_id ? EX_CTX_FOREGROUND : EX_CTX_BACKGROUND;
}

/* ---- probe / event recording ----------------------------------------------------------------- */

ex_probe *ex_add_probe(ex_run *run, char module, const char *name, ex_status st, ex_evidence ev) {
    size_t i = run->probe_count++;
    if (i >= EX_PROBES_MAX) return NULL;
    ex_probe *p = &run->probe[i];
    memset(p, 0, sizeof(*p));
    p->module = module;
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->status = st;
    p->evidence = ev;
    return p;
}

void ex_probe_value(ex_probe *p, const char *fmt, ...) {
    if (!p) return;
    char tmp[EX_VALUE_MAX];
    va_list ap;
    va_start(ap, fmt);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
#pragma clang diagnostic pop
    va_end(ap);
    ex_redact(p->value, sizeof(p->value), tmp);
}

void ex_probe_rc(ex_probe *p, int32_t rc) {
    if (!p) return;
    p->has_rc = 1;
    p->rc = rc;
}

void ex_probe_limit(ex_probe *p, const char *fmt, ...) {
    if (!p) return;
    va_list ap;
    va_start(ap, fmt);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    vsnprintf(p->limitation, sizeof(p->limitation), fmt, ap);
#pragma clang diagnostic pop
    va_end(ap);
}

ex_event *ex_add_event(ex_run *run, ex_event_kind kind, uint64_t t_us, long long unix_s) {
    if (run->event_count >= EX_EVENTS_MAX) {
        run->events_truncated = 1;
        return NULL;
    }
    ex_event *e = &run->event[run->event_count++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->t_us = t_us;
    e->unix_s = unix_s;
    e->gap_ms = -1;
    return e;
}

/* ---- orchestration ---------------------------------------------------------------------------- */

void ex_execute(const ex_backend *be, const ex_options *opt, ex_run *run) {
    memset(run, 0, sizeof(*run));
    run->opt = *opt;
    if (run->opt.session_s == 0) run->opt.session_s = EX_SESSION_DEFAULT_S;
    if (run->opt.session_s > EX_SESSION_MAX_S) run->opt.session_s = EX_SESSION_MAX_S;
    if (run->opt.poll_ms == 0) run->opt.poll_ms = EX_POLL_DEFAULT_MS;
    snprintf(run->build_id, sizeof(run->build_id), "%s", AKN_EX_BUILD_ID);

    ex_module_process(be, run);
    ex_module_libraries(be, run);
    ex_module_audio(be, run);
    ex_module_shellui(run);
    ex_module_timeline(be, run);
}
