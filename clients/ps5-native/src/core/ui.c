#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "audio_test.h"

/* Home: seven social tiles, then five native platform checks. */
#define HOME_ROW1_COUNT 7
#define HOME_ROW1_Y 440
#define HOME_ROW1_H 210
#define HOME_ROW1_W 226
#define HOME_ROW2_Y 740
#define HOME_ROW2_H 180
#define HOME_ROW2_W 326
#define HOME_GAP 24

#define DEFAULT_HOME_FOCUS 0 /* Friends */

/* Rows of the list screens. */
#define FRIEND_ROW_X AKN_MARGIN_X
#define FRIEND_ROW_Y 290
#define FRIEND_ROW_W 1000
#define FRIEND_ROW_H 84
#define ACTIVITY_ROW_X AKN_MARGIN_X
#define ACTIVITY_ROW_Y 588
#define ACTIVITY_ROW_W 840
#define ACTIVITY_ROW_H 84
#define ROW_STEP 96

static const struct {
    akn_item_id id;
    const char *title, *subtitle;
    const char *increment; /* NULL: available in this build */
} HOME_ROW1[HOME_ROW1_COUNT] = {
    { AKN_ITEM_FRIENDS, "Friends", "Online and playing", NULL },
    { AKN_ITEM_MESSAGES, "Messages", "Chats and groups", NULL },
    { AKN_ITEM_PARTIES, "Party", "Party and invites", NULL },
    { AKN_ITEM_INBOX, "Inbox", "Requests and news", NULL },
    { AKN_ITEM_PROFILE, "Profile", "Status and games", NULL },
    { AKN_ITEM_ACTIVITY, "Activity", "Games and friends", NULL },
    { AKN_ITEM_SETTINGS, "Settings", "Alerts and status", NULL },
};

static const struct {
    akn_item_id id;
    const char *title, *subtitle;
} HOME_ROW2[5] = {
    { AKN_ITEM_CONTROLLER, "Controller test", "Buttons, sticks and triggers" },
    { AKN_ITEM_AUDIO, "Audio test", "Foreground tone (milestone V1)" },
    { AKN_ITEM_CONNECTION, "Connection", "Daemon, server and account" },
    { AKN_ITEM_SYSTEM, "System", "Build, firmware and process" },
    { AKN_ITEM_EXIT, "Exit", "Back to the PS5" },
};

static akn_item make(akn_item_id id, akn_item_kind kind, int x, int y, int w, int h, const char *title, const char *subtitle) {
    akn_item it;
    memset(&it, 0, sizeof(it));
    it.id = id;
    it.kind = kind;
    it.rect.x = x;
    it.rect.y = y;
    it.rect.w = w;
    it.rect.h = h;
    it.enabled = 1;
    it.title = title;
    it.subtitle = subtitle;
    return it;
}

static const char *const ITEM_NAMES[AKN_ITEM_COUNT] = {
    "friends", "messages", "parties", "inbox", "profile", "activity", "settings", "controller", "audio", "connection",
    "system", "exit", "audio-preset", "audio-level", "audio-play", "recheck", "save-report", "exit-confirm",
    "exit-cancel", "heard-yes", "heard-no", "heard-unsure", "confirm-yes", "confirm-no", "row",
};

const char *akn_item_name(akn_item_id id) { return (id >= 0 && id < AKN_ITEM_COUNT) ? ITEM_NAMES[id] : "?"; }

akn_item_id akn_item_from_name(const char *name) {
    for (int i = 0; name && i < AKN_ITEM_COUNT; i++)
        if (strcmp(ITEM_NAMES[i], name) == 0) return (akn_item_id)i;
    return AKN_ITEM_COUNT;
}

akn_screen akn_screen_from_name(const char *name) {
    for (int i = 0; name && i < AKN_SCREEN_COUNT; i++)
        if (strcmp(akn_screen_name((akn_screen)i), name) == 0) return (akn_screen)i;
    return AKN_SCREEN_COUNT;
}

