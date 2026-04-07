#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "audio/i2s_audio.h"
#include "audio/wakeup.h"
#include "api/baidu_api.h"
#include "api/zhipu_api.h"
#include "secrets.h"

static const char *TAG = "voice_assistant";

static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;

#define RECORD_DURATION_SEC 5
#define RECORD_BUFFER_SIZE (SAMPLE_RATE * RECORD_DURATION_SEC * sizeof(int16_t))

static SemaphoreHandle_t audio_buffer_mutex = NULL;
static int16_t *record_buffer = NULL;

static volatile bool is_processing = false;

static void play_tts_response(const char *text)
{
    if (!text || strlen(text) == 0) {
        ESP_LOGW(TAG, "Empty text for TTS");
        return;
    }

    ESP_LOGI(TAG, "TTS: %s", text);

    char *audio_data = NULL;
    int audio_len = 0;

    esp_err_t err = baidu_text_to_speech(text, &audio_data, &audio_len);
    if (err == ESP_OK && audio_data && audio_len > 0) {
        ESP_LOGI(TAG, "Playing TTS audio (%d bytes)", audio_len);
        i2s_play_audio(tx_handle, audio_data, audio_len);
        free(audio_data);
    } else {
        ESP_LOGE(TAG, "TTS failed");
    }
}

static char* record_audio_and_recognize(void)
{
    if (!record_buffer) {
        ESP_LOGE(TAG, "Record buffer not allocated");
        return NULL;
    }

    ESP_LOGI(TAG, "Recording %d seconds...", RECORD_DURATION_SEC);

    int total_samples = SAMPLE_RATE * RECORD_DURATION_SEC;
    int samples_collected = 0;
    int chunk_size = 1024;

    vTaskDelay(pdMS_TO_TICKS(500));

    while (samples_collected < total_samples) {
        int samples_to_read = (total_samples - samples_collected < chunk_size) 
                            ? (total_samples - samples_collected) 
                            : chunk_size;

        int samples_read = i2s_read_microphone_optimized(rx_handle, 
                                                         record_buffer + samples_collected,
                                                         samples_to_read);
        
        if (samples_read > 0) {
            samples_collected += samples_read;
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ESP_LOGI(TAG, "Recorded %d samples (%d bytes)", samples_collected, samples_collected * 2);

    char *text = baidu_speech_to_text((char*)record_buffer, samples_collected * sizeof(int16_t));
    
    if (text) {
        ESP_LOGI(TAG, "ASR result: %s", text);
    } else {
        ESP_LOGW(TAG, "ASR returned no result");
    }

    return text;
}

static void process_voice_command(const char *recognized_text)
{
    if (!recognized_text) {
        play_tts_response("我没有听清楚，请再说一次");
        return;
    }

    ESP_LOGI(TAG, "User said: %s", recognized_text);

    char response[1024] = {0};
    char *result = zhipu_chat(recognized_text, response, sizeof(response));

    free((void*)recognized_text);

    if (result) {
        ESP_LOGI(TAG, "GLM response: %s", response);
        play_tts_response(response);
    } else {
        play_tts_response("抱歉，我遇到了问题，请稍后再试");
    }
}

static void voice_assistant_loop(void)
{
    ESP_LOGI(TAG, "Waiting for wake word...");
    ESP_LOGI(TAG, "Say 'Ni Hao Xiao Zhi' (你好小智) to wake up");

    while (1) {
        if (wakeup_detected) {
            wakeup_detected = false;
            is_processing = true;

            ESP_LOGI(TAG, "Wake word detected!");

            if (baidu_get_access_token() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get Baidu access token");
                play_tts_response("网络连接失败，请检查网络");
                is_processing = false;
                continue;
            }

            play_tts_response("我在");

            vTaskDelay(pdMS_TO_TICKS(300));

            char *recognized_text = record_audio_and_recognize();
            process_voice_command(recognized_text);

            is_processing = false;
            ESP_LOGI(TAG, "Ready for next wake word");
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_voice_assistant(void *pvParameters)
{
    ESP_LOGI(TAG, "Voice assistant starting...");

    rx_handle = (i2s_chan_handle_t)((void **)pvParameters)[0];
    tx_handle = (i2s_chan_handle_t)((void **)pvParameters)[1];

    while (!rx_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    audio_buffer_mutex = xSemaphoreCreateMutex();
    if (!audio_buffer_mutex) {
        ESP_LOGE(TAG, "Failed to create audio buffer mutex");
        vTaskDelete(NULL);
        return;
    }

    record_buffer = heap_caps_malloc(RECORD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!record_buffer) {
        ESP_LOGW(TAG, "Failed to allocate in PSRAM, trying internal RAM");
        record_buffer = malloc(RECORD_BUFFER_SIZE);
    }
    
    if (!record_buffer) {
        ESP_LOGE(TAG, "Failed to allocate record buffer");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Record buffer allocated: %d bytes", RECORD_BUFFER_SIZE);

    esp_err_t err = zhipu_api_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init Zhipu API");
    }

    err = baidu_get_access_token();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get Baidu access token, check WiFi");
    } else {
        ESP_LOGI(TAG, "Baidu access token obtained");
    }

    voice_assistant_loop();
}

void voice_assistant_set_buffers(char *buf, i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    (void)buf;
    rx_handle = rx;
    tx_handle = tx;
}