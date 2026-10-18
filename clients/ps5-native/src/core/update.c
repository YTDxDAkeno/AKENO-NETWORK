#include "update.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "akeno/json.h"

const char *akn_update_channel_name(akn_update_channel channel) {
    return channel == AKN_CHANNEL_BETA ? "beta" : "stable";
}

const akn_update_source *akn_update_official_source(void) {
    static const akn_update_source src = { AKN_UPDATE_API_URL, AKN_UPDATE_DOWNLOAD_PREFIX };
    return &src;
}

#define WHY(...)                                              \
    do {                                                      \
        if (why && why_len) snprintf(why, why_len, __VA_ARGS__); \
    } while (0)

static void copy(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    snprintf(dst, cap, "%s", src ? src : "");
}

/* Tag "native-1.2.0-beta.1" -> version "1.2.0-beta.1"; only versions without build metadata. */
static int tag_version(const char *tag, akn_semver *v) {
    size_t n = strlen(AKN_UPDATE_TAG_PREFIX);
    if (strncmp(tag, AKN_UPDATE_TAG_PREFIX, n) != 0) return -1;
    if (akn_semver_parse(tag + n, v)) return -1;
    return v->build[0] ? -1 : 0;
}

/* The asset's URL must be exactly <prefix><tag>/<name> and the asset uploaded and not empty. */
static int find_asset(const akn_update_source *src, const akeno_json *assets, const char *tag, const char *name,
                      long long max_size, akn_update_asset *out) {
    for (const akeno_json *a = assets ? assets->child : NULL; a; a = a->next) {
        if (strcmp(akeno_json_get_string(a, "name", ""), name) != 0) continue;
        const char *state = akeno_json_get_string(a, "state", "uploaded");
        if (strcmp(state, "uploaded") != 0) return -1;
        int64_t size = 0;
        if (akeno_json_int64(akeno_json_get(a, "size"), &size) || size <= 0 || size > max_size) return -1;
        char expected[sizeof(out->url)];
        int w = snprintf(expected, sizeof(expected), "%s%s/%s", src->download_prefix, tag, name);
        if (w < 0 || (size_t)w >= sizeof(expected)) return -1;
        if (strcmp(akeno_json_get_string(a, "browser_download_url", ""), expected) != 0) return -1;
        copy(out->url, sizeof(out->url), expected);
        out->size = size;
        return 0;
    }
    return -1;
}