const char *akn_screen_name(akn_screen screen) {
    switch (screen) {
    case AKN_SCREEN_HOME: return "home";
    case AKN_SCREEN_CONTROLLER: return "controller";
    case AKN_SCREEN_AUDIO: return "audio";
    case AKN_SCREEN_CONNECTION: return "connection";
    case AKN_SCREEN_SYSTEM: return "system";
    case AKN_SCREEN_FRIENDS: return "friends";
    case AKN_SCREEN_PROFILE: return "profile";
    case AKN_SCREEN_ACTIVITY: return "activity";
    case AKN_SCREEN_MESSAGES: return "messages";
    case AKN_SCREEN_CHAT: return "chat";
    case AKN_SCREEN_PARTY: return "party";
    case AKN_SCREEN_INBOX: return "inbox";
    case AKN_SCREEN_SETTINGS: return "settings";
    case AKN_SCREEN_PICKER: return "picker";
    case AKN_SCREEN_COUNT: break;
    }
    return "?";
}

const char *akn_screen_title(akn_screen screen) {
    switch (screen) {
    case AKN_SCREEN_HOME: return "Home";
    case AKN_SCREEN_CONTROLLER: return "Controller test";
    case AKN_SCREEN_AUDIO: return "Audio test";
    case AKN_SCREEN_CONNECTION: return "Connection";
    case AKN_SCREEN_SYSTEM: return "System";
    case AKN_SCREEN_FRIENDS: return "Friends";
    case AKN_SCREEN_PROFILE: return "Profile";
    case AKN_SCREEN_ACTIVITY: return "Activity";
    case AKN_SCREEN_MESSAGES: return "Messages";
    case AKN_SCREEN_CHAT: return "Conversation";
    case AKN_SCREEN_PARTY: return "Party";
    case AKN_SCREEN_INBOX: return "Notifications";
    case AKN_SCREEN_SETTINGS: return "Settings";
    case AKN_SCREEN_PICKER: return "Choose a friend";
    case AKN_SCREEN_COUNT: break;
    }
    return "?";
}

void akn_ui_init(akn_ui *ui) {
    memset(ui, 0, sizeof(*ui));
    ui->screen = AKN_SCREEN_HOME;
    ui->focus[AKN_SCREEN_HOME] = DEFAULT_HOME_FOCUS;
    ui->focus[AKN_SCREEN_AUDIO] = 2; /* Play */
    ui->audio_preset = AKN_AUDIO_DEFAULT_PRESET;
    ui->audio_level = AKN_AUDIO_DEFAULT_LEVEL;
    ui->profile_back = AKN_SCREEN_HOME;
    ui->chat_back = AKN_SCREEN_MESSAGES;
    ui->picker_back = AKN_SCREEN_HOME;
}

int akn_ui_is_list(akn_screen screen) {
    switch (screen) {
    case AKN_SCREEN_FRIENDS:
    case AKN_SCREEN_ACTIVITY:
    case AKN_SCREEN_MESSAGES:
    case AKN_SCREEN_PARTY:
    case AKN_SCREEN_INBOX:
    case AKN_SCREEN_SETTINGS:
    case AKN_SCREEN_PICKER: return 1;
    default: return 0;
    }
}

static int rows_visible(akn_screen screen) {
    return !akn_ui_is_list(screen) ? 0 : screen == AKN_SCREEN_ACTIVITY ? AKN_ACTIVITY_ROWS_VISIBLE : AKN_FRIEND_ROWS_VISIBLE;
}

akn_rect akn_ui_row_rect(const akn_ui *ui, akn_screen screen, int row) {
    int slot = row - ui->list_top[screen];
    akn_rect r = { FRIEND_ROW_X, FRIEND_ROW_Y + slot * ROW_STEP, FRIEND_ROW_W, FRIEND_ROW_H };
    if (screen == AKN_SCREEN_ACTIVITY) {
        r.x = ACTIVITY_ROW_X;
        r.y = ACTIVITY_ROW_Y + slot * ROW_STEP;
        r.w = ACTIVITY_ROW_W;
        r.h = ACTIVITY_ROW_H;
    }
    return r;
}

/* Keeps the focused row inside the visible window. */
static void scroll_to_focus(akn_ui *ui, akn_screen screen) {
    int visible = rows_visible(screen), count = ui->list_count[screen];
    int *f = &ui->focus[screen], *top = &ui->list_top[screen];
    if (*f >= count) *f = count - 1;
    if (*f < 0) *f = 0;
    if (*f < *top) *top = *f;
    if (*f >= *top + visible) *top = *f - visible + 1;
    if (*top > count - visible) *top = count - visible;
    if (*top < 0) *top = 0;
}

