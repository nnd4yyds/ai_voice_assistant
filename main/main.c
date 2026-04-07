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
#include "audio/voice_handler.h"
#include "serial_test.h"

static const char *TAG = "main";

static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;

void app_main(void)
{
    esp_log_level_set("esp-x509-crt-bundle", ESP_LOG_WARN);

    ESP_LOGI(TAG, "Voice Assistant Starting...");
    ESP_LOGI(TAG, "Wake word: 'Ni Hao Xiao Zhi' (你好小智)");
    ESP_LOGI(TAG, "After wake word detected: record 5s -> ASR -> GLM -> TTS");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initializing I2S...");
    esp_err_t mic_ret = i2s_microphone_init(&rx_handle);
    if (mic_ret != ESP_OK) {
        ESP_LOGE(TAG, "Microphone init failed");
    }
    esp_err_t spk_ret = i2s_speaker_init(&tx_handle);
    if (spk_ret != ESP_OK) {
        ESP_LOGE(TAG, "Speaker init failed");
    }
    ESP_LOGI(TAG, "I2S initialized");

    wifi_init_sta();

    xTaskCreate(task_serial_test, "serial_test", 16384, NULL, 5, NULL);

    if (mic_ret == ESP_OK && rx_handle != NULL && spk_ret == ESP_OK && tx_handle != NULL) {
        voice_handler_init(rx_handle, tx_handle);
        wakeup_set_handles(rx_handle, tx_handle);
        xTaskCreate(task_wakeup_detection, "wakeup", 8192, NULL, 5, NULL);
        ESP_LOGI(TAG, "Voice assistant enabled. Say 'Ni Hao Xiao Zhi' to wake up.");
    } else {
        ESP_LOGW(TAG, "Voice assistant disabled (audio not available)");
    }
}