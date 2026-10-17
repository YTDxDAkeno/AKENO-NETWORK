#include "social.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"
#include "akeno/json.h"

static void copy_str(char *dst, size_t len, const char *src) {
    if (!len) return;
    snprintf(dst, len, "%s", src ? src : "");
}

static const char *str_or_empty(const akeno_json *object, const char *key) {
    const akeno_json *v = akeno_json_get(object, key);
    return v && v->type == AKENO_JSON_STRING ? v->string : "";
}

static long long get_int(const akeno_json *object, const char *key, long long fallback) {
    int64_t v;
    return akeno_json_int64(akeno_json_get(object, key), &v) == 0 ? (long long)v : fallback;
}

static long long get_time(const akeno_json *object, const char *key) {
    long long t;
    return akn_parse_iso8601(str_or_empty(object, key), &t) == 0 ? t : 0;
}

static akn_presence presence_of(const char *s) {
    if (strcmp(s, "online") == 0) return AKN_PRES_ONLINE;
    if (strcmp(s, "away") == 0) return AKN_PRES_AWAY;
    if (strcmp(s, "dnd") == 0) return AKN_PRES_DND;
    if (strcmp(s, "invisible") == 0) return AKN_PRES_INVISIBLE;
    return AKN_PRES_OFFLINE;
}

static void fill_game(const akeno_json *node, akn_game *g) {
    memset(g, 0, sizeof(*g));
    if (!node || node->type != AKENO_JSON_OBJECT) return;
    copy_str(g->title_id, sizeof(g->title_id), str_or_empty(node, "titleId"));
    copy_str(g->name, sizeof(g->name), str_or_empty(node, "name"));
    g->started_at = get_time(node, "startedAt");
    g->present = g->title_id[0] != '\0';
}

/* ---- Proxy envelope -------------------------------------------------------------------------- */

akeno_json_doc *akn_proxy_parse(const char *json, size_t len, akn_proxy_result *res, const akeno_json **data) {
    memset(res, 0, sizeof(*res));
    *data = NULL;
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) {
        copy_str(res->code, sizeof(res->code), "BAD_RESPONSE");
        return NULL;
    }
    const akeno_json *root = akeno_json_root(doc);
    res->ok = akeno_json_get_bool(root, "ok", 0);
    res->status = (int)get_int(root, "status", 0);
    const akeno_json *err = akeno_json_get(root, "error");
    copy_str(res->code, sizeof(res->code), akeno_json_get_string(err, "code", ""));
    copy_str(res->message, sizeof(res->message), akeno_json_get_string(err, "message", ""));
    if (res->ok) {
        *data = akeno_json_get(root, "data");
        if (!*data) {
            res->ok = 0;
            copy_str(res->code, sizeof(res->code), "BAD_RESPONSE");
        }
    }
    return doc;
}

/* ---- Server answers ---------------------------------------------------------------------------- */

int akn_fill_presence(const akeno_json *node, akn_presence_view *out) {
    memset(out, 0, sizeof(*out));
    if (!node || node->type != AKENO_JSON_OBJECT) return -1;
    out->status = presence_of(str_or_empty(node, "status"));
    out->last_seen = get_time(node, "lastSeenAt");
    copy_str(out->activity, sizeof(out->activity), str_or_empty(node, "activity"));
    fill_game(akeno_json_get(node, "game"), &out->game);
    return 0;
}

static int fill_user(const akeno_json *u, char *id, char *online_id, char *display_name) {
    if (!u || u->type != AKENO_JSON_OBJECT) return -1;
    const char *oid = str_or_empty(u, "onlineId");
    if (!oid[0]) return -1;
    copy_str(id, 40, str_or_empty(u, "id"));
    copy_str(online_id, 40, oid);
    const char *dn = str_or_empty(u, "displayName");
    copy_str(display_name, 80, dn[0] ? dn : oid);
    return 0;
}