static void clamp_profile_scroll(akn_ui *ui) {
    int max = ui->profile_content_h - AKN_PROFILE_VIEW_H;
    if (ui->profile_scroll > max) ui->profile_scroll = max;
    if (ui->profile_scroll < 0) ui->profile_scroll = 0;
}

void akn_ui_set_profile_height(akn_ui *ui, int content_h) {
    ui->profile_content_h = content_h;
    clamp_profile_scroll(ui);
}

void akn_ui_set_list_count(akn_ui *ui, akn_screen screen, int count) {
    if (!akn_ui_is_list(screen)) return;
    ui->list_count[screen] = count < 0 ? 0 : count;
    scroll_to_focus(ui, screen);
}

int akn_ui_screen_items(const akn_ui *ui, akn_screen screen, akn_item *out, int max) {
    int n = 0;
#define ADD(item)                  \
    do {                           \
        if (n < max) out[n] = (item); \
        n++;                       \
    } while (0)
    switch (screen) {
    case AKN_SCREEN_HOME:
        for (int i = 0; i < HOME_ROW1_COUNT; i++) {
            akn_item it = make(HOME_ROW1[i].id, AKN_KIND_TILE, AKN_MARGIN_X + i * (HOME_ROW1_W + HOME_GAP), HOME_ROW1_Y,
                               HOME_ROW1_W, HOME_ROW1_H, HOME_ROW1[i].title, HOME_ROW1[i].subtitle);
            it.preview = HOME_ROW1[i].increment != NULL;
            ADD(it);
        }
        for (int i = 0; i < 5; i++)
            ADD(make(HOME_ROW2[i].id, AKN_KIND_TILE, AKN_MARGIN_X + i * (HOME_ROW2_W + HOME_GAP), HOME_ROW2_Y, HOME_ROW2_W,
                     HOME_ROW2_H, HOME_ROW2[i].title, HOME_ROW2[i].subtitle));
        break;
    case AKN_SCREEN_AUDIO: {
        const akn_audio_preset *p = akn_audio_preset_at(ui->audio_preset);
        const akn_audio_level *l = akn_audio_level_at(ui->audio_level);
        akn_item a = make(AKN_ITEM_AUDIO_PRESET, AKN_KIND_SELECTOR, AKN_MARGIN_X, 300, 780, 104, "Format", p ? p->label : "?");
        akn_item b = make(AKN_ITEM_AUDIO_LEVEL, AKN_KIND_SELECTOR, AKN_MARGIN_X, 424, 780, 104, "Level", l ? l->label : "?");
        akn_item c = make(AKN_ITEM_AUDIO_PLAY, AKN_KIND_BUTTON, AKN_MARGIN_X, 576, 780, 104,
                          ui->audio_busy ? "Playing... (press Circle to stop)" : "Play test tone", NULL);
        a.enabled = b.enabled = c.enabled = !ui->audio_busy;
        ADD(a);
        ADD(b);
        ADD(c);
        break;
    }
    case AKN_SCREEN_CONNECTION:
        ADD(make(AKN_ITEM_RECHECK, AKN_KIND_BUTTON, AKN_MARGIN_X, 860, 520, 96, "Check again", NULL));
        break;
    case AKN_SCREEN_SYSTEM:
        ADD(make(AKN_ITEM_SAVE_REPORT, AKN_KIND_BUTTON, AKN_MARGIN_X, 860, 520, 96, "Save diagnostic report", NULL));
        break;
    case AKN_SCREEN_FRIENDS:
    case AKN_SCREEN_ACTIVITY:
    case AKN_SCREEN_MESSAGES:
    case AKN_SCREEN_PARTY:
    case AKN_SCREEN_INBOX:
    case AKN_SCREEN_SETTINGS:
    case AKN_SCREEN_PICKER: {
        int end = ui->list_top[screen] + rows_visible(screen);
        if (end > ui->list_count[screen]) end = ui->list_count[screen];
        for (int i = ui->list_top[screen]; i < end; i++) {
            akn_rect r = akn_ui_row_rect(ui, screen, i);
            akn_item it = make(AKN_ITEM_ROW, AKN_KIND_BUTTON, r.x, r.y, r.w, r.h, NULL, NULL);
            it.index = i;
            ADD(it);
        }
        break;
    }
    case AKN_SCREEN_CONTROLLER:
    case AKN_SCREEN_PROFILE:
    case AKN_SCREEN_CHAT:
    case AKN_SCREEN_COUNT:
        break;
    }
#undef ADD
    return n < max ? n : max;
}

