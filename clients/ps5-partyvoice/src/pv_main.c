/*
 * AKENO Party Voice entry point. PS5: send akeno-party-voice.elf to the ELF loader (port 9021) after
 * the AKENO daemon, like the daemon itself, or start it from the homebrew launcher's entry (a
 * background payload: no app is closed). It waits on 127.0.0.1:8646 for the AKENO plugin; nothing
 * is opened (no network beyond 127.0.0.1, no audio port, no microphone) until the user joins voice.
 * It refuses to run twice (the port), never restarts itself and writes only
 * /data/akeno-party-voice/ (report.txt, voice.log, settings.json).
 *
 * PC: a simulated console for the tests:
 *   akeno-party-voice --akeno-dir <daemon data> --data-dir <dir> --daemon-port <n> [--port <n>]
 *   PV_SIM_MIC, PV_SIM_OUT, PV_SIM_BIGAPP_FILE: pv_backend_host.c
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pv.h"
#include "pv_control.h"
#include "pv_engine.h"
#include "pv_internal.h"

static volatile sig_atomic_t g_stop;
static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

#ifndef PV_PS5
static const char *option(int argc, char **argv, const char *name, const char *fallback) {
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return fallback;
}
#endif

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    pv_backend be;
    pv_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    int port = PV_PORT;
#ifdef PV_PS5
    (void)argc;
    (void)argv;
    pv_backend_ps5(&be);
    snprintf(cfg.akeno_dir, sizeof(cfg.akeno_dir), "/data/akeno");
    snprintf(cfg.data_dir, sizeof(cfg.data_dir), "/data/akeno-party-voice");
    cfg.daemon_port = 8642;
    cfg.notifications = 1;
#else
    static pv_sim sim;
    pv_sim_from_env(&sim);
    pv_backend_host(&be, &sim);
    snprintf(cfg.akeno_dir, sizeof(cfg.akeno_dir), "%s", option(argc, argv, "--akeno-dir", "."));
    snprintf(cfg.data_dir, sizeof(cfg.data_dir), "%s", option(argc, argv, "--data-dir", "."));
    cfg.daemon_port = atoi(option(argc, argv, "--daemon-port", "8642"));
    port = atoi(option(argc, argv, "--port", "0"));
    cfg.notifications = 1;
#endif
    cfg.be = &be;
    mkdir(cfg.data_dir, 0755);

    int bound = 0, in_use = 0;
    int fd = pv_control_listen(port, &bound, &in_use);
    if (fd < 0) {
        be.notify(be.user, in_use ? "AKENO Voice is already running." : "AKENO Voice could not open its local port.");
        fprintf(stderr, "[akeno-party-voice] %s\n", in_use ? "already running (port in use)" : "cannot listen");
        return 1;
    }
    /* the previous run's report stays next to the new one */
    char report[300], previous[320];
    snprintf(report, sizeof(report), "%s/report.txt", cfg.data_dir);
    snprintf(previous, sizeof(previous), "%s/report-previous.txt", cfg.data_dir);
    rename(report, previous);

    pv_engine *e = pv_engine_new(&cfg);
    if (!e) {
        close(fd);
        return 1;
    }
    pv_log(e, "service.start", "%s %s (build %s) on %s; control on 127.0.0.1:%d; nothing is opened until you join voice", PV_APP, PV_VERSION,
           PV_BUILD_ID, be.name, bound);
    printf("AKENO_PARTY_VOICE_READY port=%d\n", bound);
    fflush(stdout);
    be.notify(be.user, "AKENO Voice ready: AKENO NETWORK > Party > Voice chat");
    pv_control_config cc;
    memset(&cc, 0, sizeof(cc));
    snprintf(cc.akeno_dir, sizeof(cc.akeno_dir), "%s", cfg.akeno_dir);
    cc.port = bound;
    int exit_requested = 0;
    while (!g_stop && !exit_requested) {
        exit_requested = pv_control_serve(fd, e, &cc, 100);
        pv_engine_tick(e);
    }
    close(fd);
    pv_log(e, "service.stop", "%s", exit_requested ? "stopped from the AKENO plugin" : "stopped by a signal");
    pv_engine_free(e); /* leaves voice, waits for the threads, writes the final report */
    be.notify(be.user, "AKENO Voice stopped.");
    return 0;
}
