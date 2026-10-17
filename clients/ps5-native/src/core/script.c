#include "script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WORDS 4

static int parse_int(const char *s, int lo, int hi, int *out) {
    if (!s || !*s) return -1;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end || v < lo || v > hi) return -1;
    *out = (int)v;
    return 0;
}

static int parse_line(char *line, akn_step *st, char *err, size_t err_len) {
    char *words[MAX_WORDS] = { 0 };
    int n = 0;
    for (char *p = strtok(line, " \t"); p && n < MAX_WORDS; p = strtok(NULL, " \t")) words[n++] = p;
    if (n == 0) return 0;
    memset(st, 0, sizeof(*st));
    const char *cmd = words[0];
#define FAIL(...)                              \
    do {                                       \
        snprintf(err, err_len, __VA_ARGS__);   \
        return -1;                             \
    } while (0)
    if (strcmp(cmd, "wait") == 0) {
        st->kind = AKN_STEP_WAIT;
        if (n != 2 || parse_int(words[1], 0, 600000, &st->ms)) FAIL("wait <ms>");
    } else if (strcmp(cmd, "press") == 0 || strcmp(cmd, "pad") == 0) {
        st->kind = cmd[0] == 'p' && cmd[1] == 'r' ? AKN_STEP_PRESS : AKN_STEP_PAD;
        if (n != 2 || (st->button = akn_button_from_name(words[1])) == AKN_BTN_COUNT) FAIL("%s <button>", cmd);
    } else if (strcmp(cmd, "hold") == 0 || strcmp(cmd, "padhold") == 0) {
        st->kind = cmd[0] == 'h' ? AKN_STEP_HOLD : AKN_STEP_PADHOLD;
        if (n != 3 || (st->button = akn_button_from_name(words[1])) == AKN_BTN_COUNT || parse_int(words[2], 1, 60000, &st->ms))
            FAIL("%s <button> <ms>", cmd);
    } else if (strcmp(cmd, "stick") == 0) {
        st->kind = AKN_STEP_STICK;
        if (n != 4 || parse_int(words[1], -32768, 32767, &st->x) || parse_int(words[2], -32768, 32767, &st->y) ||
            parse_int(words[3], 1, 60000, &st->ms))
            FAIL("stick <lx> <ly> <ms>");
    } else if (strcmp(cmd, "unplug") == 0 || strcmp(cmd, "plug") == 0) {
        st->kind = cmd[0] == 'u' ? AKN_STEP_UNPLUG : AKN_STEP_PLUG;
        if (n != 1) FAIL("%s takes no argument", cmd);
    } else if (strcmp(cmd, "shot") == 0) {
        st->kind = AKN_STEP_SHOT;
        if (n != 2) FAIL("shot <file.png>");
        snprintf(st->arg, sizeof(st->arg), "%s", words[1]);
    } else if (strcmp(cmd, "expect") == 0) {
        if (n != 3) FAIL("expect screen|dialog|focus <name>");
        if (strcmp(words[1], "screen") == 0) st->kind = AKN_STEP_EXPECT_SCREEN;
        else if (strcmp(words[1], "dialog") == 0) st->kind = AKN_STEP_EXPECT_DIALOG;
        else if (strcmp(words[1], "focus") == 0) st->kind = AKN_STEP_EXPECT_FOCUS;
        else FAIL("expect screen|dialog|focus <name>");
        snprintf(st->arg, sizeof(st->arg), "%s", words[2]);
    } else if (strcmp(cmd, "until") == 0) {
        if (n != 3 || parse_int(words[2], 1, 600000, &st->ms)) FAIL("until audio|account <ms>");
        if (strcmp(words[1], "audio") == 0) st->kind = AKN_STEP_UNTIL_AUDIO;
        else if (strcmp(words[1], "account") == 0) st->kind = AKN_STEP_UNTIL_ACCOUNT;
        else FAIL("until audio|account <ms>");
    } else if (strcmp(cmd, "quit") == 0) {
        st->kind = AKN_STEP_QUIT;
        if (n != 1) FAIL("quit takes no argument");
    } else {
        FAIL("unknown step '%s'", cmd);
    }
#undef FAIL
    return 1;
}

int akn_script_parse(const char *text, akn_step *steps, int max, char *err, size_t err_len) {
    if (err_len) err[0] = '\0';
    if (!text) return 0;
    int count = 0;
    const char *p = text;
    int line_no = 0;
    while (*p) {
        const char *end = p;
        while (*end && *end != ';' && *end != '\n') end++;
        line_no++;
        char line[256];
        size_t len = (size_t)(end - p);
        if (len >= sizeof(line)) {
            snprintf(err, err_len, "step %d is too long", line_no);
            return -1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        char msg[160];
        akn_step st;
        int rc = parse_line(line, &st, msg, sizeof(msg));
        if (rc < 0) {
            snprintf(err, err_len, "step %d: %s", line_no, msg);
            return -1;
        }
        if (rc > 0) {
            if (count >= max) {
                snprintf(err, err_len, "more than %d steps", max);
                return -1;
            }
            steps[count++] = st;
        }
        p = *end ? end + 1 : end;
    }
    return count;
}
