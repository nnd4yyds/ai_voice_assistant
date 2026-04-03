#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "wifi/wifi_manager.h"
#include "audio/i2s_audio.h"
#include "tasks/voice_assistant.h"
#include "serial_test.h"

static const char *TAG = "main";

static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;
static char *audio_buffer = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "Voice Assistant Starting...");
    ESP_LOGI(TAG, "Mic: INMP441, Mode: Always-on (no wake word)");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing I2S...");
    esp_err_t mic_ret = i2s_microphone_init(&rx_handle);
    if (mic_ret != ESP_OK) {
        ESP_LOGE(TAG, "Microphone init failed, voice assistant disabled");
    }
    esp_err_t spk_ret = i2s_speaker_init(&tx_handle);
    if (spk_ret != ESP_OK) {
        ESP_LOGE(TAG, "Speaker init failed");
    }
    ESP_LOGI(TAG, "I2S initialized");

    audio_buffer = malloc(AUDIO_BUFFER_SIZE);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return;
    }

    wifi_init_sta();

    xTaskCreate(task_serial_test, "serial_test", 16384, NULL, 5, NULL);

    if (mic_ret == ESP_OK && rx_handle != NULL) {
        static void *task_params[2];
        task_params[0] = rx_handle;
        task_params[1] = tx_handle;
        voice_assistant_set_buffers(audio_buffer, rx_handle, tx_handle);
        xTaskCreate(task_voice_assistant, "voice", 16384, task_params, 5, NULL);
        ESP_LOGI(TAG, "Voice assistant enabled. Speak to start conversation.");
    } else {
        ESP_LOGW(TAG, "Voice assistant disabled (mic not available)");
    }

    ESP_LOGI(TAG, "Serial commands: 'chat <msg>' to test GLM");
}