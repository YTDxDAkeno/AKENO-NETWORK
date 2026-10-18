#include "audio_test.h"
#include "script.h"
#include "test.h"
#include "ui.h"

static akn_cmd press(akn_ui *ui, akn_button b, uint64_t now) {
    akn_input_event e = { b, AKN_EV_PRESS, AKN_SRC_PAD };
    return akn_ui_handle(ui, &e, now);
}

static akn_item_id focused(const akn_ui *ui) {
    akn_item it;
    return akn_ui_focused(ui, &it) ? it.id : AKN_ITEM_COUNT;
}

/* Home with the focus on the Controller test tile (the first platform check). */
static void init_at_controller(akn_ui *ui) {
    akn_ui_init(ui);
    ui->focus[AKN_SCREEN_HOME] = 7;
}

static void test_home_navigation(void) {
    akn_ui ui;
    akn_ui_init(&ui);
    CHECK(ui.screen == AKN_SCREEN_HOME);
    CHECK(focused(&ui) == AKN_ITEM_FRIENDS); /* Phase 2.1: the social tiles come first */
    press(&ui, AKN_BTN_DOWN, 0);
    CHECK(focused(&ui) == AKN_ITEM_CONTROLLER);
    press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(focused(&ui) == AKN_ITEM_AUDIO);
    press(&ui, AKN_BTN_UP, 0);
    CHECK(focused(&ui) == AKN_ITEM_MESSAGES || focused(&ui) == AKN_ITEM_PARTIES);
    press(&ui, AKN_BTN_DOWN, 0);
    CHECK(focused(&ui) == AKN_ITEM_AUDIO);
    for (int i = 0; i < 10; i++) press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(focused(&ui) == AKN_ITEM_EXIT);
    CHECK(ui.focus_moves >= 4);
    /* Repeats move too; releases do nothing. */
    akn_input_event rep = { AKN_BTN_LEFT, AKN_EV_REPEAT, AKN_SRC_PAD };
    akn_ui_handle(&ui, &rep, 0);
    CHECK(focused(&ui) == AKN_ITEM_SYSTEM);
    akn_input_event rel = { AKN_BTN_LEFT, AKN_EV_RELEASE, AKN_SRC_PAD };
    akn_ui_handle(&ui, &rel, 0);
    CHECK(focused(&ui) == AKN_ITEM_SYSTEM);
    /* A repeated confirm is ignored (no double activation while held). */
    akn_input_event crep = { AKN_BTN_CROSS, AKN_EV_REPEAT, AKN_SRC_PAD };
    CHECK(akn_ui_handle(&ui, &crep, 0) == AKN_CMD_NONE);
    CHECK(ui.screen == AKN_SCREEN_HOME);
}

static void test_preview_tile(void) {
    akn_ui ui;
    akn_ui_init(&ui);
    /* every social tile opens its screen (no "soon" tiles left) */
    static const struct {
        akn_item_id item;
        akn_screen screen;
    } tiles[] = { { AKN_ITEM_MESSAGES, AKN_SCREEN_MESSAGES }, { AKN_ITEM_PARTIES, AKN_SCREEN_PARTY }, { AKN_ITEM_INBOX, AKN_SCREEN_INBOX },
                  { AKN_ITEM_SETTINGS, AKN_SCREEN_SETTINGS } };
    for (size_t t = 0; t < sizeof(tiles) / sizeof(tiles[0]); t++) {
        akn_ui_init(&ui);
        for (int i = 0; i < 8 && focused(&ui) != tiles[t].item; i++) press(&ui, AKN_BTN_RIGHT, 100);
        CHECK(focused(&ui) == tiles[t].item);
        akn_item it;
        CHECK(akn_ui_focused(&ui, &it) && !it.preview);
        CHECK(press(&ui, AKN_BTN_CROSS, 100) == AKN_CMD_OPENED_SCREEN);
        CHECK(ui.screen == tiles[t].screen);
        CHECK(press(&ui, AKN_BTN_CIRCLE, 100) == AKN_CMD_OPENED_SCREEN && ui.screen == AKN_SCREEN_HOME);
    }
    akn_input in;
    akn_input_init(&in);
    akn_ui_toast(&ui, "hello", 100, 3500);
    akn_ui_tick(&ui, &in, 100 + 3499);
    CHECK(ui.toast[0] != '\0');
    akn_ui_tick(&ui, &in, 100 + 3500);
    CHECK(ui.toast[0] == '\0');
    akn_ui_toast(&ui, "hint", 0, 10000);
    akn_ui_open_screen(&ui, AKN_SCREEN_SYSTEM);
    CHECK(ui.toast[0] == '\0'); /* cleared when the screen changes */
}

