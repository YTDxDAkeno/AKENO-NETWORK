/*
 * AKENO NETWORK console app: shared state of app.c (lifecycle, workers), app_routes.c (local
 * API) and ui_server.c (local HTTP server).
 *
 * Threading model
 *   main thread      local UI server loop, all local API requests, every thread start/join
 *   realtime thread  akeno_realtime_run() with its own akeno_client
 *   pairing thread   akeno_pairing_wait() with its own akeno_client
 * Worker threads never touch another thread's client. They report through fields guarded by
 * app->lock and the main thread applies the consequences in app_tick().
 */
#ifndef AKENO_APP_INTERNAL_H
#define AKENO_APP_INTERNAL_H

#include <pthread.h>
#include <stdio.h>

#include "akeno/akeno.h"
#include "app.h"
#include "buf.h"
#include "platform.h"

#define AKENO_APP_VERSION "0.1.0-alpha"
#ifndef AKENO_BUILD_ID
#define AKENO_BUILD_ID "dev"
#endif
#define AKENO_PLATFORM_LABEL "PlayStation 5 (homebrew)"

#define APP_EVENT_RING 128
#define APP_LOG_RING 80
#define APP_EVENT_MAX_BYTES (16 * 1024)

typedef enum { PAIR_IDLE = 0, PAIR_WAITING, PAIR_DONE, PAIR_DENIED, PAIR_EXPIRED, PAIR_FAILED, PAIR_CANCELLED } app_pair_state;

typedef struct {
    unsigned long long seq;
    char *json; /* {"seq":N,"type":"...","data":...} */
} app_event;

struct akeno_app {
    akeno_app_options opt;
    akeno_platform_info platform;
    pthread_mutex_t lock;
    volatile int stop;
    long long started_at;

    /* Paths (immutable after start). */
    char data_dir[512];
    char settings_path[600];
    char credential_path[600];
    char log_path[600];
    char instance_path[600];
    char ca_path[600];
    const char *ca_file; /* NULL = embedded roots. */
    int custom_ca;

    /* Settings (lock). */
    char server[300];
    char device_name[64];
    int notify_messages;
    int notify_social;

    /* Local UI (immutable after start). */
    int port;
    char session_token[65];
    char instance_key[65];
    int browser_launches;
    int browser_last_rc;

    /* Main thread only. */
    akeno_client *ui_client;

    /* Shared status (lock). */
    int paired;
    char online_id[40];
    int credentials_changed;

    /* Pairing (lock, except the fields owned by the pairing thread while it runs). */
    app_pair_state pair_state;
    char pair_code[16];
    char pair_uri[320];
    char pair_uri_complete[352];
    long long pair_expires_at;
    char pair_error[200];
    pthread_t pair_thread;
    int pair_thread_running; /* main thread only */
    int pair_thread_done;
    volatile int pair_cancel;
    akeno_client *pair_client; /* owned by the pairing thread while it runs */
    akeno_pairing pairing;

    /* Realtime (lock). */
    pthread_t rt_thread;
    int rt_thread_running; /* main thread only */
    int rt_thread_done;
    volatile int rt_stop;
    int rt_wanted;
    char rt_state[16];
    char rt_detail[200];
    long long rt_changed_at;

    /* Events for the UI (lock). */
    app_event events[APP_EVENT_RING];
    unsigned long long next_seq;

    /* Notifications (lock). */
    double notify_tokens;
    long long notify_refill_at;
    int notifications_sent;
    int notifications_failed;
    int notifications_dropped;

    /* Diagnostics log ring (log_lock). */
    pthread_mutex_t log_lock;
    FILE *log;
    char log_ring[APP_LOG_RING][240];
    int log_next;
    int log_count;
};

/* app.c */
void app_log(akeno_app *app, akeno_log_level level, const char *event, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
akeno_status app_make_client(akeno_app *app, akeno_client **out);
void app_tick(akeno_app *app);
akeno_status app_save_settings(akeno_app *app);
void app_push_event(akeno_app *app, const char *type, const char *data_json);
void app_notify(akeno_app *app, int social, const char *text);
akeno_status app_pair_start(akeno_app *app, char *err, size_t err_len);
void app_pair_cancel(akeno_app *app);
void app_mark_unpaired(akeno_app *app, const char *why);
void app_open_ui(akeno_app *app);

/* app_routes.c */
typedef struct {
    int status;
    const char *content_type;
    akeno_buf body;
    const char *static_body; /* Alternative to body for embedded assets. */
    size_t static_len;
    int cacheable;
} ui_response;

struct ui_request_s;
void app_route(akeno_app *app, const struct ui_request_s *request, const char *body, size_t body_len, ui_response *response);

/* ui_server.c */
int ui_server_listen(int port, int *bound_port, int *in_use);
void ui_server_loop(akeno_app *app, int listen_fd);
/* Asks an already running instance on port to show its UI. Returns 0 when it accepted. */
int ui_server_signal_existing(int port, const char *instance_key);

/* ui_assets.c */
extern const char akeno_ui_index[], akeno_ui_index_end[];
extern const char akeno_ui_js[], akeno_ui_js_end[];
extern const char akeno_ui_css[], akeno_ui_css_end[];

#endif