int akn_ui_dialog_items(const akn_ui *ui, akn_item *out, int max) {
    int n = 0;
    if (ui->dialog == AKN_DIALOG_EXIT && max >= 2) {
        out[n++] = make(AKN_ITEM_EXIT_CONFIRM, AKN_KIND_BUTTON, 640, 588, 300, 88, "Exit", NULL);
        out[n++] = make(AKN_ITEM_EXIT_CANCEL, AKN_KIND_BUTTON, 980, 588, 300, 88, "Cancel", NULL);
    } else if (ui->dialog == AKN_DIALOG_HEARD && max >= 3) {
        out[n++] = make(AKN_ITEM_HEARD_YES, AKN_KIND_BUTTON, 520, 578, 280, 88, "Yes, I heard it", NULL);
        out[n++] = make(AKN_ITEM_HEARD_NO, AKN_KIND_BUTTON, 820, 578, 280, 88, "No, silence", NULL);
        out[n++] = make(AKN_ITEM_HEARD_UNSURE, AKN_KIND_BUTTON, 1120, 578, 280, 88, "Not sure", NULL);
    } else if (ui->dialog == AKN_DIALOG_CONFIRM && max >= 2) {
        out[n++] = make(AKN_ITEM_CONFIRM_YES, AKN_KIND_BUTTON, 640, 600, 300, 88, ui->confirm_yes[0] ? ui->confirm_yes : "Yes", NULL);
        out[n++] = make(AKN_ITEM_CONFIRM_NO, AKN_KIND_BUTTON, 980, 600, 300, 88, "Cancel", NULL);
    }
    return n;
}

static int clamp_focus(int focus, int count) {
    if (count <= 0) return -1;
    if (focus < 0) return 0;
    if (focus >= count) return count - 1;
    return focus;
}

int akn_ui_focused(const akn_ui *ui, akn_item *out) {
    akn_item items[AKN_MAX_ITEMS];
    if (ui->dialog != AKN_DIALOG_NONE) {
        int n = akn_ui_dialog_items(ui, items, AKN_MAX_ITEMS);
        int f = clamp_focus(ui->dialog_focus, n);
        if (f < 0) return 0;
        *out = items[f];
        return 1;
    }
    int n = akn_ui_screen_items(ui, ui->screen, items, AKN_MAX_ITEMS);
    if (akn_ui_is_list(ui->screen)) {
        for (int i = 0; i < n; i++)
            if (items[i].index == ui->focus[ui->screen]) {
                *out = items[i];
                return 1;
            }
        return 0;
    }
    int f = clamp_focus(ui->focus[ui->screen], n);
    if (f < 0) return 0;
    *out = items[f];
    return 1;
}

void akn_ui_toast(akn_ui *ui, const char *text, uint64_t now_ms, unsigned duration_ms) {
    snprintf(ui->toast, sizeof(ui->toast), "%s", text);
    ui->toast_until = now_ms + duration_ms;
}

void akn_ui_open_screen(akn_ui *ui, akn_screen screen) {
    if (screen < 0 || screen >= AKN_SCREEN_COUNT) return;
    if (screen == AKN_SCREEN_PROFILE && ui->screen != AKN_SCREEN_PROFILE) ui->profile_scroll = 0;
    if (screen == AKN_SCREEN_CHAT && ui->screen != AKN_SCREEN_CHAT) ui->chat_scroll = 0;
    ui->screen = screen;
    ui->dialog = AKN_DIALOG_NONE;
    ui->back_hold_since = 0;
    ui->toast[0] = '\0'; /* a hint belongs to the screen it was given on */
}

void akn_ui_open_heard(akn_ui *ui) {
    ui->dialog = AKN_DIALOG_HEARD;
    ui->dialog_focus = 0;
}

void akn_ui_open_keyboard(akn_ui *ui, int purpose, const char *title, const char *initial, int max_chars, int single_line) {
    akn_osk_init(&ui->osk, initial, max_chars, single_line);
    ui->osk_purpose = purpose;
    snprintf(ui->osk_title, sizeof(ui->osk_title), "%s", title ? title : "");
    ui->dialog = AKN_DIALOG_KEYBOARD;
    ui->dialog_focus = 0;
}

