/*
 * AKENO In-Game Companion (EXPERIMENTAL): an onionHEN plugin process. onionHEN starts it from
 * /data/OnionHEN/plugins/AKEN00001.elf; it adds AKENO NETWORK pages to the onionHEN Toolbox (PS5
 * Settings, also reachable while a game keeps running) and keeps them current from the AKENO
 * daemon's native interface. It does not inject into any process, read the controller, play or
 * record audio, open the browser or contact the AKENO server itself: onionHEN draws the pages and
 * delivers the presses and typed text, the daemon owns the connection. docs/IN_GAME_ACCESS.md.
 */
#include <onion/client.h>
#include <onion/ipc.h>
#include <onion/plugin.h>
#include <onion/services.h>
#include <onion/status.h>
#include <onion/transport.h>
#include <onion/ui.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "actions.h"
#include "fetch.h"
#include "ui_doc.h"

/* onionHEN v0.9.0's host rejects AUTO_START (it uses its own marker file); NOTIFY shows the result
 * of a press as a system notification, since a page shows new data only when it is opened again. */
ONION_PLUGIN_DEFINE(AKN_COMPANION_PLUGIN_ID, AKN_COMPANION_VERSION, "AKENO NETWORK (experimental)",
                    ONION_PLUGIN_CAP_NOTIFY | ONION_PLUGIN_CAP_IPC | ONION_PLUGIN_CAP_UI,
                    ONION_PLUGIN_FLAG_LONG_RUNNING | ONION_PLUGIN_FLAG_STOP_SUPPORTED);

#define POLL_MS 2000          /* the daemon's local event ring */
#define RETRY_MS 5000         /* while the daemon is not ready */
#define FULL_MS 60000         /* everything again, in case an event was missed */
#define RECONNECT_MS 5000     /* onionHEN's plugin socket */
#define NOTE_MS 120000        /* how long the last action's result stays on the main page */
#define TOMBSTONE_MS 300000

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

/* Logs carry states and verbs only: never names, IDs or message text. */
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

/* FNV-1a of the encoded document: publish only when something visible changed. */
static uint64_t doc_hash(const onion_ui_document *doc) {
    size_t size = 0, used = 0;
    if (onion_ui_document_encoded_size(doc, &size) != ONION_OK) return 0;
    unsigned char *buf = malloc(size);
    uint64_t h = 1469598103934665603ull;
    if (buf && onion_ui_document_encode(doc, buf, size, &used) == ONION_OK)
        for (size_t i = 0; i < used; i++) h = (h ^ buf[i]) * 1099511628211ull;
    free(buf);
    return h;
}

/* Replaces the pages (same contribution: atomic, keeps the handle). */
static int publish(host_link *h, const onion_ui_document *doc) {
    onion_ui_handle handle = 0;
    if (onion_ui_register(&h->host, doc, &handle) != ONION_OK) return -1;
    h->handle = handle;
    return 0;
}

typedef struct {
    char ids[AKN_UI_TOMBSTONES][AKN_UI_PAGE_ID_MAX];
    long long until[AKN_UI_TOMBSTONES];
    int count;
} tombstones;

static int has_id(char ids[][AKN_UI_PAGE_ID_MAX], int n, const char *id) {
    for (int i = 0; i < n; i++)
        if (strcmp(ids[i], id) == 0) return 1;
    return 0;
}

/* Pages the last document had and this one has not become tombstones; those back or expired go. */
static void update_tombstones(tombstones *t, char prev[][AKN_UI_PAGE_ID_MAX], int prev_n, char cur[][AKN_UI_PAGE_ID_MAX], int cur_n,
                              long long now) {
    for (int i = 0; i < t->count;) {
        if (t->until[i] <= now || has_id(cur, cur_n, t->ids[i])) {
            t->count--;
            memmove(t->ids[i], t->ids[t->count], sizeof(t->ids[0]));
            t->until[i] = t->until[t->count];
        } else {
            i++;
        }
    }
    for (int i = 0; i < prev_n; i++) {
        if (has_id(cur, cur_n, prev[i]) || has_id(t->ids, t->count, prev[i])) continue;
        int slot = 0;
        if (t->count < AKN_UI_TOMBSTONES) {
            slot = t->count++;
        } else { /* full: the oldest goes */
            for (int k = 1; k < t->count; k++)
                if (t->until[k] < t->until[slot]) slot = k;
        }
        snprintf(t->ids[slot], sizeof(t->ids[slot]), "%s", prev[i]);
        t->until[slot] = now + TOMBSTONE_MS;
    }
}

