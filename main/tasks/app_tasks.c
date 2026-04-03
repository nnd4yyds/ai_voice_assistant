#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"
#include "wifi/wifi_manager.h"
#include "audio/i2s_audio.h"
#include "audio/wakeup.h"
#include "api/baidu_api.h"
#include "api/zhipu_api.h"
#include "app_tasks.h"

static const char *TAG = "app_tasks";

static char *audio_buffer = NULL;
static int audio_buffer_index = 0;

static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;

static QueueHandle_t tts_audio_queue = NULL;

typedef struct {
    char *audio_data;
    int audio_len;
} tts_audio_msg_t;

void task_speech_recognition(void *pvParameters)
{
    i2s_chan_handle_t rx = (i2s_chan_handle_t)pvParameters;

    ESP_LOGI(TAG, "Speech recognition task started");

    while (1) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (is_wakeup_detected()) {
            ESP_LOGI(TAG, "Wakeup detected, recording...");
            reset_wakeup_flag();

            audio_buffer_index = 0;
            memset(audio_buffer, 0, AUDIO_BUFFER_SIZE);

            int record_frames = 3 * SAMPLE_RATE / BUFFER_SIZE;
            for (int i = 0; i < record_frames; i++) {
                size_t bytes_read = 0;
                esp_err_t ret = i2s_channel_read(rx, audio_buffer + audio_buffer_index,
                               BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100));
                if (ret == ESP_OK && bytes_read > 0) {
                    audio_buffer_index += bytes_read;
                }
                if (audio_buffer_index >= AUDIO_BUFFER_SIZE - BUFFER_SIZE) {
                    break;
                }
            }

            ESP_LOGI(TAG, "Recorded %d bytes, sending to ASR...", audio_buffer_index);

            char *text = baidu_speech_to_text(audio_buffer, audio_buffer_index);
            if (text) {
                ESP_LOGI(TAG, "ASR result: %s", text);

                char response[2048];
                char *reply = zhipu_chat(text, response, sizeof(response));
                free(text);

                if (reply) {
                    ESP_LOGI(TAG, "LLM reply: %s", reply);

                    char *tts_audio = NULL;
                    int tts_len = 0;
                    esp_err_t err = baidu_text_to_speech(reply, &tts_audio, &tts_len);

                    if (err == ESP_OK && tts_audio && tts_len > 0) {
                        if (tts_audio_queue) {
                            tts_audio_msg_t msg = {
                                .audio_data = tts_audio,
                                .audio_len = tts_len
                            };
                            xQueueSend(tts_audio_queue, &msg, pdMS_TO_TICKS(1000));
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_speech_synthesis(void *pvParameters)
{
    i2s_chan_handle_t tx = (i2s_chan_handle_t)pvParameters;

    ESP_LOGI(TAG, "Speech synthesis task started");

    tts_audio_queue = xQueueCreate(10, sizeof(tts_audio_msg_t));

    while (1) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        tts_audio_msg_t msg;
        if (xQueueReceive(tts_audio_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGI(TAG, "Playing TTS audio, len=%d", msg.audio_len);
            i2s_play_audio(tx, msg.audio_data, msg.audio_len);
            free(msg.audio_data);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void task_main_loop(void *pvParameters)
{
    ESP_LOGI(TAG, "Main task started");

    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "WiFi connected, initializing Baidu API...");

    esp_err_t err = baidu_get_access_token();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get Baidu access token");
    } else {
        ESP_LOGI(TAG, "Baidu API ready");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_voice_assistant(void *pvParameters)
{
    ESP_LOGI(TAG, "Voice assistant task started");

    while (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Waiting for WiFi...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "WiFi connected, initializing Baidu API...");
    esp_err_t err = baidu_get_access_token();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get Baidu access token");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Baidu API ready, waiting for wakeup word...");

    rx_handle = (i2s_chan_handle_t)pvParameters;

    while (1) {
        if (is_wakeup_detected()) {
            ESP_LOGI(TAG, "Wakeup detected, recording audio...");
            reset_wakeup_flag();

            audio_buffer_index = 0;
            memset(audio_buffer, 0, AUDIO_BUFFER_SIZE);

            ESP_LOGI(TAG, "Recording for 3 seconds...");
            int record_frames = 3 * SAMPLE_RATE / BUFFER_SIZE;
            for (int i = 0; i < record_frames; i++) {
                size_t bytes_read = 0;
                esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer + audio_buffer_index,
                               BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100));
                if (ret == ESP_OK && bytes_read > 0) {
                    audio_buffer_index += bytes_read;
                }
                if (audio_buffer_index >= AUDIO_BUFFER_SIZE - BUFFER_SIZE) {
                    break;
                }
            }

            ESP_LOGI(TAG, "Recorded %d bytes (%.1f seconds)", audio_buffer_index,
                     (float)audio_buffer_index / (SAMPLE_RATE * 2));

            ESP_LOGI(TAG, "Sending to Baidu ASR...");
            char *text = baidu_speech_to_text(audio_buffer, audio_buffer_index);
            if (text) {
                ESP_LOGI(TAG, "Recognized: \"%s\"", text);

                ESP_LOGI(TAG, "Sending to Zhipu GLM...");
                char response[2048];
                char *reply = zhipu_chat(text, response, sizeof(response));
                free(text);

                if (reply) {
                    ESP_LOGI(TAG, "Reply: \"%s\"", reply);

                    ESP_LOGI(TAG, "Converting to speech...");
                    char *tts_audio = NULL;
                    int tts_len = 0;
                    esp_err_t err = baidu_text_to_speech(reply, &tts_audio, &tts_len);

                    if (err == ESP_OK && tts_audio && tts_len > 0) {
                        ESP_LOGI(TAG, "Playing response (%d bytes)...", tts_len);

                        if (tx_handle) {
                            i2s_play_audio(tx_handle, tts_audio, tts_len);
                        }
                        free(tts_audio);
                    }
                }
            } else {
                ESP_LOGW(TAG, "ASR failed, no text recognized");
            }

            ESP_LOGI(TAG, "Ready for next wakeup...");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void set_audio_buffer(char *buffer)
{
    audio_buffer = buffer;
}

void set_i2s_handles(i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    rx_handle = rx;
    tx_handle = tx;
}