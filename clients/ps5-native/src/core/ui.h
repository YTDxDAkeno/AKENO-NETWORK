/*
 * Screen model of the native prototype: which screen and dialog are open, the focusable items
 * with their 1920x1080 rectangles, focus memory per screen, and the commands that input produces
 * for the application (start the audio test, check the connection, exit...). Portable and
 * unit-tested; the renderer draws exactly these rectangles.
 */
#ifndef AKN_UI_H
#define AKN_UI_H

#include <stdint.h>

#include "input.h"
#include "nav.h"
#include "osk.h"

#define AKN_SCREEN_W 1920
#define AKN_SCREEN_H 1080
#define AKN_MARGIN_X 96

typedef enum {
    AKN_SCREEN_HOME = 0,
    AKN_SCREEN_CONTROLLER,
    AKN_SCREEN_AUDIO,
    AKN_SCREEN_CONNECTION,
    AKN_SCREEN_SYSTEM,
    AKN_SCREEN_FRIENDS,  /* Phase 2.1: list of friends (rows) */
    AKN_SCREEN_PROFILE,  /* Phase 2.1: one profile, yours or a friend's */
    AKN_SCREEN_ACTIVITY, /* Phase 2.1: this console's game, your games, friends playing (rows) */
    AKN_SCREEN_MESSAGES, /* conversations (rows) */
    AKN_SCREEN_CHAT,     /* one conversation */
    AKN_SCREEN_PARTY,    /* your party, party invitations, friends' parties (rows) */
    AKN_SCREEN_INBOX,    /* friend requests, invitations, notifications (rows) */
    AKN_SCREEN_SETTINGS, /* notification, activity and presence settings (rows) */
    AKN_SCREEN_PICKER,   /* choose a friend (to invite to the party, to write to) (rows) */
    AKN_SCREEN_UPDATES,  /* Settings -> Updates (rows, update_view.h) */
    AKN_SCREEN_COUNT
} akn_screen;

typedef enum { AKN_DIALOG_NONE = 0, AKN_DIALOG_EXIT, AKN_DIALOG_HEARD, AKN_DIALOG_KEYBOARD, AKN_DIALOG_CONFIRM } akn_dialog;

typedef enum {
    AKN_ITEM_FRIENDS = 0,
    AKN_ITEM_MESSAGES,
    AKN_ITEM_PARTIES,
    AKN_ITEM_INBOX,
    AKN_ITEM_PROFILE,
    AKN_ITEM_ACTIVITY,
    AKN_ITEM_SETTINGS,
    AKN_ITEM_CONTROLLER,
    AKN_ITEM_AUDIO,
    AKN_ITEM_CONNECTION,
    AKN_ITEM_SYSTEM,
    AKN_ITEM_EXIT,
    AKN_ITEM_AUDIO_PRESET,
    AKN_ITEM_AUDIO_LEVEL,
    AKN_ITEM_AUDIO_PLAY,
    AKN_ITEM_RECHECK,
    AKN_ITEM_SAVE_REPORT,
    AKN_ITEM_EXIT_CONFIRM,
    AKN_ITEM_EXIT_CANCEL,
    AKN_ITEM_HEARD_YES,
    AKN_ITEM_HEARD_NO,
    AKN_ITEM_HEARD_UNSURE,
    AKN_ITEM_CONFIRM_YES,
    AKN_ITEM_CONFIRM_NO,
    AKN_ITEM_ROW, /* a row of a list screen; akn_item.index is the row */
    AKN_ITEM_COUNT
} akn_item_id;

typedef enum { AKN_KIND_TILE = 0, AKN_KIND_BUTTON, AKN_KIND_SELECTOR } akn_item_kind;

typedef struct {
    akn_item_id id;
    akn_item_kind kind;
    akn_rect rect;
    int enabled;
    int preview; /* a later Phase 2 feature shown as a preview tile */
    int index;   /* AKN_ITEM_ROW: the row in the list */
    const char *title;
    const char *subtitle;
} akn_item;

typedef enum {
    AKN_CMD_NONE = 0,
    AKN_CMD_START_AUDIO,
    AKN_CMD_CANCEL_AUDIO,
    AKN_CMD_RECHECK,
    AKN_CMD_SAVE_REPORT,
    AKN_CMD_EXIT,
    AKN_CMD_HEARD_YES,
    AKN_CMD_HEARD_NO,
    AKN_CMD_HEARD_UNSURE,
    AKN_CMD_OPENED_SCREEN, /* informational: the screen changed */
    AKN_CMD_OPEN_PROFILE,  /* the profile screen opened for row ui->open_row of ui->profile_back */
    AKN_CMD_OPEN_OWN_PROFILE,
    AKN_CMD_REFRESH,       /* triangle on a social screen: load it again */
    AKN_CMD_OPEN_CHAT,     /* Messages: the conversation of row ui->open_row */
    AKN_CMD_ROW_ACTION,    /* Party, Inbox, Settings, Picker: row ui->open_row, ui->row_secondary for Square */
    AKN_CMD_ROW_ADJUST,    /* Party, Settings: Left/Right on row ui->open_row, direction ui->adjust_dir */
    AKN_CMD_MESSAGE_FRIEND,/* Square on Friends (row ui->open_row) or a profile (open_row -1): write to them */
    AKN_CMD_COMPOSE,       /* Cross in a conversation: open the keyboard */
    AKN_CMD_KEYBOARD_SUBMIT, /* the keyboard's text (ui->osk) for ui->osk_purpose; the dialog stays open */
    AKN_CMD_KEYBOARD_CANCEL, /* the keyboard closed; its text stays in ui->osk as a draft */
    AKN_CMD_CONFIRMED,     /* the confirm dialog's action ui->confirm_action */
    AKN_CMD_LOAD_OLDER,    /* the conversation is scrolled to its oldest message */
    AKN_CMD_CHAT_PROFILE   /* Square in a direct conversation: the other user's profile */
} akn_cmd;

