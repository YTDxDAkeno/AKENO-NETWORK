#include "voice_codec.h"

#include <stdlib.h>
#include <string.h>

#include <opus.h>

struct akn_voice_encoder {
    OpusEncoder *enc;
};

struct akn_voice_decoder {
    OpusDecoder *dec;
};

akn_voice_encoder *akn_voice_encoder_new(int *err) {
    int rc = OPUS_ALLOC_FAIL;
    akn_voice_encoder *e = calloc(1, sizeof(*e));
    if (e) e->enc = opus_encoder_create(AKN_VOICE_RATE, 1, OPUS_APPLICATION_VOIP, &rc);
    if (!e || !e->enc || rc != OPUS_OK) {
        if (err) *err = rc;
        if (e && e->enc) opus_encoder_destroy(e->enc);
        free(e);
        return NULL;
    }
    opus_encoder_ctl(e->enc, OPUS_SET_BITRATE(AKN_VOICE_BITRATE));
    opus_encoder_ctl(e->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(e->enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(e->enc, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(e->enc, OPUS_SET_DTX(0));
    opus_encoder_ctl(e->enc, OPUS_SET_COMPLEXITY(5));
    if (err) *err = OPUS_OK;
    return e;
}

void akn_voice_encoder_free(akn_voice_encoder *e) {
    if (!e) return;
    opus_encoder_destroy(e->enc);
    free(e);
}

int akn_voice_encode(akn_voice_encoder *e, const int16_t *pcm, uint8_t *out, size_t out_len) {
    if (!e || !pcm || !out || out_len == 0) return OPUS_BAD_ARG;
    return opus_encode(e->enc, pcm, AKN_VOICE_FRAME, out, (opus_int32)(out_len > 4000 ? 4000 : out_len));
}

akn_voice_decoder *akn_voice_decoder_new(int *err) {
    int rc = OPUS_ALLOC_FAIL;
    akn_voice_decoder *d = calloc(1, sizeof(*d));
    if (d) d->dec = opus_decoder_create(AKN_VOICE_RATE, 1, &rc);
    if (!d || !d->dec || rc != OPUS_OK) {
        if (err) *err = rc;
        if (d && d->dec) opus_decoder_destroy(d->dec);
        free(d);
        return NULL;
    }
    if (err) *err = OPUS_OK;
    return d;
}

void akn_voice_decoder_free(akn_voice_decoder *d) {
    if (!d) return;
    opus_decoder_destroy(d->dec);
    free(d);
}

int akn_voice_decode(akn_voice_decoder *d, const uint8_t *packet, size_t len, int16_t *out) {
    if (!d || !out) return OPUS_BAD_ARG;
    if (packet && (len == 0 || len > AKN_VOICE_MAX_PACKET)) packet = NULL;
    int n = opus_decode(d->dec, packet, packet ? (opus_int32)len : 0, out, AKN_VOICE_FRAME, 0);
    if (n < 0) {
        memset(out, 0, sizeof(int16_t) * AKN_VOICE_FRAME);
        return n;
    }
    if (n < AKN_VOICE_FRAME) memset(out + n, 0, sizeof(int16_t) * (size_t)(AKN_VOICE_FRAME - n));
    return AKN_VOICE_FRAME;
}

const char *akn_voice_codec_version(void) { return opus_get_version_string(); }
