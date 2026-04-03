#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi/wifi_manager.h"
#include "tasks/app_tasks.h"
#include "serial_test.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Voice Assistant Application Started");
    ESP_LOGI(TAG, "ESP32-S3 Voice Assistant - Serial Test Mode");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    xTaskCreate(task_main_loop, "main", 8192, NULL, 5, NULL);
    xTaskCreate(task_serial_test, "serial_test", 16384, NULL, 5, NULL);

    ESP_LOGI(TAG, "All tasks created successfully - Serial Test Mode Active");
}
