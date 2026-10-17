#include "osk.h"

#include <stdio.h>
#include <string.h>

#include "text.h"

/* Four rows of ten characters per layer, then: Shift, ?123, Space (x4), Delete (x2), Done (x2). */
static const char *const LETTERS[4][AKN_OSK_COLS] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", "'" },
    { "z", "x", "c", "v", "b", "n", "m", ",", ".", "?" },
};
static const char *const SYMBOLS[4][AKN_OSK_COLS] = {
    { "!", "@", "#", "$", "%", "&", "*", "(", ")", "-" },
    { "_", "=", "+", ";", ":", "/", "\\", "\"", "<", ">" },
    { "[", "]", "{", "}", "^", "|", "~", "`", "\xE2\x82\xAC", "\xC2\xA3" },
    { "\xC3\xA9", "\xC3\xA8", "\xC3\xA0", "\xC3\xBC", "\xC3\xB6", "\xC3\xA4", "\xC3\xB1", "\xC3\xA7", "\xC3\x9F", "!" },
};

static const struct {
    akn_key_kind kind;
    int col, span;
} ACTIONS[] = {
    { AKN_KEY_SHIFT, 0, 1 }, { AKN_KEY_LAYER, 1, 1 }, { AKN_KEY_SPACE, 2, 4 }, { AKN_KEY_BACKSPACE, 6, 2 }, { AKN_KEY_DONE, 8, 2 },
};
#define ACTION_COUNT (int)(sizeof(ACTIONS) / sizeof(ACTIONS[0]))

int akn_osk_chars(const akn_osk *k) {
    int n = 0;
    for (size_t i = 0; i < k->len; i++)
        if (((unsigned char)k->text[i] & 0xC0) != 0x80) n++;
    return n;
}

void akn_osk_init(akn_osk *k, const char *initial, int max_chars, int single_line) {
    memset(k, 0, sizeof(*k));
    k->max_chars = max_chars > 0 ? max_chars : 1;
    k->single_line = single_line;
    k->row = 1;
    k->shift = 1; /* the first letter of a message is upper case */
    if (initial) akn_osk_insert(k, initial);
    if (k->len) k->shift = 0;
}

/* ASCII upper case of a one-letter key; other keys as they are. */
static void apply_shift(const char *in, int upper, char *out, size_t len) {
    snprintf(out, len, "%s", in);
    if (upper && in[0] >= 'a' && in[0] <= 'z' && in[1] == '\0') out[0] = (char)(in[0] - 'a' + 'A');
}

int akn_osk_key_at(const akn_osk *k, int row, int col, akn_osk_key *out) {
    memset(out, 0, sizeof(*out));
    if (row < 0 || row >= AKN_OSK_ROWS || col < 0 || col >= AKN_OSK_COLS) return 0;
    if (row < 4) {
        const char *t = k->layer == AKN_OSK_SYMBOLS ? SYMBOLS[row][col] : LETTERS[row][col];
        out->kind = AKN_KEY_CHAR;
        apply_shift(t, k->layer == AKN_OSK_LETTERS && (k->shift || k->caps), out->text, sizeof(out->text));
        out->col = col;
        out->span = 1;
        return 1;
    }
    for (int i = 0; i < ACTION_COUNT; i++)
        if (col >= ACTIONS[i].col && col < ACTIONS[i].col + ACTIONS[i].span) {
            out->kind = ACTIONS[i].kind;
            out->col = ACTIONS[i].col;
            out->span = ACTIONS[i].span;
            return 1;
        }
    return 0;
}

int akn_osk_row_keys(const akn_osk *k, int row, akn_osk_key *out, int max) {
    int n = 0;
    for (int col = 0; col < AKN_OSK_COLS && n < max;) {
        if (!akn_osk_key_at(k, row, col, &out[n])) break;
        col = out[n].col + out[n].span;
        n++;
    }
    return n;
}

static size_t prev_boundary(const char *s, size_t at) {
    if (at == 0) return 0;
    at--;
    while (at > 0 && ((unsigned char)s[at] & 0xC0) == 0x80) at--;
    return at;
}

static size_t next_boundary(const char *s, size_t len, size_t at) {
    if (at >= len) return len;
    at++;
    while (at < len && ((unsigned char)s[at] & 0xC0) == 0x80) at++;
    return at;
}

akn_osk_result akn_osk_insert(akn_osk *k, const char *utf8) {
    int changed = 0;
    size_t n = strlen(utf8);
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t step = akn_utf8_next(utf8 + i, n - i, &cp);
        if (step == 0) break;
        int invalid = cp == 0xFFFD && !(step == 3 && memcmp(utf8 + i, "\xEF\xBF\xBD", 3) == 0);
        int control = cp < 0x20 || cp == 0x7F;
        if (cp == '\n' && !k->single_line) control = 0;
        if (cp == '\t') cp = ' ', control = 0;
        if (!invalid && !control && akn_osk_chars(k) < k->max_chars && k->len + step <= AKN_OSK_TEXT_MAX) {
            const char *src = cp == ' ' ? " " : utf8 + i;
            memmove(k->text + k->cursor + step, k->text + k->cursor, k->len - k->cursor + 1);
            memcpy(k->text + k->cursor, src, step);
            k->len += step;
            k->cursor += step;
            k->keys_typed++;
            changed = 1;
        }
        i += step;
    }
    k->text[k->len] = '\0';
    return changed ? AKN_OSK_CHANGED : AKN_OSK_NONE;
}