static void test_screens_and_back(void) {
    akn_ui ui;
    init_at_controller(&ui);
    CHECK(press(&ui, AKN_BTN_RIGHT, 0) == AKN_CMD_NONE);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_AUDIO);
    CHECK(focused(&ui) == AKN_ITEM_AUDIO_PLAY);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_START_AUDIO);
    /* Selectors: left/right change the value, cross cycles it. */
    press(&ui, AKN_BTN_UP, 0);
    CHECK(focused(&ui) == AKN_ITEM_AUDIO_LEVEL);
    CHECK(ui.audio_level == AKN_AUDIO_DEFAULT_LEVEL);
    press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(ui.audio_level == AKN_AUDIO_DEFAULT_LEVEL + 1);
    press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(ui.audio_level == 0); /* wraps */
    press(&ui, AKN_BTN_LEFT, 0);
    CHECK(ui.audio_level == AKN_AUDIO_LEVEL_COUNT - 1);
    press(&ui, AKN_BTN_UP, 0);
    press(&ui, AKN_BTN_CROSS, 0);
    CHECK(ui.audio_preset == 1);
    /* While the tone plays: only Circle, and it cancels. */
    ui.audio_busy = 1;
    CHECK(press(&ui, AKN_BTN_DOWN, 0) == AKN_CMD_NONE);
    CHECK(focused(&ui) == AKN_ITEM_AUDIO_PRESET);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_NONE);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_CANCEL_AUDIO);
    CHECK(ui.screen == AKN_SCREEN_AUDIO);
    ui.audio_busy = 0;
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_HOME);
    CHECK(focused(&ui) == AKN_ITEM_AUDIO); /* focus remembered */
    press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_CONNECTION);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_RECHECK);
    press(&ui, AKN_BTN_CIRCLE, 0);
    press(&ui, AKN_BTN_RIGHT, 0);
    press(&ui, AKN_BTN_CROSS, 0);
    CHECK(ui.screen == AKN_SCREEN_SYSTEM);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_SAVE_REPORT);
}

static void test_controller_screen(void) {
    akn_ui ui;
    init_at_controller(&ui);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_CONTROLLER);
    /* Every button is shown, none acts; a short Circle does not leave. */
    CHECK(press(&ui, AKN_BTN_CIRCLE, 10) == AKN_CMD_NONE);
    CHECK(press(&ui, AKN_BTN_CROSS, 10) == AKN_CMD_NONE);
    akn_input in;
    akn_input_queue q;
    akn_input_init(&in);
    akn_queue_init(&q);
    akn_input_button(&in, &q, AKN_BTN_CIRCLE, AKN_SRC_PAD, 1, 1000);
    CHECK(akn_ui_tick(&ui, &in, 1000) == AKN_CMD_NONE);
    CHECK(akn_ui_back_hold_progress(&ui, 1500) > 0.49 && akn_ui_back_hold_progress(&ui, 1500) < 0.51);
    CHECK(akn_ui_tick(&ui, &in, 1999) == AKN_CMD_NONE);
    akn_input_button(&in, &q, AKN_BTN_CIRCLE, AKN_SRC_PAD, 0, 1999); /* released early: reset */
    akn_ui_tick(&ui, &in, 2000);
    CHECK(ui.back_hold_since == 0);
    akn_input_button(&in, &q, AKN_BTN_CIRCLE, AKN_SRC_PAD, 1, 3000);
    akn_ui_tick(&ui, &in, 3000);
    CHECK(akn_ui_tick(&ui, &in, 4000) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_HOME);
    CHECK(akn_ui_back_hold_progress(&ui, 4001) == 0);
}

