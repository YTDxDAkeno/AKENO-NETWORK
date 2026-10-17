/*
 * Quick access: the experimental controller shortcut (Settings -> Quick Access, off by default).
 *
 * While turned on, a worker thread reads the controller state every 50 ms through the platform
 * layer (read-only; on the PS5 scePadReadState from libScePad.sprx, loaded with dlopen) and feeds
 * the samples to the shortcut detector (shortcut.c). A deliberate hold of the chosen combination
 * makes the main thread open the running app's own page, exactly as a second launch of AKENO does
 * (no second instance, no new workers, pairing and realtime connection unchanged).
 *
 * What is NOT known before a hardware test: whether a background payload receives button data at
 * all while a game or the browser has the focus. The diagnostics count reads, samples with buttons
 * and detections so a single test answers it. A test mode reports detections without opening
 * anything. Nothing is ever written to the controller and no input is consumed or blocked.
 */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_internal.h"
#include "transport.h"

#define POLL_MS 50
#define REOPEN_AFTER_ERRORS 100 /* About 5 s of failed reads: open the controller again once. */
#define TEST_MAX_SECONDS 60

static int qa_should_stop(akeno_app *app) { return app->stop || app->qa_stop || app_exit_requested(); }

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void qa_set_state(akeno_app *app, const char *state, const char *detail) {
    pthread_mutex_lock(&app->lock);
    app_copy_str(app->qa.state, sizeof(app->qa.state), state);
    app_copy_str(app->qa.detail, sizeof(app->qa.detail), detail);
    pthread_mutex_unlock(&app->lock);
}

/* Opens the controller; records the result. Returns 0 when it can be read. */
static int qa_open(akeno_app *app) {
    akeno_pad_info info;
    char err[200] = "";
    int rc = akeno_platform_pad_open(&info, err, sizeof(err));
    pthread_mutex_lock(&app->lock);
    app->qa.pad = info;
    pthread_mutex_unlock(&app->lock);
    if (rc == 0) {
        qa_set_state(app, "reading", "Reading the controller state (read-only). Hold the combination to open AKENO.");
        app_log(app, AKENO_LOG_INFO, "quickaccess.reading", "Controller shortcut: reading the controller of the %s.", info.user_source);
    } else {
        qa_set_state(app, rc == -2 ? "unavailable" : "failed", err);
        app_log(app, AKENO_LOG_WARN, "quickaccess.unavailable", "Controller shortcut %s: %s", rc == -2 ? "unavailable" : "failed", err);
    }
    return rc;
}

static void *qa_main(void *arg) {
    akeno_app *app = arg;
    qa_set_state(app, "starting", "Opening the controller.");
    int ok = qa_open(app) == 0;
    akeno_shortcut detector;
    unsigned gen = 0;
    int have_config = 0;
    int consecutive_errors = 0;
    int reopened = 0;
    while (ok && !qa_should_stop(app)) {
        pthread_mutex_lock(&app->lock);
        if (!have_config || gen != app->qa.config_gen) {
            const akeno_shortcut_combo *combo = akeno_shortcut_combo_find(app->qa_combo);
            if (!combo) combo = akeno_shortcut_default_combo();
            akeno_shortcut_init(&detector, combo->mask, app->qa_hold_ms, AKENO_SHORTCUT_COOLDOWN_MS);
            gen = app->qa.config_gen;
            have_config = 1;
        }
        pthread_mutex_unlock(&app->lock);

        unsigned buttons = 0;
        int connected = 0, rc = 0;
        int read_ok = akeno_platform_pad_read(&buttons, &connected, &rc) == 0;
        long long now = akeno_now_ms();
        unsigned known = buttons & AKENO_PAD_KNOWN_BUTTONS;
        int fired = 0;
        if (read_ok) {
            consecutive_errors = 0;
            fired = akeno_shortcut_feed(&detector, known, now);
        } else {
            akeno_shortcut_reset_hold(&detector);
            consecutive_errors++;
        }
        pthread_mutex_lock(&app->lock);
        app_quick_access *qa = &app->qa;
        qa->last_read_rc = rc;
        if (read_ok) {
            qa->reads++;
            qa->connected = connected;
            if (known) {
                qa->samples_with_buttons++;
                qa->last_buttons = known;
                qa->last_buttons_at = now;
            }
            if (buttons & ~AKENO_PAD_KNOWN_BUTTONS) qa->other_bits_samples++;
        } else {
            qa->read_errors++;
        }
        int testing = qa->test_until > now;
        if (fired) {
            qa->detections++;
            qa->last_detection_at = now;
            if (testing) qa->test_detections++;
            else qa->trigger_pending = 1;
        }
        pthread_mutex_unlock(&app->lock);
        if (fired) {
            app_log(app, AKENO_LOG_INFO, "quickaccess.detected", "Controller shortcut detected%s.", testing ? " (test: nothing opened)" : "");
            app_push_event(app, "quickaccess.detected", testing ? "{\"test\":true}" : "{\"test\":false}");
        }
        if (consecutive_errors >= REOPEN_AFTER_ERRORS) {
            if (reopened) {
                char detail[200];
                snprintf(detail, sizeof(detail), "Reading the controller keeps failing (last result 0x%08x).", (unsigned)rc);
                qa_set_state(app, "failed", detail);
                app_log(app, AKENO_LOG_WARN, "quickaccess.failed", "%s", detail);
                break;
            }
            reopened = 1;
            consecutive_errors = 0;
            akeno_platform_pad_close();
            ok = qa_open(app) == 0;
        }
        sleep_ms(POLL_MS);
    }
    akeno_platform_pad_close();
    pthread_mutex_lock(&app->lock);
    if (strcmp(app->qa.state, "reading") == 0 || strcmp(app->qa.state, "starting") == 0) {
        app_copy_str(app->qa.state, sizeof(app->qa.state), "stopped");
        app_copy_str(app->qa.detail, sizeof(app->qa.detail), "The controller shortcut is off.");
    } else {
        app->qa.failed_gen = app->qa.config_gen; /* Not retried until the settings change or a test starts. */
    }
    app->qa_thread_done = 1;
    pthread_mutex_unlock(&app->lock);
    return NULL;
}

