/*
 * Module E: a low-overhead event timeline. It samples the live context at a bounded interval
 * (default 1 Hz), records context changes and user-entered markers, and notes scheduling gaps as
 * observations - never automatically as system suspension. The session is capped (default 5 min,
 * hard max 30 min) and shuts down cleanly on a stop marker or when the time is up.
 *
 * Markers and control come from the backend's read_control() (a control file the owner appends to;
 * the baseline uses no network). Commands, one per line:
 *   mark <text>      record a user marker (home screen, game, AKENO menu, party, ...)
 *   extend [min]     extend the session by <min> minutes (default 5), capped at the hard max
 *   stop             end the session cleanly now
 */
#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ex_redact.h"

/* Parse and apply one control line. Returns 1 if it was a stop command. */
static int apply_control_line(ex_run *run, const char *line, uint64_t t_us, long long unix_s,
                              int32_t bigapp, ex_context_kind ctx) {
    while (*line == ' ' || *line == '\t') line++;
    if (line[0] == '\0' || line[0] == '#') return 0;

    if (strncmp(line, "mark", 4) == 0 && (line[4] == ' ' || line[4] == '\0')) {
        const char *text = line + 4;
        while (*text == ' ' || *text == '\t') text++;
        ex_event *e = ex_add_event(run, EX_EVT_MARKER, t_us, unix_s);
        if (e) {
            e->bigapp_app_id = bigapp;
            e->context = ctx;
            ex_redact(e->text, sizeof(e->text), text[0] ? text : "(unnamed)");
        }
        run->markers++;
        return 0;
    }
    if (strncmp(line, "extend", 6) == 0 && (line[6] == ' ' || line[6] == '\0')) {
        int minutes = 5;
        if (line[6] == ' ') {
            int v = atoi(line + 7);
            if (v > 0) minutes = v;
        }
        unsigned add = (unsigned)minutes * 60u;
        unsigned target = run->requested_session_s + add;
        if (target > EX_SESSION_MAX_S) target = EX_SESSION_MAX_S;
        run->requested_session_s = target;
        ex_event *e = ex_add_event(run, EX_EVT_EXTEND, t_us, unix_s);
        if (e) {
            e->bigapp_app_id = bigapp;
            e->context = ctx;
            snprintf(e->text, sizeof(e->text), "session extended to %u s", target);
        }
        return 0;
    }
    if (strncmp(line, "stop", 4) == 0) {
        ex_event *e = ex_add_event(run, EX_EVT_STOP, t_us, unix_s);
        if (e) {
            e->bigapp_app_id = bigapp;
            e->context = ctx;
            snprintf(e->text, sizeof(e->text), "stop requested by the user");
        }
        run->stopped_early = 1;
        return 1;
    }
    return 0;
}

