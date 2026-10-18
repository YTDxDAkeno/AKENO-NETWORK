/*
 * PC implementation of the platform layer (development and automated tests). Paths and the
 * daemon port come from the environment so tests never touch a real console directory:
 *   AKENO_NATIVE_AKENO_DIR   the daemon's data directory (default ./akeno-data)
 *   AKENO_NATIVE_DIR         this app's own directory (default ./akeno-native-data)
 *   AKENO_NATIVE_DAEMON_PORT the daemon's port (default 8642)
 *   AKENO_NATIVE_SIM_AUDIO   init|open|output|unpaced: simulate an audio failure / no pacing
 *   AKENO_NATIVE_SIM_MIC     tone:<Hz>|silence|fail: a simulated microphone (unset: none)
 * Audio goes to SDL's default output device (SDL_AUDIODRIVER=dummy in CI).
 */
#define _POSIX_C_SOURCE 200809L

#include "akn_platform.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *env_or(const char *name, const char *fallback) {
    const char *v = getenv(name);
    return v && v[0] ? v : fallback;
}

void akn_platform_info_get(akn_platform_info *info) {
    memset(info, 0, sizeof(*info));
    info->name = "PC";
    snprintf(info->firmware, sizeof(info->firmware), "n/a");
    snprintf(info->akeno_dir, sizeof(info->akeno_dir), "%s", env_or("AKENO_NATIVE_AKENO_DIR", "akeno-data"));
    snprintf(info->native_dir, sizeof(info->native_dir), "%s", env_or("AKENO_NATIVE_DIR", "akeno-native-data"));
    int port = atoi(env_or("AKENO_NATIVE_DAEMON_PORT", "8642"));
    info->daemon_port = port > 0 && port < 65536 ? port : 8642;
}

uint64_t akn_platform_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

uint64_t akn_platform_now_ms(void) { return akn_platform_now_us() / 1000u; }

long long akn_platform_unix_time(void) { return (long long)time(NULL); }

akn_context_kind akn_platform_context(akn_context_raw *raw) {
    memset(raw, 0, sizeof(*raw));
    return AKN_CTX_DESKTOP;
}

int akn_platform_notify(const char *text) {
    fprintf(stderr, "[notification] %s\n", text);
    return 0;
}

void akn_platform_pad_status(akn_pad_status *status) { memset(status, 0, sizeof(*status)); }

void akn_platform_before_video(void) {}

void akn_platform_exit_to_system(void) {}

/* ---- Audio: an SDL output device, blocking like sceAudioOutOutput --------------------------- */

static struct {
    SDL_AudioDeviceID dev;
    unsigned block_bytes;
    unsigned grain;
} g_audio;

static const char *sim(void) { return env_or("AKENO_NATIVE_SIM_AUDIO", ""); }

static int32_t host_init(void *user) {
    (void)user;
    if (strcmp(sim(), "init") == 0) return (int32_t)0x80260001; /* simulated failure code */
    if (SDL_WasInit(SDL_INIT_AUDIO)) return AKN_AUDIO_ALREADY_INITIALIZED;
    return SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 ? 0 : -1;
}

static int32_t host_open(void *user, unsigned grain, unsigned rate, int format) {
    (void)user;
    if (strcmp(sim(), "open") == 0) return (int32_t)0x8026000A;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = (int)rate;
    want.format = (format == 3 || format == 4) ? AUDIO_F32SYS : AUDIO_S16SYS;
    want.channels = (format == 0 || format == 3) ? 1 : 2;
    want.samples = (Uint16)grain;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev == 0) return -1;
    g_audio.dev = dev;
    g_audio.grain = grain;
    g_audio.block_bytes = grain * want.channels * (want.format == AUDIO_F32SYS ? 4u : 2u);
    SDL_PauseAudioDevice(dev, 0);
    return (int32_t)dev;
}

static int32_t host_output(void *user, int32_t handle, const void *samples) {
    (void)user;
    if ((SDL_AudioDeviceID)handle != g_audio.dev) return (int32_t)0x80260003;
    if (strcmp(sim(), "output") == 0) return (int32_t)0x80260004;
    if (SDL_QueueAudio(g_audio.dev, samples, g_audio.block_bytes) != 0) return -1;
    if (strcmp(sim(), "unpaced") == 0) return 0;
    /* Block while more than two blocks wait, so calls are paced by the device like on the PS5. */
    uint64_t limit = akn_platform_now_ms() + 2000;
    while (SDL_GetQueuedAudioSize(g_audio.dev) > 2 * g_audio.block_bytes && akn_platform_now_ms() < limit) SDL_Delay(1);
    return 0;
}

static int32_t host_close(void *user, int32_t handle) {
    (void)user;
    if ((SDL_AudioDeviceID)handle != g_audio.dev || !g_audio.dev) return (int32_t)0x80260003;
    SDL_CloseAudioDevice(g_audio.dev);
    g_audio.dev = 0;
    return 0;
}

static uint64_t host_now_us(void *user) {
    (void)user;
    return akn_platform_now_us();
}

const akn_audio_ops *akn_platform_audio_ops(void) {
    static const akn_audio_ops ops = { host_init, host_open, host_output, host_close, host_now_us, NULL, "SDL audio device (PC)" };
    return &ops;
}

/* ---- Microphone: simulated (tests); a real PC has no microphone support in this build ---------- */

static struct {
    int hz;
    double phase;
    uint64_t next_us;
    unsigned grain;
} g_mic;

static int32_t sim_mic_open(unsigned grain, unsigned *rate) {
    const char *mode = env_or("AKENO_NATIVE_SIM_MIC", "");
    if (strcmp(mode, "fail") == 0) return (int32_t)0x80260105; /* simulated failure code */
    g_mic.hz = strncmp(mode, "tone:", 5) == 0 ? atoi(mode + 5) : 0;
    g_mic.phase = 0;
    g_mic.grain = grain;
    g_mic.next_us = akn_platform_now_us();
    *rate = 16000;
    return 1;
}

static int32_t sim_mic_read(int32_t handle, int16_t *samples) {
    if (handle != 1) return (int32_t)0x80260103;
    for (unsigned i = 0; i < g_mic.grain; i++) {
        samples[i] = g_mic.hz > 0 ? (int16_t)(9000.0 * sin(g_mic.phase)) : 0;
        g_mic.phase += 2.0 * 3.14159265358979323846 * g_mic.hz / 16000.0;
        if (g_mic.phase > 2.0 * 3.14159265358979323846) g_mic.phase -= 2.0 * 3.14159265358979323846;
    }
    /* paced like a real device: one grain per grain duration */
    g_mic.next_us += (uint64_t)g_mic.grain * 1000000u / 16000u;
    uint64_t now = akn_platform_now_us();
    if (g_mic.next_us > now) SDL_Delay((Uint32)((g_mic.next_us - now) / 1000u));
    else if (now - g_mic.next_us > 200000u) g_mic.next_us = now;
    return 0;
}

static int32_t sim_mic_close(int32_t handle) { return handle == 1 ? 0 : (int32_t)0x80260103; }

const akn_mic_ops *akn_platform_mic(char *why, size_t why_len) {
    static const akn_mic_ops ops = { sim_mic_open, sim_mic_read, sim_mic_close, "simulated microphone (PC test)" };
    if (!env_or("AKENO_NATIVE_SIM_MIC", "")[0]) {
        if (why && why_len) snprintf(why, why_len, "The PC build has no microphone (AKENO_NATIVE_SIM_MIC is not set).");
        return NULL;
    }
    return &ops;
}
