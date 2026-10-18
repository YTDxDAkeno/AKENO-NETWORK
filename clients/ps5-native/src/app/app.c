#define _POSIX_C_SOURCE 200809L

#include "app.h"
#include "voice_app.h"
#include "update_app.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "akeno/akeno.h"
#include "embed.h"
#include "net.h"
#include "png.h"

AKENO_EMBED_FILE(akn_font_regular, AKN_FONT_REGULAR_PATH);
AKENO_EMBED_FILE(akn_font_bold, AKN_FONT_BOLD_PATH);

#define PAD_STATUS_INTERVAL_MS 500
#define TRIGGER_DOWN 16000
#define TRIGGER_UP 8000

/* ---- Workers ----------------------------------------------------------------------------------- */

typedef struct {
    akn_app *app;
    int preset, level, number;
} audio_job;

static int audio_worker(void *arg) {
    audio_job *job = arg;
    akn_app *app = job->app;
    akn_audio_result result;
    akn_audio_run(akn_platform_audio_ops(), job->preset, job->level, job->number, &app->audio_cancel, &result);
    char text[1600];
    akn_audio_format(&result, text, sizeof(text));
    for (char *line = text; *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        if (*line) akn_logf(&app->log, AKN_LOG_INFO, "audio.result", "%s", line);
        if (!end) break;
        line = end + 1;
    }
    SDL_LockMutex(app->lock);
    app->audio_current = result;
    app->audio_running = 0;
    app->audio_done = 1;
    SDL_UnlockMutex(app->lock);
    free(job);
    return 0;
}

static int net_worker(void *arg) {
    akn_app *app = arg;
    akn_account_view view;
    SDL_LockMutex(app->lock);
    view = app->account;
    SDL_UnlockMutex(app->lock);
    akn_net_check(&app->platform, &view, &app->log);
    SDL_LockMutex(app->lock);
    app->account = view;
    app->net_running = 0;
    SDL_UnlockMutex(app->lock);
    return 0;
}

static void start_net_check(akn_app *app) {
    SDL_LockMutex(app->lock);
    int busy = app->net_running;
    if (!busy) {
        app->net_running = 1;
        app->account.state = AKN_ACC_CHECKING;
    }
    SDL_UnlockMutex(app->lock);
    if (busy) return;
    if (app->net_thread) SDL_WaitThread(app->net_thread, NULL); /* the previous one has finished */
    app->net_thread = SDL_CreateThread(net_worker, "akeno-net", app);
    if (!app->net_thread) {
        SDL_LockMutex(app->lock);
        app->net_running = 0;
        app->account.state = AKN_ACC_ERROR;
        SDL_UnlockMutex(app->lock);
        akn_logf(&app->log, AKN_LOG_ERROR, "net.thread", "could not start the connection check: %s", SDL_GetError());
    }
}

static void start_audio(akn_app *app, uint64_t now) {
    SDL_LockMutex(app->lock);
    int busy = app->audio_running || app->audio_done;
    SDL_UnlockMutex(app->lock);
    if (busy) return;
    if (akn_voice_busy(app)) {
        akn_ui_toast(&app->ui, "Leave voice (or stop the microphone test) before the audio test.", now, 3000);
        return;
    }
    if (app->audio_count >= AKN_AUDIO_MAX_TESTS) {
        akn_ui_toast(&app->ui, "That was the last audio test for this launch (30). Start AKENO Native again for more.", now, 4000);
        return;
    }
    if (app->audio_last_end && now - app->audio_last_end < AKN_AUDIO_MIN_GAP_MS) {
        akn_ui_toast(&app->ui, "Please wait a few seconds between audio tests.", now, 2500);
        return;
    }
    audio_job *job = malloc(sizeof(*job));
    if (!job) return;
    job->app = app;
    job->preset = app->ui.audio_preset;
    job->level = app->ui.audio_level;
    job->number = app->audio_count + 1;
    app->audio_cancel = 0;
    SDL_LockMutex(app->lock);
    app->audio_running = 1;
    SDL_UnlockMutex(app->lock);
    if (app->audio_thread) SDL_WaitThread(app->audio_thread, NULL);
    app->audio_thread = SDL_CreateThread(audio_worker, "akeno-audio", job);
    if (!app->audio_thread) {
        free(job);
        SDL_LockMutex(app->lock);
        app->audio_running = 0;
        SDL_UnlockMutex(app->lock);
        akn_logf(&app->log, AKN_LOG_ERROR, "audio.thread", "could not start the audio test: %s", SDL_GetError());
        return;
    }
    app->ui.audio_busy = 1;
    const akn_audio_preset *p = akn_audio_preset_at(job->preset);
    const akn_audio_level *l = akn_audio_level_at(job->level);
    akn_logf(&app->log, AKN_LOG_INFO, "audio.start", "test %d: %s, %s", job->number, p ? p->label : "?", l ? l->label : "?");
}

/* A finished audio test: keep it and ask whether it was heard. */
static void collect_audio(akn_app *app, uint64_t now) {
    SDL_LockMutex(app->lock);
    int done = app->audio_done;
    akn_audio_result r = app->audio_current;
    app->audio_done = 0;
    SDL_UnlockMutex(app->lock);
    if (!done) return;
    if (app->audio_count < AKN_AUDIO_HISTORY) app->audio_history[app->audio_count] = r;
    else {
        memmove(&app->audio_history[0], &app->audio_history[1], sizeof(r) * (AKN_AUDIO_HISTORY - 1));
        app->audio_history[AKN_AUDIO_HISTORY - 1] = r;
    }
    app->audio_count++;
    app->audio_last_end = now;
    app->ui.audio_busy = 0;
    /* Only a tone that was actually submitted can be heard; ask exactly then. */
    if (r.outcome == AKN_AUDIO_ACCEPTED_PACED || r.outcome == AKN_AUDIO_ACCEPTED_NOT_PACED) akn_ui_open_heard(&app->ui);
    akn_report_save(app);
}

static akn_audio_result *latest_audio(akn_app *app) {
    if (app->audio_count == 0) return NULL;
    int i = app->audio_count < AKN_AUDIO_HISTORY ? app->audio_count - 1 : AKN_AUDIO_HISTORY - 1;
    return &app->audio_history[i];
}

/* ---- Controller -------------------------------------------------------------------------------- */

static void open_pad(akn_app *app, int device_index) {
    if (app->pad) return; /* one controller drives the interface */
    SDL_GameController *pad = SDL_GameControllerOpen(device_index);
    if (!pad) {
        snprintf(app->pad_error, sizeof(app->pad_error), "%s", SDL_GetError());
        akn_logf(&app->log, AKN_LOG_WARN, "controller.open_failed", "SDL_GameControllerOpen(%d): %s", device_index, app->pad_error);
        return;
    }
    app->pad = pad;
    app->pad_instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
    snprintf(app->pad_name, sizeof(app->pad_name), "%s", SDL_GameControllerName(pad) ? SDL_GameControllerName(pad) : "controller");
    app->pad_error[0] = '\0';
    app->pad_seen = 1;
    app->pad_disconnected = 0;
    akn_logf(&app->log, AKN_LOG_INFO, "controller.opened", "%s (device %d)", app->pad_name, device_index);
}

static void close_pad(akn_app *app, uint64_t now) {
    if (!app->pad) return;
    SDL_GameControllerClose(app->pad);
    app->pad = NULL;
    akn_input_release_source(&app->input, &app->queue, AKN_SRC_PAD, now);
    app->pad_disconnected = 1;
    akn_logf(&app->log, AKN_LOG_WARN, "controller.removed", "the controller went away");
    for (int i = 0; i < SDL_NumJoysticks(); i++) /* another attached controller takes over */
        if (SDL_IsGameController(i)) {
            open_pad(app, i);
            if (app->pad) break;
        }
}

static akn_button map_pad_button(int b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return AKN_BTN_CROSS;
    case SDL_CONTROLLER_BUTTON_B: return AKN_BTN_CIRCLE;
    case SDL_CONTROLLER_BUTTON_X: return AKN_BTN_SQUARE;
    case SDL_CONTROLLER_BUTTON_Y: return AKN_BTN_TRIANGLE;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return AKN_BTN_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return AKN_BTN_R1;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return AKN_BTN_L3;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return AKN_BTN_R3;
    case SDL_CONTROLLER_BUTTON_START: return AKN_BTN_OPTIONS;
    case SDL_CONTROLLER_BUTTON_TOUCHPAD: return AKN_BTN_TOUCHPAD;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return AKN_BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return AKN_BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return AKN_BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return AKN_BTN_RIGHT;
    default: return AKN_BTN_COUNT; /* the PS button belongs to the system */
    }
}

static akn_button map_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return AKN_BTN_UP;
    case SDLK_DOWN: return AKN_BTN_DOWN;
    case SDLK_LEFT: return AKN_BTN_LEFT;
    case SDLK_RIGHT: return AKN_BTN_RIGHT;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: return AKN_BTN_CROSS;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: return AKN_BTN_CIRCLE;
    case SDLK_TAB: return AKN_BTN_OPTIONS;
    case SDLK_q: return AKN_BTN_L1;
    case SDLK_e: return AKN_BTN_R1;
    default: return AKN_BTN_COUNT;
    }
}

static void run_command(akn_app *app, akn_cmd cmd, uint64_t now);

static void life_event(akn_app *app, akn_life_event e) {
    akn_lifecycle_event(&app->life, e);
    akn_logf(&app->log, AKN_LOG_INFO, "life.event", "%s", akn_lifecycle_event_name(e));
}

