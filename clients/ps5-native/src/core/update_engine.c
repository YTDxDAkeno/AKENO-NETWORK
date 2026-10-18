#include "update_engine.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update_fs.h"
#include "update_install.h"
#include "update_verify.h"
#include "update_zip.h"

#define WHY(...)                                                 \
    do {                                                         \
        if (why && why_len) snprintf(why, why_len, __VA_ARGS__); \
    } while (0)

static akn_check_result fail(akn_check_outcome *out, akn_check_result r) {
    out->result = r;
    return r;
}

void akn_updater_check(const akn_updater_cfg *cfg, volatile int *cancel, akn_check_outcome *out) {
    memset(out, 0, sizeof(*out));
    akn_dl_hooks hooks = { cancel, NULL, NULL };
    akeno_buf list;
    akeno_buf_init(&list, AKN_UPDATE_RELEASES_MAX_BYTES);
    akn_dl_result r = akn_update_get(&cfg->dl, cfg->source->api_url, "application/vnd.github+json",
                                     AKN_UPDATE_RELEASES_MAX_BYTES, &list, NULL, &hooks, NULL, out->why, sizeof(out->why));
    if (r != AKN_DL_OK) {
        akeno_buf_free(&list);
        fail(out, AKN_CHECK_FAILED);
        return;
    }
    akn_pick_result pick = akn_update_pick(cfg->source, list.data ? list.data : "", list.len, cfg->env.current_version,
                                           cfg->channel, &out->release, &out->skipped, out->why, sizeof(out->why));
    akeno_buf_free(&list);
    if (pick == AKN_PICK_ERROR) {
        fail(out, AKN_CHECK_FAILED);
        return;
    }
    if (pick == AKN_PICK_UP_TO_DATE) {
        fail(out, AKN_CHECK_UP_TO_DATE);
        return;
    }
    if (cfg->root) {
        akn_update_active active;
        akn_update_active_read(cfg->root, &active);
        out->previously_failed = active.present && akn_update_is_rejected(&active, out->release.version);
    }
    if (!cfg->trusted_key) {
        snprintf(out->why, sizeof(out->why),
                 "Version %s is on GitHub, but this build has no update signing key, so it cannot verify or install it.",
                 out->release.version);
        fail(out, AKN_CHECK_UNVERIFIED);
        return;
    }
    akeno_buf manifest, sig;
    akeno_buf_init(&manifest, AKN_UPDATE_MANIFEST_MAX_BYTES);
    akeno_buf_init(&sig, AKN_UPDATE_SIGNATURE_MAX_BYTES);
    if (akn_update_get(&cfg->dl, out->release.manifest.url, "application/octet-stream", AKN_UPDATE_MANIFEST_MAX_BYTES,
                       &manifest, NULL, &hooks, NULL, out->why, sizeof(out->why)) != AKN_DL_OK ||
        akn_update_get(&cfg->dl, out->release.signature.url, "application/octet-stream", AKN_UPDATE_SIGNATURE_MAX_BYTES,
                       &sig, NULL, &hooks, NULL, out->why, sizeof(out->why)) != AKN_DL_OK) {
        akeno_buf_free(&manifest);
        akeno_buf_free(&sig);
        fail(out, AKN_CHECK_FAILED);
        return;
    }
    char vwhy[160];
    if (!manifest.data || !sig.data ||
        akn_update_verify_signature(cfg->trusted_key, manifest.data, manifest.len, (const uint8_t *)sig.data, sig.len, vwhy,
                                    sizeof(vwhy)) != 0) {
        snprintf(out->why, sizeof(out->why), "Version %s was refused: %s", out->release.version,
                 manifest.data && sig.data ? vwhy : "its description is empty.");
        akeno_buf_free(&manifest);
        akeno_buf_free(&sig);
        fail(out, AKN_CHECK_REJECTED);
        return;
    }
    int parsed = akn_update_manifest_parse(manifest.data, manifest.len, &out->release, &out->manifest, vwhy, sizeof(vwhy));
    akeno_buf_free(&manifest);
    akeno_buf_free(&sig);
    if (parsed != 0) {
        snprintf(out->why, sizeof(out->why), "Version %s was refused: %s", out->release.version, vwhy);
        fail(out, AKN_CHECK_REJECTED);
        return;
    }
    if (out->manifest.notes[0] && !out->release.notes[0])
        snprintf(out->release.notes, sizeof(out->release.notes), "%s", out->manifest.notes);
    out->compat = akn_update_compatible(&out->manifest, &cfg->env, out->why, sizeof(out->why));
    if (out->compat != AKN_COMPAT_OK) {
        fail(out, AKN_CHECK_INCOMPATIBLE);
        return;
    }
    if (!cfg->root) {
        snprintf(out->why, sizeof(out->why),
                 "Version %s is available, but AKENO was not started from its launcher folder, so it cannot install it. "
                 "Start AKENO NETWORK from the Homebrew Launcher.",
                 out->release.version);
        fail(out, AKN_CHECK_INCOMPATIBLE);
        return;
    }
    fail(out, AKN_CHECK_AVAILABLE);
}

