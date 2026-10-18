/*
 * The AKENO voice gateway protocol (version 1), spoken over a TLS WebSocket
 * (docs/PS5_VOICE.md#gateway-protocol):
 *
 *   text   client -> gateway   {"type":"hello","protocol":1,"token":"<voice token>","client":"...",
 *                               "muted":b,"deafened":b}   (first message, within 5 s)
 *                              {"type":"mute","muted":b} {"type":"deafen","deafened":b}
 *                              {"type":"ping","id":n} {"type":"leave"}
 *   text   gateway -> client   {"type":"ready",...} {"type":"participants","list":[...]}
 *                              {"type":"state","state":"connected|reconnecting"}
 *                              {"type":"error","code":"...","message":"..."} {"type":"pong","id":n}
 *   binary both directions     0x01, sequence number (uint16, big endian), one 20 ms Opus packet
 *
 * Pure functions: building, parsing and validating messages. Unit-tested.
 */
#ifndef AKN_VOICE_PROTO_H
#define AKN_VOICE_PROTO_H

#include <stddef.h>
#include <stdint.h>

#include "voice_codec.h"

#define AKN_VOICE_PROTOCOL 1
#define AKN_VOICE_AUDIO_TAG 0x01
#define AKN_VOICE_MAX_PARTICIPANTS 16
#define AKN_VOICE_TOKEN_MAX 2048

/* Close codes the gateway uses (4000-4999 are application codes). */
#define AKN_VOICE_CLOSE_AUTH 4001      /* token refused or expired */
#define AKN_VOICE_CLOSE_REMOVED 4003   /* removed from the room (left the party, blocked, revoked) */
#define AKN_VOICE_CLOSE_REPLACED 4004  /* the same account joined voice elsewhere */
#define AKN_VOICE_CLOSE_ENDED 4005     /* the room was closed */
#define AKN_VOICE_CLOSE_TIMEOUT 4008   /* no hello / idle */

typedef struct {
    char id[64];
    char name[96];
    int muted;
    int speaking;
    int level;      /* 0..100 */
    int self;
} akn_voice_participant;

typedef enum {
    AKN_VMSG_UNKNOWN = 0,
    AKN_VMSG_READY,
    AKN_VMSG_PARTICIPANTS,
    AKN_VMSG_STATE,
    AKN_VMSG_ERROR,
    AKN_VMSG_PONG
} akn_voice_msg_type;

typedef struct {
    akn_voice_msg_type type;
    /* READY */
    int protocol, sample_rate, frame_ms, channels;
    char room[96], identity[64];
    /* PARTICIPANTS */
    akn_voice_participant list[AKN_VOICE_MAX_PARTICIPANTS];
    int count;
    /* STATE */
    int reconnecting;
    /* ERROR */
    char code[48], message[200];
    /* PONG */
    long long id;
} akn_voice_msg;

/* Builds client messages; return the length, or -1 when out is too small / input invalid. */
int akn_voice_hello_json(const char *token, const char *client, int muted, int deafened, char *out, size_t len);
int akn_voice_flag_json(const char *type, const char *key, int value, char *out, size_t len);
int akn_voice_ping_json(long long id, char *out, size_t len);

/* Parses a gateway text message. Returns 0 on success (msg->type may still be UNKNOWN). */
int akn_voice_parse(const char *text, size_t len, akn_voice_msg *msg);
/* READY must announce what this client speaks (protocol 1, 48 kHz, mono, 20 ms). */
int akn_voice_ready_ok(const akn_voice_msg *msg);

/* Audio frames: tag + seq + Opus. pack returns the frame length or -1. */
int akn_voice_pack_audio(uint16_t seq, const uint8_t *opus, size_t len, uint8_t *out, size_t out_len);
/* unpack: 0 on success with *opus pointing into frame. */
int akn_voice_unpack_audio(const uint8_t *frame, size_t len, uint16_t *seq, const uint8_t **opus, size_t *opus_len);

/*
 * The voice credential from the AKENO server (through the daemon):
 * {"url":"wss://...","token":"...","room":"...","identity":"...","expiresAt":"..."} (optionally
 * wrapped in {"data":...}). Returns 0 when it is complete and the URL is a wss:// URL.
 */
typedef struct {
    char url[256];
    char token[AKN_VOICE_TOKEN_MAX];
    char room[96];
    char identity[64];
} akn_voice_credential;

int akn_voice_parse_credential(const char *json, size_t len, akn_voice_credential *out);
/* The gateway must be on the AKENO server's own host (the token is never sent elsewhere). */
int akn_voice_same_host(const char *gateway_url, const char *server_url);

/* A friendly line for a close code or error code from the gateway. */
const char *akn_voice_close_text(int close_code);
const char *akn_voice_error_text(const char *code);

#endif
