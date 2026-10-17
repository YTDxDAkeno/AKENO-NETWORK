/*
 * Test scripts for the PC build (AKENO_NATIVE_SCRIPT): drive the app through the same input path
 * as a controller, take screenshots and assert state. Steps are separated by ';' or newlines:
 *
 *   wait <ms>                 pause
 *   press <button>            keyboard-equivalent press and release (cross, circle, up, ...)
 *   hold <button> <ms>        hold a button (repeat and hold gestures)
 *   pad <button>              press and release on the virtual SDL game controller
 *   padhold <button> <ms>     hold on the virtual controller
 *   stick <lx> <ly> <ms>      hold the virtual left stick at -32768..32767, then centre it
 *   unplug | plug             detach / re-attach the virtual controller
 *   shot <file.png>           screenshot of the 1920x1080 frame
 *   expect screen <name>      fail unless that screen is open (home, controller, audio, ...)
 *   expect dialog <name>      none, exit or heard
 *   expect focus <item>       fail unless that item has the focus (controller, audio-play, ...)
 *   until audio|account <ms>  wait until the audio test / connection check is finished
 *   until ipc|friends|profile|activity <ms>
 *                             wait until the daemon interface was checked / that data has loaded
 *   expect text <text...>     fail unless the last frame drew this text (PC capture of text runs)
 *   expect notext <text...>   fail if it did
 *   until text <ms> <text...> wait until a frame draws this text
 *   sync <label>              print "AKN-SYNC <label>" and wait (60 s at most) until the file
 *                             <AKENO_NATIVE_SYNC_DIR>/<label> exists: the harness acts in between
 *   quit                      leave like the Exit button does
 * Parsing is portable and unit-tested; execution lives in the app.
 */
#ifndef AKN_SCRIPT_H
#define AKN_SCRIPT_H

#include <stddef.h>

#include "input.h"

typedef enum {
    AKN_STEP_WAIT = 0,
    AKN_STEP_PRESS,
    AKN_STEP_HOLD,
    AKN_STEP_PAD,
    AKN_STEP_PADHOLD,
    AKN_STEP_STICK,
    AKN_STEP_UNPLUG,
    AKN_STEP_PLUG,
    AKN_STEP_SHOT,
    AKN_STEP_EXPECT_SCREEN,
    AKN_STEP_EXPECT_DIALOG,
    AKN_STEP_EXPECT_FOCUS,
    AKN_STEP_UNTIL_AUDIO,
    AKN_STEP_UNTIL_ACCOUNT,
    AKN_STEP_UNTIL_IPC,
    AKN_STEP_UNTIL_FRIENDS,
    AKN_STEP_UNTIL_PROFILE,
    AKN_STEP_UNTIL_ACTIVITY,
    AKN_STEP_EXPECT_TEXT,
    AKN_STEP_EXPECT_NOTEXT,
    AKN_STEP_UNTIL_TEXT,
    AKN_STEP_SYNC,
    AKN_STEP_QUIT
} akn_step_kind;

typedef struct {
    akn_step_kind kind;
    akn_button button;
    int ms;
    int x, y;
    char arg[200];
} akn_step;

/* Returns the number of steps, or -1 with a message in err. */
int akn_script_parse(const char *text, akn_step *steps, int max, char *err, size_t err_len);

#endif
