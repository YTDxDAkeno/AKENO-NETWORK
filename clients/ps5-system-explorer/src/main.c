/*
 * AKENO System Explorer entry point.
 *
 * PS5: send akeno-system-explorer.elf with the ELF loader (port 9021) like the AKENO daemon, or
 * start it from the homebrew launcher's AKENO System Explorer entry. It runs as a background
 * payload, writes a bounded diagnostic session (default 5 minutes) to /data/akeno-system-explorer/
 * and ends cleanly. Append commands to /data/akeno-system-explorer/control to mark events, extend
 * or stop (see docs). A lock file keeps a second copy from running.
 *
 * PC: a simulated console for development. AKN_EX_QUICK=1 for a short session; AKN_EX_SIM =
 * background | foreground | home-to-game | refused.
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

/* A single-instance lock file (no network). Returns the fd (keep it open) or -1 if already held. */
static int lock_instance(const char *dir) {
    char path[320];
    snprintf(path, sizeof(path), "%s/explorer.lock", dir);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return -1;
    char pid[32];
    int n = snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
    if (write(fd, pid, (size_t)n) < 0) { /* best-effort */
    }
    return fd;
}

static size_t (*const g_writers[])(const ex_run *, char *, size_t) = {
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

#ifdef AKN_EX_PS5
    (void)argc;
    (void)argv;
    out_dir = "/data/akeno-system-explorer";
    ex_backend_ps5(&be);
#else
    static ex_sim sim;
    memset(&sim, 0, sizeof(sim));
    sim.have_fw = 1;
    sim.fw = 0x12200000u;
    sim.have_proc = sim.have_appinfo = sim.have_bigapp = sim.have_user = 1;
    sim.have_rlimit = sim.have_rusage = 1;
    sim.game_app = 0x60000123;
    sim.game_after_samples = -1;
    const char *mode = getenv("AKN_EX_SIM");
    if (mode && strcmp(mode, "foreground") == 0) sim.foreground = 1;
    else if (mode && strcmp(mode, "home-to-game") == 0) sim.game_after_samples = 3;
    else if (mode && strcmp(mode, "refused") == 0) {
        sim.have_appinfo = 0;
        sim.user_rc = (int32_t)0x80960009;
        sim.load_fail = (1u << 8) | (1u << 9); /* the quarantined libraries are not loaded anyway */
    }
    if (getenv("AKN_EX_QUICK")) {
        opt.session_s = 2;
        opt.poll_ms = 100;
    }
    out_dir = argc > 1 ? argv[1] : ".";
    ex_backend_host(&be, &sim);
#endif

    mkdir(out_dir, 0755);
    int lock = lock_instance(out_dir);
    if (lock < 0) {
        be.notify(be.user, "AKENO System Explorer is already running (lock held).");
        return 1;
    }

    be.notify(be.user, "AKENO System Explorer started (read-only diagnostics). See /data/akeno-system-explorer/.");

    static ex_run run;
    ex_execute(&be, &opt, &run);

    static char buf[256 * 1024];
    int rc = 0;
    for (size_t i = 0; i < sizeof(g_files) / sizeof(g_files[0]); i++) {
        size_t n = g_writers[i](&run, buf, sizeof(buf));
        if (write_file(out_dir, g_files[i], buf, n) != 0) rc = 2;
#ifndef AKN_EX_PS5
        if (strcmp(g_files[i], "system-summary.txt") == 0) fwrite(buf, 1, n, stdout);
#endif
    }

    be.notify(be.user, rc == 0 ? "AKENO System Explorer finished. Reports written."
                               : "AKENO System Explorer finished with a report write error.");

    char lockpath[320];
    snprintf(lockpath, sizeof(lockpath), "%s/explorer.lock", out_dir);
    close(lock);
    unlink(lockpath);
    return rc;
}
