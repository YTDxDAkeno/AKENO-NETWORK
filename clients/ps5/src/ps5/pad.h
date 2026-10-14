#ifndef AKENO_PS5_PAD_H
#define AKENO_PS5_PAD_H

#include <stdint.h>

/* Opens the first controller of the active user. Returns a handle >= 0, or -1 on failure. */
int akeno_ps5_pad_open(void);
/* Returns the bitmask of buttons newly pressed since the previous call (edge-triggered). */
uint32_t akeno_ps5_pad_poll_pressed(int handle);
void akeno_ps5_pad_close(int handle);

#endif
