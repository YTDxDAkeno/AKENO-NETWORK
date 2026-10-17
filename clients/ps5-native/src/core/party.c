#include "party.h"

#include <stdio.h>
#include <string.h>

#include "account.h"
#include "akeno/json.h"
#include "chat.h"
#include "image.h"

static void copy_str(char *dst, size_t len, const char *src) {
    if (!len) return;
    snprintf(dst, len, "%s", src ? src : "");
}

static const char *str_of(const akeno_json *object, const char *key) {
    const akeno_json *v = akeno_json_get(object, key);
    return v && v->type == AKENO_JSON_STRING ? v->string : "";
}

static long long int_of(const akeno_json *object, const char *key, long long fallback) {
    int64_t v;
    return akeno_json_int64(akeno_json_get(object, key), &v) == 0 ? (long long)v : fallback;
}

static long long time_of(const akeno_json *object, const char *key) {
    long long t;
    return akn_parse_iso8601(str_of(object, key), &t) == 0 ? t : 0;
}

/* A UserSummary: ID, Online ID, display name (else the Online ID), picture. -1 without an Online ID. */
static int user_of(const akeno_json *u, char id[40], char online_id[40], char name[80], char avatar[33]) {
    if (!u || u->type != AKENO_JSON_OBJECT || !str_of(u, "onlineId")[0]) return -1;
    copy_str(id, 40, str_of(u, "id"));
    copy_str(online_id, 40, str_of(u, "onlineId"));
    const char *dn = str_of(u, "displayName");
    copy_str(name, 80, dn[0] ? dn : online_id);
    avatar[0] = '\0';
    if (akn_is_media_id(str_of(u, "avatarImage"))) copy_str(avatar, 33, str_of(u, "avatarImage"));
    return 0;
}

int akn_fill_party(const akeno_json *node, akn_party *out) {
    memset(out, 0, sizeof(*out));
    if (!node || node->type != AKENO_JSON_OBJECT || !str_of(node, "id")[0]) return -1;
    out->present = 1;
    copy_str(out->id, sizeof(out->id), str_of(node, "id"));
    copy_str(out->name, sizeof(out->name), str_of(node, "name"));
    copy_str(out->privacy, sizeof(out->privacy), str_of(node, "privacy"));
    copy_str(out->leader_id, sizeof(out->leader_id), str_of(node, "leaderId"));
    copy_str(out->conversation_id, sizeof(out->conversation_id), str_of(node, "conversationId"));
    out->max_members = (int)int_of(node, "maxMembers", 0);
    out->member_count = (int)int_of(node, "memberCount", 0);
    out->is_member = akeno_json_get_bool(node, "isMember", 0);
    const akeno_json *voice = akeno_json_get(node, "voice");
    out->voice_available = akeno_json_get_bool(voice, "available", 0);
    copy_str(out->voice_reason, sizeof(out->voice_reason), str_of(voice, "reason"));
    const akeno_json *members = akeno_json_get(node, "members");
    for (const akeno_json *m = members && members->type == AKENO_JSON_ARRAY ? members->child : NULL; m; m = m->next) {
        if (out->members_shown >= AKN_PARTY_MEMBERS) break;
        akn_party_member *pm = &out->members[out->members_shown];
        memset(pm, 0, sizeof(*pm));
        const akeno_json *u = akeno_json_get(m, "user");
        if (user_of(u, pm->id, pm->online_id, pm->display_name, pm->avatar) != 0) continue;
        akn_fill_presence(akeno_json_get(u, "presence"), &pm->presence);
        pm->leader = akeno_json_get_bool(m, "isLeader", 0) || (out->leader_id[0] && strcmp(out->leader_id, pm->id) == 0);
        const akeno_json *v = akeno_json_get(m, "voice");
        pm->voice_connected = akeno_json_get_bool(v, "connected", 0);
        const akeno_json *muted = akeno_json_get(v, "muted");
        pm->voice_muted = muted && muted->type == AKENO_JSON_BOOL ? muted->boolean : -1;
        out->members_shown++;
    }
    if (out->member_count < out->members_shown) out->member_count = out->members_shown;
    return 0;
}

