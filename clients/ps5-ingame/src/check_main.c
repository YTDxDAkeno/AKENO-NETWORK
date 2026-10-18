/*
 * PC check for the AKENO In-Game Companion: loads the menu from a running AKENO daemon (host
 * build, as in the end-to-end test), prints it, builds the OnionHEN page from it and validates
 * it with the SDK. A PC run says nothing about OnionHEN or a PS5.
 *
 *   akeno-ingame-check --port <daemon port> --akeno-dir <dir> [--presence away] [--accept-invite]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fetch.h"
#include "ui_doc.h"

int main(int argc, char **argv) {
    akn_daemon_link link;
    memset(&link, 0, sizeof(link));
    link.port = 8642;
    snprintf(link.akeno_dir, sizeof(link.akeno_dir), "/data/akeno");
    const char *presence = NULL;
    int accept = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) link.port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--akeno-dir") == 0 && i + 1 < argc) snprintf(link.akeno_dir, sizeof(link.akeno_dir), "%s", argv[++i]);
        else if (strcmp(argv[i], "--presence") == 0 && i + 1 < argc) presence = argv[++i];
        else if (strcmp(argv[i], "--accept-invite") == 0) accept = 1;
        else {
            fprintf(stderr, "usage: %s --port N --akeno-dir DIR [--presence STATUS] [--accept-invite]\n", argv[0]);
            return 2;
        }
    }
    akn_companion *c = calloc(1, sizeof(*c));
    if (!c) return 1;
    char err[200] = "";
    int rc = 0;
    if (presence && akn_companion_set_presence(&link, presence, err, sizeof(err)) != 0) {
        fprintf(stderr, "presence: %s\n", err);
        rc = 1;
    }
    akn_companion_load(&link, c);
    if (accept && akn_companion_accept_invitation(&link, c->invite_id, err, sizeof(err)) != 0) {
        fprintf(stderr, "accept: %s\n", err);
        rc = 1;
    }
    if (presence || accept) akn_companion_load(&link, c);
    char text[4096];
    akn_companion_text(c, text, sizeof(text));
    fputs(text, stdout);
    onion_ui_document *doc = NULL;
    onion_status st = akn_companion_document(c, "PC check", &doc);
    size_t size = 0;
    if (st == ONION_OK) st = onion_ui_document_encoded_size(doc, &size);
    printf("OnionHEN page: %s, %zu nodes, %zu bytes\n", st == ONION_OK ? "valid" : onion_status_string(st), doc ? onion_ui_document_node_count(doc) : 0,
           size);
    printf("Daemon requests: %lu (%lu failed)\n", link.requests, link.failures);
    onion_ui_document_destroy(doc);
    free(c);
    return st == ONION_OK ? rc : 1;
}
