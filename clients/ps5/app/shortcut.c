/* Controller shortcut detection; see shortcut.h. */
#include "shortcut.h"

#include <string.h>

static const akeno_shortcut_combo k_combos[] = {
    { "l1r1options", "L1 + R1 + OPTIONS", AKENO_PAD_L1 | AKENO_PAD_R1 | AKENO_PAD_OPTIONS },
    { "l3r3", "L3 + R3 (press both sticks)", AKENO_PAD_L3 | AKENO_PAD_R3 },
    { "touchoptions", "Touch pad button + OPTIONS", AKENO_PAD_TOUCH_PAD | AKENO_PAD_OPTIONS },
};

const akeno_shortcut_combo *akeno_shortcut_combos(size_t *count) {
    if (count) *count = sizeof(k_combos) / sizeof(k_combos[0]);
    return k_combos;
}

const akeno_shortcut_combo *akeno_shortcut_combo_find(const char *id) {
    if (!id) return NULL;
    for (size_t i = 0; i < sizeof(k_combos) / sizeof(k_combos[0]); i++)
        if (strcmp(k_combos[i].id, id) == 0) return &k_combos[i];
    return NULL;
}

const akeno_shortcut_combo *akeno_shortcut_default_combo(void) { return &k_combos[0]; }

int akeno_shortcut_hold_valid(int hold_ms) { return hold_ms == 1000 || hold_ms == 1500 || hold_ms == 2000; }

void akeno_shortcut_init(akeno_shortcut *s, unsigned mask, int hold_ms, int cooldown_ms) {
    memset(s, 0, sizeof(*s));
    s->mask = mask & AKENO_PAD_KNOWN_BUTTONS;
    s->hold_ms = hold_ms < AKENO_SHORTCUT_HOLD_MIN_MS ? AKENO_SHORTCUT_HOLD_MIN_MS
                 : hold_ms > AKENO_SHORTCUT_HOLD_MAX_MS ? AKENO_SHORTCUT_HOLD_MAX_MS
                                                        : hold_ms;
    s->cooldown_ms = cooldown_ms < 0 ? 0 : cooldown_ms;
}

void akeno_shortcut_reset_hold(akeno_shortcut *s) { s->holding = 0; }

int akeno_shortcut_feed(akeno_shortcut *s, unsigned buttons, long long now_ms) {
    /* A combination needs at least two buttons; a single button would fire during normal play. */
    if (s->mask == 0 || (s->mask & (s->mask - 1)) == 0) return 0;
    if (s->sampled && (now_ms < s->last_sample || now_ms - s->last_sample > AKENO_SHORTCUT_MAX_GAP_MS)) s->holding = 0;
    s->sampled = 1;
    s->last_sample = now_ms;
    unsigned pressed = buttons & s->mask;
    if (pressed != s->mask) {
        s->holding = 0;
        if (pressed == 0) s->latched = 0; /* Re-armed only after a full release. */
        return 0;
    }
    if (s->latched) return 0;
    if (!s->holding) {
        s->holding = 1;
        s->held_since = now_ms;
        return 0;
    }
    if (now_ms - s->held_since < s->hold_ms) return 0;
    s->latched = 1;
    s->holding = 0;
    if (s->fired && now_ms - s->last_fire < s->cooldown_ms) return 0; /* Too soon: swallow this hold. */
    s->fired = 1;
    s->last_fire = now_ms;
    return 1;
}
