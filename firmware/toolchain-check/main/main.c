// StickS3 x OpenCode — 固件工具链自检（非产品代码）
//
// 目的：证明 ESP-IDF 5.5.x + esp32s3 工具链可编译并识别目标芯片。
// 不实现任何产品行为，不属于 M1-M6 任何交付物。

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "toolchain_check";

void app_main(void) {
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "idf version      : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "chip model       : %d (9 = ESP32-S3)", (int)chip.model);
    ESP_LOGI(TAG, "chip cores       : %d", chip.cores);
    ESP_LOGI(TAG, "chip revision    : %d", (int)chip.revision);
    ESP_LOGI(TAG, "flash size bytes : %lu", (unsigned long)flash_size);
    ESP_LOGI(TAG, "free heap        : %lu", (unsigned long)esp_get_free_heap_size());
}
