#include "audio_capture.h"

#include <string.h>

#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "es8311.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "pm1.h"

static const char *TAG = "audio_capture";

#define AUDIO_I2C_SDA 47
#define AUDIO_I2C_SCL 48
#define AUDIO_I2S_MCLK GPIO_NUM_18
#define AUDIO_I2S_BCLK GPIO_NUM_17
#define AUDIO_I2S_WS GPIO_NUM_15
#define AUDIO_I2S_DIN GPIO_NUM_16
#define AUDIO_I2S_DOUT GPIO_NUM_14
#define AUDIO_SAMPLE_RATE 16000
/* MCLK 倍数：codec 与 I2S 必须一致。2026-09-23 时基排查候选 A（256→384）：
 * 实测：候选 A(384) 仍不符（比值 2.56→2.00），coeff 表假设与实际不符；
 * 待按 M5Unified 官方配置核对后定值（见 evidence/M3/2026-09-23-采音时基诊断.md）。 */
#define AUDIO_MCLK_MULTIPLE 256u
#define AUDIO_ES8311_ADDR ES8311_ADDRESS_0 /* 0x18：CE 拉低 */

static i2s_chan_handle_t s_rx_chan;
static i2s_chan_handle_t s_tx_chan;
static es8311_handle_t s_codec;

bool audio_capture_ready(void) {
    return s_rx_chan != NULL;
}

static esp_err_t probe_codec_once(es8311_handle_t *out_codec) {
    /* 实测（2026-09-22 启动日志）：L3B(PM1 GPIO2) 未使能时 ES8311 对 I²C 完全无应答
     * （0x18/0x19 均 NACK，而 0x68/0x6E 正常）。L3B 原本只在 display_init 中开启，
     * 而音频探测早于它，故此处必须提前使能 L3B。同时禁用 PM1 看门狗
     * （其复位会清 GPIO 寄存器导致外设失电 NACK）。 */
    if (pm1_init() != ESP_OK || pm1_l3b_lcd(true) != ESP_OK ||
        pm1_es8311(true) != ESP_OK) {
        ESP_LOGE(TAG, "PM1 电源使能失败");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    /* I²C 上电瞬态实测会吞掉首次通信（PM1 同类问题）：create+init 带重试；
     * 并兼容 CE 电平翻转导致的地址漂移（0x18/0x19）。 */
    uint8_t probe_addr = 0;
    for (uint8_t a = 3; a < 0x78; ++a) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (!cmd) continue;
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (uint8_t)((a << 1) | I2C_MASTER_WRITE), true);
        i2c_master_stop(cmd);
        const esp_err_t serr =
            i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (serr == ESP_OK) {
            ESP_LOGI(TAG, "i2c scan ack: 0x%02x", a);
            if (a == ES8311_ADDRESS_0 || a == ES8311_ADDRESS_1) {
                probe_addr = a;
            }
        }
    }
    if (probe_addr == 0) {
        ESP_LOGE(TAG, "总线上无 ES8311 应答（0x18/0x19 均 NACK）");
    }

    es8311_handle_t codec = NULL;
    esp_err_t es_err = ESP_FAIL;
    const uint16_t addrs[2] = {probe_addr ? probe_addr : ES8311_ADDRESS_0,
                               ES8311_ADDRESS_1};
    for (int a = 0; a < 2 && codec == NULL; ++a) {
        for (int attempt = 0; attempt < 2 && codec == NULL; ++attempt) {
            codec = es8311_create(I2C_NUM_0, addrs[a]);
            if (codec != NULL) {
                const es8311_clock_config_t clk_cfg = {
                    .mclk_inverted = false,
                    .sclk_inverted = false,
                    .mclk_from_mclk_pin = true,
                    .mclk_frequency = AUDIO_MCLK_MULTIPLE * AUDIO_SAMPLE_RATE,
                    .sample_frequency = AUDIO_SAMPLE_RATE,
                };
                es_err = es8311_init(codec, &clk_cfg, ES8311_RESOLUTION_16,
                                     ES8311_RESOLUTION_16);
                if (es_err != ESP_OK) {
                    ESP_LOGW(TAG, "es8311 init@0x%02x: %s（第 %d 次）",
                             addrs[a], esp_err_to_name(es_err), attempt + 1);
                    es8311_delete(codec);
                    codec = NULL;
                    vTaskDelay(pdMS_TO_TICKS(50));
                } else {
                    ESP_LOGI(TAG, "es8311 ready @0x%02x", addrs[a]);
                }
            }
        }
    }
    if (codec == NULL) {
        ESP_LOGE(TAG, "es8311 初始化失败（双地址重试后仍无应答）");
        return es_err;
    }
    es8311_microphone_config(codec, false);
    es8311_microphone_gain_set(codec, ES8311_MIC_GAIN_24DB);
    *out_codec = codec;
    return ESP_OK;
}

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

    es8311_handle_t codec = NULL;
    for (int round = 1; round <= 5 && codec == NULL; ++round) {
        if (round > 1) {
            ESP_LOGW(TAG, "codec 重试第 %d/5 轮", round);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        if (probe_codec_once(&codec) == ESP_OK) {
            break;
        }
    }
    if (codec == NULL) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "es8311 ready (16kHz/16bit)");
    s_codec = codec;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    err = i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s new channel: %s", esp_err_to_name(err));
        return err;
    }
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        /* STEREO 而非 MONO（2026-09-23 实测）：MONO 槽位使帧宽=1×16 位，
         * WS 变为 BCLK/16=32kHz，而全链按 16kHz → 2 倍拉伸（tone 1kHz→500Hz）。
         * STEREO 帧宽 2×16=32 位 → WS=16kHz；读取时取左声道。 */
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_WS,
            .dout = AUDIO_I2S_DOUT,
            .din = AUDIO_I2S_DIN,
            .invert_flags = {false, false, false},
        },
    };
    std_cfg.clk_cfg.mclk_multiple = AUDIO_MCLK_MULTIPLE == 384 ? I2S_MCLK_MULTIPLE_384 : I2S_MCLK_MULTIPLE_256; /* 与 AUDIO_MCLK_MULTIPLE 保持一致 */
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
    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx std init: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx enable: %s", esp_err_to_name(err));
        return err;
    }
    if (s_codec != NULL) {
        int vol = 0;
        es8311_voice_volume_set(s_codec, 70, &vol);
        es8311_voice_mute(s_codec, false);
    }
    ESP_LOGI(TAG, "i2s rx running (MCLK%lu BCLK%lu WS%lu DIN%lu)",
             (unsigned long)AUDIO_I2S_MCLK, (unsigned long)AUDIO_I2S_BCLK,
             (unsigned long)AUDIO_I2S_WS, (unsigned long)AUDIO_I2S_DIN);
    return ESP_OK;
}

