/*
 * Removes secrets and private details from text before it is logged or written to a report:
 *   - AKENO secrets: device tokens (akd_...), pairing codes (akp_...), any ak?_ value
 *   - "Bearer <token>" and "X-Akeno-*-Key: <value>"
 *   - runs of 32 or more hex digits (instance keys, session secrets)
 *   - the host (and port) of every http(s)/ws URL except loopback
 * Same policy and tests as clients/ps5-native/src/core/redact.c. NUL-terminated, never longer
 * than the input plus the markers.
 */
#ifndef AKN_EX_REDACT_H
#define AKN_EX_REDACT_H

#include <stddef.h>

char *ex_redact(char *dst, size_t dst_len, const char *src);

#endif
