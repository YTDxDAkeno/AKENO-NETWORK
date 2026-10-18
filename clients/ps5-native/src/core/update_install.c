#define _POSIX_C_SOURCE 200809L
#include "update_install.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "akeno/json.h"
#include "update_fs.h"

#define WHY(...)                                                 \
    do {                                                         \
        if (why && why_len) snprintf(why, why_len, __VA_ARGS__); \
    } while (0)

#define HISTORY_MAX (32 * 1024)

static int path_join(char *out, size_t cap, const char *root, const char *rel) {
    return snprintf(out, cap, "%s/%s", root, rel) < (int)cap ? 0 : -1;
}

int akn_update_root_valid(const char *root) {
    if (!root || root[0] != '/' || strlen(root) > 200 || strstr(root, "/../") || strstr(root, "/./")) return 0;
    char p[320];
    if (path_join(p, sizeof(p), root, "homebrew.js")) return 0;
    return akn_fs_is_dir(root) && akn_fs_is_file(p);
}

static int version_ok(const char *v) {
    akn_semver s;
    return v && akn_semver_parse(v, &s) == 0 && !s.build[0];
}

void akn_update_version_elf(const char *version, char *out, size_t out_len) {
    if (version_ok(version)) snprintf(out, out_len, "update/versions/%s/eboot.elf", version);
    else if (out_len) out[0] = '\0';
}

/* Only the factory ELF or a version's ELF may ever be named in active.json. */
static int elf_ok(const char *elf) {
    if (strcmp(elf, AKN_UPDATE_FACTORY_ELF) == 0) return 1;
    const char *prefix = "update/versions/";
    size_t n = strlen(prefix);
    if (strncmp(elf, prefix, n) != 0) return 0;
    const char *v = elf + n;
    const char *slash = strchr(v, '/');
    if (!slash || strcmp(slash, "/eboot.elf") != 0 || (size_t)(slash - v) >= AKN_SEMVER_TEXT_MAX) return 0;
    char ver[AKN_SEMVER_TEXT_MAX];
    memcpy(ver, v, (size_t)(slash - v));
    ver[slash - v] = '\0';
    return version_ok(ver);
}

int akn_update_active_read(const char *root, akn_update_active *out) {
    memset(out, 0, sizeof(*out));
    char path[320];
    if (path_join(path, sizeof(path), root, "update/active.json")) return -1;
    size_t len = 0;
    char *text = akn_fs_read_file(path, 16384, &len);
    if (!text) return errno == ENOENT ? 0 : -1;
    akeno_json_doc *doc = akeno_json_parse(text, len, NULL, 0);
    free(text);
    const akeno_json *j = akeno_json_root(doc);
    int rc = -1;
    if (j && j->type == AKENO_JSON_OBJECT) {
        int64_t format = 0, attempts = 0, at = 0;
        snprintf(out->version, sizeof(out->version), "%s", akeno_json_get_string(j, "version", ""));
        snprintf(out->elf, sizeof(out->elf), "%s", akeno_json_get_string(j, "elf", ""));
        out->pending = strcmp(akeno_json_get_string(j, "state", ""), "pending") == 0;
        (void)akeno_json_int64(akeno_json_get(j, "attempts"), &attempts);
        (void)akeno_json_int64(akeno_json_get(j, "installed_at"), &at);
        out->attempts = attempts < 0 ? 0 : attempts > 1000 ? 1000 : (int)attempts;
        out->installed_at = at;
        const akeno_json *prev = akeno_json_get(j, "previous");
        snprintf(out->previous_version, sizeof(out->previous_version), "%s", akeno_json_get_string(prev, "version", ""));
        snprintf(out->previous_elf, sizeof(out->previous_elf), "%s",
                 akeno_json_get_string(prev, "elf", AKN_UPDATE_FACTORY_ELF));
        const akeno_json *rej = akeno_json_get(j, "rejected");
        for (const akeno_json *r = rej && rej->type == AKENO_JSON_ARRAY ? rej->child : NULL;
             r && out->rejected_count < AKN_UPDATE_REJECTED_MAX; r = r->next) {
            const char *v = akeno_json_string(r, "");
            if (version_ok(v)) snprintf(out->rejected[out->rejected_count++], AKN_SEMVER_TEXT_MAX, "%s", v);
        }
        if (akeno_json_int64(akeno_json_get(j, "format"), &format) == 0 && format == 1 && version_ok(out->version) &&
            elf_ok(out->elf) && elf_ok(out->previous_elf) && (!out->previous_version[0] || version_ok(out->previous_version))) {
            out->present = 1;
            rc = 0;
        }
    }
    akeno_json_free(doc);
    if (rc != 0) {
        akn_update_active keep;
        memset(&keep, 0, sizeof(keep));
        *out = keep; /* unusable: the launcher starts the factory version */
    }
    return rc;
}

