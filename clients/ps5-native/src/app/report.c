#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "redact.h"

typedef struct {
    char *buf;
    size_t len, used;
} sb;

static void add(sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void add(sb *b, const char *fmt, ...) {
    if (b->used + 1 >= b->len) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->buf + b->used, b->len - b->used, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    b->used += (size_t)n;
    if (b->used >= b->len) b->used = b->len - 1;
}

static const char *yes_no(int v) { return v ? "yes" : "no"; }

void akn_report_build(akn_app *app, char *out, size_t out_len) {
    char *raw = malloc(out_len);
    if (!raw) {
        snprintf(out, out_len, "out of memory\n");
        return;
    }
    sb b = { raw, out_len, 0 };
    raw[0] = '\0';
    uint64_t now = akn_platform_now_ms();
    add(&b, "AKENO NETWORK Native - diagnostic report\n");
    add(&b, "========================================\n");
    add(&b, "Version:          %s\n", AKN_NATIVE_VERSION);
    add(&b, "Build:            %s\n", AKN_BUILD_ID);
    add(&b, "Platform:         %s, firmware %s (0x%08x)\n", app->platform.name, app->platform.firmware,
        app->platform.firmware_raw);
    add(&b, "Uptime:           %.1f s\n", (now - app->started_ms) / 1000.0);
    add(&b, "\n[Process]\n");
    add(&b, "Context:          %s\n", app->context_text);
    add(&b, "App info head:    ");
    for (int i = 0; i < 32; i++) add(&b, "%s%02x", i && i % 4 == 0 ? " " : "", app->context_raw.appinfo_raw[i]);
    add(&b, "\nDevices allowed:  %s\n", yes_no(akn_context_allows_devices(&app->context_raw, app->context)));

    add(&b, "\n[Video]\n");
    if (app->video_error[0]) add(&b, "Error:            %s\n", app->video_error);
    add(&b, "Driver:           %s\n", app->video_driver[0] ? app->video_driver : "not started");
    add(&b, "Renderer:         %s, output %dx%d, layout %dx%d\n", app->renderer_name[0] ? app->renderer_name : "-", app->output_w,
        app->output_h, AKN_SCREEN_W, AKN_SCREEN_H);
    add(&b, "Frames:           %lu, draw time avg %.1f ms, max %.1f ms\n", app->frames, app->frame_ms_avg, app->frame_ms_max);
    add(&b, "Glyph misses:     %lu\n", app->gfx ? akn_gfx_glyph_misses(app->gfx) : 0UL);

    add(&b, "\n[Controller]\n");
    add(&b, "Open:             %s\n", app->pad ? app->pad_name : (app->pad_seen ? "closed (went away)" : "no controller opened"));
    add(&b, "Open error:       %s\n", app->pad_error[0] ? app->pad_error : "none");
    add(&b, "Attached/removed: %d / %d events\n", app->pads_attached, app->pads_removed);
    add(&b, "Controller events:%lu (SDL button and axis events)\n", app->pad_events);
    if (app->pad_status.available)
        add(&b, "System pad state: user 0x%08x, handle %s, connected %s, rc 0x%08x\n", (unsigned)app->pad_status.user_id,
            yes_no(app->pad_status.has_handle), yes_no(app->pad_status.connected), (unsigned)app->pad_status.rc);
    else add(&b, "System pad state: not available on this platform\n");
    add(&b, "Presses:         ");
    for (int i = 0; i < AKN_BTN_COUNT; i++) add(&b, " %s=%lu", akn_button_name((akn_button)i), app->input.presses[i]);
    add(&b, "\nFocus moves:      %lu, confirms %lu, dropped events %lu\n", app->ui.focus_moves, app->ui.confirms, app->queue.dropped);

    add(&b, "\n[Audio]\n");
    add(&b, "Backend:          %s\n", akn_platform_audio_ops()->backend);
    add(&b, "Tests run:        %d\n", app->audio_count);
    int first = app->audio_count > AKN_AUDIO_HISTORY ? app->audio_count - AKN_AUDIO_HISTORY : 0;
    for (int i = first; i < app->audio_count; i++) {
        char text[1600];
        akn_audio_format(&app->audio_history[i - first], text, sizeof(text));
        add(&b, "%s", text);
    }

    akn_account_view acc;
    SDL_LockMutex(app->lock);
    acc = app->account;
    int net_running = app->net_running;
    SDL_UnlockMutex(app->lock);
    add(&b, "\n[Connection]\n");
    add(&b, "Checks:           %lu%s\n", acc.checks, net_running ? " (one running now)" : "");
    add(&b, "AKENO daemon:     %s%s%s\n", akn_daemon_state_text(acc.daemon), acc.daemon_version[0] ? " " : "", acc.daemon_version);
    add(&b, "Server:           %s\n", acc.server_desc ? acc.server_desc : "not checked");
    add(&b, "Credential file:  %s%s\n", acc.credential_present ? "present" : "missing",
        acc.credential_present ? (acc.credential_private ? ", private (0600)" : ", READABLE BY OTHERS") : "");
    add(&b, "Account:          %s\n", akn_account_state_text(acc.state));
    if (acc.state == AKN_ACC_SIGNED_IN) {
        add(&b, "Signed in as:     %s (@%s)\n", acc.display_name, acc.online_id);
        add(&b, "Device name:      %s\n", acc.device_name);
        add(&b, "Permissions:      %d: %s\n", acc.scope_count, acc.scopes);
        if (acc.have_skew) add(&b, "Clock offset:     %+lld s (server - console)\n", acc.clock_skew_s);
    }
    add(&b, "Last request:     library %s, HTTP %d %s, %u ms\n", acc.status_name[0] ? acc.status_name : "-", acc.http_status,
        acc.error_code, acc.duration_ms);
    add(&b, "Written by AKENO Native in the daemon's folder: nothing (read-only use)\n");

    /* Phase 2.1: counts and states only; other people's names never go into the report. */
    add(&b, "\n[Native interface]\n");
    SDL_LockMutex(app->lock);
    const akn_social *s = &app->social;
    add(&b, "State:            %s%s%s\n", akn_ipc_state_text(s->ipc), s->ipc_detail[0] ? ": " : "", s->ipc_detail);
    add(&b, "Daemon:           protocol %d, version %s, build %s, paired %s\n", s->daemon.ipc, s->daemon.version[0] ? s->daemon.version : "-",
        s->daemon.build[0] ? s->daemon.build : "-", s->daemon.paired ? "yes" : "no");
    add(&b, "Daemon realtime:  %s\n", s->daemon.realtime[0] ? s->daemon.realtime : "-");
    add(&b, "Game detection:   %s%s\n", s->daemon.activity_state[0] ? s->daemon.activity_state : "-",
        s->daemon.console_game.present ? " (a game is running)" : "");
    if (s->friends)
        add(&b, "Friends:          %d (%d online, %d playing), %d incoming and %d outgoing requests%s\n", s->friends->count, s->friends->online,
            s->friends->playing, s->friends->incoming_requests, s->friends->outgoing_requests, s->friends->truncated ? ", list cut" : "");
    else add(&b, "Friends:          not loaded%s%s\n", s->friends_error[0] ? ": " : "", s->friends_error);
    add(&b, "Profile:          %s\n", s->profile_load == AKN_LOAD_OK ? (s->profile_own ? "own profile loaded" : "a friend's profile loaded")
                                      : s->profile_load == AKN_LOAD_ERROR ? s->profile_error : "not opened");
    add(&b, "Requests:         %lu (GET only), %lu errors, %lu event polls, %lu events, %lu connects\n", s->requests, s->errors,
        s->event_polls, s->events, s->reconnects);
    SDL_UnlockMutex(app->lock);

    add(&b, "\n[Recent log]\n");
    char lines[AKN_LOG_RING][AKN_LOG_LINE];
    int n = akn_log_recent(&app->log, lines, AKN_LOG_RING);
    for (int i = 0; i < n; i++) add(&b, "%s\n", lines[i]);
    akn_redact(out, out_len, raw);
    free(raw);
}

int akn_report_save(akn_app *app) {
    size_t len = 32 * 1024;
    char *text = malloc(len);
    if (!text) return -1;
    akn_report_build(app, text, len);
    char tmp[340];
    snprintf(tmp, sizeof(tmp), "%s.tmp", app->report_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int rc = -1;
    if (fd >= 0) {
        size_t n = strlen(text);
        int ok = write(fd, text, n) == (ssize_t)n;
        ok = close(fd) == 0 && ok;
        if (ok && rename(tmp, app->report_path) == 0) rc = 0;
        else unlink(tmp);
    }
    free(text);
    if (rc == 0) snprintf(app->last_saved, sizeof(app->last_saved), "Saved %s", app->report_path);
    else akn_logf(&app->log, AKN_LOG_ERROR, "report.write_failed", "could not write the report");
    return rc;
}
