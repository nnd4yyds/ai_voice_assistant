#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "audio/i2s_audio.h"
#include "api/baidu_api.h"
#include "api/zhipu_api.h"

static const char *TAG = "voice_assistant";

#define VAD_SILENCE_THRESHOLD_MS 1000
#define VAD_SPEECH_THRESHOLD_MS 500
#define MAX_RECORD_DURATION_MS 5000

static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;
static char *audio_buffer = NULL;

static bool is_speaking = false;
static int silence_duration_ms = 0;
static int speech_duration_ms = 0;

static int detect_energy(const int16_t *data, int len)
{
    int64_t sum = 0;
    for (int i = 0; i < len; i++) {
        sum += (int64_t)data[i] * data[i];
    }
    return (int)(sum / len);
}

static int record_audio(void)
{
    int total_bytes = 0;
    int16_t *detect_buffer = malloc(1024 * sizeof(int16_t));
    
    if (!detect_buffer) {
        ESP_LOGE(TAG, "Failed to allocate detect buffer");
        return 0;
    }

    is_speaking = false;
    silence_duration_ms = 0;
    speech_duration_ms = 0;

    while (total_bytes < AUDIO_BUFFER_SIZE - BUFFER_SIZE) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, (uint8_t *)detect_buffer,
                        1024 * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(100));
        
        if (ret == ESP_OK && bytes_read > 0) {
            int energy = detect_energy(detect_buffer, bytes_read / sizeof(int16_t));
            int threshold = 500000;

            if (!is_speaking) {
                if (energy > threshold) {
                    speech_duration_ms += 62;
                    if (speech_duration_ms >= VAD_SPEECH_THRESHOLD_MS) {
                        is_speaking = true;
                        ESP_LOGI(TAG, "Speech detected, start recording...");
                        silence_duration_ms = 0;
                    }
                } else {
                    speech_duration_ms = 0;
                }
            }

            if (is_speaking) {
                if (energy > threshold) {
                    silence_duration_ms = 0;
                } else {
                    silence_duration_ms += 62;
                    if (silence_duration_ms >= VAD_SILENCE_THRESHOLD_MS) {
                        ESP_LOGI(TAG, "Silence detected, stop recording");
                        break;
                    }
                }

                memcpy(audio_buffer + total_bytes, detect_buffer, bytes_read);
                total_bytes += bytes_read;

                if (total_bytes >= MAX_RECORD_DURATION_MS * SAMPLE_RATE * 2 / 1000) {
                    ESP_LOGI(TAG, "Max duration reached");
                    break;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(detect_buffer);
    return total_bytes;
}

void task_voice_assistant(void *pvParameters)
{
    ESP_LOGI(TAG, "Voice assistant started (no wake word mode)");

    rx_handle = (i2s_chan_handle_t)((void **)pvParameters)[0];
    tx_handle = (i2s_chan_handle_t)((void **)pvParameters)[1];

    while (!rx_handle || !tx_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Waiting for Baidu API token...");
    esp_err_t err = baidu_get_access_token();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get Baidu token");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Baidu API ready, listening...");

    while (1) {
        ESP_LOGD(TAG, "Listening for speech...");

        int audio_len = record_audio();
        
        if (audio_len > SAMPLE_RATE * sizeof(int16_t)) {
            ESP_LOGI(TAG, "Recorded %d bytes (%.1f sec), sending to ASR...",
                     audio_len, (float)audio_len / (SAMPLE_RATE * 2));

            char *text = baidu_speech_to_text(audio_buffer, audio_len);
            if (text) {
                ESP_LOGI(TAG, "ASR: \"%s\"", text);

                if (strlen(text) > 0) {
                    char response[2048];
                    char *reply = zhipu_chat(text, response, sizeof(response));
                    free(text);

                    if (reply) {
                        ESP_LOGI(TAG, "Reply: \"%s\"", reply);

                        char *tts_audio = NULL;
                        int tts_len = 0;
                        err = baidu_text_to_speech(reply, &tts_audio, &tts_len);

                        if (err == ESP_OK && tts_audio && tts_len > 0) {
                            ESP_LOGI(TAG, "Playing TTS (%d bytes)...", tts_len);
                            i2s_play_audio(tx_handle, tts_audio, tts_len);
                            free(tts_audio);
                        }
                    }
                } else {
                    free(text);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void voice_assistant_set_buffers(char *buf, i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    audio_buffer = buf;
    rx_handle = rx;
    tx_handle = tx;
}