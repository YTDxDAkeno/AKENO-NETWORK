/*
 * akn-update: the updater engine on a PC, for the end-to-end tests (tests/updater/run.mjs) and
 * for checking a release by hand. The same code as on the console; only the file system root and
 * the key are given on the command line.
 *
 *   akn-update check   [options]       prints result=..., version=..., why=...
 *   akn-update install [options]       check, then download, verify, unpack and install
 *   akn-update boot    --root DIR --current V    what the start of version V finds (trial, rollback)
 *   akn-update confirm --root DIR --current V
 *   akn-update history --root DIR
 *
 * Options: --api URL --prefix URL (defaults: the official GitHub repository), --root DIR,
 * --key PUBLIC.pem, --channel stable|beta, --current VERSION, --firmware 12.20,
 * --daemon-protocol N, --own-elf PATH, --host NAME (repeatable; replaces the GitHub host list),
 * --ca FILE, --timeout-ms N.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update.h"
#include "update_engine.h"
#include "update_fs.h"
#include "update_install.h"
#include "update_verify.h"

static const char *k_result[] = { "up-to-date", "available", "incompatible", "unverified", "rejected", "failed" };
static const char *k_phase[] = { "download", "verify", "unpack", "install" };
static const char *k_boot[] = { "normal", "trial", "rolled-back", "other" };

static void progress(void *ctx, akn_install_phase phase, long long done, long long total) {
    (void)ctx;
    printf("progress=%s %lld %lld\n", k_phase[phase], done, total);
    fflush(stdout);
}

static int usage(void) {
    fprintf(stderr, "usage: akn-update check|install|boot|confirm|history [options] (see tools/update_cli.c)\n");
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage();
    const char *cmd = argv[1];
    akn_update_source source = *akn_update_official_source();
    akn_updater_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.source = &source;
    cfg.dl.hosts = akn_update_github_hosts();
    cfg.dl.user_agent = "AKENO-NETWORK-Native-updater-test";
    cfg.env.current_version = "0.0.0";
    cfg.channel = AKN_CHANNEL_STABLE;
    const char *hosts[9] = { 0 };
    int host_count = 0;
    char *key = NULL;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v) return usage();
        i++;
        if (strcmp(a, "--api") == 0) source.api_url = v;
        else if (strcmp(a, "--prefix") == 0) source.download_prefix = v;
        else if (strcmp(a, "--root") == 0) cfg.root = v;
        else if (strcmp(a, "--channel") == 0) cfg.channel = strcmp(v, "beta") == 0 ? AKN_CHANNEL_BETA : AKN_CHANNEL_STABLE;
        else if (strcmp(a, "--current") == 0) cfg.env.current_version = v;
        else if (strcmp(a, "--firmware") == 0) {
            int code = akn_update_firmware_code(v);
            cfg.env.firmware_raw = code < 0 ? 0 : (uint32_t)code << 16;
        } else if (strcmp(a, "--daemon-protocol") == 0) cfg.env.daemon_protocol = atoi(v);
        else if (strcmp(a, "--own-elf") == 0) cfg.own_elf = v;
        else if (strcmp(a, "--ca") == 0) cfg.dl.ca_file = v;
        else if (strcmp(a, "--timeout-ms") == 0) cfg.dl.connect_timeout_ms = cfg.dl.idle_timeout_ms = atoi(v);
        else if (strcmp(a, "--host") == 0) {
            if (host_count >= 8) return usage();
            hosts[host_count++] = v;
            cfg.dl.hosts = hosts;
        } else if (strcmp(a, "--key") == 0) {
            size_t len = 0;
            free(key);
            key = akn_fs_read_file(v, 8192, &len);
            if (!key) {
                fprintf(stderr, "cannot read %s\n", v);
                return 2;
            }
        } else return usage();
    }
    cfg.trusted_key = key ? key : akn_update_trusted_key();

    if (strcmp(cmd, "boot") == 0 || strcmp(cmd, "confirm") == 0 || strcmp(cmd, "history") == 0) {
        if (!cfg.root) return usage();
        if (strcmp(cmd, "boot") == 0) {
            akn_boot_result r;
            akn_boot_kind k = akn_update_boot(cfg.root, cfg.env.current_version, &r);
            printf("boot=%s\nattempt=%d\nfailed_version=%s\nown_elf=%s\n", k_boot[k], r.attempt, r.failed_version, r.own_elf);
        } else if (strcmp(cmd, "confirm") == 0) {
            printf("confirmed=%d\n", akn_update_confirm(cfg.root, cfg.env.current_version));
        } else {
            static char text[8192];
            akn_update_history_read(cfg.root, text, sizeof(text));
            fputs(text, stdout);
        }
        free(key);
        return 0;
    }
    if (strcmp(cmd, "check") != 0 && strcmp(cmd, "install") != 0) return usage();

    static akn_check_outcome out;
    akn_updater_check(&cfg, NULL, &out);
    printf("result=%s\nversion=%s\nprerelease=%d\nskipped=%d\npreviously_failed=%d\nwhy=%s\n", k_result[out.result],
           out.release.version, out.release.prerelease, out.skipped, out.previously_failed, out.why);
    if (out.result == AKN_CHECK_AVAILABLE) {
        char size[32];
        akn_update_format_size(out.manifest.package_size, size, sizeof(size));
        printf("size=%s\nnotes=%.200s\n", size, out.release.notes);
    }
    fflush(stdout);
    int rc = out.result == AKN_CHECK_AVAILABLE || out.result == AKN_CHECK_UP_TO_DATE ? 0 : 1;
    if (strcmp(cmd, "install") == 0) {
        if (out.result != AKN_CHECK_AVAILABLE) {
            printf("install=skipped\n");
            free(key);
            return 1;
        }
        akn_install_hooks hooks = { NULL, progress, NULL };
        char why[300] = "";
        rc = akn_updater_install(&cfg, &out, &hooks, why, sizeof(why)) == 0 ? 0 : 1;
        printf("install=%s\ninstall_why=%s\n", rc == 0 ? "ok" : "failed", why);
    }
    free(key);
    return rc;
}
