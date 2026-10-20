/* PC simulation backend for the Audio Lab (al_backend_host.h). */
#define _POSIX_C_SOURCE 200809L
#include "al_backend.h"
#include "al_backend_host.h"
#include "al.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static long long h_now_unix(void *u) {
    al_sim *s = u;
    return s->real_time ? (long long)time(NULL) : s->unix0 + (long long)(s->vclock_us / 1000000u);
}

static uint64_t h_now_us(void *u) {
    al_sim *s = u;
    if (s->real_time) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
    }
    return s->vclock_us;
}

static void h_sleep_ms(void *u, unsigned ms) {
    al_sim *s = u;
    s->sleep_calls++;
    s->last_sleep_ms = ms;
    if (s->real_time) {
        struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    } else {
        s->vclock_us += (uint64_t)ms * 1000u;
    }
}

static int h_notify(void *u, const char *text) {
    al_sim *s = u;
    s->notifications++;
    snprintf(s->last_notification, sizeof(s->last_notification), "%s", text);
    return 0;
}

static int h_fw(void *u, uint32_t *raw) {
    al_sim *s = u;
    *raw = s->fw;
    return s->have_fw;
}

static int h_pid(void *u) {
    al_sim *s = u;
    return s->pid ? s->pid : 4242;
}

static int h_audio_run(void *u, const al_audio_params *p, al_audio_result *r) {
    al_sim *s = u;
    s->audio_runs++;
    memset(r, 0, sizeof(*r));
    r->call_min_us = -1;
    r->buffer_aligned = 1;
    if (s->audio_mode == 1) {
        r->library_loaded = 0;
        snprintf(r->error, sizeof(r->error), "libSceAudioOut.sprx not available (simulated)");
        return -2;
    }
    r->library_loaded = 1;
    r->init_called = 1;
    if (s->audio_mode == 2) {
        r->init_rc = (int32_t)0x80260001;
        snprintf(r->error, sizeof(r->error), "sceAudioOutInit failed (simulated)");
        return -1;
    }
    r->open_called = 1;
    if (s->audio_mode == 3) {
        r->open_rc = (int32_t)0x80260011;
        snprintf(r->error, sizeof(r->error), "sceAudioOutOpen returned 0x80260011 (simulated)");
        return -1;
    }
    r->open_rc = 42; /* a handle */
    unsigned long submitted = 0;
    unsigned calls = 0;
    for (unsigned long done = 0; done < p->frames; done += p->grain) {
        if (p->cancel && atomic_load(p->cancel)) {
            r->cancelled = 1;
            break;
        }
        double peak = 0.0;
        unsigned long nonzero = p->fill(p->fill_user, (void *)0, 0, &peak); /* host fill ignores block */
        (void)nonzero;
        calls++;
        r->calls = calls;
        if (s->audio_mode == 4 && calls == 3) {
            r->first_error_rc = (int32_t)0x80260003;
            r->first_error_call = calls;
            r->last_output_rc = r->first_error_rc;
            snprintf(r->error, sizeof(r->error), "sceAudioOutOutput returned 0x80260003 on call %u (simulated)", calls);
            r->close_called = 1;
            if (r->call_min_us < 0) r->call_min_us = 0;
            return -1;
        }
        r->calls_ok++;
        r->last_output_rc = 0;
        submitted += p->grain;
        if (peak > r->peak) r->peak = peak;
        if (calls > 1) {
            long long us = (long long)p->grain * 1000000LL / (long long)AUDIO_LAB_RATE; /* paced */
            r->call_total_us += us;
            if (r->call_min_us < 0 || us < r->call_min_us) r->call_min_us = us;
            if (us > r->call_max_us) r->call_max_us = us;
        }
    }
    r->frames_submitted = submitted;
    r->nonzero_frames = submitted; /* tone present */
    r->wall_us = (long long)submitted * 1000000LL / (long long)AUDIO_LAB_RATE;
    r->close_called = 1;
    r->close_rc = 0;
    if (r->call_min_us < 0) r->call_min_us = 0;
    return 0;
}

void al_backend_host(al_backend *be, void *sim) {
    al_backend b = { "PC simulation (not a PS5)", sim, h_now_unix, h_now_us, h_sleep_ms, h_notify, h_fw, h_pid,
                     h_audio_run };
    *be = b;
}
