// StickS3 固件主入口（M2 骨架 + M01 采音通路）
// 当前：芯片自检 + s3p_proto 黄金向量自检（设备启动期执行）+ 麦克风采音统计（通路证据）。
// BLE（M02）按冻结协议增量进入。
#include "s3p_proto.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_capture.h"
#include "audio_session.h"
#include "ble_gatt.h"
#include "button.h"
#include "display.h"
#include "s3p_state.h"

static const char *TAG = "sticks3";

static void on_button_press(void) {
    audio_session_request_start();
}

static void on_button_release(void) {
    audio_session_request_stop();
}

static int proto_selftest(void) {
    uint8_t buf[S3P_END_LEN] = {0};
    uint8_t payload[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};

    // SV-01：BEGIN（recordingID=1, frameCount=500）
    (void)s3p_encode_begin(buf, 1u, 500u);
    if (buf[0] != S3P_PKT_BEGIN || buf[1] != 1u) return 1;
    s3p_begin_t begin;
    if (s3p_parse(buf, S3P_BEGIN_LEN, &begin, NULL, NULL) != S3P_PARSE_OK) return 2;
    if (begin.recording_id != 1u || begin.frame_count != 500u || begin.sample_rate != 16000u) return 3;

    // SV-02：AUDIO 单片（frag 0/1，8B 载荷）
    uint8_t audio[64] = {0};
    (void)s3p_encode_audio_header(audio, 1u, 0u, 0u, 1u, sizeof(payload));
    for (size_t i = 0; i < sizeof(payload); i += 1) audio[S3P_AUDIO_HDR_LEN + i] = payload[i];
    s3p_audio_t audio_parsed;
    if (s3p_parse(audio, S3P_AUDIO_HDR_LEN + sizeof(payload), NULL, &audio_parsed, NULL) != S3P_PARSE_OK) return 4;
    if (audio_parsed.frame_seq != 0u || audio_parsed.frag_count != 1u || audio_parsed.frag_len != sizeof(payload)) return 5;
    if (audio_parsed.payload[0] != 'A' || audio_parsed.payload[7] != 'H') return 6;

    // SV-03：END + summary（独立 FNV-1a）
    const uint32_t summary = s3p_fnv1a32(payload, sizeof(payload));
    (void)s3p_encode_end(buf, 1u, 3u, 2880u, summary);
    s3p_end_t end;
    if (s3p_parse(buf, S3P_END_LEN, NULL, NULL, &end) != S3P_PARSE_OK) return 7;
    if (end.total_frames != 3u || end.total_samples != 2880u || end.summary != summary) return 8;

    // SV-05：非法版本拒绝
    buf[1] = 2u;
    if (s3p_parse(buf, S3P_END_LEN, NULL, NULL, &end) != S3P_PARSE_BAD_VERSION) return 9;

    return 0;
}

/* M01 采音通路证据：3 秒窗口的 min/max/avg + 过零率（ZCR）。
 * ZCR 用途（2026-09-23 时基排查）：正常语音 ZCR 约 1500~2500/s；
 * 若原始 PCM ZCR 远低于此（如 ~800），则设备采音时基偏低（疑似 2 倍），
 * 与主机侧解码后 ZCR 对比可二分定位 codec/I2S 侧 vs Opus 链侧。 */
static void audio_path_evidence(void) {
    static int16_t samples[960]; /* 60ms @16kHz 单声道 */
    for (int sec = 1; sec <= 3; sec += 1) {
        int32_t sum = 0;
        int16_t mn = INT16_MAX;
        int16_t mx = INT16_MIN;
        uint32_t total = 0;
        uint32_t zc = 0;
        int16_t prev = 0;
        int have_prev = 0;
        for (int frame = 0; frame < 16; frame += 1) {
            size_t got = 0;
            if (audio_capture_read(samples, 960, &got) != ESP_OK) {
                ESP_LOGE(TAG, "audio read failed @ %ds", sec);
                return;
            }
            for (size_t i = 0; i < got; i += 1) {
                sum += samples[i];
                if (samples[i] < mn) mn = samples[i];
                if (samples[i] > mx) mx = samples[i];
                if (have_prev && ((prev < 0) != (samples[i] < 0))) zc += 1;
                prev = samples[i];
                have_prev = 1;
            }
            total += (uint32_t)got;
        }
        const int avg = total > 0 ? (int)(sum / (int32_t)total) : 0;
        const uint32_t zcr = total > 1 ? (zc * 16000u) / total : 0;
        ESP_LOGI(TAG, "audio %ds: samples=%lu min=%d max=%d avg=%d zcr=%lu/s",
                 sec, (unsigned long)total, (int)mn, (int)mx, avg,
                 (unsigned long)zcr);
    }
}