int akn_update_active_write(const char *root, const akn_update_active *a) {
    if (!version_ok(a->version) || !elf_ok(a->elf) || !elf_ok(a->previous_elf) ||
        (a->previous_version[0] && !version_ok(a->previous_version)))
        return -1;
    char rejected[AKN_UPDATE_REJECTED_MAX * (AKN_SEMVER_TEXT_MAX + 4) + 4] = "";
    size_t used = 0;
    for (int i = 0; i < a->rejected_count && i < AKN_UPDATE_REJECTED_MAX; i++) {
        if (!version_ok(a->rejected[i])) continue;
        int w = snprintf(rejected + used, sizeof(rejected) - used, "%s\"%s\"", used ? "," : "", a->rejected[i]);
        if (w < 0 || (size_t)w >= sizeof(rejected) - used) break;
        used += (size_t)w;
    }
    char text[2048];
    int n = snprintf(text, sizeof(text),
                     "{\n  \"format\": 1,\n  \"version\": \"%s\",\n  \"elf\": \"%s\",\n  \"state\": \"%s\",\n"
                     "  \"attempts\": %d,\n  \"max_attempts\": %d,\n  \"installed_at\": %lld,\n"
                     "  \"previous\": { \"version\": \"%s\", \"elf\": \"%s\" },\n  \"rejected\": [%s]\n}\n",
                     a->version, a->elf, a->pending ? "pending" : "confirmed", a->attempts, AKN_UPDATE_MAX_ATTEMPTS,
                     a->installed_at, a->previous_version, a->previous_elf, rejected);
    if (n < 0 || (size_t)n >= sizeof(text)) return -1;
    char dir[320], path[320];
    if (path_join(dir, sizeof(dir), root, "update") || path_join(path, sizeof(path), root, "update/active.json")) return -1;
    if (akn_fs_mkdirs(dir) != 0) return -1;
    return akn_fs_write_atomic(path, text, (size_t)n);
}

void akn_update_recover(const char *root) {
    char p[320];
    if (path_join(p, sizeof(p), root, "update/staging") == 0) akn_fs_remove_tree(p);
    if (path_join(p, sizeof(p), root, "update/active.json.tmp") == 0) unlink(p);
    char versions[320];
    if (path_join(versions, sizeof(versions), root, "update/versions")) return;
    DIR *d = opendir(versions);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 4 && strcmp(e->d_name + n - 4, ".tmp") == 0) {
            char child[400];
            if (snprintf(child, sizeof(child), "%s/%s", versions, e->d_name) < (int)sizeof(child)) akn_fs_remove_tree(child);
        }
    }
    closedir(d);
}

int akn_update_staging(const char *root, char *out, size_t out_len) {
    if (path_join(out, out_len, root, "update/staging")) return -1;
    if (akn_fs_remove_tree(out) != 0) return -1;
    return akn_fs_mkdirs(out);
}

static void stamp(char *out, size_t cap) {
    time_t now = time(NULL);
    struct tm tm;
    if (gmtime_r(&now, &tm)) strftime(out, cap, "%Y-%m-%d %H:%M UTC", &tm);
    else snprintf(out, cap, "unknown time");
}

void akn_update_history(const char *root, const char *line) {
    char path[320], dir[320];
    if (path_join(path, sizeof(path), root, "update/history.log") || path_join(dir, sizeof(dir), root, "update")) return;
    if (akn_fs_mkdirs(dir) != 0) return;
    size_t len = 0;
    char *old = akn_fs_read_file(path, HISTORY_MAX * 2, &len);
    /* Keep the newest half when the file grows too large. */
    const char *keep = old ? old : "";
    if (old && len > HISTORY_MAX) {
        keep = old + len - HISTORY_MAX / 2;
        const char *nl = strchr(keep, '\n');
        keep = nl ? nl + 1 : old + len;
    }
    char when[40];
    stamp(when, sizeof(when));
    size_t cap = strlen(keep) + strlen(line) + 64;
    char *text = malloc(cap);
    if (text) {
        int n = snprintf(text, cap, "%s%s  %s\n", keep, when, line);
        if (n > 0) (void)akn_fs_write_atomic(path, text, (size_t)n);
        free(text);
    }
    free(old);
}

