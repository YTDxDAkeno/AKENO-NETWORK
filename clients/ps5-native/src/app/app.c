#define _POSIX_C_SOURCE 200809L

#include "app.h"

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
        if (e->key.repeat) break; /* AKENO repeats D-pad directions itself */
        akn_button b = map_key(e->key.keysym.sym);
        if (b != AKN_BTN_COUNT) akn_input_button(&app->input, &app->queue, b, AKN_SRC_KEYBOARD, e->type == SDL_KEYDOWN, now);
        break;
    }
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) akn_input_release_source(&app->input, &app->queue, AKN_SRC_KEYBOARD, now);
        break;
    default: break;
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

/* ---- Social screens ---------------------------------------------------------------------------- */

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
    akn_ui_set_list_count(ui, AKN_SCREEN_ACTIVITY, akn_social_playing_count(s));
    int f = ui->focus[AKN_SCREEN_FRIENDS];
    if (s->friends && f >= 0 && f < s->friends->count) snprintf(app->focus_friend_id, sizeof(app->focus_friend_id), "%s", s->friends->items[f].id);
    else app->focus_friend_id[0] = '\0';
    SDL_UnlockMutex(app->lock);
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
    unsigned want = AKN_WANT_RECONNECT;
    if (app->ui.screen == AKN_SCREEN_FRIENDS) want |= AKN_WANT_FRIENDS;
    else if (app->ui.screen == AKN_SCREEN_PROFILE) want |= AKN_WANT_PROFILE;
    else if (app->ui.screen == AKN_SCREEN_ACTIVITY) want |= AKN_WANT_STATE | AKN_WANT_OWN_ACTIVITY | AKN_WANT_FRIENDS;
    SDL_LockMutex(app->lock);
    if (app->social.ipc == AKN_IPC_READY) want &= ~(unsigned)AKN_WANT_RECONNECT;
    SDL_UnlockMutex(app->lock);
    akn_social_want(app, want);
    akn_ui_toast(&app->ui, "Loading again...", now, 1500);
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
        if (app->ui.screen == AKN_SCREEN_CONNECTION) {
            SDL_LockMutex(app->lock);
            int never = app->account.checks == 0 && !app->net_running;
            SDL_UnlockMutex(app->lock);
            if (never) start_net_check(app);
        }
        opened_social_screen(app);
        break;
    case AKN_CMD_OPEN_PROFILE: open_row_profile(app, now); break;
    case AKN_CMD_OPEN_OWN_PROFILE: open_own_profile(app); break;
    case AKN_CMD_REFRESH: refresh_screen(app, now); break;
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
            const char *d = app->ui.dialog == AKN_DIALOG_EXIT ? "exit" : app->ui.dialog == AKN_DIALOG_HEARD ? "heard" : "none";
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
        case AKN_STEP_UNTIL_ACTIVITY: {
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
    if (options->script) akn_gfx_capture_enable(app->gfx);

    app->last_frame_ms = akn_platform_now_ms();
    while (!app->quit) {
        uint64_t now = akn_platform_now_ms();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handle_event(app, &e, now);
        sync_social(app);
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
        akn_screens_draw(app, now);
        double ms = (akn_platform_now_us() - t0) / 1000.0;
        app->frames++;
        app->frame_ms_avg = app->frames == 1 ? ms : app->frame_ms_avg * 0.98 + ms * 0.02;
        if (ms > app->frame_ms_max && app->frames > 10) app->frame_ms_max = ms;
        if (shot) save_screenshot(app, shot);
        SDL_RenderPresent(app->renderer);
        if (app->frames == 1) akn_logf(&app->log, AKN_LOG_INFO, "video.first_frame", "first frame presented (%.1f ms to draw)", ms);
    }

    /* Clean shutdown: stop the tone, close the port, the controller and the screen. */
    app->audio_cancel = 1;
    if (app->audio_thread) SDL_WaitThread(app->audio_thread, NULL);
    app->audio_thread = NULL;
    collect_audio(app, akn_platform_now_ms());
    int social_done = akn_social_stop(app);
    SDL_LockMutex(app->lock);
    int net_busy = app->net_running || !social_done;
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
    SDL_Quit();
    akeno_global_cleanup();
    akn_log_close(&app->log);
    SDL_DestroyMutex(app->lock);
    free(app);
    return code;
}
