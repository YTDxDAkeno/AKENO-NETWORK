/*
 * The companion's reads and its two writes, through the AKENO daemon's native interface on
 * 127.0.0.1 (the same key, allow-list and proxy AKENO Native uses; docs/PS5_NATIVE_PHASE2.md). The
 * companion opens no connection of its own to the AKENO server and no realtime session.
 */
#ifndef AKN_COMPANION_FETCH_H
#define AKN_COMPANION_FETCH_H

#include "companion.h"

typedef struct {
    int port;               /* the daemon's local port (8642 on the console) */
    char akeno_dir[256];    /* /data/akeno */
    unsigned long requests, failures;
} akn_daemon_link;

/* Loads everything the menu shows (about ten local requests). Never fails: the state says why the
 * menu is short. */
void akn_companion_load(akn_daemon_link *link, akn_companion *out);

/* PUT /api/v1/presence: online, away, dnd or invisible. 0 on success. */
int akn_companion_set_presence(akn_daemon_link *link, const char *status, char *err, size_t err_len);
/* POST /api/v1/invitations/<id>/accept. 0 on success. */
int akn_companion_accept_invitation(akn_daemon_link *link, const char *invitation_id, char *err, size_t err_len);

#endif
