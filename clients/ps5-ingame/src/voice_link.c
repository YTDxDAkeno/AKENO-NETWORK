#include "voice_link.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "account.h"
#include "akeno/json.h"
#include "ipc.h"

#define TIMEOUT_MS 3000
#define REPLY_MAX (64 * 1024)

static int port_of(const akn_daemon_link *link) { return link->voice_port > 0 ? link->voice_port : AKN_VOICE_PORT; }

static void copy(char *dst, size_t len, const char *src) {
    snprintf(dst, len, "%s", src ? src : "");
    akn_utf8_whole(dst);
}

static long long num(const akeno_json *o, const char *key) {
    int64_t v = 0;
    return akeno_json_int64(akeno_json_get(o, key), &v) == 0 ? (long long)v : 0;
}

static int flag(const akeno_json *o, const char *key) { return akeno_json_get_bool(o, key, 0); }

int akn_voice_parse_status(const char *json, size_t len, akn_ui_voice *out) {
    memset(out, 0, sizeof(*out));
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    const akeno_json *root = akeno_json_root(doc);
    const akeno_json *st = akeno_json_get(root, "status"); /* a command's answer wraps it */
    if (st && st->type == AKENO_JSON_OBJECT) root = st;
    if (!doc || !root || root->type != AKENO_JSON_OBJECT || strcmp(akeno_json_get_string(root, "app", ""), "akeno-party-voice") != 0) {
        akeno_json_free(doc);
        out->service = AKN_VOICE_SVC_INCOMPATIBLE;
        return -1;
    }
    if (num(root, "protocol") != AKN_VOICE_PROTOCOL) {
        akeno_json_free(doc);
        out->service = AKN_VOICE_SVC_INCOMPATIBLE;
        return -1;
    }
    out->service = AKN_VOICE_SVC_READY;
    copy(out->version, sizeof(out->version), akeno_json_get_string(root, "version", ""));
    copy(out->summary, sizeof(out->summary), akeno_json_get_string(root, "summary", ""));
    out->in_voice = flag(root, "inVoice");
    const char *party = akeno_json_get_string(root, "partyId", "");
    if (akn_companion_id_ok(party)) copy(out->party_id, sizeof(out->party_id), party);
    const akeno_json *net = akeno_json_get(root, "network");
    copy(out->net, sizeof(out->net), akeno_json_get_string(net, "state", "off"));
    copy(out->net_error, sizeof(out->net_error), akeno_json_get_string(net, "error", ""));
    const akeno_json *mic = akeno_json_get(root, "mic");
    out->mic_enabled = flag(mic, "enabled");
    out->muted = flag(mic, "muted");
    out->mic_level = (int)num(mic, "level");
    out->mic_gain = (int)num(mic, "gain");
    out->mic_rc = num(mic, "rc");
    copy(out->mic, sizeof(out->mic), akeno_json_get_string(mic, "state", "off"));
    const akeno_json *o = akeno_json_get(root, "output");
    copy(out->out, sizeof(out->out), akeno_json_get_string(o, "state", "off"));
    out->out_rc = num(o, "rc");
    out->deafened = flag(o, "deafened");
    out->volume = (int)num(o, "volume");
    out->route = (int)num(o, "route");
    copy(out->route_label, sizeof(out->route_label), akeno_json_get_string(o, "routeLabel", ""));
    copy(out->paced, sizeof(out->paced), akeno_json_get_string(o, "paced", "unknown"));
    const akeno_json *ev = akeno_json_get(root, "evidence");
    out->ev_network = flag(ev, "networkConnected");
    out->ev_mic = flag(ev, "micTransmitting");
    out->ev_speaker = flag(ev, "speakerOutput");
    const akeno_json *stats = akeno_json_get(root, "stats");
    out->sent = (unsigned long)num(stats, "sent");
    out->received = (unsigned long)num(stats, "received");
    const akeno_json *people = akeno_json_get(root, "participants");
    if (people && people->type == AKENO_JSON_ARRAY) {
        for (const akeno_json *p = people->child; p; p = p->next) {
            if (p->type != AKENO_JSON_OBJECT) continue;
            out->people_total++;
            if (out->people_count >= AKN_UI_VOICE_PEOPLE) continue;
            akn_ui_voice_person *v = &out->people[out->people_count++];
            copy(v->name, sizeof(v->name), akeno_json_get_string(p, "name", ""));
            if (!v->name[0]) copy(v->name, sizeof(v->name), "Someone");
            v->muted = flag(p, "muted");
            v->speaking = flag(p, "speaking");
            v->self = flag(p, "self");
        }
    }
    akeno_json_free(doc);
    return 0;
}

