/*
 * PC simulation of the PS5 for the Audio Lab's tests. Not a PS5: a passing simulated run is never
 * evidence of console behaviour. Every console answer can be bent to a failure the engine and the
 * reports must handle.
 */
#ifndef AKENO_AL_BACKEND_HOST_H
#define AKENO_AL_BACKEND_HOST_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int real_time;               /* 0: virtual clock; 1: wall clock */
    uint64_t vclock_us;
    long long unix0;

    int have_fw;
    uint32_t fw;
    int pid;

    /* audio_run behaviour */
    int audio_mode;              /* 0 accepted+paced; 1 not-supported; 2 init-fail; 3 open-fail; 4 submit-fail */
    /* capture */
    unsigned long notifications;
    char last_notification[256];
    unsigned audio_runs;
    unsigned sleep_calls;
    unsigned last_sleep_ms;
} al_sim;

#endif
