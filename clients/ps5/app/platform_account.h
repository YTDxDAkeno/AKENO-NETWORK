/*
 * AKENO Account Bridge: what the console app may learn about the local console user and its
 * Remote Play readiness (docs/PS5_ACCOUNT_BRIDGE.md). Everything here only READS:
 *
 *   - nothing writes to the console's registry, settings or user accounts;
 *   - no Remote Play function is called (pairing functions are only looked up by name);
 *   - the account ID itself never leaves the platform layer: callers only learn whether one
 *     exists. Sign-in data, account type/flags and Remote Play registration keys are never read.
 *
 * Every step is reported through a log callback BEFORE and AFTER it runs (flushed by the caller),
 * so if a call ever hangs or ends the process, the log names the exact step.
 *
 * Implementations: platform_ps5_account.c (console) and platform_host_account.c (Linux tests,
 * driven by AKENO_SIM_* environment variables).
 */
#ifndef AKENO_APP_PLATFORM_ACCOUNT_H
#define AKENO_APP_PLATFORM_ACCOUNT_H

#include <stddef.h>

#define AKENO_CONSOLE_USER_NAME_MAX 33 /* 32 bytes of UTF-8 and the terminator */

typedef void (*akeno_bridge_log_fn)(void *ctx, const char *event, const char *detail);

typedef struct {
    int user_id;                            /* The console's number for the selected local user. */
    char name[AKENO_CONSOLE_USER_NAME_MAX]; /* Its local user name (UTF-8, may be truncated). */
} akeno_console_user;

/*
 * The local user selected on the console (the foreground user) and its name. Read-only.
 * Returns 0, -1 when a call failed, -2 when the functions are not available (err says why).
 */
int akeno_platform_console_user(akeno_console_user *out, char *err, size_t err_len, akeno_bridge_log_fn log, void *log_ctx);

/* Names looked up in libSceRemoteplay.sprx by the presence step (never called). */
#define AKENO_RP_PAIRING_NAMES 4
extern const char *const akeno_rp_pairing_names[AKENO_RP_PAIRING_NAMES];

/* Step 1: presence only. Libraries are loaded and names looked up; NOTHING is called. */
typedef struct {
    int done;
    int regmgr_loaded;          /* libSceRegMgr.sprx */
    int regmgr_get_int;         /* sceRegMgrGetInt is exported */
    int regmgr_get_bin;         /* sceRegMgrGetBin is exported */
    int remoteplay_loaded;      /* libSceRemoteplay.sprx */
    int remoteplay_names[AKENO_RP_PAIRING_NAMES];
    char error[160];
} akeno_account_presence;

int akeno_platform_account_presence(akeno_account_presence *out, akeno_bridge_log_fn log, void *log_ctx);

/* Step 2: read-only registry reads for one local user (requires step 1 to have found both functions). */
typedef struct {
    int done;
    int slot;              /* Registry user slot 1..16 holding user_id; 0 when none matched. */
    int slots_scanned;
    int user_id_errors;    /* Slots whose user ID could not be read. */
    int account_rc;        /* Return code of the account ID read (0 = read). */
    int has_account_id;    /* 1 when the stored account ID is non-zero. Only meaningful when account_rc == 0. */
    int rp_global_rc;      /* Return code of the console-wide Remote Play setting read. */
    int rp_global;         /* Its value (1 = Remote Play enabled in Settings, per two sources). */
    int rp_user_rc;        /* Return code of the per-user Remote Play value read. */
    int rp_user;           /* Its value (meaning not confirmed; one source). */
    char error[160];
} akeno_account_read;

/*
 * Returns 0 when the reads ran (individual results in out), -1 when they could not run (err).
 * It never retries and never writes.
 */
int akeno_platform_account_read(int user_id, akeno_account_read *out, akeno_bridge_log_fn log, void *log_ctx);

#endif