static void handle_event(akn_app *app, const SDL_Event *e, uint64_t now) {
    switch (e->type) {
    case SDL_QUIT: app->quit = 1; break;
    case SDL_CONTROLLERDEVICEADDED:
        app->pads_attached++;
        akn_logf(&app->log, AKN_LOG_INFO, "controller.added", "device %d", e->cdevice.which);
        open_pad(app, e->cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        app->pads_removed++;
        if (app->pad && e->cdevice.which == app->pad_instance) close_pad(app, now);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        if (!app->pad || e->cbutton.which != app->pad_instance) break;
        app->pad_events++;
        akn_button b = map_pad_button(e->cbutton.button);
        if (b != AKN_BTN_COUNT) akn_input_button(&app->input, &app->queue, b, AKN_SRC_PAD, e->type == SDL_CONTROLLERBUTTONDOWN, now);
        break;
    }
    case SDL_CONTROLLERAXISMOTION: {
        if (!app->pad || e->caxis.which != app->pad_instance) break;
        app->pad_events++;
        int v = e->caxis.value;
        switch (e->caxis.axis) {
        case SDL_CONTROLLER_AXIS_LEFTX: akn_input_axis(&app->input, &app->queue, AKN_AXIS_LX, v, now); break;
        case SDL_CONTROLLER_AXIS_LEFTY: akn_input_axis(&app->input, &app->queue, AKN_AXIS_LY, v, now); break;
        case SDL_CONTROLLER_AXIS_RIGHTX: akn_input_axis(&app->input, &app->queue, AKN_AXIS_RX, v, now); break;
        case SDL_CONTROLLER_AXIS_RIGHTY: akn_input_axis(&app->input, &app->queue, AKN_AXIS_RY, v, now); break;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: {
            int left = e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
            akn_input_axis(&app->input, &app->queue, left ? AKN_AXIS_L2 : AKN_AXIS_R2, v, now);
            akn_button b = left ? AKN_BTN_L2 : AKN_BTN_R2;
            if (v >= TRIGGER_DOWN) akn_input_button(&app->input, &app->queue, b, AKN_SRC_PAD, 1, now);
            else if (v <= TRIGGER_UP) akn_input_button(&app->input, &app->queue, b, AKN_SRC_PAD, 0, now);
            break;
        }
        default: break;
        }
        break;
    }
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (app->ui.dialog == AKN_DIALOG_KEYBOARD && SDL_IsTextInputActive()) { /* typing: only the editing keys act */
            SDL_Keycode k = e->key.keysym.sym;
            if (e->type == SDL_KEYDOWN && k == SDLK_BACKSPACE) akn_osk_backspace(&app->ui.osk);
            if (e->type == SDL_KEYDOWN && (k == SDLK_RETURN || k == SDLK_KP_ENTER) && !(e->key.keysym.mod & KMOD_SHIFT))
                run_command(app, AKN_CMD_KEYBOARD_SUBMIT, now);
            if (e->type == SDL_KEYDOWN && (k == SDLK_RETURN || k == SDLK_KP_ENTER) && (e->key.keysym.mod & KMOD_SHIFT)) akn_osk_insert(&app->ui.osk, "\n");
            if (k != SDLK_ESCAPE && k != SDLK_UP && k != SDLK_DOWN && k != SDLK_LEFT && k != SDLK_RIGHT) break;
        }
        if (e->key.repeat) break; /* AKENO repeats D-pad directions itself */
        akn_button b = map_key(e->key.keysym.sym);
        if (b != AKN_BTN_COUNT) akn_input_button(&app->input, &app->queue, b, AKN_SRC_KEYBOARD, e->type == SDL_KEYDOWN, now);
        break;
    }
    case SDL_TEXTINPUT: /* a USB keyboard types into the on-screen keyboard */
        if (app->ui.dialog == AKN_DIALOG_KEYBOARD) akn_osk_insert(&app->ui.osk, e->text.text);
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) akn_input_release_source(&app->input, &app->queue, AKN_SRC_KEYBOARD, now);
        switch (e->window.event) {
        case SDL_WINDOWEVENT_FOCUS_LOST: life_event(app, AKN_LIFE_FOCUS_LOST); break;
        case SDL_WINDOWEVENT_FOCUS_GAINED: life_event(app, AKN_LIFE_FOCUS_GAINED); break;
        case SDL_WINDOWEVENT_HIDDEN: life_event(app, AKN_LIFE_HIDDEN); break;
        case SDL_WINDOWEVENT_SHOWN: life_event(app, AKN_LIFE_SHOWN); break;
        case SDL_WINDOWEVENT_MINIMIZED: life_event(app, AKN_LIFE_MINIMIZED); break;
        case SDL_WINDOWEVENT_RESTORED: life_event(app, AKN_LIFE_RESTORED); break;
        default: break;
        }
        break;
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND: life_event(app, AKN_LIFE_APP_BACKGROUND); break;
    case SDL_APP_WILLENTERFOREGROUND:
    case SDL_APP_DIDENTERFOREGROUND: life_event(app, AKN_LIFE_APP_FOREGROUND); break;
    default: break;
    }
}

/* Test F: one main-loop iteration; a long gap is logged with the voice state around it. */
static void life_tick(akn_app *app, uint64_t now) {
    if (akn_lifecycle_tick(&app->life, now, app->voice.state, app->voice.mic)) {
        const akn_life_pause *p = akn_lifecycle_last(&app->life);
        akn_lifecycle_bigapp(&app->life, akn_platform_running_app());
        app->life_app_at = now;
        akn_logf(&app->log, AKN_LOG_INFO, "life.pause", "the main loop was paused for %.1f s; voice %s, running app 0x%x",
                 p->gap_ms / 1000.0, akn_voice_state_text(&app->voice), (unsigned)app->life.bigapp_last);
    } else if (now - app->life_app_at >= 5000) {
        akn_lifecycle_bigapp(&app->life, akn_platform_running_app());
        app->life_app_at = now;
    }
}

static void poll_pad_status(akn_app *app, uint64_t now) {
    if (now - app->pad_status_at < PAD_STATUS_INTERVAL_MS) return;
    app->pad_status_at = now;
    akn_pad_status st;
    akn_platform_pad_status(&st);
    if (!st.available) return;
    int was = app->pad_status.has_handle && app->pad_status.connected;
    int is = st.has_handle && st.connected;
    if (st.has_handle && !st.connected && was) {
        akn_input_release_source(&app->input, &app->queue, AKN_SRC_PAD, now);
        akn_logf(&app->log, AKN_LOG_WARN, "controller.disconnected", "system pad state: not connected (rc 0x%08x)", (unsigned)st.rc);
    } else if (is && !was && app->pad_seen) {
        akn_logf(&app->log, AKN_LOG_INFO, "controller.connected", "system pad state: connected");
    }
    app->pad_disconnected = app->pad_seen && (!app->pad || (st.has_handle && !st.connected));
    app->pad_status = st;
}

/* The Connection screen checks the account once when it is first opened. */
static void run_net_check_if_never(akn_app *app) {
    SDL_LockMutex(app->lock);
    int never = app->account.checks == 0 && !app->net_running;
    SDL_UnlockMutex(app->lock);
    if (never) start_net_check(app);
}

/* ---- Social screens ---------------------------------------------------------------------------- */

/* For the image worker: pictures are fetched only while the native interface is connected. */
static int images_ready(void *user) {
    akn_app *app = user;
    SDL_LockMutex(app->lock);
    int ready = app->social.ipc == AKN_IPC_READY;
    SDL_UnlockMutex(app->lock);
    return ready;
}

#define FRIENDS_STALE_MS 30000

/* Once per frame: row counts for the list screens, and the focus stays on the same friend when
 * the list is reloaded or reordered by a presence change. */
static void sync_social(akn_app *app) {
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    akn_ui *ui = &app->ui;
    if (s->friends_gen != app->focus_friend_gen) {
        int i = s->friends && app->focus_friend_id[0] ? akn_friends_find(s->friends, app->focus_friend_id) : -1;
        if (i >= 0) ui->focus[AKN_SCREEN_FRIENDS] = i;
        app->focus_friend_gen = s->friends_gen;
    }
    akn_ui_set_list_count(ui, AKN_SCREEN_FRIENDS, s->friends ? s->friends->count : 0);
    akn_ui_set_list_count(ui, AKN_SCREEN_PICKER, s->friends ? s->friends->count : 0);
    akn_ui_set_list_count(ui, AKN_SCREEN_ACTIVITY, akn_social_playing_count(s));
    akn_ui_set_list_count(ui, AKN_SCREEN_MESSAGES, s->convs ? s->convs->count : 0);
    akn_party_row prows[AKN_PARTY_ROWS_MAX];
    akn_ui_set_list_count(ui, AKN_SCREEN_PARTY,
                          akn_party_rows_ex(&s->party, s->inbox, s->joinable, app->voice.state != AKN_VOICE_OFF && app->voice.state != AKN_VOICE_FAILED,
                                            prows, AKN_PARTY_ROWS_MAX));
    /* voice belongs to a party: leaving it (or being removed) ends voice here at once */
    int voice_orphaned = app->voice.state != AKN_VOICE_OFF && app->voice.state != AKN_VOICE_FAILED && app->voice.party_id[0] &&
                         s->party_load == AKN_LOAD_OK && (!s->party.present || strcmp(s->party.id, app->voice.party_id) != 0);
    akn_inbox_row irows[AKN_INBOX_ROWS_MAX];
    akn_ui_set_list_count(ui, AKN_SCREEN_INBOX, akn_social_inbox_rows(s, irows, AKN_INBOX_ROWS_MAX));
    akn_ui_set_list_count(ui, AKN_SCREEN_SETTINGS, AKN_SET_COUNT);
    akn_ui_set_list_count(ui, AKN_SCREEN_UPDATES, AKN_UPD_ROW_COUNT);
    /* the outcome of a write */
    if (s->result_seq != app->result_seen) {
        app->result_seen = s->result_seq;
        akn_ui_toast(ui, s->result, akn_platform_now_ms(), s->result_error ? 5000 : 3000);
    }
    /* a conversation the worker found (writing to a friend) */
    if (s->open_chat_ready) {
        s->open_chat_ready = 0;
        if (ui->dialog == AKN_DIALOG_NONE) akn_ui_open_chat(ui, ui->screen == AKN_SCREEN_CHAT ? ui->chat_back : ui->screen);
    }
    /* the conversation on screen, for the daemon's notifications and for marking it read */
    const char *focus = ui->screen == AKN_SCREEN_CHAT && s->thread_conv.id[0] ? s->thread_conv.id : "";
    if (strcmp(focus, s->focus_conv) != 0) {
        snprintf(s->focus_conv, sizeof(s->focus_conv), "%s", focus);
        s->focus_dirty = 1;
        SDL_CondSignal(app->social_cond);
    }
    int f = ui->focus[AKN_SCREEN_FRIENDS];
    if (s->friends && f >= 0 && f < s->friends->count) snprintf(app->focus_friend_id, sizeof(app->focus_friend_id), "%s", s->friends->items[f].id);
    else app->focus_friend_id[0] = '\0';
    SDL_UnlockMutex(app->lock);
    if (voice_orphaned) akn_voice_leave(app, "You are no longer in that party.");
}