void akn_ui_open_confirm(akn_ui *ui, int action, const char *title, const char *text, const char *yes) {
    ui->confirm_action = action;
    snprintf(ui->confirm_title, sizeof(ui->confirm_title), "%s", title ? title : "");
    snprintf(ui->confirm_text, sizeof(ui->confirm_text), "%s", text ? text : "");
    snprintf(ui->confirm_yes, sizeof(ui->confirm_yes), "%s", yes ? yes : "");
    ui->dialog = AKN_DIALOG_CONFIRM;
    ui->dialog_focus = 1; /* Cancel first: nothing happens by accident */
}

void akn_ui_close_dialog(akn_ui *ui) { ui->dialog = AKN_DIALOG_NONE; }

void akn_ui_open_chat(akn_ui *ui, akn_screen back) {
    ui->chat_back = back == AKN_SCREEN_CHAT ? AKN_SCREEN_MESSAGES : back;
    akn_ui_open_screen(ui, AKN_SCREEN_CHAT);
    ui->chat_scroll = 0;
}

void akn_ui_open_picker(akn_ui *ui, akn_screen back, int purpose) {
    ui->picker_back = back == AKN_SCREEN_PICKER ? AKN_SCREEN_HOME : back;
    ui->picker_purpose = purpose;
    ui->focus[AKN_SCREEN_PICKER] = 0;
    ui->list_top[AKN_SCREEN_PICKER] = 0;
    akn_ui_open_screen(ui, AKN_SCREEN_PICKER);
}

static void clamp_chat_scroll(akn_ui *ui) {
    int max = ui->chat_content_h - AKN_CHAT_VIEW_H;
    if (ui->chat_scroll > max) ui->chat_scroll = max;
    if (ui->chat_scroll < 0) ui->chat_scroll = 0;
}

void akn_ui_set_chat_height(akn_ui *ui, int content_h) {
    ui->chat_content_h = content_h;
    clamp_chat_scroll(ui);
}

static void open_exit(akn_ui *ui, int focus_cancel) {
    ui->dialog = AKN_DIALOG_EXIT;
    ui->dialog_focus = focus_cancel ? 1 : 0;
}

static akn_dir dir_of(akn_button b) {
    switch (b) {
    case AKN_BTN_UP: return AKN_DIR_UP;
    case AKN_BTN_DOWN: return AKN_DIR_DOWN;
    case AKN_BTN_LEFT: return AKN_DIR_LEFT;
    default: return AKN_DIR_RIGHT;
    }
}

static void move_focus(akn_ui *ui, const akn_item *items, int n, int *focus, akn_dir dir) {
    akn_nav_item nav[AKN_MAX_ITEMS];
    for (int i = 0; i < n; i++) {
        nav[i].rect = items[i].rect;
        nav[i].enabled = items[i].enabled;
    }
    int next = akn_nav_next(nav, n, clamp_focus(*focus, n), dir);
    if (next >= 0 && next != *focus) {
        *focus = next;
        ui->focus_moves++;
    }
}

static akn_cmd activate_dialog(akn_ui *ui, akn_item_id id) {
    ui->confirms++;
    ui->dialog = AKN_DIALOG_NONE;
    switch (id) {
    case AKN_ITEM_EXIT_CONFIRM: return AKN_CMD_EXIT;
    case AKN_ITEM_HEARD_YES: return AKN_CMD_HEARD_YES;
    case AKN_ITEM_HEARD_NO: return AKN_CMD_HEARD_NO;
    case AKN_ITEM_HEARD_UNSURE: return AKN_CMD_HEARD_UNSURE;
    case AKN_ITEM_CONFIRM_YES: return AKN_CMD_CONFIRMED;
    default: return AKN_CMD_NONE;
    }
}