static void test_dialogs(void) {
    akn_ui ui;
    akn_ui_init(&ui);
    /* Circle on Home opens the exit dialog with Cancel focused. */
    press(&ui, AKN_BTN_CIRCLE, 0);
    CHECK(ui.dialog == AKN_DIALOG_EXIT);
    CHECK(focused(&ui) == AKN_ITEM_EXIT_CANCEL);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_NONE);
    CHECK(ui.dialog == AKN_DIALOG_NONE);
    /* The Exit tile opens it with Exit focused. */
    ui.focus[AKN_SCREEN_HOME] = 7;
    for (int i = 0; i < 6; i++) press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(focused(&ui) == AKN_ITEM_EXIT);
    press(&ui, AKN_BTN_CROSS, 0);
    CHECK(focused(&ui) == AKN_ITEM_EXIT_CONFIRM);
    press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(focused(&ui) == AKN_ITEM_EXIT_CANCEL);
    press(&ui, AKN_BTN_LEFT, 0);
    /* Dialog input does not move the screen's focus underneath. */
    CHECK(ui.focus[AKN_SCREEN_HOME] == 11);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_EXIT);
    /* The heard dialog: answers and Circle = not sure. */
    akn_ui_open_heard(&ui);
    CHECK(focused(&ui) == AKN_ITEM_HEARD_YES);
    press(&ui, AKN_BTN_RIGHT, 0);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_HEARD_NO);
    akn_ui_open_heard(&ui);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_HEARD_UNSURE);
    CHECK(ui.dialog == AKN_DIALOG_NONE);
    akn_ui_open_heard(&ui);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_HEARD_YES);
}

