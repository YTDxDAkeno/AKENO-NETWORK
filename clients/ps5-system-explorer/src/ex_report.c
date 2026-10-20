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
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int n = vsnprintf(tmp, sizeof(tmp), f, ap);
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
    /* 6.1: session / recovery, foreground sessions and poll statistics. */
    const ex_session_info *si = &run->opt.session;
    lit(&k, "\"session\":{");
    jkv_s(&k, "id", si->id);
    lit(&k, ",");
    jkv_s(&k, "action", ex_instance_action_name(si->action));
    lit(&k, ",");
    jkv_s(&k, "lock", ex_lock_result_name(si->lock));
    lit(&k, ",");
    fmt(&k, "\"titleQueryEnabled\":%s,", si->title_query_enabled ? "true" : "false");
    fmt(&k, "\"staleControlBytes\":%lld,", si->stale_control_bytes);
    fmt(&k, "\"previousReportsMoved\":%s,", si->previous_reports_moved ? "true" : "false");
    fmt(&k, "\"superseded\":%s,", run->superseded ? "true" : "false");
    jkv_s(&k, "recovery", si->recovery);
    lit(&k, "},");
    lit(&k, "\"foregroundSessions\":[");
    for (size_t i = 0; i < run->fg_count && i < EX_FG_MAX; i++) {
        const ex_fg_session *f = &run->fg[i];
        if (i) lit(&k, ",");
        lit(&k, "{");
        fmt(&k, "\"appId\":\"0x%08x\",", (unsigned)f->app_id);
        fmt(&k, "\"startMs\":%llu,\"endMs\":%llu,", (unsigned long long)(f->start_us / 1000u),
            (unsigned long long)((f->open ? run->ended_us - run->started_us : f->end_us) / 1000u));
        jkv_s(&k, "titleStatus", ex_title_status_name(f->title_status));
        lit(&k, ",");
        if (f->title_id[0]) { jkv_s(&k, "titleId", f->title_id); lit(&k, ","); }
        jkv_s(&k, "endState", f->end == EX_FG_END_OPEN ? "still_running"
                              : f->end == EX_FG_END_CHANGED ? "replaced" : "not_reported");
        if (f->end == EX_FG_END_NOT_REPORTED) {
            lit(&k, ",");
            jkv_s(&k, "afterState", f->after == EX_FG_AFTER_STILL_ANSWERS ? "still_answers"
                                   : f->after == EX_FG_AFTER_GONE ? "gone" : "unknown");
        }
        lit(&k, "}");
    }
    lit(&k, "],");
    lit(&k, "\"poll\":{");
    fmt(&k, "\"polls\":%u,\"intervalMinMs\":%llu,\"intervalMaxMs\":%llu,\"latencyMaxUs\":%llu,"
            "\"homeSamples\":%u,\"userQueryFailures\":%u,\"titleCalls\":%u,\"titleFailures\":%u},",
        run->poll.polls, (unsigned long long)(run->poll.intervals ? run->poll.interval_min_us / 1000u : 0),
        (unsigned long long)(run->poll.interval_max_us / 1000u), (unsigned long long)run->poll.latency_max_us,
        run->poll.bigapp_none, run->poll.user_failures, run->poll.title_calls, run->poll.title_failures);
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
        if (e->kind == EX_EVT_CONTEXT_CHANGE) fmt(&k, "\"fromAppId\":\"0x%08x\",", (unsigned)e->from_app_id);
        if (e->title_id[0]) { jkv_s(&k, "titleId", e->title_id); lit(&k, ","); }
        if (e->source[0]) { jkv_s(&k, "markerSource", e->source); lit(&k, ","); }
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

/* Finds the value of a named Module A probe (for the German header). Returns "" if absent. */
static const char *probe_value(const ex_run *run, char module, const char *name) {
    for (size_t i = 0; i < run->probe_count && i < EX_PROBES_MAX; i++)
        if (run->probe[i].module == module && strcmp(run->probe[i].name, name) == 0)
            return run->probe[i].value;
    return "";
}

/* ---- the German owner header (6.1) ----------------------------------------------------------- */

