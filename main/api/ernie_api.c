#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "ernie_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "secrets.h"

static const char *TAG = "ernie_api";

#define ERNIE_API_URL "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/completions"
#define MAX_HISTORY 10

static char *response_buffer = NULL;
static int response_buffer_len = 0;
static SemaphoreHandle_t response_mutex = NULL;
static cJSON *conversation_history = NULL;
static char *qianfan_access_token = NULL;
static SemaphoreHandle_t qianfan_token_mutex = NULL;

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
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_err_t qianfan_get_access_token(void)
{
    if (!qianfan_token_mutex) {
        qianfan_token_mutex = xSemaphoreCreateMutex();
    }
    
    if (xSemaphoreTake(qianfan_token_mutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire qianfan token mutex");
        return ESP_FAIL;
    }
    
    if (qianfan_access_token) {
        free(qianfan_access_token);
        qianfan_access_token = NULL;
    }
    
    char url[512];
    snprintf(url, sizeof(url),
             "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%s&client_secret=%s",
             QIANFAN_API_KEY, QIANFAN_SECRET_KEY);
    
    ESP_LOGI(TAG, "Requesting Qianfan access token...");
    
    response_buffer = NULL;
    response_buffer_len = 0;
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 && response_buffer && response_buffer_len > 0) {
            cJSON *json = cJSON_Parse(response_buffer);
            if (json) {
                cJSON *token_obj = cJSON_GetObjectItem(json, "access_token");
                if (token_obj && cJSON_IsString(token_obj)) {
                    qianfan_access_token = strdup(token_obj->valuestring);
                    ESP_LOGI(TAG, "Got Qianfan access_token successfully");
                    cJSON_Delete(json);
                    esp_http_client_cleanup(client);
                    xSemaphoreGive(qianfan_token_mutex);
                    return ESP_OK;
                }
                cJSON_Delete(json);
            }
        }
        ESP_LOGE(TAG, "Qianfan token request failed: status=%d, response=%s", status_code, response_buffer ? response_buffer : "NULL");
    }
    
    if (response_buffer) {
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
    }
    
    esp_http_client_cleanup(client);
    xSemaphoreGive(qianfan_token_mutex);
    return ESP_FAIL;
}

const char *qianfan_get_access_token_str(void)
{
    return qianfan_access_token;
}

esp_err_t ernie_api_init(void)
{
    if (!conversation_history) {
        conversation_history = cJSON_CreateArray();
    }
    if (!response_mutex) {
        response_mutex = xSemaphoreCreateMutex();
    }
    if (!qianfan_token_mutex) {
        qianfan_token_mutex = xSemaphoreCreateMutex();
    }
    return ESP_OK;
}

static char* build_request_body(const char *user_message)
{
    if (!conversation_history) {
        ernie_api_init();
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON *messages = cJSON_CreateArray();
    
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
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_str;
}

static void add_to_history(const char *role, const char *content)
{
    if (!conversation_history) {
        ernie_api_init();
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

char* ernie_chat(const char *user_message, char *response_buffer_out, int buffer_size)
{
    if (!qianfan_access_token) {
        ESP_LOGI(TAG, "No Qianfan token, requesting...");
        if (qianfan_get_access_token() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get Qianfan access token");
            return NULL;
        }
    }
    
    if (!qianfan_access_token) {
        ESP_LOGE(TAG, "No Qianfan access token available");
        return NULL;
    }
    
    const char *token = qianfan_access_token;
    
    char url[1024];
    snprintf(url, sizeof(url), "%s?access_token=%s", ERNIE_API_URL, token);
    
    char *request_body = build_request_body(user_message);
    if (!request_body) {
        ESP_LOGE(TAG, "Failed to build request body");
        return NULL;
    }
    
    ESP_LOGI(TAG, "Sending message to ERNIE: %s", user_message);
    ESP_LOGD(TAG, "Request body: %s", request_body);
    
    if (response_mutex && xSemaphoreTake(response_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
        xSemaphoreGive(response_mutex);
    }
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, request_body, strlen(request_body));
    
    esp_err_t err = esp_http_client_perform(client);
    
    char *result = NULL;
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d, response_len: %d", status_code, response_buffer_len);
        
        if (status_code == 200 && response_buffer) {
            if (xSemaphoreTake(response_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                cJSON *root = cJSON_Parse(response_buffer);
                if (root) {
                    cJSON *result_obj = cJSON_GetObjectItem(root, "result");
                    if (result_obj && cJSON_IsString(result_obj)) {
                        const char *result_str = result_obj->valuestring;
                        int len = strlen(result_str);
                        if (len < buffer_size) {
                            strncpy(response_buffer_out, result_str, buffer_size - 1);
                            response_buffer_out[buffer_size - 1] = '\0';
                            result = response_buffer_out;
                            
                            add_to_history("user", user_message);
                            add_to_history("assistant", result_str);
                            
                            ESP_LOGI(TAG, "ERNIE response: %s", result_str);
                        }
                    } else {
                        ESP_LOGE(TAG, "No 'result' field in response: %s", response_buffer);
                    }
                    cJSON_Delete(root);
                } else {
                    ESP_LOGE(TAG, "Failed to parse JSON response: %s", response_buffer);
                }
                xSemaphoreGive(response_mutex);
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed or no response");
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    free(request_body);
    
    return result;
}

void ernie_clear_history(void)
{
    if (conversation_history) {
        cJSON_Delete(conversation_history);
        conversation_history = NULL;
    }
    ernie_api_init();
    ESP_LOGI(TAG, "Conversation history cleared");
}