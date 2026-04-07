#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "voice_handler.h"
#include "i2s_audio.h"
#include "../api/baidu_api.h"
#include "../api/zhipu_api.h"

static const char *TAG = "voice_handler";

static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;

#define RECORD_DURATION_SEC 5
#define RECORD_BUFFER_SIZE (16000 * RECORD_DURATION_SEC * sizeof(int16_t))

static int16_t *record_buffer = NULL;

int voice_handler_init(i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    rx_handle = rx;
    tx_handle = tx;

    record_buffer = heap_caps_malloc(RECORD_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!record_buffer) {
        ESP_LOGW(TAG, "PSRAM alloc failed, trying internal RAM");
        record_buffer = malloc(RECORD_BUFFER_SIZE);
    }
    
    if (!record_buffer) {
        ESP_LOGE(TAG, "Failed to allocate record buffer");
        return -1;
    }

    ESP_LOGI(TAG, "Record buffer: %d bytes", RECORD_BUFFER_SIZE);
    
    zhipu_api_init();
    
    return 0;
}

static void play_tts(const char *text)
{
    if (!text || strlen(text) == 0) {
        ESP_LOGW(TAG, "Empty TTS text");
        return;
    }

    ESP_LOGI(TAG, "TTS: %s", text);

    char *audio = NULL;
    int len = 0;
    esp_err_t err = baidu_text_to_speech(text, &audio, &len);
    
    if (err == ESP_OK && audio && len > 0) {
        ESP_LOGI(TAG, "Playing TTS (%d bytes)", len);
        i2s_play_audio(tx_handle, audio, len);
        free(audio);
    } else {
        ESP_LOGE(TAG, "TTS failed");
    }
}

static char *record_and_recognize(void)
{
    if (!record_buffer) {
        ESP_LOGE(TAG, "No record buffer");
        return NULL;
    }

    ESP_LOGI(TAG, "Recording %ds...", RECORD_DURATION_SEC);

    int total_samples = 16000 * RECORD_DURATION_SEC;
    int collected = 0;
    int chunk = 1024;

    vTaskDelay(pdMS_TO_TICKS(500));

    while (collected < total_samples) {
        int to_read = (total_samples - collected < chunk) ? (total_samples - collected) : chunk;
        int read = i2s_read_microphone_optimized(rx_handle, record_buffer + collected, to_read);
        
        if (read > 0) {
            collected += read;
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    ESP_LOGI(TAG, "Recorded %d samples", collected);

    char *text = baidu_speech_to_text((char*)record_buffer, collected * sizeof(int16_t));
    if (text) {
        ESP_LOGI(TAG, "ASR: %s", text);
    } else {
        ESP_LOGW(TAG, "ASR: no result");
    }

    return text;
}

void voice_handler_process(void)
{
    ESP_LOGI(TAG, "Processing voice command...");

    if (baidu_get_access_token() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get Baidu token");
        play_tts("网络连接失败");
        return;
    }

    play_tts("我在");

    char *text = record_and_recognize();
    if (!text) {
        play_tts("没听清楚，请再说");
        return;
    }

    ESP_LOGI(TAG, "User: %s", text);

    char response[1024] = {0};
    char *reply = zhipu_chat(text, response, sizeof(response));
    free(text);

    if (reply) {
        ESP_LOGI(TAG, "GLM: %s", response);
        play_tts(response);
    } else {
        play_tts("处理失败，请重试");
    }
}