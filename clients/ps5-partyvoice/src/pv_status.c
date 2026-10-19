#define _POSIX_C_SOURCE 200809L
#include "pv_status.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pv.h"

#define SEP " \xC2\xB7 " /* " · " */

int pv_evidence_network(const pv_status *s) { return s->ever_connected; }
int pv_evidence_remote_audio(const pv_status *s) { return s->received > 0 && s->decoded_nonzero > 0; }
int pv_evidence_submitted(const pv_status *s) { return s->out_speech_blocks > 0 || s->test_tone_blocks > 0; }
int pv_evidence_mic_open(const pv_status *s) { return s->mic_open_ok > 0; }
int pv_evidence_mic_capturing(const pv_status *s) { return s->mic_nonzero_frames > 0; }
int pv_evidence_mic(const pv_status *s) { return s->sent_nonzero > 0; }
int pv_evidence_speaker(const pv_status *s) { return s->decoded_nonzero > 0 && s->out_speech_blocks > 0; }

void pv_status_summary(const pv_status *s, char *out, size_t len) {
    const char *net;
    switch (s->net) {
    case PV_NET_OFF: net = "Not in voice"; break;
    case PV_NET_REQUESTING:
    case PV_NET_CONNECTING: net = "Connecting"; break;
    case PV_NET_CONNECTED: net = "Connected"; break;
    case PV_NET_RECONNECTING: net = "Reconnecting"; break;
    case PV_NET_ENDED: net = "Disconnected"; break;
    default: net = "Not in voice"; break;
    }
    if (s->net == PV_NET_OFF || s->net == PV_NET_ENDED) {
        snprintf(out, len, "%s", net);
        return;
    }
    const char *speaker = s->deafened ? "Deafened"
                          : s->out == PV_OUT_FAILED ? "Audio output unavailable"
                          : s->out == PV_OUT_ACTIVE && s->net == PV_NET_CONNECTED ? "Listening"
                          : "Speaker starting";
    const char *mic = s->mic == PV_MIC_ACTIVE ? (s->muted || s->deafened ? "Microphone muted" : "Microphone active")
                      : s->mic == PV_MIC_OPENING ? "Microphone starting"
                      : s->mic == PV_MIC_UNAVAILABLE || s->mic == PV_MIC_FAILED ? "Microphone unavailable"
                      : "Microphone off";
    snprintf(out, len, "%s" SEP "%s" SEP "%s", net, speaker, mic);
}

static const char *b(int v) { return v ? "true" : "false"; }

static const char *paced_name(int paced) { return paced == 1 ? "yes" : paced == 0 ? "no" : "unknown"; }

static const char *probe_name(int v) { return v == 1 ? "consumed" : v == 0 ? "returns-at-once" : v == -2 ? "not-run" : "undecided"; }

static const char *process_name(akn_context_kind k) {
    return k == AKN_CTX_FOREGROUND ? "foreground" : k == AKN_CTX_BACKGROUND ? "background" : k == AKN_CTX_DESKTOP ? "desktop" : "unknown";
}

static const char *test_name(pv_test_state t) {
    switch (t) {
    case PV_TEST_NONE: return "none";
    case PV_TEST_PLAYING: return "playing";
    case PV_TEST_SUBMITTED: return "submitted";
    case PV_TEST_NOT_PLAYED: return "not-played";
    case PV_TEST_OUTPUT_FAILED: return "output-failed";
    }
    return "none";
}

