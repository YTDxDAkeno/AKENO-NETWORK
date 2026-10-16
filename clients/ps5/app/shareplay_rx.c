/* SharePlay Player 2 diagnostic receiver. See shareplay_rx.h. */
#include "shareplay_rx.h"

#include <string.h>

static const char VIRTUAL_PAD_REASON[] =
    "Unsupported: no verified, safe way exists for a PS5 payload to create a second controller. Player 2 input is shown here for diagnostics only and controls nothing.";
static const char NATIVE_CAPTURE_REASON[] =
    "Unsupported: no verified way exists for a payload to capture the video of a running game. Stream with a capture card or the PS Remote Play window on a PC instead.";

int akeno_sp_virtual_pad_open(const char **reason) {
    if (reason) *reason = VIRTUAL_PAD_REASON;
    return -1;
}

int akeno_sp_native_capture_open(const char **reason) {
    if (reason) *reason = NATIVE_CAPTURE_REASON;
    return -1;
}

void akeno_sp_rx_init(akeno_sp_rx *rx) {
    memset(rx, 0, sizeof(*rx));
}

static void copy_id(char *dst, const char *src) {
    size_t n = strlen(src);
    if (n >= AKENO_SP_ID_MAX) n = AKENO_SP_ID_MAX - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static int get_int(const akeno_json *object, const char *key, int64_t min, int64_t max, int64_t *out) {
    const akeno_json *node = akeno_json_get(object, key);
    if (!node || akeno_json_int64(node, out) != 0) return -1;
    return *out < min || *out > max ? -1 : 0;
}

/* Identifiers are UUIDs: only hex digits and dashes, at most 36 characters. */
static int valid_id(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > 36) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-')) return 0;
    }
    return 1;
}

static int read_array(const akeno_json *object, const char *key, size_t count, int64_t min, int64_t max, int32_t *out) {
    const akeno_json *array = akeno_json_get(object, key);
    if (!array || array->type != AKENO_JSON_ARRAY || array->count != count) return -1;
    for (size_t i = 0; i < count; i++) {
        int64_t value;
        if (akeno_json_int64(akeno_json_at(array, i), &value) != 0 || value < min || value > max) return -1;
        out[i] = (int32_t)value;
    }
    return 0;
}

static void count_rate(akeno_sp_rx *rx, long long now_ms) {
    if (now_ms - rx->window_start >= 1000) {
        rx->rate = now_ms - rx->window_start < 2000 ? rx->window_count : 0;
        rx->window_start = now_ms;
        rx->window_count = 0;
    }
    rx->window_count++;
}

akeno_sp_result akeno_sp_rx_apply(akeno_sp_rx *rx, const akeno_json *data, long long now_ms) {
    if (!data || data->type != AKENO_JSON_OBJECT) goto invalid;
    const char *kind = akeno_json_get_string(data, "kind", "");
    int release = strcmp(kind, "release") == 0;
    if (!release && strcmp(kind, "state") != 0) goto invalid;
    const char *session = akeno_json_get_string(data, "sessionId", "");
    const char *grant = akeno_json_get_string(data, "grantId", "");
    const char *guest = akeno_json_get_string(data, "guestId", "");
    if (!valid_id(session) || !valid_id(grant) || !valid_id(guest)) goto invalid;
    int64_t epoch, seq, slot, buttons;
    if (get_int(data, "epoch", 0, 0xffffffffLL, &epoch) || get_int(data, "seq", 0, 0xffffffffLL, &seq) ||
        get_int(data, "slot", 0, 255, &slot) || get_int(data, "buttons", 0, 0xffffffffLL, &buttons))
        goto invalid;
    if (slot != AKENO_SP_SLOT_PLAYER_2) goto invalid;
    if ((uint64_t)buttons >= (1u << AKENO_SP_BUTTON_COUNT) || ((uint32_t)buttons & AKENO_SP_BLOCKED_BUTTONS)) goto invalid;
    akeno_sp_state state;
    memset(&state, 0, sizeof(state));
    state.buttons = (uint32_t)buttons;
    if (read_array(data, "axes", 4, -AKENO_SP_AXIS_MAX, AKENO_SP_AXIS_MAX, state.axes) ||
        read_array(data, "triggers", 2, 0, 255, state.triggers))
        goto invalid;

    /* A different grant (new approval) restarts the sequence numbers. */
    if (!rx->have_grant || strcmp(rx->grant_id, grant) != 0 || rx->epoch != (uint32_t)epoch) {
        copy_id(rx->grant_id, grant);
        copy_id(rx->session_id, session);
        copy_id(rx->guest_id, guest);
        rx->epoch = (uint32_t)epoch;
        rx->last_seq = 0;
        rx->have_grant = 1;
    }
    if (release) {
        /* Going to neutral is always safe: applied whatever its sequence number. */
        memset(&rx->state, 0, sizeof(rx->state));
        rx->releases++;
        rx->last_at = now_ms;
        rx->tripped = 0;
        const char *reason = akeno_json_get_string(data, "reason", "Player 2 released the controller.");
        size_t n = strlen(reason);
        if (n >= sizeof(rx->last_reason)) n = sizeof(rx->last_reason) - 1;
        memcpy(rx->last_reason, reason, n);
        rx->last_reason[n] = '\0';
        return AKENO_SP_APPLIED;
    }
    if (seq == 0 || (uint32_t)seq <= rx->last_seq) {
        rx->stale++;
        return AKENO_SP_STALE;
    }
    rx->last_seq = (uint32_t)seq;
    rx->state = state;
    rx->last_at = now_ms;
    if (rx->first_at == 0) rx->first_at = now_ms;
    rx->accepted++;
    rx->tripped = 0;
    rx->last_reason[0] = '\0';
    count_rate(rx, now_ms);
    return AKENO_SP_APPLIED;

invalid:
    rx->invalid++;
    return AKENO_SP_INVALID;
}