int akn_update_history_read(const char *root, char *out, size_t out_len) {
    if (!out_len) return -1;
    out[0] = '\0';
    char path[320];
    if (path_join(path, sizeof(path), root, "update/history.log")) return -1;
    size_t len = 0;
    char *text = akn_fs_read_file(path, HISTORY_MAX * 2, &len);
    if (!text) return -1;
    const char *start = text;
    if (len >= out_len) {
        start = text + len - (out_len - 1);
        const char *nl = strchr(start, '\n');
        if (nl) start = nl + 1;
    }
    snprintf(out, out_len, "%s", start);
    free(text);
    return 0;
}

static void remove_version_dir(const char *root, const char *name) {
    char p[400];
    if (snprintf(p, sizeof(p), "%s/update/versions/%s", root, name) < (int)sizeof(p)) akn_fs_remove_tree(p);
}

int akn_update_install(const char *root, const akn_update_manifest *m, const char *staging_files, const char *own_version,
                       const char *own_elf, char *why, size_t why_len) {
    if (!version_ok(m->version) || !version_ok(own_version) || !elf_ok(own_elf)) {
        WHY("Internal error: invalid version or path.");
        return -1;
    }
    char versions[320], target[400], tmp[400];
    if (path_join(versions, sizeof(versions), root, "update/versions") ||
        snprintf(target, sizeof(target), "%s/%s", versions, m->version) >= (int)sizeof(target) ||
        snprintf(tmp, sizeof(tmp), "%s/%s.tmp", versions, m->version) >= (int)sizeof(tmp)) {
        WHY("The install path is too long.");
        return -1;
    }
    if (akn_fs_mkdirs(versions) != 0) {
        const char *t = akn_fs_error_text(errno);
        WHY("Cannot create the versions folder: %s", t ? t : strerror(errno));
        return -1;
    }
    char current_elf[160];
    akn_update_version_elf(m->version, current_elf, sizeof(current_elf));
    if (strcmp(own_elf, current_elf) == 0) {
        WHY("Version %s is the one running; it cannot be replaced while it runs.", m->version);
        return -1;
    }
    /* staging -> versions/<v>.tmp -> versions/<v>: each step a rename on the same file system. */
    akn_fs_remove_tree(tmp);
    if (rename(staging_files, tmp) != 0) {
        const char *t = akn_fs_error_text(errno);
        WHY("Moving the update into place failed: %s", t ? t : strerror(errno));
        return -1;
    }
    akn_fs_remove_tree(target); /* a half-installed or rejected copy of the same version */
    if (rename(tmp, target) != 0) {
        const char *t = akn_fs_error_text(errno);
        WHY("Moving the update into place failed: %s", t ? t : strerror(errno));
        akn_fs_remove_tree(tmp);
        return -1;
    }
    akn_fs_sync_dir(versions);

    akn_update_active before, next;
    akn_update_active_read(root, &before);
    memset(&next, 0, sizeof(next));
    next.present = 1;
    snprintf(next.version, sizeof(next.version), "%s", m->version);
    snprintf(next.elf, sizeof(next.elf), "%s", current_elf);
    next.pending = 1;
    next.attempts = 0;
    next.installed_at = (long long)time(NULL);
    snprintf(next.previous_elf, sizeof(next.previous_elf), "%s", own_elf);
    /* The factory ELF has no version entry: "" stands for it; otherwise the running version. */
    if (strcmp(own_elf, AKN_UPDATE_FACTORY_ELF) != 0) snprintf(next.previous_version, sizeof(next.previous_version), "%s", own_version);
    for (int i = 0; i < before.rejected_count; i++)
        if (strcmp(before.rejected[i], m->version) != 0)
            snprintf(next.rejected[next.rejected_count++], AKN_SEMVER_TEXT_MAX, "%s", before.rejected[i]);
    if (akn_update_active_write(root, &next) != 0) {
        const char *t = akn_fs_error_text(errno);
        WHY("Activating the update failed: %s", t ? t : strerror(errno));
        akn_fs_remove_tree(target);
        return -1;
    }

    /* Keep the new version and the one it falls back to; delete the rest of AKENO's versions. */
    DIR *d = opendir(versions);
    if (d) {
        struct dirent *e;
        char keep_prev[AKN_SEMVER_TEXT_MAX] = "";
        snprintf(keep_prev, sizeof(keep_prev), "%s", next.previous_version);
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (strcmp(e->d_name, next.version) == 0 || (keep_prev[0] && strcmp(e->d_name, keep_prev) == 0)) continue;
            if (!version_ok(e->d_name)) continue; /* not ours */
            remove_version_dir(root, e->d_name);
        }
        closedir(d);
    }
    char line[256];
    snprintf(line, sizeof(line), "installed %s (pending its first start; falls back to %s)", m->version,
             next.previous_version[0] ? next.previous_version : "the factory version");
    akn_update_history(root, line);
    return 0;
}