int akn_fill_friends(const akeno_json *data, akn_friend_list *out) {
    const akeno_json *arr = akeno_json_get(data, "friends");
    if (!arr || arr->type != AKENO_JSON_ARRAY) return -1;
    out->count = 0;
    out->truncated = 0;
    for (const akeno_json *f = arr->child; f; f = f->next) {
        if (out->count >= AKN_FRIENDS_MAX) {
            out->truncated = 1;
            break;
        }
        akn_friend *d = &out->items[out->count];
        memset(d, 0, sizeof(*d));
        if (fill_user(f, d->id, d->online_id, d->display_name) != 0) continue;
        copy_str(d->status_message, sizeof(d->status_message), str_or_empty(f, "statusMessage"));
        d->deleted = akeno_json_get_bool(f, "deleted", 0);
        akn_fill_presence(akeno_json_get(f, "presence"), &d->presence);
        out->count++;
    }
    akn_friends_sort(out);
    return 0;
}

int akn_fill_requests(const akeno_json *data, akn_friend_list *out) {
    const akeno_json *in = akeno_json_get(data, "incoming"), *outgoing = akeno_json_get(data, "outgoing");
    if (!in || in->type != AKENO_JSON_ARRAY || !outgoing || outgoing->type != AKENO_JSON_ARRAY) return -1;
    out->incoming_requests = (int)in->count;
    out->outgoing_requests = (int)outgoing->count;
    return 0;
}

int akn_fill_profile(const akeno_json *data, akn_profile *out) {
    memset(out, 0, sizeof(*out));
    if (fill_user(akeno_json_get(data, "user"), out->id, out->online_id, out->display_name) != 0) return -1;
    akn_fill_presence(akeno_json_get(data, "presence"), &out->presence);
    out->joined_at = get_time(data, "joinedAt");
    out->restricted = akeno_json_get_bool(data, "restricted", 0);
    copy_str(out->bio, sizeof(out->bio), str_or_empty(data, "bio"));
    copy_str(out->status_message, sizeof(out->status_message), str_or_empty(data, "statusMessage"));
    const akeno_json *fav = akeno_json_get(data, "favoriteGames");
    for (const akeno_json *g = fav && fav->type == AKENO_JSON_ARRAY ? fav->child : NULL; g && out->favorite_count < AKN_FAVORITES; g = g->next)
        if (g->type == AKENO_JSON_STRING && g->string[0]) copy_str(out->favorites[out->favorite_count++], sizeof(out->favorites[0]), g->string);
    copy_str(out->relationship, sizeof(out->relationship), akeno_json_get_string(akeno_json_get(data, "relationship"), "state", ""));
    out->mutual_friends = (int)get_int(data, "mutualFriendCount", 0);
    const akeno_json *badges = akeno_json_get(data, "badges");
    out->badges = badges && badges->type == AKENO_JSON_ARRAY ? (int)badges->count : 0;
    const akeno_json *party = akeno_json_get(data, "party");
    if (party && party->type == AKENO_JSON_OBJECT) {
        out->has_party = 1;
        copy_str(out->party_name, sizeof(out->party_name), str_or_empty(party, "name"));
        out->party_members = (int)get_int(party, "memberCount", 0);
        out->party_max = (int)get_int(party, "maxMembers", 0);
    }
    return 0;
}