/* S4.4：进入「等待/错误」各响一次；录音中延后；过期快照不响（设计 §310 / M4 不变量）。 */
static void alert_task(void *arg) {
    (void)arg;
    uint8_t prev = (uint8_t)S3P_STATUS_UNKNOWN;
    bool pending = false;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
        const bool valid = ble_gatt_host_state_valid();
        const uint8_t flags = valid ? ble_gatt_host_flags() : 0u;
        const uint8_t st = valid ? ble_gatt_host_status() : (uint8_t)S3P_STATUS_UNKNOWN;
        const bool expired = (flags & S3P_FLAG_EXPIRED) != 0u;
        const bool alert =
            valid && !expired && (st == S3P_STATUS_WAITING || st == S3P_STATUS_ERROR);
        const bool was_alert = (prev == S3P_STATUS_WAITING || prev == S3P_STATUS_ERROR);
        if (alert && !was_alert) {
            if (audio_session_active()) {
                pending = true;
            } else {
                audio_capture_beep();
            }
        }
        if (pending && !audio_session_active()) {
            if (alert) {
                audio_capture_beep();
            }
            pending = false;
        }
        prev = st;
    }
}

void app_main(void) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "idf version      : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "chip model       : %d (9 = ESP32-S3)", (int)chip.model);
    ESP_LOGI(TAG, "flash size bytes : %lu", (unsigned long)flash_size);

    const int proto_fail = proto_selftest();
    if (proto_fail == 0) {
        ESP_LOGI(TAG, "s3p_proto selftest: PASS (SV-01/02/03/05)");
    } else {
        ESP_LOGE(TAG, "s3p_proto selftest: FAIL at step %d", proto_fail);
    }

    if (audio_capture_init() == ESP_OK) {
        audio_capture_log_clocks();
        audio_capture_probe_rate(1000);
        audio_path_evidence();

    } else {
        ESP_LOGE(TAG, "audio capture init FAILED");
    }

    if (audio_session_init() == ESP_OK) {
        ESP_LOGI(TAG, "opus 会话层就绪");
    } else {
        ESP_LOGE(TAG, "audio session init FAILED");
    }

    if (button_init() == ESP_OK) {
        button_set_handlers(on_button_press, on_button_release);
        ESP_LOGI(TAG, "按键触发就绪：按住 BtnA 录音 / 松开结束");
    } else {
        ESP_LOGE(TAG, "button init FAILED");
    }

    if (ble_gatt_init() == ESP_OK) {
        ble_gatt_set_ctrl_cb(audio_session_on_ctrl);
        ble_gatt_set_disconnect_cb(audio_session_abort);
        ESP_LOGI(TAG, "ble gatt init OK（等待主机连接配对）");
    } else {
        ESP_LOGE(TAG, "ble gatt init FAILED");
    }

    if (display_init() == ESP_OK) {
        ESP_LOGI(TAG, "display init OK（自检图案后进入状态轮询）");
    } else {
        ESP_LOGE(TAG, "display init FAILED");
    }

    /* S4.4 自检：开机响一声（后续按等待/错误事件触发，每事件一次） */
    if (audio_capture_beep() != ESP_OK) {
        ESP_LOGW(TAG, "提示音自检失败（I2S TX / es8311）");
    }
    if (xTaskCreate(alert_task, "alert", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGW(TAG, "提示音任务创建失败");
    }
    ESP_LOGI(TAG, "free heap        : %lu", (unsigned long)esp_get_free_heap_size());

    /* 诊断心跳：USB-Serial-JTAG 在正常 boot 时重新枚举，启动日志在主机接入前丢失；
     * 周期心跳保证监控接入后必能观察到设备存活与子系统状态。 */
    int hb_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        hb_count += 1;
        if (hb_count % 3 == 0) {
            audio_capture_log_clocks(); /* 周期输出：不依赖启动瞬间抓日志 */
        }
        ESP_LOGI(TAG, "hb: uptime=%lus heap=%lu audio_ready=%d ble_conn=%d",
                 (unsigned long)(esp_timer_get_time() / 1000000),
                 (unsigned long)esp_get_free_heap_size(),
                 audio_capture_ready(), ble_gatt_connected());
    }
}
