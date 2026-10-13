#ifndef AKENO_BACKOFF_H
#define AKENO_BACKOFF_H

/*
 * Reconnect delay for attempt n (0-based): exponential from base_ms, capped at max_ms, with
 * "equal jitter" (half fixed, half random) so many clients do not reconnect in lockstep.
 * random01 must be in [0, 1).
 */
int akeno_backoff_delay_ms(int attempt, int base_ms, int max_ms, double random01);

#endif
