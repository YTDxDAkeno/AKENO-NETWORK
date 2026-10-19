/*
 * What AKENO Party Voice tells others: the status document of the local control interface (the
 * plugin's voice page reads it) and the report for the owner's hardware test
 * (/data/akeno-party-voice/report.txt). Portable and unit-tested.
 *
 * The report separates eight outcomes and never claims more than was measured:
 *   NETWORK CONNECTED             the gateway accepted the voice pass, said ready and sent the participants
 *   REMOTE AUDIO RECEIVED         packets arrived and decoded into frames with sound
 *   AUDIO SUBMITTED               blocks with sound (speech or the test sound) were accepted by the output call
 *   AUDIO AUDIBLE                 never measured: only the owner's answer
 *   MICROPHONE OPEN               sceAudioInOpen returned a handle
 *   MICROPHONE CAPTURING          frames that are not exact zeros were captured
 *   MICROPHONE TRANSMITTING       frames with sound were encoded and sent to the gateway
 *   REMOTE USER HEARD MICROPHONE  never measured: only the friend's answer
 * It holds states, codes and counts only: no names, no IDs, no voice pass, no audio.
 */
#ifndef PV_STATUS_H
#define PV_STATUS_H

#include <stddef.h>

#include "buf.h"
#include "pv_engine.h"

int pv_evidence_network(const pv_status *s);
int pv_evidence_remote_audio(const pv_status *s);
int pv_evidence_submitted(const pv_status *s);
int pv_evidence_mic_open(const pv_status *s);
int pv_evidence_mic_capturing(const pv_status *s);
int pv_evidence_mic(const pv_status *s);     /* transmitting */
int pv_evidence_speaker(const pv_status *s); /* party speech decoded and submitted (0.1.0's "speaker output") */

/* One line for the page: "Connected · Listening · Microphone active". */
void pv_status_summary(const pv_status *s, char *out, size_t len);

/* The status document: {"ok":true,"app":...,"network":{...},"mic":{...},"output":{...},...}. */
int pv_status_json(const pv_status *s, akeno_buf *out);

/* The report; returns its length. */
size_t pv_report_text(const pv_status *s, char *out, size_t len);

/* Writes path through a temporary file and a rename. 0 on success. */
int pv_write_file_atomic(const char *path, const char *text, size_t len);

#endif
