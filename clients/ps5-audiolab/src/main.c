/*
 * AKENO Audio Lab 3.1 entry point. A standalone, isolated, file-controlled PS5 experiment.
 *
 * PS5: send akeno-audio-lab.elf with the ELF loader (port 9021) like the System Explorer. It runs
 * as a background payload, writes to /data/akeno-audiolab/ ONLY, and ends on an `exit` command or
 * after AL_SESSION_MAX_S. One instance at a time (flock): a second start is refused, so two audio
 * sessions can never run in parallel. A tone is played ONLY after the owner appends a `tone`
 * command to /data/akeno-audiolab/control; nothing plays on its own.
 *
 * Session boundary (safety): after init and after the lock is held, al_control_begin() moves any
 * pre-existing control file aside, so a stale `tone` from a previous run is NEVER re-executed; only
 * commands appended after the lab started are read.
 *
 * Control commands (one per line, each line newline-terminated): status | tone [preset] [level] |
 * stop | exit.
 *
 * Host: a simulation for tests; the control script comes from the sim struct, not a file.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "al.h"
#include "al_backend_host.h"

#ifndef LOCK_EX
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#endif
int flock(int fd, int operation);

static int write_file(const char *dir, const char *name, const char *text, size_t len) {
    char path[320], tmp[340];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int ok = write(fd, text, len) == (ssize_t)len && fsync(fd) == 0;
    close(fd);
    return ok && rename(tmp, path) == 0 ? 0 : -1;
}

static char g_buf[128 * 1024];

/* Reports read al_state; the tone worker can be running, so hold st->lock while reading. */
static void write_reports(al_state *st, const char *dir) {
    al_state_lock(st);
    size_t n = al_report_json(st, g_buf, sizeof(g_buf));
    al_state_unlock(st);
    write_file(dir, "report.json", g_buf, n);
    al_state_lock(st);
    n = al_report_txt(st, g_buf, sizeof(g_buf));
    al_state_unlock(st);
    write_file(dir, "report.txt", g_buf, n);
#ifndef AL_PS5
    fwrite(g_buf, 1, n, stdout);
#endif
}

static void write_status(al_state *st, const al_backend *be, const char *dir, const char *state) {
    al_state_lock(st);
    size_t n = al_report_status_json(st, state, be->now_unix(be->user), g_buf, sizeof(g_buf));
    al_state_unlock(st);
    write_file(dir, "status.json", g_buf, n);
}

/* ---- the tone worker (one at a time) ------------------------------------------------------- */

typedef struct {
    al_state *st;
    const al_backend *be;
    const al_preset *preset;
    const al_level *level;
    atomic_int *cancel;
    atomic_int running;
    atomic_int done; /* a finished worker waiting to be joined and reported */
} tone_ctx;

static void *tone_worker(void *arg) {
    tone_ctx *c = arg;
    al_engine_run_tone(c->st, c->be, c->preset, c->level, c->cancel);
    atomic_store(&c->running, 0);
    atomic_store(&c->done, 1);
    return NULL;
}