int akn_update_is_rejected(const akn_update_active *a, const char *version) {
    for (int i = 0; i < a->rejected_count; i++)
        if (strcmp(a->rejected[i], version) == 0) return 1;
    return 0;
}

akn_boot_kind akn_update_boot(const char *root, const char *own_version, akn_boot_result *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->own_elf, sizeof(out->own_elf), "%s", AKN_UPDATE_FACTORY_ELF);
    akn_update_recover(root);
    akn_update_active a;
    akn_update_active_read(root, &a);
    if (!a.present) return out->kind = AKN_BOOT_NORMAL;
    char line[256];
    if (strcmp(a.version, own_version) == 0) {
        snprintf(out->own_elf, sizeof(out->own_elf), "%s", a.elf);
        if (!a.pending) return out->kind = AKN_BOOT_NORMAL;
        a.attempts++;
        out->attempt = a.attempts;
        akn_update_active_write(root, &a);
        snprintf(line, sizeof(line), "started %s (trial start %d of %d)", a.version, a.attempts, AKN_UPDATE_MAX_ATTEMPTS);
        akn_update_history(root, line);
        return out->kind = AKN_BOOT_TRIAL;
    }
    if (a.previous_version[0] && strcmp(a.previous_version, own_version) == 0)
        snprintf(out->own_elf, sizeof(out->own_elf), "%s", a.previous_elf);
    if (a.pending && a.attempts >= AKN_UPDATE_MAX_ATTEMPTS) {
        /* The launcher fell back: make the previous version the active one and remember the failure. */
        snprintf(out->failed_version, sizeof(out->failed_version), "%s", a.version);
        akn_update_active next;
        memset(&next, 0, sizeof(next));
        next.present = 1;
        if (a.previous_version[0]) {
            snprintf(next.version, sizeof(next.version), "%s", a.previous_version);
            snprintf(next.elf, sizeof(next.elf), "%s", a.previous_elf);
        } else {
            snprintf(next.version, sizeof(next.version), "%s", own_version);
            snprintf(next.elf, sizeof(next.elf), "%s", AKN_UPDATE_FACTORY_ELF);
        }
        snprintf(next.previous_elf, sizeof(next.previous_elf), "%s", AKN_UPDATE_FACTORY_ELF);
        next.installed_at = a.installed_at;
        for (int i = 0; i < a.rejected_count && next.rejected_count < AKN_UPDATE_REJECTED_MAX - 1; i++)
            snprintf(next.rejected[next.rejected_count++], AKN_SEMVER_TEXT_MAX, "%s", a.rejected[i]);
        snprintf(next.rejected[next.rejected_count++], AKN_SEMVER_TEXT_MAX, "%s", a.version);
        if (akn_update_active_write(root, &next) == 0) remove_version_dir(root, a.version);
        snprintf(line, sizeof(line), "rolled back from %s to %s: it did not finish starting %d times", a.version,
                 own_version, a.attempts);
        akn_update_history(root, line);
        return out->kind = AKN_BOOT_ROLLED_BACK;
    }
    return out->kind = AKN_BOOT_OTHER;
}

int akn_update_confirm(const char *root, const char *own_version) {
    akn_update_active a;
    akn_update_active_read(root, &a);
    if (!a.present || !a.pending || strcmp(a.version, own_version) != 0) return 0;
    a.pending = 0;
    a.attempts = 0;
    if (akn_update_active_write(root, &a) != 0) return 0;
    char line[160];
    snprintf(line, sizeof(line), "%s started normally (confirmed)", a.version);
    akn_update_history(root, line);
    return 1;
}
