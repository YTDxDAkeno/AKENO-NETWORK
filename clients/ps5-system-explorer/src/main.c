/*
 * AKENO System Explorer entry point (6.1).
 *
 * PS5: send akeno-system-explorer.elf with the ELF loader (port 9021) like the AKENO daemon, or
 * start it from the homebrew launcher's AKENO System Explorer entry. It runs as a background
 * payload, writes a bounded diagnostic session (default 5 minutes) to /data/akeno-system-explorer/
 * and ends cleanly.
 *
 * 6.1 instance handling: a relaunch never starts a second diagnostic session. If a session is
 * alive (advisory flock on explorer.lock, or a fresh heartbeat in status.json), the relaunch hands
 * its argument over as a marker through the control file and exits. If a previous session was cut
 * off, its control file is moved aside (never executed) and its reports are moved to previous/,
 * then a new session starts. See ex_session.c and docs/PHASE6_1_PROCESS_AUDIO_RESEARCH_DE.md.
 *
 * PC: a simulated console for development. AKN_EX_QUICK=1 for a short session; AKN_EX_SIM =
 * background | foreground | home-to-game | refused. An optional argument after the output
 * directory is handed over as a marker to a running session, exactly as on the PS5.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ex.h"
#include "ex_backend_host.h"

/* flock(2) and its constants, declared directly rather than through <sys/file.h>: that header
 * pulls in kernel structs needing the BSD types on the PS5 SDK, which _POSIX_C_SOURCE hides. The
 * LOCK_* values are identical on BSD and Linux, and the PS5 libkernel exports flock. */
#ifndef LOCK_SH
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#endif
int flock(int fd, int operation);

#define OUT_DIR_PS5 "/data/akeno-system-explorer"

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

static long long read_file(const char *path, char *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return n;
}

/* ---- the heartbeat checkpoint (status.json), shared by both backends --------------------------
 * Writes status.json every heartbeat. Before writing, it re-reads status.json: if a DIFFERENT
 * session is RUNNING there with a newer heartbeat, this session was superseded (possible only if
 * the advisory lock is not enforced) and stands down. Returns nonzero to end the session then. */
typedef struct {
    char dir[256];
    long long (*now)(void);
} checkpoint_ctx;

static long long host_now(void) { return (long long)time(NULL); }

static int checkpoint(void *vctx, const ex_run *run, int final) {
    checkpoint_ctx *c = vctx;
    long long now = c->now();

    if (!final) {
        char buf[1024];
        char path[320];
        snprintf(path, sizeof(path), "%s/status.json", c->dir);
        if (read_file(path, buf, sizeof(buf)) > 0) {
            int running = 0;
            long long hb = 0;
            char sid[48];
            if (ex_status_parse(buf, &running, &hb, sid, sizeof(sid)) == 0 && running &&
                strcmp(sid, run->opt.session.id) != 0 && hb > run->started_unix) {
                return 1; /* a newer, different session owns the directory */
            }
        }
    }

    static char out[2048];
    const char *state = final ? (run->superseded ? "SUPERSEDED" : "COMPLETED") : "RUNNING";
    size_t n = ex_report_status_json(run, state, now, out, sizeof(out));
    write_file(c->dir, "status.json", out, n);
    return 0;
}

/* ---- instance probe and recovery ------------------------------------------------------------- */

/* Takes the advisory lock, if the platform supports it. Keeps the fd open for the whole run. */
static ex_lock_result take_lock(const char *dir, int *lock_fd, int *existed, char *session, size_t session_len) {
    char path[320];
    snprintf(path, sizeof(path), "%s/explorer.lock", dir);
    if (session && session_len) session[0] = '\0';

    struct stat st;
    *existed = stat(path, &st) == 0;
    if (*existed && session && session_len) {
        char buf[64];
        if (read_file(path, buf, sizeof(buf)) > 0) {
            char *nl = strpbrk(buf, "\r\n");
            if (nl) *nl = '\0';
            snprintf(session, session_len, "%s", buf);
        }
    }

    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        *lock_fd = -1;
        return EX_LOCK_ERROR;
    }
    int rc = flock(fd, LOCK_EX | LOCK_NB);
    if (rc == 0) {
        *lock_fd = fd;
        return EX_LOCK_ACQUIRED;
    }
    if (errno == EWOULDBLOCK) {
        *lock_fd = fd; /* keep it open for the probe; another live process holds it */
        return EX_LOCK_HELD;
    }
    /* flock not supported here (e.g. some filesystems): fall back to the heartbeat alone. */
    *lock_fd = fd;
    return EX_LOCK_UNSUPPORTED;
}