static void opened_social_screen(akn_app *app) {
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    unsigned want = s->ipc == AKN_IPC_READY ? 0 : AKN_WANT_RECONNECT;
    if (app->ui.screen == AKN_SCREEN_FRIENDS &&
        (s->friends_load != AKN_LOAD_OK || akn_platform_now_ms() - s->friends_at > FRIENDS_STALE_MS))
        want |= AKN_WANT_FRIENDS;
    if (app->ui.screen == AKN_SCREEN_ACTIVITY) {
        want |= AKN_WANT_STATE | AKN_WANT_OWN_ACTIVITY;
        if (s->own_activity_load == AKN_LOAD_NONE) s->own_activity_load = AKN_LOAD_LOADING;
        if (s->friends_load != AKN_LOAD_OK) want |= AKN_WANT_FRIENDS;
    }
    if (app->ui.screen == AKN_SCREEN_MESSAGES) want |= AKN_WANT_CONVERSATIONS;
    if (app->ui.screen == AKN_SCREEN_PARTY) want |= AKN_WANT_PARTY | AKN_WANT_INBOX | (s->friends_load != AKN_LOAD_OK ? AKN_WANT_FRIENDS : 0);
    if (app->ui.screen == AKN_SCREEN_INBOX) want |= AKN_WANT_INBOX;
    if (app->ui.screen == AKN_SCREEN_SETTINGS) want |= AKN_WANT_STATE;
    if (app->ui.screen == AKN_SCREEN_PICKER && s->friends_load != AKN_LOAD_OK) want |= AKN_WANT_FRIENDS;
    SDL_UnlockMutex(app->lock);
    if (want) akn_social_want(app, want);
}

static void open_row_profile(akn_app *app, uint64_t now) {
    char id[40] = "";
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    int row = app->ui.open_row;
    int i = app->ui.profile_back == AKN_SCREEN_ACTIVITY ? akn_social_playing_index(s, row) : row;
    if (s->friends && i >= 0 && i < s->friends->count) snprintf(id, sizeof(id), "%s", s->friends->items[i].id);
    SDL_UnlockMutex(app->lock);
    if (!id[0]) {
        akn_ui_open_screen(&app->ui, app->ui.profile_back);
        akn_ui_toast(&app->ui, "That friend is no longer in the list.", now, 2500);
        return;
    }
    akn_logf(&app->log, AKN_LOG_INFO, "ui.profile", "a friend's profile (from %s)", akn_screen_name(app->ui.profile_back));
    akn_social_open_profile(app, id, 0);
}

static void open_own_profile(akn_app *app) {
    char handle[48];
    SDL_LockMutex(app->lock);
    snprintf(handle, sizeof(handle), "%s", app->social.daemon.online_id[0] ? app->social.daemon.online_id : app->account.online_id);
    SDL_UnlockMutex(app->lock);
    akn_logf(&app->log, AKN_LOG_INFO, "ui.profile", "your own profile");
    if (handle[0]) akn_social_open_profile(app, handle, 1);
    else akn_social_want(app, AKN_WANT_RECONNECT);
}

static void refresh_screen(akn_app *app, uint64_t now) {
    if (app->ui.screen == AKN_SCREEN_UPDATES) {
        char why[120];
        if (akn_upd_check(app, 0, why, sizeof(why)) != 0) akn_ui_toast(&app->ui, why, now, 2500);
        return;
    }
    unsigned want = AKN_WANT_RECONNECT;
    if (app->ui.screen == AKN_SCREEN_FRIENDS) want |= AKN_WANT_FRIENDS;
    else if (app->ui.screen == AKN_SCREEN_PROFILE) want |= AKN_WANT_PROFILE;
    else if (app->ui.screen == AKN_SCREEN_ACTIVITY) want |= AKN_WANT_STATE | AKN_WANT_OWN_ACTIVITY | AKN_WANT_FRIENDS;
    else if (app->ui.screen == AKN_SCREEN_MESSAGES) want |= AKN_WANT_CONVERSATIONS;
    else if (app->ui.screen == AKN_SCREEN_CHAT) {
        want |= AKN_WANT_THREAD;
        if (akn_social_retry_failed(app) > 0) {
            akn_ui_toast(&app->ui, "Sending the failed messages again...", now, 2000);
            akn_social_want(app, want);
            return;
        }
    } else if (app->ui.screen == AKN_SCREEN_PARTY) want |= AKN_WANT_PARTY | AKN_WANT_INBOX;
    else if (app->ui.screen == AKN_SCREEN_INBOX) want |= AKN_WANT_INBOX;
    else if (app->ui.screen == AKN_SCREEN_SETTINGS) want |= AKN_WANT_STATE | AKN_WANT_ME;
    else if (app->ui.screen == AKN_SCREEN_PICKER) want |= AKN_WANT_FRIENDS;
    SDL_LockMutex(app->lock);
    if (app->social.ipc == AKN_IPC_READY) want &= ~(unsigned)AKN_WANT_RECONNECT;
    SDL_UnlockMutex(app->lock);
    akn_social_want(app, want);
    akn_ui_toast(&app->ui, "Loading again...", now, 1500);
}

/* ---- Messages, party, inbox, settings ------------------------------------------------------------ */

/* An ID from the server as one URL path segment: letters, digits and '-' only. */
static int safe_id(const char *id) {
    size_t n = strlen(id);
    if (n == 0 || n > 64) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-')) return 0;
    return 1;
}

static void need_writes(akn_app *app, uint64_t now) {
    akn_ui_toast(&app->ui,
                 "This needs the AKENO daemon's native interface 2: send akeno-network-ps5-native-ipc.elf from this release, then try again.",
                 now, 6000);
}

/* Queues a write with an ID in its path; toasts when it cannot. */
/* path: prefix, then the ID when there is one, then suffix (e.g. "/api/v1/parties/" ID "/leave"). */
static void act(akn_app *app, akn_action_kind kind, const char *method, const char *prefix, const char *id, const char *body, const char *label,
                uint64_t now) {
    if (!akn_social_can_write(app)) {
        need_writes(app, now);
        return;
    }
    akn_action a;
    memset(&a, 0, sizeof(a));
    a.kind = kind;
    snprintf(a.method, sizeof(a.method), "%s", method);
    if (id) {
        if (!safe_id(id)) return;
        const char *suffix = strstr(prefix, "%s");
        snprintf(a.path, sizeof(a.path), "%.*s%s%s", suffix ? (int)(suffix - prefix) : (int)strlen(prefix), prefix, id, suffix ? suffix + 2 : "");
    } else {
        snprintf(a.path, sizeof(a.path), "%s", prefix);
    }
    snprintf(a.body, sizeof(a.body), "%s", body ? body : "");
    snprintf(a.label, sizeof(a.label), "%s", label ? label : "");
    if (id) snprintf(a.target, sizeof(a.target), "%s", id);
    if (akn_social_act(app, &a) != 0) akn_ui_toast(&app->ui, "AKENO Native is still busy with earlier changes. Try again in a moment.", now, 3000);
    else akn_ui_toast(&app->ui, "One moment...", now, 1500);
}

static void open_conversation(akn_app *app, const akn_conversation *conv, akn_screen back) {
    akn_social_open_chat(app, conv);
    akn_ui_open_chat(&app->ui, back);
    akn_logf(&app->log, AKN_LOG_INFO, "ui.chat", "a %s conversation", conv->kind == AKN_CONV_PARTY ? "party" : conv->kind == AKN_CONV_GROUP ? "group" : "direct");
}

static void open_chat_row(akn_app *app) {
    akn_conversation conv;
    int found = 0;
    SDL_LockMutex(app->lock);
    const akn_conv_list *l = app->social.convs;
    if (l && app->ui.open_row >= 0 && app->ui.open_row < l->count) {
        conv = l->items[app->ui.open_row];
        found = 1;
    }
    SDL_UnlockMutex(app->lock);
    if (found) open_conversation(app, &conv, AKN_SCREEN_MESSAGES);
}

/* Writes to a user: their direct conversation if it is in the list, else the server finds or
 * creates it (POST /conversations) and the worker opens it. */
static void message_user(akn_app *app, const char *user_id, const char *name, akn_screen back, uint64_t now) {
    akn_conversation conv;
    int found = 0;
    SDL_LockMutex(app->lock);
    const akn_conv_list *l = app->social.convs;
    for (int i = 0; l && i < l->count; i++)
        if (l->items[i].kind == AKN_CONV_DIRECT && strcmp(l->items[i].target_id, user_id) == 0) {
            conv = l->items[i];
            found = 1;
            break;
        }
    int self = strcmp(user_id, app->social.me.id) == 0;
    SDL_UnlockMutex(app->lock);
    if (self) return;
    if (found) {
        open_conversation(app, &conv, back);
        return;
    }
    if (!safe_id(user_id)) return;
    char body[96];
    snprintf(body, sizeof(body), "{\"userId\":\"%s\"}", user_id);
    act(app, AKN_ACT_OPEN_DIRECT, "POST", "/api/v1/conversations", NULL, body, name, now);
}

