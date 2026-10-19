/*
 * The companion's link to the AKENO voice service (akeno-party-voice, clients/ps5-partyvoice): a
 * separate background payload that keeps the console in its party's voice chat while a game runs.
 * The plugin never touches audio itself; it reads the service's status and sends the user's
 * presses (join, microphone, mute, deafen, volumes, speaker route, test sound, leave) over
 * 127.0.0.1:8646 with the daemon's native key - the same key and client it uses for the daemon.
 * Whatever the service does (or if it crashed), the other pages keep working.
 */
#ifndef AKN_COMPANION_VOICE_LINK_H
#define AKN_COMPANION_VOICE_LINK_H

#include <stddef.h>

#include "companion.h"
#include "fetch.h"

#define AKN_VOICE_PORT 8646
#define AKN_VOICE_PROTOCOL 1

/* Reads the service's status. Never fails: out->service says what was found. */
void akn_voice_load(const akn_daemon_link *link, akn_ui_voice *out);

/* POST /api/voice/<verb> with a JSON body. 0 on success (*out, when not NULL, gets the new status),
 * -1 with a sentence for the page in err. */
int akn_voice_send(const akn_daemon_link *link, const char *verb, const char *body, akn_ui_voice *out, char *err, size_t err_len);

/* Parses the status document (unit-tested). 0 on success. */
int akn_voice_parse_status(const char *json, size_t len, akn_ui_voice *out);

/* A fingerprint of what the voice page shows; with_speaking 0 leaves the speaking flags and levels
 * out (they change many times a second and are only published every few seconds). */
unsigned long long akn_voice_signature(const akn_ui_voice *v, int with_speaking);

#endif
