/*
 * Logical controller input: merges DualSense buttons, the left stick and (on a PC) the keyboard
 * into press / repeat / release events, with D-pad repeat and a stick-to-D-pad mapping that
 * has hysteresis. Portable and unit-tested; timing comes from the caller.
 */
#ifndef AKN_INPUT_H
#define AKN_INPUT_H

#include <stdint.h>

typedef enum {
    AKN_BTN_UP = 0,
    AKN_BTN_DOWN,
    AKN_BTN_LEFT,
    AKN_BTN_RIGHT,
    AKN_BTN_CROSS,    /* confirm */
    AKN_BTN_CIRCLE,   /* back */
    AKN_BTN_SQUARE,
    AKN_BTN_TRIANGLE,
    AKN_BTN_L1,
    AKN_BTN_R1,
    AKN_BTN_L2,
    AKN_BTN_R2,
    AKN_BTN_L3,
    AKN_BTN_R3,
    AKN_BTN_OPTIONS,
    AKN_BTN_TOUCHPAD,
    AKN_BTN_COUNT
} akn_button;

typedef enum { AKN_SRC_PAD = 0, AKN_SRC_STICK, AKN_SRC_KEYBOARD, AKN_SRC_SCRIPT, AKN_SRC_COUNT } akn_source;

typedef enum { AKN_EV_PRESS = 0, AKN_EV_REPEAT, AKN_EV_RELEASE } akn_event_kind;

typedef struct {
    akn_button button;
    akn_event_kind kind;
    akn_source source;
} akn_input_event;

#define AKN_INPUT_QUEUE_LEN 64

typedef struct {
    akn_input_event ev[AKN_INPUT_QUEUE_LEN];
    int head, count;
    unsigned long dropped; /* events lost because the queue was full */
} akn_input_queue;

/* Stick axes in SDL's range (-32768..32767). */
enum { AKN_AXIS_LX = 0, AKN_AXIS_LY, AKN_AXIS_RX, AKN_AXIS_RY, AKN_AXIS_L2, AKN_AXIS_R2, AKN_AXIS_COUNT };

#define AKN_REPEAT_DELAY_MS 400u
#define AKN_REPEAT_INTERVAL_MS 90u
#define AKN_STICK_ENTER 16000 /* about 50 % deflection activates a direction */
#define AKN_STICK_EXIT 10000  /* below about 30 % it is released again */

typedef struct {
    uint32_t src_down[AKN_SRC_COUNT]; /* bit per button, per source */
    uint32_t down;                    /* union of all sources */
    uint64_t repeat_at[AKN_BTN_COUNT];
    int16_t axes[AKN_AXIS_COUNT];
    akn_button stick_dir;             /* direction held by the stick, or AKN_BTN_COUNT */
    unsigned long presses[AKN_BTN_COUNT];
    unsigned long total_events;
} akn_input;

void akn_input_init(akn_input *in);
void akn_queue_init(akn_input_queue *q);
/* Returns 1 and fills *out when an event was waiting. */
int akn_queue_pop(akn_input_queue *q, akn_input_event *out);

/* A button changed on one source. Emits PRESS when the first source presses it, RELEASE when the
 * last one lets go; repeated reports of the same state are ignored. */
void akn_input_button(akn_input *in, akn_input_queue *q, akn_button b, akn_source src, int down, uint64_t now_ms);
/* An axis moved. The left stick drives the D-pad through AKN_SRC_STICK (dominant axis only). */
void akn_input_axis(akn_input *in, akn_input_queue *q, int axis, int value, uint64_t now_ms);
/* Emits REPEAT events for held D-pad directions; call every frame. */
void akn_input_tick(akn_input *in, akn_input_queue *q, uint64_t now_ms);
/* Releases everything a source holds (controller disconnected, window lost focus). */
void akn_input_release_source(akn_input *in, akn_input_queue *q, akn_source src, uint64_t now_ms);

int akn_input_is_down(const akn_input *in, akn_button b);
const char *akn_button_name(akn_button b);
/* Parses the names returned by akn_button_name (case-insensitive); AKN_BTN_COUNT if unknown. */
akn_button akn_button_from_name(const char *name);

#endif