static void message_friend(akn_app *app, uint64_t now) {
    char id[40] = "", name[80] = "";
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    int friend_profile = 0;
    if (app->ui.open_row < 0) { /* the open profile */
        snprintf(id, sizeof(id), "%s", s->profile.id);
        snprintf(name, sizeof(name), "%s", s->profile.display_name);
        friend_profile = strcmp(s->profile.relationship, "friend") == 0;
    } else if (s->friends && app->ui.open_row < s->friends->count) {
        snprintf(id, sizeof(id), "%s", s->friends->items[app->ui.open_row].id);
        snprintf(name, sizeof(name), "%s", s->friends->items[app->ui.open_row].display_name);
        friend_profile = 1;
    }
    SDL_UnlockMutex(app->lock);
    if (!id[0]) return;
    if (!friend_profile) {
        akn_ui_toast(&app->ui, "AKENO Native writes to friends. Add this player as a friend on the AKENO website first.", now, 4000);
        return;
    }
    message_user(app, id, name, app->ui.screen, now);
}

static void compose(akn_app *app, uint64_t now) {
    akn_conversation conv;
    SDL_LockMutex(app->lock);
    conv = app->social.thread_conv;
    SDL_UnlockMutex(app->lock);
    if (!conv.id[0]) return;
    if (!akn_social_can_write(app)) {
        need_writes(app, now);
        return;
    }
    if (!conv.can_send) {
        akn_ui_toast(&app->ui, "You cannot write in this conversation right now (privacy settings, a block, or you left the party).", now, 4500);
        return;
    }
    char title[160], draft[AKN_OSK_TEXT_MAX + 1] = "";
    snprintf(title, sizeof(title), "Message to %s", conv.title);
    if (strcmp(app->draft_conv, conv.id) == 0) snprintf(draft, sizeof(draft), "%s", app->ui.osk.text);
    akn_ui_open_keyboard(&app->ui, AKN_KB_MESSAGE, title, draft, AKN_MSG_SEND_MAX, 0);
    snprintf(app->draft_conv, sizeof(app->draft_conv), "%s", conv.id);
    SDL_StartTextInput();
}

static void keyboard_submit(akn_app *app, uint64_t now) {
    akn_ui *ui = &app->ui;
    if (ui->osk_purpose == AKN_KB_MESSAGE) {
        if (!akn_message_valid(ui->osk.text)) {
            akn_ui_toast(ui, "Type a message first.", now, 2000);
            return;
        }
        if (akn_social_send(app, ui->osk.text) != 0) {
            akn_ui_toast(ui, "The message could not be sent now. Check the connection and try again.", now, 3500);
            return;
        }
        akn_osk_init(&ui->osk, NULL, AKN_MSG_SEND_MAX, 0);
        app->draft_conv[0] = '\0';
        ui->chat_scroll = 0;
        akn_logf(&app->log, AKN_LOG_INFO, "ui.message", "message queued"); /* never its text */
    } else if (ui->osk_purpose == AKN_KB_PARTY_NAME) {
        char body[400];
        if (akn_party_create_json(ui->osk.text, 0, body, sizeof(body)) != 0) {
            akn_ui_toast(ui, "That name cannot be used.", now, 2500);
            return;
        }
        act(app, AKN_ACT_PARTY_CREATE, "POST", "/api/v1/parties", NULL, body, ui->osk.text, now);
    }
    akn_ui_close_dialog(ui);
    SDL_StopTextInput();
}

static void confirm(akn_app *app, int action, const char *target, const char *label, const char *title, const char *text, const char *yes) {
    snprintf(app->confirm_target, sizeof(app->confirm_target), "%s", target);
    snprintf(app->confirm_label, sizeof(app->confirm_label), "%s", label);
    akn_ui_open_confirm(&app->ui, action, title, text, yes);
}

static void confirmed(akn_app *app, uint64_t now) {
    switch (app->ui.confirm_action) {
    case AKN_CONFIRM_LEAVE_PARTY:
        act(app, AKN_ACT_PARTY_LEAVE, "POST", "/api/v1/parties/%s/leave", app->confirm_target, NULL, app->confirm_label, now);
        break;
    case AKN_CONFIRM_DECLINE_INVITATION:
        act(app, AKN_ACT_INVITATION_DECLINE, "POST", "/api/v1/invitations/%s/decline", app->confirm_target, NULL, app->confirm_label, now);
        break;
    case AKN_CONFIRM_DECLINE_REQUEST:
        act(app, AKN_ACT_REQUEST_DECLINE, "POST", "/api/v1/friends/requests/%s/decline", app->confirm_target, NULL, app->confirm_label, now);
        break;
    case AKN_CONFIRM_JOIN_VOICE: {
        char why[200];
        if (akn_voice_join(app, app->confirm_target, app->confirm_label, why, sizeof(why)) != 0) akn_ui_toast(&app->ui, why, now, 4000);
        else akn_ui_toast(&app->ui, "Joining voice...", now, 1500);
        break;
    }
    case AKN_CONFIRM_MIC_TEST: {
        char why[200];
        if (akn_voice_mic_test(app, why, sizeof(why)) != 0) akn_ui_toast(&app->ui, why, now, 4000);
        break;
    }
    case AKN_CONFIRM_UPDATE_INSTALL: {
        char why[300];
        akn_ui_open_screen(&app->ui, AKN_SCREEN_UPDATES);
        app->ui.focus[AKN_SCREEN_UPDATES] = AKN_UPD_ROW_INSTALL;
        if (akn_upd_install(app, why, sizeof(why)) != 0) akn_ui_toast(&app->ui, why, now, 4000);
        break;
    }
    case AKN_CONFIRM_UPDATE_RESTART:
        akn_logf(&app->log, AKN_LOG_INFO, "update.restart", "closing so the launcher starts %s", app->upd.installed);
        app->quit = 1;
        break;
    default: break;
    }
}

static void party_row(akn_app *app, uint64_t now) {
    akn_party_row rows[AKN_PARTY_ROWS_MAX];
    akn_party_row r;
    char id[40] = "", name[100] = "", conv_id[40] = "";
    int secondary = app->ui.row_secondary, members = 0, valid = 0, self = 0;
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    int in_voice = app->voice.state != AKN_VOICE_OFF && app->voice.state != AKN_VOICE_FAILED;
    int n = akn_party_rows_ex(&s->party, s->inbox, s->joinable, in_voice, rows, AKN_PARTY_ROWS_MAX);
    if (app->ui.open_row >= 0 && app->ui.open_row < n) {
        r = rows[app->ui.open_row];
        valid = 1;
        switch (r.kind) {
        case AKN_PROW_VOICE:
        case AKN_PROW_VOICE_MUTE:
        case AKN_PROW_VOICE_DEAFEN:
        case AKN_PROW_VOICE_OUTPUT:
        case AKN_PROW_VOICE_MIC:
        case AKN_PROW_CHAT:
        case AKN_PROW_INVITE:
        case AKN_PROW_LEAVE:
            snprintf(id, sizeof(id), "%s", s->party.id);
            snprintf(name, sizeof(name), "%s", s->party.name[0] ? s->party.name : "the party");
            snprintf(conv_id, sizeof(conv_id), "%s", s->party.conversation_id);
            members = s->party.member_count;
            break;
        case AKN_PROW_MEMBER:
            snprintf(id, sizeof(id), "%s", s->party.members[r.index].id);
            snprintf(name, sizeof(name), "%s", s->party.members[r.index].display_name);
            self = strcmp(id, s->me.id) == 0;
            break;
        case AKN_PROW_INVITATION:
            snprintf(id, sizeof(id), "%s", s->inbox->invitations[r.index].id);
            snprintf(name, sizeof(name), "%s", s->inbox->invitations[r.index].target_name);
            break;
        case AKN_PROW_JOINABLE:
            snprintf(id, sizeof(id), "%s", s->joinable->items[r.index].id);
            snprintf(name, sizeof(name), "%s", s->joinable->items[r.index].name[0] ? s->joinable->items[r.index].name : "the party");
            break;
        case AKN_PROW_CREATE: break;
        }
    }
    SDL_UnlockMutex(app->lock);
    if (!valid) return;
    switch (r.kind) {
    case AKN_PROW_CREATE:
        if (!akn_social_can_write(app)) {
            need_writes(app, now);
            return;
        }
        akn_ui_open_keyboard(&app->ui, AKN_KB_PARTY_NAME, "Name your party (optional), then press Options", "", 48, 1);
        SDL_StartTextInput();
        break;
    case AKN_PROW_CHAT: {
        akn_conversation conv;
        memset(&conv, 0, sizeof(conv));
        conv.kind = AKN_CONV_PARTY;
        snprintf(conv.id, sizeof(conv.id), "%s", conv_id);
        snprintf(conv.target_id, sizeof(conv.target_id), "%s", id);
        snprintf(conv.title, sizeof(conv.title), "%.79s", name);
        conv.can_send = 1;
        conv.members = members;
        open_conversation(app, &conv, AKN_SCREEN_PARTY);
        break;
    }
    case AKN_PROW_INVITE: akn_ui_open_picker(&app->ui, AKN_SCREEN_PARTY, AKN_PICK_INVITE); break;
    case AKN_PROW_LEAVE:
        confirm(app, AKN_CONFIRM_LEAVE_PARTY, id, name, "Leave the party?", "You can join again while it is open, or be invited again.", "Leave");
        break;
    case AKN_PROW_MEMBER:
        if (secondary) {
            if (!self) message_user(app, id, name, AKN_SCREEN_PARTY, now);
        } else {
            app->ui.profile_back = AKN_SCREEN_PARTY;
            akn_ui_open_screen(&app->ui, AKN_SCREEN_PROFILE);
            akn_social_open_profile(app, id, self);
        }
        break;
    case AKN_PROW_INVITATION:
        if (secondary) confirm(app, AKN_CONFIRM_DECLINE_INVITATION, id, name, "Decline the invitation?", name, "Decline");
        else act(app, AKN_ACT_INVITATION_ACCEPT, "POST", "/api/v1/invitations/%s/accept", id, NULL, name, now);
        break;
    case AKN_PROW_JOINABLE:
        if (!secondary) act(app, AKN_ACT_PARTY_JOIN, "POST", "/api/v1/parties/%s/join", id, NULL, name, now);
        break;
    case AKN_PROW_VOICE:
        if (akn_voice_busy(app) && !app->voice.mic_test) {
            akn_voice_leave(app, "You left voice.");
            akn_ui_toast(&app->ui, "You left voice.", now, 2000);
        } else if (!secondary) {
            confirm(app, AKN_CONFIRM_JOIN_VOICE, id, name, "Join voice with your microphone?",
                    "AKENO opens your microphone (DualSense or headset) for this party's voice chat only, after you press Join. "
                    "Nothing is recorded or stored. You can mute or leave at any time.",
                    "Join voice");
        }
        break;
    case AKN_PROW_VOICE_MUTE: akn_voice_toggle_mute(app); break;
    case AKN_PROW_VOICE_DEAFEN: akn_voice_toggle_deafen(app); break;
    case AKN_PROW_VOICE_OUTPUT:
    case AKN_PROW_VOICE_MIC: akn_voice_adjust(app, r.kind == AKN_PROW_VOICE_MIC, secondary ? -1 : 1); break;
    }
}

