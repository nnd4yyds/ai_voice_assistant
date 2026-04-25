#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_afe_sr_models.h"
#include "model_path.h"
#include "driver/i2s_std.h"
#include "secrets.h"
#include "i2s_audio.h"
#include "wakeup.h"
#include "voice_handler.h"

static const char *TAG = "wakeup";

volatile bool wakeup_detected = false;
static esp_afe_sr_data_t *afe_data = NULL;
static esp_afe_sr_iface_t *afe_handle = NULL;
static srmodel_list_t *g_srmodels = NULL;
static i2s_chan_handle_t g_rx_handle = NULL;
static i2s_chan_handle_t g_tx_handle = NULL;

static const char *safe_model_str(const char *s)
{
    return (s && s[0]) ? s : "unknown";
}

static char *find_wakenet_model(srmodel_list_t *models)
{
    if (!models || !models->model_name || models->num <= 0) {
        return NULL;
    }

    const char *preferred_model_name = "wn9_nihaoxiaozhi_tts";

    for (int i = 0; i < models->num; i++) {
        char *name = models->model_name[i];
        if (name && strcmp(name, preferred_model_name) == 0) {
            return name;
        }
    }

    for (int i = 0; i < models->num; i++) {
        char *name = models->model_name[i];
        if (name && strstr(name, "wn9") && strstr(name, "nihaoxiaozhi")) {
            return name;
        }
    }

    for (int i = 0; i < models->num; i++) {
        char *name = models->model_name[i];
        if (name && strstr(name, "wn")) {
            return name;
        }
    }

    return NULL;
}

static const char *find_model_info_by_name(srmodel_list_t *models, const char *model_name)
{
    if (!models || !models->model_name || !models->model_info || !model_name) {
        return NULL;
    }

    for (int i = 0; i < models->num; i++) {
        if (models->model_name[i] && strcmp(models->model_name[i], model_name) == 0) {
            return models->model_info[i];
        }
    }

    return NULL;
}

esp_afe_sr_iface_t *wakeup_detection_init(void)
{
    return (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
}

esp_afe_sr_data_t *wakeup_detection_create(esp_afe_sr_iface_t *afe_handle, i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    g_rx_handle = rx;
    g_tx_handle = tx;

    g_srmodels = esp_srmodel_init("model");
    if (!g_srmodels) {
        ESP_LOGE(TAG, "Failed to initialize SR models from partition label 'model'");
        return NULL;
    }

    ESP_LOGI(TAG, "Available SR models: %d", g_srmodels->num);
    for (int i = 0; i < g_srmodels->num; i++) {
        const char *model_name = (g_srmodels->model_name && g_srmodels->model_name[i])
                                     ? g_srmodels->model_name[i]
                                     : NULL;
        const char *model_info = (g_srmodels->model_info && g_srmodels->model_info[i])
                                     ? g_srmodels->model_info[i]
                                     : NULL;

        ESP_LOGI(TAG, "  model[%d]: %s (%s)", i, safe_model_str(model_name),
                 safe_model_str(model_info));
    }

    char *wakenet_model_name = find_wakenet_model(g_srmodels);
    if (!wakenet_model_name) {
        ESP_LOGE(TAG, "WakeNet model for '你好小智' was not found in the flashed model partition");
        ESP_LOGE(TAG, "Enable CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS and rebuild/flash the model partition");
        return NULL;
    }

    const char *wakenet_model_info = find_model_info_by_name(g_srmodels, wakenet_model_name);
    ESP_LOGI(TAG, "Selected WakeNet model: %s", wakenet_model_name);
    ESP_LOGI(TAG, "Selected WakeNet model info: %s", safe_model_str(wakenet_model_info));

    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "Heap before AFE create: internal=%u bytes, psram=%u bytes",
             (unsigned)internal_free, (unsigned)psram_free);

    if (psram_free == 0) {
        ESP_LOGE(TAG, "PSRAM is not available, skip WakeNet AFE creation to avoid esp-sr NULL dereference");
        ESP_LOGE(TAG, "Enable/configure PSRAM for ESP32-S3 and rebuild. WakeNet9 model '%s' requires PSRAM on this project.",
                 wakenet_model_name);
        return NULL;
    }

    afe_config_t afe_config = AFE_CONFIG_DEFAULT();

    /*
     * This project uses a single microphone and no reference channel.
     *
     * The ESP32-S3 default AFE config is intended for a 2-mic + 1-ref board
     * and allocates most buffers from PSRAM. On boards/configurations without
     * PSRAM this can make create_from_config() dereference NULL internally and
     * panic with StoreProhibited just after "wakenet_init: 1".
     *
     * Keep the first boot path conservative: WakeNet only, single channel.
     * Put large esp-sr buffers in PSRAM. VAD/SE can be re-enabled later after
     * PSRAM and channel layout are confirmed.
     */
    afe_config.aec_init = false;
    afe_config.se_init = false;
    afe_config.vad_init = false;
    afe_config.wakenet_init = true;
    afe_config.wakenet_model_name = wakenet_model_name;
    afe_config.wakenet_model_name_2 = NULL;
    afe_config.wakenet_mode = DET_MODE_90;
    afe_config.voice_communication_init = false;
    afe_config.voice_communication_agc_init = false;
    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.afe_perferred_core = 1;
    afe_config.afe_perferred_priority = 5;
    afe_config.afe_ringbuf_size = 50;
    afe_config.afe_linear_gain = 1.0;
    afe_config.afe_mode = SR_MODE_LOW_COST;
    afe_config.agc_mode = AFE_MN_PEAK_AGC_MODE_2;
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;
    afe_config.pcm_config.sample_rate = 16000;
    afe_config.debug_init = false;
    afe_config.afe_ns_mode = NS_MODE_SSP;
    afe_config.afe_ns_model_name = NULL;
    afe_config.fixed_first_channel = true;

    afe_data = afe_handle->create_from_config(&afe_config);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data");
        return NULL;
    }

    ESP_LOGI(TAG, "Wakeup detection initialized");
    ESP_LOGI(TAG, "Wake word: 'Ni Hao Xiao Zhi' (你好小智)");
    return afe_data;
}

