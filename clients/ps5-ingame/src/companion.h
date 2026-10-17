/*
 * AKENO In-Game Companion (EXPERIMENTAL, docs/IN_GAME_ACCESS.md): what the compact AKENO menu shows
 * while a game runs, built from the same daemon answers AKENO Native reads (friends, conversations,
 * party, invitations, notifications, the daemon's state). The menu itself is an OnionHEN Toolbox
 * page contributed through the OnionHEN plugin SDK (ui_doc.c); this model is portable and
 * unit-tested on a PC.
 *
 *   AKENO NETWORK
 *   Friends         5 online
 *   Messages        2 unread
 *   Party           Squad, 3 members
 *   Invitations     1 party invitation
 *   Notifications   1 new
 *   This console    Nioh 3
 */
#ifndef AKN_COMPANION_H
#define AKN_COMPANION_H

#include <stddef.h>

#include "chat.h"
#include "party.h"
#include "social.h"

#define AKN_COMPANION_FRIENDS 8
#define AKN_COMPANION_CONVS 5

typedef enum {
    AKN_COMP_NO_DAEMON = 0, /* nothing answers on the daemon's port */
    AKN_COMP_NO_INTERFACE,  /* the daemon has no native interface (the release build) */
    AKN_COMP_NO_KEY,        /* the key file is missing or not private */
    AKN_COMP_NOT_PAIRED,
    AKN_COMP_READY
} akn_companion_state;

typedef struct {
    char label[64];  /* left column */
    char value[160]; /* right column */
} akn_companion_row;

typedef struct {
    akn_companion_state state;
    char status_line[200];      /* one line about the connection */
    char online_id[40];
    char presence[16];          /* online, away, dnd, invisible ("" unknown) */
    int writes;                 /* the daemon takes writes (native interface 2) */
    /* summary */
    int friends_total, friends_online, friends_playing;
    int unread_messages, unread_conversations;
    int in_party, party_members, party_max;
    char party_name[80];
    char party_voice[160];
    int party_invites, other_invites, friend_requests, notifications_unread;
    char console_game[120];
    /* details */
    akn_companion_row online[AKN_COMPANION_FRIENDS];
    int online_shown;
    akn_companion_row convs[AKN_COMPANION_CONVS];
    int convs_shown;
    char invite_id[40];         /* the newest party invitation (accept action) */
    char invite_line[200];
} akn_companion;

/* Builds the menu from the answers (any may be NULL when it could not be loaded). */
void akn_companion_build(akn_companion *c, akn_companion_state state, const akn_daemon_info *daemon, const akn_friend_list *friends,
                         const akn_conv_list *convs, const akn_party *party, const akn_inbox *inbox, const char *presence, long long now);

/* The summary rows, in menu order. Returns the count. */
int akn_companion_summary(const akn_companion *c, akn_companion_row *out, int max);

/* Text for a person who reads the menu in a terminal (the PC check mode and the logs' tests). */
void akn_companion_text(const akn_companion *c, char *out, size_t len);

const char *akn_companion_presence_label(const char *presence);

#endif
