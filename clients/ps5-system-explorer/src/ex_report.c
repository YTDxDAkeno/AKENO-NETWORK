/*
 * Module F: structured, machine-readable reports. Everything here is derived from the recorded
 * probes and events; nothing is invented. A missing measurement is written as its explicit status
 * (NOT_AVAILABLE / UNSUPPORTED_ON_FW_12_20 / SKIPPED), never as a fake value. All free text has
 * already been redacted at record time; the JSON writer also escapes it.
 */
#include "ex.h"

#include <stdio.h>
#include <string.h>

/* ---- a tiny append-only text sink ------------------------------------------------------------ */

typedef struct {
    char *out;
    size_t len, used;
} sink;

static void raw(sink *k, const char *s, size_t n) {
    if (k->len == 0) return;
    size_t room = k->len - 1 - k->used;
    if (n > room) n = room;
    memcpy(k->out + k->used, s, n);
    k->used += n;
    k->out[k->used] = '\0';
}

static void lit(sink *k, const char *s) { raw(k, s, strlen(s)); }

#include <stdarg.h>
static void fmt(sink *k, const char *f, ...) __attribute__((format(printf, 2, 3)));
static void fmt(sink *k, const char *f, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, f);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
    int n = vsnprintf(tmp, sizeof(tmp), f, ap);
#pragma clang diagnostic pop
    va_end(ap);
    if (n < 0) return;
    raw(k, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

/* JSON string content (quotes added by the caller). */
static void jstr(sink *k, const char *s) {
    if (!s) s = "";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': lit(k, "\\\""); break;
        case '\\': lit(k, "\\\\"); break;
        case '\n': lit(k, "\\n"); break;
        case '\r': lit(k, "\\r"); break;
        case '\t': lit(k, "\\t"); break;
        default:
            if (*p < 0x20) fmt(k, "\\u%04x", *p);
            else raw(k, (const char *)p, 1);
        }
    }
}

static void jkv_s(sink *k, const char *key, const char *val) {
    fmt(k, "\"%s\":\"", key);
    jstr(k, val);
    lit(k, "\"");
}

/* ---- shared fragments ------------------------------------------------------------------------ */

static void probe_json(sink *k, const ex_probe *p) {
    char mod[2] = { p->module, 0 };
    lit(k, "{");
    jkv_s(k, "module", mod);
    lit(k, ",");
    jkv_s(k, "name", p->name);
    lit(k, ",");
    jkv_s(k, "status", ex_status_name(p->status));
    lit(k, ",");
    jkv_s(k, "evidence", ex_evidence_name(p->evidence));
    lit(k, ",");
    if (p->has_rc) fmt(k, "\"rc\":\"0x%08x\",", (unsigned)p->rc);
    else lit(k, "\"rc\":null,");
    jkv_s(k, "value", p->value);
    lit(k, ",");
    jkv_s(k, "limitation", p->limitation);
    lit(k, "}");
}

static void sysinfo_json(sink *k, const ex_run *run) {
    const ex_sysinfo *s = &run->sys;
    lit(k, "{");
    jkv_s(k, "platform", s->platform);
    lit(k, ",");
    jkv_s(k, "firmware", s->firmware[0] ? s->firmware : "unknown");
    lit(k, ",");
    fmt(k, "\"firmwareRaw\":\"0x%08x\",", s->firmware_raw);
    fmt(k, "\"pid\":%d,\"ppid\":%d,", s->pid, s->ppid);
    jkv_s(k, "comm", s->comm);
    lit(k, ",");
    jkv_s(k, "context", ex_context_name(run->context));
    lit(k, ",");
    if (s->have_appinfo && s->appinfo_rc == 0)
        fmt(k, "\"ownAppId\":\"0x%08x\",", s->own_app_id);
    else
        lit(k, "\"ownAppId\":null,");
    jkv_s(k, "ownTitleId", s->own_title_id);
    lit(k, ",");
    fmt(k, "\"bigAppId\":\"0x%08x\",", (unsigned)s->bigapp_app_id);
    if (s->have_user && s->user_rc == 0) fmt(k, "\"foregroundUser\":\"0x%08x\"", s->foreground_user);
    else lit(k, "\"foregroundUser\":null");
    lit(k, "}");
}

