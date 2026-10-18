/*
 * AKENO In-Game Companion (EXPERIMENTAL, docs/IN_GAME_ACCESS.md): what the AKENO pages in the PS5
 * ShellUI show while a game runs, built from the same daemon answers AKENO Native reads (friends,
 * conversations and their latest messages, the party, friends' parties, invitations, friend
 * requests, notifications, the daemon's state). The pages themselves are an onionHEN Toolbox
 * contribution through the onionHEN plugin SDK (ui_doc.c); this model is portable and unit-tested
 * on a PC.
 *
 *   AKENO NETWORK
 *   @akeno_tester · Online · Nioh 3      > profile and status
 *   Friends · 5 online · 2 playing       > friends > one friend: message, invite, join
 *   Messages · 2 unread                  > conversations > latest messages, reply, mark read
 *   Party · Squad · 3/8                  > members, chat, invite, leave / create, join, accept
 *   Requests & invitations · 1           > accept or decline, notifications
 *   Refresh
 *   Diagnostics & help
 */
#ifndef AKN_COMPANION_H
#define AKN_COMPANION_H

#include <stddef.h>

#include "chat.h"
#include "party.h"
#include "social.h"

/* The summary's lists (text output and the old menu). */
#define AKN_COMPANION_FRIENDS 8
#define AKN_COMPANION_CONVS 5

/* What the pages list. Chosen so that the largest possible document stays below the host's 256
 * nodes (tests/test_companion.c builds it with every list full). */
#define AKN_UI_FRIENDS 12      /* rows on the Friends page */
#define AKN_UI_FRIEND_PAGES 8  /* of these, the first ones get their own page */
#define AKN_UI_CONVS 6         /* conversations, each with its own page */
#define AKN_UI_THREAD 5        /* latest messages per conversation */
#define AKN_UI_MEMBERS 8
#define AKN_UI_INVITE_TARGETS 6 /* online friends you can invite to your party */
#define AKN_UI_JOINABLE 4
#define AKN_UI_REQUESTS 4
#define AKN_UI_INVITES 4
#define AKN_UI_NOTIFICATIONS 3

/* Quick replies typed with the PS5 keyboard. onionHEN v0.9.0 checks an input's value in bytes
 * against max_length and below 256, while the keyboard counts characters with the same number. */
#define AKN_UI_REPLY_MAX 250

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
    char sender[80];   /* "You" for your own */
    char body[256];    /* one line */
    int mine;
    long long at;      /* unix seconds */
} akn_ui_message;

/* The latest messages of one conversation, kept between refreshes (fetch.c loads a conversation
 * again only when its newest message changed). */
typedef struct {
    char conv_id[40];
    long long last_message_id; /* of the conversation list entry the messages were loaded for */
    int state;                 /* 0 not loaded, 1 loaded, -1 failed */
    char error[120];
    akn_ui_message items[AKN_UI_THREAD]; /* oldest first */
    int count;
} akn_thread_cache;

typedef struct {
    char id[40];             /* user ID */
    char online_id[40];
    char name[80];
    char line[160];          /* presence: "Playing Astro Bot", "Away", "Offline · last seen 3 h ago" */
    char status_message[160];
    int online;
    int in_my_party;
    char conv_id[40];        /* their direct conversation when it is on the Messages page, else "" */
    int conv_unread;
    char party_id[40];       /* a party of theirs you may join, else "" */
    char party_name[80];
    int party_members, party_max;
} akn_ui_friend;

typedef struct {
    char id[40];
    akn_conv_kind kind;
    char title[80];
    char preview[140];
    int preview_mine;
    int unread;
    int can_send;
    long long last_message_id;
    akn_thread_cache thread;     /* its latest messages (state 0 when they were not loaded) */
} akn_ui_conv;

typedef struct {
    char id[40];
    char name[80];
    int leader, me, voice_connected, voice_muted;
} akn_ui_member;

typedef struct {
    char id[40];
    char line[160];          /* "Dan · party "Late night"" */
    akn_invitation_kind kind;
} akn_ui_invite;

typedef struct {
    char id[40];
    char name[80];
    char online_id[40];
} akn_ui_request;

typedef struct {
    char id[40];
    char line[160];          /* "Bob's party "Squad" · 2/8" */
} akn_ui_joinable;

typedef struct {
    char id[40];             /* user ID */
    char name[80];
} akn_ui_person;

