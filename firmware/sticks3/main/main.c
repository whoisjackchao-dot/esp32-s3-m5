// StickS3 固件主入口（M2 骨架）
// 当前：芯片自检 + s3p_proto 自检（黄金向量 SV-01/SV-02/SV-03/SV-05 启动期自校验）。
// 采音（M01）/BLE（M02）/屏幕（M03）按故事增量进入。
#include "s3p_proto.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "sticks3";

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
    ESP_LOGI(TAG, "free heap        : %lu", (unsigned long)esp_get_free_heap_size());
}