static akn_cmd activate(akn_ui *ui, const akn_item *it, uint64_t now_ms) {
    ui->confirms++;
    if (it->preview) {
        const char *increment = "2";
        for (int i = 0; i < HOME_ROW1_COUNT; i++)
            if (HOME_ROW1[i].id == it->id && HOME_ROW1[i].increment) increment = HOME_ROW1[i].increment;
        char text[200];
        snprintf(text, sizeof(text), "%s arrives in Phase %s. Until then, use the AKENO page in the browser.", it->title, increment);
        akn_ui_toast(ui, text, now_ms, 3500);
        return AKN_CMD_NONE;
    }
    switch (it->id) {
    case AKN_ITEM_FRIENDS: akn_ui_open_screen(ui, AKN_SCREEN_FRIENDS); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_ACTIVITY: akn_ui_open_screen(ui, AKN_SCREEN_ACTIVITY); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_MESSAGES: akn_ui_open_screen(ui, AKN_SCREEN_MESSAGES); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_PARTIES: akn_ui_open_screen(ui, AKN_SCREEN_PARTY); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_INBOX: akn_ui_open_screen(ui, AKN_SCREEN_INBOX); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_SETTINGS: akn_ui_open_screen(ui, AKN_SCREEN_SETTINGS); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_PROFILE:
        akn_ui_open_screen(ui, AKN_SCREEN_PROFILE);
        ui->profile_back = AKN_SCREEN_HOME;
        return AKN_CMD_OPEN_OWN_PROFILE;
    case AKN_ITEM_ROW:
        ui->open_row = it->index;
        if (ui->screen == AKN_SCREEN_MESSAGES) return AKN_CMD_OPEN_CHAT;
        if (ui->screen != AKN_SCREEN_FRIENDS && ui->screen != AKN_SCREEN_ACTIVITY) {
            ui->row_secondary = 0;
            return AKN_CMD_ROW_ACTION;
        }
        ui->profile_back = ui->screen;
        akn_ui_open_screen(ui, AKN_SCREEN_PROFILE);
        return AKN_CMD_OPEN_PROFILE;
    case AKN_ITEM_CONTROLLER: akn_ui_open_screen(ui, AKN_SCREEN_CONTROLLER); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_AUDIO: akn_ui_open_screen(ui, AKN_SCREEN_AUDIO); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_CONNECTION: akn_ui_open_screen(ui, AKN_SCREEN_CONNECTION); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_SYSTEM: akn_ui_open_screen(ui, AKN_SCREEN_SYSTEM); return AKN_CMD_OPENED_SCREEN;
    case AKN_ITEM_EXIT: open_exit(ui, 0); return AKN_CMD_NONE;
    case AKN_ITEM_AUDIO_PRESET: ui->audio_preset = (ui->audio_preset + 1) % AKN_AUDIO_PRESET_COUNT; return AKN_CMD_NONE;
    case AKN_ITEM_AUDIO_LEVEL: ui->audio_level = (ui->audio_level + 1) % AKN_AUDIO_LEVEL_COUNT; return AKN_CMD_NONE;
    case AKN_ITEM_AUDIO_PLAY: return AKN_CMD_START_AUDIO;
    case AKN_ITEM_RECHECK: return AKN_CMD_RECHECK;
    case AKN_ITEM_SAVE_REPORT: return AKN_CMD_SAVE_REPORT;
    default: return AKN_CMD_NONE;
    }
}

