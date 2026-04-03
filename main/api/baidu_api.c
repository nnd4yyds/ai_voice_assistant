#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "secrets.h"
#include "baidu_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"

static const char *TAG = "baidu_api";

static char *access_token = NULL;
static SemaphoreHandle_t token_mutex = NULL;
static char *response_buffer = NULL;
static int response_buffer_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                int new_len = response_buffer_len + evt->data_len;
                char *new_buf = realloc(response_buffer, new_len + 1);
                if (new_buf) {
                    response_buffer = new_buf;
                    memcpy(response_buffer + response_buffer_len, evt->data, evt->data_len);
                    response_buffer_len = new_len;
                    response_buffer[response_buffer_len] = '\0';
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

static void ensure_mutex(void)
{
    if (!token_mutex) {
        token_mutex = xSemaphoreCreateMutex();
    }
}

static void url_encode(const char *src, char *dest, size_t dest_len)
{
    const char *hex = "0123456789ABCDEF";
    size_t j = 0;
    
    for (size_t i = 0; src[i] && j < dest_len - 3; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dest[j++] = c;
        } else if (j + 3 < dest_len) {
            dest[j++] = '%';
            dest[j++] = hex[(c >> 4) & 0x0F];
            dest[j++] = hex[c & 0x0F];
        }
    }
    dest[j] = '\0';
}

static esp_err_t try_get_token(const char *url)
{
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
    
    ESP_LOGI(TAG, "Requesting token...");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP status: %d, content_length: %d, buffer_len: %d", 
                 status_code, content_length, response_buffer_len);
        
        if (status_code == 200) {
            if (response_buffer && response_buffer_len > 0) {
                ESP_LOGI(TAG, "Response from event handler: %s", response_buffer);
            }
            
            if ((!response_buffer || response_buffer_len == 0) && content_length > 0) {
                ESP_LOGI(TAG, "Trying esp_http_client_read...");
                response_buffer = malloc(content_length + 1);
                if (response_buffer) {
                    int read_len = esp_http_client_read(client, response_buffer, content_length);
                    if (read_len > 0) {
                        response_buffer_len = read_len;
                        response_buffer[read_len] = '\0';
                        ESP_LOGI(TAG, "Response from read: %s", response_buffer);
                    }
                }
            }
            
            if (response_buffer && response_buffer_len > 0) {
                cJSON *json = cJSON_Parse(response_buffer);
                if (json) {
                    cJSON *token_obj = cJSON_GetObjectItem(json, "access_token");
                    if (token_obj && cJSON_IsString(token_obj)) {
                        access_token = strdup(token_obj->valuestring);
                        ESP_LOGI(TAG, "Got access_token successfully");
                        cJSON_Delete(json);
                        esp_http_client_cleanup(client);
                        return ESP_OK;
                    } else {
                        ESP_LOGE(TAG, "No access_token in response");
                    }
                    cJSON_Delete(json);
                } else {
                    ESP_LOGE(TAG, "Failed to parse JSON: %s", response_buffer);
                }
            } else {
                ESP_LOGE(TAG, "No response data available");
            }
        } else {
            ESP_LOGE(TAG, "HTTP status: %d, len: %d", status_code, response_buffer_len);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %d (%s)", err, esp_err_to_name(err));
    }

    if (response_buffer) {
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
    }

    esp_http_client_cleanup(client);
    return ESP_FAIL;
}

