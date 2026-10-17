/*
 * AKENO social data as the native app shows it (Phase 2.1: friends, profiles, game activity),
 * parsed from answers that reach it only through the AKENO daemon's local interface
 * (docs/PS5_NATIVE_PHASE2.md): the daemon's hello and state, its event ring, and the existing
 * server API behind the daemon's proxy, whose answers arrive wrapped as
 * {"ok":true,"status":200,"data":...}. Portable and unit-tested; the transport is app/ipc.c and
 * the worker app/social.c.
 */
#ifndef AKN_SOCIAL_H
#define AKN_SOCIAL_H

#include <stddef.h>
#include <stdint.h>

struct akeno_json;
struct akeno_json_doc;

typedef enum { AKN_PRES_OFFLINE = 0, AKN_PRES_ONLINE, AKN_PRES_AWAY, AKN_PRES_DND, AKN_PRES_INVISIBLE } akn_presence;

/* Media IDs of a verified game's artwork (GameArtwork); "" when that shape does not exist. */
typedef struct {
    char icon[33], cover[33], banner[33];
} akn_art;

typedef struct {
    int present;
    char title_id[16];
    char name[96];        /* "" when the server does not know the title */
    long long started_at; /* unix seconds, 0 when unknown */
    akn_art art;
} akn_game;

typedef struct {
    akn_presence status;
    long long last_seen; /* unix seconds, 0 when hidden or unknown */
    char activity[120];  /* self-reported text, never verified */
    akn_game game;       /* detected by the user's console */
} akn_presence_view;

typedef struct {
    char id[40];
    char online_id[40];
    char display_name[80];
    char status_message[160];
    char avatar_image[33]; /* uploaded picture (media ID) or "" for initials */
    int deleted;
    akn_presence_view presence;
} akn_friend;

#define AKN_FRIENDS_MAX 200

typedef struct {
    akn_friend items[AKN_FRIENDS_MAX];
    int count;
    int truncated; /* the server listed more than AKN_FRIENDS_MAX */
    int online;    /* not offline */
    int playing;
    int incoming_requests, outgoing_requests;
} akn_friend_list;

#define AKN_RECENT_GAMES 12
#define AKN_FAVORITES 12
#define AKN_PROFILE_BADGES 8

typedef struct {
    char title_id[16];
    char name[96];
    long long last_played;
    long long total_seconds; /* -1 when the owner hides playtime */
    int sessions;
    int playing_now;
    akn_art art;
} akn_recent_game;

typedef struct {
    int history_visible, playtime_visible;
    akn_game current;
    akn_recent_game recent[AKN_RECENT_GAMES];
    int recent_count;
    long long total_seconds; /* -1 when hidden */
    int total_games, total_sessions;
    long long tracked_since; /* 0 when unknown */
} akn_activity;

typedef struct {
    char id[40], online_id[40], display_name[80];
    akn_presence_view presence;
    long long joined_at;
    int restricted; /* details hidden by the owner's privacy settings */
    char bio[600];
    char status_message[160];
    char favorites[AKN_FAVORITES][80];
    akn_art favorite_art[AKN_FAVORITES]; /* from favoriteGameDetails where the name matches a verified game */
    int favorite_count;
    char avatar_image[33];  /* media ID or "" */
    char banner_image[33];  /* media ID or "" (null when details are restricted) */
    int banner_overlay;
    char theme[16];         /* profile theme ID */
    uint32_t accent;        /* 0xRRGGBB of a custom theme, else the theme's */
    char badge_ids[AKN_PROFILE_BADGES][24]; /* featured first, then the rest, as on the website */
    int badge_count;        /* entries in badge_ids */
    int featured_badges;    /* the first featured_badges entries are featured */
    char relationship[24]; /* self, friend, none, request_sent, request_received, blocked */
    int mutual_friends;
    int badges;
    int has_party;
    char party_name[80];
    int party_members, party_max;
} akn_profile;

/* The daemon, from GET /api/native/hello and GET /api/state. */
typedef struct {
    int ipc; /* native IPC protocol version; 0 = none */
    char version[32];
    char build[64];
    int paired;
    char online_id[40];
    char realtime[24];
    char realtime_detail[160];
    int activity_enabled;
    char activity_state[24];  /* idle, playing, off, ... (the daemon's game detection) */
    akn_game console_game;    /* the game running on this console */
    long long console_game_seconds;
    int inbox_unseen;
    unsigned long long events_next;
} akn_daemon_info;

