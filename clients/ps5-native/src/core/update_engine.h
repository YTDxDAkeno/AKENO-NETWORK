/*
 * The updater's two blocking steps, used by the app's update worker (src/app/update_app.c) and by
 * the PC test tool (tools/update_cli.c):
 *
 *   check:   releases list -> newest candidate -> manifest + signature downloaded and verified ->
 *            manifest parsed -> compatibility with this console.
 *   install: package downloaded into staging -> size and SHA-256 against the signed manifest ->
 *            unpacked and every file checked -> installed side by side (update_install.h).
 *
 * Everything that fails leaves the running version and its data untouched.
 */
#ifndef AKN_UPDATE_ENGINE_H
#define AKN_UPDATE_ENGINE_H

#include "update.h"
#include "update_net.h"

typedef struct {
    const akn_update_source *source;
    akn_dl_config dl;
    const char *trusted_key;       /* PEM; NULL: nothing can be installed */
    const char *root;              /* the launcher folder; NULL: nothing can be installed */
    akn_update_env env;
    akn_update_channel channel;
    const char *own_elf;           /* the running ELF relative to root (update_install.h) */
} akn_updater_cfg;

typedef enum {
    AKN_CHECK_UP_TO_DATE = 0,
    AKN_CHECK_AVAILABLE,           /* verified, compatible, installable */
    AKN_CHECK_INCOMPATIBLE,        /* verified, but not for this console now (why) */
    AKN_CHECK_UNVERIFIED,          /* a release exists but this build cannot verify it (no key) */
    AKN_CHECK_REJECTED,            /* the release failed verification (why) */
    AKN_CHECK_FAILED               /* network or GitHub error (why) */
} akn_check_result;

typedef struct {
    akn_check_result result;
    akn_update_release release;
    akn_update_manifest manifest;
    akn_compat compat;
    int skipped;                   /* newer releases refused by the list checks */
    int previously_failed;         /* the version was rolled back on this console before */
    char why[300];
} akn_check_outcome;

void akn_updater_check(const akn_updater_cfg *cfg, volatile int *cancel, akn_check_outcome *out);

typedef enum { AKN_PHASE_DOWNLOAD = 0, AKN_PHASE_VERIFY, AKN_PHASE_UNPACK, AKN_PHASE_INSTALL } akn_install_phase;

typedef struct {
    volatile int *cancel;
    void (*progress)(void *ctx, akn_install_phase phase, long long done, long long total);
    void *ctx;
} akn_install_hooks;

/* 0 on success (restart to use the new version), -1 with why (cancelled: why says so). */
int akn_updater_install(const akn_updater_cfg *cfg, const akn_check_outcome *checked, const akn_install_hooks *hooks,
                        char *why, size_t why_len);

#endif