void ex_module_timeline(const ex_backend *be, ex_run *run) {
    run->started_us = be->now_us(be->user);
    run->started_unix = be->unix_time(be->user);
    run->requested_session_s = run->opt.session_s;
    unsigned poll_ms = run->opt.poll_ms;

    int32_t last_bigapp = 0, last_user = 0;
    be->sample_context(be->user, &last_bigapp, &last_user);
    ex_context_kind ctx = run->context;

    ex_event *start = ex_add_event(run, EX_EVT_START, 0, run->started_unix);
    if (start) {
        start->bigapp_app_id = last_bigapp;
        start->context = ctx;
        snprintf(start->text, sizeof(start->text), "session start (%s), up to %u s", be->name,
                 run->requested_session_s);
    }

    unsigned sample_every = 15000u / (poll_ms ? poll_ms : 1000u);
    if (sample_every == 0) sample_every = 1;
    unsigned polls = 0;

    char pending[512];
    size_t pending_len = 0;

    uint64_t prev_us = run->started_us;
    int stop = 0;

    while (!stop) {
        uint64_t now = be->now_us(be->user);
        uint64_t elapsed_us = now - run->started_us;
        if (elapsed_us >= (uint64_t)run->requested_session_s * 1000000u) break;

        /* A scheduling gap: the actual interval since the last poll, beyond a tolerance. */
        int gap_ms = (int)((now - prev_us) / 1000u);
        if (polls > 0 && gap_ms > (int)poll_ms * EX_GAP_FACTOR) {
            ex_event *e = ex_add_event(run, EX_EVT_SCHED_GAP, elapsed_us, be->unix_time(be->user));
            if (e) {
                e->bigapp_app_id = last_bigapp;
                e->context = ctx;
                e->gap_ms = gap_ms;
                snprintf(e->text, sizeof(e->text), "%d ms between polls (expected ~%u ms); recorded, not interpreted",
                         gap_ms, poll_ms);
            }
            run->sched_gaps++;
            if (gap_ms > run->gap_max_ms) run->gap_max_ms = gap_ms;
        }
        prev_us = now;

        /* Context sample. */
        int32_t bigapp = last_bigapp, user = last_user;
        be->sample_context(be->user, &bigapp, &user);
        int changed = bigapp != last_bigapp || user != last_user;
        if (changed) {
            ex_event *e = ex_add_event(run, EX_EVT_CONTEXT_CHANGE, elapsed_us, be->unix_time(be->user));
            if (e) {
                e->bigapp_app_id = bigapp;
                e->context = ctx;
                snprintf(e->text, sizeof(e->text), "running app 0x%08x->0x%08x, user 0x%08x->0x%08x",
                         (unsigned)last_bigapp, (unsigned)bigapp, (unsigned)last_user, (unsigned)user);
            }
            run->context_changes++;
            last_bigapp = bigapp;
            last_user = user;
        } else if (polls % sample_every == 0) {
            ex_event *e = ex_add_event(run, EX_EVT_SAMPLE, elapsed_us, be->unix_time(be->user));
            if (e) {
                e->bigapp_app_id = bigapp;
                e->context = ctx;
                snprintf(e->text, sizeof(e->text), "running app 0x%08x, user 0x%08x", (unsigned)bigapp,
                         (unsigned)user);
            }
        }

        /* Control / markers. */
        char buf[256];
        int n = be->read_control ? be->read_control(be->user, buf, sizeof(buf) - 1) : 0;
        for (int i = 0; i < n && !stop; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                pending[pending_len] = '\0';
                stop |= apply_control_line(run, pending, elapsed_us, be->unix_time(be->user), last_bigapp, ctx);
                pending_len = 0;
            } else if (pending_len < sizeof(pending) - 1) {
                pending[pending_len++] = c;
            }
        }

        polls++;
        if (!stop) be->sleep_ms(be->user, poll_ms);
    }

    /* A trailing control line without a newline. */
    if (pending_len > 0 && !stop) {
        pending[pending_len] = '\0';
        uint64_t elapsed_us = be->now_us(be->user) - run->started_us;
        apply_control_line(run, pending, elapsed_us, be->unix_time(be->user), last_bigapp, ctx);
    }

    run->ended_us = be->now_us(be->user);
    uint64_t total_us = run->ended_us - run->started_us;
    ex_event *end = ex_add_event(run, EX_EVT_STOP, total_us, be->unix_time(be->user));
    if (end) {
        end->bigapp_app_id = last_bigapp;
        end->context = ctx;
        snprintf(end->text, sizeof(end->text), "session end after %llu s (%s)",
                 (unsigned long long)(total_us / 1000000u), run->stopped_early ? "stopped by user" : "time up");
    }

    ex_probe *p = ex_add_probe(run, 'E', "timeline", EX_ST_OK,
                               run->context == EX_CTX_DESKTOP ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
    ex_probe_value(p, "%zu events in %llu s: %u context changes, %u markers, %u scheduling gaps (max %d ms)",
                   run->event_count, (unsigned long long)(total_us / 1000000u), run->context_changes,
                   run->markers, run->sched_gaps, run->gap_max_ms);
    ex_probe_limit(p, "A scheduling gap is the measured interval between polls; it is not interpreted "
                      "as system suspension.");
}
