/*
 * Controller shortcut detection (portable; unit-tested on Linux). Decides from a stream of
 * controller button samples when the user has deliberately held a button combination, so that
 * AKENO can open its page. Nothing here reads a controller: the samples come from the platform
 * layer (akeno_platform_pad_read) or from the tests.
 *
 * Button bits are those of the PS5 joystick driver of ps5-payload-dev/SDL
 * (src/joystick/ps5/SDL_ps5joystick.h, commit ee4c47d).
 */
#ifndef AKENO_APP_SHORTCUT_H
#define AKENO_APP_SHORTCUT_H

#include <stddef.h>

#define AKENO_PAD_L3 0x0002u
#define AKENO_PAD_R3 0x0004u
#define AKENO_PAD_OPTIONS 0x0008u
#define AKENO_PAD_UP 0x0010u
#define AKENO_PAD_RIGHT 0x0020u
#define AKENO_PAD_DOWN 0x0040u
#define AKENO_PAD_LEFT 0x0080u
#define AKENO_PAD_L2 0x0100u
#define AKENO_PAD_R2 0x0200u
#define AKENO_PAD_L1 0x0400u
#define AKENO_PAD_R1 0x0800u
#define AKENO_PAD_TRIANGLE 0x1000u
#define AKENO_PAD_CIRCLE 0x2000u
#define AKENO_PAD_CROSS 0x4000u
#define AKENO_PAD_SQUARE 0x8000u
#define AKENO_PAD_TOUCH_PAD 0x100000u
/* Every bit above; anything else in a sample is ignored for detection (counted in diagnostics). */
#define AKENO_PAD_KNOWN_BUTTONS 0x10FFFEu

#define AKENO_SHORTCUT_HOLD_MIN_MS 1000
#define AKENO_SHORTCUT_HOLD_MAX_MS 2000
#define AKENO_SHORTCUT_COOLDOWN_MS 5000
/* A gap between samples longer than this (read errors, a stalled thread) restarts the hold. */
#define AKENO_SHORTCUT_MAX_GAP_MS 500

typedef struct {
    const char *id;    /* Settings value, e.g. "l1r1options". */
    const char *label; /* Shown on the page. */
    unsigned mask;     /* Every button that must be held. */
} akeno_shortcut_combo;

/* The selectable combinations (all chords of at least two buttons). */
const akeno_shortcut_combo *akeno_shortcut_combos(size_t *count);
/* The combination with this id, or NULL. */
const akeno_shortcut_combo *akeno_shortcut_combo_find(const char *id);
/* Default combination (L1 + R1 + OPTIONS: OPTIONS pauses most games before AKENO opens). */
const akeno_shortcut_combo *akeno_shortcut_default_combo(void);
/* 1 when hold_ms is one of the offered hold times (1000, 1500, 2000). */
int akeno_shortcut_hold_valid(int hold_ms);

typedef struct {
    unsigned mask;
    int hold_ms;
    int cooldown_ms;
    int holding;           /* The combination is fully held since held_since. */
    long long held_since;
    int sampled;           /* At least one sample was fed (last_sample is valid). */
    long long last_sample;
    int fired;             /* Fired at least once (last_fire is valid). */
    long long last_fire;
    int latched;           /* Fired (or suppressed); waiting until every combination button is released. */
} akeno_shortcut;

void akeno_shortcut_init(akeno_shortcut *s, unsigned mask, int hold_ms, int cooldown_ms);

/*
 * Feeds one button sample taken at now_ms (monotonic). Returns 1 exactly once per deliberate
 * hold: all combination buttons pressed together for hold_ms. After that nothing fires again
 * until every combination button has been released, and never within cooldown_ms of the last
 * activation. Extra buttons pressed at the same time do not matter.
 */
int akeno_shortcut_feed(akeno_shortcut *s, unsigned buttons, long long now_ms);

/* Forgets a partial hold (used after read errors). The latch and cooldown are kept. */
void akeno_shortcut_reset_hold(akeno_shortcut *s);

#endif
