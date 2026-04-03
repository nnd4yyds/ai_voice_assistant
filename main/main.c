#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "wifi/wifi_manager.h"
#include "audio/i2s_audio.h"
#include "audio/wakeup.h"
#include "tasks/app_tasks.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Voice Assistant Application Started");
    ESP_LOGI(TAG, "ESP32-S3 Voice Assistant Demo with Wakeup Detection");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    char *audio_buffer = malloc(AUDIO_BUFFER_SIZE);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return;
    }

    wifi_init_sta();

    i2s_chan_handle_t rx_handle = NULL;
    i2s_chan_handle_t tx_handle = NULL;
    i2s_microphone_init(&rx_handle);
    i2s_speaker_init(&tx_handle);

    wakeup_detection_init();

    xTaskCreate(task_main_loop, "main", 4096, NULL, 5, NULL);
    xTaskCreate(task_wakeup_detection, "wakeup", 4096, (void *)rx_handle, 5, NULL);
    xTaskCreate(task_speech_recognition, "speech_rec", 4096, (void *)rx_handle, 5, NULL);
    xTaskCreate(task_speech_synthesis, "speech_synth", 4096, (void *)tx_handle, 5, NULL);

    ESP_LOGI(TAG, "All tasks created successfully");
}