static void test_social_screens(void) {
    akn_ui ui;
    akn_ui_init(&ui);
    /* Messages: Cross opens the focused conversation */
    akn_ui_open_screen(&ui, AKN_SCREEN_MESSAGES);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_NONE); /* no rows yet */
    akn_ui_set_list_count(&ui, AKN_SCREEN_MESSAGES, 3);
    press(&ui, AKN_BTN_DOWN, 0);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPEN_CHAT && ui.open_row == 1);
    CHECK(press(&ui, AKN_BTN_SQUARE, 0) == AKN_CMD_NONE);
    /* the conversation: Cross writes, up scrolls back, at the top it asks for older messages */
    akn_ui_open_chat(&ui, AKN_SCREEN_MESSAGES);
    CHECK(ui.screen == AKN_SCREEN_CHAT && ui.chat_scroll == 0);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_COMPOSE);
    akn_ui_set_chat_height(&ui, AKN_CHAT_VIEW_H + 200);
    CHECK(press(&ui, AKN_BTN_UP, 0) == AKN_CMD_NONE && ui.chat_scroll == AKN_CHAT_SCROLL_STEP);
    CHECK(press(&ui, AKN_BTN_UP, 0) == AKN_CMD_NONE && ui.chat_scroll == 200);
    CHECK(press(&ui, AKN_BTN_UP, 0) == AKN_CMD_LOAD_OLDER);
    akn_input_event rep = { AKN_BTN_UP, AKN_EV_REPEAT, AKN_SRC_PAD };
    CHECK(akn_ui_handle(&ui, &rep, 0) == AKN_CMD_NONE); /* holding up does not flood requests */
    CHECK(press(&ui, AKN_BTN_R1, 0) == AKN_CMD_NONE && ui.chat_scroll == 0);
    CHECK(press(&ui, AKN_BTN_TRIANGLE, 0) == AKN_CMD_REFRESH);
    CHECK(press(&ui, AKN_BTN_SQUARE, 0) == AKN_CMD_CHAT_PROFILE);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_OPENED_SCREEN && ui.screen == AKN_SCREEN_MESSAGES);
    /* the keyboard: typing stays in the dialog, Options submits, Circle keeps a draft */
    akn_ui_open_chat(&ui, AKN_SCREEN_PARTY);
    akn_ui_open_keyboard(&ui, 7, "To Bob", "draft", 100, 0);
    CHECK(ui.dialog == AKN_DIALOG_KEYBOARD && ui.osk_purpose == 7);
    CHECK_STR(ui.osk.text, "draft");
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_NONE);
    CHECK_STR(ui.osk.text, "draftq");
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_KEYBOARD_CANCEL && ui.dialog == AKN_DIALOG_NONE && ui.screen == AKN_SCREEN_CHAT);
    CHECK_STR(ui.osk.text, "draftq");
    akn_ui_open_keyboard(&ui, 7, "To Bob", ui.osk.text, 100, 0);
    CHECK(press(&ui, AKN_BTN_OPTIONS, 0) == AKN_CMD_KEYBOARD_SUBMIT && ui.dialog == AKN_DIALOG_KEYBOARD);
    akn_ui_close_dialog(&ui);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_OPENED_SCREEN && ui.screen == AKN_SCREEN_PARTY);
    /* Party, Inbox, Settings rows: Cross and Square are actions on the row */
    akn_ui_set_list_count(&ui, AKN_SCREEN_PARTY, 4);
    press(&ui, AKN_BTN_DOWN, 0);
    press(&ui, AKN_BTN_DOWN, 0);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_ROW_ACTION && ui.open_row == 2 && !ui.row_secondary);
    CHECK(press(&ui, AKN_BTN_SQUARE, 0) == AKN_CMD_ROW_ACTION && ui.open_row == 2 && ui.row_secondary);
    /* the friend picker returns to where it was opened */
    akn_ui_open_picker(&ui, AKN_SCREEN_PARTY, 3);
    CHECK(ui.screen == AKN_SCREEN_PICKER && ui.picker_purpose == 3);
    akn_ui_set_list_count(&ui, AKN_SCREEN_PICKER, 2);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_ROW_ACTION && ui.open_row == 0);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_OPENED_SCREEN && ui.screen == AKN_SCREEN_PARTY);
    /* Friends: Square writes to the focused friend; a profile: Square writes to its owner */
    akn_ui_open_screen(&ui, AKN_SCREEN_FRIENDS);
    akn_ui_set_list_count(&ui, AKN_SCREEN_FRIENDS, 2);
    CHECK(press(&ui, AKN_BTN_SQUARE, 0) == AKN_CMD_MESSAGE_FRIEND && ui.open_row == 0);
    akn_ui_open_screen(&ui, AKN_SCREEN_PROFILE);
    CHECK(press(&ui, AKN_BTN_SQUARE, 0) == AKN_CMD_MESSAGE_FRIEND && ui.open_row == -1);
    /* confirm: Cancel is focused first; Circle cancels; Yes confirms */
    akn_ui_open_confirm(&ui, 42, "Leave the party?", "text", "Leave");
    CHECK(focused(&ui) == AKN_ITEM_CONFIRM_NO);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_NONE && ui.dialog == AKN_DIALOG_NONE);
    akn_ui_open_confirm(&ui, 42, "Leave the party?", "text", "Leave");
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_NONE && ui.dialog == AKN_DIALOG_NONE);
    akn_ui_open_confirm(&ui, 42, "Leave the party?", "text", "Leave");
    press(&ui, AKN_BTN_LEFT, 0);
    CHECK(focused(&ui) == AKN_ITEM_CONFIRM_YES);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_CONFIRMED && ui.confirm_action == 42 && ui.dialog == AKN_DIALOG_NONE);
}

