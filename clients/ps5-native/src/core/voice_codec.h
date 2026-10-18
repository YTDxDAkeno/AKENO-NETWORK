/*
 * Party voice codec: Opus (libopus 1.5.2), 48 kHz mono, 20 ms frames, the format the AKENO voice
 * gateway speaks. Encoder: VOIP mode, 32 kbit/s, in-band FEC for 10 % loss, DTX off (the gateway
 * expects a steady 50 packets per second while the microphone is live). Decoder: packet loss
 * concealment for missing packets.
 */
#ifndef AKN_VOICE_CODEC_H
#define AKN_VOICE_CODEC_H

#include <stddef.h>
#include <stdint.h>

#define AKN_VOICE_RATE 48000
#define AKN_VOICE_FRAME 960          /* samples per 20 ms frame */
#define AKN_VOICE_MAX_PACKET 400     /* bytes; far above 32 kbit/s * 20 ms */
#define AKN_VOICE_BITRATE 32000

typedef struct akn_voice_encoder akn_voice_encoder;
typedef struct akn_voice_decoder akn_voice_decoder;

/* NULL on failure; *err (may be NULL) gets the Opus error code. */
akn_voice_encoder *akn_voice_encoder_new(int *err);
void akn_voice_encoder_free(akn_voice_encoder *e);
/* Encodes one frame of AKN_VOICE_FRAME samples. Returns the packet size (> 0) or < 0 on error. */
int akn_voice_encode(akn_voice_encoder *e, const int16_t *pcm, uint8_t *out, size_t out_len);

akn_voice_decoder *akn_voice_decoder_new(int *err);
void akn_voice_decoder_free(akn_voice_decoder *d);
/*
 * Decodes one packet into AKN_VOICE_FRAME samples. packet == NULL conceals one lost frame.
 * Returns the number of samples written (AKN_VOICE_FRAME) or < 0 on error (out is then silence).
 */
int akn_voice_decode(akn_voice_decoder *d, const uint8_t *packet, size_t len, int16_t *out);

const char *akn_voice_codec_version(void);

#endif
