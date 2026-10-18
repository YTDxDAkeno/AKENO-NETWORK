#include "input.h"

#include <ctype.h>
#include <string.h>

static const char *const BUTTON_NAMES[AKN_BTN_COUNT] = {
    "up", "down", "left", "right", "cross", "circle", "square", "triangle",
    "l1", "r1", "l2", "r2", "l3", "r3", "options", "touchpad",
};

const char *akn_button_name(akn_button b) {
    return (b >= 0 && b < AKN_BTN_COUNT) ? BUTTON_NAMES[b] : "unknown";
}

akn_button akn_button_from_name(const char *name) {
    if (!name) return AKN_BTN_COUNT;
    for (int i = 0; i < AKN_BTN_COUNT; i++) {
        const char *a = BUTTON_NAMES[i], *b = name;
        while (*a && *b && tolower((unsigned char)*b) == *a) a++, b++;
        if (!*a && !*b) return (akn_button)i;
    }
    return AKN_BTN_COUNT;
}

void akn_queue_init(akn_input_queue *q) { memset(q, 0, sizeof(*q)); }

static void push(akn_input *in, akn_input_queue *q, akn_button b, akn_event_kind kind, akn_source src) {
    in->total_events++;
    if (q->count >= AKN_INPUT_QUEUE_LEN) {
        q->dropped++;
        return;
    }
    int tail = (q->head + q->count) % AKN_INPUT_QUEUE_LEN;
    q->ev[tail].button = b;
    q->ev[tail].kind = kind;
    q->ev[tail].source = src;
    q->count++;
}

int akn_queue_pop(akn_input_queue *q, akn_input_event *out) {
    if (q->count == 0) return 0;
    *out = q->ev[q->head];
    q->head = (q->head + 1) % AKN_INPUT_QUEUE_LEN;
    q->count--;
    return 1;
}

void akn_input_init(akn_input *in) {
    memset(in, 0, sizeof(*in));
    in->stick_dir = AKN_BTN_COUNT;
}

static int is_direction(akn_button b) { return b <= AKN_BTN_RIGHT; }

void akn_input_button(akn_input *in, akn_input_queue *q, akn_button b, akn_source src, int down, uint64_t now_ms) {
    if (b < 0 || b >= AKN_BTN_COUNT || src < 0 || src >= AKN_SRC_COUNT) return;
    uint32_t bit = 1u << b;
    int was_src = (in->src_down[src] & bit) != 0;
    if (!!down == was_src) return;
    int was_any = (in->down & bit) != 0;
    if (down) in->src_down[src] |= bit;
    else in->src_down[src] &= ~bit;
    uint32_t any = 0;
    for (int s = 0; s < AKN_SRC_COUNT; s++) any |= in->src_down[s];
    in->down = any;
    int is_any = (any & bit) != 0;
    if (is_any && !was_any) {
        in->presses[b]++;
        in->repeat_at[b] = now_ms + AKN_REPEAT_DELAY_MS;
        push(in, q, b, AKN_EV_PRESS, src);
    } else if (!is_any && was_any) {
        push(in, q, b, AKN_EV_RELEASE, src);
    }
}

void akn_input_axis(akn_input *in, akn_input_queue *q, int axis, int value, uint64_t now_ms) {
    if (axis < 0 || axis >= AKN_AXIS_COUNT) return;
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    in->axes[axis] = (int16_t)value;
    if (axis != AKN_AXIS_LX && axis != AKN_AXIS_LY) return;

    int x = in->axes[AKN_AXIS_LX], y = in->axes[AKN_AXIS_LY];
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    akn_button want = in->stick_dir;
    if (want != AKN_BTN_COUNT) {
        /* Keep the held direction until its axis falls below the exit threshold. */
        int held = (want == AKN_BTN_LEFT || want == AKN_BTN_RIGHT) ? ax : ay;
        int sign_ok = (want == AKN_BTN_LEFT && x < 0) || (want == AKN_BTN_RIGHT && x > 0) ||
                      (want == AKN_BTN_UP && y < 0) || (want == AKN_BTN_DOWN && y > 0);
        if (held < AKN_STICK_EXIT || !sign_ok) want = AKN_BTN_COUNT;
    }
    if (want == AKN_BTN_COUNT) {
        if (ax >= ay && ax >= AKN_STICK_ENTER) want = x < 0 ? AKN_BTN_LEFT : AKN_BTN_RIGHT;
        else if (ay > ax && ay >= AKN_STICK_ENTER) want = y < 0 ? AKN_BTN_UP : AKN_BTN_DOWN;
    }
    if (want == in->stick_dir) return;
    if (in->stick_dir != AKN_BTN_COUNT) akn_input_button(in, q, in->stick_dir, AKN_SRC_STICK, 0, now_ms);
    in->stick_dir = want;
    if (want != AKN_BTN_COUNT) akn_input_button(in, q, want, AKN_SRC_STICK, 1, now_ms);
}

void akn_input_tick(akn_input *in, akn_input_queue *q, uint64_t now_ms) {
    for (int b = 0; b < AKN_BTN_COUNT; b++) {
        if (!is_direction((akn_button)b) || !(in->down & (1u << b))) continue;
        if (now_ms >= in->repeat_at[b]) {
            akn_source src = AKN_SRC_PAD;
            for (int s = 0; s < AKN_SRC_COUNT; s++)
                if (in->src_down[s] & (1u << b)) {
                    src = (akn_source)s;
                    break;
                }
            push(in, q, (akn_button)b, AKN_EV_REPEAT, src);
            /* Catch up without bursts after a stall (e.g. a slow frame). */
            in->repeat_at[b] = now_ms + AKN_REPEAT_INTERVAL_MS;
        }
    }
}

void akn_input_release_source(akn_input *in, akn_input_queue *q, akn_source src, uint64_t now_ms) {
    if (src < 0 || src >= AKN_SRC_COUNT) return;
    for (int b = 0; b < AKN_BTN_COUNT; b++)
        if (in->src_down[src] & (1u << b)) akn_input_button(in, q, (akn_button)b, src, 0, now_ms);
    if (src == AKN_SRC_PAD) {
        /* A disconnected controller also stops its stick. */
        for (int a = 0; a < AKN_AXIS_COUNT; a++) in->axes[a] = 0;
        if (in->stick_dir != AKN_BTN_COUNT) akn_input_button(in, q, in->stick_dir, AKN_SRC_STICK, 0, now_ms);
        in->stick_dir = AKN_BTN_COUNT;
    }
}

int akn_input_is_down(const akn_input *in, akn_button b) {
    return b >= 0 && b < AKN_BTN_COUNT && (in->down & (1u << b)) != 0;
}
