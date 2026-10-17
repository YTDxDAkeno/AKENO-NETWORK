#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "audio_test.h"

/* Home: six Phase 2 preview tiles, then five native platform checks. */
#define HOME_ROW1_Y 440
#define HOME_ROW1_H 210
#define HOME_ROW1_W 268
#define HOME_ROW2_Y 740
#define HOME_ROW2_H 180
#define HOME_ROW2_W 326
#define HOME_GAP 24

#define DEFAULT_HOME_FOCUS 6 /* Controller test: the point of this prototype */

static const struct {
    akn_item_id id;
    const char *title, *subtitle;
} HOME_ROW1[6] = {
    { AKN_ITEM_FRIENDS, "Friends", "Friends and requests" },
    { AKN_ITEM_MESSAGES, "Messages", "Direct and group chats" },
    { AKN_ITEM_PARTIES, "Parties", "Voice parties and invites" },
    { AKN_ITEM_PROFILE, "Profile", "Avatar, banner, badges" },
    { AKN_ITEM_ACTIVITY, "Activity", "Games and play time" },
    { AKN_ITEM_SETTINGS, "Settings", "Notifications and privacy" },
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
    "friends", "messages", "parties", "profile", "activity", "settings", "controller", "audio", "connection",
    "system", "exit", "audio-preset", "audio-level", "audio-play", "recheck", "save-report", "exit-confirm",
    "exit-cancel", "heard-yes", "heard-no", "heard-unsure",
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
        for (int i = 0; i < 6; i++) {
            akn_item it = make(HOME_ROW1[i].id, AKN_KIND_TILE, AKN_MARGIN_X + i * (HOME_ROW1_W + HOME_GAP), HOME_ROW1_Y,
                               HOME_ROW1_W, HOME_ROW1_H, HOME_ROW1[i].title, HOME_ROW1[i].subtitle);
            it.preview = 1;
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
    case AKN_SCREEN_CONTROLLER:
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
    ui->screen = screen;
    ui->dialog = AKN_DIALOG_NONE;
    ui->back_hold_since = 0;
    ui->toast[0] = '\0'; /* a hint belongs to the screen it was given on */
}

void akn_ui_open_heard(akn_ui *ui) {
    ui->dialog = AKN_DIALOG_HEARD;
    ui->dialog_focus = 0;
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
    default: return AKN_CMD_NONE;
    }
}

static akn_cmd activate(akn_ui *ui, const akn_item *it, uint64_t now_ms) {
    ui->confirms++;
    if (it->preview) {
        char text[200];
        snprintf(text, sizeof(text), "%s arrives in Phase 2. This prototype proves the console platform first.", it->title);
        akn_ui_toast(ui, text, now_ms, 3500);
        return AKN_CMD_NONE;
    }
    switch (it->id) {
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
