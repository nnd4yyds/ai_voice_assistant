#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "secrets.h"
#include "baidu_api.h"

static const char *TAG = "baidu_api";

static char *access_token = NULL;

esp_err_t baidu_get_access_token(void)
{
    if (access_token) {
        free(access_token);
        access_token = NULL;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=%s&client_secret=%s",
             BAIDU_API_KEY, BAIDU_SECRET_KEY);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            int content_length = esp_http_client_get_content_length(client);
            char *response = malloc(content_length + 1);
            if (response) {
                esp_http_client_read(client, response, content_length);
                response[content_length] = '\0';

                cJSON *json = cJSON_Parse(response);
                if (json) {
                    cJSON *token = cJSON_GetObjectItem(json, "access_token");
                    if (token && cJSON_IsString(token)) {
                        access_token = strdup(token->valuestring);
                        ESP_LOGI(TAG, "Got access_token: %s", access_token);
                    }
                    cJSON_Delete(json);
                }
                free(response);
            }
        }
    }

    esp_http_client_cleanup(client);
    return err;
}

char *baidu_speech_to_text(const char *audio_data, int audio_len)
{
    if (!access_token || !audio_data || audio_len == 0) {
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
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "audio/pcm;rate=16000");
    esp_http_client_set_post_field(client, audio_data, audio_len);

    esp_err_t err = esp_http_client_perform(client);
    char *result = NULL;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            int content_length = esp_http_client_get_content_length(client);
            char *response = malloc(content_length + 1);
            if (response) {
                esp_http_client_read(client, response, content_length);
                response[content_length] = '\0';

                cJSON *json = cJSON_Parse(response);
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
                free(response);
            }
        }
    }

    esp_http_client_cleanup(client);
    return result;
}

esp_err_t baidu_text_to_speech(const char *text, char **audio_data, int *audio_len)
{
    if (!access_token || !text || !audio_data || !audio_len) {
        return ESP_FAIL;
    }

    char url[512];
    snprintf(url, sizeof(url),
             "https://tsn.baidu.com/text2audio?tex=%s&lan=zh&cuid=%s&ctp=1&tok=%s&spd=5&pit=5&vol=5&per=0&aue=4",
             text, "ESP32S3", access_token);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            int content_length = esp_http_client_get_content_length(client);
            *audio_data = malloc(content_length);
            if (*audio_data) {
                esp_http_client_read(client, *audio_data, content_length);
                *audio_len = content_length;
                ESP_LOGI(TAG, "Text to speech success, audio length: %d", content_length);
            }
        }
    }

    esp_http_client_cleanup(client);
    return err;
}