/* The daemon's proxy envelope. */
typedef struct {
    int ok;
    int status;     /* the server's HTTP status; 0 when the daemon did not reach it */
    char code[48];  /* error code (server or daemon) */
    char message[200];
} akn_proxy_result;

/* Parses a proxy answer; returns the document (free with akeno_json_free) and points *data at the
 * "data" member (NULL on errors). Returns NULL only when the text is not JSON. */
struct akeno_json_doc *akn_proxy_parse(const char *json, size_t len, akn_proxy_result *res, const struct akeno_json **data);

/* Fill functions take the "data" node of a proxy answer. 0 on success, -1 on an unexpected shape. */
int akn_fill_presence(const struct akeno_json *node, akn_presence_view *out);
int akn_fill_friends(const struct akeno_json *data, akn_friend_list *out);   /* GET /friends */
int akn_fill_requests(const struct akeno_json *data, akn_friend_list *out);  /* GET /friends/requests (counts) */
int akn_fill_profile(const struct akeno_json *data, akn_profile *out);       /* GET /users/:handle */
int akn_fill_activity(const struct akeno_json *data, akn_activity *out);     /* GET /users/:handle/activity */

int akn_parse_daemon_hello(const char *json, size_t len, akn_daemon_info *out);
int akn_parse_daemon_state(const char *json, size_t len, akn_daemon_info *out);

/* What an events answer asks the app to reload. */
enum {
    AKN_EVF_PRESENCE = 1 << 0,    /* presence.updated (already applied to the list and profile) */
    AKN_EVF_FRIENDS = 1 << 1,     /* friends.changed, profile.changed of a friend */
    AKN_EVF_PROFILE = 1 << 2,     /* profile.changed of the open profile */
    AKN_EVF_ACTIVITY = 1 << 3,    /* activity.self, activity.local */
    AKN_EVF_STATE = 1 << 4,       /* realtime.state, activity.local, inbox changes */
    AKN_EVF_MESSAGES = 1 << 5,    /* message.created, conversation.read (Phase 2.2) */
    AKN_EVF_PARTY = 1 << 6,       /* party.changed (Phase 2.3) */
    AKN_EVF_INVITATIONS = 1 << 7, /* invitations.changed, notification.created */
    AKN_EVF_GAP = 1 << 8,         /* events were lost (ring overflow): reload everything shown */
    AKN_EVF_REVOKED = 1 << 9      /* session.revoked */
};

typedef struct {
    unsigned long long next; /* pass as ?after= next time */
    unsigned flags;
    int events;
    int presence_applied;
} akn_events_result;

/* Applies GET /api/events?after=N: presence updates go straight into the list and the open profile
 * (either may be NULL); everything else becomes a reload flag. 0, or -1 if malformed. */
int akn_apply_events(const char *json, size_t len, akn_friend_list *friends, akn_profile *profile, akn_events_result *out);

/* Online, then away, busy, offline; playing before not playing; then by name (ASCII case-folded). */
void akn_friends_sort(akn_friend_list *list);
void akn_friends_count(akn_friend_list *list); /* recomputes online and playing */
int akn_friends_find(const akn_friend_list *list, const char *id);

/* Profile themes of the website (accent and banner gradient, 0xRRGGBB). */
uint32_t akn_theme_accent(const char *theme);
void akn_theme_gradient(const char *theme, uint32_t *from, uint32_t *to);

const char *akn_presence_label(akn_presence p);
/* One line for a list row: "Playing Astro Bot", "Online · Ranked match", "Last seen 3 h ago", ... */
void akn_presence_line(const akn_presence_view *p, long long now, char *out, size_t len);
/* The game's name, else its title ID. */
const char *akn_game_label(const akn_game *g);
/* "2 h 05 min", "12 min", "under a minute" */
void akn_format_duration(long long seconds, char *out, size_t len);
/* "just now", "5 min ago", "3 h ago", "2 days ago", "on 2026-10-01" */
void akn_format_ago(long long then, long long now, char *out, size_t len);
/* Up to two initials for an avatar placeholder ("Alice Liddell" -> "AL", "it_bob" -> "I"). */
void akn_initials(const char *display_name, const char *online_id, char out[8]);

#endif