akn_pick_result akn_update_pick(const akn_update_source *src, const char *json, size_t len, const char *current_version,
                                akn_update_channel channel, akn_update_release *out, int *skipped, char *why,
                                size_t why_len) {
    if (skipped) *skipped = 0;
    if (why && why_len) why[0] = '\0';
    akn_semver current;
    if (akn_semver_parse(current_version, &current)) {
        WHY("The running version \"%s\" is not a valid version.", current_version);
        return AKN_PICK_ERROR;
    }
    char err[96];
    akeno_json_doc *doc = akeno_json_parse(json, len, err, sizeof(err));
    const akeno_json *root = akeno_json_root(doc);
    if (!root || root->type != AKENO_JSON_ARRAY) {
        WHY("GitHub's release list could not be read (%s).", doc ? "not a list" : err);
        akeno_json_free(doc);
        return AKN_PICK_ERROR;
    }
    int found = 0;
    akn_semver best;
    memset(&best, 0, sizeof(best));
    for (const akeno_json *r = root->child; r; r = r->next) {
        if (akeno_json_get_bool(r, "draft", 1)) continue;
        const char *tag = akeno_json_get_string(r, "tag_name", "");
        akn_semver v;
        if (tag_version(tag, &v)) continue; /* not an AKENO Native release (or a malformed tag) */
        if (akn_semver_compare(&v, &current) <= 0) continue;
        int pre = akeno_json_get_bool(r, "prerelease", 0) || akn_semver_is_prerelease(&v);
        if (pre && channel != AKN_CHANNEL_BETA) continue;
        if (found && akn_semver_compare(&v, &best) <= 0) continue;
        akn_update_release rel;
        memset(&rel, 0, sizeof(rel));
        copy(rel.tag, sizeof(rel.tag), tag);
        akn_semver_format(&v, rel.version, sizeof(rel.version));
        snprintf(rel.package_name, sizeof(rel.package_name), "akeno-native-%s.zip", rel.version);
        const akeno_json *assets = akeno_json_get(r, "assets");
        if (find_asset(src, assets, tag, AKN_UPDATE_MANIFEST_ASSET, AKN_UPDATE_MANIFEST_MAX_BYTES, &rel.manifest) ||
            find_asset(src, assets, tag, AKN_UPDATE_SIGNATURE_ASSET, AKN_UPDATE_SIGNATURE_MAX_BYTES, &rel.signature) ||
            find_asset(src, assets, tag, rel.package_name, AKN_UPDATE_PACKAGE_MAX_BYTES, &rel.package)) {
            if (skipped) (*skipped)++;
            WHY("Release %s was skipped: its update files are missing or not from %s.", tag,
                    src->download_prefix);
            continue;
        }
        rel.prerelease = pre;
        copy(rel.name, sizeof(rel.name), akeno_json_get_string(r, "name", tag));
        copy(rel.published_at, sizeof(rel.published_at), akeno_json_get_string(r, "published_at", ""));
        copy(rel.html_url, sizeof(rel.html_url), akeno_json_get_string(r, "html_url", ""));
        copy(rel.notes, sizeof(rel.notes), akeno_json_get_string(r, "body", ""));
        *out = rel;
        best = v;
        found = 1;
    }
    akeno_json_free(doc);
    return found ? AKN_PICK_FOUND : AKN_PICK_UP_TO_DATE;
}

int akn_update_path_allowed(const char *path) {
    static const char *const allowed[] = { "eboot.elf", "homebrew.js", "sce_sys/icon0.png",
                                           "daemon/akeno-network-ps5-native-ipc.elf" };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
        if (strcmp(path, allowed[i]) == 0) return 1;
    return 0;
}

static int is_hex64(const char *s) {
    if (strlen(s) != 64) return 0;
    for (int i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i])) return 0;
    return 1;
}

static int get_size(const akeno_json *obj, const char *key, long long max, long long *out) {
    int64_t v = 0;
    if (akeno_json_int64(akeno_json_get(obj, key), &v) || v <= 0 || v > max) return -1;
    *out = v;
    return 0;
}

static int get_int(const akeno_json *obj, const char *key, int *out) {
    const akeno_json *n = akeno_json_get(obj, key);
    if (!n) {
        *out = 0;
        return 0;
    }
    int64_t v = 0;
    if (akeno_json_int64(n, &v) || v < 0 || v > 1000) return -1;
    *out = (int)v;
    return 0;
}

int akn_update_firmware_code(const char *text) {
    unsigned major = 0, minor = 0;
    char extra;
    if (!text || sscanf(text, "%2u.%2u%c", &major, &minor, &extra) != 2) return -1;
    if (strlen(text) != 5 || text[2] != '.') return -1;
    return (int)(((major / 10) << 12) | ((major % 10) << 8) | ((minor / 10) << 4) | (minor % 10));
}

