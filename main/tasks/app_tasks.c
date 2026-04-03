#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "wifi/wifi_manager.h"
#include "audio/i2s_audio.h"
#include "audio/wakeup.h"
#include "api/baidu_api.h"
#include "app_tasks.h"

static const char *TAG = "app_tasks";

static char *audio_buffer = NULL;
static int audio_buffer_index = 0;

void task_speech_recognition(void *pvParameters)
{
    i2s_chan_handle_t rx_handle = (i2s_chan_handle_t)pvParameters;

    ESP_LOGI(TAG, "Speech recognition task started");

    while (1) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (is_wakeup_detected()) {
            ESP_LOGI(TAG, "Wakeup detected, starting speech recognition...");
            reset_wakeup_flag();

            audio_buffer_index = 0;
            memset(audio_buffer, 0, AUDIO_BUFFER_SIZE);

            for (int i = 0; i < 3 * SAMPLE_RATE / BUFFER_SIZE; i++) {
                size_t bytes_read = 0;
                i2s_channel_read(rx_handle, audio_buffer + audio_buffer_index,
                               BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100));
                if (bytes_read > 0) {
                    audio_buffer_index += bytes_read;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }

            if (audio_buffer_index > 0) {
                char *result = baidu_speech_to_text(audio_buffer, audio_buffer_index);
                if (result) {
                    ESP_LOGI(TAG, "Recognized: %s", result);
                    free(result);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_speech_synthesis(void *pvParameters)
{
    i2s_chan_handle_t tx_handle = (i2s_chan_handle_t)pvParameters;

    ESP_LOGI(TAG, "Speech synthesis task started");

    while (1) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        static bool first_run = true;
        if (first_run) {
            first_run = false;
            vTaskDelay(pdMS_TO_TICKS(5000));

            char *audio_data = NULL;
            int audio_len = 0;
            esp_err_t err = baidu_text_to_speech("你好，我是ESP32语音助手", &audio_data, &audio_len);
            if (err == ESP_OK && audio_data) {
                i2s_play_audio(tx_handle, audio_data, audio_len);
                free(audio_data);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_main_loop(void *pvParameters)
{
    ESP_LOGI(TAG, "Main task started");

    while (!wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 不自动获取 token，由 serial_test 任务手动获取
    ESP_LOGI(TAG, "WiFi connected, ready for commands");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void set_audio_buffer(char *buffer)
{
    audio_buffer = buffer;
}
