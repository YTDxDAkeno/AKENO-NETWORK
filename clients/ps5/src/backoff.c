#include "backoff.h"

int akeno_backoff_delay_ms(int attempt, int base_ms, int max_ms, double random01) {
    if (base_ms < 1) base_ms = 1;
    if (max_ms < base_ms) max_ms = base_ms;
    if (attempt < 0) attempt = 0;
    long long delay = base_ms;
    for (int i = 0; i < attempt && delay < max_ms; i++) delay *= 2;
    if (delay > max_ms) delay = max_ms;
    if (random01 < 0) random01 = 0;
    if (random01 >= 1) random01 = 0.999999;
    long long half = delay / 2;
    return (int)(half + (long long)((double)(delay - half) * random01));
}