int akn_update_manifest_parse(const char *json, size_t len, const akn_update_release *release,
                              akn_update_manifest *out, char *why, size_t why_len) {
    memset(out, 0, sizeof(*out));
    char err[96];
    akeno_json_doc *doc = akeno_json_parse(json, len, err, sizeof(err));
    const akeno_json *m = akeno_json_root(doc);
    int rc = -1;
#define FAIL(...)                                                   \
    do {                                                            \
        if (why && why_len) snprintf(why, why_len, __VA_ARGS__);    \
        goto done;                                                  \
    } while (0)
    if (!m || m->type != AKENO_JSON_OBJECT) FAIL("The update description is not valid JSON.");
    int64_t format = 0;
    if (akeno_json_int64(akeno_json_get(m, "format"), &format) || format != 1)
        FAIL("The update description has an unknown format.");
    if (strcmp(akeno_json_get_string(m, "product", ""), AKN_UPDATE_PRODUCT) != 0)
        FAIL("The update is not for AKENO NETWORK Native.");
    copy(out->version, sizeof(out->version), akeno_json_get_string(m, "version", ""));
    copy(out->tag, sizeof(out->tag), akeno_json_get_string(m, "tag", ""));
    akn_semver v;
    if (akn_semver_parse(out->version, &v) || v.build[0]) FAIL("The update has an invalid version.");
    /* The signed description must belong to exactly this release (no replay under another tag). */
    if (strcmp(out->tag, release->tag) != 0 || strcmp(out->version, release->version) != 0)
        FAIL("The update description belongs to %s, not to release %s.", out->tag, release->tag);
    copy(out->channel, sizeof(out->channel), akeno_json_get_string(m, "channel", ""));
    if (strcmp(out->channel, "stable") != 0 && strcmp(out->channel, "beta") != 0)
        FAIL("The update names an unknown channel.");
    if (strcmp(out->channel, "stable") == 0 && akn_semver_is_prerelease(&v))
        FAIL("A pre-release cannot be on the stable channel.");

    const akeno_json *pkg = akeno_json_get(m, "package");
    copy(out->package_name, sizeof(out->package_name), akeno_json_get_string(pkg, "name", ""));
    copy(out->package_sha256, sizeof(out->package_sha256), akeno_json_get_string(pkg, "sha256", ""));
    if (strcmp(out->package_name, release->package_name) != 0) FAIL("The update package has an unexpected name.");
    if (get_size(pkg, "size", AKN_UPDATE_PACKAGE_MAX_BYTES, &out->package_size) || !is_hex64(out->package_sha256))
        FAIL("The update package's size or checksum is missing.");
    if (release->package.size && release->package.size != out->package_size)
        FAIL("The package on GitHub does not have the signed size.");

    const akeno_json *files = akeno_json_get(m, "files");
    if (!files || files->type != AKENO_JSON_ARRAY || files->count == 0 || files->count > AKN_UPDATE_FILES_MAX)
        FAIL("The update lists no files or too many.");
    int have_eboot = 0;
    for (const akeno_json *f = files->child; f; f = f->next) {
        akn_update_file *uf = &out->files[out->file_count];
        copy(uf->path, sizeof(uf->path), akeno_json_get_string(f, "path", ""));
        copy(uf->sha256, sizeof(uf->sha256), akeno_json_get_string(f, "sha256", ""));
        if (!akn_update_path_allowed(uf->path)) FAIL("The update contains a file AKENO does not install: %.60s", uf->path);
        if (get_size(f, "size", AKN_UPDATE_FILE_MAX_BYTES, &uf->size) || !is_hex64(uf->sha256))
            FAIL("A file of the update has no valid size or checksum: %.60s", uf->path);
        for (int i = 0; i < out->file_count; i++)
            if (strcmp(out->files[i].path, uf->path) == 0) FAIL("The update lists a file twice: %.60s", uf->path);
        if (strcmp(uf->path, "eboot.elf") == 0) have_eboot = 1;
        out->file_count++;
    }
    if (!have_eboot) FAIL("The update does not contain the program (eboot.elf).");

    const akeno_json *req = akeno_json_get(m, "requires");
    copy(out->firmware_min, sizeof(out->firmware_min), akeno_json_get_string(req, "firmware_min", ""));
    copy(out->firmware_max, sizeof(out->firmware_max), akeno_json_get_string(req, "firmware_max", ""));
    if ((out->firmware_min[0] && akn_update_firmware_code(out->firmware_min) < 0) ||
        (out->firmware_max[0] && akn_update_firmware_code(out->firmware_max) < 0))
        FAIL("The update names an invalid firmware range.");
    if (get_int(req, "daemon_protocol_min", &out->daemon_protocol_min) ||
        get_int(req, "daemon_protocol_max", &out->daemon_protocol_max))
        FAIL("The update names an invalid daemon protocol.");
    copy(out->min_installed, sizeof(out->min_installed), akeno_json_get_string(req, "min_installed_version", ""));
    if (out->min_installed[0] && akn_semver_parse(out->min_installed, &v)) FAIL("The update names an invalid minimum version.");
    copy(out->notes, sizeof(out->notes), akeno_json_get_string(m, "notes", ""));
    rc = 0;
#undef FAIL
done:
    akeno_json_free(doc);
    return rc;
}