void audio_capture_log_clocks(void) {
    if (s_rx_chan == NULL) {
        ESP_LOGW(TAG, "clocks: i2s 未初始化");
        return;
    }
    i2s_chan_info_t info = {0};
    if (i2s_channel_get_info(s_rx_chan, &info) != ESP_OK) {
        ESP_LOGW(TAG, "clocks: get_info 失败");
        return;
    }
    ESP_LOGI(TAG, "clocks: clk_src=%d sclk=%luHz mclk=%luHz bclk=%luHz (期望 MCLK=4096000)",
             (int)info.clk_src, (unsigned long)info.sclk_hz,
             (unsigned long)info.mclk_hz, (unsigned long)info.bclk_hz);
}

uint32_t audio_capture_probe_rate(uint32_t window_ms) {
    static int16_t buf[960];
    if (s_rx_chan == NULL) return 0;
    const int64_t t0 = esp_timer_get_time();
    uint32_t total = 0;
    while ((esp_timer_get_time() - t0) < (int64_t)window_ms * 1000) {
        size_t got = 0;
        if (audio_capture_read(buf, 960, &got) != ESP_OK) break;
        total += (uint32_t)got;
    }
    const int64_t dt = esp_timer_get_time() - t0;
    const uint32_t rate = dt > 0 ? (uint32_t)((uint64_t)total * 1000000ull / (uint64_t)dt) : 0;
    ESP_LOGI(TAG, "rate_probe: %lu samples in %lldms -> %lu/s (期望 16000)",
             (unsigned long)total, (long long)(dt / 1000), (unsigned long)rate);
    return rate;
}

esp_err_t audio_capture_read(int16_t *samples, size_t count, size_t *read_out) {
    static int16_t stereo[2 * 960];
    if (count > 960) count = 960;
    size_t bytes_read = 0;
    const esp_err_t err = i2s_channel_read(s_rx_chan, stereo,
                                           count * 2 * sizeof(int16_t), &bytes_read,
                                           pdMS_TO_TICKS(2000));
    const size_t frames = bytes_read / (2 * sizeof(int16_t));
    for (size_t i = 0; i < frames; i += 1) {
        samples[i] = stereo[i * 2];
    }
    if (read_out) *read_out = frames;
    return err;
}

esp_err_t audio_capture_play(const int16_t *mono, size_t samples) {
    if (s_tx_chan == NULL || mono == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    static int16_t stereo[2 * 960];
    size_t done = 0;
    while (done < samples) {
        size_t n = samples - done;
        if (n > 960) {
            n = 960;
        }
        for (size_t i = 0; i < n; ++i) {
            stereo[2 * i] = mono[done + i];
            stereo[2 * i + 1] = mono[done + i];
        }
        size_t written = 0;
        const esp_err_t err = i2s_channel_write(s_tx_chan, stereo, n * 2 * sizeof(int16_t),
                                                &written, pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            return err;
        }
        done += n;
    }
    return ESP_OK;
}

esp_err_t audio_capture_beep(void) {
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    static int16_t tone[2400]; /* 150ms @16kHz */
    const size_t n = sizeof(tone) / sizeof(tone[0]);
    for (size_t i = 0; i < n; ++i) {
        /* 1kHz 方波：每 8 个样本翻转（16kHz/1000/2 = 8） */
        tone[i] = ((i / (AUDIO_SAMPLE_RATE / 1000 / 2)) % 2u == 0) ? (int16_t)10000
                                                                  : (int16_t)-10000;
    }
    pm1_es8311(true); /* 功放仅在提示音期间使能 */
    vTaskDelay(pdMS_TO_TICKS(10));
    if (s_codec != NULL) {
        es8311_voice_volume_set(s_codec, 70, NULL);
        es8311_voice_mute(s_codec, false);
    }
    audio_capture_play(tone, n);
    /* 静音尾（20ms 0）：防止 I2S underrun 停在方波沿上产生持续电平 */
    static int16_t silence[320];
    audio_capture_play(silence, sizeof(silence) / sizeof(silence[0]));
    if (s_codec != NULL) {
        es8311_voice_mute(s_codec, true);
    }
    pm1_es8311(false); /* 关功放 → 杜绝残留长鸣 */
    return ESP_OK;
}

esp_err_t audio_capture_beep_n(uint32_t n) {
    esp_err_t err = ESP_OK;
    for (uint32_t i = 0; i < n; ++i) {
        if (i > 0u) {
            vTaskDelay(pdMS_TO_TICKS(120)); /* 声间静音，避免连成一声长响 */
        }
        err = audio_capture_beep();
        if (err != ESP_OK) {
            return err;
        }
    }
    return err;
}