bool is_wakeup_detected(void)
{
    return wakeup_detected;
}

void reset_wakeup_flag(void)
{
    wakeup_detected = false;
}

void task_wakeup_detection(void *pvParameters)
{
    ESP_LOGI(TAG, "Wakeup detection task started");

    afe_handle = wakeup_detection_init();
    if (!afe_handle) {
        ESP_LOGE(TAG, "Failed to init AFE handle");
        vTaskDelete(NULL);
        return;
    }

    afe_data = wakeup_detection_create(afe_handle, g_rx_handle, g_tx_handle);
    if (!afe_data) {
        ESP_LOGE(TAG, "Failed to create AFE data");
        vTaskDelete(NULL);
        return;
    }

    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);
    ESP_LOGI(TAG, "Audio chunk size: %d samples", audio_chunksize);

    int16_t *audio_buffer = calloc(audio_chunksize, sizeof(int16_t));
    if (!audio_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        vTaskDelete(NULL);
        return;
    }

    int feed_size = audio_chunksize;
    uint32_t audio_stat_counter = 0;

    ESP_LOGI(TAG, "Wakeup detection running...");

    while (1) {
        int samples_read = i2s_read_microphone_optimized(g_rx_handle, audio_buffer, feed_size);

        if (samples_read == feed_size) {
            int64_t sum_abs = 0;
            int16_t max_abs = 0;
            for (int i = 0; i < samples_read; i++) {
                int16_t v = audio_buffer[i] < 0 ? -audio_buffer[i] : audio_buffer[i];
                sum_abs += v;
                if (v > max_abs) {
                    max_abs = v;
                }
            }

            if (++audio_stat_counter >= 100) {
                audio_stat_counter = 0;
                ESP_LOGI(TAG, "Mic level: avg_abs=%lld, peak_abs=%d", sum_abs / samples_read, max_abs);
            }

            afe_handle->feed(afe_data, audio_buffer);

            afe_fetch_result_t *result = afe_handle->fetch(afe_data);
            if (result && result->wakeup_state == WAKENET_DETECTED) {
                wakeup_detected = true;
                ESP_LOGI(TAG, "Wake word detected! wake_word_index=%d, wakenet_model_index=%d",
                         result->wake_word_index, result->wakenet_model_index);

                voice_handler_process();
                reset_wakeup_flag();
            }
        } else if (samples_read > 0) {
            ESP_LOGW(TAG, "Short microphone read: %d/%d samples", samples_read, feed_size);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(audio_buffer);
    vTaskDelete(NULL);
}

void wakeup_set_handles(i2s_chan_handle_t rx, i2s_chan_handle_t tx)
{
    g_rx_handle = rx;
    g_tx_handle = tx;
}
