#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "secrets.h"
#include "i2s_audio.h"
#include <string.h>

static const char *TAG = "i2s_audio";

esp_err_t i2s_microphone_init(i2s_chan_handle_t *rx_handle)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S RX channel");
        return ret;
    }

    // INMP441 microphone outputs 32-bit I2S data (24-bit effective)
    // Configure as 32-bit to match hardware specification
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
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
    ret = i2s_channel_init_std_mode(*rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S RX std mode");
        return ret;
    }
    ret = i2s_channel_enable(*rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S RX channel");
        return ret;
    }
    ESP_LOGI(TAG, "Microphone initialized");
    return ESP_OK;
}

esp_err_t i2s_speaker_init(i2s_chan_handle_t *tx_handle)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    esp_err_t ret = i2s_new_channel(&chan_cfg, tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S TX channel");
        return ret;
    }

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
    ret = i2s_channel_init_std_mode(*tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S TX std mode");
        return ret;
    }
    ret = i2s_channel_enable(*tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S TX channel");
        return ret;
    }
    ESP_LOGI(TAG, "Speaker initialized");
    return ESP_OK;
}

void i2s_play_audio(i2s_chan_handle_t tx_handle, const char *audio_data, int audio_len)
{
    if (!tx_handle || !audio_data || audio_len == 0) {
        return;
    }

    size_t bytes_written = 0;
    i2s_channel_write(tx_handle, audio_data, audio_len, &bytes_written, pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Played %d bytes of audio", bytes_written);
}

// Read audio data from INMP441 microphone and convert 32-bit to 16-bit PCM
// INMP441 outputs 32-bit I2S data (24-bit effective, MSB aligned)
// This function extracts the upper 16 bits for voice recognition APIs
int i2s_read_microphone(i2s_chan_handle_t rx_handle, char *audio_buffer, int buffer_size)
{
    if (!rx_handle || !audio_buffer || buffer_size == 0) {
        return 0;
    }

    // Allocate buffer for 32-bit raw data from I2S
    int32_t *raw_buffer = malloc(buffer_size * sizeof(int32_t));
    if (!raw_buffer) {
        ESP_LOGE(TAG, "Failed to allocate raw buffer");
        return 0;
    }

    size_t bytes_read = 0;
    size_t bytes_to_read = buffer_size * sizeof(int32_t);
    
    // Read 32-bit samples from INMP441
    esp_err_t ret = i2s_channel_read(rx_handle, (uint8_t *)raw_buffer, 
                                     bytes_to_read, &bytes_read, pdMS_TO_TICKS(100));
    
    if (ret != ESP_OK || bytes_read == 0) {
        free(raw_buffer);
        return 0;
    }

    int samples_read = bytes_read / sizeof(int32_t);
    int16_t *output = (int16_t *)audio_buffer;
    
    // Convert 32-bit to 16-bit by extracting upper 16 bits
    for (int i = 0; i < samples_read; i++) {
        int32_t sample = raw_buffer[i];
        output[i] = (int16_t)(sample >> 16);
    }

    free(raw_buffer);
    return samples_read * sizeof(int16_t);
}
