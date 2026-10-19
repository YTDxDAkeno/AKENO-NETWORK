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
int pv_evidence_mic(const pv_status *s) { return s->sent_nonzero > 0; }
int pv_evidence_speaker(const pv_status *s) { return s->decoded_nonzero > 0 && s->out_nonzero_blocks > 0; }

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
    rc |= akeno_buf_printf(o, ",\"mic\":{\"enabled\":%s,\"state\":\"%s\",\"muted\":%s,\"rc\":%d,\"rate\":%u,\"level\":%d,\"gain\":%d,\"detail\":",
                           b(s->mic_enabled), pv_mic_state_name(s->mic), b(s->muted || s->deafened), (int)s->mic_rc, s->mic_rate, s->mic_level,
                           s->mic_gain);
    rc |= akeno_buf_append_json_string(o, s->mic_detail);
    rc |= akeno_buf_printf(o, "},\"output\":{\"state\":\"%s\",\"deafened\":%s,\"volume\":%d,\"route\":%d,\"routeLabel\":", pv_out_state_name(s->out),
                           b(s->deafened), s->out_volume, s->route);
    rc |= akeno_buf_append_json_string(o, route ? route->label : "");
    rc |= akeno_buf_printf(o, ",\"rc\":%d,\"paced\":\"%s\",\"level\":%d,\"detail\":", (int)s->out_rc, paced_name(s->paced), s->rx_level);
    rc |= akeno_buf_append_json_string(o, s->out_detail);
    rc |= akeno_buf_printf(o, "},\"evidence\":{\"networkConnected\":%s,\"micTransmitting\":%s,\"speakerOutput\":%s,\"gatewayHeardLevel\":%d}",
                           b(pv_evidence_network(s)), b(pv_evidence_mic(s)), b(pv_evidence_speaker(s)), s->gateway_self_level_max);
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
                           "\"underruns\":%lu,\"outCalls\":%lu,\"outErrors\":%lu,\"outBlocksWithSound\":%lu,\"micReads\":%lu,\"micReadErrors\":%lu,"
                           "\"micFrames\":%lu,\"micFramesWithSound\":%lu,\"encoded\":%lu,\"rxToneHz\":%d,\"txToneHz\":%d,\"testSounds\":%lu}",
                           s->sent, s->sent_nonzero, s->received, s->decoded, s->decoded_nonzero, s->concealed, s->underruns, s->out_calls,
                           s->out_errors, s->out_nonzero_blocks, s->mic_reads, s->mic_read_errors, s->mic_frames, s->mic_nonzero_frames, s->encoded,
                           s->rx_tone_hz, s->tx_tone_hz, s->test_sounds);
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

size_t pv_report_text(const pv_status *s, char *out, size_t len) {
    sink k = { out, len, 0 };
    if (len) out[0] = '\0';
    char c1[32], c2[32], summary[160];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    char when[40];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", &tm);
    pv_status_summary(s, summary, sizeof(summary));
    const pv_route *route = pv_route_get(s->route);

    put(&k, "AKENO Party Voice report (EXPERIMENTAL)\n");
    put(&k, "Service:   %s %s, build %s\n", PV_APP, PV_VERSION, PV_BUILD_ID);
    put(&k, "Written:   %s, %llu s after the service started\n", when, (unsigned long long)(s->uptime_ms / 1000u));
    put(&k, "Platform:  %s\n", s->platform);
    put(&k, "Privacy:   states, codes and counts only - no names, no IDs, no voice pass, no audio, no messages\n\n");

    put(&k, "[Summary]\n");
    put(&k, "Now:                      %s\n", summary);
    put(&k, "NETWORK CONNECTED:        %s%s\n", yes(pv_evidence_network(s)),
        pv_evidence_network(s) ? " - the gateway accepted the voice pass and sent the participants" : "");
    put(&k, "MICROPHONE TRANSMITTING:  %s%s\n", yes(pv_evidence_mic(s)),
        pv_evidence_mic(s) ? " - frames with sound were captured, encoded and sent; whether your friend hears you is THEIR answer" : "");
    if (pv_evidence_mic(s) && s->gateway_self_level_max > 0)
        put(&k, "                          the gateway decoded your audio (level up to %d of 100)\n", s->gateway_self_level_max);
    put(&k, "SPEAKER OUTPUT ACTIVE:    %s%s\n", yes(pv_evidence_speaker(s)),
        pv_evidence_speaker(s) ? " - incoming audio was decoded and the output port took it; whether you HEARD it is YOUR answer" : "");
    if (s->last_failure[0]) put(&k, "Last problem:             %s\n", s->last_failure);
    put(&k, "\n");

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
    put(&k, "Turned on by you:         %s now (it is never opened without that); opened %lu time%s in this run\n", yes(s->mic_enabled),
        s->mic_opens, s->mic_opens == 1 ? "" : "s");
    code(c1, sizeof(c1), s->mic_rc);
    put(&k, "State:                    %s, open result %s\n", pv_mic_state_name(s->mic), c1);
    put(&k, "Library / port:           %s\n", s->mic_detail[0] ? s->mic_detail : "(not opened)");
    put(&k, "Opens:                    %lu; rate %u Hz, S16 mono, %d frames per read\n", s->mic_opens, s->mic_rate, PV_MIC_GRAIN);
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
    put(&k, "State:                    %s, open result %s\n", pv_out_state_name(s->out), c1);
    put(&k, "Port:                     %s\n", s->out_detail[0] ? s->out_detail : "(not opened)");
    put(&k, "Port opens:               %lu\n", s->out_opens);
    put(&k, "Decoded frames:           %lu (with sound %lu), concealed %lu, silence %lu, queued for output %lu\n", s->decoded,
        s->decoded_nonzero, s->concealed, s->silent_frames, s->queued_frames);
    code(c2, sizeof(c2), s->out_last_rc);
    put(&k, "Output submissions:       %lu, failed %lu (last failure %s), blocks with sound %lu\n", s->out_calls, s->out_errors,
        s->out_errors ? c2 : "none", s->out_nonzero_blocks);
    put(&k, "Timing per call:          min %.1f / avg %.1f / max %.1f ms -> %s\n", s->out_call_ms_min, s->out_call_ms_avg, s->out_call_ms_max,
        s->paced == 1 ? "consumed at playback speed (an audio clock took the blocks)"
        : s->paced == 0 ? "NOT paced (the calls returned at once: probably not played)"
                        : "too few calls to judge");
    put(&k, "Buffer underruns:         %lu rebuffers, %lu late packets, %lu dropped (too much delay)\n", s->underruns, s->late, s->overflow);
    put(&k, "Volume / deafened:        %d%% / %s; level played now %d of 100\n", s->out_volume, yes(s->deafened), s->rx_level);
    if (s->test_sounds) {
        code(c1, sizeof(c1), s->test_open_rc);
        put(&k, "Test sounds:              %lu; last outside voice: route %d, open %s, %lu blocks, %s\n", s->test_sounds, s->test_route, c1,
            s->test_calls, s->test_paced == 1 ? "paced" : s->test_paced == 0 ? "NOT paced" : "timing unknown");
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
    put(&k, "I joined the party (my friend saw me in voice):        ____\n");
    put(&k, "I hear my friend (TV / headset on the DualSense / USB): ____\n");
    put(&k, "My friend hears me:                                   ____\n");
    put(&k, "Speaker route(s) I heard the test sound on (1-4):      ____\n");
    put(&k, "Everything still works after returning to the game:   ____\n");
    put(&k, "Game sound stayed while AKENO voice played:            ____\n");
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