int akn_fill_current_party(const akeno_json *data, akn_party *out) {
    memset(out, 0, sizeof(*out));
    if (!data || data->type != AKENO_JSON_OBJECT) return -1;
    const akeno_json *p = akeno_json_get(data, "party");
    if (!p) return -1;
    if (p->type == AKENO_JSON_NULL) return 0; /* not in a party */
    return akn_fill_party(p, out);
}

int akn_fill_joinable_parties(const akeno_json *data, const char *current_party_id, akn_party_list *out) {
    memset(out, 0, sizeof(*out));
    const akeno_json *arr = akeno_json_get(data, "parties");
    if (!arr || arr->type != AKENO_JSON_ARRAY) return -1;
    for (const akeno_json *p = arr->child; p && out->count < AKN_JOINABLE_MAX; p = p->next) {
        akn_party *dst = &out->items[out->count];
        if (akn_fill_party(p, dst) != 0) continue;
        if (dst->is_member || (current_party_id && strcmp(dst->id, current_party_id) == 0)) continue;
        out->count++;
    }
    return 0;
}

static akn_invitation_kind kind_of(const char *s) {
    if (strcmp(s, "party") == 0) return AKN_INV_PARTY;
    if (strcmp(s, "group") == 0) return AKN_INV_GROUP;
    if (strcmp(s, "game_session") == 0) return AKN_INV_SESSION;
    return AKN_INV_OTHER;
}

const char *akn_invitation_kind_label(akn_invitation_kind kind) {
    switch (kind) {
    case AKN_INV_PARTY: return "party";
    case AKN_INV_GROUP: return "group";
    case AKN_INV_SESSION: return "game session";
    case AKN_INV_OTHER: break;
    }
    return "invitation";
}

int akn_fill_invitations(const akeno_json *data, akn_inbox *out) {
    out->invitation_count = 0;
    const akeno_json *arr = akeno_json_get(data, "invitations");
    if (!arr || arr->type != AKENO_JSON_ARRAY) return -1;
    for (const akeno_json *i = arr->child; i && out->invitation_count < AKN_INVITES_MAX; i = i->next) {
        akn_invitation *v = &out->invitations[out->invitation_count];
        memset(v, 0, sizeof(*v));
        char unused[40];
        if (!str_of(i, "id")[0] || user_of(akeno_json_get(i, "inviter"), unused, v->inviter_handle, v->inviter_name, v->inviter_avatar) != 0)
            continue;
        copy_str(v->id, sizeof(v->id), str_of(i, "id"));
        v->kind = kind_of(str_of(i, "kind"));
        const akeno_json *t = akeno_json_get(i, "target");
        copy_str(v->target_id, sizeof(v->target_id), str_of(t, "id"));
        copy_str(v->target_name, sizeof(v->target_name), str_of(t, "name"));
        v->created_at = time_of(i, "createdAt");
        v->expires_at = time_of(i, "expiresAt");
        out->invitation_count++;
    }
    return 0;
}

int akn_fill_friend_requests(const akeno_json *data, akn_inbox *out) {
    out->incoming_count = 0;
    out->outgoing_count = 0;
    const akeno_json *in = akeno_json_get(data, "incoming");
    const akeno_json *outgoing = akeno_json_get(data, "outgoing");
    if (!in || in->type != AKENO_JSON_ARRAY) return -1;
    for (const akeno_json *r = in->child; r && out->incoming_count < AKN_REQUESTS_MAX; r = r->next) {
        akn_friend_request *v = &out->incoming[out->incoming_count];
        memset(v, 0, sizeof(*v));
        if (!str_of(r, "id")[0] || user_of(akeno_json_get(r, "user"), v->user_id, v->online_id, v->display_name, v->avatar) != 0) continue;
        copy_str(v->id, sizeof(v->id), str_of(r, "id"));
        v->created_at = time_of(r, "createdAt");
        out->incoming_count++;
    }
    out->outgoing_count = outgoing && outgoing->type == AKENO_JSON_ARRAY ? (int)outgoing->count : 0;
    return 0;
}

