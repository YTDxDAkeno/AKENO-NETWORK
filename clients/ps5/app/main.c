/*
 * Entry point of the AKENO NETWORK console app.
 *
 * PS5: started by an ELF loader without arguments. Data lives in /data/akeno, the page is served
 * on 127.0.0.1:8642 and opened in the system browser.
 * Host (Linux): the same app for development and automated tests, configured with options.
 */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

#define AKENO_DEFAULT_PORT 8642

#if defined(__PROSPERO__)

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    akeno_app_options options = { 0 };
    options.data_dir = "/data/akeno";
    options.ui_port = AKENO_DEFAULT_PORT;
    options.open_browser = 1;
    return akeno_app_main(&options);
}

#else

static void on_signal(int sig) {
    (void)sig;
    akeno_app_request_exit();
}

static void usage(void) {
    fprintf(stderr,
            "AKENO NETWORK console app (host build)\n\n"
            "Usage: akeno-app --data-dir DIR [--server URL] [--ca-file PEM] [--port N] [--open-browser]\n\n"
            "  --data-dir DIR   settings, credential and log directory (created with mode 0700)\n"
            "  --server URL     AKENO server (saved to the settings)\n"
            "  --ca-file PEM    trust these CA certificates instead of the built-in roots\n"
            "  --port N         local page port on 127.0.0.1 (default %d, 0 = any free port)\n"
            "  --open-browser   ask the platform to open the page (host: prints the URL)\n\n"
            "Environment (tests): AKENO_IO_TIMEOUT_MS, AKENO_BACKOFF_BASE_MS, AKENO_MAX_RECONNECTS\n",
            AKENO_DEFAULT_PORT);
}

int main(int argc, char **argv) {
    akeno_app_options options = { 0 };
    options.ui_port = AKENO_DEFAULT_PORT;
    for (int i = 1; i < argc; i++) {
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        if (strcmp(argv[i], "--open-browser") == 0) options.open_browser = 1;
        else if (strcmp(argv[i], "--help") == 0) { usage(); return 0; }
        else if (!value) { usage(); return 2; }
        else if (strcmp(argv[i], "--data-dir") == 0) { options.data_dir = value; i++; }
        else if (strcmp(argv[i], "--server") == 0) { options.server = value; i++; }
        else if (strcmp(argv[i], "--ca-file") == 0) { options.ca_file = value; i++; }
        else if (strcmp(argv[i], "--port") == 0) { options.ui_port = atoi(value); i++; }
        else { usage(); return 2; }
    }
    if (!options.data_dir || options.ui_port < 0 || options.ui_port > 65535) {
        usage();
        return 2;
    }
    const char *env = getenv("AKENO_IO_TIMEOUT_MS");
    if (env) options.io_timeout_ms = atoi(env);
    env = getenv("AKENO_BACKOFF_BASE_MS");
    if (env) options.backoff_base_ms = atoi(env);
    env = getenv("AKENO_MAX_RECONNECTS");
    if (env) options.max_reconnects = atoi(env);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    return akeno_app_main(&options);
}

#endif