static void probe_instance(const char *dir, ex_lock_result lock, int existed, const char *lock_session,
                           long long now, ex_instance_probe *p) {
    memset(p, 0, sizeof(*p));
    p->lock = lock;
    p->lock_file_existed = existed;
    p->now_unix = now;
    snprintf(p->lock_session, sizeof(p->lock_session), "%s", lock_session ? lock_session : "");

    char path[320], buf[1024];
    snprintf(path, sizeof(path), "%s/status.json", dir);
    if (read_file(path, buf, sizeof(buf)) > 0) {
        int running = 0;
        long long hb = 0;
        if (ex_status_parse(buf, &running, &hb, p->status_session, sizeof(p->status_session)) == 0) {
            p->status_present = 1;
            p->status_running = running;
            p->heartbeat_unix = hb;
        }
    }
}

/* Moves a leftover control file aside (never executed) and previous reports into previous/. */
static void recover_previous(const char *dir, ex_session_info *si) {
    char from[320], to[360];
    struct stat st;

    snprintf(from, sizeof(from), "%s/control", dir);
    if (stat(from, &st) == 0) {
        si->stale_control_bytes = (long long)st.st_size;
        snprintf(to, sizeof(to), "%s/control.stale", dir);
        if (rename(from, to) != 0) unlink(from); /* at worst remove it, but never read it as commands */
    }

    char prev[320];
    snprintf(prev, sizeof(prev), "%s/previous", dir);
    mkdir(prev, 0755);
    static const char *const files[] = { "report.json", "modules.json", "audio-diagnostics.json",
                                         "events.jsonl", "system-summary.txt", "status.json" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(from, sizeof(from), "%s/%s", dir, files[i]);
        if (stat(from, &st) != 0) continue;
        snprintf(to, sizeof(to), "%s/previous/%s", dir, files[i]);
        if (rename(from, to) == 0) si->previous_reports_moved = 1;
    }
}

