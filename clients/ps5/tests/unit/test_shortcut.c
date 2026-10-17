/* Controller shortcut detection (app/shortcut.c): hold, debounce, release, cooldown, gaps. */
#include "shortcut.h"
#include "test.h"

#define COMBO (AKENO_PAD_L1 | AKENO_PAD_R1 | AKENO_PAD_OPTIONS)

/* Feeds `buttons` every 50 ms from `from` (inclusive) to `to` (exclusive); returns how often it fired. */
static int hold(akeno_shortcut *s, unsigned buttons, long long from, long long to) {
    int fired = 0;
    for (long long t = from; t < to; t += 50) fired += akeno_shortcut_feed(s, buttons, t);
    return fired;
}

static void test_combos(void) {
    size_t count = 0;
    const akeno_shortcut_combo *all = akeno_shortcut_combos(&count);
    CHECK(count == 3);
    for (size_t i = 0; i < count; i++) {
        unsigned m = all[i].mask;
        CHECK(m != 0 && (m & (m - 1)) != 0); /* at least two buttons */
        CHECK((m & ~AKENO_PAD_KNOWN_BUTTONS) == 0);
        CHECK(akeno_shortcut_combo_find(all[i].id) == &all[i]);
    }
    CHECK(akeno_shortcut_combo_find("ps") == NULL);
    CHECK(akeno_shortcut_combo_find(NULL) == NULL);
    CHECK(akeno_shortcut_combo_find("") == NULL);
    CHECK_STR(akeno_shortcut_default_combo()->id, "l1r1options");
    CHECK(akeno_shortcut_hold_valid(1000) && akeno_shortcut_hold_valid(1500) && akeno_shortcut_hold_valid(2000));
    CHECK(!akeno_shortcut_hold_valid(0) && !akeno_shortcut_hold_valid(500) && !akeno_shortcut_hold_valid(5000));
}

static void test_fires_once_after_hold(void) {
    akeno_shortcut s;
    akeno_shortcut_init(&s, COMBO, 1000, 5000);
    CHECK(hold(&s, COMBO, 0, 950) == 0);   /* not yet one second */
    CHECK(akeno_shortcut_feed(&s, COMBO, 1000) == 1);
    CHECK(hold(&s, COMBO, 1050, 4000) == 0); /* still held: never again */
    CHECK(hold(&s, 0, 4000, 4100) == 0);     /* released */
    CHECK(hold(&s, COMBO, 6000, 7050) == 1); /* a new deliberate hold after the cooldown */
}

static void test_short_presses_and_partial_chords(void) {
    akeno_shortcut s;
    akeno_shortcut_init(&s, COMBO, 1000, 5000);
    /* Tapping the combination repeatedly never adds up to a hold. */
    for (long long t = 0; t < 10000; t += 600) {
        CHECK(hold(&s, COMBO, t, t + 500) == 0);
        CHECK(hold(&s, 0, t + 500, t + 600) == 0);
    }
    /* Two of three buttons held for a long time (normal play) never fires. */
    CHECK(hold(&s, AKENO_PAD_L1 | AKENO_PAD_R1, 20000, 30000) == 0);
    CHECK(hold(&s, AKENO_PAD_OPTIONS | AKENO_PAD_CROSS, 30000, 40000) == 0);
    /* Extra buttons do not prevent a deliberate hold. */
    CHECK(hold(&s, COMBO | AKENO_PAD_CROSS | AKENO_PAD_L3, 40000, 41050) == 1);
}

static void test_release_needed_before_rearm(void) {
    akeno_shortcut s;
    akeno_shortcut_init(&s, COMBO, 1000, 0);
    CHECK(hold(&s, COMBO, 0, 1050) == 1);
    /* Letting go of one button and pressing it again is not a release of the combination. */
    CHECK(hold(&s, AKENO_PAD_L1 | AKENO_PAD_R1, 1050, 1200) == 0);
    CHECK(hold(&s, COMBO, 1200, 3000) == 0);
    CHECK(hold(&s, 0, 3000, 3050) == 0);
    CHECK(hold(&s, COMBO, 3050, 4100) == 1);
}

