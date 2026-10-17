/*
 * Host (Linux) simulation of the Account Bridge reads (platform_account.h), for tests. Nothing is
 * read from the PC; the answers come from environment variables:
 *
 *   AKENO_SIM_CONSOLE_USER   "<user id>:<name>"   the selected local user (unset: not available)
 *   AKENO_SIM_REGMGR         "present"            libSceRegMgr exports both read functions
 *                                                 (unset or anything else: library not loadable)
 *   AKENO_SIM_ACCOUNT_ID     "0" or a hex value   stored account ID of that user (default 0)
 *   AKENO_SIM_ACCOUNT_RC     hex return code      make the account ID read fail (default 0)
 *   AKENO_SIM_USER_SLOT      1..16                slot holding the user (default 1; 0 = not found)
 *   AKENO_SIM_RP_GLOBAL      integer              console-wide Remote Play value (default 0)
 *   AKENO_SIM_RP_USER        integer              per-user Remote Play value (default 0)
 *   AKENO_SIM_REMOTEPLAY     "present"            libSceRemoteplay and its pairing names exist
 *
 * Every step is reported through the log callback exactly like on the console.
 */
#define _POSIX_C_SOURCE 200809L
#include "platform_account.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_present;

static void step(akeno_bridge_log_fn log, void *ctx, const char *event, const char *detail) {
    if (log) log(ctx, event, detail);
}

static long env_long(const char *name, long fallback, int base) {
    const char *v = getenv(name);
    if (!v || !v[0]) return fallback;
    return strtol(v, NULL, base);
}

int akeno_platform_console_user(akeno_console_user *out, char *err, size_t err_len, akeno_bridge_log_fn log, void *log_ctx) {
    memset(out, 0, sizeof(*out));
    const char *sim = getenv("AKENO_SIM_CONSOLE_USER");
    step(log, log_ctx, "bridge.call.begin", "sceUserServiceGetForegroundUser");
    const char *colon = sim ? strchr(sim, ':') : NULL;
    step(log, log_ctx, "bridge.call.end", colon ? "rc=0x00000000" : "unavailable");
    if (!colon) {
        snprintf(err, err_len, "Not available on this platform (only on a PS5).");
        return -2;
    }
    out->user_id = (int)strtol(sim, NULL, 10);
    snprintf(out->name, sizeof(out->name), "%s", colon + 1);
    return 0;
}

int akeno_platform_account_presence(akeno_account_presence *out, akeno_bridge_log_fn log, void *log_ctx) {
    memset(out, 0, sizeof(*out));
    out->done = 1;
    const char *regmgr = getenv("AKENO_SIM_REGMGR");
    const char *rp = getenv("AKENO_SIM_REMOTEPLAY");
    step(log, log_ctx, "bridge.dlopen.begin", "libSceRegMgr.sprx");
    g_present = regmgr && strcmp(regmgr, "present") == 0;
    step(log, log_ctx, "bridge.dlopen.end", g_present ? "libSceRegMgr.sprx" : "failed");
    out->regmgr_loaded = out->regmgr_get_int = out->regmgr_get_bin = g_present;
    if (!g_present) snprintf(out->error, sizeof(out->error), "libSceRegMgr.sprx could not be loaded.");
    out->remoteplay_loaded = rp && strcmp(rp, "present") == 0;
    for (int i = 0; i < AKENO_RP_PAIRING_NAMES; i++) out->remoteplay_names[i] = out->remoteplay_loaded;
    return g_present ? 0 : -1;
}

int akeno_platform_account_read(int user_id, akeno_account_read *out, akeno_bridge_log_fn log, void *log_ctx) {
    (void)user_id;
    memset(out, 0, sizeof(*out));
    if (!g_present) {
        snprintf(out->error, sizeof(out->error), "Run the presence check first; the read functions were not found.");
        return -1;
    }
    out->done = 1;
    long slot = env_long("AKENO_SIM_USER_SLOT", 1, 10);
    out->slots_scanned = slot >= 1 && slot <= 16 ? (int)slot : 16;
    out->slot = slot >= 1 && slot <= 16 ? (int)slot : 0;
    step(log, log_ctx, "bridge.read.begin", "sceRegMgrGetInt 0x41810000");
    out->rp_global = (int)env_long("AKENO_SIM_RP_GLOBAL", 0, 10);
    step(log, log_ctx, "bridge.read.end", "rc=0x00000000");
    if (!out->slot) {
        snprintf(out->error, sizeof(out->error), "The selected user was not found in the console's user table.");
        out->account_rc = out->rp_user_rc = -1;
        return 0;
    }
    step(log, log_ctx, "bridge.read.begin", "sceRegMgrGetBin account");
    out->account_rc = (int)(unsigned)strtoul(getenv("AKENO_SIM_ACCOUNT_RC") ? getenv("AKENO_SIM_ACCOUNT_RC") : "0", NULL, 16);
    out->has_account_id = out->account_rc == 0 && strtoull(getenv("AKENO_SIM_ACCOUNT_ID") ? getenv("AKENO_SIM_ACCOUNT_ID") : "0", NULL, 16) != 0;
    step(log, log_ctx, "bridge.read.end", out->account_rc ? "rc=error" : "rc=0x00000000");
    out->rp_user = (int)env_long("AKENO_SIM_RP_USER", 0, 10);
    return 0;
}