esp_err_t baidu_get_access_token(void)
{
    ensure_mutex();
    if (xSemaphoreTake(token_mutex, pdMS_TO_TICKS(120000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire token mutex");
        return ESP_FAIL;
    }

    if (access_token) {
        free(access_token);
        access_token = NULL;
    }

    // 使用 GET 方法，参数放在 URL 中
    char url[512];
    snprintf(url, sizeof(url),
             "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%s&client_secret=%s",
             BAIDU_API_KEY, BAIDU_SECRET_KEY);

    ESP_LOGI(TAG, "Requesting access token from Baidu API...");

    // 重试 3 次，每次间隔 3 秒
    esp_err_t err = ESP_FAIL;
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            ESP_LOGI(TAG, "Retrying... (%d/3)", retry + 1);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        
        err = try_get_token(url);
        if (err == ESP_OK) {
            break;
        }
    }

    xSemaphoreGive(token_mutex);
    return err;
}

char *baidu_speech_to_text(const char *audio_data, int audio_len)
{
    ensure_mutex();
    if (xSemaphoreTake(token_mutex, pdMS_TO_TICKS(20000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire token mutex");
        return NULL;
    }

    if (!access_token || !audio_data || audio_len == 0) {
        xSemaphoreGive(token_mutex);
        return NULL;
    }

    char url[512];
    snprintf(url, sizeof(url),
             "https://vop.baidu.com/server_api?dev_pid=1537&cuid=%s&token=%s",
             "ESP32S3", access_token);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };

    response_buffer = NULL;
    response_buffer_len = 0;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "audio/pcm;rate=16000");
    esp_http_client_set_post_field(client, audio_data, audio_len);

    esp_err_t err = esp_http_client_perform(client);
    char *result = NULL;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "STT HTTP status: %d, buffer_len: %d", status_code, response_buffer_len);
        if (status_code == 200 && response_buffer && response_buffer_len > 0) {
            cJSON *json = cJSON_Parse(response_buffer);
            if (json) {
                cJSON *result_array = cJSON_GetObjectItem(json, "result");
                if (result_array && cJSON_IsArray(result_array)) {
                    cJSON *first_result = cJSON_GetArrayItem(result_array, 0);
                    if (first_result && cJSON_IsString(first_result)) {
                        result = strdup(first_result->valuestring);
                        ESP_LOGI(TAG, "Speech recognition result: %s", result);
                    }
                }
                cJSON_Delete(json);
            }
        }
    }

    esp_http_client_cleanup(client);
    if (response_buffer) {
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
    }
    xSemaphoreGive(token_mutex);
    return result;
}

esp_err_t baidu_text_to_speech(const char *text, char **audio_data, int *audio_len)
{
    ensure_mutex();
    if (xSemaphoreTake(token_mutex, pdMS_TO_TICKS(20000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire token mutex");
        return ESP_FAIL;
    }

    if (!access_token || !text || !audio_data || !audio_len) {
        xSemaphoreGive(token_mutex);
        return ESP_FAIL;
    }

    char encoded_text[1024];
    url_encode(text, encoded_text, sizeof(encoded_text));
    
    char url[2048];
    snprintf(url, sizeof(url),
             "https://tsn.baidu.com/text2audio?tex=%s&lan=zh&cuid=%s&ctp=1&tok=%s&spd=5&pit=5&vol=5&per=0&aue=4&ie=UTF-8",
             encoded_text, "ESP32S3", access_token);
    ESP_LOGI(TAG, "TTS URL: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 20000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
    };

    response_buffer = NULL;
    response_buffer_len = 0;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "TTS HTTP status: %d, buffer_len: %d", status_code, response_buffer_len);
        
        if (status_code == 200 && response_buffer && response_buffer_len > 0) {
            ESP_LOGI(TAG, "First 4 bytes hex: %02X %02X %02X %02X", 
                     (unsigned char)response_buffer[0], (unsigned char)response_buffer[1], 
                     (unsigned char)response_buffer[2], (unsigned char)response_buffer[3]);
            
            if (response_buffer[0] == '{') {
                ESP_LOGE(TAG, "TTS returned JSON error: %s", response_buffer);
            } else {
                *audio_data = response_buffer;
                *audio_len = response_buffer_len;
                response_buffer = NULL;
                response_buffer_len = 0;
                ESP_LOGI(TAG, "Text to speech success, audio length: %d", *audio_len);
            }
        } else {
            ESP_LOGE(TAG, "TTS failed: status=%d, buffer_len=%d", status_code, response_buffer_len);
        }
    }

    esp_http_client_cleanup(client);
    if (response_buffer) {
        free(response_buffer);
        response_buffer = NULL;
        response_buffer_len = 0;
    }
    xSemaphoreGive(token_mutex);
    return err;
}

char *baidu_text_to_speech_to_text(const char *text)
{
    char *audio_data = NULL;
    int audio_len = 0;
    
    esp_err_t err = baidu_text_to_speech(text, &audio_data, &audio_len);
    if (err != ESP_OK || !audio_data || audio_len == 0) {
        ESP_LOGE(TAG, "TTS failed, cannot proceed to STT");
        return NULL;
    }
    
    ESP_LOGI(TAG, "TTS done, audio_len=%d, sending to STT...", audio_len);
    
    char *result = baidu_speech_to_text(audio_data, audio_len);
    free(audio_data);
    
    return result;
}

const char *baidu_get_access_token_str(void)
{
    return access_token;
}
