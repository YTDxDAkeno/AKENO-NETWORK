/*
 * The local control interface of AKENO Party Voice (protocol 1), on 127.0.0.1 only. The onionHEN
 * plugin AKEN00001 (and AKENO Native, later) use it with the AKENO daemon's per-launch native key,
 * exactly as they use the daemon: the same key file (/data/akeno/native.key, owner-only), the same
 * header (X-Akeno-Native-Key), and requests with an Origin header (web pages) are refused.
 *
 *   GET  /api/voice/hello                        no key: {"ok","app","version","protocol"}
 *   GET  /api/voice/status                       the status document (pv_status.h)
 *   POST /api/voice/join      {"partyId":"<id>"} join that party's voice (microphone off)
 *   POST /api/voice/leave     {}
 *   POST /api/voice/mic       {"enabled":bool}   the user turned the microphone on (consent) or off
 *   POST /api/voice/mute      {"muted":bool}
 *   POST /api/voice/deafen    {"deafened":bool}
 *   POST /api/voice/volume    {"output":0-150} and/or {"mic":50-150}
 *   POST /api/voice/route     {"route":1-4}      speaker route (the Voice Lab's four variants)
 *   POST /api/voice/test-sound {}                N beeps on route N
 *   POST /api/voice/exit      {}                 leave voice and stop the service
 *
 * Bodies are JSON objects with exactly the listed members and types; anything else is refused.
 * Answers: {"ok":true,"status":{...}} or {"ok":false,"error":{"code","message"}}.
 */
#ifndef PV_CONTROL_H
#define PV_CONTROL_H

#include "pv_engine.h"

typedef struct {
    char akeno_dir[256]; /* where native.key is */
    int port;            /* the port this interface listens on (Host header check) */
} pv_control_config;

/* Listens on 127.0.0.1:port (0: any free port). Returns the socket, or -1 (*in_use when another
 * process has the port: the service is already running). */
int pv_control_listen(int port, int *bound_port, int *in_use);

/* Serves the connections that arrive within wait_ms, one at a time. Returns 1 when a client asked
 * the service to stop. */
int pv_control_serve(int listen_fd, pv_engine *e, const pv_control_config *config, int wait_ms);

/* The request handling without sockets (unit tests): fills the HTTP status and body. */
typedef struct {
    int status;
    char body[16384];
    int exit_requested;
} pv_response;

void pv_control_handle(pv_engine *e, const pv_control_config *config, const char *request, size_t len, pv_response *out);

#endif