int main(void) {
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    akn_daemon_link link;
    memset(&link, 0, sizeof(link));
    link.port = 8642;
    snprintf(link.akeno_dir, sizeof(link.akeno_dir), "/data/akeno");
    akn_store *store = akn_store_new();
    akn_companion *model = calloc(1, sizeof(*model));
    char (*prev_ids)[AKN_UI_PAGE_ID_MAX] = calloc(AKN_UI_PAGES_MAX, AKN_UI_PAGE_ID_MAX);
    char (*cur_ids)[AKN_UI_PAGE_ID_MAX] = calloc(AKN_UI_PAGES_MAX, AKN_UI_PAGE_ID_MAX);
    tombstones *tomb = calloc(1, sizeof(*tomb));
    if (!store || !model || !prev_ids || !cur_ids || !tomb) return 1;
    int prev_n = 0;
    char note[200] = "";
    long long note_until = 0;
    host_link host;
    memset(&host, 0, sizeof(host));
    long long next_poll = 0, next_full = 0, next_connect = 0;
    unsigned want = AKN_LOAD_ALL;
    int dirty = 1;
    uint64_t published = 0;
    akn_companion_state last_state = AKN_COMP_READY + 1;
    say("starting");
    while (g_running) {
        long long now = now_ms();
        if (!host.open) {
            if (now < next_connect) {
                usleep(200 * 1000);
                continue;
            }
            next_connect = now + RECONNECT_MS;
            if (host_open(&host) != 0) continue; /* onionHEN not ready, or rest mode: try again */
            say("connected to onionHEN");
            published = 0;
            dirty = 1;
        }
        if (now >= next_poll) {
            int changed = 0;
            want |= akn_store_poll(&link, store, &changed);
            dirty |= changed;
            next_poll = now + (store->state == AKN_COMP_READY ? POLL_MS : RETRY_MS);
        }
        if (now >= next_full) {
            want = AKN_LOAD_ALL;
            next_full = now + FULL_MS;
        }
        if (want) {
            unsigned failed = akn_store_load(&link, store, want, (long long)time(NULL));
            if (store->state != last_state) {
                say(store->state == AKN_COMP_READY ? "daemon ready" : "daemon not ready; the page says why");
                last_state = store->state;
            }
            if (failed && store->state == AKN_COMP_READY) say("a part could not be loaded; shown on the diagnostics page");
            want = 0;
            dirty = 1;
        }
        if (note[0] && now >= note_until) {
            note[0] = '\0';
            dirty = 1;
        }
        if (dirty) {
            dirty = 0;
            akn_store_model(store, model, (long long)time(NULL));
            int cur_n = akn_companion_page_ids(model, cur_ids, AKN_UI_PAGES_MAX);
            update_tombstones(tomb, prev_ids, prev_n, cur_ids, cur_n, now);
            memcpy(prev_ids, cur_ids, (size_t)AKN_UI_PAGES_MAX * AKN_UI_PAGE_ID_MAX);
            prev_n = cur_n;
            akn_doc_options o;
            memset(&o, 0, sizeof(o));
            o.note = note;
            o.tombstones = tomb->ids;
            o.tombstone_count = tomb->count;
            onion_ui_document *doc = NULL;
            if (akn_companion_document_ex(model, &o, &doc) != ONION_OK) {
                say("the pages could not be built");
            } else {
                uint64_t h = doc_hash(doc);
                if (h != published) {
                    if (publish(&host, doc) != 0) {
                        say("the pages could not be published; reconnecting");
                        onion_ui_document_destroy(doc);
                        host_close(&host);
                        continue;
                    }
                    published = h;
                }
                onion_ui_document_destroy(doc);
            }
        }
        onion_ui_event_v1 ev;
        onion_status st = onion_client_poll_ui_event(&host.client, &ev);
        if (st == ONION_E_NOT_FOUND) {
            usleep(100 * 1000);
            continue;
        }
        if (st != ONION_OK) {
            say("lost onionHEN; reconnecting");
            host_close(&host);
            continue;
        }
        ev.node_id[sizeof(ev.node_id) - 1] = '\0';
        ev.value[sizeof(ev.value) - 1] = '\0';
        akn_action_result res;
        akn_companion_action(&link, model, ev.node_id, ev.value, &res);
        if (!res.handled) continue;
        say(res.ok ? "action done" : "action failed; the page says why");
        if (res.notify && res.message[0]) (void)onion_service_notify(&host.host, res.message);
        snprintf(note, sizeof(note), "%s", res.notify ? res.message : "");
        note_until = now + NOTE_MS;
        want |= res.reload;
        dirty = 1;
        published = 0; /* a text field keeps what was typed until the pages are published again */
        memset(&ev, 0, sizeof(ev));
    }
    host_close(&host);
    akn_store_free(store);
    memset(model, 0, sizeof(*model));
    free(model);
    free(prev_ids);
    free(cur_ids);
    free(tomb);
    say("stopped");
    return 0;
}
