/*
 * The Audio Lab's control-file handling, as a portable module so the session boundary can be
 * unit-tested against real files (the critical "a stale command must not fire on restart" rule).
 *
 * Session boundary: al_control_begin() is called ONCE, after the lab has initialised and taken its
 * instance lock. It moves any control file present at that moment aside (to control.consumed), so
 * commands left over from a previous run can never be read as new commands. Only bytes appended
 * after this point are ever executed. al_control_read() then reads the fresh file from offset 0;
 * if the file shrank (replaced/truncated) it restarts from 0 and reports a reset.
 */
#ifndef AKENO_AL_CONTROL_H
#define AKENO_AL_CONTROL_H

#include <stddef.h>

/* Establishes the session boundary in <dir>: renames <dir>/control to <dir>/control.consumed if it
 * exists (best effort; a leftover command file is never read as commands afterwards). */
void al_control_begin(const char *dir);

/* Reads bytes appended to <dir>/control since the last call. *offset persists across calls and must
 * start at 0. Sets *reset to 1 when the file was replaced/truncated (reading restarted at 0).
 * Returns the number of bytes read into buf (0 if none / no file). */
int al_control_read(const char *dir, long *offset, char *buf, size_t len, int *reset);

#endif
