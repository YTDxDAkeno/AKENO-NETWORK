/*
 * Module E: a low-overhead event timeline (6.1). It samples the live context at a bounded interval
 * (default 1 Hz), records context changes, user markers, scheduling gaps (as observations, never as
 * suspension) and - new in 6.1 - foreground-application sessions with their verified title IDs,
 * poll-latency statistics, and a periodic heartbeat (status.json) through the checkpoint callback.
 *
 * Markers and control come from the backend's read_control() (a control file the owner appends to;
 * the baseline uses no network). Commands, one per line:
 *   mark <text>      record a user marker (home screen, game, AKENO menu, party, ...)
 *   extend [min]     extend the session by <min> minutes (default 5), capped at the hard max
 *   stop             end the session cleanly now
 *
 * A relaunched Explorer that found a live session hands over its marker through the same control
 * file (main appends "mark <text>"), so marking never needs a second diagnostic session.
 */
#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ex_redact.h"

/* ---- foreground-session bookkeeping ---------------------------------------------------------- */

static ex_fg_session *fg_current(ex_run *run) {
    if (run->fg_count == 0) return NULL;
    ex_fg_session *f = &run->fg[run->fg_count - 1 < EX_FG_MAX ? run->fg_count - 1 : EX_FG_MAX - 1];
    return f->open ? f : NULL;
}

static void fg_open(ex_run *run, int32_t app_id, uint64_t t_us) {
    if (run->fg_count >= EX_FG_MAX) {
        run->fg_truncated = 1;
        return;
    }
    ex_fg_session *f = &run->fg[run->fg_count++];
    memset(f, 0, sizeof(*f));
    f->app_id = app_id;
    f->start_us = t_us;
    f->open = 1;
    f->end = EX_FG_END_OPEN;
}

static void fg_close(ex_fg_session *f, uint64_t t_us, ex_fg_end end) {
    if (!f || !f->open) return;
    f->end_us = t_us;
    f->end = end;
    f->open = 0;
}

/* ---- control parsing ------------------------------------------------------------------------- */