typedef struct {
    const akn_install_hooks *hooks;
} dl_ctx;

static void dl_progress(void *ctx, long long done, long long total) {
    const akn_install_hooks *h = ((dl_ctx *)ctx)->hooks;
    if (h && h->progress) h->progress(h->ctx, AKN_PHASE_DOWNLOAD, done, total);
}

static void phase(const akn_install_hooks *h, akn_install_phase p) {
    if (h && h->progress) h->progress(h->ctx, p, 0, -1);
}

int akn_updater_install(const akn_updater_cfg *cfg, const akn_check_outcome *checked, const akn_install_hooks *hooks,
                        char *why, size_t why_len) {
    if (checked->result != AKN_CHECK_AVAILABLE || !cfg->root || !cfg->trusted_key) {
        WHY("There is no verified update to install.");
        return -1;
    }
    const akn_update_manifest *m = &checked->manifest;
    akn_update_recover(cfg->root);
    char staging[320];
    if (akn_update_staging(cfg->root, staging, sizeof(staging)) != 0) {
        const char *t = akn_fs_error_text(errno);
        WHY("Preparing the download folder failed: %s", t ? t : strerror(errno));
        return -1;
    }
    long long free_bytes = akn_fs_free_bytes(staging);
    long long needed = m->package_size * 3 + 4LL * 1024 * 1024; /* package, unpacked copy, margin */
    if (free_bytes >= 0 && free_bytes < needed) {
        char need[32], have[32];
        akn_update_format_size(needed, need, sizeof(need));
        akn_update_format_size(free_bytes, have, sizeof(have));
        WHY("Not enough free space: the update needs %s, the console has %s free.", need, have);
        akn_update_recover(cfg->root);
        return -1;
    }
    char package[400];
    snprintf(package, sizeof(package), "%s/package.zip", staging);
    dl_ctx ctx = { hooks };
    akn_dl_hooks dh = { hooks ? hooks->cancel : NULL, dl_progress, &ctx };
    akn_dl_result r = akn_update_get(&cfg->dl, checked->release.package.url, "application/octet-stream",
                                     m->package_size, NULL, package, &dh, NULL, why, why_len);
    if (r != AKN_DL_OK) {
        akn_update_recover(cfg->root);
        return -1;
    }
    phase(hooks, AKN_PHASE_VERIFY);
    size_t len = 0;
    uint8_t *data = (uint8_t *)akn_fs_read_file(package, (size_t)AKN_UPDATE_PACKAGE_MAX_BYTES, &len);
    uint8_t digest[32];
    char hex[65];
    if (!data) {
        WHY("The downloaded package cannot be read back.");
        akn_update_recover(cfg->root);
        return -1;
    }
    akn_sha256(data, len, digest);
    akn_update_hex(digest, hex);
    if ((long long)len != m->package_size || strcmp(hex, m->package_sha256) != 0) {
        WHY("The downloaded package does not match the signed checksum (damaged download).");
        free(data);
        akn_update_recover(cfg->root);
        return -1;
    }
    phase(hooks, AKN_PHASE_UNPACK);
    char files[400];
    snprintf(files, sizeof(files), "%s/files", staging);
    int rc = akn_fs_mkdirs(files) == 0 ? akn_zip_extract_package(data, len, m, files, hooks ? hooks->cancel : NULL, why, why_len)
                                       : -1;
    free(data);
    if (rc != 0) {
        if (why && why_len && !why[0]) WHY("Unpacking the update failed.");
        akn_update_recover(cfg->root);
        return -1;
    }
    /* Read every file back from storage before it can become active. */
    for (int i = 0; i < m->file_count; i++) {
        char path[512];
        long long size = 0;
        snprintf(path, sizeof(path), "%s/%s", files, m->files[i].path);
        if (akn_sha256_file(path, digest, &size) != 0 || size != m->files[i].size) {
            WHY("%s could not be read back after unpacking.", m->files[i].path);
            akn_update_recover(cfg->root);
            return -1;
        }
        akn_update_hex(digest, hex);
        if (strcmp(hex, m->files[i].sha256) != 0) {
            WHY("%s changed on storage after unpacking.", m->files[i].path);
            akn_update_recover(cfg->root);
            return -1;
        }
    }
    if (hooks && hooks->cancel && *hooks->cancel) {
        WHY("Cancelled.");
        akn_update_recover(cfg->root);
        return -1;
    }
    phase(hooks, AKN_PHASE_INSTALL);
    rc = akn_update_install(cfg->root, m, files, cfg->env.current_version, cfg->own_elf ? cfg->own_elf : "eboot.elf", why,
                            why_len);
    akn_update_recover(cfg->root);
    return rc;
}
