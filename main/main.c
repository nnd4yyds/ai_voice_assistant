#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "driver/i2s_std.h"
#include "cJSON.h"

// ESP-SR库（唤醒词检测）
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"

static const char *TAG = "Voice_Assistant";

// WiFi配置
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define WIFI_MAXIMUM_RETRY 5

// 百度AI配置
#define BAIDU_API_KEY "YOUR_API_KEY"
#define BAIDU_SECRET_KEY "YOUR_SECRET_KEY"
#define BAIDU_APP_ID "YOUR_APP_ID"

// I2S引脚配置（默认配置，请根据实际硬件调整）
#define I2S_MIC_BCK_IO     41
#define I2S_MIC_WS_IO      42
#define I2S_MIC_DI_IO      2
#define I2S_SPK_BCK_IO     17
#define I2S_SPK_WS_IO      18
#define I2S_SPK_DO_IO      8

// 音频参数
#define SAMPLE_RATE        16000
#define BITS_PER_SAMPLE    16
#define CHANNEL_NUM        1
#define BUFFER_SIZE        1024
#define AUDIO_BUFFER_SIZE  16000 * 5 // 5秒音频

// 唤醒词检测参数
#define WAKEUP_KEYWORD     "Hi ESP"  // 唤醒词
#define DETECT_MODE        DET_MODE_90 // 检测灵敏度

// 全局变量
static int s_retry_num = 0;
static bool wifi_connected = false;
static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_handle_t tx_handle = NULL;
static char *access_token = NULL;
static char *audio_buffer = NULL;
static int audio_buffer_index = 0;

// ESP-SR相关变量
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
static bool wakeup_detected = false;

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

// I2S麦克风初始化
void i2s_microphone_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_MIC_BCK_IO,
            .ws = I2S_MIC_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_MIC_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

// I2S扬声器初始化
void i2s_speaker_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SPK_BCK_IO,
            .ws = I2S_SPK_WS_IO,
            .dout = I2S_SPK_DO_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

// 初始化唤醒词检测
void wakeup_detection_init(void)
{
    // 获取唤醒词检测接口
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    
    // 配置AFE（音频前端处理）
    afe_config_t afe_config = {
        .aec_init = true,           // 启用回声消除
        .se_init = true,            // 启用语音增强
        .vad_init = true,           // 启用语音活动检测
        .wakenet_init = true,       // 启用唤醒词检测
        .voice_communication_init = false,
        .afe_mode = SR_MODE_HIGH_PERF,  // 高性能模式
        .afe_perferred_core = 0,    // 使用核心0
        .afe_perferred_priority = 5, // 优先级
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_INTERNAL,
        .agc_mode = AFE_MN_PEAK_AGC_MODE_2,
        .pcm_config = {
            .total_ch_num = 1,      // 单声道
            .mic_num = 1,           // 1个麦克风
            .ref_num = 0,           // 无参考信号
        },
    };
    
    // 创建AFE数据
    afe_data = afe_handle->create_from_config(&afe_config);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data");
        return;
    }
    
    ESP_LOGI(TAG, "Wakeup detection initialized");
}