int akn_fill_activity(const akeno_json *data, akn_activity *out) {
    memset(out, 0, sizeof(*out));
    if (!data || data->type != AKENO_JSON_OBJECT) return -1;
    out->history_visible = akeno_json_get_bool(data, "historyVisible", 0);
    out->playtime_visible = akeno_json_get_bool(data, "playtimeVisible", 0);
    fill_game(akeno_json_get(data, "current"), &out->current);
    const akeno_json *recent = akeno_json_get(data, "recentGames");
    for (const akeno_json *g = recent && recent->type == AKENO_JSON_ARRAY ? recent->child : NULL; g && out->recent_count < AKN_RECENT_GAMES;
         g = g->next) {
        akn_recent_game *r = &out->recent[out->recent_count];
        copy_str(r->title_id, sizeof(r->title_id), str_or_empty(g, "titleId"));
        if (!r->title_id[0]) continue;
        copy_str(r->name, sizeof(r->name), str_or_empty(g, "name"));
        r->last_played = get_time(g, "lastPlayedAt");
        r->total_seconds = get_int(g, "totalSeconds", -1);
        r->sessions = (int)get_int(g, "sessionCount", 0);
        r->playing_now = akeno_json_get_bool(g, "playingNow", 0);
        out->recent_count++;
    }
    const akeno_json *totals = akeno_json_get(data, "totals");
    out->total_seconds = get_int(totals, "seconds", -1);
    out->total_games = (int)get_int(totals, "games", 0);
    out->total_sessions = (int)get_int(totals, "sessions", 0);
    return 0;
}

/* ---- Daemon answers ---------------------------------------------------------------------------- */

int akn_parse_daemon_hello(const char *json, size_t len, akn_daemon_info *out) {
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    int rc = -1;
    long long ipc = get_int(root, "ipc", 0);
    if (akeno_json_get_bool(root, "ok", 0) && strcmp(akeno_json_get_string(root, "app", ""), "akeno-network-console") == 0 && ipc > 0 &&
        ipc < 1000) {
        out->ipc = (int)ipc;
        copy_str(out->version, sizeof(out->version), str_or_empty(root, "version"));
        copy_str(out->build, sizeof(out->build), str_or_empty(root, "build"));
        out->paired = akeno_json_get_bool(root, "paired", 0);
        copy_str(out->online_id, sizeof(out->online_id), str_or_empty(root, "onlineId"));
        copy_str(out->realtime, sizeof(out->realtime), str_or_empty(root, "realtime"));
        long long next = get_int(root, "eventsNext", 0);
        out->events_next = next > 0 ? (unsigned long long)next : 0;
        rc = 0;
    }
    akeno_json_free(doc);
    return rc;
}

int akn_parse_daemon_state(const char *json, size_t len, akn_daemon_info *out) {
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    int rc = -1;
    if (akeno_json_get_bool(root, "ok", 0)) {
        out->paired = akeno_json_get_bool(root, "paired", 0);
        copy_str(out->online_id, sizeof(out->online_id), str_or_empty(root, "onlineId"));
        const akeno_json *rt = akeno_json_get(root, "realtime");
        copy_str(out->realtime, sizeof(out->realtime), str_or_empty(rt, "state"));
        copy_str(out->realtime_detail, sizeof(out->realtime_detail), str_or_empty(rt, "detail"));
        const akeno_json *act = akeno_json_get(root, "activity");
        out->activity_enabled = akeno_json_get_bool(act, "enabled", 0);
        copy_str(out->activity_state, sizeof(out->activity_state), str_or_empty(act, "state"));
        memset(&out->console_game, 0, sizeof(out->console_game));
        copy_str(out->console_game.title_id, sizeof(out->console_game.title_id), str_or_empty(act, "titleId"));
        copy_str(out->console_game.name, sizeof(out->console_game.name), str_or_empty(act, "name"));
        out->console_game.started_at = get_time(act, "startedAt");
        out->console_game.present = out->console_game.title_id[0] != '\0';
        out->console_game_seconds = get_int(act, "sinceSeconds", 0);
        out->inbox_unseen = (int)get_int(akeno_json_get(root, "inbox"), "unseen", 0);
        long long next = get_int(root, "eventsNext", 0);
        out->events_next = next > 0 ? (unsigned long long)next : 0;
        rc = 0;
    }
    akeno_json_free(doc);
    return rc;
}

/* ---- Events ------------------------------------------------------------------------------------ */