/* Left/Right on a volume row of the Party screen. */
static void updates_row(akn_app *app, uint64_t now);

static void row_adjust(akn_app *app) {
    if (app->ui.screen == AKN_SCREEN_UPDATES) {
        if (app->ui.open_row == AKN_UPD_ROW_CHANNEL || app->ui.open_row == AKN_UPD_ROW_AUTO) {
            app->ui.row_secondary = 0;
            updates_row(app, akn_platform_now_ms());
        }
        return;
    }
    if (app->ui.screen != AKN_SCREEN_PARTY) return;
    akn_party_row rows[AKN_PARTY_ROWS_MAX];
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    int in_voice = app->voice.state != AKN_VOICE_OFF && app->voice.state != AKN_VOICE_FAILED;
    int n = akn_party_rows_ex(&s->party, s->inbox, s->joinable, in_voice, rows, AKN_PARTY_ROWS_MAX);
    int row = app->ui.open_row;
    akn_party_row_kind kind = row >= 0 && row < n ? rows[row].kind : AKN_PROW_CREATE;
    SDL_UnlockMutex(app->lock);
    if (kind == AKN_PROW_VOICE_OUTPUT || kind == AKN_PROW_VOICE_MIC) akn_voice_adjust(app, kind == AKN_PROW_VOICE_MIC, app->ui.adjust_dir);
}

static void inbox_row(akn_app *app, uint64_t now) {
    akn_inbox_row rows[AKN_INBOX_ROWS_MAX];
    akn_inbox_row r;
    char id[40] = "", name[160] = "";
    int valid = 0, secondary = app->ui.row_secondary;
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    int n = akn_social_inbox_rows(s, rows, AKN_INBOX_ROWS_MAX);
    if (app->ui.open_row >= 0 && app->ui.open_row < n) {
        r = rows[app->ui.open_row];
        valid = 1;
        if (r.kind == AKN_IROW_REQUEST) {
            snprintf(id, sizeof(id), "%s", s->inbox->incoming[r.index].id);
            snprintf(name, sizeof(name), "%s", s->inbox->incoming[r.index].display_name);
        } else if (r.kind == AKN_IROW_INVITATION) {
            const akn_invitation *v = &s->inbox->invitations[r.index];
            snprintf(id, sizeof(id), "%s", v->id);
            snprintf(name, sizeof(name), "the %s \"%s\"", akn_invitation_kind_label(v->kind), v->target_name);
        }
    }
    SDL_UnlockMutex(app->lock);
    if (!valid) return;
    switch (r.kind) {
    case AKN_IROW_REQUEST:
        if (secondary) confirm(app, AKN_CONFIRM_DECLINE_REQUEST, id, name, "Decline the friend request?", name, "Decline");
        else act(app, AKN_ACT_REQUEST_ACCEPT, "POST", "/api/v1/friends/requests/%s/accept", id, NULL, name, now);
        break;
    case AKN_IROW_INVITATION:
        if (secondary) confirm(app, AKN_CONFIRM_DECLINE_INVITATION, id, name, "Decline the invitation?", name, "Decline");
        else act(app, AKN_ACT_INVITATION_ACCEPT, "POST", "/api/v1/invitations/%s/accept", id, NULL, name, now);
        break;
    case AKN_IROW_NOTIFICATIONS:
        act(app, AKN_ACT_NOTIFICATIONS_READ, "POST", "/api/v1/notifications/read", NULL, "{\"all\":true}", "", now);
        break;
    }
}

static void settings_row(akn_app *app, uint64_t now) {
    int row = app->ui.open_row;
    SDL_LockMutex(app->lock);
    akn_daemon_info d = app->social.daemon;
    char pref[16];
    snprintf(pref, sizeof(pref), "%s", app->social.presence_pref);
    if (!pref[0]) {
        akn_presence p = app->social.me.presence.status;
        snprintf(pref, sizeof(pref), "%s", p == AKN_PRES_AWAY ? "away" : p == AKN_PRES_DND ? "dnd" : "online");
    }
    SDL_UnlockMutex(app->lock);
    char body[80];
    switch ((akn_setting_row)row) {
    case AKN_SET_NOTIFY_MESSAGES:
    case AKN_SET_NOTIFY_SOCIAL:
    case AKN_SET_ACTIVITY: {
        if (!d.settings_known) {
            akn_ui_toast(&app->ui, "The settings are still loading.", now, 2000);
            return;
        }
        const char *key = row == AKN_SET_NOTIFY_MESSAGES ? "notifyMessages" : row == AKN_SET_NOTIFY_SOCIAL ? "notifySocial" : "activityEnabled";
        int value = row == AKN_SET_NOTIFY_MESSAGES ? d.notify_messages : row == AKN_SET_NOTIFY_SOCIAL ? d.notify_social : d.activity_enabled;
        snprintf(body, sizeof(body), "{\"%s\":%s}", key, value ? "false" : "true");
        const char *label = row == AKN_SET_NOTIFY_MESSAGES ? (value ? "Message notifications off." : "Message notifications on.")
                            : row == AKN_SET_NOTIFY_SOCIAL ? (value ? "Friend and invitation notifications off." : "Friend and invitation notifications on.")
                                                           : (value ? "Game activity is no longer shared." : "Game activity is shared again.");
        act(app, AKN_ACT_SETTING, "POST", "/api/settings", NULL, body, label, now);
        break;
    }
    case AKN_SET_PRESENCE: {
        static const char *const order[] = { "online", "away", "dnd", "invisible" };
        int i = 0;
        while (i < 4 && strcmp(order[i], pref) != 0) i++;
        const char *next = order[(i + (app->ui.row_secondary ? 3 : 1)) % 4];
        snprintf(body, sizeof(body), "{\"status\":\"%s\"}", next);
        act(app, AKN_ACT_PRESENCE, "PUT", "/api/v1/presence", NULL, body, next, now);
        break;
    }
    case AKN_SET_CONNECTION: akn_ui_open_screen(&app->ui, AKN_SCREEN_CONNECTION); run_net_check_if_never(app); break;
    case AKN_SET_SYSTEM: akn_ui_open_screen(&app->ui, AKN_SCREEN_SYSTEM); break;
    case AKN_SET_AUDIO: akn_ui_open_screen(&app->ui, AKN_SCREEN_AUDIO); break;
    case AKN_SET_CONTROLLER: akn_ui_open_screen(&app->ui, AKN_SCREEN_CONTROLLER); break;
    case AKN_SET_MIC_TEST:
        if (app->voice.mic_test) {
            if (app->ui.row_secondary) akn_voice_toggle_monitor(app);
            else akn_voice_leave(app, "Microphone test stopped.");
        } else if (akn_voice_busy(app)) {
            akn_ui_toast(&app->ui, "Leave voice first.", now, 2500);
        } else {
            confirm(app, AKN_CONFIRM_MIC_TEST, "", "", "Test your microphone?",
                    "AKENO opens your microphone (DualSense or headset) on this console only and shows its level. Nothing is sent, "
                    "recorded or stored. Press Cross again to stop.",
                    "Start the test");
        }
        break;
    case AKN_SET_UPDATES: akn_ui_open_screen(&app->ui, AKN_SCREEN_UPDATES); break;
    case AKN_SET_COUNT: break;
    }
}

/* Settings -> Updates. */
static void updates_row(akn_app *app, uint64_t now) {
    akn_upd_view v;
    akn_upd_view_get(app, &v, NULL, 0, NULL, 0);
    akn_upd_row row = (akn_upd_row)app->ui.open_row;
    char why[300] = "";
    int downloading = v.phase >= AKN_UPD_DOWNLOADING && v.phase <= AKN_UPD_INSTALLING;
    if (row == AKN_UPD_ROW_INSTALL && downloading) {
        if (app->ui.row_secondary) {
            akn_upd_cancel(app);
            akn_ui_toast(&app->ui, "Cancelling the update...", now, 2000);
        } else {
            akn_ui_toast(&app->ui, "The update is running. Square cancels it.", now, 2500);
        }
        return;
    }
    if (!akn_upd_row_enabled(&v, row)) {
        if (row == AKN_UPD_ROW_HISTORY || row == AKN_UPD_ROW_CURRENT) {
            app->upd.show_notes = 0;
            return;
        }
        char title[80], sub[300];
        akn_upd_row_text(&v, row, title, sizeof(title), sub, sizeof(sub));
        akn_ui_toast(&app->ui, sub, now, 3000);
        return;
    }
    switch (row) {
    case AKN_UPD_ROW_CHANNEL:
        akn_upd_set_channel(app, v.prefs.channel == AKN_CHANNEL_BETA ? AKN_CHANNEL_STABLE : AKN_CHANNEL_BETA);
        akn_ui_toast(&app->ui, app->upd.prefs.channel == AKN_CHANNEL_BETA ? "Beta channel: pre-releases are offered too." : "Stable channel.", now, 2500);
        break;
    case AKN_UPD_ROW_AUTO:
        akn_upd_set_auto(app, !v.prefs.auto_check);
        akn_ui_toast(&app->ui, app->upd.prefs.auto_check ? "Automatic update checks on." : "Automatic update checks off.", now, 2500);
        break;
    case AKN_UPD_ROW_CHECK:
        if (akn_upd_check(app, 0, why, sizeof(why)) != 0) akn_ui_toast(&app->ui, why, now, 3000);
        break;
    case AKN_UPD_ROW_LATEST:
    case AKN_UPD_ROW_CHANGELOG: app->upd.show_notes = !app->upd.show_notes; break;
    case AKN_UPD_ROW_INSTALL: {
        char text[240], size[32];
        akn_update_format_size(v.size, size, sizeof(size));
        snprintf(text, sizeof(text), "AKENO downloads %s (%s), checks its signature and installs it next to this version. "
                 "Your data and sign-in stay as they are.", v.version, size);
        confirm(app, AKN_CONFIRM_UPDATE_INSTALL, "", "", "Install the update?", text, "Install");
        break;
    }
    case AKN_UPD_ROW_RESTART:
        confirm(app, AKN_CONFIRM_UPDATE_RESTART, "", "", "Close AKENO now?",
                "Then start AKENO NETWORK again from the Homebrew Launcher: it starts the new version. If it does not start "
                "twice, the launcher goes back to this one.", "Close AKENO");
        break;
    default: break;
    }
}

