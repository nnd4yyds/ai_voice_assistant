#ifndef I2S_AUDIO_H
#define I2S_AUDIO_H

#include "driver/i2s_std.h"
#include <stdbool.h>

#define SAMPLE_RATE     16000
#define BITS_PER_SAMPLE 16
#define CHANNEL_NUM     1
#define BUFFER_SIZE     1024
#define AUDIO_BUFFER_SIZE (16000 * 5)

void i2s_microphone_init(i2s_chan_handle_t *rx_handle);
void i2s_speaker_init(i2s_chan_handle_t *tx_handle);
void i2s_play_audio(i2s_chan_handle_t tx_handle, const char *audio_data, int audio_len);

#endif // I2S_AUDIO_H