// 唤醒词检测任务
void task_wakeup_detection(void *pvParameters)
{
    ESP_LOGI(TAG, "Wakeup detection task started");
    
    // 等待初始化完成
    while (!afe_data || !rx_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    ESP_LOGI(TAG, "Audio chunk size: %d", audio_chunksize);
    
    // 分配音频缓冲区
    int16_t *audio_buffer = malloc(audio_chunksize * sizeof(int16_t));
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer for wakeup detection");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        // 从麦克风读取音频数据
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer, 
                                        audio_chunksize * sizeof(int16_t), 
                                        &bytes_read, pdMS_TO_TICKS(100));
        
        if (ret == ESP_OK && bytes_read > 0) {
            // 将音频数据送入AFE处理
            afe_handle->feed(afe_data, audio_buffer);
            
            // 获取AFE处理结果
            afe_fetch_result_t* result = afe_handle->fetch(afe_data);
            if (result) {
                // 检查是否检测到唤醒词
                if (result->wakeup_state == WAKENET_DETECTED) {
                    ESP_LOGI(TAG, "Wakeup word detected! Score: %d", result->wake_word_index);
                    wakeup_detected = true;
                    
                    // 可以在这里触发其他操作，如播放提示音
                    // play_wakeup_sound();
                }
                
                // 检查语音活动
                if (result->vad_state == AFE_VAD_SPEECH) {
                    ESP_LOGD(TAG, "Speech detected");
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    free(audio_buffer);
}

// 获取百度access_token
esp_err_t get_baidu_access_token(void)
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

// 语音识别
char* speech_to_text(const char *audio_data, int audio_len)
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
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // 设置请求头
    esp_http_client_set_header(client, "Content-Type", "audio/pcm;rate=16000");
    
    // 设置请求体
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

// 语音合成
esp_err_t text_to_speech(const char *text, char **audio_data, int *audio_len)
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

// 播放音频
void play_audio(const char *audio_data, int audio_len)
{
    if (!tx_handle || !audio_data || audio_len == 0) {
        return;
    }

    size_t bytes_written = 0;
    i2s_channel_write(tx_handle, audio_data, audio_len, &bytes_written, pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Played %d bytes of audio", bytes_written);
}

// 语音识别任务
void task_speech_recognition(void *pvParameters)
{
    ESP_LOGI(TAG, "Speech recognition task started");
    
    while (1) {
        if (!wifi_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // 检查是否检测到唤醒词
        if (wakeup_detected) {
            ESP_LOGI(TAG, "Wakeup detected, starting speech recognition...");
            wakeup_detected = false;
            
            // 播放提示音
            // play_beep();
            
            // 录制语音
            audio_buffer_index = 0;
            memset(audio_buffer, 0, AUDIO_BUFFER_SIZE);
            
            // 录制3秒音频
            for (int i = 0; i < 3 * SAMPLE_RATE / BUFFER_SIZE; i++) {
                size_t bytes_read = 0;
                i2s_channel_read(rx_handle, audio_buffer + audio_buffer_index, 
                               BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100));
                if (bytes_read > 0) {
                    audio_buffer_index += bytes_read;
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            
            // 调用语音识别
            if (audio_buffer_index > 0) {
                char *result = speech_to_text(audio_buffer, audio_buffer_index);
                if (result) {
                    ESP_LOGI(TAG, "Recognized: %s", result);
                    // 这里可以将识别结果发送到队列，供主任务处理
                    free(result);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// 语音合成任务
void task_speech_synthesis(void *pvParameters)
{
    ESP_LOGI(TAG, "Speech synthesis task started");
    
    while (1) {
        if (!wifi_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // 这里可以从队列接收文本，然后调用语音合成
        // 示例：合成并播放一段文本
        static bool first_run = true;
        if (first_run) {
            first_run = false;
            vTaskDelay(pdMS_TO_TICKS(5000)); // 等待5秒后播放
            
            char *audio_data = NULL;
            int audio_len = 0;
            esp_err_t err = text_to_speech("你好，我是ESP32语音助手", &audio_data, &audio_len);
            if (err == ESP_OK && audio_data) {
                play_audio(audio_data, audio_len);
                free(audio_data);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 主任务
void task_main(void *pvParameters)
{
    ESP_LOGI(TAG, "Main task started");
    
    // 等待WiFi连接
    while (!wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // 获取百度access_token
    get_baidu_access_token();
    
    while (1) {
        // 主逻辑
        // 1. 唤醒词检测（已在单独任务中处理）
        // 2. 录音（在语音识别任务中处理）
        // 3. 调用语音识别
        // 4. 处理识别结果
        // 5. 调用语音合成
        // 6. 播放回复
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Voice Assistant Application Started");
    ESP_LOGI(TAG, "ESP32-S3 Voice Assistant Demo with Wakeup Detection");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 分配音频缓冲区
    audio_buffer = malloc(AUDIO_BUFFER_SIZE);
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return;
    }
    
    // 初始化WiFi
    wifi_init_sta();
    
    // 初始化I2S（需要根据实际硬件调整引脚）
    // i2s_microphone_init();
    // i2s_speaker_init();
    
    // 初始化唤醒词检测
    // wakeup_detection_init();
    
    // 创建任务
    xTaskCreate(task_main, "main", 4096, NULL, 5, NULL);
    xTaskCreate(task_wakeup_detection, "wakeup", 4096, NULL, 5, NULL);
    xTaskCreate(task_speech_recognition, "speech_rec", 4096, NULL, 5, NULL);
    xTaskCreate(task_speech_synthesis, "speech_synth", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "All tasks created successfully");
}