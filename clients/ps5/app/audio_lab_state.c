/* Audio lab v3: read-only port-state measurement; see audio_lab_state.h. */
#include "audio_lab_state.h"

#include <stdio.h>
#include <string.h>

void audio_lab_state_prepare(unsigned char *arena, unsigned char fill) {
    memset(arena, AUDIO_LAB_STATE_CANARY, AUDIO_LAB_STATE_ARENA_BYTES);
    memset(arena + AUDIO_LAB_STATE_LEAD_BYTES, fill, AUDIO_LAB_STATE_WINDOW_BYTES);
}

unsigned char *audio_lab_state_window(unsigned char *arena) { return arena + AUDIO_LAB_STATE_LEAD_BYTES; }

static int all_equal(const unsigned char *p, size_t n, unsigned char v) {
    for (size_t i = 0; i < n; i++)
        if (p[i] != v) return 0;
    return 1;
}

void audio_lab_state_capture(const unsigned char *arena, int32_t rc, unsigned char fill, audio_lab_state_call *out) {
    const size_t tail = AUDIO_LAB_STATE_LEAD_BYTES + AUDIO_LAB_STATE_WINDOW_BYTES;
    out->called = 1;
    out->rc = rc;
    out->fill = fill;
    out->lead_intact = all_equal(arena, AUDIO_LAB_STATE_LEAD_BYTES, AUDIO_LAB_STATE_CANARY);
    out->tail_intact = all_equal(arena + tail, AUDIO_LAB_STATE_ARENA_BYTES - tail, AUDIO_LAB_STATE_CANARY);
    memcpy(out->bytes, arena + AUDIO_LAB_STATE_LEAD_BYTES, AUDIO_LAB_STATE_WINDOW_BYTES);
}

void audio_lab_state_combine(audio_lab_state_sample *s) {
    s->written_count = 0;
    s->highest_written = -1;
    s->guards_intact = 1;
    s->calls_agree = 1;
    memset(s->written, 0, sizeof(s->written));
    memset(s->value, 0, sizeof(s->value));
    for (int c = 0; c < 2; c++) {
        const audio_lab_state_call *k = &s->call[c];
        if (!k->called) continue;
        if (!k->lead_intact || !k->tail_intact) s->guards_intact = 0;
    }
    for (size_t i = 0; i < AUDIO_LAB_STATE_WINDOW_BYTES; i++) {
        int by[2] = { 0, 0 };
        for (int c = 0; c < 2; c++) {
            const audio_lab_state_call *k = &s->call[c];
            by[c] = k->called && k->bytes[i] != k->fill;
        }
        if (!by[0] && !by[1]) continue;
        /* A byte the first call wrote shows its value there; one only the second call changed was
         * written with the first fill's value (or changed between the calls). */
        unsigned char v = by[0] ? s->call[0].bytes[i] : s->call[1].bytes[i];
        if (by[0] && s->call[1].called && s->call[1].bytes[i] != v) s->calls_agree = 0;
        if (!by[0] && s->call[0].called && v != s->call[0].fill) s->calls_agree = 0; /* the first call left it alone */
        s->written[i] = 1;
        s->value[i] = v;
        s->written_count++;
        s->highest_written = (int)i;
    }
}

int audio_lab_state_measure(audio_lab_port_state_fn fn, int32_t handle, unsigned char *arena, audio_lab_state_sample *s) {
    static const unsigned char fills[2] = { AUDIO_LAB_STATE_FILL_A, AUDIO_LAB_STATE_FILL_B };
    memset(s, 0, sizeof(*s));
    s->attempted = 1;
    int stop = 0;
    for (int c = 0; c < 2; c++) {
        audio_lab_state_prepare(arena, fills[c]);
        int32_t rc = fn(handle, audio_lab_state_window(arena));
        audio_lab_state_capture(arena, rc, fills[c], &s->call[c]);
        if (!s->call[c].lead_intact || !s->call[c].tail_intact) {
            stop = 1;
            break;
        }
        if (rc < 0) break;
    }
    audio_lab_state_combine(s);
    return stop ? -1 : 0;
}

