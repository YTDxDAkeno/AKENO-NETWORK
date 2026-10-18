/*
 * AKENO Native's own on-screen keyboard: typing with the DualSense, drawn by the app itself. It
 * uses only what Native 0.1 proved on a PS5 with firmware 12.20 (the SDL renderer and the
 * controller) and no system dialog, so it adds no new system function to the ELF. A USB keyboard
 * also types into it where SDL delivers text input.
 *
 *   D-pad / left stick   move between keys          Cross     type the key
 *   Square               delete the last character  Triangle  space
 *   L1                   Shift (one letter)          R1        letters / symbols
 *   L2 / R2              move the cursor             Options   send (or done)
 *   Circle               close (the text is kept as a draft)
 *
 * Portable and unit-tested; the screen draws exactly the keys of akn_osk_key_at().
 */
#ifndef AKN_OSK_H
#define AKN_OSK_H

#include <stddef.h>

#include "input.h"

#define AKN_OSK_COLS 10
#define AKN_OSK_ROWS 5          /* four rows of characters, then the action row */
#define AKN_OSK_TEXT_MAX 2048   /* bytes */

typedef enum { AKN_OSK_LETTERS = 0, AKN_OSK_SYMBOLS } akn_osk_layer;

typedef enum {
    AKN_KEY_CHAR = 0,
    AKN_KEY_SHIFT,
    AKN_KEY_LAYER,
    AKN_KEY_SPACE,
    AKN_KEY_BACKSPACE,
    AKN_KEY_DONE
} akn_key_kind;

typedef struct {
    akn_key_kind kind;
    char text[8];   /* AKN_KEY_CHAR: the UTF-8 it types (as shown, Shift applied) */
    int col, span;  /* columns it covers in its row */
} akn_osk_key;

typedef enum { AKN_OSK_NONE = 0, AKN_OSK_CHANGED, AKN_OSK_SUBMIT, AKN_OSK_CANCEL } akn_osk_result;

typedef struct {
    char text[AKN_OSK_TEXT_MAX + 1];
    size_t len;        /* bytes */
    size_t cursor;     /* byte offset, always at a character boundary */
    int max_chars;     /* characters accepted (code points) */
    int single_line;   /* Enter is not offered (names) */
    int row, col;      /* the focused key */
    akn_osk_layer layer;
    int shift;         /* the next letter is upper case */
    int caps;          /* Shift pressed twice: upper case until pressed again */
    unsigned long keys_typed;
} akn_osk;

void akn_osk_init(akn_osk *k, const char *initial, int max_chars, int single_line);
/* The key at (row, col), following spans; 0 when there is none. */
int akn_osk_key_at(const akn_osk *k, int row, int col, akn_osk_key *out);
/* Keys in a row (by span), for drawing. */
int akn_osk_row_keys(const akn_osk *k, int row, akn_osk_key *out, int max);
/* One controller event. */
akn_osk_result akn_osk_handle(akn_osk *k, const akn_input_event *ev);
/* Text from a USB keyboard (SDL_TEXTINPUT); control characters are ignored. */
akn_osk_result akn_osk_insert(akn_osk *k, const char *utf8);
akn_osk_result akn_osk_backspace(akn_osk *k);
int akn_osk_chars(const akn_osk *k);

#endif
