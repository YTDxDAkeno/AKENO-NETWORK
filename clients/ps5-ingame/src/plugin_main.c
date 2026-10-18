/*
 * AKENO In-Game Companion (EXPERIMENTAL): an OnionHEN plugin process. OnionHEN starts it from
 * /data/OnionHEN/plugins/AKEN00001.elf; it adds an "AKENO NETWORK" page to the OnionHEN Toolbox
 * (PS5 Settings, also reachable while a game keeps running) and keeps it current from the AKENO
 * daemon's native interface. It does not inject into any process, read the controller, play or
 * record audio, open the browser or contact the AKENO server itself: OnionHEN draws the page and
 * delivers the selections, the daemon owns the connection. docs/IN_GAME_ACCESS.md.
 */
#include <onion/client.h>
#include <onion/ipc.h>
#include <onion/plugin.h>
#include <onion/status.h>
#include <onion/transport.h>
#include <onion/ui.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fetch.h"
#include "ui_doc.h"

ONION_PLUGIN_DEFINE(AKN_COMPANION_PLUGIN_ID, AKN_COMPANION_VERSION, "AKENO NETWORK (experimental)",
                    ONION_PLUGIN_CAP_IPC | ONION_PLUGIN_CAP_UI,
                    ONION_PLUGIN_FLAG_AUTO_START | ONION_PLUGIN_FLAG_LONG_RUNNING | ONION_PLUGIN_FLAG_STOP_SUPPORTED);

#define REFRESH_MS 10000
#define RECONNECT_MS 5000

static volatile sig_atomic_t g_running = 1;
static void stop(int sig) {
    (void)sig;
    g_running = 0;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void say(const char *what) { printf("[akeno-ingame] %s\n", what); fflush(stdout); }

typedef struct {
    onion_transport transport;
    onion_socket_transport socket_state;
    onion_client client;
    onion_host_services_v1 host;
    onion_ui_handle handle;
    int open;
} host_link;

static void host_close(host_link *h) {
    if (h->handle) (void)onion_ui_unregister(&h->host, h->handle);
    if (h->open) {
        onion_client_deinit(&h->client);
        onion_socket_transport_deinit(&h->transport);
    }
    memset(h, 0, sizeof(*h));
}

static int host_open(host_link *h) {
    memset(h, 0, sizeof(*h));
    onion_client init = ONION_CLIENT_INITIALIZER;
    h->client = init;
    onion_status st = onion_socket_transport_connect(&h->transport, &h->socket_state, ONION_PLUGIN_IPC_SOCKET_PATH);
    if (st != ONION_OK) return -1;
    h->open = 1;
    if (onion_client_init(&h->client, &h->transport) != ONION_OK || onion_client_open_session(&h->client, &onion_plugin_descriptor) != ONION_OK ||
        onion_client_make_services(&h->client, &h->host) != ONION_OK) {
        host_close(h);
        return -1;
    }
    return 0;
}

/* Replaces the page (same contribution: atomic, keeps the handle). */
static int publish(host_link *h, const akn_companion *c, const char *note) {
    onion_ui_document *doc = NULL;
    if (akn_companion_document(c, note, &doc) != ONION_OK) return -1;
    onion_ui_handle handle = 0;
    onion_status st = onion_ui_register(&h->host, doc, &handle);
    onion_ui_document_destroy(doc);
    if (st != ONION_OK) return -1;
    h->handle = handle;
    return 0;
}

int main(void) {
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    akn_daemon_link link;
    memset(&link, 0, sizeof(link));
    link.port = 8642;
    snprintf(link.akeno_dir, sizeof(link.akeno_dir), "/data/akeno");
    akn_companion *model = calloc(1, sizeof(*model));
    char *last = calloc(1, 4096), *text = calloc(1, 4096);
    if (!model || !last || !text) return 1;
    char note[256] = "Reads the AKENO daemon on this console. AKENO NETWORK Native has the full app.";
    host_link host;
    memset(&host, 0, sizeof(host));
    long long next_refresh = 0, next_connect = 0;
    say("starting");
    while (g_running) {
        long long now = now_ms();
        if (!host.open) {
            if (now < next_connect) {
                usleep(200 * 1000);
                continue;
            }
            next_connect = now + RECONNECT_MS;
            if (host_open(&host) != 0) continue; /* OnionHEN not ready, or rest mode: try again */
            say("connected to OnionHEN");
            last[0] = '\0';
            next_refresh = 0;
        }
        if (now >= next_refresh) {
            akn_companion_load(&link, model);
            akn_companion_text(model, text, 4096);
            if (strcmp(text, last) != 0) {
                if (publish(&host, model, note) != 0) {
                    say("the page could not be published; reconnecting");
                    host_close(&host);
                    continue;
                }
                memcpy(last, text, 4096);
            }
            next_refresh = now + REFRESH_MS;
        }
        onion_ui_event_v1 ev;
        onion_status st = onion_client_poll_ui_event(&host.client, &ev);
        if (st == ONION_E_NOT_FOUND) {
            usleep(200 * 1000);
            continue;
        }
        if (st != ONION_OK) {
            say("lost OnionHEN; reconnecting");
            host_close(&host);
            continue;
        }
        char err[200] = "";
        int rc = 0;
        if (strcmp(ev.node_id, "presence") == 0) rc = akn_companion_set_presence(&link, ev.value, err, sizeof(err));
        else if (strcmp(ev.node_id, "accept_invite") == 0) rc = akn_companion_accept_invitation(&link, model->invite_id, err, sizeof(err));
        /* "refresh" and anything else: load again */
        snprintf(note, sizeof(note), "%s", rc == 0 ? "Reads the AKENO daemon on this console. AKENO NETWORK Native has the full app."
                                                   : err);
        next_refresh = 0;
        last[0] = '\0';
    }
    host_close(&host);
    memset(model, 0, sizeof(*model));
    free(model);
    free(last);
    free(text);
    say("stopped");
    return 0;
}
