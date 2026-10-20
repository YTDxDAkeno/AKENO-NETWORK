/*
 * Instance handling and crash recovery (6.1). A starting Explorer finds out from the output
 * directory whether a session is alive, and decides with ONE rule what to do:
 *
 *   - A live session runs   -> hand it a marker and exit (a relaunch never starts a second session).
 *   - A session was cut off -> start a new session and record what was left behind.
 *   - Nothing runs          -> start a new session.
 *
 * Two independent liveness signals are used, because neither is verified on FW 12.20:
 *   1. an advisory flock() on explorer.lock. The kernel drops it when its holder dies, so a
 *      leftover file after a crash is harmless. libkernel exports flock (SDK stub), but whether
 *      the PS5 enforces it on /data has not been measured;
 *   2. the heartbeat in status.json (rewritten at least every EX_HEARTBEAT_S seconds). It works
 *      without flock, but a crash is only recognised once it is EX_HEARTBEAT_STALE_S old.
 *
 * A fresh heartbeat always counts as "alive", even when flock could be taken: if flock is not
 * enforced, the conservative answer is a marker, never a second session. The price is that after a
 * crash a new session can only start once the heartbeat is stale (under a minute).
 */
#include "ex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ex_lock_result_name(ex_lock_result r) {
    switch (r) {
    case EX_LOCK_ACQUIRED: return "flock acquired";
    case EX_LOCK_HELD: return "flock held by a live process";
    case EX_LOCK_UNSUPPORTED: return "flock unsupported (heartbeat only)";
    case EX_LOCK_ERROR: return "lock file not available";
    }
    return "unknown";
}

const char *ex_instance_action_name(ex_instance_action a) {
    switch (a) {
    case EX_INST_START: return "START";
    case EX_INST_START_RECOVERED: return "START_RECOVERED";
    case EX_INST_MARKER: return "MARKER";
    case EX_INST_REFUSE: return "REFUSE";
    }
    return "UNKNOWN";
}

static int heartbeat_fresh(const ex_instance_probe *p) {
    if (!p->status_present || !p->status_running) return 0;
    long long age = p->now_unix - p->heartbeat_unix;
    return age >= -EX_HEARTBEAT_STALE_S && age <= EX_HEARTBEAT_STALE_S;
}

ex_instance_action ex_instance_decide(const ex_instance_probe *p, char *why, size_t why_len) {
    char tmp[1];
    if (!why || !why_len) {
        why = tmp;
        why_len = sizeof(tmp);
    }
    int fresh = heartbeat_fresh(p);
    long long age = p->now_unix - p->heartbeat_unix;

    if (p->lock == EX_LOCK_HELD) {
        if (fresh) {
            snprintf(why, why_len, "session %s is alive (flock held, heartbeat %lld s old)", p->status_session, age);
            return EX_INST_MARKER;
        }
        snprintf(why, why_len, "a live process holds the lock but its heartbeat is %s: nothing started",
                 p->status_present ? "stale" : "missing");
        return EX_INST_REFUSE;
    }
    if (p->lock == EX_LOCK_ERROR && !fresh) {
        snprintf(why, why_len, "the lock file cannot be opened: nothing started");
        return EX_INST_REFUSE;
    }
    if (fresh) {
        snprintf(why, why_len, "session %s has a fresh heartbeat (%lld s; %s): treated as alive", p->status_session,
                 age, ex_lock_result_name(p->lock));
        return EX_INST_MARKER;
    }
    if (p->status_present && p->status_running) {
        snprintf(why, why_len, "previous session %s was interrupted (last heartbeat %lld s ago)", p->status_session,
                 age);
        return EX_INST_START_RECOVERED;
    }
    if (p->lock_file_existed) {
        snprintf(why, why_len, "a leftover explorer.lock%s%s was found without a running session",
                 p->lock_session[0] ? " of session " : "", p->lock_session);
        return EX_INST_START_RECOVERED;
    }
    snprintf(why, why_len, "no session running");
    return EX_INST_START;
}

/* Finds "key": in a flat JSON object and returns a pointer just past the colon and any spaces. */
static const char *find_key(const char *text, const char *key) {
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(text, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

int ex_status_parse(const char *text, int *running, long long *heartbeat_unix, char *session, size_t session_len) {
    *running = 0;
    *heartbeat_unix = 0;
    if (session && session_len) session[0] = '\0';
    if (!text) return -1;
    const char *state = find_key(text, "state");
    const char *hb = find_key(text, "heartbeatUnix");
    if (!state || *state != '"' || !hb) return -1;
    *running = strncmp(state, "\"RUNNING\"", 9) == 0;
    char *end = NULL;
    long long v = strtoll(hb, &end, 10);
    if (end == hb) return -1;
    *heartbeat_unix = v;
    const char *sid = find_key(text, "sessionId");
    if (sid && *sid == '"' && session && session_len) {
        sid++;
        size_t n = 0;
        while (sid[n] && sid[n] != '"' && n + 1 < session_len) {
            session[n] = sid[n];
            n++;
        }
        session[n] = '\0';
    }
    return 0;
}