static void picker_row(akn_app *app, uint64_t now) {
    char id[40] = "", name[80] = "", party[40] = "";
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    if (s->friends && app->ui.open_row >= 0 && app->ui.open_row < s->friends->count) {
        snprintf(id, sizeof(id), "%s", s->friends->items[app->ui.open_row].id);
        snprintf(name, sizeof(name), "%s", s->friends->items[app->ui.open_row].display_name);
    }
    snprintf(party, sizeof(party), "%s", s->party.present ? s->party.id : "");
    SDL_UnlockMutex(app->lock);
    if (!id[0]) return;
    if (app->ui.picker_purpose == AKN_PICK_INVITE) {
        if (!party[0] || !safe_id(party) || !safe_id(id)) {
            akn_ui_toast(&app->ui, "You are not in a party any more.", now, 2500);
            return;
        }
        char body[96];
        snprintf(body, sizeof(body), "{\"userId\":\"%s\"}", id);
        act(app, AKN_ACT_PARTY_INVITE, "POST", "/api/v1/parties/%s/invitations", party, body, name, now);
    } else {
        message_user(app, id, name, app->ui.picker_back, now);
    }
}

static void row_action(akn_app *app, uint64_t now) {
    switch (app->ui.screen) {
    case AKN_SCREEN_PARTY: party_row(app, now); break;
    case AKN_SCREEN_INBOX: inbox_row(app, now); break;
    case AKN_SCREEN_SETTINGS: settings_row(app, now); break;
    case AKN_SCREEN_PICKER: picker_row(app, now); break;
    case AKN_SCREEN_UPDATES: updates_row(app, now); break;
    default: break;
    }
}

static void load_older(akn_app *app) {
    SDL_LockMutex(app->lock);
    akn_social *s = &app->social;
    int go = s->thread && s->thread->has_more && !s->older_loading;
    if (go) {
        s->older_loading = 1;
        s->want |= AKN_WANT_OLDER;
        SDL_CondSignal(app->social_cond);
    }
    SDL_UnlockMutex(app->lock);
}

static void chat_profile(akn_app *app, uint64_t now) {
    akn_conversation conv;
    SDL_LockMutex(app->lock);
    conv = app->social.thread_conv;
    SDL_UnlockMutex(app->lock);
    if (conv.kind != AKN_CONV_DIRECT || !conv.target_id[0]) {
        akn_ui_toast(&app->ui, conv.kind == AKN_CONV_PARTY ? "The party's members are on the Party screen." : "Group chats have no single profile.",
                     now, 2500);
        return;
    }
    app->ui.profile_back = AKN_SCREEN_CHAT;
    akn_ui_open_screen(&app->ui, AKN_SCREEN_PROFILE);
    akn_social_open_profile(app, conv.target_id, 0);
}

/* ---- Commands ---------------------------------------------------------------------------------- */

static void set_heard(akn_app *app, akn_heard heard) {
    akn_audio_result *r = latest_audio(app);
    if (!r) return;
    r->heard = heard;
    akn_logf(&app->log, AKN_LOG_INFO, "audio.heard", "test %d: %s", r->test_number, akn_heard_name(heard));
    akn_report_save(app);
}

static void run_command(akn_app *app, akn_cmd cmd, uint64_t now) {
    switch (cmd) {
    case AKN_CMD_NONE: break;
    case AKN_CMD_START_AUDIO: start_audio(app, now); break;
    case AKN_CMD_CANCEL_AUDIO:
        app->audio_cancel = 1;
        akn_logf(&app->log, AKN_LOG_INFO, "audio.cancel", "stopping the tone");
        break;
    case AKN_CMD_RECHECK:
        start_net_check(app);
        break;
    case AKN_CMD_SAVE_REPORT:
        if (akn_report_save(app) == 0) {
            char text[400];
            snprintf(text, sizeof(text), "Report saved to %s", app->report_path);
            akn_ui_toast(&app->ui, text, now, 3500);
        } else {
            akn_ui_toast(&app->ui, "The report could not be saved (see the log).", now, 3500);
        }
        break;
    case AKN_CMD_EXIT:
        akn_logf(&app->log, AKN_LOG_INFO, "app.exit", "exit requested");
        app->quit = 1;
        break;
    case AKN_CMD_HEARD_YES: set_heard(app, AKN_HEARD_YES); break;
    case AKN_CMD_HEARD_NO: set_heard(app, AKN_HEARD_NO); break;
    case AKN_CMD_HEARD_UNSURE: set_heard(app, AKN_HEARD_UNSURE); break;
    case AKN_CMD_OPENED_SCREEN:
        akn_logf(&app->log, AKN_LOG_INFO, "ui.screen", "%s", akn_screen_name(app->ui.screen));
        if (app->ui.screen == AKN_SCREEN_CONNECTION) run_net_check_if_never(app);
        opened_social_screen(app);
        break;
    case AKN_CMD_OPEN_PROFILE: open_row_profile(app, now); break;
    case AKN_CMD_OPEN_OWN_PROFILE: open_own_profile(app); break;
    case AKN_CMD_REFRESH: refresh_screen(app, now); break;
    case AKN_CMD_OPEN_CHAT: open_chat_row(app); break;
    case AKN_CMD_ROW_ACTION: row_action(app, now); break;
    case AKN_CMD_ROW_ADJUST: row_adjust(app); break;
    case AKN_CMD_MESSAGE_FRIEND: message_friend(app, now); break;
    case AKN_CMD_COMPOSE: compose(app, now); break;
    case AKN_CMD_KEYBOARD_SUBMIT: keyboard_submit(app, now); break;
    case AKN_CMD_KEYBOARD_CANCEL: SDL_StopTextInput(); break; /* the text stays as a draft */
    case AKN_CMD_CONFIRMED: confirmed(app, now); break;
    case AKN_CMD_LOAD_OLDER: load_older(app); break;
    case AKN_CMD_CHAT_PROFILE: chat_profile(app, now); break;
    }
}

/* ---- PC test scripts ----------------------------------------------------------------------------- */

static int sdl_button_of(akn_button b) {
    switch (b) {
    case AKN_BTN_CROSS: return SDL_CONTROLLER_BUTTON_A;
    case AKN_BTN_CIRCLE: return SDL_CONTROLLER_BUTTON_B;
    case AKN_BTN_SQUARE: return SDL_CONTROLLER_BUTTON_X;
    case AKN_BTN_TRIANGLE: return SDL_CONTROLLER_BUTTON_Y;
    case AKN_BTN_L1: return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    case AKN_BTN_R1: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    case AKN_BTN_L3: return SDL_CONTROLLER_BUTTON_LEFTSTICK;
    case AKN_BTN_R3: return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    case AKN_BTN_OPTIONS: return SDL_CONTROLLER_BUTTON_START;
    case AKN_BTN_TOUCHPAD: return SDL_CONTROLLER_BUTTON_TOUCHPAD;
    case AKN_BTN_UP: return SDL_CONTROLLER_BUTTON_DPAD_UP;
    case AKN_BTN_DOWN: return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    case AKN_BTN_LEFT: return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    case AKN_BTN_RIGHT: return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    default: return -1;
    }
}

static SDL_Joystick *g_vjoy; /* the test's handle on the virtual controller */

static int attach_virtual_pad(akn_app *app) {
    SDL_VirtualJoystickDesc desc;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "AKENO virtual controller";
    int index = SDL_JoystickAttachVirtualEx(&desc);
    if (index < 0) {
        akn_logf(&app->log, AKN_LOG_ERROR, "script.virtual_pad", "%s", SDL_GetError());
        return -1;
    }
    g_vjoy = SDL_JoystickOpen(index);
    app->virtual_pad = index;
    return g_vjoy ? 0 : -1;
}

static void detach_virtual_pad(akn_app *app) {
    if (g_vjoy) SDL_JoystickClose(g_vjoy);
    g_vjoy = NULL;
    if (app->virtual_pad >= 0) SDL_JoystickDetachVirtual(app->virtual_pad);
    app->virtual_pad = -1;
}

static void script_fail(akn_app *app, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void script_fail(akn_app *app, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->script_error, sizeof(app->script_error), fmt, ap);
    va_end(ap);
    akn_logf(&app->log, AKN_LOG_ERROR, "script.failed", "step %d: %s", app->step_index + 1, app->script_error);
    fprintf(stderr, "script failed at step %d: %s\n", app->step_index + 1, app->script_error);
    app->script_failed = 1;
    app->quit = 1;
}