static void test_cooldown(void) {
    akeno_shortcut s;
    akeno_shortcut_init(&s, COMBO, 1000, 5000);
    CHECK(hold(&s, COMBO, 0, 1050) == 1);
    CHECK(hold(&s, 0, 1050, 1100) == 0);
    /* A second deliberate hold within the cooldown is swallowed, and needs a release again. */
    CHECK(hold(&s, COMBO, 1100, 3000) == 0);
    CHECK(hold(&s, COMBO, 3000, 8000) == 0);
    CHECK(hold(&s, 0, 8000, 8050) == 0);
    CHECK(hold(&s, COMBO, 8050, 9100) == 1);
}

static void test_gaps_restart_the_hold(void) {
    akeno_shortcut s;
    akeno_shortcut_init(&s, COMBO, 1000, 5000);
    CHECK(hold(&s, COMBO, 0, 600) == 0);
    /* Samples stop for a second (read errors): the earlier part of the hold does not count. */
    CHECK(akeno_shortcut_feed(&s, COMBO, 1700) == 0);
    CHECK(hold(&s, COMBO, 1750, 2650) == 0);
    CHECK(akeno_shortcut_feed(&s, COMBO, 2700) == 1);
    /* reset_hold forgets a partial hold but keeps the latch. */
    akeno_shortcut_init(&s, COMBO, 1000, 0);
    CHECK(hold(&s, COMBO, 0, 900) == 0);
    akeno_shortcut_reset_hold(&s);
    CHECK(hold(&s, COMBO, 900, 1850) == 0);
    CHECK(akeno_shortcut_feed(&s, COMBO, 1900) == 1);
    /* Time going backwards also restarts the hold. */
    akeno_shortcut_init(&s, COMBO, 1000, 0);
    CHECK(hold(&s, COMBO, 5000, 5900) == 0);
    CHECK(akeno_shortcut_feed(&s, COMBO, 100) == 0);
    CHECK(akeno_shortcut_feed(&s, COMBO, 150) == 0);
}

static void test_hold_time_and_invalid_masks(void) {
    akeno_shortcut s;
    akeno_shortcut_init(&s, COMBO, 2000, 0);
    CHECK(hold(&s, COMBO, 0, 2000) == 0);
    CHECK(akeno_shortcut_feed(&s, COMBO, 2000) == 1);
    /* Hold times are clamped to 1..2 s. */
    akeno_shortcut_init(&s, COMBO, 10, 0);
    CHECK(s.hold_ms == AKENO_SHORTCUT_HOLD_MIN_MS);
    akeno_shortcut_init(&s, COMBO, 99999, 0);
    CHECK(s.hold_ms == AKENO_SHORTCUT_HOLD_MAX_MS);
    /* A single button or no button can never be a shortcut. */
    akeno_shortcut_init(&s, AKENO_PAD_CROSS, 1000, 0);
    CHECK(hold(&s, AKENO_PAD_CROSS, 0, 5000) == 0);
    akeno_shortcut_init(&s, 0, 1000, 0);
    CHECK(hold(&s, 0xFFFFFFFFu, 0, 5000) == 0);
    /* Bits the detector does not know are ignored in the mask. */
    akeno_shortcut_init(&s, 0x80000000u | AKENO_PAD_L3, 1000, 0);
    CHECK(s.mask == AKENO_PAD_L3);
    CHECK(hold(&s, 0xFFFFFFFFu, 0, 5000) == 0);
}

void run_shortcut_tests(void);
void run_shortcut_tests(void) {
    test_combos();
    test_fires_once_after_hold();
    test_short_presses_and_partial_chords();
    test_release_needed_before_rearm();
    test_cooldown();
    test_gaps_restart_the_hold();
    test_hold_time_and_invalid_masks();
}
