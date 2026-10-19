/*
 * PC check for the AKENO In-Game Companion: loads the pages from a running AKENO daemon (host build,
 * as in the end-to-end test), prints them, builds the onionHEN document from them and validates it
 * with the SDK. --event presses a control the way onionHEN delivers it from the PS5 (node ID and,
 * for a list or a text field, its value) through the same code as the plugin. A PC run says nothing
 * about onionHEN or a PS5.
 *
 *   akeno-ingame-check --port <daemon port> --akeno-dir <dir> [--voice-port <n>] [--presence away] [--accept-invite]
 *                      [--event <node id> [<value>]]... [--pages]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "actions.h"
#include "fetch.h"
#include "ui_doc.h"

#define EVENTS_MAX 8

int main(int argc, char **argv) {
    akn_daemon_link link;
    memset(&link, 0, sizeof(link));
    link.port = 8642;
    snprintf(link.akeno_dir, sizeof(link.akeno_dir), "/data/akeno");
    const char *presence = NULL;
    const char *event_ids[EVENTS_MAX], *event_values[EVENTS_MAX];
    int events = 0, accept = 0, pages = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) link.port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--voice-port") == 0 && i + 1 < argc) link.voice_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--akeno-dir") == 0 && i + 1 < argc) snprintf(link.akeno_dir, sizeof(link.akeno_dir), "%s", argv[++i]);
        else if (strcmp(argv[i], "--presence") == 0 && i + 1 < argc) presence = argv[++i];
        else if (strcmp(argv[i], "--accept-invite") == 0) accept = 1;
        else if (strcmp(argv[i], "--pages") == 0) pages = 1;
        else if (strcmp(argv[i], "--event") == 0 && i + 1 < argc && events < EVENTS_MAX) {
            event_ids[events] = argv[++i];
            event_values[events] = i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0 ? argv[++i] : "";
            events++;
        } else {
            fprintf(stderr, "usage: %s --port N --akeno-dir DIR [--voice-port N] [--presence STATUS] [--accept-invite] [--event NODE [VALUE]]... [--pages]\n",
                    argv[0]);
            return 2;
        }
    }
    akn_companion *c = calloc(1, sizeof(*c));
    akn_store *store = akn_store_new();
    char *text = malloc(16384);
    if (!c || !store || !text) {
        free(c);
        akn_store_free(store);
        free(text);
        return 1;
    }
    char err[200] = "";
    int rc = 0;
    if (presence && akn_companion_set_presence(&link, presence, err, sizeof(err)) != 0) {
        fprintf(stderr, "presence: %s\n", err);
        rc = 1;
    }
    /* the plugin's own store: one full load, then only what a press changed, as on the console */
    akn_store_load(&link, store, AKN_LOAD_ALL, (long long)time(NULL));
    akn_store_model(store, c, (long long)time(NULL));
    if (accept) {
        if (akn_companion_accept_invitation(&link, c->invite_id, err, sizeof(err)) != 0) {
            fprintf(stderr, "accept: %s\n", err);
            rc = 1;
        }
        akn_store_load(&link, store, AKN_LOAD_PARTY | AKN_LOAD_INBOX | AKN_LOAD_MESSAGES, (long long)time(NULL));
        akn_store_model(store, c, (long long)time(NULL));
    }
    for (int i = 0; i < events; i++) {
        akn_action_result res;
        akn_companion_action(&link, c, event_ids[i], event_values[i], &res);
        printf("Action %s: %s: %s\n", event_ids[i], res.ok ? "ok" : res.handled ? "failed" : "unknown", res.message);
        if (!res.ok) rc = 1;
        if (res.reload) akn_store_load(&link, store, res.reload, (long long)time(NULL));
        akn_store_model(store, c, (long long)time(NULL));
    }
    akn_companion_text(c, text, 16384);
    fputs(text, stdout);
    free(text);
    onion_ui_document *doc = NULL;
    onion_status st = akn_companion_document(c, "PC check", &doc);
    size_t size = 0;
    if (st == ONION_OK) st = onion_ui_document_encoded_size(doc, &size);
    printf("OnionHEN page: %s, %zu nodes, %zu bytes\n", st == ONION_OK ? "valid" : onion_status_string(st), doc ? onion_ui_document_node_count(doc) : 0,
           size);
    if (pages) {
        char ids[AKN_UI_PAGES_MAX][AKN_UI_PAGE_ID_MAX];
        int n = akn_companion_page_ids(c, ids, AKN_UI_PAGES_MAX);
        for (int i = 0; i < n; i++) printf("Page %s\n", ids[i]);
    }
    printf("Daemon requests: %lu (%lu failed)\n", link.requests, link.failures);
    onion_ui_document_destroy(doc);
    akn_store_free(store);
    free(c);
    return st == ONION_OK ? rc : 1;
}