static void test_layout(void) {
    /* Every focusable item lies inside the 1920x1080 screen and items do not overlap. */
    akn_ui ui;
    akn_ui_init(&ui);
    for (int s = 0; s < AKN_SCREEN_COUNT; s++) akn_ui_set_list_count(&ui, (akn_screen)s, 50);
    for (int s = 0; s < AKN_SCREEN_COUNT; s++) {
        akn_item items[AKN_MAX_ITEMS];
        int n = akn_ui_screen_items(&ui, (akn_screen)s, items, AKN_MAX_ITEMS);
        if (akn_ui_is_list((akn_screen)s)) CHECK(n == (s == AKN_SCREEN_ACTIVITY ? AKN_ACTIVITY_ROWS_VISIBLE : AKN_FRIEND_ROWS_VISIBLE));
        for (int i = 0; i < n; i++) {
            akn_rect r = items[i].rect;
            CHECK(r.x >= 0 && r.y >= 0 && r.x + r.w <= AKN_SCREEN_W && r.y + r.h <= 990);
            CHECK(items[i].title != NULL || items[i].id == AKN_ITEM_ROW);
            for (int j = i + 1; j < n; j++) {
                akn_rect o = items[j].rect;
                CHECK(r.x + r.w <= o.x || o.x + o.w <= r.x || r.y + r.h <= o.y || o.y + o.h <= r.y);
            }
        }
    }
    for (int i = 0; i < AKN_ITEM_COUNT; i++) CHECK((int)akn_item_from_name(akn_item_name((akn_item_id)i)) == i);
    for (int i = 0; i < AKN_SCREEN_COUNT; i++) CHECK((int)akn_screen_from_name(akn_screen_name((akn_screen)i)) == i);
    CHECK(akn_item_from_name("nope") == AKN_ITEM_COUNT);
}

