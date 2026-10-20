/*
 * AKENO Audio Lab 3.1 entry point. A standalone, isolated, automatic one-shot PS5 experiment.
 *
 * It uses only /data/akeno-audiolab/, takes a non-blocking instance lock, announces itself,
 * waits five seconds, plays the one unchanged default tone, writes its reports, and exits. There
 * is no control file, FTP command, network service, microphone, or repeat loop.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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

static int write_reports(al_state *st, const char *dir) {
    int rc = 0;
    al_state_lock(st);
    size_t n = al_report_json(st, g_buf, sizeof(g_buf));
    al_state_unlock(st);
    if (write_file(dir, "report.json", g_buf, n) != 0) rc = -1;
    al_state_lock(st);
    n = al_report_txt(st, g_buf, sizeof(g_buf));
    al_state_unlock(st);
    if (write_file(dir, "report.txt", g_buf, n) != 0) rc = -1;
#ifndef AL_PS5
    fwrite(g_buf, 1, n, stdout);
#endif
    return rc;
}

static int write_status(al_state *st, const al_backend *be, const char *dir, const char *state) {
    al_state_lock(st);
    size_t n = al_report_status_json(st, state, be->now_unix(be->user), g_buf, sizeof(g_buf));
    al_state_unlock(st);
    return write_file(dir, "status.json", g_buf, n);
}

static int result_code(al_state *st, int run_id) {
    int rc = run_id < 1 ? run_id : -1;
    al_state_lock(st);
    al_run *run = run_id > 0 ? al_run_by_id(st, run_id) : NULL;
    if (run && run->outcome == AL_OUT_ACCEPTED && run->result.close_called && run->result.close_rc == 0) {
        rc = 0;
    } else if (run && run->result.close_rc) {
        rc = run->result.close_rc;
    } else if (run && run->result.first_error_rc) {
        rc = run->result.first_error_rc;
    } else if (run && run->result.open_rc < 0) {
        rc = run->result.open_rc;
    } else if (run && run->result.init_rc < 0) {
        rc = run->result.init_rc;
    }
    al_state_unlock(st);
    return rc;
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
    dir = argc > 1 ? argv[1] : ".";
    al_backend_host(&be, &sim);
#endif

    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        be.notify(be.user, "AKENO Audio Lab: Berichtsordner konnte nicht erstellt werden (0xffffffff).");
        return 1;
    }

    char lockpath[320];
    snprintf(lockpath, sizeof(lockpath), "%s/audiolab.lock", dir);
    int lock_fd = open(lockpath, O_WRONLY | O_CREAT, 0644);
    if (lock_fd < 0) {
        be.notify(be.user, "AKENO Audio Lab: Sperrdatei konnte nicht erstellt werden (0xffffffff).");
        return 1;
    }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        be.notify(be.user, errno == EWOULDBLOCK
                               ? "AKENO Audio Lab läuft bereits; zweiter Start wurde abgebrochen."
                               : "AKENO Audio Lab: Instanzsperre fehlgeschlagen (0xffffffff).");
        close(lock_fd);
        return 1;
    }

    static al_state st;
    static atomic_int cancel;
    al_engine_init(&st, &be);
    atomic_init(&cancel, 0);

    if (write_reports(&st, dir) != 0 || write_status(&st, &be, dir, "WAITING") != 0) {
        be.notify(be.user, "AKENO Audio Lab: Berichte konnten nicht vorbereitet werden (0xffffffff). Kein Ton.");
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        unlink(lockpath);
        return 1;
    }

    be.notify(be.user, "AKENO Audio Lab 3.1: automatischer Testton in 5 Sekunden.");
    int run_id = al_engine_run_one_shot(&st, &be, &cancel);
    int code = result_code(&st, run_id);
    al_state_lock(&st);
    st.exited = 1;
    snprintf(st.last_note, sizeof(st.last_note), code == 0
             ? "Automatischer Einmaltest abgeschlossen."
             : "Automatischer Einmaltest fehlgeschlagen (0x%08x).", (unsigned)code);
    al_state_unlock(&st);

    int reports_saved = write_reports(&st, dir) == 0 && write_status(&st, &be, dir, "COMPLETED") == 0;
    if (!reports_saved)
        code = -1;

    char message[192];
    if (code == 0)
        snprintf(message, sizeof(message), "AKENO Audio Lab: Testton abgeschlossen (0x00000000). Berichte gespeichert.");
    else if (!reports_saved)
        snprintf(message, sizeof(message), "AKENO Audio Lab: Berichtfehler (0xffffffff).");
    else
        snprintf(message, sizeof(message), "AKENO Audio Lab: Test fehlgeschlagen (0x%08x). Berichte gespeichert.",
                 (unsigned)code);
    be.notify(be.user, message);

    flock(lock_fd, LOCK_UN);
    close(lock_fd);
    unlink(lockpath);
    return code == 0 ? 0 : 1;
}
