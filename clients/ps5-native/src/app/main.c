/*
 * AKENO NETWORK Native entry point.
 *
 * PS5: started by a homebrew launcher (homebrew.js passes --launcher) as the foreground
 * application; returns to the system with sceSystemServiceLoadExec("exit") like SDL's own PS5
 * main, but only when it really ran in the foreground. Sent to an ELF loader instead, it detects
 * the background context, opens nothing and just ends (see app.c, background_mode).
 *
 * PC: a window for development; AKENO_NATIVE_SCRIPT runs an automated test (script.h),
 * AKENO_NATIVE_WINDOW=WxH sets the window size (default 1280x720; the 1920x1080 layout is scaled),
 * AKENO_NATIVE_SYNC_DIR is where a test harness confirms the script's "sync" steps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

int main(int argc, char **argv) {
    akn_options options;
    memset(&options, 0, sizeof(options));
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--launcher") == 0) options.launcher_arg = 1;
#ifndef AKN_PLATFORM_PS5
    options.script = getenv("AKENO_NATIVE_SCRIPT");
    options.sync_dir = getenv("AKENO_NATIVE_SYNC_DIR");
    const char *window = getenv("AKENO_NATIVE_WINDOW");
    if (!window || sscanf(window, "%dx%d", &options.window_w, &options.window_h) != 2) {
        options.window_w = 1280;
        options.window_h = 720;
    }
#endif
    int opened = 0;
    int code = akn_app_run(&options, &opened);
#ifdef AKN_PLATFORM_PS5
    if (opened) akn_platform_exit_to_system();
#endif
    return code;
}