int akn_apply_events(const char *json, size_t len, akn_friend_list *friends, akn_profile *profile, akn_events_result *out) {
    unsigned long long after = out->next;
    memset(out, 0, sizeof(*out));
    out->next = after;
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    const akeno_json *events = akeno_json_get(root, "events");
    long long next = get_int(root, "next", -1);
    if (!akeno_json_get_bool(root, "ok", 0) || !events || events->type != AKENO_JSON_ARRAY || next < 0) {
        akeno_json_free(doc);
        return -1;
    }
    /* The daemon restarted (its counter went back) or events were dropped: reload what is shown. */
    if (akeno_json_get_bool(root, "gap", 0) || (unsigned long long)next < after) out->flags |= AKN_EVF_GAP;
    out->next = (unsigned long long)next;
    for (const akeno_json *e = events->child; e; e = e->next) {
        const char *type = str_or_empty(e, "type");
        const akeno_json *data = akeno_json_get(e, "data");
        const char *user = str_or_empty(data, "userId");
        out->events++;
        if (strcmp(type, "presence.updated") == 0) {
            akn_presence_view p;
            if (!user[0] || akn_fill_presence(akeno_json_get(data, "presence"), &p) != 0) {
                out->flags |= AKN_EVF_FRIENDS; /* too large for the ring (data null): reload */
                continue;
            }
            out->flags |= AKN_EVF_PRESENCE;
            int i = friends ? akn_friends_find(friends, user) : -1;
            if (i >= 0) {
                friends->items[i].presence = p;
                out->presence_applied++;
            }
            if (profile && strcmp(profile->id, user) == 0) profile->presence = p;
        } else if (strcmp(type, "friends.changed") == 0) {
            out->flags |= AKN_EVF_FRIENDS;
        } else if (strcmp(type, "profile.changed") == 0) {
            if (profile && profile->id[0] && strcmp(profile->id, user) == 0) out->flags |= AKN_EVF_PROFILE;
            if (friends && akn_friends_find(friends, user) >= 0) out->flags |= AKN_EVF_FRIENDS;
        } else if (strcmp(type, "activity.self") == 0) {
            out->flags |= AKN_EVF_ACTIVITY;
        } else if (strcmp(type, "activity.local") == 0) {
            out->flags |= AKN_EVF_ACTIVITY | AKN_EVF_STATE;
        } else if (strcmp(type, "realtime.state") == 0) {
            out->flags |= AKN_EVF_STATE;
        } else if (strcmp(type, "message.created") == 0 || strcmp(type, "conversation.read") == 0) {
            out->flags |= AKN_EVF_MESSAGES | AKN_EVF_STATE;
        } else if (strcmp(type, "party.changed") == 0) {
            out->flags |= AKN_EVF_PARTY;
        } else if (strcmp(type, "invitations.changed") == 0 || strcmp(type, "notification.created") == 0) {
            out->flags |= AKN_EVF_INVITATIONS;
        } else if (strcmp(type, "session.revoked") == 0) {
            out->flags |= AKN_EVF_REVOKED | AKN_EVF_STATE;
        }
    }
    if (out->presence_applied && friends) akn_friends_sort(friends);
    akeno_json_free(doc);
    return 0;
}

/* ---- Lists and text ---------------------------------------------------------------------------- */

static int presence_rank(akn_presence p) {
    switch (p) {
    case AKN_PRES_ONLINE: return 0;
    case AKN_PRES_AWAY: return 1;
    case AKN_PRES_DND: return 2;
    default: return 3;
    }
}

static int name_cmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
}

static int friend_cmp(const void *pa, const void *pb) {
    const akn_friend *a = pa, *b = pb;
    int d = presence_rank(a->presence.status) - presence_rank(b->presence.status);
    if (d) return d;
    d = (b->presence.game.present != 0) - (a->presence.game.present != 0);
    if (d) return d;
    d = name_cmp(a->display_name, b->display_name);
    return d ? d : strcmp(a->online_id, b->online_id);
}