akn_cmd akn_ui_handle(akn_ui *ui, const akn_input_event *ev, uint64_t now_ms) {
    if (ev->kind == AKN_EV_RELEASE) return AKN_CMD_NONE;
    int is_dir = ev->button <= AKN_BTN_RIGHT;
    if (ev->kind == AKN_EV_REPEAT && !is_dir) return AKN_CMD_NONE;
    akn_item items[AKN_MAX_ITEMS];

    if (ui->dialog == AKN_DIALOG_KEYBOARD) {
        switch (akn_osk_handle(&ui->osk, ev)) {
        case AKN_OSK_SUBMIT: ui->confirms++; return AKN_CMD_KEYBOARD_SUBMIT;
        case AKN_OSK_CANCEL: ui->dialog = AKN_DIALOG_NONE; return AKN_CMD_KEYBOARD_CANCEL;
        default: return AKN_CMD_NONE;
        }
    }
    if (ui->dialog != AKN_DIALOG_NONE) {
        int n = akn_ui_dialog_items(ui, items, AKN_MAX_ITEMS);
        if (is_dir) {
            move_focus(ui, items, n, &ui->dialog_focus, dir_of(ev->button));
        } else if (ev->button == AKN_BTN_CROSS) {
            int f = clamp_focus(ui->dialog_focus, n);
            if (f >= 0) return activate_dialog(ui, items[f].id);
        } else if (ev->button == AKN_BTN_CIRCLE) {
            akn_dialog was = ui->dialog;
            ui->dialog = AKN_DIALOG_NONE;
            return was == AKN_DIALOG_HEARD ? AKN_CMD_HEARD_UNSURE : AKN_CMD_NONE;
        }
        return AKN_CMD_NONE;
    }

    /* The controller test shows every input instead of acting on it; hold ◯ leaves (tick). */
    if (ui->screen == AKN_SCREEN_CONTROLLER) return AKN_CMD_NONE;
    if (ui->screen == AKN_SCREEN_AUDIO && ui->audio_busy)
        return ev->button == AKN_BTN_CIRCLE ? AKN_CMD_CANCEL_AUDIO : AKN_CMD_NONE;

    int n = akn_ui_screen_items(ui, ui->screen, items, AKN_MAX_ITEMS);
    int *focus = &ui->focus[ui->screen];
    if (ev->button == AKN_BTN_TRIANGLE && (akn_ui_is_list(ui->screen) || ui->screen == AKN_SCREEN_PROFILE || ui->screen == AKN_SCREEN_CHAT))
        return AKN_CMD_REFRESH;
    if (ui->screen == AKN_SCREEN_CHAT) { /* a conversation: up/down scroll it, Cross writes */
        int before = ui->chat_scroll;
        switch (ev->button) {
        case AKN_BTN_CROSS:
            if (ev->kind != AKN_EV_PRESS) return AKN_CMD_NONE;
            ui->confirms++;
            return AKN_CMD_COMPOSE;
        case AKN_BTN_CIRCLE:
            akn_ui_open_screen(ui, ui->chat_back);
            return AKN_CMD_OPENED_SCREEN;
        case AKN_BTN_SQUARE: return AKN_CMD_CHAT_PROFILE;
        case AKN_BTN_UP: ui->chat_scroll += AKN_CHAT_SCROLL_STEP; break;
        case AKN_BTN_DOWN: ui->chat_scroll -= AKN_CHAT_SCROLL_STEP; break;
        case AKN_BTN_L1: ui->chat_scroll += AKN_CHAT_VIEW_H - 100; break;
        case AKN_BTN_R1: ui->chat_scroll -= AKN_CHAT_VIEW_H - 100; break;
        default: return AKN_CMD_NONE;
        }
        clamp_chat_scroll(ui);
        if (ui->chat_scroll != before) {
            ui->focus_moves++;
            return AKN_CMD_NONE;
        }
        /* at the top already: ask for older messages */
        return (ev->button == AKN_BTN_UP || ev->button == AKN_BTN_L1) && ev->kind == AKN_EV_PRESS ? AKN_CMD_LOAD_OLDER : AKN_CMD_NONE;
    }
    if (ui->screen == AKN_SCREEN_PROFILE && ev->button == AKN_BTN_SQUARE) {
        ui->open_row = -1;
        return AKN_CMD_MESSAGE_FRIEND;
    }
    if (ui->screen == AKN_SCREEN_PROFILE && ev->button == AKN_BTN_CIRCLE) {
        akn_screen back = ui->profile_back;
        akn_ui_open_screen(ui, back == AKN_SCREEN_PROFILE ? AKN_SCREEN_HOME : back);
        return AKN_CMD_OPENED_SCREEN;
    }
    if (ui->screen == AKN_SCREEN_PROFILE) { /* a long page: up/down scroll it, L1/R1 by a page */
        int before = ui->profile_scroll;
        if (ev->button == AKN_BTN_DOWN) ui->profile_scroll += AKN_PROFILE_SCROLL_STEP;
        else if (ev->button == AKN_BTN_UP) ui->profile_scroll -= AKN_PROFILE_SCROLL_STEP;
        else if (ev->button == AKN_BTN_R1) ui->profile_scroll += AKN_PROFILE_VIEW_H - 120;
        else if (ev->button == AKN_BTN_L1) ui->profile_scroll -= AKN_PROFILE_VIEW_H - 120;
        clamp_profile_scroll(ui);
        if (ui->profile_scroll != before) ui->focus_moves++;
        return AKN_CMD_NONE;
    }
    if (akn_ui_is_list(ui->screen)) {
        int count = ui->list_count[ui->screen], before = *focus;
        int page = rows_visible(ui->screen);
        if (ev->button == AKN_BTN_UP) *focus -= 1;
        else if (ev->button == AKN_BTN_DOWN) *focus += 1;
        else if (ev->button == AKN_BTN_L1) *focus -= page;
        else if (ev->button == AKN_BTN_R1) *focus += page;
        scroll_to_focus(ui, ui->screen);
        if (*focus != before) {
            ui->focus_moves++;
            return AKN_CMD_NONE;
        }
        if ((ev->button == AKN_BTN_LEFT || ev->button == AKN_BTN_RIGHT) && count > 0 &&
            (ui->screen == AKN_SCREEN_PARTY || ui->screen == AKN_SCREEN_SETTINGS)) {
            ui->open_row = *focus;
            ui->adjust_dir = ev->button == AKN_BTN_RIGHT ? 1 : -1;
            return AKN_CMD_ROW_ADJUST;
        }
        if (ev->button == AKN_BTN_CROSS && count > 0) {
            akn_item it;
            if (akn_ui_focused(ui, &it)) return activate(ui, &it, now_ms);
        }
        if (ev->button == AKN_BTN_SQUARE && count > 0) {
            ui->open_row = *focus;
            if (ui->screen == AKN_SCREEN_FRIENDS) return AKN_CMD_MESSAGE_FRIEND;
            if (ui->screen != AKN_SCREEN_ACTIVITY && ui->screen != AKN_SCREEN_MESSAGES) {
                ui->row_secondary = 1;
                return AKN_CMD_ROW_ACTION;
            }
        }
        if (ev->button == AKN_BTN_CIRCLE) {
            akn_ui_open_screen(ui, ui->screen == AKN_SCREEN_PICKER ? ui->picker_back : AKN_SCREEN_HOME);
            return AKN_CMD_OPENED_SCREEN;
        }
        return AKN_CMD_NONE;
    }
    int f = clamp_focus(*focus, n);
    if (is_dir) {
        if (f >= 0 && items[f].kind == AKN_KIND_SELECTOR && (ev->button == AKN_BTN_LEFT || ev->button == AKN_BTN_RIGHT)) {
            int step = ev->button == AKN_BTN_RIGHT ? 1 : -1;
            if (items[f].id == AKN_ITEM_AUDIO_PRESET)
                ui->audio_preset = (ui->audio_preset + step + AKN_AUDIO_PRESET_COUNT) % AKN_AUDIO_PRESET_COUNT;
            else ui->audio_level = (ui->audio_level + step + AKN_AUDIO_LEVEL_COUNT) % AKN_AUDIO_LEVEL_COUNT;
            return AKN_CMD_NONE;
        }
        move_focus(ui, items, n, focus, dir_of(ev->button));
        return AKN_CMD_NONE;
    }
    if (ev->button == AKN_BTN_CROSS && f >= 0 && items[f].enabled) return activate(ui, &items[f], now_ms);
    if (ev->button == AKN_BTN_CIRCLE) {
        if (ui->screen == AKN_SCREEN_HOME) {
            open_exit(ui, 1);
            return AKN_CMD_NONE;
        }
        akn_ui_open_screen(ui, AKN_SCREEN_HOME);
        return AKN_CMD_OPENED_SCREEN;
    }
    return AKN_CMD_NONE;
}