int akeno_sp_rx_current(akeno_sp_rx *rx, long long now_ms, akeno_sp_state *out) {
    int expired = rx->last_at == 0 || now_ms - rx->last_at > AKENO_SP_WATCHDOG_MS;
    if (expired) {
        memset(out, 0, sizeof(*out));
        int held = rx->state.buttons != 0 || rx->state.axes[0] || rx->state.axes[1] || rx->state.axes[2] || rx->state.axes[3] ||
                   rx->state.triggers[0] || rx->state.triggers[1];
        if (rx->last_at != 0 && !rx->tripped && held) {
            rx->tripped = 1;
            rx->watchdog_trips++;
        }
        return rx->last_at != 0;
    }
    *out = rx->state;
    return 0;
}

int akeno_sp_rx_write_json(akeno_sp_rx *rx, long long now_ms, akeno_buf *out) {
    akeno_sp_state current;
    int expired = akeno_sp_rx_current(rx, now_ms, &current);
    if (now_ms - rx->window_start >= 2000) rx->rate = 0;
    int rc = akeno_buf_append_str(out, "{\"receiver\":{\"grantId\":");
    rc |= rx->have_grant ? akeno_buf_append_json_string(out, rx->grant_id) : akeno_buf_append_str(out, "null");
    rc |= akeno_buf_append_str(out, ",\"guestId\":");
    rc |= rx->have_grant ? akeno_buf_append_json_string(out, rx->guest_id) : akeno_buf_append_str(out, "null");
    rc |= akeno_buf_printf(out,
                           ",\"epoch\":%u,\"lastSeq\":%u,\"buttons\":%u,\"axes\":[%d,%d,%d,%d],\"triggers\":[%d,%d],"
                           "\"neutralByWatchdog\":%s,\"ageMs\":%lld,\"accepted\":%llu,\"stale\":%llu,\"invalid\":%llu,"
                           "\"releases\":%llu,\"watchdogTrips\":%llu,\"packetsPerSecond\":%u,\"watchdogMs\":%d,\"lastReason\":",
                           rx->epoch, rx->last_seq, current.buttons, current.axes[0], current.axes[1], current.axes[2], current.axes[3],
                           current.triggers[0], current.triggers[1], expired ? "true" : "false",
                           rx->last_at ? now_ms - rx->last_at : -1LL, rx->accepted, rx->stale, rx->invalid, rx->releases,
                           rx->watchdog_trips, rx->rate, AKENO_SP_WATCHDOG_MS);
    rc |= akeno_buf_append_json_string(out, rx->last_reason);
    const char *pad_reason = NULL;
    const char *capture_reason = NULL;
    akeno_sp_virtual_pad_open(&pad_reason);
    akeno_sp_native_capture_open(&capture_reason);
    rc |= akeno_buf_append_str(out, "},\"capabilities\":{\"virtualController\":{\"supported\":false,\"reason\":");
    rc |= akeno_buf_append_json_string(out, pad_reason);
    rc |= akeno_buf_append_str(out, "},\"nativeCapture\":{\"supported\":false,\"reason\":");
    rc |= akeno_buf_append_json_string(out, capture_reason);
    rc |= akeno_buf_printf(out, "},\"protocolVersion\":%d}}", AKENO_SP_PROTOCOL_VERSION);
    return rc ? -1 : 0;
}
