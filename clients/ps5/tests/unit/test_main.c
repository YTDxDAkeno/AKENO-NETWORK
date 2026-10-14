#include <stdio.h>

#include "test.h"

int g_failures = 0;
int g_checks = 0;

void run_core_tests(void);
void run_events_tests(void);

int main(void) {
    run_core_tests();
    run_events_tests();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