#define AKN_MAX_ITEMS 16
#define AKN_BACK_HOLD_MS 1000u

typedef struct {
    akn_screen screen;
    akn_dialog dialog;
    int focus[AKN_SCREEN_COUNT]; /* index into the screen's items */
    int dialog_focus;
    int audio_preset, audio_level;
    int audio_busy;              /* set by the application while a tone plays */
    uint64_t back_hold_since;    /* controller test: when ◯ went down, 0 if up */
    char toast[200];
    uint64_t toast_until;
    unsigned long focus_moves;
    unsigned long confirms;
    /* List screens (friends, activity): focus[] is the row; the app sets the row count. */
    int list_count[AKN_SCREEN_COUNT];
    int list_top[AKN_SCREEN_COUNT]; /* first visible row */
    akn_screen profile_back;        /* where circle on the profile returns to */
    int open_row;                   /* the row AKN_CMD_OPEN_PROFILE refers to */
    int profile_scroll;             /* pixels the profile is scrolled down */
    int profile_content_h;          /* height of the profile's content (set by the screen) */
    int row_secondary;              /* AKN_CMD_ROW_ACTION: 1 when Square chose it */
    int adjust_dir;                 /* AKN_CMD_ROW_ADJUST: -1 Left, +1 Right */
    akn_screen chat_back;           /* where circle in a conversation returns to */
    int chat_scroll;                /* pixels scrolled up from the newest message */
    int chat_content_h;             /* height of the conversation's messages (set by the screen) */
    akn_screen picker_back;
    int picker_purpose;             /* the app's reason for the friend picker */
    akn_osk osk;                    /* the keyboard dialog */
    int osk_purpose;
    char osk_title[120];
    int confirm_action;             /* the app's action behind the confirm dialog */
    char confirm_title[120], confirm_text[240], confirm_yes[40], confirm_no[24]; /* confirm_no "" = "Cancel" */
} akn_ui;

#define AKN_PROFILE_VIEW_Y 120      /* the profile scrolls between the top bar and the prompts */
#define AKN_PROFILE_VIEW_H 870
#define AKN_PROFILE_SCROLL_STEP 140

#define AKN_FRIEND_ROWS_VISIBLE 7  /* also Messages, Party, Inbox, Settings, the picker */
#define AKN_ACTIVITY_ROWS_VISIBLE 4

#define AKN_CHAT_VIEW_Y 280           /* the conversation scrolls between its header and the composer */
#define AKN_CHAT_VIEW_H 560
#define AKN_CHAT_SCROLL_STEP 120

void akn_ui_init(akn_ui *ui);
int akn_ui_screen_items(const akn_ui *ui, akn_screen screen, akn_item *out, int max);
int akn_ui_dialog_items(const akn_ui *ui, akn_item *out, int max);
/* The focused item of the open dialog, or else of the screen. Returns 0 when there is none. */
int akn_ui_focused(const akn_ui *ui, akn_item *out);

/* Applies one input event. Returns the command for the application, if any. */
akn_cmd akn_ui_handle(akn_ui *ui, const akn_input_event *ev, uint64_t now_ms);
/* Time-based behaviour (hold ◯ to leave the controller test, toast expiry). */
akn_cmd akn_ui_tick(akn_ui *ui, const akn_input *in, uint64_t now_ms);

void akn_ui_open_screen(akn_ui *ui, akn_screen screen);
/* List screens: the number of rows the app has now (clamps the focus and the scroll position). */
void akn_ui_set_list_count(akn_ui *ui, akn_screen screen, int count);
int akn_ui_is_list(akn_screen screen);
/* Row index -> rectangle on a list screen (rows outside the visible window are not drawn). */
akn_rect akn_ui_row_rect(const akn_ui *ui, akn_screen screen, int row);
/* The profile screen: content height in pixels (clamps the scroll position). */
void akn_ui_set_profile_height(akn_ui *ui, int content_h);
void akn_ui_open_heard(akn_ui *ui);
/* The keyboard dialog for the app's purpose, starting with initial (a draft). */
void akn_ui_open_keyboard(akn_ui *ui, int purpose, const char *title, const char *initial, int max_chars, int single_line);
/* A yes/no question before something that cannot be undone from here (leave the party, decline). */
void akn_ui_open_confirm(akn_ui *ui, int action, const char *title, const char *text, const char *yes);
void akn_ui_close_dialog(akn_ui *ui);
/* Opens a conversation; circle returns to back. */
void akn_ui_open_chat(akn_ui *ui, akn_screen back);
/* Opens the friend picker; circle returns to back. */
void akn_ui_open_picker(akn_ui *ui, akn_screen back, int purpose);
/* The conversation screen: content height in pixels (clamps the scroll position). */
void akn_ui_set_chat_height(akn_ui *ui, int content_h);
void akn_ui_toast(akn_ui *ui, const char *text, uint64_t now_ms, unsigned duration_ms);

const char *akn_screen_name(akn_screen screen);
const char *akn_screen_title(akn_screen screen);
/* Stable lower-case names ("controller", "audio-play", "heard-no", ...) for scripts and logs. */
const char *akn_item_name(akn_item_id id);
akn_item_id akn_item_from_name(const char *name);
akn_screen akn_screen_from_name(const char *name);
/* 0..1 progress of the hold-◯ gesture on the controller test screen. */
double akn_ui_back_hold_progress(const akn_ui *ui, uint64_t now_ms);

#endif
