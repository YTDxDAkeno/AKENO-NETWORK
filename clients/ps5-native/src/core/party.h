/*
 * Parties, invitations, friend requests and notifications in AKENO Native (Phase 2.3), parsed from
 * the existing server API through the daemon: GET /parties/current, GET /parties (parties you may
 * join), GET /invitations, GET /friends/requests and GET /notifications. Actions go through the
 * daemon's native write list: create, join, leave, invite (POST /parties...), accept or decline
 * invitations and friend requests, mark notifications read.
 *
 * Voice: the server reports whether party voice is available and each member's voice state
 * (connected, muted); AKENO Native joins voice through the AKENO voice gateway (docs/PS5_VOICE.md).
 * Portable and unit-tested.
 */
#ifndef AKN_PARTY_H
#define AKN_PARTY_H

#include <stddef.h>

#include "social.h"

struct akeno_json;

#define AKN_PARTY_MEMBERS 16
#define AKN_JOINABLE_MAX 24
#define AKN_INVITES_MAX 32
#define AKN_REQUESTS_MAX 64
#define AKN_NOTIFICATIONS_MAX 30

typedef struct {
    char id[40], online_id[40], display_name[80];
    char avatar[33];
    akn_presence_view presence;
    int leader;
    int voice_connected;
    int voice_muted;     /* -1 unknown */
} akn_party_member;

typedef struct {
    int present;
    char id[40];
    char name[80];
    char privacy[16];    /* "friends" or "invite_only" */
    char leader_id[40];
    char conversation_id[40];
    int max_members, member_count;
    int is_member;
    akn_party_member members[AKN_PARTY_MEMBERS];
    int members_shown;
    int voice_available;
    char voice_reason[160];
} akn_party;

typedef struct {
    akn_party items[AKN_JOINABLE_MAX];
    int count;
} akn_party_list;

typedef enum { AKN_INV_PARTY = 0, AKN_INV_GROUP, AKN_INV_SESSION, AKN_INV_OTHER } akn_invitation_kind;

typedef struct {
    char id[40];
    akn_invitation_kind kind;
    char inviter_name[80], inviter_handle[40], inviter_avatar[33];
    char target_id[40], target_name[100];
    long long created_at, expires_at;
} akn_invitation;

typedef struct {
    char id[40];
    char user_id[40], online_id[40], display_name[80], avatar[33];
    long long created_at;
} akn_friend_request;

typedef struct {
    long long id;
    char text[200];       /* "Bob sent you a friend request.", ... */
    long long created_at;
    int unread;
} akn_notification;

typedef struct {
    akn_invitation invitations[AKN_INVITES_MAX];
    int invitation_count;
    akn_friend_request incoming[AKN_REQUESTS_MAX];
    int incoming_count;
    int outgoing_count;
    akn_notification notifications[AKN_NOTIFICATIONS_MAX];
    int notification_count;
    int notifications_unread;  /* the server's count (more than the list may hold) */
} akn_inbox;

/* GET /parties/current ({"party": PartyDto|null}). 0, or -1 on an unexpected shape. */
int akn_fill_current_party(const struct akeno_json *data, akn_party *out);
/* One PartyDto (also the answer of POST /parties and /join). */
int akn_fill_party(const struct akeno_json *node, akn_party *out);
/* GET /parties: the parties you may join (your friends'), without the one you are in. */
int akn_fill_joinable_parties(const struct akeno_json *data, const char *current_party_id, akn_party_list *out);
int akn_fill_invitations(const struct akeno_json *data, akn_inbox *out);
int akn_fill_friend_requests(const struct akeno_json *data, akn_inbox *out);
int akn_fill_notifications(const struct akeno_json *data, akn_inbox *out);

const char *akn_invitation_kind_label(akn_invitation_kind kind); /* "party", "group", "game session" */
/* Index of the party member with this user ID, or -1. */
int akn_party_member_find(const akn_party *party, const char *user_id);
/* The JSON of POST /parties: {"name":"...","privacy":"friends"}; name may be empty. -1 if it does not fit. */
int akn_party_create_json(const char *name, int invite_only, char *out, size_t len);

/*
 * What the Party screen lists, top to bottom, as one list the controller moves through. Built from
 * the party, the invitations and the joinable parties; each row says what it is and which item.
 */
typedef enum {
    AKN_PROW_CREATE = 0,  /* not in a party: create one */
    AKN_PROW_CHAT,        /* in a party: open the party chat */
    AKN_PROW_INVITE,      /* in a party: invite friends */
    AKN_PROW_LEAVE,       /* in a party: leave it */
    AKN_PROW_MEMBER,      /* index: member */
    AKN_PROW_INVITATION,  /* index: party invitation */
    AKN_PROW_JOINABLE,    /* index: a friend's party you may join */
    AKN_PROW_VOICE,       /* in a party with voice: join or leave voice */
    AKN_PROW_VOICE_MUTE,  /* in voice: microphone on/off */
    AKN_PROW_VOICE_DEAFEN,/* in voice: hear nobody */
    AKN_PROW_VOICE_OUTPUT,/* in voice: volume of the others (Left/Right) */
    AKN_PROW_VOICE_MIC    /* in voice: microphone volume (Left/Right) */
} akn_party_row_kind;

typedef struct {
    akn_party_row_kind kind;
    int index;
} akn_party_row;

#define AKN_PARTY_ROWS_MAX (9 + AKN_PARTY_MEMBERS + AKN_INVITES_MAX + AKN_JOINABLE_MAX)

int akn_party_rows(const akn_party *party, const akn_inbox *inbox, const akn_party_list *joinable, akn_party_row *out, int max);
/* The same with party voice: a Join/Leave voice row after Leave when the server offers voice, and
 * the voice controls below it while voice_controls is set (connected or connecting). */
int akn_party_rows_ex(const akn_party *party, const akn_inbox *inbox, const akn_party_list *joinable, int voice_controls,
                      akn_party_row *out, int max);

#endif
