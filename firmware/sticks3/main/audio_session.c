#include "audio_session.h"

#include <string.h>

#include "audio_capture.h"
#include "ble_gatt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "opus.h"
#include "s3p_proto.h"

static const char *TAG = "audio_session";

/* M2 测试会话：3 秒 = 50 帧 = 48000 样本（≥ TX-18 的 4800 有效样本线） */
#define SESSION_FRAMES 50u

static OpusEncoder *s_enc;
static TaskHandle_t s_task;
static uint32_t s_rec_id;
static volatile bool s_active;

static int16_t s_pcm[S3P_SAMPLES_PER_FRAME];
static uint8_t s_opus[512];
static uint8_t s_pkt[14 + 240];

/* FNV-1a 连续状态更新；与 s3p_fnv1a32 同算法（跨帧累计载荷字节） */
static uint32_t fnv_update(uint32_t h, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i += 1) {
        h ^= d[i];
        h *= 0x01000193u;
    }
    return h;
}

static void send_error_event(uint32_t rec_id, uint16_t code) {
    uint8_t ev[8] = {S3P_PKT_ERROR, (uint8_t)S3P_PROTOCOL_VERSION, 0};
    s3p_put_u32(ev + 2, rec_id);
    s3p_put_u16(ev + 6, code);
    (void)ble_gatt_notify_event(ev, sizeof(ev));
}

static void send_device_cancel(uint32_t rec_id) {
    uint8_t ev[6] = {S3P_PKT_DEVICE_CANCEL, (uint8_t)S3P_PROTOCOL_VERSION, 0};
    s3p_put_u32(ev + 2, rec_id);
    (void)ble_gatt_notify_event(ev, sizeof(ev));
}

static int64_t now_ms(void) {
    return esp_timer_get_time() / 1000;
}

