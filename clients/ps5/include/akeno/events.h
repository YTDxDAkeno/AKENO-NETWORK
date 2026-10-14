/*
 * Platform-neutral helpers for a polling client (used by the PS5 payload and by the CLI):
 *   - a monotonic "high-water mark" over the server's increasing notification ids, so only
 *     genuinely new events are surfaced, and the backlog present at startup is not replayed;
 *   - an English formatter for a notification's data, mirroring the web client's wording.
 * Both are pure and unit-tested on the host.
 */
#ifndef AKENO_EVENTS_H
#define AKENO_EVENTS_H

#include <stdint.h>

#include "json.h"

typedef struct {
    int64_t highest;
    int primed; /* The first observation only establishes the baseline. */
} akeno_watermark;

void akeno_watermark_init(akeno_watermark *w);

/*
 * Records that an event with this id has been observed. Returns 1 when it is new and should be
 * surfaced (id strictly greater than every id seen before, and not the priming observation),
 * 0 otherwise. Call it for every event in each poll, oldest first.
 */
int akeno_watermark_observe(akeno_watermark *w, int64_t id);

/* Marks the watermark primed at `highest` without surfacing anything (e.g. after the first poll). */
void akeno_watermark_prime(akeno_watermark *w, int64_t highest);

/*
 * Writes an English, one-line description of a notification's `data` object into out.
 * Returns the number of characters that would be written (like snprintf), or -1 if data is not a
 * recognized notification object. Unknown types produce a generic, safe message.
 */
int akeno_describe_notification(const akeno_json *data, char *out, size_t out_len);

#endif
