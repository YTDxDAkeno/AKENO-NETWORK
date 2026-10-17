/*
 * PS5 implementation of the Account Bridge reads (platform_account.h). READ-ONLY by construction:
 * this file resolves no function that writes (no sceRegMgrSet*, no sceRemoteplay* call), and the
 * account ID is reduced to "present / not present" before anything leaves it.
 *
 * Nothing here is linked: like sceNotificationSend in platform_ps5.c, every function is resolved
 * with dlopen/dlsym only after an explicit user action, so the proven start-up and import set of
 * the release ELF stay exactly as they are (console/check-imports.sh). Modules are never closed:
 * the capture diagnostic showed that starting/unloading unverified modules is the risky part
 * (docs/PS5_DIRECT_CAPTURE_RESEARCH.md), and an already loaded module is not restarted by dlopen.
 *
 * Evidence for each call (docs/PS5_ACCOUNT_BRIDGE.md has the full table). No call below has run
 * on a console in this project yet; the first run is the hardware verification.
 *
 *   sceUserServiceGetForegroundUser(int *)          ps5-payload-dev/linkdev pairui.c (payload process),
 *                                                   onionHEN account_activator.cpp (ShellUI, 12.20 profile)
 *   sceUserServiceGetUserName(int, char *, size_t)  onionHEN account_activator.cpp
 *   libSceUserService.sprx                          already loaded: the release ELF links
 *                                                   sceUserServiceInitialize (verified on 12.20)
 *   sceRegMgrGetInt(int, int *)                     ps5-payload-dev/offact offact.c and linkdev regmgr.c
 *   sceRegMgrGetBin(int, void *, size_t)            (payload processes), onionHEN (ShellUI); offact
 *                                                   reports from 12.x/13.x users after an SDK rebuild
 *   libSceRemoteplay.sprx                           loaded in AKENO's process on this console (12.20,
 *                                                   capture diagnostic hardware run 2); names only here
 *
 * Registry entries read (entity numbers as in all three sources; slot n adds (n-1) * 0x10000):
 *   0x07800100  user n: user ID          int     to find the slot of the foreground user
 *   0x07800500  user n: account ID       8 bytes reduced to non-zero / zero
 *   0x41810000  Remote Play enabled      int     console-wide setting (onionHEN reads it)
 *   0x07807801  user n: Remote Play      int     per-user value; name from linkdev's table only
 * Never read: sign-in ID, account type and flags, passcode, Remote Play registration keys.
 */
#include "platform_account.h"

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REG_SLOTS 16
#define REG_SLOT_STRIDE 0x10000
#define REG_USER_ID 0x07800100
#define REG_ACCOUNT_ID 0x07800500
#define REG_USER_REMOTEPLAY 0x07807801
#define REG_REMOTEPLAY_ENABLE 0x41810000

/* Width of the arguments differs between sources (int/long, size_t/int); all values here are
 * small and positive, which the x86-64 calling convention passes identically. */
typedef int (*regmgr_get_int_fn)(int key, int *value);
typedef int (*regmgr_get_bin_fn)(int key, void *value, size_t size);
typedef int (*user_get_foreground_fn)(int *user_id);
typedef int (*user_get_name_fn)(int user_id, char *name, size_t size);

/* Resolved by the presence step; the read step uses nothing else. Main thread only. */
static regmgr_get_int_fn g_get_int;
static regmgr_get_bin_fn g_get_bin;

static void step(akeno_bridge_log_fn log, void *ctx, const char *event, const char *detail) {
    if (log) log(ctx, event, detail);
}

static void *open_library(const char *name, akeno_bridge_log_fn log, void *ctx) {
    step(log, ctx, "bridge.dlopen.begin", name);
    void *lib = dlopen(name, RTLD_LAZY);
    step(log, ctx, "bridge.dlopen.end", lib ? name : "failed");
    return lib;
}

static void *find(void *lib, const char *name, akeno_bridge_log_fn log, void *ctx) {
    step(log, ctx, "bridge.dlsym.begin", name);
    void *fn = dlsym(lib, name);
    step(log, ctx, "bridge.dlsym.end", fn ? "found" : "missing");
    return fn;
}

