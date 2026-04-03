#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "driver/i2s_std.h"
#include "secrets.h"
#include "i2s_audio.h"
#include "wakeup.h"

static const char *TAG = "wakeup";

static volatile bool wakeup_detected = false;
static esp_afe_sr_data_t *afe_data = NULL;

esp_afe_sr_iface_t *wakeup_detection_init(void)
{
    return (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
}

esp_afe_sr_data_t *wakeup_detection_create(esp_afe_sr_iface_t *afe_handle)
{
    afe_config_t afe_config = {
        .aec_init = true,
        .se_init = true,
        .vad_init = true,
        .wakenet_init = true,
        .voice_communication_init = false,
        .afe_mode = SR_MODE_HIGH_PERF,
        .afe_perferred_core = 0,
        .afe_perferred_priority = 5,
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_INTERNAL,
        .agc_mode = AFE_MN_PEAK_AGC_MODE_2,
        .pcm_config = {
            .total_ch_num = 1,
            .mic_num = 1,
            .ref_num = 0,
        },
    };

    afe_data = afe_handle->create_from_config(&afe_config);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data");
        return NULL;
    }

    ESP_LOGI(TAG, "Wakeup detection initialized");
    return afe_data;
}

bool is_wakeup_detected(void)
{
    return wakeup_detected;
}

void reset_wakeup_flag(void)
{
    wakeup_detected = false;
}

void task_wakeup_detection(void *pvParameters)
{
    i2s_chan_handle_t rx_handle = (i2s_chan_handle_t)pvParameters;

    ESP_LOGI(TAG, "Wakeup detection task started");

    esp_afe_sr_iface_t *afe_handle = wakeup_detection_init();
    afe_data = wakeup_detection_create(afe_handle);

    while (!afe_data || !rx_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    ESP_LOGI(TAG, "Audio chunk size: %d", audio_chunksize);

    int16_t *audio_buffer = malloc(audio_chunksize * sizeof(int16_t));
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer for wakeup detection");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer,
                                         audio_chunksize * sizeof(int16_t),
                                         &bytes_read, pdMS_TO_TICKS(100));

        if (ret == ESP_OK && bytes_read > 0) {
            afe_handle->feed(afe_data, audio_buffer);

            afe_fetch_result_t *result = afe_handle->fetch(afe_data);
            if (result) {
                if (result->wakeup_state == WAKENET_DETECTED) {
                    ESP_LOGI(TAG, "Wakeup word detected! Score: %d", result->wake_word_index);
                    wakeup_detected = true;
                }

                if (result->vad_state == AFE_VAD_SPEECH) {
                    ESP_LOGD(TAG, "Speech detected");
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(audio_buffer);
}
