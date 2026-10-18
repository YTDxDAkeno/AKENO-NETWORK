/*
 * Receive-side jitter buffer for the 20 ms Opus packets of the voice gateway. Packets carry a
 * 16-bit sequence number. The buffer reorders, drops duplicates and late packets, starts playback
 * after a short prefill, reports a gap as one lost frame (for concealment) and, when packets pile
 * up (the sender's clock runs faster than the console's audio clock, or after a stall), drops
 * the oldest so the delay stays bounded.
 */
#ifndef AKN_JITTER_H
#define AKN_JITTER_H

#include <stddef.h>
#include <stdint.h>

#include "voice_codec.h"

#define AKN_JITTER_SLOTS 32        /* 640 ms */
#define AKN_JITTER_PREFILL 3       /* 60 ms before playback starts */
#define AKN_JITTER_MAX_DEPTH 10    /* 200 ms; beyond it the oldest packets are dropped */
#define AKN_JITTER_MAX_CONCEAL 5   /* lost frames concealed in a row before going silent */

typedef struct {
    int used;
    uint16_t seq;
    uint16_t len;
    uint8_t data[AKN_VOICE_MAX_PACKET];
} akn_jitter_slot;

typedef enum {
    AKN_JITTER_EMPTY = 0,   /* nothing to play: silence (buffering) */
    AKN_JITTER_PACKET,      /* a packet: decode it */
    AKN_JITTER_LOST         /* the next packet is missing: conceal one frame */
} akn_jitter_result;

typedef struct {
    akn_jitter_slot slots[AKN_JITTER_SLOTS];
    int playing;            /* prefill done */
    uint16_t next;          /* sequence number expected next while playing */
    int concealed_run;
    /* statistics */
    unsigned long received, duplicates, late, dropped_overflow, lost, played, underruns;
} akn_jitter;

void akn_jitter_init(akn_jitter *j);
/* Adds a packet (copied). Returns 0 when stored, 1 when ignored (duplicate, late, too large). */
int akn_jitter_put(akn_jitter *j, uint16_t seq, const uint8_t *data, size_t len);
/* Packets waiting. */
int akn_jitter_depth(const akn_jitter *j);
/* The next frame to play: copies a packet into out (AKN_VOICE_MAX_PACKET bytes) and sets *len. */
akn_jitter_result akn_jitter_get(akn_jitter *j, uint8_t *out, size_t *len);

#endif
