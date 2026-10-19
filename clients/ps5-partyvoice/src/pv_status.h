/*
 * What AKENO Party Voice tells others: the status document of the local control interface (the
 * plugin's voice page reads it) and the report for the owner's hardware test
 * (/data/akeno-party-voice/report.txt). Portable and unit-tested.
 *
 * The report separates the three outcomes and never claims more than was measured:
 *   NETWORK CONNECTED        the gateway accepted the voice pass, said ready and sent the participants
 *   MICROPHONE TRANSMITTING  frames with sound were captured, encoded and sent (whether the friend
 *                            hears them is the friend's answer)
 *   SPEAKER OUTPUT ACTIVE    incoming audio was decoded and the output port took it (whether it is
 *                            audible is the owner's answer)
 * It holds states, codes and counts only: no names, no IDs, no voice pass, no audio.
 */
#ifndef PV_STATUS_H
#define PV_STATUS_H

#include <stddef.h>

#include "buf.h"
#include "pv_engine.h"

int pv_evidence_network(const pv_status *s);
int pv_evidence_mic(const pv_status *s);
int pv_evidence_speaker(const pv_status *s);

/* One line for the page: "Connected · Listening · Microphone active". */
void pv_status_summary(const pv_status *s, char *out, size_t len);

/* The status document: {"ok":true,"app":...,"network":{...},"mic":{...},"output":{...},...}. */
int pv_status_json(const pv_status *s, akeno_buf *out);

/* The report; returns its length. */
size_t pv_report_text(const pv_status *s, char *out, size_t len);

/* Writes path through a temporary file and a rename. 0 on success. */
int pv_write_file_atomic(const char *path, const char *text, size_t len);

#endif
