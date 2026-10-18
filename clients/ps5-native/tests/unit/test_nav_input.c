#include "input.h"
#include "nav.h"
#include "test.h"

static akn_nav_item item(int x, int y, int w, int h) {
    akn_nav_item it = { { x, y, w, h }, 1 };
    return it;
}

static void test_grid(void) {
    /* 3 x 2 grid */
    akn_nav_item g[6];
    for (int i = 0; i < 6; i++) g[i] = item(100 + (i % 3) * 300, 100 + (i / 3) * 300, 250, 250);
    CHECK(akn_nav_next(g, 6, 0, AKN_DIR_RIGHT) == 1);
    CHECK(akn_nav_next(g, 6, 1, AKN_DIR_DOWN) == 4);
    CHECK(akn_nav_next(g, 6, 4, AKN_DIR_UP) == 1);
    CHECK(akn_nav_next(g, 6, 0, AKN_DIR_LEFT) == 0); /* edge: stays */
    CHECK(akn_nav_next(g, 6, 0, AKN_DIR_UP) == 0);
    CHECK(akn_nav_next(g, 6, 5, AKN_DIR_RIGHT) == 5);
    g[1].enabled = 0;
    CHECK(akn_nav_next(g, 6, 0, AKN_DIR_RIGHT) == 2); /* disabled items are skipped */
    CHECK(akn_nav_next(g, 6, -1, AKN_DIR_RIGHT) == 0);
    CHECK(akn_nav_next(g, 0, 0, AKN_DIR_RIGHT) == -1);
    g[0].enabled = 0;
    CHECK(akn_nav_first(g, 6) == 2);
}

static void test_uneven_rows(void) {
    /* The home screen: six narrow tiles above five wider ones. */
    akn_nav_item g[11];
    for (int i = 0; i < 6; i++) g[i] = item(96 + i * 292, 440, 268, 210);
    for (int i = 0; i < 5; i++) g[6 + i] = item(96 + i * 350, 740, 326, 180);
    CHECK(akn_nav_next(g, 11, 0, AKN_DIR_DOWN) == 6);
    CHECK(akn_nav_next(g, 11, 5, AKN_DIR_DOWN) == 10); /* rightmost top tile -> rightmost bottom tile */
    CHECK(akn_nav_next(g, 11, 10, AKN_DIR_UP) == 5);
    CHECK(akn_nav_next(g, 11, 7, AKN_DIR_UP) == 1);   /* overlap decides, not just the centre */
    CHECK(akn_nav_next(g, 11, 6, AKN_DIR_RIGHT) == 7);
    CHECK(akn_nav_next(g, 11, 9, AKN_DIR_RIGHT) == 10);
    /* moving right along the bottom row never jumps up a row */
    CHECK(akn_nav_next(g, 11, 10, AKN_DIR_RIGHT) == 10);
}

static void test_list(void) {
    akn_nav_item g[3] = { item(96, 300, 780, 104), item(96, 424, 780, 104), item(96, 576, 780, 104) };
    CHECK(akn_nav_next(g, 3, 0, AKN_DIR_DOWN) == 1);
    CHECK(akn_nav_next(g, 3, 1, AKN_DIR_DOWN) == 2);
    CHECK(akn_nav_next(g, 3, 2, AKN_DIR_DOWN) == 2);
    CHECK(akn_nav_next(g, 3, 2, AKN_DIR_UP) == 1);
    CHECK(akn_nav_next(g, 3, 1, AKN_DIR_RIGHT) == 1);
}

static int expect_event(akn_input_queue *q, akn_button b, akn_event_kind k) {
    akn_input_event e;
    return akn_queue_pop(q, &e) && e.button == b && e.kind == k;
}

static void test_input_press_release(void) {
    akn_input in;
    akn_input_queue q;
    akn_input_init(&in);
    akn_queue_init(&q);
    akn_input_button(&in, &q, AKN_BTN_CROSS, AKN_SRC_PAD, 1, 1000);
    akn_input_button(&in, &q, AKN_BTN_CROSS, AKN_SRC_PAD, 1, 1001); /* duplicate report */
    CHECK(expect_event(&q, AKN_BTN_CROSS, AKN_EV_PRESS));
    CHECK(q.count == 0);
    CHECK(akn_input_is_down(&in, AKN_BTN_CROSS));
    /* A second source holding the same button keeps it down. */
    akn_input_button(&in, &q, AKN_BTN_CROSS, AKN_SRC_KEYBOARD, 1, 1002);
    akn_input_button(&in, &q, AKN_BTN_CROSS, AKN_SRC_PAD, 0, 1003);
    CHECK(q.count == 0);
    akn_input_button(&in, &q, AKN_BTN_CROSS, AKN_SRC_KEYBOARD, 0, 1004);
    CHECK(expect_event(&q, AKN_BTN_CROSS, AKN_EV_RELEASE));
    CHECK(in.presses[AKN_BTN_CROSS] == 1);
    /* Out-of-range values are ignored. */
    akn_input_button(&in, &q, AKN_BTN_COUNT, AKN_SRC_PAD, 1, 1005);
    akn_input_button(&in, &q, AKN_BTN_UP, AKN_SRC_COUNT, 1, 1005);
    CHECK(q.count == 0);
}