akn_osk_result akn_osk_backspace(akn_osk *k) {
    if (k->cursor == 0) return AKN_OSK_NONE;
    size_t from = prev_boundary(k->text, k->cursor);
    memmove(k->text + from, k->text + k->cursor, k->len - k->cursor + 1);
    k->len -= k->cursor - from;
    k->cursor = from;
    if (k->len == 0) k->shift = 1;
    return AKN_OSK_CHANGED;
}

/* Moves within the grid; the action row's wide keys are one stop each. */
static void move(akn_osk *k, akn_button dir) {
    akn_osk_key key;
    akn_osk_key_at(k, k->row, k->col, &key);
    if (dir == AKN_BTN_UP) k->row = k->row > 0 ? k->row - 1 : AKN_OSK_ROWS - 1;
    else if (dir == AKN_BTN_DOWN) k->row = k->row < AKN_OSK_ROWS - 1 ? k->row + 1 : 0;
    else if (dir == AKN_BTN_LEFT) k->col = key.col > 0 ? key.col - 1 : AKN_OSK_COLS - 1;
    else if (dir == AKN_BTN_RIGHT) k->col = key.col + key.span < AKN_OSK_COLS ? key.col + key.span : 0;
    akn_osk_key_at(k, k->row, k->col, &key);
    if (dir == AKN_BTN_LEFT || dir == AKN_BTN_RIGHT) k->col = key.col; /* land on the start of a wide key */
}

static akn_osk_result press_key(akn_osk *k) {
    akn_osk_key key;
    if (!akn_osk_key_at(k, k->row, k->col, &key)) return AKN_OSK_NONE;
    switch (key.kind) {
    case AKN_KEY_CHAR: {
        akn_osk_result r = akn_osk_insert(k, key.text);
        if (r == AKN_OSK_CHANGED && k->layer == AKN_OSK_LETTERS) k->shift = 0;
        return r;
    }
    case AKN_KEY_SHIFT:
        if (k->caps) k->caps = 0, k->shift = 0;
        else if (k->shift) k->caps = 1;
        else k->shift = 1;
        return AKN_OSK_CHANGED;
    case AKN_KEY_LAYER: k->layer = k->layer == AKN_OSK_LETTERS ? AKN_OSK_SYMBOLS : AKN_OSK_LETTERS; return AKN_OSK_CHANGED;
    case AKN_KEY_SPACE: return akn_osk_insert(k, " ");
    case AKN_KEY_BACKSPACE: return akn_osk_backspace(k);
    case AKN_KEY_DONE: return AKN_OSK_SUBMIT;
    }
    return AKN_OSK_NONE;
}

akn_osk_result akn_osk_handle(akn_osk *k, const akn_input_event *ev) {
    if (ev->kind == AKN_EV_RELEASE) return AKN_OSK_NONE;
    int dir = ev->button <= AKN_BTN_RIGHT;
    int repeats = dir || ev->button == AKN_BTN_SQUARE || ev->button == AKN_BTN_L2 || ev->button == AKN_BTN_R2;
    if (ev->kind == AKN_EV_REPEAT && !repeats) return AKN_OSK_NONE;
    if (dir) {
        move(k, ev->button);
        return AKN_OSK_NONE;
    }
    switch (ev->button) {
    case AKN_BTN_CROSS: return press_key(k);
    case AKN_BTN_SQUARE: return akn_osk_backspace(k);
    case AKN_BTN_TRIANGLE: return akn_osk_insert(k, " ");
    case AKN_BTN_L1:
        if (k->layer != AKN_OSK_LETTERS) k->layer = AKN_OSK_LETTERS;
        k->shift = !k->shift;
        k->caps = 0;
        return AKN_OSK_CHANGED;
    case AKN_BTN_R1: k->layer = k->layer == AKN_OSK_LETTERS ? AKN_OSK_SYMBOLS : AKN_OSK_LETTERS; return AKN_OSK_CHANGED;
    case AKN_BTN_L2:
        if (k->cursor == 0) return AKN_OSK_NONE;
        k->cursor = prev_boundary(k->text, k->cursor);
        return AKN_OSK_CHANGED;
    case AKN_BTN_R2:
        if (k->cursor >= k->len) return AKN_OSK_NONE;
        k->cursor = next_boundary(k->text, k->len, k->cursor);
        return AKN_OSK_CHANGED;
    case AKN_BTN_OPTIONS: return AKN_OSK_SUBMIT;
    case AKN_BTN_CIRCLE: return AKN_OSK_CANCEL;
    default: return AKN_OSK_NONE;
    }
}