int main(int argc, char **argv) {
    const char *dir;
    al_backend be;

#ifdef AL_PS5
    (void)argc;
    (void)argv;
    dir = AL_OUT_DIR;
    al_backend_ps5(&be);
#else
    static al_sim sim;
    memset(&sim, 0, sizeof(sim));
    sim.real_time = 1;
    sim.have_fw = 1;
    sim.fw = 0x12200000u;
    sim.pid = (int)getpid();
    sim.port_state_exported = 1;
    sim.control = getenv("AL_SIM_CONTROL");
    sim.opt_in = getenv("AL_SIM_OPTIN") ? 1 : 0;
    dir = argc > 1 ? argv[1] : ".";
    al_backend_host(&be, &sim);
#endif

    mkdir(dir, 0755);

    /* Single instance: a flock we hold for the whole run. If the lock file cannot even be opened,
     * abort rather than run unprotected (a second instance must never slip through). */
    char lockpath[320];
    snprintf(lockpath, sizeof(lockpath), "%s/audiolab.lock", dir);
    int lock_fd = open(lockpath, O_WRONLY | O_CREAT, 0644);
    if (lock_fd < 0) {
        be.notify(be.user, "AKENO Audio Lab could not create its lock file; not starting.");
        return 1;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        be.notify(be.user, errno == EWOULDBLOCK
                               ? "AKENO Audio Lab is already running; a second start was refused."
                               : "AKENO Audio Lab could not take its instance lock; not starting.");
        close(lock_fd);
        return 1;
    }

    static al_state st;
    al_engine_init(&st, &be);

    /* Session boundary: only commands appended from here on are executed. */
    be.control_begin(be.user);

    be.notify(be.user, "AKENO Audio Lab 3.1 started (isolated, read-only until you ask for a tone).");
    write_reports(&st, dir);
    write_status(&st, &be, dir, "RUNNING");

    static atomic_int cancel;
    atomic_init(&cancel, 0);
    static tone_ctx tc;
    memset(&tc, 0, sizeof(tc));
    tc.st = &st;
    tc.be = &be;
    tc.cancel = &cancel;
    atomic_init(&tc.running, 0);
    atomic_init(&tc.done, 0);
    int thread_live = 0;
    pthread_t thread;

    char pending[256];
    size_t pending_len = 0;
    long long last_hb = st.started_unix;
    long long last_tone_end = 0;
    uint64_t start_us = be.now_us(be.user);
    int exit_requested = 0;

    while (!exit_requested) {
        if ((be.now_us(be.user) - start_us) >= (uint64_t)AL_SESSION_MAX_S * 1000000u) break;

        if (atomic_load(&tc.done) && thread_live) {
            pthread_join(thread, NULL);
            thread_live = 0;
            atomic_store(&tc.done, 0);
            last_tone_end = be.now_unix(be.user);
            write_reports(&st, dir);
        }

        int reset = 0;
        char rb[256];
        int n = be.read_control(be.user, rb, sizeof(rb), &reset);
        if (reset) pending_len = 0;
        for (int i = 0; i < n && !exit_requested; i++) {
            char ch = rb[i];
            if (ch != '\n' && ch != '\r') {
                if (pending_len < sizeof(pending) - 1) pending[pending_len++] = ch;
                continue;
            }
            pending[pending_len] = '\0';
            pending_len = 0;
            al_cmd cmd;
            al_cmd_kind kind = al_parse_command(pending, &cmd);
            if (kind == AL_CMD_NONE) continue;
            al_state_lock(&st);
            st.commands_seen++;
            al_state_unlock(&st);
            if (kind == AL_CMD_STATUS) {
                write_reports(&st, dir);
            } else if (kind == AL_CMD_STOP) {
                int running = atomic_load(&tc.running);
                if (running) atomic_store(&cancel, 1);
                al_state_lock(&st);
                st.stops++;
                snprintf(st.last_note, sizeof(st.last_note),
                         running ? "stop: cancelling the running tone" : "stop: nothing was running");
                al_state_unlock(&st);
                write_reports(&st, dir);
            } else if (kind == AL_CMD_EXIT) {
                exit_requested = 1;
            } else if (kind == AL_CMD_TONE) {
                int running = atomic_load(&tc.running);
                const al_preset *preset = al_find_preset(cmd.preset);
                const al_level *level = al_find_level(cmd.level);
                al_state_lock(&st);
                int limit = st.tone_commands >= AL_RUNS_PER_LAUNCH;
                al_state_unlock(&st);
                if (running) {
                    al_state_lock(&st);
                    snprintf(st.last_note, sizeof(st.last_note), "tone ignored: a tone is still playing");
                    al_state_unlock(&st);
                } else if (!preset || !level) {
                    al_state_lock(&st);
                    snprintf(st.last_note, sizeof(st.last_note), "tone ignored: unknown preset or level");
                    al_state_unlock(&st);
                } else if (last_tone_end && be.now_unix(be.user) - last_tone_end < (AL_COOLDOWN_MS + 999) / 1000) {
                    al_state_lock(&st);
                    snprintf(st.last_note, sizeof(st.last_note), "tone ignored: wait a few seconds between tones");
                    al_state_unlock(&st);
                } else if (limit) {
                    al_state_lock(&st);
                    snprintf(st.last_note, sizeof(st.last_note), "tone ignored: per-launch limit reached");
                    al_state_unlock(&st);
                } else {
                    atomic_store(&cancel, 0);
                    tc.preset = preset;
                    tc.level = level;
                    atomic_store(&tc.done, 0);
                    atomic_store(&tc.running, 1);
                    if (pthread_create(&thread, NULL, tone_worker, &tc) == 0) {
                        thread_live = 1;
                        al_state_lock(&st);
                        snprintf(st.last_note, sizeof(st.last_note), "tone started: %s, %s", preset->label,
                                 level->label);
                        al_state_unlock(&st);
                    } else {
                        atomic_store(&tc.running, 0);
                        al_state_lock(&st);
                        snprintf(st.last_note, sizeof(st.last_note), "tone failed: could not start a thread");
                        al_state_unlock(&st);
                    }
                }
                write_reports(&st, dir);
            }
        }

        long long now = be.now_unix(be.user);
        if (now - last_hb >= (long long)AL_HEARTBEAT_S) {
            al_state_lock(&st);
            st.last_heartbeat_unix = now;
            al_state_unlock(&st);
            write_status(&st, &be, dir, "RUNNING");
            last_hb = now;
        }
        be.sleep_ms(be.user, AL_CONTROL_POLL_MS);
    }

    if (thread_live) {
        atomic_store(&cancel, 1);
        pthread_join(thread, NULL);
        thread_live = 0;
    }
    al_state_lock(&st);
    st.exited = exit_requested;
    al_state_unlock(&st);
    write_reports(&st, dir);
    write_status(&st, &be, dir, "COMPLETED");
    be.notify(be.user, "AKENO Audio Lab finished. Reports written to /data/akeno-audiolab/.");

    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    unlink(lockpath);
    return 0;
}
