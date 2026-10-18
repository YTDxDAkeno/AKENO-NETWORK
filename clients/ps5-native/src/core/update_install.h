/*
 * Side-by-side installation with automatic rollback (docs/PS5_NATIVE_UPDATER.md, "Installation").
 *
 * The websrv homebrew launcher reads AKENONative/homebrew.js, which starts an ELF with
 * --launcher --install-dir <folder>. A running ELF is never overwritten. Layout of the folder:
 *
 *   eboot.elf, homebrew.js, sce_sys/icon0.png   the version installed by hand ("factory"); the
 *                                               updater never changes eboot.elf
 *   update/versions/<version>/...               versions installed by the updater
 *   update/active.json                          which version the launcher starts, its state and the
 *                                               previous version (replaced atomically)
 *   update/staging/                             download and unpacking; wiped before every use
 *   update/history.log                          one line per install, start, rollback
 *
 * After an install active.json says "pending". The new version counts its own starts
 * (akn_update_boot) and marks itself "confirmed" once it has run normally (akn_update_confirm).
 * homebrew.js starts the previous version instead when a pending version has been started
 * AKN_UPDATE_MAX_ATTEMPTS times without confirming (it crashed or hung); the next start of an
 * updater-aware version records the rollback and never offers that version again.
 */
#ifndef AKN_UPDATE_INSTALL_H
#define AKN_UPDATE_INSTALL_H

#include <stddef.h>

#include "update.h"

#define AKN_UPDATE_MAX_ATTEMPTS 2
#define AKN_UPDATE_REJECTED_MAX 4
#define AKN_UPDATE_FACTORY_ELF "eboot.elf"

typedef struct {
    int present;
    char version[AKN_SEMVER_TEXT_MAX];
    char elf[160];                 /* relative to the launcher folder */
    int pending;                   /* not yet confirmed */
    int attempts;                  /* starts of a pending version */
    char previous_version[AKN_SEMVER_TEXT_MAX]; /* "" = the factory version */
    char previous_elf[160];
    long long installed_at;
    char rejected[AKN_UPDATE_REJECTED_MAX][AKN_SEMVER_TEXT_MAX]; /* failed to start: not offered again */
    int rejected_count;
} akn_update_active;

/* Whether dir is usable as the launcher folder (absolute, exists, holds homebrew.js). */
int akn_update_root_valid(const char *root);

/* "update/versions/<v>/eboot.elf" for a valid version, else "". */
void akn_update_version_elf(const char *version, char *out, size_t out_len);

/* Reads update/active.json; out->present = 0 when it does not exist or cannot be used. */
int akn_update_active_read(const char *root, akn_update_active *out);
int akn_update_active_write(const char *root, const akn_update_active *a);

/* Removes what an interrupted check, download or install left behind (staging, *.tmp). */
void akn_update_recover(const char *root);

/* <root>/update/staging, freshly emptied. 0 or -1. */
int akn_update_staging(const char *root, char *out, size_t out_len);

/*
 * Moves the verified files from staging_files into update/versions/<m->version>/ and makes it the
 * pending version, with the running version (own_version, own_elf relative to root) as the
 * previous one. Older versions other than these two are deleted. 0, or -1 with the reason.
 */
int akn_update_install(const char *root, const akn_update_manifest *m, const char *staging_files, const char *own_version,
                       const char *own_elf, char *why, size_t why_len);

typedef enum {
    AKN_BOOT_NORMAL = 0,     /* nothing pending */
    AKN_BOOT_TRIAL,          /* this is a newly installed version on one of its first starts */
    AKN_BOOT_ROLLED_BACK,    /* the launcher went back from a version that did not start; recorded */
    AKN_BOOT_OTHER           /* an older or the factory version was started on purpose */
} akn_boot_kind;

typedef struct {
    akn_boot_kind kind;
    char failed_version[AKN_SEMVER_TEXT_MAX]; /* AKN_BOOT_ROLLED_BACK */
    char own_elf[160];                        /* the ELF this version runs from, relative to root */
    int attempt;                              /* AKN_BOOT_TRIAL: 1, 2, ... */
} akn_boot_result;

/* Called once, early at start. Counts trial starts and records rollbacks. */
akn_boot_kind akn_update_boot(const char *root, const char *own_version, akn_boot_result *out);

/* Marks own_version as working (it ran normally). 1 when it changed something, else 0. */
int akn_update_confirm(const char *root, const char *own_version);

int akn_update_is_rejected(const akn_update_active *a, const char *version);

/* Appends a line ("2026-10-18 12:00 installed 1.2.0 ...") to update/history.log (bounded). */
void akn_update_history(const char *root, const char *line);

/* The last lines of the history, newest last. */
int akn_update_history_read(const char *root, char *out, size_t out_len);

#endif
