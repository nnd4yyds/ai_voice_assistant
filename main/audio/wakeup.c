#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_afe_sr_models.h"
#include "driver/i2s_std.h"
#include "secrets.h"
#include "i2s_audio.h"
#include "wakeup.h"
#include "voice_handler.h"

static const char *TAG = "wakeup";

volatile bool wakeup_detected = false;
static esp_afe_sr_data_t *afe_data = NULL;
static esp_afe_sr_iface_t *afe_handle = NULL;
static i2s_chan_handle_t g_rx_handle = NULL;
static i2s_chan_handle_t g_tx_handle = NULL;

esp_afe_sr_iface_t *wakeup_detection_init(void)
{
    return (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
}

esp_afe_sr_data_t *wakeup_detection_create(esp_afe_sr_iface_t *afe_handle, i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    g_rx_handle = rx;
    g_tx_handle = tx;

    afe_config_t afe_config = {
        .aec_init = false,
        .se_init = true,
        .vad_init = true,
        .wakenet_init = true,
        .wakenet_model_name = "wn9_nihaoxiaozhi_tts",
        .wakenet_model_name_2 = NULL,
        .wakenet_mode = DET_MODE_90,
        .voice_communication_init = false,
        .voice_communication_agc_init = false,
        .voice_communication_agc_gain = 15,
        .vad_mode = VAD_MODE_3,
        .agc_mode = AFE_MN_PEAK_AGC_MODE_2,
        .pcm_config = {
            .total_ch_num = 1,
            .mic_num = 1,
            .ref_num = 0,
            .sample_rate = 16000,
        },
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM,
        .afe_perferred_core = 0,
        .afe_perferred_priority = 5,
        .afe_ringbuf_size = 50,
        .afe_linear_gain = 1.0,
        .afe_mode = SR_MODE_LOW_COST,
        .debug_init = false,
        .debug_hook = {{AFE_DEBUG_HOOK_MASE_TASK_IN, NULL}, {AFE_DEBUG_HOOK_FETCH_TASK_IN, NULL}},
        .afe_ns_mode = NS_MODE_SSP,
        .afe_ns_model_name = NULL,
        .fixed_first_channel = true,
    };

    afe_data = afe_handle->create_from_config(&afe_config);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data");
        return NULL;
    }

    ESP_LOGI(TAG, "Wakeup detection initialized");
    ESP_LOGI(TAG, "Wake word: 'Ni Hao Xiao Zhi' (你好小智)");
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
    ESP_LOGI(TAG, "Wakeup detection task started");

    afe_handle = wakeup_detection_init();
    if (!afe_handle) {
        ESP_LOGE(TAG, "Failed to init AFE handle");
        vTaskDelete(NULL);
        return;
    }

    afe_data = wakeup_detection_create(afe_handle, g_rx_handle, g_tx_handle);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data");
        vTaskDelete(NULL);
        return;
    }

    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    ESP_LOGI(TAG, "Audio chunk size: %d samples", audio_chunksize);

    int16_t *audio_buffer = malloc(audio_chunksize * sizeof(int16_t) * 2);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }

    int feed_size = audio_chunksize;

    ESP_LOGI(TAG, "Wakeup detection running...");

    while (1) {
        int samples_read = i2s_read_microphone_optimized(g_rx_handle, audio_buffer, feed_size);
        
        if (samples_read > 0) {
            afe_handle->feed(afe_data, audio_buffer);

            afe_fetch_result_t *result = afe_handle->fetch(afe_data);
            if (result && result->wakeup_state == WAKENET_DETECTED) {
                ESP_LOGI(TAG, "Wake word detected!");
                
                voice_handler_process();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(audio_buffer);
    vTaskDelete(NULL);
}

void wakeup_set_handles(i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    g_rx_handle = rx;
    g_tx_handle = tx;
}