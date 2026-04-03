#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi/wifi_manager.h"
#include "api/baidu_api.h"
#include "serial_test.h"

static const char *TAG = "serial_test";

void task_serial_test(void *pvParameters)
{
    ESP_LOGI(TAG, "Serial test task started");
    ESP_LOGI(TAG, "Type text and press Enter to test TTS->STT round-trip");
    ESP_LOGI(TAG, "Type 'token' to refresh access token");
    ESP_LOGI(TAG, "Type 'quit' to exit");

    char input_buffer[256];
    int input_index = 0;
    bool wifi_logged = false;

    while (1)
    {
        if (!wifi_is_connected())
        {
            if (!wifi_logged) {
                ESP_LOGW(TAG, "Waiting for WiFi connection... (Check secrets.h)");
                wifi_logged = true;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        else
        {
            if (!wifi_logged) {
                ESP_LOGI(TAG, "WiFi connected! Ready for input.");
                wifi_logged = true;
            }
        }

        int ch = getchar();
        if (ch == EOF)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (ch == '\n' || ch == '\r')
        {
            if (input_index > 0)
            {
                input_buffer[input_index] = '\0';
                printf("\r\n");
                ESP_LOGI(TAG, "Received: %s", input_buffer);

                if (strcmp(input_buffer, "quit") == 0)
                {
                    ESP_LOGI(TAG, "Exiting serial test");
                    vTaskDelete(NULL);
                }
                else if (strcmp(input_buffer, "token") == 0)
                {
                    ESP_LOGI(TAG, "Refreshing access token...");
                    baidu_get_access_token();
                }
                else
                {
                    ESP_LOGI(TAG, "TTS->STT test with text: %s", input_buffer);

                    char *result = baidu_text_to_speech_to_text(input_buffer);

                    if (result)
                    {
                        ESP_LOGI(TAG, "Round-trip success!");
                        printf("\r\n=== Recognition Result ===\r\n");
                        printf("%s\r\n", result);
                        printf("========================\r\n");
                        free(result);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Round-trip test failed");
                    }
                }

                input_index = 0;
            }
        }
        else if (ch >= 32)
        {
            if (input_index < sizeof(input_buffer) - 1)
            {
                input_buffer[input_index++] = (char)ch;
                putchar(ch);
            }
        }
        else if (ch == '\b' || ch == 127)
        {
            if (input_index > 0)
            {
                input_index--;
                printf("\b \b");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