static int field_known(const audio_lab_state_sample *s, size_t at, size_t n) {
    for (size_t i = at; i < at + n; i++)
        if (!s->written[i]) return 0;
    return 1;
}

static unsigned long long field_le(const audio_lab_state_sample *s, size_t at, size_t n) {
    unsigned long long v = 0;
    for (size_t i = 0; i < n; i++) v |= (unsigned long long)s->value[at + i] << (8 * i);
    return v;
}

void audio_lab_state_decode(const audio_lab_state_sample *s, audio_lab_state_decoded *d) {
    memset(d, 0, sizeof(*d));
    if (!s->call[0].called || s->call[0].rc < 0) return;
    d->output_known = field_known(s, 0, 2);
    d->channel_known = field_known(s, 2, 1);
    d->volume_known = field_known(s, 4, 2);
    d->reroute_known = field_known(s, 6, 2);
    d->flag_known = field_known(s, 8, 8);
    d->output = (unsigned)field_le(s, 0, 2);
    d->channel = (unsigned)field_le(s, 2, 1);
    d->volume = (int)(int16_t)(uint16_t)field_le(s, 4, 2);
    d->reroute_counter = (unsigned)field_le(s, 6, 2);
    d->flag = field_le(s, 8, 8);
}

void audio_lab_state_output_names(unsigned output, char *out, size_t out_len) {
    static const struct { unsigned bit; const char *name; } k_bits[] = {
        { 0x01, "primary" }, { 0x02, "secondary" }, { 0x04, "tertiary" }, { 0x40, "headphone" }, { 0x80, "external" },
    };
    size_t used = 0;
    out[0] = 0;
    unsigned rest = output;
    for (size_t i = 0; i < sizeof(k_bits) / sizeof(k_bits[0]); i++) {
        if (!(output & k_bits[i].bit)) continue;
        rest &= ~k_bits[i].bit;
        int n = snprintf(out + used, out_len - used, "%s%s", used ? ", " : "", k_bits[i].name);
        if (n < 0 || (size_t)n >= out_len - used) return;
        used += (size_t)n;
    }
    if (rest) {
        int n = snprintf(out + used, out_len - used, "%sunknown bits 0x%x", used ? ", " : "", rest);
        if (n > 0 && (size_t)n < out_len - used) used += (size_t)n;
    }
    if (!used) snprintf(out, out_len, "none");
}

size_t audio_lab_state_shown_bytes(const audio_lab_state_sample *s) {
    if (!s->guards_intact) return AUDIO_LAB_STATE_WINDOW_BYTES;
    size_t n = AUDIO_LAB_STATE_LAYOUT_BYTES;
    if (s->highest_written >= 0) {
        size_t need = ((size_t)s->highest_written + 16u) / 16u * 16u;
        if (need > n) n = need;
    }
    return n > AUDIO_LAB_STATE_WINDOW_BYTES ? AUDIO_LAB_STATE_WINDOW_BYTES : n;
}

size_t audio_lab_state_hex(const unsigned char *bytes, size_t n, char *out, size_t out_len) {
    static const char digits[] = "0123456789abcdef";
    size_t used = 0;
    if (!out_len) return 0;
    for (size_t i = 0; i < n && used + 3 < out_len; i++) {
        if (i) out[used++] = ' ';
        out[used++] = digits[bytes[i] >> 4];
        out[used++] = digits[bytes[i] & 15];
    }
    out[used] = 0;
    return used;
}

size_t audio_lab_state_values_hex(const audio_lab_state_sample *s, size_t n, char *out, size_t out_len) {
    static const char digits[] = "0123456789abcdef";
    size_t used = 0;
    if (!out_len) return 0;
    if (n > AUDIO_LAB_STATE_WINDOW_BYTES) n = AUDIO_LAB_STATE_WINDOW_BYTES;
    for (size_t i = 0; i < n && used + 3 < out_len; i++) {
        if (i) out[used++] = ' ';
        out[used++] = s->written[i] ? digits[s->value[i] >> 4] : '-';
        out[used++] = s->written[i] ? digits[s->value[i] & 15] : '-';
    }
    out[used] = 0;
    return used;
}
