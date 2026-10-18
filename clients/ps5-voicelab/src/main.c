/*
 * AKENO Voice Lab entry point. PS5: send akeno-voicelab.elf with the ELF loader (port 9021) like
 * the AKENO daemon, or start it from the homebrew launcher's AKENO Voice Lab entry, which runs it as
 * a background payload. It refuses to run twice (it holds 127.0.0.1:8644 while it runs), writes
 * /data/akeno-voicelab/report.txt and ends. PC: a simulated console for development
 * (AKN_LAB_SIM=speech|zeros|refused, AKN_LAB_QUICK=1).
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "backend.h"

#define LAB_PORT 8644

static int single_instance(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(LAB_PORT);
    a.sin_addr.s_addr = htonl(0x7F000001u) /* 127.0.0.1 */;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 1) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int write_file(const char *path, const char *text, size_t len) {
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int ok = write(fd, text, len) == (ssize_t)len && fsync(fd) == 0;
    close(fd);
    return ok && rename(tmp, path) == 0 ? 0 : -1;
}

int main(int argc, char **argv) {
    akn_lab_options opt;
    memset(&opt, 0, sizeof(opt));
    const char *out_dir = "/data/akeno-voicelab";
    akn_lab_backend be;
#ifdef AKN_LAB_PS5
    (void)argc;
    (void)argv;
    akn_lab_backend_ps5(&be);
#else
    static akn_lab_sim sim;
    memset(&sim, 0, sizeof(sim));
    sim.game_after_polls = 2;
    sim.pace = 1;
    const char *mode = getenv("AKN_LAB_SIM");
    if (mode && strcmp(mode, "zeros") == 0) sim.mic_mode = 1;
    if (mode && strcmp(mode, "refused") == 0) {
        sim.mic_mode = 2;
        sim.fail_port_types = (1 << 2) | (1 << 3);
    }
    opt.quick = getenv("AKN_LAB_QUICK") != NULL;
    out_dir = argc > 1 ? argv[1] : ".";
    akn_lab_backend_host(&be, &sim);
#endif
    int lock = single_instance();
    if (lock < 0) {
        be.notify(be.user, "AKENO Voice Lab is already running (or port 8644 is in use).");
        return 1;
    }
    mkdir(out_dir, 0755);
    static akn_lab_run run;
    akn_lab_execute(&be, &opt, &run);
    static char report[32768];
    size_t n = akn_lab_report(&run, report, sizeof(report));
    char path[300];
    snprintf(path, sizeof(path), "%s/report.txt", out_dir);
    int rc = write_file(path, report, n);
    char stamped[300];
    snprintf(stamped, sizeof(stamped), "%s/report-%lld.txt", out_dir, (long long)time(NULL));
    if (rc == 0) write_file(stamped, report, n);
#ifndef AKN_LAB_PS5
    fwrite(report, 1, n, stdout);
#endif
    close(lock);
    return rc == 0 ? 0 : 2;
}
