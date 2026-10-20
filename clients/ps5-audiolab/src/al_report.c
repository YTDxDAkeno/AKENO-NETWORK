/*
 * AKENO Audio Lab 3.1 reports. Everything is derived from measured values; nothing is invented.
 * There are no tokens, credentials, messages or audio samples in any report (the payload has no
 * network and captures nothing), so no redaction is needed. A tone that was accepted is never
 * reported as audible: audibility is only ever the owner's own "heard" verdict.
 */
#include "al.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

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

static void fmt(sink *k, const char *f, ...) __attribute__((format(printf, 2, 3)));
static void fmt(sink *k, const char *f, ...) {
    char tmp[1024];
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

static const char *heard_name(al_heard h) {
    return h == AL_HEARD_YES ? "yes" : h == AL_HEARD_NO ? "no" : "unreported";
}
static const char *paced_name(int p) { return p == 1 ? "paced" : p == 0 ? "faster-than-realtime" : "too-few-calls"; }

/* The one-line result class of a run (the evidence matrix of Part 4). */
static const char *matrix_class(const al_run *r) {
    switch (r->outcome) {
    case AL_OUT_NOT_RUN: return "not-run";
    case AL_OUT_NOT_SUPPORTED: return "audio-unavailable";
    case AL_OUT_INIT_FAILED: return "init-failed";
    case AL_OUT_OPEN_FAILED: return "port-open-failed";
    case AL_OUT_SUBMIT_FAILED: return "sample-output-failed";
    case AL_OUT_CANCELLED: return "cancelled";
    case AL_OUT_ACCEPTED:
        if (r->heard == AL_HEARD_YES) return "AUDIBLE (owner-confirmed)";
        if (r->paced == 0) return "accepted-but-not-paced";
        if (r->heard == AL_HEARD_NO) return "accepted+paced-but-inaudible";
        return "accepted (audibility unreported)";
    }
    return "unknown";
}

/* ---- port-state block (permanently disabled; retained as an explicit report field) ---------- */

static void port_state_json(sink *k, const al_run *r) {
    static const char *const names[AUDIO_LAB_STATE_POINTS] = { "before", "during", "beforeClose" };
    lit(k, "\"portState\":{");
    fmt(k, "\"enabled\":false,\"optIn\":false,\"exported\":false,\"stopped\":false,\"points\":[");
    for (int i = 0; i < AUDIO_LAB_STATE_POINTS; i++) {
        const audio_lab_state_sample *s = &r->result.port_state[i];
        if (i) lit(k, ",");
        lit(k, "{\"point\":\"");
        lit(k, names[i]);
        fmt(k, "\",\"attempted\":%s", s->attempted ? "true" : "false");
        lit(k, "}");
    }
    lit(k, "]}");
}

/* ---- report.json --------------------------------------------------------------------------- */

size_t al_report_json(const al_state *st, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    lit(&k, "{");
    fmt(&k, "\"report\":\"AKENO Audio Lab\",\"experimental\":true,\"version\":\"%s\",\"build\":\"%s\",",
        AL_VERSION, st->build_id);
    fmt(&k, "\"firmware\":\"%s\",\"pid\":%d,\"startedUnix\":%lld,", st->firmware[0] ? st->firmware : "unknown",
        st->pid, st->started_unix);
    fmt(&k, "\"portStateOptIn\":%s,\"portStateSupported\":%s,", st->port_state_opt_in ? "true" : "false",
        st->port_state_supported ? "true" : "false");
    fmt(&k, "\"tonesPlayed\":%u,", st->tones_played);
    lit(&k, "\"runs\":[");
    int first = 1;
    for (int id = st->next_run - 1, shown = 0; id >= 1 && shown < AL_RUNS_KEPT; id--) {
        const al_run *r = &st->runs[id % AL_RUNS_KEPT];
        if (r->id != id) continue;
        shown++;
        if (!first) lit(&k, ",");
        first = 0;
        lit(&k, "{");
        fmt(&k, "\"id\":%d,\"startedUnix\":%lld,\"preset\":\"%s\",\"level\":\"%s\",", r->id, r->started_unix,
            r->preset ? r->preset->id : "", r->level ? r->level->id : "");
        fmt(&k, "\"outcome\":\"%s\",\"class\":\"%s\",\"paced\":\"%s\",\"heard\":\"%s\",\"route\":\"",
            al_outcome_name(r->outcome), matrix_class(r), paced_name(r->paced), heard_name(r->heard));
        jstr(&k, r->route);
        lit(&k, "\",");
        fmt(&k, "\"calls\":%lu,\"callsOk\":%lu,\"framesSubmitted\":%lu,\"nonzeroFrames\":%lu,",
            r->result.calls, r->result.calls_ok, r->result.frames_submitted, r->result.nonzero_frames);
        fmt(&k, "\"openRc\":\"0x%08x\",\"lastOutputRc\":\"0x%08x\",\"closeCalled\":%s,\"closeRc\":\"0x%08x\",",
            (unsigned)r->result.open_rc, (unsigned)r->result.last_output_rc,
            r->result.close_called ? "true" : "false", (unsigned)r->result.close_rc);
        fmt(&k, "\"callMinUs\":%lld,\"callMaxUs\":%lld,\"wallUs\":%lld,\"bufferAligned\":%s,",
            r->result.call_min_us, r->result.call_max_us, r->result.wall_us, r->result.buffer_aligned ? "true" : "false");
        lit(&k, "\"error\":\"");
        jstr(&k, r->result.error);
        lit(&k, "\",");
        port_state_json(&k, r);
        lit(&k, "}");
    }
    lit(&k, "]}");
    return k.used;
}

/* ---- status.json (heartbeat) --------------------------------------------------------------- */

size_t al_report_status_json(const al_state *st, const char *state, long long now_unix, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    fmt(&k, "{\"report\":\"AKENO Audio Lab status\",\"version\":\"%s\",\"state\":\"%s\",", AL_VERSION, state);
    fmt(&k, "\"build\":\"%s\",\"pid\":%d,\"startedUnix\":%lld,\"heartbeatUnix\":%lld,", st->build_id, st->pid,
        st->started_unix, now_unix);
    fmt(&k, "\"elapsedS\":%lld,\"tonesPlayed\":%u,\"portStateOptIn\":false}",
        now_unix - st->started_unix, st->tones_played);
    return k.used;
}

/* ---- report.txt (German owner summary + result matrix + detail) ---------------------------- */

static void run_line_txt(sink *k, const al_run *r) {
    fmt(k, "  Lauf %d: %s, %s   [%s]\n", r->id, r->preset ? r->preset->label : "?",
        r->level ? r->level->label : "?", matrix_class(r));
    fmt(k, "    Ergebnis: %s | Takt: %s | gehoert: %s%s%s\n", al_outcome_name(r->outcome), paced_name(r->paced),
        heard_name(r->heard), r->route[0] ? " @ " : "", r->route);
    fmt(k, "    Aufrufe %lu (ok %lu), Frames %lu, Nonzero %lu, open=0x%08x lastOut=0x%08x close=0x%08x\n",
        r->result.calls, r->result.calls_ok, r->result.frames_submitted, r->result.nonzero_frames,
        (unsigned)r->result.open_rc, (unsigned)r->result.last_output_rc, (unsigned)r->result.close_rc);
    if (r->result.error[0]) fmt(k, "    Hinweis: %s\n", r->result.error);
    lit(k, "    Port-State: dauerhaft deaktiviert; keine Abfrage und keine Dekodierung.\n");
}

size_t al_report_txt(const al_state *st, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    lit(&k, "AKENO AUDIO LAB 3.1 - Diagnosebericht (Deutsch)\n");
    lit(&k, "================================================\n\n");
    fmt(&k, "Build:      %s\n", st->build_id);
    fmt(&k, "Firmware:   %s\n", st->firmware[0] ? st->firmware : "unbekannt");
    fmt(&k, "PID:        %d (eigenstaendiges Experiment-Payload, Hintergrundprozess)\n", st->pid);
    fmt(&k, "Verzeichnis: %s (ausschliesslich; kein /data/akeno, kein Netzwerk)\n", AL_OUT_DIR);
    fmt(&k, "Toene gespielt: %u | Port-State: dauerhaft deaktiviert\n\n", st->tones_played);

    lit(&k, "Was dieser Test zeigen kann und was nicht\n");
    lit(&k, "  - Das Ergebnis zeigt nur, ob dieses Payload seinen eigenen Audioport oeffnen, Daten\n");
    lit(&k, "    ausgeben und wieder schliessen konnte. Es ist kein Nachweis fuer Hoerbarkeit.\n");
    lit(&k, "  - Der Port-State-Aufruf bleibt deaktiviert und erscheint in diesem Bericht nicht als Messung.\n\n");

    if (st->next_run <= 1) {
        lit(&k, "Noch kein Ton gespielt: der automatische Einmaltest wurde vor der Audioausgabe abgebrochen.\n");
    } else {
        lit(&k, "Laeufe (neueste zuerst)\n");
        for (int id = st->next_run - 1, shown = 0; id >= 1 && shown < AL_RUNS_KEPT; id--) {
            const al_run *r = &st->runs[id % AL_RUNS_KEPT];
            if (r->id != id) continue;
            shown++;
            run_line_txt(&k, r);
        }
    }
    lit(&k, "\nErgebnisklassen: port-open-failed | sample-output-failed | accepted-but-not-paced |\n");
    lit(&k, "  accepted+paced-but-inaudible | AUDIBLE (owner-confirmed). 'gehoert' ist IMMER deine\n");
    lit(&k, "  eigene Angabe, nie aus einem Rueckgabewert abgeleitet.\n");
    return k.used;
}