static int apply_control_line(ex_run *run, const char *line, uint64_t t_us, long long unix_s,
                              int32_t bigapp, ex_context_kind ctx, const char *source) {
    while (*line == ' ' || *line == '\t') line++;
    if (line[0] == '\0' || line[0] == '#') return 0;

    if (strncmp(line, "mark", 4) == 0 && (line[4] == ' ' || line[4] == '\0')) {
        const char *text = line + 4;
        while (*text == ' ' || *text == '\t') text++;
        ex_event *e = ex_add_event(run, EX_EVT_MARKER, t_us, unix_s);
        if (e) {
            e->bigapp_app_id = bigapp;
            e->context = ctx;
            snprintf(e->source, sizeof(e->source), "%s", source);
            ex_redact(e->text, sizeof(e->text), text[0] ? text : "(unnamed)");
        }
        run->markers++;
        if (strcmp(source, "relaunch") == 0) run->markers_relaunch++;
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

/* ---- title lookups during the session -------------------------------------------------------- */

/* Looks up the title ID for a running app id and records it on the open foreground session and as a
 * title event. Honours the owner's switch, the per-session budget and the overrun guard-stop. */
static void lookup_title(const ex_backend *be, ex_run *run, int32_t app_id, uint64_t t_us, long long unix_s) {
    ex_fg_session *f = fg_current(run);
    char title[16] = { 0 };
    ex_title_status ts;
    ex_title_query q;

    if (!run->opt.session.title_query_enabled) ts = EX_TITLE_DISABLED;
    else if (run->title_guard_stop) ts = EX_TITLE_GUARD_STOP;
    else if (run->poll.title_calls >= EX_TITLE_BUDGET_MAX) ts = EX_TITLE_BUDGET;
    else if (app_id <= 0) ts = EX_TITLE_NO_APP;
    else {
        be->query_title(be->user, app_id, &q);
        run->poll.title_calls++;
        if (q.called && !q.guard_ok) run->title_guard_stop = 1;
        ts = ex_title_classify(&q, title, sizeof(title));
    }
    if (ts != EX_TITLE_OK) run->poll.title_failures++;

    if (f && f->app_id == app_id) {
        f->title_status = ts;
        if (ts == EX_TITLE_OK) snprintf(f->title_id, sizeof(f->title_id), "%s", title);
        else if (ts == EX_TITLE_ERROR) f->title_rc = q.rc;
    }
    ex_event *e = ex_add_event(run, EX_EVT_TITLE, t_us, unix_s);
    if (e) {
        e->bigapp_app_id = app_id;
        snprintf(e->title_id, sizeof(e->title_id), "%s", title);
        if (ts == EX_TITLE_OK)
            snprintf(e->text, sizeof(e->text), "app 0x%08x title id %s (%s)", (unsigned)app_id, title,
                     ex_title_class(title));
        else
            snprintf(e->text, sizeof(e->text), "app 0x%08x title id %s", (unsigned)app_id,
                     ex_title_status_name(ts));
    }
}

/* ---- poll statistics ------------------------------------------------------------------------- */

static void stat_interval(ex_poll_stats *st, uint64_t dt_us) {
    if (st->intervals == 0 || dt_us < st->interval_min_us) st->interval_min_us = dt_us;
    if (dt_us > st->interval_max_us) st->interval_max_us = dt_us;
    st->interval_sum_us += dt_us;
    st->intervals++;
}

static void stat_latency(ex_poll_stats *st, uint64_t dt_us) {
    if (st->polls == 0 || dt_us < st->latency_min_us) st->latency_min_us = dt_us;
    if (dt_us > st->latency_max_us) st->latency_max_us = dt_us;
    st->latency_sum_us += dt_us;
}

/* ---- the session ----------------------------------------------------------------------------- */

static void heartbeat(const ex_backend *be, ex_run *run, uint64_t now, int final) {
    (void)be;
    if (!run->opt.checkpoint) return;
    if (run->opt.checkpoint(run->opt.checkpoint_ctx, run, final) != 0) {
        run->superseded = 1;
        run->stopped_early = 1;
    }
    run->last_heartbeat_us = now;
}

void ex_module_timeline(const ex_backend *be, ex_run *run) {
    run->started_us = be->now_us(be->user);
    run->started_unix = be->unix_time(be->user);
    run->requested_session_s = run->opt.session_s;
    unsigned poll_ms = run->opt.poll_ms;
    ex_context_kind ctx = run->context;

    ex_ctx_sample sm;
    memset(&sm, 0, sizeof(sm));
    be->sample_context(be->user, &sm);
    int32_t last_bigapp = sm.bigapp, last_user = (sm.have_user_fn && sm.user_rc == 0) ? (int32_t)sm.user : 0;

    ex_event *start = ex_add_event(run, EX_EVT_START, 0, run->started_unix);
    if (start) {
        start->bigapp_app_id = last_bigapp;
        start->context = ctx;
        snprintf(start->text, sizeof(start->text), "session %s start (%s), up to %u s",
                 run->opt.session.id, be->name, run->requested_session_s);
    }
    if (last_bigapp > 0) {
        fg_open(run, last_bigapp, 0);
        lookup_title(be, run, last_bigapp, 0, run->started_unix);
    }

    heartbeat(be, run, run->started_us, 0);

    unsigned sample_every = 15000u / (poll_ms ? poll_ms : 1000u);
    if (sample_every == 0) sample_every = 1;
    unsigned polls = 0;
    unsigned since_recheck = 0;

    char pending[512];
    size_t pending_len = 0;

    uint64_t prev_us = run->started_us;
    uint64_t last_hb_us = run->started_us;
    int stop = 0;

    while (!stop) {
        uint64_t now = be->now_us(be->user);
        uint64_t elapsed_us = now - run->started_us;
        if (elapsed_us >= (uint64_t)run->requested_session_s * 1000000u) break;

        int gap_ms = (int)((now - prev_us) / 1000u);
        if (polls > 0) {
            stat_interval(&run->poll, now - prev_us);
            if (gap_ms > (int)poll_ms * EX_GAP_FACTOR) {
                ex_event *e = ex_add_event(run, EX_EVT_SCHED_GAP, elapsed_us, be->unix_time(be->user));
                if (e) {
                    e->bigapp_app_id = last_bigapp;
                    e->context = ctx;
                    e->gap_ms = gap_ms;
                    snprintf(e->text, sizeof(e->text),
                             "%d ms between polls (expected ~%u ms); recorded, not interpreted", gap_ms, poll_ms);
                }
                run->sched_gaps++;
                if (gap_ms > run->gap_max_ms) run->gap_max_ms = gap_ms;
            }
        }
        prev_us = now;

        /* Context sample, timed. */
        uint64_t t0 = be->now_us(be->user);
        be->sample_context(be->user, &sm);
        uint64_t t1 = be->now_us(be->user);
        stat_latency(&run->poll, t1 - t0);
        run->poll.polls++;
        int32_t bigapp = sm.bigapp;
        int32_t user = (sm.have_user_fn && sm.user_rc == 0) ? (int32_t)sm.user : last_user;
        if (!sm.have_bigapp_fn) run->poll.bigapp_unavailable++;
        else if (bigapp <= 0) {
            run->poll.bigapp_none++;
            run->poll.bigapp_last_none_raw = bigapp;
        }
        if (sm.have_user_fn && sm.user_rc != 0) {
            run->poll.user_failures++;
            run->poll.user_last_rc = sm.user_rc;
        }

        int changed = bigapp != last_bigapp || user != last_user;
        if (changed) {
            ex_event *e = ex_add_event(run, EX_EVT_CONTEXT_CHANGE, elapsed_us, be->unix_time(be->user));
            if (e) {
                e->from_app_id = last_bigapp;
                e->bigapp_app_id = bigapp;
                e->context = ctx;
                snprintf(e->text, sizeof(e->text), "running app 0x%08x->0x%08x, user 0x%08x->0x%08x",
                         (unsigned)last_bigapp, (unsigned)bigapp, (unsigned)last_user, (unsigned)user);
            }
            run->context_changes++;

            if (bigapp != last_bigapp) {
                ex_fg_session *cur = fg_current(run);
                if (cur) fg_close(cur, elapsed_us, bigapp > 0 ? EX_FG_END_CHANGED : EX_FG_END_NOT_REPORTED);
                if (bigapp > 0) {
                    fg_open(run, bigapp, elapsed_us);
                    lookup_title(be, run, bigapp, elapsed_us, be->unix_time(be->user));
                    since_recheck = 0;
                }
            }
            last_bigapp = bigapp;
            last_user = user;
        } else if (polls % sample_every == 0) {
            ex_event *e = ex_add_event(run, EX_EVT_SAMPLE, elapsed_us, be->unix_time(be->user));
            if (e) {
                e->bigapp_app_id = bigapp;
                e->context = ctx;
                ex_fg_session *cur = fg_current(run);
                if (cur) snprintf(e->title_id, sizeof(e->title_id), "%s", cur->title_id);
                snprintf(e->text, sizeof(e->text), "running app 0x%08x, user 0x%08x", (unsigned)bigapp,
                         (unsigned)user);
            }
        }

        /* After the running app vanished, re-ask (within budget) whether its id still answers, so
         * "it ended" and "it moved to the background / home screen" can be told apart. */
        if (bigapp <= 0 && run->fg_count > 0) {
            ex_fg_session *last_fg = &run->fg[run->fg_count - 1 < EX_FG_MAX ? run->fg_count - 1 : EX_FG_MAX - 1];
            if (!last_fg->open && last_fg->end == EX_FG_END_NOT_REPORTED && last_fg->after == EX_FG_AFTER_UNKNOWN &&
                last_fg->app_id > 0 && run->opt.session.title_query_enabled && !run->title_guard_stop &&
                run->poll.title_calls < EX_TITLE_BUDGET_MAX && ++since_recheck >= EX_TITLE_RECHECK_POLLS) {
                since_recheck = 0;
                ex_title_query q;
                be->query_title(be->user, last_fg->app_id, &q);
                run->poll.title_calls++;
                if (q.called && !q.guard_ok) run->title_guard_stop = 1;
                char t[16];
                ex_title_status ts = ex_title_classify(&q, t, sizeof(t));
                int same = ts == EX_TITLE_OK && last_fg->title_id[0] && strcmp(t, last_fg->title_id) == 0;
                last_fg->after = same ? EX_FG_AFTER_STILL_ANSWERS : EX_FG_AFTER_GONE;
                last_fg->after_us = elapsed_us;
                ex_event *e = ex_add_event(run, EX_EVT_APP_STATE, elapsed_us, be->unix_time(be->user));
                if (e) {
                    e->bigapp_app_id = last_fg->app_id;
                    snprintf(e->title_id, sizeof(e->title_id), "%s", last_fg->title_id);
                    snprintf(e->text, sizeof(e->text), "app 0x%08x %s after it left the foreground",
                             (unsigned)last_fg->app_id,
                             same ? "still answers GetAppTitleId" : "no longer answers GetAppTitleId");
                }
            }
        }

        /* Control / markers. */
        int reset = 0;
        char buf[256];
        int n = be->read_control ? be->read_control(be->user, buf, sizeof(buf) - 1, &reset) : 0;
        if (reset) {
            run->control_resets++;
            pending_len = 0;
            ex_event *e = ex_add_event(run, EX_EVT_CONTROL, elapsed_us, be->unix_time(be->user));
            if (e) {
                e->bigapp_app_id = last_bigapp;
                e->context = ctx;
                snprintf(e->text, sizeof(e->text), "control file was replaced or truncated; reading restarted");
            }
        }
        for (int i = 0; i < n && !stop; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                pending[pending_len] = '\0';
                stop |= apply_control_line(run, pending, elapsed_us, be->unix_time(be->user), last_bigapp, ctx,
                                           "control");
                pending_len = 0;
            } else if (pending_len < sizeof(pending) - 1) {
                pending[pending_len++] = c;
            }
        }

        /* Heartbeat / takeover check. */
        if (now - last_hb_us >= (uint64_t)EX_HEARTBEAT_S * 1000000u) {
            heartbeat(be, run, now, 0);
            last_hb_us = now;
            if (run->superseded) {
                stop = 1;
                ex_event *e = ex_add_event(run, EX_EVT_STOP, elapsed_us, be->unix_time(be->user));
                if (e) {
                    e->bigapp_app_id = last_bigapp;
                    e->context = ctx;
                    snprintf(e->text, sizeof(e->text), "another session took over the lock; ending");
                }
            }
        }

        polls++;
        if (!stop) be->sleep_ms(be->user, poll_ms);
    }

    /* A trailing control line that has settled (no newline) is applied once, and noted. */
    if (pending_len > 0 && !stop) {
        pending[pending_len] = '\0';
        uint64_t elapsed_us = be->now_us(be->user) - run->started_us;
        run->control_partial++;
        apply_control_line(run, pending, elapsed_us, be->unix_time(be->user), last_bigapp, ctx, "control");
    }

    run->ended_us = be->now_us(be->user);
    uint64_t total_us = run->ended_us - run->started_us;
    ex_fg_session *cur = fg_current(run);
    if (cur) fg_close(cur, total_us, EX_FG_END_OPEN);

    be->read_usage(be->user, &run->usage_end);
    if (run->usage_end.have && run->usage_end.maxrss_bytes > run->maxrss_seen)
        run->maxrss_seen = run->usage_end.maxrss_bytes;

    ex_event *end = ex_add_event(run, EX_EVT_STOP, total_us, be->unix_time(be->user));
    if (end) {
        end->bigapp_app_id = last_bigapp;
        end->context = ctx;
        snprintf(end->text, sizeof(end->text), "session end after %llu s (%s)",
                 (unsigned long long)(total_us / 1000000u),
                 run->superseded ? "superseded" : run->stopped_early ? "stopped by user" : "time up");
    }

    heartbeat(be, run, run->ended_us, 1);

    ex_probe *p = ex_add_probe(run, 'E', "timeline", EX_ST_OK,
                               run->context == EX_CTX_DESKTOP ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
    ex_probe_value(p, "%zu events in %llu s: %u context changes, %u markers (%u via relaunch), %u scheduling "
                      "gaps (max %d ms), %zu foreground session(s)",
                   run->event_count, (unsigned long long)(total_us / 1000000u), run->context_changes,
                   run->markers, run->markers_relaunch, run->sched_gaps, run->gap_max_ms, run->fg_count);
    ex_probe_limit(p, "A scheduling gap is the measured interval between polls; it is not interpreted "
                      "as system suspension.");

    /* 6.1: poll latency and foreground-time summary. */
    p = ex_add_probe(run, 'E', "poll_timing", run->poll.intervals ? EX_ST_OK : EX_ST_NOT_AVAILABLE,
                     run->context == EX_CTX_DESKTOP ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
    if (run->poll.intervals)
        ex_probe_value(p, "%u polls; interval min/avg/max %llu/%llu/%llu ms; query latency min/avg/max %llu/%llu/%llu us; "
                          "%u home/none samples, %u user-query failures (last rc 0x%08x)",
                       run->poll.polls, (unsigned long long)(run->poll.interval_min_us / 1000u),
                       (unsigned long long)(run->poll.interval_sum_us / run->poll.intervals / 1000u),
                       (unsigned long long)(run->poll.interval_max_us / 1000u),
                       (unsigned long long)run->poll.latency_min_us,
                       (unsigned long long)(run->poll.polls ? run->poll.latency_sum_us / run->poll.polls : 0),
                       (unsigned long long)run->poll.latency_max_us, run->poll.bigapp_none,
                       run->poll.user_failures, (unsigned)run->poll.user_last_rc);
    else
        ex_probe_value(p, "no completed poll interval");
    ex_probe_limit(p, "Latencies of read-only context queries; they do not measure the whole process's scheduling.");

    uint64_t fg_total = 0;
    uint64_t first_fg = 0;
    int have_first = 0;
    for (size_t i = 0; i < run->fg_count && i < EX_FG_MAX; i++) {
        uint64_t s0 = run->fg[i].start_us, e0 = run->fg[i].open ? total_us : run->fg[i].end_us;
        if (e0 > s0) fg_total += e0 - s0;
        if (run->fg[i].app_id > 0 && !have_first) {
            first_fg = run->fg[i].start_us;
            have_first = 1;
        }
    }
    p = ex_add_probe(run, 'E', "foreground_time", run->fg_count ? EX_ST_OK : EX_ST_NOT_AVAILABLE,
                     run->context == EX_CTX_DESKTOP ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
    if (run->fg_count) {
        char when[8] = "00:00", dur[8];
        if (have_first) ex_fmt_mmss(first_fg, when, sizeof(when));
        ex_fmt_mmss(fg_total, dur, sizeof(dur));
        ex_probe_value(p, "a foreground app was present for %s (mm:ss), first seen at %s%s", dur,
                       have_first ? when : "n/a", run->fg_truncated ? "; more sessions than recorded" : "");
    } else {
        ex_probe_value(p, "no foreground application was observed during the session");
    }
    ex_probe_limit(p, "Time a running big app was reported; presence, not focus or audio rights.");

    /* 6.1: peak memory within this session (not a cross-run comparison). */
    p = ex_add_probe(run, 'E', "memory_peak", run->usage_end.have ? EX_ST_OK : EX_ST_NOT_AVAILABLE,
                     run->context == EX_CTX_DESKTOP ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
    if (run->usage_end.have)
        ex_probe_value(p, "peak rss at start %lld KiB, at end %lld KiB (delta %lld KiB)",
                       run->sys.maxrss_bytes >= 0 ? run->sys.maxrss_bytes / 1024 : -1,
                       run->usage_end.maxrss_bytes / 1024,
                       run->sys.maxrss_bytes >= 0 ? (run->usage_end.maxrss_bytes - run->sys.maxrss_bytes) / 1024 : -1);
    else
        ex_probe_value(p, "getrusage did not answer at the end");
    ex_probe_limit(p, "Peak RSS of this process within one session; a difference between two runs is not "
                      "by itself a leak.");

    if (run->title_guard_stop) {
        p = ex_add_probe(run, 'E', "title_query_guard", EX_ST_ERROR, EX_EV_HARDWARE_VERIFIED);
        ex_probe_value(p, "sceSystemServiceGetAppTitleId wrote outside its guarded window; the query was disabled");
        ex_probe_limit(p, "The guard only detects an overrun; after one, no further title call is made.");
    }
}
