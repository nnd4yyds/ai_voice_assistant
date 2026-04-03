#include "esp_log.h"
#include "secrets.h"
#include "i2s_audio.h"

static const char *TAG = "i2s_audio";

void i2s_microphone_init(i2s_chan_handle_t *rx_handle)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, rx_handle));

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
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(*rx_handle));
    ESP_LOGI(TAG, "Microphone initialized");
}

void i2s_speaker_init(i2s_chan_handle_t *tx_handle)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, tx_handle, NULL));

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
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(*tx_handle));
    ESP_LOGI(TAG, "Speaker initialized");
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
