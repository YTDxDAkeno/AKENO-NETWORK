#include "voice_proto.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "akeno/json.h"
#include "chat.h"

static void copy(char *dst, size_t len, const char *src) { snprintf(dst, len, "%s", src ? src : ""); }

int akn_voice_hello_json(const char *token, const char *client, int muted, int deafened, char *out, size_t len) {
    if (!token || !token[0] || strlen(token) >= AKN_VOICE_TOKEN_MAX) return -1;
    char qt[AKN_VOICE_TOKEN_MAX * 2 + 4], qc[200];
    if (akn_json_quote(token, qt, sizeof(qt)) < 0 || akn_json_quote(client ? client : "", qc, sizeof(qc)) < 0) return -1;
    int n = snprintf(out, len, "{\"type\":\"hello\",\"protocol\":%d,\"token\":%s,\"client\":%s,\"muted\":%s,\"deafened\":%s}",
                     AKN_VOICE_PROTOCOL, qt, qc, muted ? "true" : "false", deafened ? "true" : "false");
    return n < 0 || (size_t)n >= len ? -1 : n;
}

int akn_voice_flag_json(const char *type, const char *key, int value, char *out, size_t len) {
    int n = snprintf(out, len, "{\"type\":\"%s\",\"%s\":%s}", type, key, value ? "true" : "false");
    return n < 0 || (size_t)n >= len ? -1 : n;
}

int akn_voice_ping_json(long long id, char *out, size_t len) {
    int n = snprintf(out, len, "{\"type\":\"ping\",\"id\":%lld}", id);
    return n < 0 || (size_t)n >= len ? -1 : n;
}

static int get_int(const akeno_json *o, const char *key, int fallback) {
    int64_t v;
    const akeno_json *n = akeno_json_get(o, key);
    if (!n || akeno_json_int64(n, &v) != 0 || v < -1000000 || v > 1000000) return fallback;
    return (int)v;
}