/* Returns a screenshot path when this frame should be captured. */
static const char *run_script(akn_app *app, uint64_t now) {
    while (!app->quit && app->step_index < app->step_count) {
        akn_step *st = &app->steps[app->step_index];
        int next = 0;
        const char *shot = NULL;
        switch (st->kind) {
        case AKN_STEP_WAIT:
            if (app->step_phase == 0) app->step_until = now + (uint64_t)st->ms, app->step_phase = 1;
            else if (now >= app->step_until) next = 1;
            break;
        case AKN_STEP_PRESS:
        case AKN_STEP_HOLD:
            if (app->step_phase == 0) {
                akn_input_button(&app->input, &app->queue, st->button, AKN_SRC_SCRIPT, 1, now);
                app->step_until = now + (uint64_t)(st->kind == AKN_STEP_PRESS ? 60 : st->ms);
                app->step_phase = 1;
            } else if (app->step_phase == 1 && now >= app->step_until) {
                akn_input_button(&app->input, &app->queue, st->button, AKN_SRC_SCRIPT, 0, now);
                app->step_until = now + 60;
                app->step_phase = 2;
            } else if (app->step_phase == 2 && now >= app->step_until) {
                next = 1;
            }
            break;
        case AKN_STEP_PAD:
        case AKN_STEP_PADHOLD: {
            int b = sdl_button_of(st->button);
            if (!g_vjoy || b < 0) {
                script_fail(app, "no virtual controller for %s", akn_button_name(st->button));
                break;
            }
            if (app->step_phase == 0) {
                SDL_JoystickSetVirtualButton(g_vjoy, b, SDL_PRESSED);
                app->step_until = now + (uint64_t)(st->kind == AKN_STEP_PAD ? 60 : st->ms);
                app->step_phase = 1;
            } else if (app->step_phase == 1 && now >= app->step_until) {
                SDL_JoystickSetVirtualButton(g_vjoy, b, SDL_RELEASED);
                app->step_until = now + 60;
                app->step_phase = 2;
            } else if (app->step_phase == 2 && now >= app->step_until) {
                next = 1;
            }
            break;
        }
        case AKN_STEP_STICK:
            if (!g_vjoy) {
                script_fail(app, "no virtual controller");
                break;
            }
            if (app->step_phase == 0) {
                SDL_JoystickSetVirtualAxis(g_vjoy, SDL_CONTROLLER_AXIS_LEFTX, (Sint16)st->x);
                SDL_JoystickSetVirtualAxis(g_vjoy, SDL_CONTROLLER_AXIS_LEFTY, (Sint16)st->y);
                app->step_until = now + (uint64_t)st->ms;
                app->step_phase = 1;
            } else if (app->step_phase == 1 && now >= app->step_until) {
                SDL_JoystickSetVirtualAxis(g_vjoy, SDL_CONTROLLER_AXIS_LEFTX, 0);
                SDL_JoystickSetVirtualAxis(g_vjoy, SDL_CONTROLLER_AXIS_LEFTY, 0);
                app->step_until = now + 60;
                app->step_phase = 2;
            } else if (app->step_phase == 2 && now >= app->step_until) {
                next = 1;
            }
            break;
        case AKN_STEP_UNPLUG:
            detach_virtual_pad(app);
            next = 1;
            break;
        case AKN_STEP_PLUG:
            if (attach_virtual_pad(app) != 0) script_fail(app, "could not attach the virtual controller");
            next = 1;
            break;
        case AKN_STEP_SHOT:
            shot = st->arg;
            next = 1;
            break;
        case AKN_STEP_EXPECT_SCREEN:
            if (strcmp(akn_screen_name(app->ui.screen), st->arg) != 0)
                script_fail(app, "expected screen %s, found %s", st->arg, akn_screen_name(app->ui.screen));
            next = 1;
            break;
        case AKN_STEP_EXPECT_DIALOG: {
            const char *d = app->ui.dialog == AKN_DIALOG_EXIT       ? "exit"
                            : app->ui.dialog == AKN_DIALOG_HEARD    ? "heard"
                            : app->ui.dialog == AKN_DIALOG_KEYBOARD ? "keyboard"
                            : app->ui.dialog == AKN_DIALOG_CONFIRM  ? "confirm"
                                                                    : "none";
            if (strcmp(d, st->arg) != 0) script_fail(app, "expected dialog %s, found %s", st->arg, d);
            next = 1;
            break;
        }
        case AKN_STEP_EXPECT_FOCUS: {
            akn_item it;
            const char *f = akn_ui_focused(&app->ui, &it) ? akn_item_name(it.id) : "none";
            if (strcmp(f, st->arg) != 0) script_fail(app, "expected focus %s, found %s", st->arg, f);
            next = 1;
            break;
        }
        case AKN_STEP_UNTIL_AUDIO:
        case AKN_STEP_UNTIL_ACCOUNT:
        case AKN_STEP_UNTIL_IPC:
        case AKN_STEP_UNTIL_FRIENDS:
        case AKN_STEP_UNTIL_PROFILE:
        case AKN_STEP_UNTIL_ACTIVITY:
        case AKN_STEP_UNTIL_PICTURES: {
            if (app->step_phase == 0) app->step_until = now + (uint64_t)st->ms, app->step_phase = 1;
            SDL_LockMutex(app->lock);
            const akn_social *s = &app->social;
            int done;
            const char *what;
            switch (st->kind) {
            case AKN_STEP_UNTIL_AUDIO:
                done = !app->audio_running && !app->audio_done && app->audio_count > 0, what = "audio";
                break;
            case AKN_STEP_UNTIL_ACCOUNT: done = !app->net_running && app->account.checks > 0, what = "account"; break;
            case AKN_STEP_UNTIL_IPC: done = s->ipc != AKN_IPC_UNCHECKED, what = "ipc"; break;
            case AKN_STEP_UNTIL_FRIENDS:
                done = (s->friends_load == AKN_LOAD_OK || s->friends_load == AKN_LOAD_ERROR) && !(s->want & AKN_WANT_FRIENDS), what = "friends";
                break;
            case AKN_STEP_UNTIL_PROFILE:
                done = (s->profile_load == AKN_LOAD_OK || s->profile_load == AKN_LOAD_ERROR) && !(s->want & AKN_WANT_PROFILE), what = "profile";
                break;
            case AKN_STEP_UNTIL_PICTURES: done = akn_images_pending(app->images) == 0, what = "pictures"; break;
            default:
                done = (s->own_activity_load == AKN_LOAD_OK || s->own_activity_load == AKN_LOAD_ERROR) &&
                       !(s->want & AKN_WANT_OWN_ACTIVITY),
                what = "activity";
                break;
            }
            SDL_UnlockMutex(app->lock);
            if (done) next = 1;
            else if (now >= app->step_until) script_fail(app, "timed out waiting for %s", what);
            break;
        }
        case AKN_STEP_EXPECT_TEXT:
        case AKN_STEP_EXPECT_NOTEXT: {
            int found = akn_gfx_capture_contains(app->gfx, st->arg);
            if (found != (st->kind == AKN_STEP_EXPECT_TEXT))
                script_fail(app, "%s \"%s\" on screen", found ? "unexpected" : "missing", st->arg);
            next = 1;
            break;
        }
        case AKN_STEP_UNTIL_TEXT:
            if (app->step_phase == 0) app->step_until = now + (uint64_t)st->ms, app->step_phase = 1;
            if (app->frames > 0 && akn_gfx_capture_contains(app->gfx, st->arg)) next = 1;
            else if (now >= app->step_until) script_fail(app, "timed out waiting for \"%s\" on screen", st->arg);
            break;
        case AKN_STEP_SYNC: {
            char path[600];
            snprintf(path, sizeof(path), "%s/%s", app->options.sync_dir ? app->options.sync_dir : ".", st->arg);
            if (app->step_phase == 0) {
                printf("AKN-SYNC %s\n", st->arg);
                fflush(stdout);
                app->step_until = now + 60000;
                app->step_phase = 1;
            } else if (access(path, F_OK) == 0) {
                next = 1;
            } else if (now >= app->step_until) {
                script_fail(app, "no answer to sync %s", st->arg);
            }
            break;
        }
        case AKN_STEP_QUIT:
            app->quit = 1;
            next = 1;
            break;
        }
        if (!next) return shot;
        app->step_index++;
        app->step_phase = 0;
        if (shot) return shot; /* capture this frame before continuing */
    }
    if (app->step_count && app->step_index >= app->step_count) app->quit = 1;
    return NULL;
}

static void save_screenshot(akn_app *app, const char *path) {
    int w = app->output_w, h = app->output_h;
    uint8_t *px = malloc((size_t)w * h * 4);
    if (!px) return;
    if (SDL_RenderReadPixels(app->renderer, NULL, SDL_PIXELFORMAT_RGBA32, px, w * 4) == 0 &&
        akn_png_write(path, px, w, h, w * 4) == 0)
        akn_logf(&app->log, AKN_LOG_INFO, "script.shot", "%dx%d screenshot written", w, h);
    else akn_logf(&app->log, AKN_LOG_ERROR, "script.shot", "screenshot failed: %s", SDL_GetError());
    free(px);
}

/* ---- Main loop ------------------------------------------------------------------------------------ */

/* Hex in 8-digit words: readable, and never a 32+ digit run that the redaction would hide. */
static void hex_words(const unsigned char *data, size_t len, char *out, size_t out_len) {
    static const char hex[] = "0123456789abcdef";
    size_t o = 0;
    for (size_t i = 0; i < len && o + 4 < out_len; i++) {
        if (i && i % 4 == 0) out[o++] = ' ';
        out[o++] = hex[data[i] >> 4];
        out[o++] = hex[data[i] & 15];
    }
    out[o] = '\0';
}

static int background_mode(akn_app *app) {
    akn_logf(&app->log, AKN_LOG_WARN, "app.background",
             "not the foreground application: no screen, controller or audio is opened (start AKENO Native from the homebrew launcher)");
    akn_report_save(app);
    akn_platform_notify("AKENO Native must be started from the Homebrew Launcher. It was started as a background payload, "
                        "so it opened nothing and stopped. Report: /data/akeno-native/native-report.txt");
    return 0;
}

