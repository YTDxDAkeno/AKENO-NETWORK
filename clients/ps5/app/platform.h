/*
 * The few things the console app needs from its platform. Every PS5 call used here is taken
 * from a sample of the pinned ps5-payload-sdk (see platform_ps5.c); nothing else is touched.
 */
#ifndef AKENO_APP_PLATFORM_H
#define AKENO_APP_PLATFORM_H

#include <stddef.h>

#include "game_detect.h"

typedef struct {
    const char *name;        /* "PlayStation 5" or the host OS name. */
    char firmware[16];       /* "12.20" on the PS5 when known, "" otherwise. */
    unsigned int firmware_raw;
    const char *notifications; /* How notify() delivers messages. */
    const char *browser;       /* How open_browser() works. */
    const char *game_detection; /* How list_apps() finds running games. */
    const char *actionable;     /* How notify_action() works. */
} akeno_platform_info;

/* Called once at startup. */
void akeno_platform_init(akeno_platform_info *info);
void akeno_platform_cleanup(void);

/* Shows a short system notification (plain UTF-8 text). Returns 0 when it was accepted. */
int akeno_platform_notify(const char *text);

/* Opens url in the system web browser. Returns 0 when the request was accepted. */
int akeno_platform_open_browser(const char *url);

/*
 * Lists processes with the application info the system reports for them (read-only). Returns the
 * number of entries written to out (at most max), or -1 when processes cannot be listed (err says
 * why). *total receives the number of processes seen.
 */
int akeno_platform_list_apps(akeno_proc_info *out, size_t max, size_t *total, char *err, size_t err_len);

/* Directory under which title metadata files are looked up ("" on the console), or NULL if none. */
const char *akeno_platform_metadata_root(void);

/*
 * Experimental: a notification with one button that should open url. Returns 0 when the system
 * accepted it, -1 when it refused, -2 when the function is not available on this system (on the
 * PS5: libSceNotification.sprx or sceNotificationSend could not be loaded at run time). Whether
 * the button opens the URL is not known before it is tried on a console. Callers fall back to
 * akeno_platform_notify() on any non-zero result.
 */
int akeno_platform_notify_action(const char *title, const char *text, const char *button, const char *url);

/* Short diagnostic text about the button notification function ("loaded", "not loaded", why it failed). */
const char *akeno_platform_actionable_status(void);

/* Writes one diagnostic line to the platform's debug channel (in addition to the log file). */
void akeno_platform_debug_line(const char *line);

/*
 * Experimental controller shortcut (Settings -> Quick Access, off by default). Read-only access to
 * the controller state, with the declarations of the PS5 joystick driver of ps5-payload-dev/SDL.
 * On the PS5 libScePad.sprx is loaded with dlopen() only when the user turns the shortcut on, so
 * the app starts exactly as without it. Nothing is ever written to the controller (no vibration,
 * light bar, focus or remapping calls). Whether a background payload receives button data while
 * a game or the browser has the focus is NOT known; that is what the hardware test answers.
 */
typedef struct {
    char method[160];      /* How the controller is read, for Diagnostics. */
    int library_loaded;    /* libScePad.sprx loaded and every needed function found. */
    int init_called;
    int init_rc;           /* scePadInit() result (once per process). */
    int user_id;           /* User whose controller is read; -1 when none was found. */
    char user_source[48];  /* "foreground user", "first logged-in user" or why none. */
    int open_rc;           /* scePadOpen() result: a handle (>= 0) or an error code. */
    int shared_handle;     /* scePadOpen said "already opened"; scePadGetHandle was used. */
    int handle;            /* Handle in use, -1 when closed. */
} akeno_pad_info;

/*
 * Opens the controller of the foreground user for reading. Returns 0 when it can be read, -1
 * when a call failed, -2 when the function is not available on this system (err says why; info
 * records every step either way).
 */
int akeno_platform_pad_open(akeno_pad_info *info, char *err, size_t err_len);
/*
 * Reads the current controller state: *buttons (AKENO_PAD_* bits of shortcut.h, plus any other
 * bits the system sets), *connected and the raw result *rc. Returns 0 when the read succeeded.
 */