int akn_voice_parse(const char *text, size_t len, akn_voice_msg *msg) {
    memset(msg, 0, sizeof(*msg));
    akeno_json_doc *doc = akeno_json_parse(text, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    if (!root || root->type != AKENO_JSON_OBJECT) {
        akeno_json_free(doc);
        return -1;
    }
    const char *type = akeno_json_get_string(root, "type", "");
    if (strcmp(type, "ready") == 0) {
        msg->type = AKN_VMSG_READY;
        msg->protocol = get_int(root, "protocol", 0);
        msg->sample_rate = get_int(root, "sampleRate", 0);
        msg->frame_ms = get_int(root, "frameMs", 0);
        msg->channels = get_int(root, "channels", 0);
        copy(msg->room, sizeof(msg->room), akeno_json_get_string(root, "room", ""));
        copy(msg->identity, sizeof(msg->identity), akeno_json_get_string(root, "identity", ""));
    } else if (strcmp(type, "participants") == 0) {
        msg->type = AKN_VMSG_PARTICIPANTS;
        const akeno_json *list = akeno_json_get(root, "list");
        if (list && list->type == AKENO_JSON_ARRAY) {
            for (const akeno_json *p = list->child; p && msg->count < AKN_VOICE_MAX_PARTICIPANTS; p = p->next) {
                if (p->type != AKENO_JSON_OBJECT) continue;
                akn_voice_participant *v = &msg->list[msg->count];
                const char *id = akeno_json_get_string(p, "id", "");
                if (!id[0]) continue;
                copy(v->id, sizeof(v->id), id);
                copy(v->name, sizeof(v->name), akeno_json_get_string(p, "name", ""));
                v->muted = akeno_json_get_bool(p, "muted", 0);
                v->speaking = akeno_json_get_bool(p, "speaking", 0);
                v->self = akeno_json_get_bool(p, "self", 0);
                int level = get_int(p, "level", 0);
                v->level = level < 0 ? 0 : level > 100 ? 100 : level;
                msg->count++;
            }
        }
    } else if (strcmp(type, "state") == 0) {
        msg->type = AKN_VMSG_STATE;
        msg->reconnecting = strcmp(akeno_json_get_string(root, "state", ""), "reconnecting") == 0;
    } else if (strcmp(type, "error") == 0) {
        msg->type = AKN_VMSG_ERROR;
        copy(msg->code, sizeof(msg->code), akeno_json_get_string(root, "code", "ERROR"));
        copy(msg->message, sizeof(msg->message), akeno_json_get_string(root, "message", ""));
    } else if (strcmp(type, "pong") == 0) {
        msg->type = AKN_VMSG_PONG;
        int64_t id = 0;
        const akeno_json *n = akeno_json_get(root, "id");
        if (n) akeno_json_int64(n, &id);
        msg->id = id;
    }
    akeno_json_free(doc);
    return 0;
}

int akn_voice_ready_ok(const akn_voice_msg *msg) {
    return msg->type == AKN_VMSG_READY && msg->protocol == AKN_VOICE_PROTOCOL && msg->sample_rate == AKN_VOICE_RATE &&
           msg->frame_ms == 20 && msg->channels == 1;
}

int akn_voice_pack_audio(uint16_t seq, const uint8_t *opus, size_t len, uint8_t *out, size_t out_len) {
    if (!opus || len == 0 || len > AKN_VOICE_MAX_PACKET || out_len < len + 3) return -1;
    out[0] = AKN_VOICE_AUDIO_TAG;
    out[1] = (uint8_t)(seq >> 8);
    out[2] = (uint8_t)seq;
    memcpy(out + 3, opus, len);
    return (int)(len + 3);
}

int akn_voice_unpack_audio(const uint8_t *frame, size_t len, uint16_t *seq, const uint8_t **opus, size_t *opus_len) {
    if (!frame || len < 4 || len > AKN_VOICE_MAX_PACKET + 3 || frame[0] != AKN_VOICE_AUDIO_TAG) return -1;
    *seq = (uint16_t)((frame[1] << 8) | frame[2]);
    *opus = frame + 3;
    *opus_len = len - 3;
    return 0;
}

int akn_voice_credential_from(const akeno_json *root, akn_voice_credential *out) {
    memset(out, 0, sizeof(*out));
    if (!root || root->type != AKENO_JSON_OBJECT) return -1;
    const char *url = akeno_json_get_string(root, "url", "");
    const char *token = akeno_json_get_string(root, "token", "");
    if (strncmp(url, "wss://", 6) != 0 || strlen(url) >= sizeof(out->url) || !token[0] || strlen(token) >= sizeof(out->token)) return -1;
    copy(out->url, sizeof(out->url), url);
    copy(out->token, sizeof(out->token), token);
    copy(out->room, sizeof(out->room), akeno_json_get_string(root, "room", ""));
    copy(out->identity, sizeof(out->identity), akeno_json_get_string(root, "identity", ""));
    return 0;
}

int akn_voice_parse_credential(const char *json, size_t len, akn_voice_credential *out) {
    memset(out, 0, sizeof(*out));
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    const akeno_json *data = akeno_json_get(root, "data");
    if (data && data->type == AKENO_JSON_OBJECT) root = data;
    int rc = akn_voice_credential_from(root, out);
    akeno_json_free(doc);
    return rc;
}

/* host of "scheme://[user@]host[:port]/..." into out; 0 on success */
static int host_of(const char *url, char *out, size_t len) {
    const char *p = strstr(url ? url : "", "://");
    if (!p) return -1;
    p += 3;
    const char *end = p + strcspn(p, "/?#");
    const char *at = memchr(p, '@', (size_t)(end - p));
    if (at) p = at + 1;
    const char *host_end = end;
    if (*p == '[') {
        const char *rb = memchr(p, ']', (size_t)(end - p));
        if (!rb) return -1;
        host_end = rb + 1;
    } else {
        const char *colon = memchr(p, ':', (size_t)(end - p));
        if (colon) host_end = colon;
    }
    size_t n = (size_t)(host_end - p);
    if (n == 0 || n >= len) return -1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

int akn_voice_same_host(const char *gateway_url, const char *server_url) {
    char a[256], b[256];
    if (host_of(gateway_url, a, sizeof(a)) != 0 || host_of(server_url, b, sizeof(b)) != 0) return 0;
    return strcasecmp(a, b) == 0;
}

const char *akn_voice_close_text(int close_code) {
    switch (close_code) {
    case AKN_VOICE_CLOSE_AUTH: return "The voice server did not accept this console's voice pass.";
    case AKN_VOICE_CLOSE_REMOVED: return "You were removed from the voice chat.";
    case AKN_VOICE_CLOSE_REPLACED: return "You joined this voice chat on another device.";
    case AKN_VOICE_CLOSE_ENDED: return "The party's voice chat ended.";
    case AKN_VOICE_CLOSE_TIMEOUT: return "The voice server closed an idle connection.";
    case 1000: return "Voice chat ended.";
    default: return "The connection to the voice server was lost.";
    }
}

const char *akn_voice_error_text(const char *code) {
    if (!code) return "Voice failed.";
    if (strcmp(code, "VOICE_UNAVAILABLE") == 0) return "Voice chat is not available on this AKENO server right now.";
    if (strcmp(code, "VOICE_GATEWAY_UNAVAILABLE") == 0) return "Console voice is not set up on this AKENO server.";
    if (strcmp(code, "NOT_MEMBER") == 0) return "Only members of this party can join its voice chat.";
    if (strcmp(code, "BLOCKED") == 0) return "You cannot join while someone you blocked, or who blocked you, is in it.";
    if (strcmp(code, "FORBIDDEN") == 0 || strcmp(code, "INSUFFICIENT_SCOPE") == 0)
        return "This console is not allowed to join voice. Check its permissions on the AKENO website.";
    if (strcmp(code, "TOKEN_INVALID") == 0) return "The voice pass was refused. Try again.";
    if (strcmp(code, "ROOM_FULL") == 0) return "The voice chat is full.";
    if (strcmp(code, "VOICE_SERVER_UNREACHABLE") == 0) return "The voice server is not reachable right now.";
    return "Voice failed.";
}