/* Diagnostics shown on the help page and in the logs. */
typedef struct {
    int daemon_ipc;
    char daemon_version[32];
    char daemon_build[64];
    char realtime[24];
    char realtime_detail[160];
    unsigned long requests, failures;
    long long loaded_at;     /* unix seconds of the last load without errors, 0 never */
    char last_error[200];
    int sections_failed;     /* bit set of what could not be loaded the last time */
} akn_ui_diag;

typedef struct {
    akn_companion_state state;
    char status_line[200];      /* one line about the connection */
    char online_id[40];
    char display_name[80];
    char self_id[40];
    char presence[16];          /* online, away, dnd, invisible ("" unknown) */
    int writes;                 /* the daemon takes writes (native interface 2 or later) */
    /* summary */
    int friends_total, friends_online, friends_playing;
    int unread_messages, unread_conversations;
    int in_party, party_members, party_max;
    char party_id[40];
    char party_name[80];
    char party_conv_id[40];
    char party_voice[160];
    int party_invites, other_invites, friend_requests, notifications_unread;
    char console_game[120];
    /* the summary's details */
    akn_companion_row online[AKN_COMPANION_FRIENDS];
    int online_shown;
    akn_companion_row convs[AKN_COMPANION_CONVS];
    int convs_shown;
    char invite_id[40];         /* the newest party invitation */
    char invite_line[200];
    /* the pages */
    akn_ui_friend friends[AKN_UI_FRIENDS];
    int friends_shown;
    akn_ui_conv conversations[AKN_UI_CONVS];
    int conversations_shown, conversations_total;
    akn_ui_member members[AKN_UI_MEMBERS];
    int members_shown;
    akn_ui_person invite_targets[AKN_UI_INVITE_TARGETS]; /* online friends who are not in your party */
    int invite_targets_shown;
    akn_ui_joinable joinable[AKN_UI_JOINABLE];
    int joinable_shown;
    akn_ui_invite invites[AKN_UI_INVITES];
    int invites_shown;
    akn_ui_request requests[AKN_UI_REQUESTS];
    int requests_shown;
    char notifications[AKN_UI_NOTIFICATIONS][200];
    int notifications_shown;
    akn_ui_diag diag;
} akn_companion;

/* Everything beyond the summary's inputs; any member may be NULL / 0. */
typedef struct {
    const akn_profile *me;              /* your profile (self ID, display name) */
    const akn_party_list *joinable;     /* GET /parties */
    const akn_thread_cache *threads;    /* latest messages by conversation */
    int thread_count;
    const akn_ui_diag *diag;
} akn_companion_extra;

/* Builds the menu from the answers (any may be NULL when it could not be loaded). */
void akn_companion_build(akn_companion *c, akn_companion_state state, const akn_daemon_info *daemon, const akn_friend_list *friends,
                         const akn_conv_list *convs, const akn_party *party, const akn_inbox *inbox, const char *presence, long long now);
void akn_companion_build_ex(akn_companion *c, akn_companion_state state, const akn_daemon_info *daemon, const akn_friend_list *friends,
                            const akn_conv_list *convs, const akn_party *party, const akn_inbox *inbox, const char *presence, long long now,
                            const akn_companion_extra *extra);

/* The summary rows, in menu order. Returns the count. */
int akn_companion_summary(const akn_companion *c, akn_companion_row *out, int max);

/* Text for a person who reads the menu in a terminal (the PC check mode and the logs' tests). */
void akn_companion_text(const akn_companion *c, char *out, size_t len);

const char *akn_companion_presence_label(const char *presence);

/* Drops an incomplete UTF-8 character at the end of s (what a byte-limited copy leaves behind). */
void akn_utf8_whole(char *s);
/* At most max bytes of src into buf (cap bytes), never half a character; returns buf. */
const char *akn_utf8_cut(char *buf, size_t cap, const char *src, size_t max);

/* A server ID that may appear in a node ID and in a daemon path segment: 1-39 of [A-Za-z0-9-]. */
int akn_companion_id_ok(const char *id);

/* Lookups by ID in the built model; NULL when not shown. */
const akn_ui_friend *akn_companion_friend(const akn_companion *c, const char *user_id);
const akn_ui_conv *akn_companion_conv(const akn_companion *c, const char *conv_id);

#endif