int akeno_platform_console_user(akeno_console_user *out, char *err, size_t err_len, akeno_bridge_log_fn log, void *log_ctx) {
    memset(out, 0, sizeof(*out));
    /* Already mapped (DT_NEEDED of this ELF), so this only looks up the existing module. */
    void *lib = open_library("libSceUserService.sprx", log, log_ctx);
    if (!lib) {
        snprintf(err, err_len, "libSceUserService.sprx could not be opened.");
        return -2;
    }
    user_get_foreground_fn foreground = (user_get_foreground_fn)find(lib, "sceUserServiceGetForegroundUser", log, log_ctx);
    user_get_name_fn get_name = (user_get_name_fn)find(lib, "sceUserServiceGetUserName", log, log_ctx);
    if (!foreground || !get_name) {
        snprintf(err, err_len, "This firmware does not export the user functions.");
        return -2;
    }
    int user_id = -1;
    step(log, log_ctx, "bridge.call.begin", "sceUserServiceGetForegroundUser");
    int rc = foreground(&user_id);
    char detail[64];
    snprintf(detail, sizeof(detail), "rc=0x%08x", (unsigned)rc);
    step(log, log_ctx, "bridge.call.end", detail);
    if (rc != 0 || user_id < 0) {
        snprintf(err, err_len, "No local user is selected (0x%08x).", (unsigned)rc);
        return -1;
    }
    char name[64];
    memset(name, 0, sizeof(name));
    step(log, log_ctx, "bridge.call.begin", "sceUserServiceGetUserName");
    rc = get_name(user_id, name, sizeof(name));
    snprintf(detail, sizeof(detail), "rc=0x%08x", (unsigned)rc);
    step(log, log_ctx, "bridge.call.end", detail);
    if (rc != 0) {
        snprintf(err, err_len, "The local user name could not be read (0x%08x).", (unsigned)rc);
        return -1;
    }
    name[sizeof(name) - 1] = '\0';
    out->user_id = user_id;
    snprintf(out->name, sizeof(out->name), "%s", name);
    return 0;
}

int akeno_platform_account_presence(akeno_account_presence *out, akeno_bridge_log_fn log, void *log_ctx) {
    memset(out, 0, sizeof(*out));
    out->done = 1;
    void *regmgr = open_library("libSceRegMgr.sprx", log, log_ctx);
    out->regmgr_loaded = regmgr != NULL;
    if (regmgr) {
        g_get_int = (regmgr_get_int_fn)find(regmgr, "sceRegMgrGetInt", log, log_ctx);
        g_get_bin = (regmgr_get_bin_fn)find(regmgr, "sceRegMgrGetBin", log, log_ctx);
        out->regmgr_get_int = g_get_int != NULL;
        out->regmgr_get_bin = g_get_bin != NULL;
    } else {
        snprintf(out->error, sizeof(out->error), "libSceRegMgr.sprx could not be loaded.");
    }
    void *rp = open_library("libSceRemoteplay.sprx", log, log_ctx);
    out->remoteplay_loaded = rp != NULL;
    for (int i = 0; rp && i < AKENO_RP_PAIRING_NAMES; i++) out->remoteplay_names[i] = find(rp, akeno_rp_pairing_names[i], log, log_ctx) != NULL;
    return out->regmgr_get_int && out->regmgr_get_bin ? 0 : -1;
}

static int read_int(int key, int *value, akeno_bridge_log_fn log, void *ctx) {
    char detail[64];
    snprintf(detail, sizeof(detail), "sceRegMgrGetInt 0x%08x", (unsigned)key);
    step(log, ctx, "bridge.read.begin", detail);
    *value = 0;
    int rc = g_get_int(key, value);
    snprintf(detail, sizeof(detail), "rc=0x%08x", (unsigned)rc);
    step(log, ctx, "bridge.read.end", detail);
    return rc;
}

int akeno_platform_account_read(int user_id, akeno_account_read *out, akeno_bridge_log_fn log, void *log_ctx) {
    memset(out, 0, sizeof(*out));
    if (!g_get_int || !g_get_bin) {
        snprintf(out->error, sizeof(out->error), "Run the presence check first; the read functions were not found.");
        return -1;
    }
    out->done = 1;
    for (int slot = 1; slot <= REG_SLOTS && !out->slot; slot++) {
        int value = 0;
        out->slots_scanned++;
        if (read_int(REG_USER_ID + (slot - 1) * REG_SLOT_STRIDE, &value, log, log_ctx) != 0) out->user_id_errors++;
        else if (value == user_id) out->slot = slot;
    }
    out->rp_global_rc = read_int(REG_REMOTEPLAY_ENABLE, &out->rp_global, log, log_ctx);
    if (!out->slot) {
        snprintf(out->error, sizeof(out->error), "The selected user was not found in the console's user table.");
        out->account_rc = -1;
        out->rp_user_rc = -1;
        return 0;
    }
    uint64_t account_id = 0;
    char detail[64];
    snprintf(detail, sizeof(detail), "sceRegMgrGetBin 0x%08x", (unsigned)(REG_ACCOUNT_ID + (out->slot - 1) * REG_SLOT_STRIDE));
    step(log, log_ctx, "bridge.read.begin", detail);
    out->account_rc = g_get_bin(REG_ACCOUNT_ID + (out->slot - 1) * REG_SLOT_STRIDE, &account_id, sizeof(account_id));
    snprintf(detail, sizeof(detail), "rc=0x%08x", (unsigned)out->account_rc);
    step(log, log_ctx, "bridge.read.end", detail);
    /* Only "present / not present" leaves this function; the value itself is never stored. */
    out->has_account_id = out->account_rc == 0 && account_id != 0;
    out->rp_user_rc = read_int(REG_USER_REMOTEPLAY + (out->slot - 1) * REG_SLOT_STRIDE, &out->rp_user, log, log_ctx);
    return 0;
}
