/*
 * AKENO NETWORK PS5 payload.
 *
 * A userland homebrew ELF for an already-jailbroken PS5. It pairs with an AKENO NETWORK account
 * (no password on the console), then polls the account's notifications and surfaces new ones as
 * native system toasts. It is controller-driven (refresh, cycle presence, sign out, quit) and
 * network-only: it makes no system modifications, loads no kernel code, and touches nothing
 * outside its own data directory.
 *
 * Live updates use bounded polling rather than the WebSocket: the console build does not yet
 * ship a verified TLS byte-stream binding for WSS (see docs/FIRMWARE_COMPATIBILITY.md). All REST
 * calls, pairing, credential handling and the notification formatter are the same code paths
 * tested on Linux.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "akeno/akeno.h"
#include "akeno/events.h"
#include "ps5/notify.h"
#include "ps5/pad.h"
#include "ps5/sce.h"

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

#ifndef AKENO_DEFAULT_SERVER
#define AKENO_DEFAULT_SERVER "https://85.190.112.10.sslip.io"
#endif

#define CONFIG_PATH "/data/akeno/config.txt"
#define CREDENTIAL_PATH "/data/akeno/device.json"
#define POLL_INTERVAL_MS 15000
#define PAD_POLL_MS 150
#define MAX_CONSECUTIVE_FAILURES 20
#define NOTIF_LIMIT 30

static const char *const PRESENCE_CYCLE[] = { "online", "away", "dnd" };

static volatile int g_quit = 0;
static int g_pad = -1;

/* Routes library diagnostics to the debug log (klog/stdout on the console). */
static void log_cb(void *user, akeno_log_level level, const char *event, const char *message) {
    (void)user;
    static const char *const names[] = { "debug", "info", "warn", "error" };
    printf("[akeno:%s] %s: %s\n", names[level], event, message);
}

/* Pressed during a blocking pairing wait: OPTIONS cancels. */
static int should_stop(void *user) {
    (void)user;
    if (g_quit) return 1;
    if (akeno_ps5_pad_poll_pressed(g_pad) & SCE_PAD_BUTTON_OPTIONS) {
        g_quit = 1;
        return 1;
    }
    return 0;
}

/* Reads the server URL from CONFIG_PATH (first line) or falls back to the compiled default. */
static void load_server(char *out, size_t out_len) {
    snprintf(out, out_len, "%s", AKENO_DEFAULT_SERVER);
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) return;
    char line[512];
    if (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) snprintf(out, out_len, "%s", line);
    }
    fclose(f);
}

static void toast(const char *fmt, ...) {
    char message[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);
    printf("[akeno:toast] %s\n", message);
    akeno_ps5_notify(message);
}

/* Blocking pairing: shows the code, waits for website approval, stores the credential. */
static akeno_status run_pairing(akeno_client *client) {
    akeno_pairing pairing;
    akeno_response response;
    akeno_status status = akeno_pairing_start(client, "PlayStation 5 (AKENO)", "PlayStation 5 (homebrew)", &pairing, &response);
    akeno_response_free(&response);
    if (status != AKENO_OK) {
        toast("AKENO: could not start pairing (%s).", akeno_status_name(status));
        return status;
    }
    toast("AKENO pairing: open %s and enter code %s", pairing.verification_uri, pairing.user_code);
    status = akeno_pairing_wait(client, &pairing, should_stop, NULL);
    if (status == AKENO_OK) {
        toast("AKENO: paired as %s.", akeno_client_online_id(client));
    } else if (status == AKENO_ERR_STOPPED) {
        toast("AKENO: pairing cancelled.");
    } else {
        toast("AKENO: pairing failed (%s).", akeno_status_name(status));
    }
    return status;
}

/* Fetches notifications and toasts the ones newer than the watermark. Returns an akeno_status. */
static akeno_status poll_notifications(akeno_client *client, akeno_watermark *watermark) {
    char path[64];
    snprintf(path, sizeof path, "/notifications?limit=%d", NOTIF_LIMIT);
    akeno_response response;
    akeno_status status = akeno_request(client, "GET", path, NULL, &response);
    if (status != AKENO_OK) {
        akeno_response_free(&response);
        return status;
    }
    const akeno_json *list = akeno_json_get(akeno_response_json(&response), "notifications");
    size_t count = list ? list->count : 0;

    /* Visit in ascending id order so the watermark advances correctly whatever the array order. */
    int64_t ids[NOTIF_LIMIT];
    const akeno_json *nodes[NOTIF_LIMIT];
    size_t n = 0;
    for (const akeno_json *item = list ? list->child : NULL; item && n < NOTIF_LIMIT; item = item->next) {
        int64_t id = 0;
        if (akeno_json_int64(akeno_json_get(item, "id"), &id) != 0) continue;
        size_t pos = n;
        while (pos > 0 && ids[pos - 1] > id) {
            ids[pos] = ids[pos - 1];
            nodes[pos] = nodes[pos - 1];
            pos--;
        }
        ids[pos] = id;
        nodes[pos] = item;
        n++;
    }
    for (size_t i = 0; i < n; i++) {
        if (akeno_watermark_observe(watermark, ids[i])) {
            char text[320];
            if (akeno_describe_notification(akeno_json_get(nodes[i], "data"), text, sizeof text) >= 0) toast("%s", text);
        }
    }
    if (!watermark->primed) akeno_watermark_prime(watermark, watermark->highest);
    (void)count;
    akeno_response_free(&response);
    return AKENO_OK;
}

