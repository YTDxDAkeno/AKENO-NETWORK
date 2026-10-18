#include "update_prefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akeno/json.h"
#include "update_fs.h"

void akn_update_prefs_defaults(akn_update_prefs *p, const char *own_version) {
    memset(p, 0, sizeof(*p));
    akn_semver v;
    p->channel = own_version && akn_semver_parse(own_version, &v) == 0 && akn_semver_is_prerelease(&v) ? AKN_CHANNEL_BETA
                                                                                                    : AKN_CHANNEL_STABLE;
    p->auto_check = 1;
}

void akn_update_prefs_load(const char *dir, const char *own_version, akn_update_prefs *p) {
    akn_update_prefs_defaults(p, own_version);
    char path[320];
    if (snprintf(path, sizeof(path), "%s/updater.json", dir) >= (int)sizeof(path)) return;
    size_t len = 0;
    char *text = akn_fs_read_file(path, 4096, &len);
    if (!text) return;
    akeno_json_doc *doc = akeno_json_parse(text, len, NULL, 0);
    const akeno_json *root = akeno_json_root(doc);
    if (root && root->type == AKENO_JSON_OBJECT) {
        const char *ch = akeno_json_get_string(root, "channel", "");
        if (strcmp(ch, "beta") == 0) p->channel = AKN_CHANNEL_BETA;
        else if (strcmp(ch, "stable") == 0) p->channel = AKN_CHANNEL_STABLE;
        p->auto_check = akeno_json_get_bool(root, "auto_check", p->auto_check);
        int64_t t = 0;
        if (akeno_json_int64(akeno_json_get(root, "last_check"), &t) == 0 && t >= 0) p->last_check = (long long)t;
        akn_semver v;
        const char *seen = akeno_json_get_string(root, "last_seen", "");
        if (akn_semver_parse(seen, &v) == 0) snprintf(p->last_seen, sizeof(p->last_seen), "%s", seen);
    }
    akeno_json_free(doc);
    free(text);
}

int akn_update_prefs_save(const char *dir, const akn_update_prefs *p) {
    char path[320], text[512];
    if (snprintf(path, sizeof(path), "%s/updater.json", dir) >= (int)sizeof(path)) return -1;
    int n = snprintf(text, sizeof(text), "{\n  \"channel\": \"%s\",\n  \"auto_check\": %s,\n  \"last_check\": %lld,\n  \"last_seen\": \"%s\"\n}\n",
                     akn_update_channel_name(p->channel), p->auto_check ? "true" : "false", p->last_check, p->last_seen);
    if (n < 0 || (size_t)n >= sizeof(text)) return -1;
    if (akn_fs_mkdirs(dir) != 0) return -1;
    return akn_fs_write_atomic(path, text, (size_t)n);
}

int akn_update_prefs_due(const akn_update_prefs *p, long long now_unix) {
    if (!p->auto_check) return 0;
    if (p->last_check <= 0 || now_unix < p->last_check) return 1;
    return now_unix - p->last_check >= AKN_UPDATE_AUTO_INTERVAL_S;
}
