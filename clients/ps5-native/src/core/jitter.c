#include "jitter.h"

#include <string.h>

void akn_jitter_init(akn_jitter *j) { memset(j, 0, sizeof(*j)); }

/* Signed distance a - b on the 16-bit sequence circle. */
static int seq_diff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(a - b); }

int akn_jitter_depth(const akn_jitter *j) {
    int n = 0;
    for (int i = 0; i < AKN_JITTER_SLOTS; i++) n += j->slots[i].used;
    return n;
}

/* The oldest stored sequence number, or -1. */
static int oldest(const akn_jitter *j) {
    int best = -1;
    for (int i = 0; i < AKN_JITTER_SLOTS; i++) {
        if (!j->slots[i].used) continue;
        if (best < 0 || seq_diff(j->slots[i].seq, (uint16_t)best) < 0) best = j->slots[i].seq;
    }
    return best;
}

static void drop_oldest(akn_jitter *j) {
    int o = oldest(j);
    if (o < 0) return;
    j->slots[(uint16_t)o % AKN_JITTER_SLOTS].used = 0;
    j->dropped_overflow++;
    if (j->playing && seq_diff((uint16_t)o, j->next) >= 0) j->next = (uint16_t)(o + 1);
}

int akn_jitter_put(akn_jitter *j, uint16_t seq, const uint8_t *data, size_t len) {
    if (!data || len == 0 || len > AKN_VOICE_MAX_PACKET) return 1;
    if (j->playing && seq_diff(seq, j->next) < 0) {
        j->late++;
        return 1;
    }
    if (j->playing && seq_diff(seq, j->next) >= AKN_JITTER_SLOTS) {
        /* far ahead (the sender restarted or a long stall): start over */
        memset(j->slots, 0, sizeof(j->slots));
        j->playing = 0;
    }
    akn_jitter_slot *s = &j->slots[seq % AKN_JITTER_SLOTS];
    if (s->used && s->seq == seq) {
        j->duplicates++;
        return 1;
    }
    if (s->used) { /* an older packet in the slot that was never played: it is stale */
        j->dropped_overflow++;
    }
    s->used = 1;
    s->seq = seq;
    s->len = (uint16_t)len;
    memcpy(s->data, data, len);
    j->received++;
    while (akn_jitter_depth(j) > AKN_JITTER_MAX_DEPTH) drop_oldest(j);
    return 0;
}

akn_jitter_result akn_jitter_get(akn_jitter *j, uint8_t *out, size_t *len) {
    if (!j->playing) {
        if (akn_jitter_depth(j) < AKN_JITTER_PREFILL) return AKN_JITTER_EMPTY;
        j->playing = 1;
        j->next = (uint16_t)oldest(j);
        j->concealed_run = 0;
    }
    akn_jitter_slot *s = &j->slots[j->next % AKN_JITTER_SLOTS];
    if (s->used && s->seq == j->next) {
        memcpy(out, s->data, s->len);
        *len = s->len;
        s->used = 0;
        j->next++;
        j->played++;
        j->concealed_run = 0;
        return AKN_JITTER_PACKET;
    }
    if (akn_jitter_depth(j) == 0 || j->concealed_run >= AKN_JITTER_MAX_CONCEAL) {
        /* the stream stopped (muted sender, network stall): rebuffer */
        j->playing = 0;
        j->underruns++;
        return AKN_JITTER_EMPTY;
    }
    j->next++;
    j->lost++;
    j->concealed_run++;
    *len = 0;
    return AKN_JITTER_LOST;
}