static void test_lists(void) {
    akn_ui ui;
    akn_ui_init(&ui);
    akn_item it;
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPENED_SCREEN); /* the Friends tile */
    CHECK(ui.screen == AKN_SCREEN_FRIENDS);
    CHECK(!akn_ui_focused(&ui, &it)); /* no rows yet */
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_NONE);
    CHECK(press(&ui, AKN_BTN_DOWN, 0) == AKN_CMD_NONE);
    akn_ui_set_list_count(&ui, AKN_SCREEN_FRIENDS, 20);
    CHECK(akn_ui_focused(&ui, &it) && it.id == AKN_ITEM_ROW && it.index == 0);
    for (int i = 0; i < 9; i++) press(&ui, AKN_BTN_DOWN, 0);
    CHECK(ui.focus[AKN_SCREEN_FRIENDS] == 9);
    CHECK(ui.list_top[AKN_SCREEN_FRIENDS] == 9 - AKN_FRIEND_ROWS_VISIBLE + 1); /* scrolled just enough */
    CHECK(akn_ui_focused(&ui, &it) && it.index == 9);
    akn_rect last = akn_ui_row_rect(&ui, AKN_SCREEN_FRIENDS, 9);
    CHECK(last.y + last.h <= 990);
    press(&ui, AKN_BTN_LEFT, 0); /* nothing to the side */
    CHECK(ui.focus[AKN_SCREEN_FRIENDS] == 9);
    press(&ui, AKN_BTN_R1, 0);
    CHECK(ui.focus[AKN_SCREEN_FRIENDS] == 9 + AKN_FRIEND_ROWS_VISIBLE);
    press(&ui, AKN_BTN_R1, 0);
    CHECK(ui.focus[AKN_SCREEN_FRIENDS] == 19); /* clamped to the last row */
    press(&ui, AKN_BTN_DOWN, 0);
    CHECK(ui.focus[AKN_SCREEN_FRIENDS] == 19);
    CHECK(ui.list_top[AKN_SCREEN_FRIENDS] == 20 - AKN_FRIEND_ROWS_VISIBLE);
    for (int i = 0; i < 5; i++) press(&ui, AKN_BTN_L1, 0);
    CHECK(ui.focus[AKN_SCREEN_FRIENDS] == 0 && ui.list_top[AKN_SCREEN_FRIENDS] == 0);
    ui.focus[AKN_SCREEN_FRIENDS] = 19;
    akn_ui_set_list_count(&ui, AKN_SCREEN_FRIENDS, 20);
    akn_ui_set_list_count(&ui, AKN_SCREEN_FRIENDS, 5); /* the list shrank */
    CHECK(ui.focus[AKN_SCREEN_FRIENDS] == 4 && ui.list_top[AKN_SCREEN_FRIENDS] == 0);
    /* Cross opens that friend's profile; Circle comes back to the same row. */
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPEN_PROFILE);
    CHECK(ui.screen == AKN_SCREEN_PROFILE && ui.open_row == 4 && ui.profile_back == AKN_SCREEN_FRIENDS);
    CHECK(!akn_ui_focused(&ui, &it));
    CHECK(press(&ui, AKN_BTN_TRIANGLE, 0) == AKN_CMD_REFRESH);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_NONE);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_FRIENDS && ui.focus[AKN_SCREEN_FRIENDS] == 4);
    CHECK(press(&ui, AKN_BTN_TRIANGLE, 0) == AKN_CMD_REFRESH);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_HOME && ui.dialog == AKN_DIALOG_NONE);
    /* The Profile tile opens your own profile and returns to Home. */
    ui.focus[AKN_SCREEN_HOME] = 4;
    CHECK(focused(&ui) == AKN_ITEM_PROFILE);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPEN_OWN_PROFILE);
    CHECK(ui.screen == AKN_SCREEN_PROFILE && ui.profile_back == AKN_SCREEN_HOME);
    CHECK(press(&ui, AKN_BTN_CIRCLE, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_HOME);
    /* Activity: rows of friends playing now. */
    ui.focus[AKN_SCREEN_HOME] = 5;
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPENED_SCREEN);
    CHECK(ui.screen == AKN_SCREEN_ACTIVITY);
    akn_ui_set_list_count(&ui, AKN_SCREEN_ACTIVITY, 2);
    press(&ui, AKN_BTN_DOWN, 0);
    press(&ui, AKN_BTN_DOWN, 0);
    CHECK(ui.focus[AKN_SCREEN_ACTIVITY] == 1);
    CHECK(press(&ui, AKN_BTN_CROSS, 0) == AKN_CMD_OPEN_PROFILE);
    CHECK(ui.open_row == 1 && ui.profile_back == AKN_SCREEN_ACTIVITY);
    press(&ui, AKN_BTN_CIRCLE, 0);
    CHECK(ui.screen == AKN_SCREEN_ACTIVITY);
    /* Not list screens: no rows, no refresh. */
    CHECK(!akn_ui_is_list(AKN_SCREEN_HOME) && !akn_ui_is_list(AKN_SCREEN_PROFILE));
    akn_ui_set_list_count(&ui, AKN_SCREEN_HOME, 9);
    CHECK(ui.list_count[AKN_SCREEN_HOME] == 0);
}