int pv_status_json(const pv_status *s, akeno_buf *o) {
    char summary[160];
    pv_status_summary(s, summary, sizeof(summary));
    const pv_route *route = pv_route_get(s->route);
    int rc = 0;
    rc |= akeno_buf_printf(o, "{\"ok\":true,\"app\":\"%s\",\"version\":\"%s\",\"build\":\"%s\",\"protocol\":%d,\"summary\":", PV_APP, PV_VERSION,
                           PV_BUILD_ID, PV_API_PROTOCOL);
    rc |= akeno_buf_append_json_string(o, summary);
    rc |= akeno_buf_printf(o, ",\"inVoice\":%s,\"partyId\":", b(s->in_voice));
    rc |= akeno_buf_append_json_string(o, s->party_id);
    rc |= akeno_buf_printf(o, ",\"network\":{\"state\":\"%s\",\"error\":", pv_net_state_name(s->net));
    rc |= akeno_buf_append_json_string(o, s->net_error);
    rc |= akeno_buf_printf(o, ",\"closeCode\":%d,\"attempts\":%d,\"reconnects\":%lu,\"connections\":%lu,\"ready\":%s,\"participantsReceived\":%s,"
                              "\"latencyMs\":%d,\"daemonIpc\":%d}",
                           s->close_code, s->attempts, s->reconnects, s->connections, b(s->ready_seen), b(s->participants_seen), s->latency_ms,
                           s->daemon_ipc);
    rc |= akeno_buf_printf(o, ",\"mic\":{\"enabled\":%s,\"state\":\"%s\",\"muted\":%s,\"rc\":%d,\"rate\":%u,\"level\":%d,\"gain\":%d,\"refusal\":\"%s\","
                              "\"openCalls\":%lu,\"avoided\":%lu,\"detail\":",
                           b(s->mic_enabled), pv_mic_state_name(s->mic), b(s->muted || s->deafened), (int)s->mic_rc, s->mic_rate, s->mic_level,
                           s->mic_gain, pv_refusal_name(s->mic_refusal), s->mic_open_calls, s->mic_avoided);
    rc |= akeno_buf_append_json_string(o, s->mic_detail);
    rc |= akeno_buf_append_str(o, ",\"attempts\":");
    rc |= akeno_buf_append_json_string(o, s->mic_attempts);
    rc |= akeno_buf_printf(o, "},\"output\":{\"state\":\"%s\",\"deafened\":%s,\"volume\":%d,\"route\":%d,\"routeLabel\":", pv_out_state_name(s->out),
                           b(s->deafened), s->out_volume, s->route);
    rc |= akeno_buf_append_json_string(o, route ? route->label : "");
    rc |= akeno_buf_printf(o, ",\"rc\":%d,\"refusal\":\"%s\",\"paced\":\"%s\",\"probe\":\"%s\",\"probeMs\":%.1f,\"selfPacedSleeps\":%lu,\"level\":%d,"
                              "\"detail\":",
                           (int)s->out_rc, pv_refusal_name(s->out_refusal), paced_name(s->paced), probe_name(s->probe_verdict), s->probe_ms,
                           s->out_self_sleeps, s->rx_level);
    rc |= akeno_buf_append_json_string(o, s->out_detail);
    rc |= akeno_buf_printf(o, "},\"test\":{\"state\":\"%s\",\"route\":%d,\"inVoice\":%s,\"openRc\":%d,\"toneBlocks\":%lu,\"verifiedBlocks\":%lu,"
                              "\"hzMin\":%d,\"hzMax\":%d,\"peak\":%d,\"paced\":\"%s\"}",
                           test_name(s->test_state), s->test_route, b(s->test_in_voice), (int)s->test_open_rc, s->test_tone_blocks,
                           s->test_verified_blocks, s->test_hz_min, s->test_hz_max, s->test_peak_percent, paced_name(s->test_paced));
    rc |= akeno_buf_printf(o, ",\"process\":{\"kind\":\"%s\",\"detail\":", process_name(s->process_kind));
    rc |= akeno_buf_append_json_string(o, s->process_detail);
    /* audible and heard are never measured: only people can answer them */
    rc |= akeno_buf_printf(o,
                           "},\"evidence\":{\"networkConnected\":%s,\"remoteAudioReceived\":%s,\"audioSubmitted\":%s,\"audioAudible\":\"unconfirmed\","
                           "\"micOpen\":%s,\"micCapturing\":%s,\"micTransmitting\":%s,\"remoteHeardMic\":\"unconfirmed\",\"speakerOutput\":%s,"
                           "\"gatewayHeardLevel\":%d}",
                           b(pv_evidence_network(s)), b(pv_evidence_remote_audio(s)), b(pv_evidence_submitted(s)), b(pv_evidence_mic_open(s)),
                           b(pv_evidence_mic_capturing(s)), b(pv_evidence_mic(s)), b(pv_evidence_speaker(s)), s->gateway_self_level_max);
    rc |= akeno_buf_append_str(o, ",\"participants\":[");
    for (int i = 0; i < s->people_count; i++) {
        const akn_voice_participant *p = &s->people[i];
        rc |= akeno_buf_append_str(o, i ? ",{\"id\":" : "{\"id\":");
        rc |= akeno_buf_append_json_string(o, p->id);
        rc |= akeno_buf_append_str(o, ",\"name\":");
        rc |= akeno_buf_append_json_string(o, p->name);
        rc |= akeno_buf_printf(o, ",\"muted\":%s,\"speaking\":%s,\"self\":%s}", b(p->muted), b(p->speaking && !p->muted), b(p->self));
    }
    rc |= akeno_buf_printf(o,
                           "],\"stats\":{\"sent\":%lu,\"sentWithSound\":%lu,\"received\":%lu,\"decoded\":%lu,\"decodedWithSound\":%lu,\"concealed\":%lu,"
                           "\"underruns\":%lu,\"outCalls\":%lu,\"outErrors\":%lu,\"outBlocksWithSound\":%lu,\"outSpeechBlocks\":%lu,\"micReads\":%lu,"
                           "\"micReadErrors\":%lu,\"micFrames\":%lu,\"micFramesWithSound\":%lu,\"encoded\":%lu,\"rxToneHz\":%d,\"txToneHz\":%d,"
                           "\"testSounds\":%lu}",
                           s->sent, s->sent_nonzero, s->received, s->decoded, s->decoded_nonzero, s->concealed, s->underruns, s->out_calls,
                           s->out_errors, s->out_nonzero_blocks, s->out_speech_blocks, s->mic_reads, s->mic_read_errors, s->mic_frames,
                           s->mic_nonzero_frames, s->encoded, s->rx_tone_hz, s->tx_tone_hz, s->test_sounds);
    rc |= akeno_buf_printf(o, ",\"runtime\":{\"uptimeS\":%llu,\"heartbeatGapMaxMs\":%d,\"gameRunning\":%s,\"gameSecondsInVoice\":%lu,\"cpuMs\":%lld}}",
                           (unsigned long long)(s->uptime_ms / 1000u), s->heartbeat_gap_max_ms, b(s->game_running), s->game_seconds_in_voice, s->cpu_ms);
    return rc ? -1 : 0;
}

