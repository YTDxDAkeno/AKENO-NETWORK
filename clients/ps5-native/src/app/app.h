/*
 * AKENO NETWORK Native: application state shared by the main loop (app.c), the screens
 * (screens.c) and the report (report.c).
 */
#ifndef AKN_APP_H
#define AKN_APP_H

#include <SDL.h>
#include <stdint.h>

#include "account.h"
#include "audio_test.h"
#include "context.h"
#include "gfx.h"
#include "input.h"
#include "log.h"
#include "akn_platform.h"
#include "script.h"
#include "social.h"
#include "ui.h"

#define AKN_NATIVE_VERSION "0.2.0-preview"
#ifndef AKN_BUILD_ID
#define AKN_BUILD_ID "dev"
#endif

#define AKN_AUDIO_HISTORY 8
#define AKN_SCRIPT_MAX 256

/* The daemon's native interface, as the social worker sees it (docs/PS5_NATIVE_PHASE2.md). */
typedef enum {
    AKN_IPC_UNCHECKED = 0,
    AKN_IPC_NO_DAEMON,     /* nothing (or something else) answers on the daemon port */
    AKN_IPC_NOT_SUPPORTED, /* the daemon has no native interface (the Alpha 0.5 release build) */
    AKN_IPC_NO_KEY,        /* the daemon's key file is missing */
    AKN_IPC_UNSAFE_KEY,    /* the key file is a link, readable by others, or malformed: not used */
    AKN_IPC_KEY_REJECTED,  /* the daemon refused the key (it restarted): read again */
    AKN_IPC_READY
} akn_ipc_state;

typedef enum { AKN_LOAD_NONE = 0, AKN_LOAD_LOADING, AKN_LOAD_OK, AKN_LOAD_ERROR } akn_load_state;

/* What the UI asks the social worker to load (akn_social.want). */
enum {
    AKN_WANT_STATE = 1 << 0,
    AKN_WANT_FRIENDS = 1 << 1,
    AKN_WANT_PROFILE = 1 << 2,
    AKN_WANT_OWN_ACTIVITY = 1 << 3,
    AKN_WANT_RECONNECT = 1 << 4
};

typedef struct {
    /* All fields: app->lock. */
    akn_ipc_state ipc;
    char ipc_detail[200];
    akn_daemon_info daemon;
    akn_friend_list *friends;  /* heap; replaced as a whole by the worker */
    akn_load_state friends_load;
    char friends_error[200];
    uint64_t friends_at;
    unsigned long friends_gen; /* changes whenever the list or its order changes */
    akn_profile profile;
    akn_load_state profile_load;
    char profile_error[200];
    char profile_handle[48];   /* user ID or Online ID asked for */
    int profile_own;
    akn_activity profile_activity;
    akn_load_state profile_activity_load;
    akn_activity own_activity;
    akn_load_state own_activity_load;
    unsigned want;
    int stop, busy, done;
    unsigned long long events_after;
    unsigned long requests, events, event_polls, errors, reconnects;
} akn_social;

typedef struct {
    int launcher_arg;    /* --launcher (homebrew.js) */
    const char *script;  /* PC tests: steps (AKENO_NATIVE_SCRIPT) */
    const char *sync_dir; /* PC tests: "sync" steps wait for a file here (AKENO_NATIVE_SYNC_DIR) */
    int window_w, window_h;
} akn_options;

typedef struct {
    /* Platform and process */
    akn_platform_info platform;
    akn_context_raw context_raw;
    akn_context_kind context;
    akn_options options;
    char context_text[200];

    /* Video */
    SDL_Window *window;
    SDL_Renderer *renderer;
    akn_gfx *gfx;
    char video_driver[32];
    char renderer_name[32];
    int output_w, output_h;
    char video_error[200];

    /* Input */
    akn_ui ui;
    akn_input input;
    akn_input_queue queue;
    SDL_GameController *pad;
    SDL_JoystickID pad_instance;
    char pad_name[64];
    char pad_error[200];   /* SDL_GetError() after a failed open: carries scePadOpen's code on the PS5 */
    int pad_seen;          /* a controller was attached at least once */
    int pads_attached;     /* SDL_CONTROLLERDEVICEADDED count */
    int pads_removed;
    unsigned long pad_events;
    akn_pad_status pad_status;
    uint64_t pad_status_at;
    int pad_disconnected;  /* overlay: the controller went away */
    int virtual_pad;       /* PC tests: SDL virtual controller index, -1 if none */

    /* Workers */
    SDL_mutex *lock;
    SDL_Thread *audio_thread;
    volatile int audio_cancel;
    int audio_running;     /* lock */
    int audio_done;        /* lock: a result is waiting for the "heard?" question */
    akn_audio_result audio_current; /* lock */
    akn_audio_result audio_history[AKN_AUDIO_HISTORY];
    int audio_count;
    uint64_t audio_last_end;
    SDL_Thread *net_thread;
    int net_running;       /* lock */
    akn_account_view account; /* lock */
    SDL_Thread *social_thread;
    SDL_cond *social_cond;
    akn_social social;     /* lock */
    char focus_friend_id[40]; /* main thread: keeps the focus on the same friend when the list reorders */
    unsigned long focus_friend_gen;

    /* Frames */
    unsigned long frames;
    double frame_ms_avg, frame_ms_max;
    uint64_t started_ms;
    uint64_t last_frame_ms;

    /* Logging and report */
    akn_log log;
    char log_path[320];
    char report_path[320];
    char last_saved[400];

    /* PC test script */
    akn_step steps[AKN_SCRIPT_MAX];
    int step_count, step_index;
    uint64_t step_until;
    int step_phase;
    int script_failed;
    char script_error[200];

    int quit;
    int exit_code;
} akn_app;

/* screens.c */
void akn_screens_draw(akn_app *app, uint64_t now_ms);

/* social.c: the worker that reads friends, profiles and activity from the daemon (GET only). */
int akn_social_start(akn_app *app);
/* Asks the worker to stop; returns 1 when it has finished (else it is inside a bounded request). */
int akn_social_stop(akn_app *app);
void akn_social_want(akn_app *app, unsigned bits);
/* handle: a user ID or Online ID. */
void akn_social_open_profile(akn_app *app, const char *handle, int own);
const char *akn_ipc_state_text(akn_ipc_state state);
/* Friends with a detected game, in list order: row -> index into app->social.friends (lock held). */
int akn_social_playing_index(const akn_social *s, int row);
int akn_social_playing_count(const akn_social *s);

/* report.c: the plain-text diagnostic report (no tokens, no server address). */
void akn_report_build(akn_app *app, char *buf, size_t len);
int akn_report_save(akn_app *app);

/* app.c */
/* Runs the app. *opened_devices is set when it ran as the foreground app (screen, controller,
 * audio); only then may the caller hand control back to the system (akn_platform_exit_to_system). */
int akn_app_run(const akn_options *options, int *opened_devices);

#endif