static void counts_by_module(const ex_run *run, char module, int *ok, int *na, int *unsup, int *skip, int *err) {
    *ok = *na = *unsup = *skip = *err = 0;
    for (size_t i = 0; i < run->probe_count && i < EX_PROBES_MAX; i++) {
        if (run->probe[i].module != module) continue;
        switch (run->probe[i].status) {
        case EX_ST_OK: (*ok)++; break;
        case EX_ST_NOT_AVAILABLE: (*na)++; break;
        case EX_ST_UNSUPPORTED_FW: (*unsup)++; break;
        case EX_ST_SKIPPED: (*skip)++; break;
        case EX_ST_ERROR: (*err)++; break;
        }
    }
}

/* ---- report.json ----------------------------------------------------------------------------- */

size_t ex_report_json(const ex_run *run, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    lit(&k, "{");
    jkv_s(&k, "report", "AKENO System Explorer");
    lit(&k, ",");
    jkv_s(&k, "version", AKN_EX_VERSION);
    lit(&k, ",");
    jkv_s(&k, "build", run->build_id);
    lit(&k, ",");
    fmt(&k, "\"startedUnix\":%lld,", run->started_unix);
    fmt(&k, "\"durationS\":%llu,", (unsigned long long)((run->ended_us - run->started_us) / 1000000u));
    lit(&k, "\"system\":");
    sysinfo_json(&k, run);
    lit(&k, ",\"timeline\":{");
    fmt(&k, "\"events\":%zu,\"truncated\":%s,\"contextChanges\":%u,\"markers\":%u,\"schedGaps\":%u,\"gapMaxMs\":%d,"
            "\"requestedSessionS\":%u,\"stoppedEarly\":%s},",
        run->event_count, run->events_truncated ? "true" : "false", run->context_changes, run->markers,
        run->sched_gaps, run->gap_max_ms, run->requested_session_s, run->stopped_early ? "true" : "false");
    lit(&k, "\"moduleSummary\":{");
    const char mods[] = { 'A', 'B', 'C', 'D', 'E' };
    for (size_t m = 0; m < sizeof(mods); m++) {
        int ok, na, unsup, skip, err;
        counts_by_module(run, mods[m], &ok, &na, &unsup, &skip, &err);
        char key[2] = { mods[m], 0 };
        fmt(&k, "%s\"%s\":{\"ok\":%d,\"notAvailable\":%d,\"unsupported\":%d,\"skipped\":%d,\"error\":%d}",
            m ? "," : "", key, ok, na, unsup, skip, err);
    }
    lit(&k, "},\"probes\":[");
    for (size_t i = 0; i < run->probe_count && i < EX_PROBES_MAX; i++) {
        if (i) lit(&k, ",");
        probe_json(&k, &run->probe[i]);
    }
    if (run->probe_count > EX_PROBES_MAX)
        fmt(&k, "%s{\"module\":\"-\",\"name\":\"(truncated)\",\"status\":\"SKIPPED\",\"evidence\":\"UNKNOWN\","
                "\"rc\":null,\"value\":\"%zu probes exceeded the buffer\",\"limitation\":\"\"}",
            run->probe_count ? "," : "", run->probe_count - EX_PROBES_MAX);
    lit(&k, "]}");
    return k.used;
}

/* ---- modules.json ---------------------------------------------------------------------------- */

size_t ex_report_modules_json(const ex_run *run, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    static const struct { char id; const char *title; } modules[] = {
        { 'A', "Process and application environment" },
        { 'B', "System libraries and exported symbols" },
        { 'C', "Audio and microphone diagnostics (read-only)" },
        { 'D', "ShellUI and Game Base research" },
        { 'E', "Event timeline" },
    };
    lit(&k, "{");
    jkv_s(&k, "version", AKN_EX_VERSION);
    lit(&k, ",\"modules\":[");
    for (size_t m = 0; m < sizeof(modules) / sizeof(modules[0]); m++) {
        if (m) lit(&k, ",");
        char id[2] = { modules[m].id, 0 };
        lit(&k, "{");
        jkv_s(&k, "id", id);
        lit(&k, ",");
        jkv_s(&k, "title", modules[m].title);
        lit(&k, ",\"probes\":[");
        int first = 1;
        for (size_t i = 0; i < run->probe_count && i < EX_PROBES_MAX; i++) {
            if (run->probe[i].module != modules[m].id) continue;
            if (!first) lit(&k, ",");
            first = 0;
            probe_json(&k, &run->probe[i]);
        }
        lit(&k, "]}");
    }
    lit(&k, "]}");
    return k.used;
}

/* ---- audio-diagnostics.json ------------------------------------------------------------------ */