void akn_friends_count(akn_friend_list *list) {
    list->online = list->playing = 0;
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].presence.status != AKN_PRES_OFFLINE) list->online++;
        if (list->items[i].presence.game.present) list->playing++;
    }
}

void akn_friends_sort(akn_friend_list *list) {
    if (list->count > 1) qsort(list->items, (size_t)list->count, sizeof(list->items[0]), friend_cmp);
    akn_friends_count(list);
}

int akn_friends_find(const akn_friend_list *list, const char *id) {
    if (!id || !id[0]) return -1;
    for (int i = 0; i < list->count; i++)
        if (strcmp(list->items[i].id, id) == 0) return i;
    return -1;
}

const char *akn_presence_label(akn_presence p) {
    switch (p) {
    case AKN_PRES_ONLINE: return "Online";
    case AKN_PRES_AWAY: return "Away";
    case AKN_PRES_DND: return "Do not disturb";
    case AKN_PRES_INVISIBLE: return "Invisible";
    default: return "Offline";
    }
}

const char *akn_game_label(const akn_game *g) { return g->name[0] ? g->name : g->title_id; }

void akn_format_duration(long long s, char *out, size_t len) {
    if (s < 60) snprintf(out, len, "under a minute");
    else if (s < 3600) snprintf(out, len, "%lld min", s / 60);
    else snprintf(out, len, "%lld h %02lld min", s / 3600, (s % 3600) / 60);
}

void akn_format_ago(long long then, long long now, char *out, size_t len) {
    long long d = now - then;
    if (then <= 0) snprintf(out, len, "a while ago");
    else if (d < 60) snprintf(out, len, "just now");
    else if (d < 3600) snprintf(out, len, "%lld min ago", d / 60);
    else if (d < 86400) snprintf(out, len, "%lld h ago", d / 3600);
    else if (d < 30 * 86400) snprintf(out, len, "%lld day%s ago", d / 86400, d / 86400 == 1 ? "" : "s");
    else {
        long long days = then / 86400; /* civil date from days since 1970 (Howard Hinnant) */
        days += 719468;
        long long era = (days >= 0 ? days : days - 146096) / 146097;
        long long doe = days - era * 146097;
        long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long long mp = (5 * doy + 2) / 153;
        long long day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
        long long year = yoe + era * 400 + (month <= 2);
        snprintf(out, len, "on %04lld-%02lld-%02lld", year, month, day);
    }
}

void akn_presence_line(const akn_presence_view *p, long long now, char *out, size_t len) {
    if (p->game.present) {
        snprintf(out, len, "Playing %s", akn_game_label(&p->game));
    } else if (p->status == AKN_PRES_OFFLINE) {
        char ago[40];
        if (p->last_seen > 0) {
            akn_format_ago(p->last_seen, now, ago, sizeof(ago));
            snprintf(out, len, "Offline \xC2\xB7 last seen %s", ago);
        } else {
            snprintf(out, len, "Offline");
        }
    } else if (p->activity[0]) {
        snprintf(out, len, "%s \xC2\xB7 %s", akn_presence_label(p->status), p->activity);
    } else {
        snprintf(out, len, "%s", akn_presence_label(p->status));
    }
}

/* Copies the first UTF-8 character of s (upper-cased if ASCII) to out; returns bytes written. */
static size_t first_char(const char *s, char *out) {
    unsigned char c = (unsigned char)s[0];
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    for (size_t i = 1; i < n; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) return 0;
    memcpy(out, s, n);
    if (n == 1) out[0] = (char)toupper(c);
    return n;
}

void akn_initials(const char *display_name, const char *online_id, char out[8]) {
    const char *s = display_name && display_name[0] ? display_name : online_id ? online_id : "";
    size_t o = 0;
    while (*s == ' ') s++;
    if (*s) o += first_char(s, out + o);
    const char *space = strrchr(s, ' ');
    if (o && space && space[1]) o += first_char(space + 1, out + o);
    if (!o) out[o++] = '?';
    out[o] = '\0';
}