int akn_app_run(const akn_options *options, int *opened_devices) {
    *opened_devices = 0;
    akn_app *app = calloc(1, sizeof(*app));
    if (!app) return 1;
    app->options = *options;
    app->virtual_pad = -1;
    app->started_ms = akn_platform_now_ms();
    akn_platform_info_get(&app->platform);
    if (mkdir(app->platform.native_dir, 0700) != 0 && errno != EEXIST) { /* logged below once the log exists */
    }
    snprintf(app->log_path, sizeof(app->log_path), "%s/native.log", app->platform.native_dir);
    snprintf(app->report_path, sizeof(app->report_path), "%s/native-report.txt", app->platform.native_dir);
    int log_ok = akn_log_open(&app->log, app->log_path, 512 * 1024, akn_platform_now_ms) == 0;
    app->log.echo_stderr = options->script != NULL;
    app->lock = SDL_CreateMutex();
    akn_voice_init(&app->voice);
    akn_logf(&app->log, AKN_LOG_INFO, "app.start", "AKENO NETWORK Native %s, build %s, %s firmware %s (0x%08x)%s",
             AKN_NATIVE_VERSION, AKN_BUILD_ID, app->platform.name, app->platform.firmware, app->platform.firmware_raw,
             log_ok ? "" : "; log file unavailable");

    app->context = akn_platform_context(&app->context_raw);
    app->context_raw.launcher_arg = options->launcher_arg;
    akn_context_describe(&app->context_raw, app->context, app->context_text, sizeof(app->context_text));
    char raw_hex[96];
    hex_words(app->context_raw.appinfo_raw, 32, raw_hex, sizeof(raw_hex));
    akn_logf(&app->log, AKN_LOG_INFO, "app.context", "%s; app info head %s", app->context_text, raw_hex);
    if (!akn_context_allows_devices(&app->context_raw, app->context)) {
        int rc = background_mode(app);
        akn_log_close(&app->log);
        SDL_DestroyMutex(app->lock);
        free(app);
        return rc;
    }
    akn_upd_init(app); /* trial starts and rollback are counted before anything else can fail */
    if (app->context == AKN_CTX_BACKGROUND || app->context == AKN_CTX_UNKNOWN)
        akn_logf(&app->log, AKN_LOG_WARN, "app.context_disagrees", "started with --launcher but the process check says %s",
                 akn_context_name(app->context));

    if (akeno_global_init() != AKENO_OK)
        akn_logf(&app->log, AKN_LOG_ERROR, "tls.init_failed", "secure connections are unavailable (no random source?)");

    *opened_devices = 1;
    akn_platform_before_video();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        snprintf(app->video_error, sizeof(app->video_error), "%s", SDL_GetError());
        akn_logf(&app->log, AKN_LOG_ERROR, "video.init_failed", "SDL_Init: %s", app->video_error);
        akn_report_save(app);
        akn_platform_notify("AKENO Native could not open the screen. Report: /data/akeno-native/native-report.txt");
        akn_log_close(&app->log);
        return 2;
    }
    int ww = options->window_w > 0 ? options->window_w : AKN_SCREEN_W;
    int wh = options->window_h > 0 ? options->window_h : AKN_SCREEN_H;
    app->window = SDL_CreateWindow("AKENO NETWORK Native", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, SDL_WINDOW_SHOWN);
    if (app->window) {
        app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_PRESENTVSYNC);
        if (!app->renderer) app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    }
    char err[160] = "";
    if (app->renderer) {
        SDL_RenderSetLogicalSize(app->renderer, AKN_SCREEN_W, AKN_SCREEN_H);
        app->gfx = akn_gfx_create(app->renderer, (const unsigned char *)akn_font_regular,
                                  (size_t)(akn_font_regular_end - akn_font_regular - 1), (const unsigned char *)akn_font_bold,
                                  (size_t)(akn_font_bold_end - akn_font_bold - 1), err, sizeof(err));
    }
    if (!app->window || !app->renderer || !app->gfx) {
        snprintf(app->video_error, sizeof(app->video_error), "%s", err[0] ? err : SDL_GetError());
        akn_logf(&app->log, AKN_LOG_ERROR, "video.failed", "%s", app->video_error);
        akn_report_save(app);
        akn_platform_notify("AKENO Native could not open the screen. Report: /data/akeno-native/native-report.txt");
        if (app->renderer) SDL_DestroyRenderer(app->renderer);
        if (app->window) SDL_DestroyWindow(app->window);
        SDL_Quit();
        akn_log_close(&app->log);
        return 2;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(app->renderer, &info) == 0) snprintf(app->renderer_name, sizeof(app->renderer_name), "%s", info.name);
    snprintf(app->video_driver, sizeof(app->video_driver), "%s", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?");
    SDL_GetRendererOutputSize(app->renderer, &app->output_w, &app->output_h);
    akn_logf(&app->log, AKN_LOG_INFO, "video.ready", "driver %s, renderer %s, output %dx%d, logical %dx%d", app->video_driver,
             app->renderer_name, app->output_w, app->output_h, AKN_SCREEN_W, AKN_SCREEN_H);

    akn_ui_init(&app->ui);
    akn_input_init(&app->input);
    akn_queue_init(&app->queue);

    if (options->script) {
        char perr[160];
        app->step_count = akn_script_parse(options->script, app->steps, AKN_SCRIPT_MAX, perr, sizeof(perr));
        if (app->step_count < 0) {
            fprintf(stderr, "invalid script: %s\n", perr);
            app->step_count = 0;
            app->script_failed = 1;
            app->quit = 1;
        }
        for (int i = 0; i < app->step_count; i++)
            if (app->steps[i].kind == AKN_STEP_PAD || app->steps[i].kind == AKN_STEP_PADHOLD || app->steps[i].kind == AKN_STEP_STICK) {
                attach_virtual_pad(app);
                break;
            }
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) open_pad(app, i);
    if (!app->pad && SDL_NumJoysticks() == 0) akn_logf(&app->log, AKN_LOG_INFO, "controller.none", "no controller attached yet");

    start_net_check(app);
    if (akn_social_start(app) != 0) akn_logf(&app->log, AKN_LOG_ERROR, "social.start", "the social screens are unavailable");
    char key_path[320];
    snprintf(key_path, sizeof(key_path), "%s/native.key", app->platform.akeno_dir);
    app->images = akn_images_create(app->platform.daemon_port, key_path, &app->log, images_ready, app);
    app->images_started = app->images != NULL;
    if (!app->images) akn_logf(&app->log, AKN_LOG_WARN, "images.start", "pictures are unavailable; initials are shown instead");
    if (options->script) akn_gfx_capture_enable(app->gfx);

    app->last_frame_ms = akn_platform_now_ms();
    akn_lifecycle_init(&app->life, app->last_frame_ms, akn_platform_running_app());
    app->life_app_at = app->last_frame_ms;
    while (!app->quit) {
        uint64_t now = akn_platform_now_ms();
        life_tick(app, now);
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle_event(app, &e, now);
        sync_social(app);
        akn_voice_poll(app);
        akn_upd_poll(app, now);
        const char *shot = app->step_count ? run_script(app, now) : NULL;
        akn_input_tick(&app->input, &app->queue, now);
        akn_input_event ev;
        while (akn_queue_pop(&app->queue, &ev)) {
            if (ev.kind != AKN_EV_REPEAT)
                akn_logf(&app->log, AKN_LOG_DEBUG, "input", "%s %s (%s)", akn_button_name(ev.button),
                         ev.kind == AKN_EV_PRESS ? "pressed" : "released",
                         ev.source == AKN_SRC_PAD ? "controller" : ev.source == AKN_SRC_STICK ? "stick"
                                                 : ev.source == AKN_SRC_KEYBOARD ? "keyboard" : "script");
            run_command(app, akn_ui_handle(&app->ui, &ev, now), now);
        }
        run_command(app, akn_ui_tick(&app->ui, &app->input, now), now);
        collect_audio(app, now);
        poll_pad_status(app, now);

        uint64_t t0 = akn_platform_now_us();
        akn_gfx_capture_begin_frame(app->gfx);
        akn_images_pump(app->images, app->renderer);
        akn_screens_draw(app, now);
        double ms = (akn_platform_now_us() - t0) / 1000.0;
        app->frames++;
        app->frame_ms_avg = app->frames == 1 ? ms : app->frame_ms_avg * 0.98 + ms * 0.02;
        if (ms > app->frame_ms_max && app->frames > 10) app->frame_ms_max = ms;
        if (shot) save_screenshot(app, shot);
        SDL_RenderPresent(app->renderer);
        if (app->frames == 1) akn_logf(&app->log, AKN_LOG_INFO, "video.first_frame", "first frame presented (%.1f ms to draw)", ms);
    }

    /* Clean shutdown: leave voice (closes the microphone), stop the tone, close the port, the controller and the screen. */
    akn_voice_shutdown(app);
    akn_upd_shutdown(app);
    app->audio_cancel = 1;
    if (app->audio_thread) SDL_WaitThread(app->audio_thread, NULL);
    app->audio_thread = NULL;
    collect_audio(app, akn_platform_now_ms());
    int social_done = akn_social_stop(app);
    akn_images_get_stats(app->images, &app->images_final);
    int images_done = akn_images_destroy(app->images);
    app->images = NULL;
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 3; k++)
            if (app->badge_tex[i][k]) SDL_DestroyTexture(app->badge_tex[i][k]);
    SDL_LockMutex(app->lock);
    int net_busy = app->net_running || !social_done || !images_done;
    SDL_UnlockMutex(app->lock);
    akn_logf(&app->log, AKN_LOG_INFO, "app.stop", "%lu frames, average draw %.1f ms%s", app->frames, app->frame_ms_avg,
             app->script_failed ? "; test script FAILED" : "");
    akn_report_save(app);
    detach_virtual_pad(app);
    if (app->pad) SDL_GameControllerClose(app->pad);
    akn_gfx_destroy(app->gfx);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
    int code = app->script_failed ? 3 : app->exit_code;
    if (net_busy) {
        /* A connection check or a daemon request is still inside its (bounded) timeouts: let the
         * process end take it instead of waiting; its memory and the log stay valid until then. */
        if (app->net_thread) SDL_DetachThread(app->net_thread);
        return code;
    }
    if (app->net_thread) SDL_WaitThread(app->net_thread, NULL);
    if (app->social_cond) SDL_DestroyCond(app->social_cond);
    free(app->social.friends);
    free(app->social.convs);
    free(app->social.thread);
    free(app->social.joinable);
    free(app->social.inbox);
    SDL_Quit();
    akeno_global_cleanup();
    akn_log_close(&app->log);
    SDL_DestroyMutex(app->lock);
    free(app);
    return code;
}
