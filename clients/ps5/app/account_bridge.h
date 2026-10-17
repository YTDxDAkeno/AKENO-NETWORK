/*
 * AKENO Account Bridge, console side (docs/PS5_ACCOUNT_BRIDGE.md): pure logic, no system calls,
 * so it is unit-tested on Linux. The reads themselves are in platform_account.h.
 *
 * Link: the console tells the AKENO server which local user is selected on it. The paired device
 * token is the proof; the user name is a local, unverified label, never PSN identity.
 * Check: an optional, opt-in, read-only look at whether that user has an account ID and whether
 * the console's Remote Play prerequisites are present. Nothing is ever written.
 */
#ifndef AKENO_APP_ACCOUNT_BRIDGE_H
#define AKENO_APP_ACCOUNT_BRIDGE_H

#include <stddef.h>

#include "buf.h"
#include "platform_account.h"

/* The server's limit for a local user name (CONSOLE_USER_LIMITS.userNameMax), in bytes here. */
#define AKENO_BRIDGE_NAME_MAX 32

/*
 * Makes a console user name acceptable to the server: ASCII other than letters, digits, space,
 * '.', '_' and '-' becomes '_', multibyte UTF-8 is kept (the server validates it), the result is
 * trimmed and cut at a UTF-8 boundary to AKENO_BRIDGE_NAME_MAX bytes. An empty result becomes
 * "PS5 user". out_len must be at least AKENO_BRIDGE_NAME_MAX + 1.
 */
void akeno_bridge_clean_name(const char *in, char *out, size_t out_len);

/* The server's account state for a read result: "not_checked", "no_account_id",
 * "account_id_present" or "check_failed". NULL read means not checked. */
const char *akeno_bridge_account_state(const akeno_account_read *read);

/* Body of PUT /devices/current/console-user. account_state may be NULL (keep the server's). */
int akeno_bridge_link_body(const akeno_console_user *user, const char *account_state, akeno_buf *out);

/*
 * What AKENO can say about native Remote Play pairing for this user, from read-only facts only.
 * It never says pairing or streaming works: at best that the readable prerequisites are present.
 */
typedef struct {
    const char *code; /* not-checked, presence-only, blocked-no-account-id, blocked-setting-off,
                         blocked-no-library, incomplete, prerequisites-present */
    const char *summary;
} akeno_bridge_assessment;

akeno_bridge_assessment akeno_bridge_assess(const akeno_account_presence *presence, const akeno_account_read *read);

/* Appends {"presence":...,"read":...,"accountState":...,"assessment":...} members (no braces). */
void akeno_bridge_results_json(const akeno_account_presence *presence, const akeno_account_read *read, akeno_buf *b);

#endif
