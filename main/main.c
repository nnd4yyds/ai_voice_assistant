#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "FreeRTOS_Test";

// 全局变量
static QueueHandle_t test_queue = NULL;
static SemaphoreHandle_t test_mutex = NULL;

// 任务1: 发送数据到队列
void task_sender(void *pvParameters)
{
    int count = 0;
    
    while (1) {
        count++;
        if (xQueueSend(test_queue, &count, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Sender: Sent value %d", count);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 任务2: 从队列接收数据
void task_receiver(void *pvParameters)
{
    int received_value;
    
    while (1) {
        if (xQueueReceive(test_queue, &received_value, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Receiver: Got value %d", received_value);
            
            // 使用互斥锁保护共享资源
            if (xSemaphoreTake(test_mutex, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "Receiver: Processing value %d", received_value);
                // 模拟处理时间
                vTaskDelay(pdMS_TO_TICKS(100));
                xSemaphoreGive(test_mutex);
            }
        }
    }
}

// 任务3: 周期性任务
void task_periodic(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        ESP_LOGI(TAG, "Periodic task running");
        
        // 延时直到下一个周期
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "FreeRTOS Test Application Started");
    ESP_LOGI(TAG, "ESP32-S3 FreeRTOS Demo");
    
    // 创建队列
    test_queue = xQueueCreate(10, sizeof(int));
    if (test_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue");
        return;
    }
    
    // 创建互斥锁
    test_mutex = xSemaphoreCreateMutex();
    if (test_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vQueueDelete(test_queue);
        return;
    }
    
    // 创建任务
    xTaskCreate(task_sender, "sender", 2048, NULL, 5, NULL);
    xTaskCreate(task_receiver, "receiver", 2048, NULL, 5, NULL);
    xTaskCreate(task_periodic, "periodic", 2048, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully");
}