size_t ex_report_audio_json(const ex_run *run, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    lit(&k, "{");
    jkv_s(&k, "version", AKN_EX_VERSION);
    lit(&k, ",");
    jkv_s(&k, "note", "read-only baseline: no audio port and no microphone were opened");
    lit(&k, ",");
    jkv_s(&k, "context", ex_context_name(run->context));
    lit(&k, ",\"probes\":[");
    int first = 1;
    for (size_t i = 0; i < run->probe_count && i < EX_PROBES_MAX; i++) {
        if (run->probe[i].module != 'C') continue;
        if (!first) lit(&k, ",");
        first = 0;
        probe_json(&k, &run->probe[i]);
    }
    lit(&k, "]}");
    return k.used;
}

/* ---- events.jsonl ---------------------------------------------------------------------------- */

size_t ex_report_events_jsonl(const ex_run *run, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    size_t n = run->event_count < EX_EVENTS_MAX ? run->event_count : EX_EVENTS_MAX;
    for (size_t i = 0; i < n; i++) {
        const ex_event *e = &run->event[i];
        lit(&k, "{");
        fmt(&k, "\"tMs\":%llu,", (unsigned long long)(e->t_us / 1000u));
        fmt(&k, "\"unix\":%lld,", e->unix_s);
        jkv_s(&k, "kind", ex_event_kind_name(e->kind));
        lit(&k, ",");
        jkv_s(&k, "context", ex_context_name(e->context));
        lit(&k, ",");
        fmt(&k, "\"bigAppId\":\"0x%08x\",", (unsigned)e->bigapp_app_id);
        if (e->gap_ms >= 0) fmt(&k, "\"gapMs\":%d,", e->gap_ms);
        jkv_s(&k, "text", e->text);
        lit(&k, "}\n");
    }
    return k.used;
}

/* ---- system-summary.txt ---------------------------------------------------------------------- */

static const char *st_mark(ex_status s) {
    switch (s) {
    case EX_ST_OK: return "[ok]   ";
    case EX_ST_NOT_AVAILABLE: return "[n/a]  ";
    case EX_ST_UNSUPPORTED_FW: return "[unsup]";
    case EX_ST_SKIPPED: return "[skip] ";
    case EX_ST_ERROR: return "[err]  ";
    }
    return "[?]    ";
}

size_t ex_report_summary_txt(const ex_run *run, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    const ex_sysinfo *s = &run->sys;
    fmt(&k, "AKENO System Explorer %s (build %s)\n", AKN_EX_VERSION, run->build_id);
    fmt(&k, "Platform: %s   Firmware: %s   Context: %s\n", s->platform,
        s->firmware[0] ? s->firmware : "unknown", ex_context_name(run->context));
    fmt(&k, "PID %d (ppid %d), comm '%s'\n", s->pid, s->ppid, s->comm);
    fmt(&k, "Session: %llu s, %zu events, %u context changes, %u markers, %u scheduling gaps (max %d ms)%s\n\n",
        (unsigned long long)((run->ended_us - run->started_us) / 1000000u), run->event_count,
        run->context_changes, run->markers, run->sched_gaps, run->gap_max_ms,
        run->events_truncated ? " [events truncated]" : "");

    static const struct { char id; const char *title; } modules[] = {
        { 'A', "Process and application environment" },
        { 'B', "System libraries and exported symbols" },
        { 'C', "Audio and microphone diagnostics (read-only)" },
        { 'D', "ShellUI and Game Base research" },
        { 'E', "Event timeline" },
    };
    for (size_t m = 0; m < sizeof(modules) / sizeof(modules[0]); m++) {
        fmt(&k, "== Module %c: %s ==\n", modules[m].id, modules[m].title);
        for (size_t i = 0; i < run->probe_count && i < EX_PROBES_MAX; i++) {
            const ex_probe *p = &run->probe[i];
            if (p->module != modules[m].id) continue;
            fmt(&k, "  %s %-26s %-18s %s\n", st_mark(p->status), p->name, ex_evidence_name(p->evidence),
                p->value);
            if (p->limitation[0]) fmt(&k, "                 limit: %s\n", p->limitation);
        }
        lit(&k, "\n");
    }
    lit(&k, "Legend: [ok] measured  [n/a] not available  [unsup] not on FW 12.20  [skip] not run  [err] error code\n");
    lit(&k, "A successful call or a resolved symbol is never proof of working sound, voice or Game Base access.\n");
    return k.used;
}
