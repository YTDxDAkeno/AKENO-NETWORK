/*
 * AKENO Audio Lab 3.1 entry point. A standalone, isolated, file-controlled PS5 experiment.
 *
 * PS5: send akeno-audio-lab.elf with the ELF loader (port 9021) like the System Explorer. It runs
 * as a background payload, writes to /data/akeno-audiolab/ ONLY, and ends on an `exit` command or
 * after AL_SESSION_MAX_S. One instance at a time (flock): a second start is refused, so two audio
 * sessions can never run in parallel. A tone is played ONLY after the owner appends a `tone`
 * command to /data/akeno-audiolab/control; nothing plays on its own.
 *
 * Control commands (one per line, appended to the control file):
 *   status            rewrite report.json / report.txt now
 *   tone [preset] [level]   play one tone (defaults: stereo16-1024, moderate)
 *   stop              cancel a playing or pending tone
 *   exit              end the session cleanly
 *
 * Host: a simulation for tests; the control script comes from the sim struct, not a file.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
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

static void write_reports(const al_state *st, const char *dir) {
    size_t n = al_report_json(st, g_buf, sizeof(g_buf));
    write_file(dir, "report.json", g_buf, n);
    n = al_report_txt(st, g_buf, sizeof(g_buf));
    write_file(dir, "report.txt", g_buf, n);
#ifndef AL_PS5
    fwrite(g_buf, 1, n, stdout);
#endif
}

static void write_status(const al_state *st, const al_backend *be, const char *dir, const char *state) {
    size_t n = al_report_status_json(st, state, be->now_unix(be->user), g_buf, sizeof(g_buf));
    write_file(dir, "status.json", g_buf, n);
}

/* ---- the tone worker (one at a time) ------------------------------------------------------- */

typedef struct {
    al_state *st;
    const al_backend *be;
    const al_preset *preset;
    const al_level *level;
    volatile int *cancel;
    pthread_mutex_t lock;
    int running;
    int done; /* a finished worker waiting to be joined and reported */
} tone_ctx;

static void *tone_worker(void *arg) {
    tone_ctx *c = arg;
    al_engine_run_tone(c->st, c->be, c->preset, c->level, c->cancel);
    pthread_mutex_lock(&c->lock);
    c->running = 0;
    c->done = 1;
    pthread_mutex_unlock(&c->lock);
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

    /* Single instance: a flock we hold for the whole run. A second start is refused. */
    char lockpath[320];
    snprintf(lockpath, sizeof(lockpath), "%s/audiolab.lock", dir);
    int lock_fd = open(lockpath, O_WRONLY | O_CREAT, 0644);
    if (lock_fd >= 0 && flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        be.notify(be.user, "AKENO Audio Lab is already running; a second start was refused.");
        close(lock_fd);
        return 1;
    }

    static al_state st;
    al_engine_init(&st, &be);

    be.notify(be.user, "AKENO Audio Lab 3.1 started (isolated, read-only until you ask for a tone).");
    write_reports(&st, dir);
    write_status(&st, &be, dir, "RUNNING");

    static volatile int cancel = 0;
    static tone_ctx tc;
    memset(&tc, 0, sizeof(tc));
    tc.st = &st;
    tc.be = &be;
    tc.cancel = &cancel;
    pthread_mutex_init(&tc.lock, NULL);
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

        /* Collect a finished tone worker and write its report. */
        pthread_mutex_lock(&tc.lock);
        int done = tc.done;
        pthread_mutex_unlock(&tc.lock);
        if (done && thread_live) {
            pthread_join(thread, NULL);
            thread_live = 0;
            tc.done = 0;
            last_tone_end = be.now_unix(be.user);
            write_reports(&st, dir);
        }

        int reset = 0;
        char rb[256];
        int n = be.read_control(be.user, rb, sizeof(rb), &reset);
        if (reset) pending_len = 0;
        for (int i = 0; i < n && !exit_requested; i++) {
            char ch = rb[i];
            if (ch == '\n' || ch == '\r') {
                pending[pending_len] = '\0';
                al_cmd cmd;
                al_cmd_kind kind = al_parse_command(pending, &cmd);
                pending_len = 0;
                if (kind == AL_CMD_NONE) continue;
                st.commands_seen++;
                if (kind == AL_CMD_STATUS) {
                    write_reports(&st, dir);
                } else if (kind == AL_CMD_STOP) {
                    pthread_mutex_lock(&tc.lock);
                    int running = tc.running;
                    pthread_mutex_unlock(&tc.lock);
                    if (running) cancel = 1;
                    st.stops++;
                    snprintf(st.last_note, sizeof(st.last_note), running ? "stop: cancelling the running tone"
                                                                         : "stop: nothing was running");
                    write_reports(&st, dir);
                } else if (kind == AL_CMD_EXIT) {
                    exit_requested = 1;
                } else if (kind == AL_CMD_TONE) {
                    pthread_mutex_lock(&tc.lock);
                    int running = tc.running;
                    pthread_mutex_unlock(&tc.lock);
                    const al_preset *preset = al_find_preset(cmd.preset);
                    const al_level *level = al_find_level(cmd.level);
                    if (running) {
                        snprintf(st.last_note, sizeof(st.last_note), "tone ignored: a tone is still playing");
                    } else if (!preset || !level) {
                        snprintf(st.last_note, sizeof(st.last_note), "tone ignored: unknown preset or level");
                    } else if (last_tone_end && be.now_unix(be.user) - last_tone_end < (AL_COOLDOWN_MS + 999) / 1000) {
                        snprintf(st.last_note, sizeof(st.last_note), "tone ignored: wait a few seconds between tones");
                    } else if (st.tone_commands >= AL_RUNS_PER_LAUNCH) {
                        snprintf(st.last_note, sizeof(st.last_note), "tone ignored: per-launch limit reached");
                    } else {
                        cancel = 0;
                        tc.preset = preset;
                        tc.level = level;
                        tc.done = 0;
                        tc.running = 1;
                        if (pthread_create(&thread, NULL, tone_worker, &tc) == 0) {
                            thread_live = 1;
                            snprintf(st.last_note, sizeof(st.last_note), "tone started: %s, %s", preset->label,
                                     level->label);
                        } else {
                            tc.running = 0;
                            snprintf(st.last_note, sizeof(st.last_note), "tone failed: could not start a thread");
                        }
                    }
                    write_reports(&st, dir);
                }
            } else if (pending_len < sizeof(pending) - 1) {
                pending[pending_len++] = ch;
            }
        }

        long long now = be.now_unix(be.user);
        if (now - last_hb >= (long long)AL_HEARTBEAT_S) {
            st.last_heartbeat_unix = now;
            write_status(&st, &be, dir, "RUNNING");
            last_hb = now;
        }
        be.sleep_ms(be.user, AL_CONTROL_POLL_MS);
    }

    /* Clean shutdown: stop and join any running tone, write the final reports. */
    if (thread_live) {
        cancel = 1;
        pthread_join(thread, NULL);
        thread_live = 0;
    }
    st.exited = exit_requested;
    write_reports(&st, dir);
    write_status(&st, &be, dir, "COMPLETED");
    be.notify(be.user, "AKENO Audio Lab finished. Reports written to /data/akeno-audiolab/.");

    if (lock_fd >= 0) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        unlink(lockpath);
    }
    return 0;
}