static void set_presence(akeno_client *client, const char *status_name) {
    char body[48];
    snprintf(body, sizeof body, "{\"status\":\"%s\"}", status_name);
    akeno_response response;
    akeno_status status = akeno_request(client, "PUT", "/presence", body, &response);
    akeno_response_free(&response);
    toast(status == AKENO_OK ? "AKENO status: %s" : "AKENO: could not set status (%s)",
          status == AKENO_OK ? status_name : akeno_status_name(status));
}

/* The main poll/notify loop. Returns when the user quits or after too many failures. */
static akeno_status run_loop(akeno_client *client) {
    akeno_watermark watermark;
    akeno_watermark_init(&watermark);
    int failures = 0;
    int presence = 0;
    long long next_poll = 0; /* poll immediately on entry */

    toast("AKENO NETWORK is running. Cross: refresh. Triangle: status. Options: sign out. Circle: quit.");
    while (!g_quit) {
        uint32_t pressed = akeno_ps5_pad_poll_pressed(g_pad);
        if (pressed & SCE_PAD_BUTTON_CIRCLE) break;
        if (pressed & SCE_PAD_BUTTON_CROSS) next_poll = 0;
        if (pressed & SCE_PAD_BUTTON_TRIANGLE) {
            presence = (presence + 1) % (int)(sizeof PRESENCE_CYCLE / sizeof PRESENCE_CYCLE[0]);
            set_presence(client, PRESENCE_CYCLE[presence]);
        }
        if (pressed & SCE_PAD_BUTTON_OPTIONS) {
            toast("AKENO: signing out this console.");
            akeno_logout(client);
            return AKENO_ERR_STOPPED;
        }

        long long now = now_ms();
        if (now >= next_poll) {
            akeno_status status = poll_notifications(client, &watermark);
            if (status == AKENO_OK) {
                failures = 0;
            } else if (status == AKENO_ERR_UNAUTHORIZED) {
                toast("AKENO: this console was unpaired. Pair it again from the website.");
                return status;
            } else if (++failures >= MAX_CONSECUTIVE_FAILURES) {
                toast("AKENO: lost contact with the server. Stopping.");
                return AKENO_ERR_GAVE_UP;
            }
            /* Back off a little while the server is unreachable, capped at a minute. */
            long long interval = status == AKENO_OK ? POLL_INTERVAL_MS : POLL_INTERVAL_MS * (failures < 4 ? failures : 4);
            next_poll = now + interval;
        }
        sleep_ms(PAD_POLL_MS);
    }
    return AKENO_OK;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("AKENO NETWORK PS5 payload " AKENO_CLIENT_VERSION "\n");

    if (akeno_global_init() != AKENO_OK) {
        toast("AKENO: could not initialize networking.");
        return 1;
    }
    g_pad = akeno_ps5_pad_open();

    char server[512];
    load_server(server, sizeof server);

    akeno_config config;
    memset(&config, 0, sizeof config);
    config.base_url = server;
    config.credential_path = CREDENTIAL_PATH;
    config.log = log_cb;
    config.log_level = AKENO_LOG_INFO;

    akeno_client *client = NULL;
    akeno_status status = akeno_client_create(&config, &client);
    if (status != AKENO_OK) {
        toast("AKENO: bad configuration (%s). Check %s.", akeno_status_name(status), CONFIG_PATH);
        akeno_global_cleanup();
        return 1;
    }

    if (!akeno_client_is_paired(client)) {
        status = run_pairing(client);
    }
    if (status == AKENO_OK && akeno_client_is_paired(client)) {
        status = run_loop(client);
    }

    toast("AKENO NETWORK stopped.");
    akeno_client_destroy(client);
    akeno_ps5_pad_close(g_pad);
    akeno_global_cleanup();
    return status == AKENO_OK || status == AKENO_ERR_STOPPED ? 0 : 1;
}
