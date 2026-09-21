#include "audio_capture.h"

#include <string.h>

#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "es8311.h"
#include "esp_log.h"

static const char *TAG = "audio_capture";

#define AUDIO_I2C_SDA 47
#define AUDIO_I2C_SCL 48
#define AUDIO_I2S_MCLK GPIO_NUM_18
#define AUDIO_I2S_BCLK GPIO_NUM_17
#define AUDIO_I2S_WS GPIO_NUM_15
#define AUDIO_I2S_DIN GPIO_NUM_16
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_ES8311_ADDR ES8311_ADDRESS_0 /* 0x18：CE 拉低 */

static i2s_chan_handle_t s_rx_chan;

esp_err_t audio_capture_init(void) {
    /* espressif/es8311 1.0.0 基于旧版 I2C 驱动（i2c_port_t），须先安装 legacy master。 */
    const i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AUDIO_I2C_SDA,
        .scl_io_num = AUDIO_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, /* 0 会在 i2c_ll 时钟分频处除零崩溃 */
        .clk_flags = 0,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &i2c_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c param config: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c driver install: %s", esp_err_to_name(err));
        return err;
    }

    es8311_handle_t codec = es8311_create(I2C_NUM_0, AUDIO_ES8311_ADDR);
    if (codec == NULL) {
        ESP_LOGE(TAG, "es8311 create failed（I2C 无应答）");
        return ESP_FAIL;
    }
    const es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = 256u * AUDIO_SAMPLE_RATE,
        .sample_frequency = AUDIO_SAMPLE_RATE,
    };
    err = es8311_init(codec, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es8311 init: %s", esp_err_to_name(err));
        return err;
    }
    es8311_microphone_config(codec, false);
    es8311_microphone_gain_set(codec, ES8311_MIC_GAIN_24DB);
    ESP_LOGI(TAG, "es8311 ready (16kHz/16bit, addr 0x18)");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    err = i2s_new_channel(&chan_cfg, NULL, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s new channel: %s", esp_err_to_name(err));
        return err;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = AUDIO_I2S_DIN,
            .invert_flags = {false, false, false},
        },
    };
    err = i2s_channel_init_std_mode(s_rx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s std init: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s enable: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "i2s rx running (MCLK%lu BCLK%lu WS%lu DIN%lu)",
             (unsigned long)AUDIO_I2S_MCLK, (unsigned long)AUDIO_I2S_BCLK,
             (unsigned long)AUDIO_I2S_WS, (unsigned long)AUDIO_I2S_DIN);
    return ESP_OK;
}

esp_err_t audio_capture_read(int16_t *samples, size_t count, size_t *read_out) {
    size_t bytes_read = 0;
    const esp_err_t err = i2s_channel_read(s_rx_chan, samples, count * sizeof(int16_t), &bytes_read, pdMS_TO_TICKS(2000));
    if (read_out) *read_out = bytes_read / sizeof(int16_t);
    return err;
}