static int read_key(const akn_daemon_link *link, char *key, size_t len) {
    char path[320];
    snprintf(path, sizeof(path), "%s/native.key", link->akeno_dir);
    return akn_read_native_key(path, key, len);
}

void akn_voice_load(const akn_daemon_link *link, akn_ui_voice *out) {
    memset(out, 0, sizeof(*out));
    char key[AKN_NATIVE_KEY_LEN + 1];
    if (read_key(link, key, sizeof(key)) != 0) {
        out->service = AKN_VOICE_SVC_REFUSED;
        return;
    }
    akn_http_reply reply;
    akn_loopback_get(port_of(link), "/api/voice/status", key, TIMEOUT_MS, REPLY_MAX, &reply);
    memset(key, 0, sizeof(key));
    if (reply.transport_error || !reply.body) out->service = AKN_VOICE_SVC_ABSENT;
    else if (reply.status == 403) out->service = AKN_VOICE_SVC_REFUSED;
    else if (reply.status != 200 || akn_voice_parse_status(reply.body, reply.len, out) != 0) out->service = AKN_VOICE_SVC_INCOMPATIBLE;
    akn_http_reply_free(&reply);
}

int akn_voice_send(const akn_daemon_link *link, const char *verb, const char *body, akn_ui_voice *out, char *err, size_t err_len) {
    char key[AKN_NATIVE_KEY_LEN + 1], path[64];
    if (read_key(link, key, sizeof(key)) != 0) {
        snprintf(err, err_len, "The AKENO daemon's key is missing; voice needs the daemon running.");
        return -1;
    }
    snprintf(path, sizeof(path), "/api/voice/%s", verb);
    akn_http_reply reply;
    akn_loopback_request(port_of(link), "POST", path, key, body ? body : "{}", TIMEOUT_MS, REPLY_MAX, &reply);
    memset(key, 0, sizeof(key));
    int rc = -1;
    if (reply.transport_error || !reply.body) {
        snprintf(err, err_len, "The AKENO voice service is not running. Send akeno-party-voice.elf to port 9021.");
    } else if (reply.status == 200) {
        akn_ui_voice view;
        if (akn_voice_parse_status(reply.body, reply.len, &view) == 0 || strcmp(verb, "exit") == 0) {
            if (out) *out = view;
            rc = 0;
        } else {
            snprintf(err, err_len, "The voice service sent an answer this plugin cannot read.");
        }
    } else {
        akeno_json_doc *doc = akeno_json_parse(reply.body, reply.len, NULL, 0);
        const char *message = akeno_json_get_string(akeno_json_get(akeno_json_root(doc), "error"), "message", "");
        if (reply.status == 403) snprintf(err, err_len, "The voice service refused the key. Restart it after the daemon.");
        else if (message[0]) snprintf(err, err_len, "%s", message);
        else snprintf(err, err_len, "The voice service answered HTTP %d.", reply.status);
        akn_utf8_whole(err);
        akeno_json_free(doc);
    }
    akn_http_reply_free(&reply);
    return rc;
}

static unsigned long long mix(unsigned long long h, const void *data, size_t len) {
    const unsigned char *p = data;
    for (size_t i = 0; i < len; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static unsigned long long mix_str(unsigned long long h, const char *s) { return mix(h, s, strlen(s) + 1); }

static unsigned long long mix_int(unsigned long long h, long long v) { return mix(h, &v, sizeof(v)); }

unsigned long long akn_voice_signature(const akn_ui_voice *v, int with_speaking) {
    unsigned long long h = 1469598103934665603ull;
    h = mix_int(h, v->service);
    h = mix_str(h, v->summary);
    h = mix_int(h, v->in_voice);
    h = mix_str(h, v->party_id);
    h = mix_str(h, v->net);
    h = mix_str(h, v->net_error);
    h = mix_int(h, v->mic_enabled);
    h = mix_int(h, v->muted);
    h = mix_str(h, v->mic);
    h = mix_int(h, v->mic_rc);
    h = mix_int(h, v->mic_gain);
    h = mix_str(h, v->out);
    h = mix_int(h, v->out_rc);
    h = mix_int(h, v->deafened);
    h = mix_int(h, v->volume);
    h = mix_int(h, v->route);
    h = mix_int(h, v->ev_network);
    h = mix_int(h, v->ev_mic);
    h = mix_int(h, v->ev_speaker);
    h = mix_int(h, v->people_total);
    for (int i = 0; i < v->people_count; i++) {
        h = mix_str(h, v->people[i].name);
        h = mix_int(h, v->people[i].muted);
        h = mix_int(h, v->people[i].self);
        if (with_speaking) h = mix_int(h, v->people[i].speaking);
    }
    return h;
}