void app_quick_access_tick(akeno_app *app) {
    long long now = akeno_now_ms();
    pthread_mutex_lock(&app->lock);
    int done = app->qa_thread_running && app->qa_thread_done;
    int testing = app->qa.test_until > now;
    int wanted = (app->qa_shortcut || testing) && app->qa.failed_gen != app->qa.config_gen;
    int pending = app->qa.trigger_pending;
    app->qa.trigger_pending = 0;
    if (!app->qa_shortcut && !testing && !app->qa_thread_running && strcmp(app->qa.state, "off") != 0 &&
        strcmp(app->qa.state, "unavailable") != 0 && strcmp(app->qa.state, "failed") != 0) {
        app_copy_str(app->qa.state, sizeof(app->qa.state), "off");
        app_copy_str(app->qa.detail, sizeof(app->qa.detail), "The controller shortcut is off.");
    }
    pthread_mutex_unlock(&app->lock);

    if (done) {
        pthread_join(app->qa_thread, NULL);
        app->qa_thread_running = 0;
    }
    if (wanted && !app->qa_thread_running && !app->stop) {
        pthread_mutex_lock(&app->lock);
        app->qa_thread_done = 0;
        pthread_mutex_unlock(&app->lock);
        app->qa_stop = 0;
        if (app_start_worker(&app->qa_thread, qa_main, app) == 0) {
            app->qa_thread_running = 1;
        } else {
            pthread_mutex_lock(&app->lock);
            app_copy_str(app->qa.state, sizeof(app->qa.state), "failed");
            app_copy_str(app->qa.detail, sizeof(app->qa.detail), "Could not start the controller shortcut worker.");
            app->qa.failed_gen = app->qa.config_gen;
            pthread_mutex_unlock(&app->lock);
        }
    } else if (!wanted && app->qa_thread_running) {
        app->qa_stop = 1; /* Joined on a later tick, once the worker has closed the controller. */
    }

    if (pending) {
        /* Same as a second launch: the running app reopens its page (newest unseen message first). */
        char open[APP_OPEN_MAX];
        app_latest_message_target(app, open, sizeof(open));
        int rc = app_open_ui_at(app, open);
        pthread_mutex_lock(&app->lock);
        app->qa.activations++;
        app->qa.last_activation_at = akeno_now_ms();
        app->qa.last_browser_rc = rc;
        pthread_mutex_unlock(&app->lock);
        app_log(app, rc == 0 ? AKENO_LOG_INFO : AKENO_LOG_WARN, "quickaccess.opened", "Controller shortcut: %s.",
                rc == 0 ? "asked the system to show the AKENO page" : "the browser could not be opened");
        app_push_event(app, "quickaccess.opened", rc == 0 ? "{\"ok\":true}" : "{\"ok\":false}");
    }
}

void app_quick_access_test(akeno_app *app, int seconds) {
    if (seconds <= 0 || seconds > TEST_MAX_SECONDS) seconds = 30;
    pthread_mutex_lock(&app->lock);
    app->qa.test_until = akeno_now_ms() + (long long)seconds * 1000;
    app->qa.test_detections = 0;
    app->qa.config_gen++; /* A test also retries a reader that failed before. */
    pthread_mutex_unlock(&app->lock);
    app_log(app, AKENO_LOG_INFO, "quickaccess.test", "Controller shortcut test for %d s (nothing is opened).", seconds);
    app_quick_access_tick(app);
}