akn_cmd akn_ui_tick(akn_ui *ui, const akn_input *in, uint64_t now_ms) {
    if (ui->toast[0] && now_ms >= ui->toast_until) ui->toast[0] = '\0';
    if (ui->screen != AKN_SCREEN_CONTROLLER || ui->dialog != AKN_DIALOG_NONE) {
        ui->back_hold_since = 0;
        return AKN_CMD_NONE;
    }
    if (!akn_input_is_down(in, AKN_BTN_CIRCLE)) {
        ui->back_hold_since = 0;
        return AKN_CMD_NONE;
    }
    if (ui->back_hold_since == 0) {
        ui->back_hold_since = now_ms ? now_ms : 1;
        return AKN_CMD_NONE;
    }
    if (now_ms - ui->back_hold_since >= AKN_BACK_HOLD_MS) {
        akn_ui_open_screen(ui, AKN_SCREEN_HOME);
        return AKN_CMD_OPENED_SCREEN;
    }
    return AKN_CMD_NONE;
}

double akn_ui_back_hold_progress(const akn_ui *ui, uint64_t now_ms) {
    if (!ui->back_hold_since || now_ms < ui->back_hold_since) return 0;
    double p = (double)(now_ms - ui->back_hold_since) / AKN_BACK_HOLD_MS;
    return p > 1 ? 1 : p;
}
