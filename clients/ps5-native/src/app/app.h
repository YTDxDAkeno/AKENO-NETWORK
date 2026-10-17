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
#include "ui.h"

#define AKN_NATIVE_VERSION "0.1.0-prototype"
#ifndef AKN_BUILD_ID
#define AKN_BUILD_ID "dev"
#endif

#define AKN_AUDIO_HISTORY 8
#define AKN_SCRIPT_MAX 256

typedef struct {
    int launcher_arg;    /* --launcher (homebrew.js) */
    const char *script;  /* PC tests: steps (AKENO_NATIVE_SCRIPT) */
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

/* report.c: the plain-text diagnostic report (no tokens, no server address). */
void akn_report_build(akn_app *app, char *buf, size_t len);
int akn_report_save(akn_app *app);

/* app.c */
/* Runs the app. *opened_devices is set when it ran as the foreground app (screen, controller,
 * audio); only then may the caller hand control back to the system (akn_platform_exit_to_system). */
int akn_app_run(const akn_options *options, int *opened_devices);

#endif