int akeno_platform_pad_read(unsigned *buttons, int *connected, int *rc);
/* Closes the handle opened by akeno_platform_pad_open (the library stays loaded). */
void akeno_platform_pad_close(void);

#ifdef AKENO_EXPERIMENTAL_AUDIO
/*
 * Experimental audio research (PS5 voice prototypes A and C, docs/PS5_AUDIO_RESEARCH.md). Built
 * only into the separate "audio lab" executable; the release build contains none of this. Every
 * call is started by the user from the diagnostics page.
 */

/* Directory of the system's libraries ("/system/common/lib" on the PS5), or NULL if unknown. */
const char *akeno_platform_audio_library_dir(void);

/*
 * Loads library at run time and records in found[i] whether it exports names[i]. Only looks the
 * names up; nothing is called. Returns 0 when the library loaded, -1 otherwise (err says why).
 */
int akeno_platform_audio_lookup(const char *library, const char *const *names, int *found, size_t count, char *err, size_t err_len);

/*
 * Audio lab v2/v3: one output test through sceAudioOut with the declarations of the PS5 audio
 * driver of ps5-payload-dev/SDL. Every parameter stays inside what that driver uses (user 0xFF =
 * system, port type 0 = main, 48 kHz, S16/float mono/stereo, 256..2048 frames per call).
 * v3 adds read-only measurements of the test's own port (audio_lab_state.h): before the first
 * block, in the middle of the tone and before the port is closed.
 */
#include "audio_lab_state.h"
typedef struct {
    int user_id;           /* 0xFF (system), as the SDL driver */
    int port_type;         /* 0 (main), as the SDL driver */
    int format;            /* AUDIO_LAB_FORMAT_* (audio_lab_pcm.h) */
    unsigned grain;        /* frames per output call */
    unsigned rate;         /* 48000 */
    unsigned long frames;  /* total frames to submit (rounded up to whole calls) */
    /* Produces the next block; returns the number of frames in it that are not silent. */
    unsigned long (*fill)(void *user, void *block, unsigned frames, double *peak);
    void *fill_user;
    volatile int *cancel;  /* Set to stop early (the port is still closed properly). */
} akeno_audio_params;

typedef struct {
    int library_loaded;          /* libSceAudioOut.sprx loaded and the four functions found */
    int init_called;             /* sceAudioOutInit was called by this test (once per process) */
    int init_rc;                 /* its result (also the remembered result of an earlier call) */
    int open_called;
    int open_rc;                 /* sceAudioOutOpen result: a port handle (>= 1) or an error */
    unsigned long calls;         /* sceAudioOutOutput calls made */
    unsigned long calls_ok;      /* ... that returned >= 0 */
    int first_error_rc;          /* first negative sceAudioOutOutput result, 0 if none */
    unsigned long first_error_call;
    int last_output_rc;
    unsigned long frames_submitted; /* frames in accepted calls */
    unsigned long nonzero_frames;   /* ... of which not silent */
    double peak;                    /* largest absolute sample submitted (0..1) */
    long long wall_us;              /* from the first output call until the last returned */
    long long call_min_us, call_max_us, call_total_us; /* per accepted call, excluding the first */
    int buffer_aligned;             /* the block handed to the system is 64-byte aligned */
    int close_called;
    int close_rc;
    int cancelled;
    char error[200];
    int port_state_exported;     /* sceAudioOutGetPortState was found (looked up with dlsym) */
    int port_state_stopped;      /* it wrote outside its window once: never called again in this process */
    audio_lab_state_sample port_state[AUDIO_LAB_STATE_POINTS];
} akeno_audio_result;

/*
 * Runs one test (blocking, about frames / rate seconds). Returns 0 when every step succeeded, -1
 * when a call failed, -2 when the output functions are not available (result.error says why).
 */
int akeno_platform_audio_run(const akeno_audio_params *params, akeno_audio_result *result);
#endif

#endif
