#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"

static const char *TAG = "Test_App";

// WiFi配置
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define WIFI_MAXIMUM_RETRY 5

// 测试URL
#define TEST_URL "https://httpbin.org/get"

// 全局变量
static int s_retry_num = 0;
static bool wifi_connected = false;

// WiFi事件处理
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            ESP_LOGI(TAG, "connect to the AP fail");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        wifi_connected = true;
    }
}

// WiFi初始化
void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

// HTTP客户端测试
void http_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "HTTP test task started");
    
    // 等待WiFi连接
    while (!wifi_connected) {
        ESP_LOGI(TAG, "Waiting for WiFi connection...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "WiFi connected, starting HTTP test...");
    
    // HTTP客户端配置
    esp_http_client_config_t config = {
        .url = TEST_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        
        ESP_LOGI(TAG, "HTTP GET request successful");
        ESP_LOGI(TAG, "Status code: %d", status_code);
        ESP_LOGI(TAG, "Content length: %d", content_length);
        
        // 读取响应内容
        if (content_length > 0) {
            char *response = malloc(content_length + 1);
            if (response) {
                esp_http_client_read(client, response, content_length);
                response[content_length] = '\0';
                ESP_LOGI(TAG, "Response: %s", response);
                free(response);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    
    // 测试完成，删除任务
    ESP_LOGI(TAG, "HTTP test completed");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Test Application Started");
    ESP_LOGI(TAG, "ESP32-S3 WiFi and HTTP Test");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化WiFi
    wifi_init_sta();
    
    // 创建HTTP测试任务
    xTaskCreate(http_test_task, "http_test", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Test tasks created successfully");
}