static void test_script(void) {
    akn_step steps[16];
    char err[160];
    int n = akn_script_parse("wait 100; press cross\nhold down 900;pad circle; padhold up 300 ; stick 0 -32768 500;"
                             "unplug;plug; shot /tmp/a.png; expect screen audio; expect focus audio-play;"
                             "until audio 9000; quit",
                             steps, 16, err, sizeof(err));
    CHECK(n == 13);
    CHECK(steps[0].kind == AKN_STEP_WAIT && steps[0].ms == 100);
    CHECK(steps[1].kind == AKN_STEP_PRESS && steps[1].button == AKN_BTN_CROSS);
    CHECK(steps[2].kind == AKN_STEP_HOLD && steps[2].button == AKN_BTN_DOWN && steps[2].ms == 900);
    CHECK(steps[3].kind == AKN_STEP_PAD && steps[3].button == AKN_BTN_CIRCLE);
    CHECK(steps[4].kind == AKN_STEP_PADHOLD && steps[4].ms == 300);
    CHECK(steps[5].kind == AKN_STEP_STICK && steps[5].x == 0 && steps[5].y == -32768);
    CHECK(steps[6].kind == AKN_STEP_UNPLUG && steps[7].kind == AKN_STEP_PLUG);
    CHECK(steps[8].kind == AKN_STEP_SHOT);
    CHECK_STR(steps[8].arg, "/tmp/a.png");
    CHECK(steps[9].kind == AKN_STEP_EXPECT_SCREEN);
    CHECK_STR(steps[10].arg, "audio-play");
    CHECK(steps[11].kind == AKN_STEP_UNTIL_AUDIO && steps[11].ms == 9000);
    CHECK(steps[12].kind == AKN_STEP_QUIT);
    CHECK(akn_script_parse("press nothing", steps, 16, err, sizeof(err)) == -1);
    CHECK_CONTAINS(err, "step 1");
    CHECK(akn_script_parse("wait 1; wait -5", steps, 16, err, sizeof(err)) == -1);
    CHECK_CONTAINS(err, "step 2");
    CHECK(akn_script_parse("dance", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("stick 1 2", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("expect weather sunny", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("wait 1;wait 1;wait 1", steps, 2, err, sizeof(err)) == -1);
    CHECK(akn_script_parse(";;\n ;", steps, 16, err, sizeof(err)) == 0);
    CHECK(akn_script_parse(NULL, steps, 16, err, sizeof(err)) == 0);
    /* Phase 2.1 steps */
    n = akn_script_parse("until ipc 5000; until friends 100; until profile 1; until activity 7; expect text Ada  Lovelace;"
                         "expect notext @it_carol; until text 9000 Playing Astro Bot; sync bob-online",
                         steps, 16, err, sizeof(err));
    CHECK(n == 8);
    CHECK(steps[0].kind == AKN_STEP_UNTIL_IPC && steps[0].ms == 5000);
    CHECK(steps[1].kind == AKN_STEP_UNTIL_FRIENDS && steps[2].kind == AKN_STEP_UNTIL_PROFILE && steps[3].kind == AKN_STEP_UNTIL_ACTIVITY);
    CHECK(steps[4].kind == AKN_STEP_EXPECT_TEXT);
    CHECK_STR(steps[4].arg, "Ada Lovelace"); /* words joined by single spaces */
    CHECK(steps[5].kind == AKN_STEP_EXPECT_NOTEXT);
    CHECK_STR(steps[5].arg, "@it_carol");
    CHECK(steps[6].kind == AKN_STEP_UNTIL_TEXT && steps[6].ms == 9000);
    CHECK_STR(steps[6].arg, "Playing Astro Bot");
    CHECK(steps[7].kind == AKN_STEP_SYNC);
    CHECK_STR(steps[7].arg, "bob-online");
    CHECK(akn_script_parse("sync ../etc", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("sync", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("expect text", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("until text 100", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("until text soon hello", steps, 16, err, sizeof(err)) == -1);
    CHECK(akn_script_parse("until weather 100", steps, 16, err, sizeof(err)) == -1);
}

void run_ui_tests(void) {
    test_home_navigation();
    test_preview_tile();
    test_screens_and_back();
    test_controller_screen();
    test_dialogs();
    test_social_screens();
    test_layout();
    test_lists();
}

void run_script_tests(void) { test_script(); }
