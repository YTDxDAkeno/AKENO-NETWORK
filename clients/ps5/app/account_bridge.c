/* AKENO Account Bridge, console-side logic (see account_bridge.h). No system calls here. */
#include "account_bridge.h"

#include <stdio.h>
#include <string.h>

const char *const akeno_rp_pairing_names[AKENO_RP_PAIRING_NAMES] = {
    "sceRemoteplayInitialize", "sceRemoteplayGeneratePinCode", "sceRemoteplayConfirmDeviceRegist", "sceRemoteplayNotifyPinCodeError",
};

static int allowed_ascii(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '_' || c == '-';
}

void akeno_bridge_clean_name(const char *in, char *out, size_t out_len) {
    size_t max = out_len - 1 < AKENO_BRIDGE_NAME_MAX ? out_len - 1 : AKENO_BRIDGE_NAME_MAX;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)(in ? in : "");
    while (*p == ' ') p++;
    while (*p && o < max) {
        if (*p < 0x80) {
            out[o++] = allowed_ascii(*p) ? (char)*p : '_';
            p++;
            continue;
        }
        /* A whole UTF-8 sequence or nothing; malformed bytes are dropped. */
        size_t n = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
        size_t i = 1;
        while (n && i < n && (p[i] & 0xC0) == 0x80) i++;
        if (!n || i != n) {
            p++;
            continue;
        }
        if (o + n > max) break;
        memcpy(out + o, p, n);
        o += n;
        p += n;
    }
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = '\0';
    if (o == 0) snprintf(out, out_len, "%s", "PS5 user");
}

const char *akeno_bridge_account_state(const akeno_account_read *read) {
    if (!read || !read->done) return "not_checked";
    if (!read->slot || read->account_rc != 0) return "check_failed";
    return read->has_account_id ? "account_id_present" : "no_account_id";
}

int akeno_bridge_link_body(const akeno_console_user *user, const char *account_state, akeno_buf *out) {
    char name[AKENO_BRIDGE_NAME_MAX + 1];
    akeno_bridge_clean_name(user->name, name, sizeof(name));
    int rc = akeno_buf_printf(out, "{\"localUserId\":%d,\"userName\":", user->user_id);
    rc |= akeno_buf_append_json_string(out, name);
    if (account_state) {
        rc |= akeno_buf_append_str(out, ",\"accountState\":");
        rc |= akeno_buf_append_json_string(out, account_state);
    }
    rc |= akeno_buf_append_str(out, "}");
    return rc;
}

akeno_bridge_assessment akeno_bridge_assess(const akeno_account_presence *presence, const akeno_account_read *read) {
    akeno_bridge_assessment a;
    if (!presence || !presence->done) {
        a.code = "not-checked";
        a.summary = "Not checked. The read-only check runs only when you start it.";
        return a;
    }
    int names = 0;
    for (int i = 0; i < AKENO_RP_PAIRING_NAMES; i++) names += presence->remoteplay_names[i] != 0;
    if (!presence->remoteplay_loaded || names < AKENO_RP_PAIRING_NAMES) {
        a.code = "blocked-no-library";
        a.summary = "The console's Remote Play library or its pairing functions were not found.";
        return a;
    }
    if (!read || !read->done) {
        a.code = "presence-only";
        a.summary = "The Remote Play pairing functions exist (looked up, not called). Run the read step for this user.";
        return a;
    }
    if (!read->slot || read->account_rc != 0 || read->rp_global_rc != 0) {
        a.code = "incomplete";
        a.summary = "Some read-only values could not be read, so no conclusion is possible.";
        return a;
    }
    if (!read->has_account_id) {
        a.code = "blocked-no-account-id";
        a.summary = "Blocked: this local user has no account ID. Native Remote Play pairing needs one. AKENO does not create one.";
        return a;
    }
    if (read->rp_global != 1) {
        a.code = "blocked-setting-off";
        a.summary = "Blocked: Remote Play is not enabled in the console settings.";
        return a;
    }
    a.code = "prerequisites-present";
    a.summary = "The prerequisites AKENO can read are present. Pairing was not attempted and is not verified; streaming is not verified.";
    return a;
}

static void kv_int(akeno_buf *b, const char *key, long long v, int comma) {
    akeno_buf_printf(b, "%s\"%s\":%lld", comma ? "," : "", key, v);
}

static void kv_bool(akeno_buf *b, const char *key, int v, int comma) {
    akeno_buf_printf(b, "%s\"%s\":%s", comma ? "," : "", key, v ? "true" : "false");
}

void akeno_bridge_results_json(const akeno_account_presence *presence, const akeno_account_read *read, akeno_buf *b) {
    akeno_buf_append_str(b, "\"presence\":");
    if (presence && presence->done) {
        akeno_buf_append_str(b, "{");
        kv_bool(b, "regmgrLoaded", presence->regmgr_loaded, 0);
        kv_bool(b, "getInt", presence->regmgr_get_int, 1);
        kv_bool(b, "getBin", presence->regmgr_get_bin, 1);
        kv_bool(b, "remoteplayLoaded", presence->remoteplay_loaded, 1);
        akeno_buf_append_str(b, ",\"remoteplayNames\":[");
        for (int i = 0; i < AKENO_RP_PAIRING_NAMES; i++) {
            akeno_buf_append_str(b, i ? ",{\"name\":" : "{\"name\":");
            akeno_buf_append_json_string(b, akeno_rp_pairing_names[i]);
            kv_bool(b, "found", presence->remoteplay_names[i], 1);
            akeno_buf_append_str(b, "}");
        }
        akeno_buf_append_str(b, "],\"error\":");
        akeno_buf_append_json_string(b, presence->error);
        akeno_buf_append_str(b, "}");
    } else {
        akeno_buf_append_str(b, "null");
    }
    akeno_buf_append_str(b, ",\"read\":");
    if (read && read->done) {
        akeno_buf_append_str(b, "{");
        kv_int(b, "slot", read->slot, 0);
        kv_int(b, "slotsScanned", read->slots_scanned, 1);
        kv_int(b, "userIdErrors", read->user_id_errors, 1);
        kv_int(b, "accountResult", (unsigned)read->account_rc, 1);
        if (read->slot && read->account_rc == 0) kv_bool(b, "hasAccountId", read->has_account_id, 1);
        kv_int(b, "remotePlayResult", (unsigned)read->rp_global_rc, 1);
        if (read->rp_global_rc == 0) kv_int(b, "remotePlaySetting", read->rp_global, 1);
        kv_int(b, "userRemotePlayResult", (unsigned)read->rp_user_rc, 1);
        if (read->slot && read->rp_user_rc == 0) kv_int(b, "userRemotePlayValue", read->rp_user, 1);
        akeno_buf_append_str(b, ",\"error\":");
        akeno_buf_append_json_string(b, read->error);
        akeno_buf_append_str(b, "}");
    } else {
        akeno_buf_append_str(b, "null");
    }
    akeno_buf_append_str(b, ",\"accountState\":");
    akeno_buf_append_json_string(b, akeno_bridge_account_state(read));
    akeno_bridge_assessment a = akeno_bridge_assess(presence, read);
    akeno_buf_append_str(b, ",\"assessment\":{\"code\":");
    akeno_buf_append_json_string(b, a.code);
    akeno_buf_append_str(b, ",\"summary\":");
    akeno_buf_append_json_string(b, a.summary);
    akeno_buf_append_str(b, "}");
}