static void run_session(void) {
    if (!ble_gatt_connected() || !ble_gatt_audio_subscribed()) {
        ESP_LOGW(TAG, "会话取消：未连接或 Audio Notify 未订阅");
        return;
    }
    const uint16_t mtu = ble_gatt_mtu();
    if (mtu < S3P_MIN_MTU) {
        ESP_LOGE(TAG, "MTU %u < %u，显式拒绝录音（冻结 §2）", mtu, S3P_MIN_MTU);
        send_error_event(s_rec_id + 1, S3P_ERR_PROTOCOL);
        return;
    }

    const uint32_t rec_id = ++s_rec_id;
    const int64_t t0 = now_ms();
    ESP_LOGI(TAG, "BEGIN recID=%u frames=%u 16kHz/60ms mtu=%u", rec_id,
             SESSION_FRAMES, mtu);

    uint8_t begin_buf[S3P_BEGIN_LEN];
    (void)s3p_encode_begin(begin_buf, rec_id, SESSION_FRAMES);
    if (ble_gatt_notify_audio(begin_buf, sizeof(begin_buf)) != ESP_OK) {
        ESP_LOGE(TAG, "BEGIN 通知失败，会话取消");
        send_device_cancel(rec_id);
        return;
    }

    uint32_t summary = 0x811c9dc5u;
    uint32_t total_samples = 0;
    uint32_t total_opus_bytes = 0;
    uint32_t notify_fail = 0;
    uint16_t max_frag = (uint16_t)(mtu - S3P_AUDIO_HDR_LEN);
    if (max_frag > sizeof(s_pkt) - S3P_AUDIO_HDR_LEN) {
        max_frag = (uint16_t)(sizeof(s_pkt) - S3P_AUDIO_HDR_LEN);
    }

    for (uint32_t seq = 0; seq < SESSION_FRAMES; seq += 1) {
        size_t got = 0;
        if (audio_capture_read(s_pcm, S3P_SAMPLES_PER_FRAME, &got) != ESP_OK ||
            got != S3P_SAMPLES_PER_FRAME) {
            ESP_LOGE(TAG, "采音失败 seq=%lu", (unsigned long)seq);
            send_error_event(rec_id, S3P_ERR_MIC_INIT);
            send_device_cancel(rec_id);
            return;
        }
        const int n = opus_encode(s_enc, s_pcm, S3P_SAMPLES_PER_FRAME, s_opus,
                                  sizeof(s_opus));
        if (n <= 0) {
            ESP_LOGE(TAG, "opus encode 失败 seq=%lu rc=%d",
                     (unsigned long)seq, n);
            send_error_event(rec_id, S3P_ERR_OPUS);
            send_device_cancel(rec_id);
            return;
        }
        summary = fnv_update(summary, s_opus, (size_t)n);
        total_samples += (uint32_t)got;
        total_opus_bytes += (uint32_t)n;

        uint8_t frag_count = (uint8_t)((n + max_frag - 1) / max_frag);
        for (uint8_t f = 0; f < frag_count; f += 1) {
            const uint16_t off = (uint16_t)(f * max_frag);
            const uint16_t flen =
                (n - off) > max_frag ? max_frag : (uint16_t)(n - off);
            (void)s3p_encode_audio_header(s_pkt, rec_id, seq, f, frag_count,
                                          flen);
            memcpy(s_pkt + S3P_AUDIO_HDR_LEN, s_opus + off, flen);
            if (ble_gatt_notify_audio(s_pkt, S3P_AUDIO_HDR_LEN + flen) !=
                ESP_OK) {
                notify_fail += 1;
            }
        }
        if (notify_fail > 0) {
            ESP_LOGE(TAG, "通知失败 %lu 次，会话取消", (unsigned long)notify_fail);
            send_device_cancel(rec_id);
            return;
        }
    }

    uint8_t end_buf[S3P_END_LEN];
    (void)s3p_encode_end(end_buf, rec_id, SESSION_FRAMES, total_samples,
                         summary);
    if (ble_gatt_notify_audio(end_buf, sizeof(end_buf)) != ESP_OK) {
        ESP_LOGE(TAG, "END 通知失败");
        return;
    }
    ESP_LOGI(TAG,
             "END recID=%u frames=%lu samples=%lu opus=%luB (%luB/帧) "
             "summary=0x%08lx 耗时=%lldms",
             rec_id, (unsigned long)SESSION_FRAMES, (unsigned long)total_samples,
             (unsigned long)total_opus_bytes,
             (unsigned long)(total_opus_bytes / SESSION_FRAMES),
             (unsigned long)summary, (long long)(now_ms() - t0));
}

static void session_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_active = true;
        run_session();
        s_active = false;
    }
}

bool audio_session_active(void) {
    return s_active;
}

esp_err_t audio_session_init(void) {
    int err = OPUS_OK;
    s_enc = opus_encoder_create(S3P_SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
                                &err);
    if (s_enc == NULL || err != OPUS_OK) {
        ESP_LOGE(TAG, "opus encoder create: %d", err);
        return ESP_FAIL;
    }
    opus_encoder_ctl(s_enc, OPUS_SET_BITRATE(24000)); /* 冻结 §2 量级：20kbit/s */
    opus_encoder_ctl(s_enc, OPUS_SET_COMPLEXITY(4));
    opus_encoder_ctl(s_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(s_enc, OPUS_SET_LSB_DEPTH(16));

    /* opus_encode（SILK 路径）栈用量远超预期：12KB 栈溢出曾破坏内核链表
     * （表现为 idle task_wdt 钩子 LoadProhibited），32KB 为实测安全值。 */
    if (xTaskCreate(session_task, "audio_sess", 32768, NULL, 5, &s_task) !=
        pdPASS) {
        ESP_LOGE(TAG, "session task create failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "opus 编码器就绪（16kHz 单声道 VOIP 24kbps 60ms）");
    return ESP_OK;
}

void audio_session_on_ctrl(uint8_t msg_type, const uint8_t *data, uint16_t len) {
    (void)data;
    (void)len;
    if (msg_type == 0x10 && s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}
