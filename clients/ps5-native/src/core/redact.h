/*
 * Removes secrets and private server details from text before it is logged, shown in the
 * diagnostics screen or written to a report:
 *   - AKENO secrets: device tokens (akd_...), pairing device codes (akp_...), any ak?_ value
 *   - "Bearer <token>" and "X-Akeno-*-Key: <value>"
 *   - runs of 32 or more hex digits (instance keys, session secrets)
 *   - the host of every http(s) URL except loopback ("https://[server]/path")
 * The output is always NUL-terminated and never longer than the input plus the markers.
 */
#ifndef AKN_REDACT_H
#define AKN_REDACT_H

#include <stddef.h>

char *akn_redact(char *dst, size_t dst_len, const char *src);

/* "https (host hidden)", "http to loopback", "not configured" or "invalid": safe to log. */
const char *akn_describe_server_url(const char *url);

#endif
