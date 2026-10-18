#include "voice.h"

#include <string.h>

void akn_voice_init(akn_voice *v) {
    memset(v, 0, sizeof(*v));
    v->out_volume = 100;
    v->mic_volume = 100;
}

const char *akn_voice_state_text(const akn_voice *v) {
    switch (v->state) {
    case AKN_VOICE_OFF: return "Not in voice";
    case AKN_VOICE_REQUESTING: return "Asking AKENO for a voice pass...";
    case AKN_VOICE_CONNECTING: return "Connecting to voice...";
    case AKN_VOICE_CONNECTED: return v->deafened ? "In voice (deafened)" : v->muted ? "In voice (muted)" : "In voice";
    case AKN_VOICE_RECONNECTING: return "Reconnecting to voice...";
    case AKN_VOICE_FAILED: return "Voice stopped";
    }
    return "";
}

const char *akn_mic_state_text(akn_mic_state s) {
    switch (s) {
    case AKN_MIC_CLOSED: return "Microphone off";
    case AKN_MIC_OPEN: return "Microphone on";
    case AKN_MIC_UNAVAILABLE: return "No microphone function on this system";
    case AKN_MIC_FAILED: return "Microphone failed";
    }
    return "";
}

int akn_voice_volume_step(int value, int direction) {
    value += direction > 0 ? AKN_VOICE_VOLUME_STEP : direction < 0 ? -AKN_VOICE_VOLUME_STEP : 0;
    if (value < 0) value = 0;
    if (value > AKN_VOICE_VOLUME_MAX) value = AKN_VOICE_VOLUME_MAX;
    return value;
}

int akn_voice_should_retry(int close_code, int attempts) {
    if (attempts >= AKN_VOICE_RECONNECT_MAX) return 0;
    switch (close_code) {
    case AKN_VOICE_CLOSE_REMOVED:
    case AKN_VOICE_CLOSE_REPLACED:
    case AKN_VOICE_CLOSE_ENDED:
    case 1000:
        return 0;
    default:
        return 1;
    }
}

uint32_t akn_voice_backoff_ms(int attempt) {
    if (attempt <= 1) return 1000;
    if (attempt >= 5) return 10000;
    return 1000u << (attempt - 1);
}

void akn_voice_set_people(akn_voice *v, const akn_voice_msg *msg) {
    v->people_count = 0;
    v->self_speaking = 0;
    for (int i = 0; i < msg->count && i < AKN_VOICE_MAX_PARTICIPANTS; i++) {
        v->people[v->people_count] = msg->list[i];
        if (msg->list[i].self) v->self_speaking = msg->list[i].speaking;
        v->people_count++;
    }
}

int akn_voice_in_room(const akn_voice *v, const char *user_id) {
    if (!user_id || !user_id[0]) return 0;
    for (int i = 0; i < v->people_count; i++)
        if (strcmp(v->people[i].id, user_id) == 0) return 1;
    return 0;
}

int akn_voice_is_speaking(const akn_voice *v, const char *user_id) {
    if (!user_id || !user_id[0]) return 0;
    for (int i = 0; i < v->people_count; i++)
        if (strcmp(v->people[i].id, user_id) == 0) return v->people[i].speaking && !v->people[i].muted;
    return 0;
}