static void test_input_repeat(void) {
    akn_input in;
    akn_input_queue q;
    akn_input_init(&in);
    akn_queue_init(&q);
    akn_input_button(&in, &q, AKN_BTN_DOWN, AKN_SRC_PAD, 1, 0);
    CHECK(expect_event(&q, AKN_BTN_DOWN, AKN_EV_PRESS));
    akn_input_tick(&in, &q, AKN_REPEAT_DELAY_MS - 1);
    CHECK(q.count == 0);
    akn_input_tick(&in, &q, AKN_REPEAT_DELAY_MS);
    CHECK(expect_event(&q, AKN_BTN_DOWN, AKN_EV_REPEAT));
    akn_input_tick(&in, &q, AKN_REPEAT_DELAY_MS + AKN_REPEAT_INTERVAL_MS);
    CHECK(expect_event(&q, AKN_BTN_DOWN, AKN_EV_REPEAT));
    /* After a long stall only one repeat is emitted, not a burst. */
    akn_input_tick(&in, &q, 10000);
    CHECK(q.count == 1);
    akn_queue_pop(&q, &(akn_input_event){ 0 });
    /* Non-directional buttons never repeat. */
    akn_input_button(&in, &q, AKN_BTN_CROSS, AKN_SRC_PAD, 1, 10000);
    akn_queue_pop(&q, &(akn_input_event){ 0 });
    akn_input_tick(&in, &q, 20000);
    akn_input_event e;
    int cross_repeat = 0;
    while (akn_queue_pop(&q, &e))
        if (e.button == AKN_BTN_CROSS) cross_repeat = 1;
    CHECK(!cross_repeat);
}

static void test_input_stick(void) {
    akn_input in;
    akn_input_queue q;
    akn_input_init(&in);
    akn_queue_init(&q);
    akn_input_axis(&in, &q, AKN_AXIS_LX, 12000, 0); /* below the enter threshold */
    CHECK(q.count == 0);
    akn_input_axis(&in, &q, AKN_AXIS_LX, 20000, 1);
    CHECK(expect_event(&q, AKN_BTN_RIGHT, AKN_EV_PRESS));
    akn_input_axis(&in, &q, AKN_AXIS_LX, 12000, 2); /* still above the exit threshold: held */
    CHECK(q.count == 0);
    CHECK(akn_input_is_down(&in, AKN_BTN_RIGHT));
    akn_input_axis(&in, &q, AKN_AXIS_LX, 5000, 3);
    CHECK(expect_event(&q, AKN_BTN_RIGHT, AKN_EV_RELEASE));
    /* Dominant axis only: a diagonal pushes one direction. */
    akn_input_axis(&in, &q, AKN_AXIS_LY, -30000, 4);
    akn_input_axis(&in, &q, AKN_AXIS_LX, 20000, 5);
    CHECK(expect_event(&q, AKN_BTN_UP, AKN_EV_PRESS));
    CHECK(q.count == 0);
    /* Flipping the sign releases and presses the opposite direction. */
    akn_input_axis(&in, &q, AKN_AXIS_LX, 0, 6);
    akn_input_axis(&in, &q, AKN_AXIS_LY, 30000, 7);
    CHECK(expect_event(&q, AKN_BTN_UP, AKN_EV_RELEASE));
    CHECK(expect_event(&q, AKN_BTN_DOWN, AKN_EV_PRESS));
    /* A disconnect releases the stick and the pad's buttons. */
    akn_input_button(&in, &q, AKN_BTN_L1, AKN_SRC_PAD, 1, 8);
    akn_queue_pop(&q, &(akn_input_event){ 0 });
    akn_input_release_source(&in, &q, AKN_SRC_PAD, 9);
    CHECK(!akn_input_is_down(&in, AKN_BTN_L1));
    CHECK(!akn_input_is_down(&in, AKN_BTN_DOWN));
    CHECK(in.axes[AKN_AXIS_LY] == 0);
    /* Axis values are clamped and unknown axes ignored. */
    akn_input_axis(&in, &q, AKN_AXIS_RX, 99999, 10);
    CHECK(in.axes[AKN_AXIS_RX] == 32767);
    akn_input_axis(&in, &q, 42, 1, 11);
}

static void test_queue_overflow(void) {
    akn_input in;
    akn_input_queue q;
    akn_input_init(&in);
    akn_queue_init(&q);
    for (int i = 0; i < AKN_INPUT_QUEUE_LEN + 10; i++) akn_input_button(&in, &q, AKN_BTN_CROSS, AKN_SRC_PAD, i % 2 == 0, (uint64_t)i);
    CHECK(q.count == AKN_INPUT_QUEUE_LEN);
    CHECK(q.dropped == 10);
}

static void test_button_names(void) {
    for (int i = 0; i < AKN_BTN_COUNT; i++) CHECK((int)akn_button_from_name(akn_button_name((akn_button)i)) == i);
    CHECK(akn_button_from_name("CROSS") == AKN_BTN_CROSS);
    CHECK(akn_button_from_name("crossx") == AKN_BTN_COUNT);
    CHECK(akn_button_from_name(NULL) == AKN_BTN_COUNT);
    CHECK_STR(akn_button_name(AKN_BTN_COUNT), "unknown");
}

void run_nav_tests(void) {
    test_grid();
    test_uneven_rows();
    test_list();
}

void run_input_tests(void) {
    test_input_press_release();
    test_input_repeat();
    test_input_stick();
    test_queue_overflow();
    test_button_names();
}
