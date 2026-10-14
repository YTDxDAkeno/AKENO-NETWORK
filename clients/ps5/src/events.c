#include "akeno/events.h"

#include <stdio.h>
#include <string.h>

void akeno_watermark_init(akeno_watermark *w) {
    w->highest = 0;
    w->primed = 0;
}

int akeno_watermark_observe(akeno_watermark *w, int64_t id) {
    if (!w->primed) {
        if (id > w->highest) w->highest = id;
        return 0;
    }
    if (id <= w->highest) return 0;
    w->highest = id;
    return 1;
}

void akeno_watermark_prime(akeno_watermark *w, int64_t highest) {
    if (highest > w->highest) w->highest = highest;
    w->primed = 1;
}

static const char *actor_name(const akeno_json *data) {
    return akeno_json_get_string(akeno_json_get(data, "actor"), "displayName", "Someone");
}

static const char *kind_label(const char *kind) {
    if (strcmp(kind, "group") == 0) return "group";
    if (strcmp(kind, "party") == 0) return "party";
    if (strcmp(kind, "game_session") == 0) return "game session";
    return "item";
}

int akeno_describe_notification(const akeno_json *data, char *out, size_t out_len) {
    const char *type = akeno_json_get_string(data, "type", NULL);
    if (!type) return -1;
    const char *actor = actor_name(data);

    if (strcmp(type, "friend_request.received") == 0)
        return snprintf(out, out_len, "%s sent you a friend request.", actor);
    if (strcmp(type, "friend_request.accepted") == 0)
        return snprintf(out, out_len, "%s accepted your friend request.", actor);
    if (strcmp(type, "invitation.received") == 0) {
        const char *kind = akeno_json_get_string(data, "kind", "item");
        const char *name = akeno_json_get_string(data, "targetName", "");
        return snprintf(out, out_len, "%s invited you to the %s \"%s\".", actor, kind_label(kind), name);
    }
    if (strcmp(type, "game_session.join_requested") == 0)
        return snprintf(out, out_len, "%s asked to join your %s session.", actor,
                        akeno_json_get_string(data, "gameName", "game"));
    if (strcmp(type, "game_session.request_accepted") == 0)
        return snprintf(out, out_len, "%s accepted your request to join %s.", actor,
                        akeno_json_get_string(data, "gameName", "a game"));
    if (strcmp(type, "game_session.request_rejected") == 0)
        return snprintf(out, out_len, "%s declined your request to join %s.", actor,
                        akeno_json_get_string(data, "gameName", "a game"));
    if (strcmp(type, "group.removed") == 0)
        return snprintf(out, out_len, "%s removed you from the group \"%s\".", actor,
                        akeno_json_get_string(data, "groupName", ""));
    if (strcmp(type, "group.role_changed") == 0) {
        const char *role = akeno_json_get_string(data, "role", "member");
        return snprintf(out, out_len, "%s made you %s of \"%s\".", actor,
                        strcmp(role, "moderator") == 0 ? "a moderator" : "a member",
                        akeno_json_get_string(data, "groupName", ""));
    }
    /* Unknown but well-formed notification: a safe, generic line. */
    return snprintf(out, out_len, "You have a new AKENO NETWORK notification.");
}
