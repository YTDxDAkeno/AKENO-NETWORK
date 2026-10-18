/* Voice Lab backends: the PS5 (backend_ps5.c) and a simulated console for the PC tests. */
#ifndef AKN_LAB_BACKEND_H
#define AKN_LAB_BACKEND_H

#include "lab.h"

/* PC simulation: what the "console" does. */
typedef struct {
    int mic_mode;            /* 0 speech-like sine, 1 all zeros, 2 open fails, 3 constant value */
    int game_after_polls;    /* the running app id becomes a game after this many queries; <0 never */
    int fail_port_types;     /* bit mask of port types whose open fails (1 << type) */
    int no_foreground_user;
    int pace;                /* output calls block for one block's duration */
    unsigned long notifications;
    unsigned long bigapp_polls;
    char last_notification[256];
} akn_lab_sim;

void akn_lab_backend_host(akn_lab_backend *be, akn_lab_sim *sim);

#ifdef AKN_LAB_PS5
void akn_lab_backend_ps5(akn_lab_backend *be);
#endif

#endif