akn_compat akn_update_compatible(const akn_update_manifest *m, const akn_update_env *env, char *why, size_t why_len) {
    if (why && why_len) why[0] = '\0';
    if (akn_semver_compare_text(m->version, env->current_version) <= 0) {
        WHY("Version %s is not newer than %s.", m->version, env->current_version);
        return AKN_COMPAT_NOT_NEWER;
    }
    if (m->min_installed[0] && akn_semver_compare_text(env->current_version, m->min_installed) < 0) {
        WHY("This update needs version %s or newer installed first (you have %s).", m->min_installed,
                env->current_version);
        return AKN_COMPAT_FROM_VERSION;
    }
    int fw = env->firmware_raw ? (int)(env->firmware_raw >> 16) : -1;
    if (m->firmware_min[0] || m->firmware_max[0]) {
        if (fw < 0) {
            WHY("The console's firmware is unknown; this update needs %s to %s.",
                    m->firmware_min[0] ? m->firmware_min : "any", m->firmware_max[0] ? m->firmware_max : "any");
            return AKN_COMPAT_FIRMWARE;
        }
        if ((m->firmware_min[0] && fw < akn_update_firmware_code(m->firmware_min)) ||
            (m->firmware_max[0] && fw > akn_update_firmware_code(m->firmware_max))) {
            WHY("This update supports firmware %s to %s only.", m->firmware_min[0] ? m->firmware_min : "any",
                    m->firmware_max[0] ? m->firmware_max : "any");
            return AKN_COMPAT_FIRMWARE;
        }
    }
    if (m->daemon_protocol_min || m->daemon_protocol_max) {
        char need[48];
        if (m->daemon_protocol_max) snprintf(need, sizeof(need), "%d to %d", m->daemon_protocol_min, m->daemon_protocol_max);
        else snprintf(need, sizeof(need), "%d or newer", m->daemon_protocol_min);
        if (env->daemon_protocol < 0) {
            WHY("The AKENO daemon is not running; this update needs daemon protocol %s. Start the daemon and check again.",
                    need);
            return AKN_COMPAT_DAEMON;
        }
        if (env->daemon_protocol < m->daemon_protocol_min ||
            (m->daemon_protocol_max && env->daemon_protocol > m->daemon_protocol_max)) {
            char have[16];
            snprintf(have, sizeof(have), "%d", env->daemon_protocol);
            WHY("This update needs daemon protocol %s; the running daemon has %s.", need, have);
            return AKN_COMPAT_DAEMON;
        }
    }
    return AKN_COMPAT_OK;
}

void akn_update_hex(const uint8_t digest[32], char out[65]) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = digits[digest[i] >> 4];
        out[i * 2 + 1] = digits[digest[i] & 15];
    }
    out[64] = '\0';
}

void akn_update_format_size(long long bytes, char *out, size_t out_len) {
    if (bytes < 0) bytes = 0;
    if (bytes >= 1024LL * 1024LL) snprintf(out, out_len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024) snprintf(out, out_len, "%lld KB", (bytes + 512) / 1024);
    else snprintf(out, out_len, "%lld bytes", bytes);
}
