#include "lifecycle.h"
#include "test.h"
#include "voice.h"

void run_lifecycle_tests(void) {
    akn_lifecycle l;
    akn_lifecycle_init(&l, 1000, 0x60000001);
    CHECK(akn_lifecycle_last(&l) == NULL);
    uint64_t t = 1000;
    for (int i = 0; i < 100; i++) CHECK(akn_lifecycle_tick(&l, t += 16, AKN_VOICE_CONNECTED, AKN_MIC_OPEN) == 0);
    CHECK(l.pauses == 0);
    /* exactly the threshold is not a pause; one millisecond more is */
    CHECK(akn_lifecycle_tick(&l, t += AKN_LIFE_PAUSE_MS, AKN_VOICE_CONNECTED, AKN_MIC_OPEN) == 0);
    CHECK(akn_lifecycle_tick(&l, t += AKN_LIFE_PAUSE_MS + 1, AKN_VOICE_RECONNECTING, AKN_MIC_OPEN) == 1);
    const akn_life_pause *p = akn_lifecycle_last(&l);
    CHECK(p != NULL);
    CHECK(p->gap_ms == AKN_LIFE_PAUSE_MS + 1);
    CHECK(p->voice_before == AKN_VOICE_CONNECTED && p->voice_after == AKN_VOICE_RECONNECTING);
    CHECK(p->at_ms == t - 1000);
    /* the clock never runs backwards into a pause */
    CHECK(akn_lifecycle_tick(&l, t - 2000, AKN_VOICE_RECONNECTING, AKN_MIC_OPEN) == 0);
    t -= 2000;
    CHECK(akn_lifecycle_tick(&l, t += 42000, AKN_VOICE_FAILED, AKN_MIC_FAILED) == 1);
    CHECK(l.pauses == 2 && l.longest_ms == 42000);
    akn_lifecycle_bigapp(&l, 0x60000002);
    akn_lifecycle_bigapp(&l, 0x60000002);
    CHECK(l.bigapp_changes == 1);
    CHECK(akn_lifecycle_last(&l)->bigapp_after == 0x60000002);
    akn_lifecycle_event(&l, AKN_LIFE_FOCUS_LOST);
    akn_lifecycle_event(&l, AKN_LIFE_FOCUS_LOST);
    akn_lifecycle_event(&l, AKN_LIFE_EVENT_COUNT); /* ignored */
    CHECK(l.events[AKN_LIFE_FOCUS_LOST] == 2);
    for (int i = 0; i < 20; i++) akn_lifecycle_tick(&l, t += 5000, AKN_VOICE_OFF, AKN_MIC_CLOSED);
    CHECK(l.pauses == 22);
    CHECK(l.history[AKN_LIFE_HISTORY - 1].gap_ms == 5000);

    char report[2048];
    size_t n = akn_lifecycle_report(&l, report, sizeof(report));
    CHECK(n == strlen(report));
    CHECK_CONTAINS(report, "Pauses > 1000 ms:   22 (longest 42000 ms");
    CHECK_CONTAINS(report, "focus lost=2");
    CHECK_CONTAINS(report, "0x60000001 at start, 0x60000002 last seen, 1 changes");
    CHECK_CONTAINS(report, "voice off -> off; microphone closed -> closed");
    char small[40];
    n = akn_lifecycle_report(&l, small, sizeof(small));
    CHECK(n < sizeof(small) && strlen(small) == n);
}
