/*
 * The companion's reads and writes, through the AKENO daemon's native interface on 127.0.0.1 (the
 * same key, allow-list and proxy AKENO Native uses; docs/PS5_NATIVE_PHASE2.md). The companion opens
 * no connection of its own to the AKENO server and no realtime session: it reads the daemon's
 * local event ring to learn what changed and then reloads only that part.
 */
#ifndef AKN_COMPANION_FETCH_H
#define AKN_COMPANION_FETCH_H

#include "companion.h"

struct akeno_json;
struct akeno_json_doc;

typedef struct {
    int port;               /* the daemon's local port (8642 on the console) */
    char akeno_dir[256];    /* /data/akeno */
    unsigned long requests, failures;
} akn_daemon_link;

/* What a load reads (the daemon's hello and state are read every time). */
enum {
    AKN_LOAD_PROFILE = 1 << 0,  /* your profile and status */
    AKN_LOAD_FRIENDS = 1 << 1,
    AKN_LOAD_MESSAGES = 1 << 2, /* conversations, and the latest messages of those whose newest changed */
    AKN_LOAD_PARTY = 1 << 3,    /* your party and your friends' parties */
    AKN_LOAD_INBOX = 1 << 4,    /* friend requests, invitations, notifications */
    AKN_LOAD_ALL = (1 << 5) - 1
};

/* Everything loaded so far; kept between refreshes. */
typedef struct {
    akn_companion_state state;
    akn_daemon_info daemon;
    akn_profile me;
    int have_me;
    char presence[16];
    akn_friend_list friends;
    int have_friends;
    akn_conv_list convs;
    int have_convs;
    akn_party party;
    int have_party;
    akn_party_list joinable;
    int have_joinable;
    akn_inbox inbox;
    int have_inbox;
    akn_thread_cache threads[AKN_UI_CONVS];
    int thread_count;
    unsigned long long events_after;
    int events_ready;
    akn_ui_diag diag;
    unsigned long thread_loads; /* conversations whose messages were (re)loaded */
} akn_store;

akn_store *akn_store_new(void);
void akn_store_free(akn_store *store);

/* Loads the daemon's state and the given sections. Returns the sections that could not be loaded
 * (0 when everything worked); the store says why in diag.last_error and its state. */
unsigned akn_store_load(akn_daemon_link *link, akn_store *store, unsigned sections, long long now);

/* Reads the daemon's event ring since the last call. Presence changes are applied at once (*changed
 * is set); returns the sections to reload (AKN_LOAD_ALL after a gap or when the ring cannot be read). */
unsigned akn_store_poll(akn_daemon_link *link, akn_store *store, int *changed);

/* The pages' model from the store. */
void akn_store_model(const akn_store *store, akn_companion *out, long long now);

/* One write through the daemon's native write list (POST or PUT under /api/v1). 0 on success; on
 * success *doc (if asked for) holds the answer and *data its "data" member: free with
 * akeno_json_free. err says why it failed in a sentence for the page. */
int akn_daemon_write(akn_daemon_link *link, const char *method, const char *path, const char *body, char *err, size_t err_len,
                     struct akeno_json_doc **doc, const struct akeno_json **data);

/* Loads everything the menu shows (about fifteen local requests). Never fails: the state says why
 * the menu is short. */
void akn_companion_load(akn_daemon_link *link, akn_companion *out);

/* PUT /api/v1/presence: online, away, dnd or invisible. 0 on success. */
int akn_companion_set_presence(akn_daemon_link *link, const char *status, char *err, size_t err_len);
/* POST /api/v1/invitations/<id>/accept. 0 on success. */
int akn_companion_accept_invitation(akn_daemon_link *link, const char *invitation_id, char *err, size_t err_len);

#endif
