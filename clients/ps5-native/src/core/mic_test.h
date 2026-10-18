/*
 * The result of a local microphone test, kept in its own file so a later start (for example an
 * accidental background launch, which opens no microphone) cannot overwrite it in the report.
 * Counts, levels and result codes only: no audio and no credentials.
 */
#ifndef AKN_MIC_TEST_H
#define AKN_MIC_TEST_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AKN_MIC_TEST_OPENED = 0,     /* the microphone opened (see frames and levels) */
    AKN_MIC_TEST_UNAVAILABLE,    /* no microphone function on this system */
    AKN_MIC_TEST_OPEN_FAILED     /* opening failed: see open_rc */
} akn_mic_test_open;

typedef struct {
    char build[64];
    char backend[160];
    akn_mic_test_open open;
    int32_t open_rc;
    unsigned rate;               /* Hz the microphone port opened with */
    unsigned grain;              /* frames per read */
    unsigned long frames;        /* 20 ms frames captured */
    int peak_percent;            /* 0..100 */
    int level_max;               /* 0..100, loudest meter value */
    int level_avg;               /* 0..100, average meter value */
    unsigned long read_errors;
    int32_t last_read_rc;        /* 0, or the last failed read's code */
    int closed;                  /* the microphone was closed when the test ended */
    unsigned long duration_ms;
} akn_mic_test_record;

/* Whether the test heard something: opened, captured frames, closed again. */
int akn_mic_test_succeeded(const akn_mic_test_record *r);

/* Writes the record as "key: value" lines (the file format). Returns the length, or -1 if it does not fit. */
int akn_mic_test_format(const akn_mic_test_record *r, char *out, size_t out_len);

/* Reads a record written by akn_mic_test_format. Returns 0, or -1 for anything else. */
int akn_mic_test_parse(const char *text, akn_mic_test_record *r);

/* Adds the record to a diagnostic report as indented, human-readable lines. */
int akn_mic_test_describe(const akn_mic_test_record *r, char *out, size_t out_len);

#endif