/* ---- The report ------------------------------------------------------------------------------ */

typedef struct {
    char *out;
    size_t len, used;
} sink;

__attribute__((format(printf, 2, 3))) static void put(sink *k, const char *fmt, ...) {
    if (k->used + 1 >= k->len) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(k->out + k->used, k->len - k->used, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    k->used += (size_t)n < k->len - k->used ? (size_t)n : k->len - k->used - 1;
}

static const char *yes(int v) { return v ? "yes" : "no"; }

static void code(char *buf, size_t len, int32_t rc) {
    if (rc) snprintf(buf, len, "0x%08X", (unsigned)rc);
    else snprintf(buf, len, "0 (success)");
}

static const char *clock_text(int v) {
    return v == 1 ? "consumed by an audio clock (the calls blocked about one block each)"
           : v == 0 ? "NOT consumed in real time (the calls returned at once)"
           : v == -2 ? "not measured (no port was opened)"
                     : "undecided (too few calls, or in between)";
}

static void summary_section(sink *k, const pv_status *s) {
    char c1[32], summary[160];
    pv_status_summary(s, summary, sizeof(summary));
    put(k, "[Summary - what was measured, and what only people can confirm]\n");
    put(k, "Now:                           %s\n", summary);
    put(k, "NETWORK CONNECTED:             %s%s\n", yes(pv_evidence_network(s)),
        pv_evidence_network(s) ? " - the gateway accepted the voice pass, said ready and sent the participants" : "");
    put(k, "REMOTE AUDIO RECEIVED:         %s - %lu packets received, %lu decoded, %lu frames with sound\n", yes(pv_evidence_remote_audio(s)),
        s->received, s->decoded, s->decoded_nonzero);
    if (pv_evidence_submitted(s))
        put(k, "AUDIO SUBMITTED:               yes - accepted output blocks with sound: %lu of party speech, %lu of the test sound\n",
            s->out_speech_blocks, s->test_tone_blocks);
    else
        put(k, "AUDIO SUBMITTED:               no - no output block with sound was accepted%s\n",
            s->out == PV_OUT_FAILED || s->test_state == PV_TEST_NOT_PLAYED ? " (the output port did not open)" : "");
    if (s->probe_verdict != -2) put(k, "                               the port: %s\n", clock_text(s->probe_verdict));
    put(k, "AUDIO AUDIBLE:                 NOT CONFIRMED - only you can answer it (below). A successful call is not sound.\n");
    if (pv_evidence_mic_open(s)) {
        put(k, "MICROPHONE OPEN:               yes - sceAudioInOpen returned a handle %lu of %lu times in this run (%u Hz)\n", s->mic_open_ok,
            s->mic_open_calls, s->mic_rate);
        if (s->mic_refused_rc) {
            code(c1, sizeof(c1), s->mic_refused_rc);
            put(k, "                               last refusal in this run: %s: %s\n", c1, pv_refusal_meaning(s->mic_refused_kind));
        }
    } else if (s->mic_refused_rc) {
        code(c1, sizeof(c1), s->mic_refused_rc);
        put(k, "MICROPHONE OPEN:               no - %s: %s\n", c1, pv_refusal_meaning(s->mic_refused_kind));
    } else {
        put(k, "MICROPHONE OPEN:               no - %s\n", s->mic_opens ? "not opened" : "not turned on (it is never opened without your consent)");
    }
    put(k, "MICROPHONE CAPTURING:          %s - %lu frames captured, %lu not exact zeros\n", yes(pv_evidence_mic_capturing(s)), s->mic_frames,
        s->mic_nonzero_frames);
    put(k, "MICROPHONE TRANSMITTING:       %s - %lu packets sent, %lu of them with sound\n", yes(pv_evidence_mic(s)), s->sent, s->sent_nonzero);
    put(k, "REMOTE USER HEARD MICROPHONE:  NOT CONFIRMED - only your friend can answer it%s\n",
        s->gateway_self_level_max > 0 ? "" : " (the gateway decoded no sound from you)");
    if (s->gateway_self_level_max > 0)
        put(k, "                               the gateway decoded your audio (level up to %d of 100)\n", s->gateway_self_level_max);
    if (s->last_failure[0]) put(k, "Last problem:                  %s\n", s->last_failure);
    put(k, "\n");
}

/* What the measurements mean together; each line only when its measurements say so. */
static void diagnosis_section(sink *k, const pv_status *s) {
    put(k, "[Diagnosis]\n");
    int any = 0;
    if (s->process_kind == AKN_CTX_BACKGROUND && (pv_evidence_submitted(s) || s->probe_verdict == 1)) {
        put(k, "- This service is a background process (no app of its own). On FW 12.20 AKENO Native, the foreground app, was\n"
               "  heard with exactly the route 1 call; every background payload with that call was silent (audio lab v2:\n"
               "  accepted and paced; audio lab v1 and voice 0.1.0: accepted). The blocks above reached the console's output\n"
               "  call intact. If you heard nothing, this is the background-process limit, not lost or broken samples.\n");
        any = 1;
    }
    if (s->probe_verdict == 0 || s->paced == 0) {
        put(k, "- The output port took blocks without an audio clock consuming them (the calls returned at once): the console\n"
               "  did not play these samples at playback speed.\n");
        any = 1;
    }
    if (s->mic_refused_kind == PV_REFUSAL_PROCESS) {
        put(k, "- The microphone refusal 0x809B0081 is the code background payloads also get from scePadOpen. AKENO Native,\n"
               "  the foreground app, opened the microphone with this same call on 12.20 (16 kHz, 256 frames). To talk, use\n"
               "  AKENO Native's voice (no game running) or a phone/PC in the party.\n");
        any = 1;
    }
    if (s->out_refusal == PV_REFUSAL_RESOURCE || pv_refusal_of(s->test_open_rc, 0) == PV_REFUSAL_RESOURCE) {
        put(k, "- Route 2 (your user's port) was refused with 0x80260011 (PS4 SDK name SYSTEM_RESOURCE): this process has no\n"
               "  audio port for your user. Only route 1 opens.\n");
        any = 1;
    }
    pv_refusal_kind out_kind = pv_refusal_of(s->out_rc, 0);
    if (s->out == PV_OUT_FAILED && out_kind != PV_REFUSAL_RESOURCE && out_kind != PV_REFUSAL_NONE) {
        put(k, "- The speaker route %d was not opened (0x%08X: %s): nothing could be played.\n", s->route, (unsigned)s->out_rc,
            pv_refusal_meaning(out_kind));
        any = 1;
    }
    if (s->mic_refused_rc && s->mic_refused_kind != PV_REFUSAL_PROCESS) {
        put(k, "- The microphone was refused (0x%08X: %s). The same call is not repeated until a game starts or ends.\n",
            (unsigned)s->mic_refused_rc, pv_refusal_meaning(s->mic_refused_kind));
        any = 1;
    }
    if (s->test_state == PV_TEST_NOT_PLAYED) {
        put(k, "- The last test sound was NOT played: the port of route %d did not open.\n", s->test_route);
        any = 1;
    }
    if (s->test_state == PV_TEST_SUBMITTED && s->test_verified_blocks == 0) {
        put(k, "- The test sound reached the output call, but its 880 Hz tone was not measured in the submitted blocks.\n");
        any = 1;
    }
    if (s->process_kind == AKN_CTX_FOREGROUND) {
        put(k, "- This service ran as the foreground application: the background-process limit does not apply here.\n");
        any = 1;
    }
    if (s->heartbeat_gap_max_ms > 2000) {
        put(k, "- The system paused this process for up to %d ms.\n", s->heartbeat_gap_max_ms);
        any = 1;
    }
    if (!any) put(k, "- Nothing unusual was measured.\n");
    put(k, "\n");
}

size_t pv_report_text(const pv_status *s, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    char c1[32], c2[32];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char when[40];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", &tm);
    const pv_route *route = pv_route_get(s->route);

    put(&k, "AKENO Party Voice report (EXPERIMENTAL)\n");
    put(&k, "Service:   %s %s, build %s\n", PV_APP, PV_VERSION, PV_BUILD_ID);
    put(&k, "Written:   %s, %llu s after the service started\n", when, (unsigned long long)(s->uptime_ms / 1000u));
    put(&k, "Platform:  %s\n", s->platform);
    put(&k, "Process:   %s\n", s->process_detail[0] ? s->process_detail : "unknown");
    put(&k, "Privacy:   states, codes and counts only - no names, no IDs, no voice pass, no audio, no messages\n\n");

    summary_section(&k, s);
    diagnosis_section(&k, s);

    put(&k, "[Network]\n");
    put(&k, "State:                    %s%s%s\n", pv_net_state_name(s->net), s->net_error[0] ? " - " : "", s->net_error);
    put(&k, "Voice sessions (joins):   %lu\n", s->sessions);
    if (s->daemon_ipc < 0) put(&k, "Daemon native interface:  the daemon did not answer\n");
    else put(&k, "Daemon native interface:  %d%s\n", s->daemon_ipc, s->daemon_ipc >= 3 ? " (voice pass available)" : s->daemon_ipc ? " (needs 3 for voice)" : " (not checked yet)");
    put(&k, "Gateway ready:            %s (this session); connections %lu, reconnects %lu, attempts now %d\n", yes(s->ready_seen),
        s->connections, s->reconnects, s->attempts);
    put(&k, "Participants received:    %s; in voice now: %d\n", yes(s->participants_seen), s->people_count);
    put(&k, "Last close code:          %d\n", s->close_code);
    put(&k, "Round trip:               %d ms (last ping)\n", s->latency_ms);
    put(&k, "Connected time:           %llu s in total\n", (unsigned long long)(s->connected_total_ms / 1000u));
    put(&k, "Packets:                  %lu sent (%lu with sound), %lu received, %lu not sent (queue full), %lu malformed\n", s->sent,
        s->sent_nonzero, s->received, s->tx_dropped, s->rx_malformed);
    put(&k, "\n");

    put(&k, "[Microphone]\n");
    put(&k, "Turned on by you:         %s now (it is never opened without that); turned on %lu time%s in this run\n", yes(s->mic_enabled),
        s->mic_opens, s->mic_opens == 1 ? "" : "s");
    if (s->mic_rc) {
        code(c1, sizeof(c1), s->mic_rc);
        put(&k, "State:                    %s (this voice chat), open result %s (%s)\n", pv_mic_state_name(s->mic), c1, pv_refusal_name(s->mic_refusal));
    } else {
        put(&k, "State:                    %s (this voice chat)\n", pv_mic_state_name(s->mic));
    }
    if (s->mic_refused_rc) {
        code(c1, sizeof(c1), s->mic_refused_rc);
        put(&k, "Last refusal in this run: %s (%s) - %s\n", c1, pv_refusal_name(s->mic_refused_kind), pv_refusal_meaning(s->mic_refused_kind));
    }
    put(&k, "Library / port:           %s\n", s->mic_detail[0] ? s->mic_detail : "(not opened)");
    put(&k, "Open calls:               %lu (of at most %d per run), %lu successful; tried last time: %s\n", s->mic_open_calls, PV_MIC_OPEN_LIMIT,
        s->mic_open_ok, s->mic_attempts[0] ? s->mic_attempts : "-");
    put(&k, "Not repeated:             %lu turn-on request%s answered from a known refusal (same user, same running app)\n", s->mic_avoided,
        s->mic_avoided == 1 ? "" : "s");
    if (s->mic_rate) put(&k, "Format:                   %u Hz, S16 mono, %d frames per read\n", s->mic_rate, PV_MIC_GRAIN);
    else put(&k, "Format:                   - (never opened in this run)\n");
    code(c2, sizeof(c2), s->mic_last_read_rc);
    put(&k, "Reads:                    %lu, failed %lu (last failure %s)\n", s->mic_reads, s->mic_read_errors, s->mic_read_errors ? c2 : "none");
    put(&k, "Captured 20 ms frames:    %lu, with sound (not exact zeros) %lu\n", s->mic_frames, s->mic_nonzero_frames);
    if (s->mic_frames >= 50)
        put(&k, "Level:                    now %d of 100, peak %d%%, last second %.1f dBFS, loudest second %.1f dBFS\n", s->mic_level,
            s->mic_peak_percent, s->mic_rms_db, s->mic_rms_db_max);
    else
        put(&k, "Level:                    now %d of 100, peak %d%% (less than a second captured)\n", s->mic_level, s->mic_peak_percent);
    put(&k, "Encoded packets:          %lu (Opus failures %lu); transmitted %lu\n", s->encoded, s->encode_errors, s->sent);
    put(&k, "Muted / deafened:         %s / %s, gain %d%%\n", yes(s->muted), yes(s->deafened), s->mic_gain);
    put(&k, "\n");

    put(&k, "[Speakers]\n");
    code(c1, sizeof(c1), s->out_rc);
    put(&k, "Route:                    %d - %s\n", s->route, route ? route->label : "?");
    put(&k, "State:                    %s, open result %s (%s)\n", pv_out_state_name(s->out), c1, pv_refusal_name(s->out_refusal));
    put(&k, "Port:                     %s\n", s->out_detail[0] ? s->out_detail : "(not opened)");
    put(&k, "Port opens:               %lu; not repeated after a refusal: %lu\n", s->out_opens, s->out_avoided);
    code(c2, sizeof(c2), s->probe_rc);
    if (s->probe_verdict != -2)
        put(&k, "Clock probe:              %d silent blocks back to back; calls 2..n took %.1f ms for %.1f ms of audio -> %s%s%s\n", s->probe_blocks,
            s->probe_ms, s->probe_blocks > 1 ? (s->probe_blocks - 1) * (double)PV_OUT_GRAIN * 1000.0 / PV_OUT_RATE : 0.0,
            clock_text(s->probe_verdict), s->probe_rc ? ", output failed " : "", s->probe_rc ? c2 : "");
    else
        put(&k, "Clock probe:              not run (no port was opened)\n");
    put(&k, "Decoded frames:           %lu (with sound %lu), concealed %lu, silence %lu, queued for output %lu\n", s->decoded,
        s->decoded_nonzero, s->concealed, s->silent_frames, s->queued_frames);
    code(c2, sizeof(c2), s->out_last_rc);
    put(&k, "Output submissions:       %lu, failed %lu (last failure %s); accepted blocks with sound %lu (party speech %lu, loudest %d%%)\n",
        s->out_calls, s->out_errors, s->out_errors ? c2 : "none", s->out_nonzero_blocks, s->out_speech_blocks, s->out_speech_peak);
    if (s->out_calls)
        put(&k, "Timing:                   %.0f of %.0f ms inside output calls 2..n (per call min %.1f / avg %.1f / max %.1f ms) -> %s\n",
            s->out_blocked_ms, s->out_audio_ms, s->out_call_ms_min, s->out_call_ms_avg, s->out_call_ms_max, clock_text(s->paced));
    else
        put(&k, "Timing:                   not measured (no output call in this run)\n");
    put(&k, "Real-time guard:          slept %lu time%s because the port ran ahead of real time (0 = the port paced every block)\n",
        s->out_self_sleeps, s->out_self_sleeps == 1 ? "" : "s");
    put(&k, "Buffer underruns:         %lu rebuffers, %lu late packets, %lu dropped (too much delay)\n", s->underruns, s->late, s->overflow);
    put(&k, "Volume / deafened:        %d%% / %s; level played now %d of 100\n", s->out_volume, yes(s->deafened), s->rx_level);
    put(&k, "\n");

    put(&k, "[Test sound - %lu requested in this run]\n", s->test_sounds);
    if (s->test_state == PV_TEST_NONE) {
        put(&k, "Last:                     none yet (AKENO NETWORK > Voice chat > Speaker route > Play the test sound)\n");
    } else {
        code(c1, sizeof(c1), s->test_open_rc);
        put(&k, "Route / beeps:            %d / %d beep%s of %d Hz, -20 dBFS at 100 %% volume, %s\n", s->test_route, s->test_route,
            s->test_route == 1 ? "" : "s", 880, s->test_in_voice ? "mixed into the voice output" : "on a port of its own (outside voice)");
        put(&k, "Pipeline:                 generated -> Opus %lu packets -> jitter buffer -> decoded %lu -> volume -> stereo -> output call\n",
            s->test_encoded, s->test_decoded);
        if (!s->test_in_voice) put(&k, "Port:                     open result %s; %s\n", c1, s->test_detail[0] ? s->test_detail : "-");
        switch (s->test_state) {
        case PV_TEST_PLAYING: put(&k, "Result:                   playing\n"); break;
        case PV_TEST_NOT_PLAYED: put(&k, "Result:                   NOT PLAYED - the output port did not open; nothing was sent to the console\n"); break;
        case PV_TEST_OUTPUT_FAILED: put(&k, "Result:                   NOT SUBMITTED - the output call accepted no block of the tone\n"); break;
        default:
            put(&k, "Result:                   SUBMITTED - %lu accepted blocks carried the tone; %lu of them measured at 880 Hz +-5 %% in the\n"
                    "                          buffer handed to the console (%d..%d Hz, left = right), peak %d%% of full scale\n",
                s->test_tone_blocks, s->test_verified_blocks, s->test_hz_min, s->test_hz_max, s->test_peak_percent);
            put(&k, "Timing:                   %s\n", clock_text(s->test_paced));
            put(&k, "Audible:                  NOT CONFIRMED - only if you heard %d beep%s\n", s->test_route, s->test_route == 1 ? "" : "s");
            break;
        }
    }
    put(&k, "\n");

    put(&k, "[Runtime]\n");
    put(&k, "Backend:                  %s\n", s->backend);
    put(&k, "Heartbeats:               %lu, largest gap %d ms (large = the system paused this process)\n", s->heartbeats, s->heartbeat_gap_max_ms);
    put(&k, "Game (running app):       %s now; %lu s of connected voice while one ran; %lu changes\n", s->game_running ? "running" : "none",
        s->game_seconds_in_voice, s->game_changes);
    if (s->cpu_ms >= 0) put(&k, "CPU time:                 %lld ms\n", s->cpu_ms);
    else put(&k, "CPU time:                 not available\n");
    put(&k, "\n");

    put(&k, "[Your answers - fill in and send back]\n");
    put(&k, "AUDIO AUDIBLE - test sound (route 1, one beep) on the home screen:     ____ (TV / DualSense headset / USB headset)\n");
    put(&k, "AUDIO AUDIBLE - test sound while a game runs:                          ____\n");
    put(&k, "AUDIO AUDIBLE - I hear my friend:                                      ____\n");
    put(&k, "REMOTE USER HEARD MICROPHONE - my friend hears me:                     ____\n");
    put(&k, "My friend saw me in the voice chat:                                    ____\n");
    put(&k, "Game still controllable and its sound kept (after L2+R3 several times): ____\n");
    return k.used;
}

int pv_write_file_atomic(const char *path, const char *text, size_t len) {
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int ok = write(fd, text, len) == (ssize_t)len && fsync(fd) == 0;
    close(fd);
    return ok && rename(tmp, path) == 0 ? 0 : -1;
}
