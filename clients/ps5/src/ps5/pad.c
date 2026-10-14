/*
 * Controller input via libScePad. We read only the button bitmask and the connected flag from
 * the raw ScePadData buffer at their documented offsets, so we do not depend on the full struct
 * layout. Presses are reported edge-triggered (newly pressed since the previous poll).
 */
#include "pad.h"

#include <string.h>

#include "sce.h"

/* The active user id; 0xFF (SCE_USER_SERVICE_USER_ID_INVALID) lets the pad library pick the
 * first logged-in user when combined with scePadGetHandle on some setups. We use the simple
 * open with the system user fallback used by homebrew samples. */
int sceUserServiceGetInitialUser(int *userId);

int akeno_ps5_pad_open(void) {
    if (scePadInit() != 0) return -1;
    int user_id = 0xFF;
    sceUserServiceGetInitialUser(&user_id);
    return scePadOpen(user_id, 0 /* SCE_PAD_PORT_TYPE_STANDARD */, 0, NULL);
}

uint32_t akeno_ps5_pad_poll_pressed(int handle) {
    static uint32_t previous = 0;
    unsigned char data[SCE_PAD_DATA_SIZE];
    memset(data, 0, sizeof data);
    if (handle < 0 || scePadReadState(handle, data) != 0) {
        previous = 0;
        return 0;
    }
    uint32_t buttons = 0;
    memcpy(&buttons, data + SCE_PAD_DATA_BUTTONS_OFFSET, sizeof buttons);
    uint32_t pressed = buttons & ~previous; /* 0 -> 1 transitions */
    previous = buttons;
    return pressed;
}

void akeno_ps5_pad_close(int handle) {
    if (handle >= 0) scePadClose(handle);
}
