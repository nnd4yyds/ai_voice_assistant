#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "zhipu_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "secrets.h"

static const char *TAG = "zhipu_api";

#define ZHIPU_API_URL "https://open.bigmodel.cn/api/paas/v4/chat/completions"
#define MAX_HISTORY 10

static char *response_buffer = NULL;
static int response_buffer_len = 0;
static SemaphoreHandle_t response_mutex = NULL;
static cJSON *conversation_history = NULL;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                if (!response_mutex) {
                    response_mutex = xSemaphoreCreateMutex();
                }
                if (xSemaphoreTake(response_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    int new_len = response_buffer_len + evt->data_len;
                    char *new_buf = realloc(response_buffer, new_len + 1);
                    if (new_buf) {
                        response_buffer = new_buf;
                        memcpy(response_buffer + response_buffer_len, evt->data, evt->data_len);
                        response_buffer_len = new_len;
                        response_buffer[response_buffer_len] = '\0';
                    }
                    xSemaphoreGive(response_mutex);
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t zhipu_api_init(void)
{
    if (!conversation_history) {
        conversation_history = cJSON_CreateArray();
    }
    if (!response_mutex) {
        response_mutex = xSemaphoreCreateMutex();
    }
    return ESP_OK;
}

static char* build_request_body(const char *user_message)
{
    if (!conversation_history) {
        zhipu_api_init();
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    
    cJSON *system_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(system_msg, "role", "system");
    cJSON_AddStringToObject(system_msg, "content", "你是一个友好的语音助手，请用简洁的语言回答。");
    cJSON_AddItemToArray(messages, system_msg);
    
    if (cJSON_GetArraySize(conversation_history) > 0) {
        cJSON *history_item = NULL;
        cJSON_ArrayForEach(history_item, conversation_history) {
            cJSON_AddItemReferenceToArray(messages, history_item);
        }
    }
    
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_message);
    cJSON_AddItemToArray(messages, user_msg);
    
    cJSON_AddItemToObject(root, "messages", messages);
    cJSON_AddStringToObject(root, "model", "glm-4-flash");
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_str;
}

static void add_to_history(const char *role, const char *content)
{
    if (!conversation_history) {
        zhipu_api_init();
    }
    
    if (cJSON_GetArraySize(conversation_history) >= MAX_HISTORY * 2) {
        cJSON_DeleteItemFromArray(conversation_history, 0);
        cJSON_DeleteItemFromArray(conversation_history, 0);
    }
    
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", role);
    cJSON_AddStringToObject(msg, "content", content);
    cJSON_AddItemToArray(conversation_history, msg);
}

char* zhipu_chat(const char *message, char *response_out, int max_len)
{
    if (!message || !response_out || max_len <= 0) {
        return NULL;
    }
    
    char *request_body = build_request_body(message);
    if (!request_body) {
        ESP_LOGE(TAG, "Failed to build request body");
        return NULL;
    }
    
    ESP_LOGI(TAG, "Sending to Zhipu GLM: %s", message);
    
    if (response_mutex && xSemaphoreTake(response_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
        xSemaphoreGive(response_mutex);
    }
    
    esp_http_client_config_t config = {
        .url = ZHIPU_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", ZHIPU_API_KEY);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, request_body, strlen(request_body));
    
    ESP_LOGI(TAG, "Request body: %s", request_body);
    
    esp_err_t err = esp_http_client_perform(client);
    
    char *result = NULL;
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d, response_len: %d", status_code, response_buffer_len);
        
        if (status_code == 200 && response_buffer) {
            if (xSemaphoreTake(response_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                cJSON *root = cJSON_Parse(response_buffer);
                if (root) {
                    cJSON *choices = cJSON_GetObjectItem(root, "choices");
                    if (choices && cJSON_IsArray(choices)) {
                        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
                        if (first_choice) {
                            cJSON *message_obj = cJSON_GetObjectItem(first_choice, "message");
                            if (message_obj) {
                                cJSON *content = cJSON_GetObjectItem(message_obj, "content");
                                if (content && cJSON_IsString(content)) {
                                    int len = strlen(content->valuestring);
                                    if (len < max_len) {
                                        strncpy(response_out, content->valuestring, max_len - 1);
                                        response_out[max_len - 1] = '\0';
                                        result = response_out;
                                        
                                        add_to_history("user", message);
                                        add_to_history("assistant", content->valuestring);
                                        
                                        ESP_LOGI(TAG, "Zhipu response: %s", content->valuestring);
                                    }
                                }
                            }
                        }
                    } else {
                        cJSON *error = cJSON_GetObjectItem(root, "error");
                        if (error) {
                            cJSON *msg = cJSON_GetObjectItem(error, "message");
                            ESP_LOGE(TAG, "API error: %s", msg ? msg->valuestring : "unknown");
                        } else {
                            ESP_LOGE(TAG, "No choices in response: %s", response_buffer);
                        }
                    }
                    cJSON_Delete(root);
                } else {
                    ESP_LOGE(TAG, "Failed to parse JSON: %s", response_buffer);
                }
                xSemaphoreGive(response_mutex);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    free(request_body);
    
    return result;
}

void zhipu_clear_history(void)
{
    if (conversation_history) {
        cJSON_Delete(conversation_history);
        conversation_history = NULL;
    }
    zhipu_api_init();
    ESP_LOGI(TAG, "Conversation history cleared");
}