/* The website's wording of a notification, for the actor's display name. */
static int notification_text(const akeno_json *d, char *out, size_t len) {
    const char *type = str_of(d, "type");
    const akeno_json *actor = akeno_json_get(d, "actor");
    const char *who = str_of(actor, "displayName");
    if (!who[0]) who = str_of(actor, "onlineId");
    if (!who[0]) who = "Someone";
    if (strcmp(type, "friend_request.received") == 0) snprintf(out, len, "%s sent you a friend request.", who);
    else if (strcmp(type, "friend_request.accepted") == 0) snprintf(out, len, "%s accepted your friend request.", who);
    else if (strcmp(type, "invitation.received") == 0)
        snprintf(out, len, "%s invited you to the %s \"%s\".", who, akn_invitation_kind_label(kind_of(str_of(d, "kind"))), str_of(d, "targetName"));
    else if (strcmp(type, "game_session.join_requested") == 0) snprintf(out, len, "%s asked to join \"%s\".", who, str_of(d, "gameName"));
    else if (strcmp(type, "game_session.request_accepted") == 0) snprintf(out, len, "%s accepted you into \"%s\".", who, str_of(d, "gameName"));
    else if (strcmp(type, "game_session.request_rejected") == 0) snprintf(out, len, "%s declined your request for \"%s\".", who, str_of(d, "gameName"));
    else if (strcmp(type, "group.removed") == 0) snprintf(out, len, "%s removed you from \"%s\".", who, str_of(d, "groupName"));
    else if (strcmp(type, "group.role_changed") == 0)
        snprintf(out, len, "%s made you %s in \"%s\".", who, str_of(d, "role"), str_of(d, "groupName"));
    else return -1;
    return 0;
}

int akn_fill_notifications(const akeno_json *data, akn_inbox *out) {
    out->notification_count = 0;
    const akeno_json *arr = akeno_json_get(data, "notifications");
    if (!arr || arr->type != AKENO_JSON_ARRAY) return -1;
    out->notifications_unread = (int)int_of(data, "unreadCount", 0);
    for (const akeno_json *n = arr->child; n && out->notification_count < AKN_NOTIFICATIONS_MAX; n = n->next) {
        akn_notification *v = &out->notifications[out->notification_count];
        memset(v, 0, sizeof(*v));
        if (notification_text(akeno_json_get(n, "data"), v->text, sizeof(v->text)) != 0) continue; /* a kind this build does not know */
        v->id = int_of(n, "id", 0);
        v->created_at = time_of(n, "createdAt");
        const akeno_json *read = akeno_json_get(n, "readAt");
        v->unread = !read || read->type == AKENO_JSON_NULL;
        out->notification_count++;
    }
    return 0;
}

int akn_party_member_find(const akn_party *party, const char *user_id) {
    if (!party || !user_id || !user_id[0]) return -1;
    for (int i = 0; i < party->members_shown; i++)
        if (strcmp(party->members[i].id, user_id) == 0) return i;
    return -1;
}

int akn_party_create_json(const char *name, int invite_only, char *out, size_t len) {
    char quoted[300];
    int n;
    if (name && name[0]) {
        if (akn_json_quote(name, quoted, sizeof(quoted)) != 0) return -1;
        n = snprintf(out, len, "{\"name\":%s,\"privacy\":\"%s\"}", quoted, invite_only ? "invite_only" : "friends");
    } else {
        n = snprintf(out, len, "{\"privacy\":\"%s\"}", invite_only ? "invite_only" : "friends");
    }
    return n > 0 && (size_t)n < len ? 0 : -1;
}

int akn_party_rows(const akn_party *party, const akn_inbox *inbox, const akn_party_list *joinable, akn_party_row *out, int max) {
    int n = 0;
#define ROW(k, i)                                       \
    do {                                                \
        if (n < max) out[n] = (akn_party_row){ (k), (i) }; \
        n++;                                            \
    } while (0)
    if (party && party->present) {
        if (party->conversation_id[0]) ROW(AKN_PROW_CHAT, 0);
        ROW(AKN_PROW_INVITE, 0);
        ROW(AKN_PROW_LEAVE, 0);
        for (int i = 0; i < party->members_shown; i++) ROW(AKN_PROW_MEMBER, i);
    } else {
        ROW(AKN_PROW_CREATE, 0);
    }
    for (int i = 0; inbox && i < inbox->invitation_count; i++)
        if (inbox->invitations[i].kind == AKN_INV_PARTY) ROW(AKN_PROW_INVITATION, i);
    for (int i = 0; joinable && i < joinable->count; i++) ROW(AKN_PROW_JOINABLE, i);
#undef ROW
    return n < max ? n : max;
}
