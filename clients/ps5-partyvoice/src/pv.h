/*
 * AKENO Party Voice (EXPERIMENTAL, docs/PS5_PARTY_VOICE.md): a background payload that keeps the
 * console in its party's voice chat while a game runs. It is a separate process from the game,
 * ShellUI, the onionHEN plugin and the AKENO daemon:
 *
 *   AKEN00001 (onionHEN plugin) --- 127.0.0.1:8646, the daemon's native key ---> akeno-party-voice
 *   akeno-party-voice --- voice pass (POST /parties/:id/voice/device-token) ---> AKENO daemon (IPC 3)
 *   akeno-party-voice --- wss://<AKENO server>/voice-gateway, Opus both ways ---> voice gateway -> LiveKit
 *
 * It reuses AKENO Native's voice core unchanged (Opus codec, 16 -> 48 kHz, jitter buffer, gateway
 * protocol, reconnect policy) and the daemon's TLS/WebSocket client. Nothing is recorded or
 * stored; logs and the report hold states, codes and counts only.
 */
#ifndef PV_H
#define PV_H

#define PV_APP "akeno-party-voice"
#define PV_VERSION "0.1.0-alpha"
#define PV_API_PROTOCOL 1   /* the local control interface (pv_control.c) */
#define PV_PORT 8646        /* 127.0.0.1 only; the daemon is 8642, the Voice Lab 8644 */

#ifndef PV_BUILD_ID
#define PV_BUILD_ID "dev"
#endif

/* Bounded user settings. */
#define PV_VOLUME_MIN 0
#define PV_VOLUME_MAX 150
#define PV_GAIN_MIN 50
#define PV_GAIN_MAX 150
#define PV_STEP 10
#define PV_ROUTES 4

/* Reconnects after a lost connection: 1, 2, 4, 8, then every 10 s, for about five minutes. */
#define PV_RECONNECT_MAX 30

#endif