void app_quick_access_shutdown(akeno_app *app) {
    app->qa_stop = 1;
    if (app->qa_thread_running) {
        pthread_join(app->qa_thread, NULL);
        app->qa_thread_running = 0;
    }
}

static long long seconds_ago(long long now, long long at) { return at ? (now - at) / 1000 : -1; }

static void json_hex_or_null(akeno_buf *b, const char *key, int present, int value) {
    akeno_buf_printf(b, ",\"%s\":", key);
    if (present) akeno_buf_printf(b, "\"0x%08x\"", (unsigned)value);
    else akeno_buf_append_str(b, "null");
}

void app_quick_access_json(const akeno_app *app, akeno_buf *b) {
    const app_quick_access *qa = &app->qa;
    long long now = akeno_now_ms();
    const akeno_shortcut_combo *combo = akeno_shortcut_combo_find(app->qa_combo);
    if (!combo) combo = akeno_shortcut_default_combo();
    akeno_buf_printf(b, "{\"enabled\":%s,\"verifiedOnHardware\":false,\"combo\":", app->qa_shortcut ? "true" : "false");
    akeno_buf_append_json_string(b, combo->id);
    akeno_buf_append_str(b, ",\"comboLabel\":");
    akeno_buf_append_json_string(b, combo->label);
    akeno_buf_printf(b, ",\"holdMs\":%d,\"holds\":[1000,1500,2000],\"combos\":[", app->qa_hold_ms);
    size_t count = 0;
    const akeno_shortcut_combo *all = akeno_shortcut_combos(&count);
    for (size_t i = 0; i < count; i++) {
        if (i) akeno_buf_append_str(b, ",");
        akeno_buf_append_str(b, "{\"id\":");
        akeno_buf_append_json_string(b, all[i].id);
        akeno_buf_append_str(b, ",\"label\":");
        akeno_buf_append_json_string(b, all[i].label);
        akeno_buf_append_str(b, "}");
    }
    akeno_buf_append_str(b, "],\"state\":");
    akeno_buf_append_json_string(b, qa->state[0] ? qa->state : "off");
    akeno_buf_append_str(b, ",\"detail\":");
    akeno_buf_append_json_string(b, qa->detail[0] ? qa->detail : "The controller shortcut is off.");
    akeno_buf_append_str(b, ",\"method\":");
    akeno_buf_append_json_string(b, qa->pad.method[0] ? qa->pad.method : "not loaded (the shortcut is off)");
    akeno_buf_printf(b, ",\"library\":%s", qa->pad.library_loaded ? "true" : "false");
    json_hex_or_null(b, "initResult", qa->pad.library_loaded, qa->pad.init_rc);
    json_hex_or_null(b, "userId", qa->pad.user_id >= 0, qa->pad.user_id);
    akeno_buf_append_str(b, ",\"userSource\":");
    akeno_buf_append_json_string(b, qa->pad.user_source);
    json_hex_or_null(b, "openResult", qa->pad.user_id >= 0, qa->pad.open_rc);
    akeno_buf_printf(b, ",\"sharedHandle\":%s,\"reads\":%lld,\"readErrors\":%lld", qa->pad.shared_handle ? "true" : "false", qa->reads,
                     qa->read_errors);
    json_hex_or_null(b, "lastReadResult", qa->reads + qa->read_errors > 0, qa->last_read_rc);
    akeno_buf_printf(b,
                     ",\"connected\":%s,\"samplesWithButtons\":%lld,\"otherBitsSamples\":%lld,\"lastButtons\":\"0x%06x\","
                     "\"lastButtonsSecondsAgo\":%lld,\"detections\":%lld,\"lastDetectionSecondsAgo\":%lld,\"activations\":%lld,"
                     "\"lastActivationSecondsAgo\":%lld,\"lastBrowserResult\":%d",
                     qa->connected ? "true" : "false", qa->samples_with_buttons, qa->other_bits_samples, qa->last_buttons,
                     seconds_ago(now, qa->last_buttons_at), qa->detections, seconds_ago(now, qa->last_detection_at), qa->activations,
                     seconds_ago(now, qa->last_activation_at), qa->last_browser_rc);
    long long left = qa->test_until > now ? (qa->test_until - now + 999) / 1000 : 0;
    akeno_buf_printf(b, ",\"test\":{\"active\":%s,\"secondsLeft\":%lld,\"detections\":%lld}}", left > 0 ? "true" : "false", left,
                     qa->test_detections);
}
