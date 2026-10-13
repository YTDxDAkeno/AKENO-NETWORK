/* AKENO NETWORK console app (PS5 payload; also builds on Linux for testing). */
#ifndef AKENO_APP_H
#define AKENO_APP_H

typedef struct akeno_app akeno_app;

typedef struct {
    const char *data_dir;   /* Settings, credential and log directory (required). */
    const char *server;     /* Optional server URL; saved to the settings when given. */
    const char *ca_file;    /* Optional PEM trust store; default: <data_dir>/ca.pem if present, else built-in roots. */
    int ui_port;            /* Local UI port on 127.0.0.1; 0 picks a free port (tests). */
    int open_browser;       /* Launch the system browser at startup. */
    int io_timeout_ms;      /* 0 = library default. */
    int backoff_base_ms;    /* Realtime reconnect tuning; 0 = defaults. */
    int max_reconnects;
} akeno_app_options;

/* Runs the app until it is asked to exit. Returns a process exit code. */
int akeno_app_main(const akeno_app_options *options);

/* Async-signal-safe request to exit (host builds). */
void akeno_app_request_exit(void);

#endif