static void summary_de_header(sink *k, const ex_run *run) {
    const ex_sysinfo *s = &run->sys;
    uint64_t total_us = run->ended_us - run->started_us;

    lit(k, "AKENO SYSTEM EXPLORER 6.1 - Zusammenfassung (Deutsch)\n");
    lit(k, "=====================================================\n\n");
    fmt(k, "Build:        %s\n", run->build_id);
    fmt(k, "Plattform:    %s\n", s->platform);
    fmt(k, "Firmware:     %s%s\n", s->firmware[0] ? s->firmware : "unbekannt",
        s->firmware_reviewed ? " (die Firmware der bisherigen Belege)" : "");
    fmt(k, "Explorer:     %s (PID %d), eigene App-ID %s\n", ex_context_name(run->context), s->pid,
        (s->have_appinfo && s->appinfo_rc == 0) ? "vorhanden" : "0 / keine (Hintergrund-Payload)");
    fmt(k, "Sitzung:      %s (%s)\n\n", run->opt.session.id, ex_instance_action_name(run->opt.session.action));

    lit(k, "Vordergrund-Anwendung\n");
    /* The first observed foreground session (if any). */
    const ex_fg_session *first = NULL;
    for (size_t i = 0; i < run->fg_count && i < EX_FG_MAX; i++)
        if (run->fg[i].app_id > 0) { first = &run->fg[i]; break; }
    if (first) {
        char when[8], dur[8];
        ex_fmt_mmss(first->start_us, when, sizeof(when));
        ex_fmt_mmss((first->open ? total_us : first->end_us) - first->start_us, dur, sizeof(dur));
        fmt(k, "  Runtime-App-ID:  0x%08x  (Laufzeit-ID, KEINE dauerhafte Title-ID)\n", (unsigned)first->app_id);
        if (first->title_status == EX_TITLE_OK)
            fmt(k, "  Title-ID:        %s (%s)\n", first->title_id, ex_title_class(first->title_id));
        else
            fmt(k, "  Title-ID:        nicht ermittelt (%s)\n", ex_title_status_name(first->title_status));
        fmt(k, "  Erkannt bei:     %s (mm:ss), Vordergrund aktiv ~%s\n", when, dur);
        if (first->end == EX_FG_END_NOT_REPORTED)
            fmt(k, "  Nach dem Ende:   %s\n",
                first->after == EX_FG_AFTER_STILL_ANSWERS ? "App-ID antwortet weiter (eher in den Hintergrund/Home)"
                : first->after == EX_FG_AFTER_GONE ? "App-ID antwortet nicht mehr (eher beendet)"
                : "nicht geprueft");
    } else {
        lit(k, "  keine Vordergrund-Anwendung beobachtet\n");
    }

    fmt(k, "\nSitzungsdauer:          %llu s%s\n", (unsigned long long)(total_us / 1000000u),
        run->stopped_early ? (run->superseded ? " (abgeloest)" : " (vom Nutzer beendet)") : " (Zeit abgelaufen)");
    fmt(k, "Ereignisse:             %zu  | Kontextwechsel: %u | Marker: %u (davon %u per Neustart)\n",
        run->event_count, run->context_changes, run->markers, run->markers_relaunch);
    fmt(k, "Scheduling-Luecken:     %u (max %d ms) - nur gemessen, NICHT als Suspendierung gewertet\n",
        run->sched_gaps, run->gap_max_ms);
    fmt(k, "Fehlende/fehlerhafte Abfragen: Prozesssatz=%s, Benutzerabfrage-Fehler=%u, Title-Fehler=%u\n",
        run->sys.pq.decoded ? "dekodiert" : "nicht verfuegbar (siehe process_record)", run->poll.user_failures,
        run->poll.title_failures);

    lit(k, "\nProzesskontext (entscheidend fuer Audio/Mikrofon)\n");
    fmt(k, "  Eigene App-ID:        %s\n", (s->have_appinfo && s->appinfo_rc == 0) ? "vorhanden" : "0 / keine");
    fmt(k, "  Vordergrund-App-ID:   %s\n", probe_value(run, 'A', "foreground_app"));
    lit(k, "  Audio-Berechtigung:   UNBEKANNT (nur lesend; kein Port geoeffnet)\n");
    lit(k, "  Mikrofon-Berechtigung: UNBEKANNT (nur lesend; kein Mikrofon geoeffnet)\n");

    if (run->opt.session.recovery[0]) fmt(k, "\nWiederanlauf: %s\n", run->opt.session.recovery);
    if (run->opt.session.stale_control_bytes >= 0)
        fmt(k, "Alte control-Datei (%lld Byte) wurde beiseitegelegt und NICHT ausgefuehrt.\n",
            run->opt.session.stale_control_bytes);

    lit(k, "\nRelevante Audio-Hypothese (unveraendert, quell-/hardwarebelegt)\n");
    lit(k, "  Bei byte-identischen Aufrufen entscheidet der Prozesstyp: ein Vordergrund-App-Prozess wird\n");
    lit(k, "  gehoert und bekommt das Mikrofon, ein Hintergrund-Payload nicht (fruehere AKENO-Hardware).\n");
    lit(k, "\nEmpfohlenes naechstes Experiment\n");
    lit(k, "  Audio Lab v3 (separater, bereits gebauter ELF): liest den Zustand des EIGENEN Audio-Ports\n");
    lit(k, "  read-only. Getrennte Freigabe/Hardwareplan noetig; vom Explorer NICHT automatisch gestartet.\n");
    lit(k, "  Details und Entscheidungsbaum: docs/PHASE6_1_PROCESS_AUDIO_RESEARCH_DE.md.\n");
    lit(k, "\n-----------------------------------------------------------------\n");
    lit(k, "Vollstaendige Probenliste (jede Messung mit Belegstufe und Grenze):\n\n");
}

size_t ex_report_summary_txt(const ex_run *run, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    const ex_sysinfo *s = &run->sys;
    summary_de_header(&k, run);
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

/* ---- status.json (6.1 heartbeat / crash recovery) -------------------------------------------- */

size_t ex_report_status_json(const ex_run *run, const char *state, long long now_unix, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    lit(&k, "{");
    jkv_s(&k, "report", "AKENO System Explorer status");
    lit(&k, ",");
    jkv_s(&k, "version", AKN_EX_VERSION);
    lit(&k, ",");
    jkv_s(&k, "state", state);
    lit(&k, ",");
    jkv_s(&k, "sessionId", run->opt.session.id);
    lit(&k, ",");
    jkv_s(&k, "build", run->build_id);
    lit(&k, ",");
    fmt(&k, "\"pid\":%d,", run->sys.pid);
    fmt(&k, "\"startedUnix\":%lld,", run->started_unix);
    fmt(&k, "\"heartbeatUnix\":%lld,", now_unix);
    fmt(&k, "\"elapsedS\":%llu,", (unsigned long long)((run->last_heartbeat_us - run->started_us) / 1000000u));
    fmt(&k, "\"requestedSessionS\":%u,", run->requested_session_s);
    fmt(&k, "\"events\":%zu,\"contextChanges\":%u,\"markers\":%u", run->event_count, run->context_changes,
        run->markers);
    lit(&k, "}");
    return k.used;
}