static int append_marker(const char *dir, const char *text) {
    char path[320];
    snprintf(path, sizeof(path), "%s/control", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return -1;
    char line[320];
    int n = snprintf(line, sizeof(line), "mark %s\n", text);
    int ok = write(fd, line, (size_t)n) == n;
    close(fd);
    return ok ? 0 : -1;
}

static size_t (*const g_report_writers[])(const ex_run *, char *, size_t) = {
    ex_report_json, ex_report_modules_json, ex_report_audio_json, ex_report_events_jsonl, ex_report_summary_txt,
};
static const char *const g_files[] = {
    "report.json", "modules.json", "audio-diagnostics.json", "events.jsonl", "system-summary.txt",
};

int main(int argc, char **argv) {
    ex_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.session_s = EX_SESSION_DEFAULT_S;
    opt.poll_ms = EX_POLL_DEFAULT_MS;

    ex_backend be;
    const char *out_dir;
    const char *marker_arg = NULL;

#ifdef AKN_EX_PS5
    (void)argc;
    (void)argv;
    out_dir = OUT_DIR_PS5;
    ex_backend_ps5(&be);
#else
    static ex_sim sim;
    memset(&sim, 0, sizeof(sim));
    sim.real_time = 1;
    sim.have_fw = 1;
    sim.fw = 0x12200000u;
    sim.have_proc = 0; /* 6.1: model the FW 12.20 observation (process_record NOT_AVAILABLE) */
    sim.have_appinfo = sim.have_bigapp = sim.have_user = 1;
    sim.have_rlimit = sim.have_rusage = 1;
    sim.game_app = 0x60000123;
    sim.game_after_samples = -1;
    sim.title_map = "0x60000123=PPSA00001";
    const char *mode = getenv("AKN_EX_SIM");
    if (mode && strcmp(mode, "foreground") == 0) sim.foreground = 1;
    else if (mode && strcmp(mode, "home-to-game") == 0) sim.game_after_samples = 3;
    else if (mode && strcmp(mode, "refused") == 0) {
        sim.have_appinfo = 0;
        sim.user_rc = (int32_t)0x80960009;
        sim.load_fail = (1u << 8) | (1u << 9);
    }
    if (getenv("AKN_EX_QUICK")) {
        opt.session_s = 2;
        opt.poll_ms = 100;
    }
    out_dir = argc > 1 ? argv[1] : ".";
    marker_arg = argc > 2 ? argv[2] : NULL;
    ex_backend_host(&be, &sim);
#endif

    mkdir(out_dir, 0755);

    /* ---- instance decision (6.1) ---- */
    long long now = be.unix_time(be.user);
    int lock_fd = -1, lock_existed = 0;
    char lock_session[48];
    ex_lock_result lock = take_lock(out_dir, &lock_fd, &lock_existed, lock_session, sizeof(lock_session));

    ex_instance_probe ip;
    probe_instance(out_dir, lock, lock_existed, lock_session, now, &ip);
    char why[200];
    ex_instance_action action = ex_instance_decide(&ip, why, sizeof(why));

    if (action == EX_INST_MARKER) {
        const char *text = marker_arg ? marker_arg : "relaunch marker";
        int rc = append_marker(out_dir, text);
        char msg[320];
        snprintf(msg, sizeof(msg), "AKENO System Explorer is already running (%s). %s.", why,
                 rc == 0 ? "A marker was handed to the running session" : "A marker could not be written");
        be.notify(be.user, msg);
        if (lock_fd >= 0) close(lock_fd);
        return 0;
    }
    if (action == EX_INST_REFUSE) {
        char msg[320];
        snprintf(msg, sizeof(msg), "AKENO System Explorer did not start: %s.", why);
        be.notify(be.user, msg);
        if (lock_fd >= 0) close(lock_fd);
        return 1;
    }

    /* START or START_RECOVERED: this session owns the directory. */
    opt.session.action = action;
    opt.session.lock = lock;
    snprintf(opt.session.id, sizeof(opt.session.id), "%lld-%d", now, (int)getpid());
    opt.session.stale_control_bytes = -1;
    char no_title[320];
    snprintf(no_title, sizeof(no_title), "%s/no-title-query", out_dir);
    opt.session.title_query_enabled = access(no_title, F_OK) != 0;

    if (action == EX_INST_START_RECOVERED) {
        recover_previous(out_dir, &opt.session);
        snprintf(opt.session.recovery, sizeof(opt.session.recovery), "%.160s; cleaned up and started fresh", why);
    } else {
        snprintf(opt.session.recovery, sizeof(opt.session.recovery), "%s", why);
    }

    /* Write our lock marker (the session id) into the held lock file. */
    if (lock_fd >= 0) {
        char idline[64];
        int n = snprintf(idline, sizeof(idline), "%s\n", opt.session.id);
        if (ftruncate(lock_fd, 0) == 0 && lseek(lock_fd, 0, SEEK_SET) == 0) {
            if (write(lock_fd, idline, (size_t)n) < 0) { /* best effort */
            }
        }
    }

    static checkpoint_ctx cc;
    snprintf(cc.dir, sizeof(cc.dir), "%s", out_dir);
    cc.now = host_now;
    opt.checkpoint = checkpoint;
    opt.checkpoint_ctx = &cc;

    be.notify(be.user, "AKENO System Explorer 6.1 started (read-only diagnostics). See /data/akeno-system-explorer/.");

    static ex_run run;
    ex_execute(&be, &opt, &run);

    static char buf[256 * 1024];
    int rc = 0;
    for (size_t i = 0; i < sizeof(g_files) / sizeof(g_files[0]); i++) {
        size_t n = g_report_writers[i](&run, buf, sizeof(buf));
        if (write_file(out_dir, g_files[i], buf, n) != 0) rc = 2;
#ifndef AKN_EX_PS5
        if (strcmp(g_files[i], "system-summary.txt") == 0) fwrite(buf, 1, n, stdout);
#endif
    }

    be.notify(be.user, rc == 0 ? "AKENO System Explorer finished. Reports written."
                               : "AKENO System Explorer finished with a report write error.");

    if (lock_fd >= 0) {
        char lockpath[320];
        snprintf(lockpath, sizeof(lockpath), "%s/explorer.lock", out_dir);
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        unlink(lockpath);
    }
    return rc;
}
